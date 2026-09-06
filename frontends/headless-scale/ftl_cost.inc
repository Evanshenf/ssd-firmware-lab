/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

/* Opt-in cost diagnosis on the existing real command journey. The forwarding
 * observer never changes byte ranges, results, barriers or descriptor ownership.
 * It is detached before recovery/close. Counts are substrate API calls, not
 * syscall counts (POSIX can retry); timings include this observer, not a paired
 * throughput-acceptance benchmark. No production ABI or observer is added. */
struct cost_io {
    struct fnv1_io saved;
    uint64_t reads, writes, syncs, read_bytes, write_bytes;
};

static enum fwlab_nfc_api_result cost_read(
    void *opaque, uint64_t offset, void *buffer, size_t bytes)
{
    struct cost_io *c = opaque;
    ++c->reads;
    c->read_bytes += bytes;
    return c->saved.read(c->saved.context, offset, buffer, bytes);
}

static enum fwlab_nfc_api_result cost_write(
    void *opaque, uint64_t offset, const void *buffer, size_t bytes)
{
    struct cost_io *c = opaque;
    ++c->writes;
    c->write_bytes += bytes;
    return c->saved.write(c->saved.context, offset, buffer, bytes);
}

static enum fwlab_nfc_api_result cost_sync(void *opaque)
{
    struct cost_io *c = opaque;
    ++c->syncs;
    return c->saved.sync(c->saved.context);
}

static enum fwlab_nfc_api_result cost_size(void *opaque, uint64_t *bytes)
{
    struct cost_io *c = opaque;
    return c->saved.size(c->saved.context, bytes);
}

static enum fwlab_nfc_api_result cost_resize(void *opaque, uint64_t bytes)
{
    struct cost_io *c = opaque;
    return c->saved.resize(c->saved.context, bytes);
}

static enum fwlab_nfc_api_result cost_close(void *opaque)
{
    struct cost_io *c = opaque;
    return c->saved.close(c->saved.context);
}

static uint64_t cost_nanoseconds(void)
{
    struct timespec now;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static void cost_reset(struct cost_io *c)
{
    c->reads = c->writes = c->syncs = c->read_bytes = c->write_bytes = 0;
}

static void cost_report(struct fixture *f, const char *phase,
                        const struct cost_io *c, uint64_t host_bytes,
                        uint64_t started, uint64_t sequence,
                        const struct fwlab_ftl_scale_status *before)
{
    uint64_t elapsed = cost_nanoseconds() - started;
    struct fwlab_ftl_scale_status after;
    CHECK(scale_storage_query(f->runtime, &after) == FWLAB_SPINE_V0_OK);
    printf("SCALE_COST|phase=%s|host_bytes=%llu|elapsed_ns=%llu|read_calls=%llu|write_calls=%llu|sync_calls=%llu|read_bytes=%llu|write_bytes=%llu|media_commits=%llu|nfc_children=%llu|gc=%llu|cp=%llu|medium=%s|media_version=%u|instrumented=1|paired_acceptance=0\n",
           phase, (unsigned long long)host_bytes, (unsigned long long)elapsed,
           (unsigned long long)c->reads, (unsigned long long)c->writes,
           (unsigned long long)c->syncs, (unsigned long long)c->read_bytes,
           (unsigned long long)c->write_bytes,
           (unsigned long long)(media_sequence(f) - sequence),
           (unsigned long long)(after.nfc_children - before->nfc_children),
           (unsigned long long)(after.garbage_collections - before->garbage_collections),
           (unsigned long long)(after.checkpoints - before->checkpoints),
           medium_name(f), f->use_media_v2 ? 2u : 1u);
}

/* The private byte substrates have identical callback signatures, not
 * interchangeable structure types. Copy members; never cast one engine's IO
 * object to another type. This forwarding logic exists only in this entry. */
#define COST_SAVE_ATTACH(m) do { \
    c.saved.context = (m)->io.context; c.saved.read = (m)->io.read; \
    c.saved.write = (m)->io.write; c.saved.sync = (m)->io.sync; \
    c.saved.resize = (m)->io.resize; c.saved.size = (m)->io.size; \
    c.saved.close = (m)->io.close; \
    (m)->io.context = &c; (m)->io.read = cost_read; (m)->io.write = cost_write; \
    (m)->io.sync = cost_sync; (m)->io.resize = cost_resize; \
    (m)->io.size = cost_size; (m)->io.close = cost_close; \
} while (0)
#define COST_RESTORE(m) do { \
    (m)->io.context = c.saved.context; (m)->io.read = c.saved.read; \
    (m)->io.write = c.saved.write; (m)->io.sync = c.saved.sync; \
    (m)->io.resize = c.saved.resize; (m)->io.size = c.saved.size; \
    (m)->io.close = c.saved.close; \
} while (0)

static void cost_journey(struct fixture *f)
{
    struct cost_io c = {0};
    struct fwlab_ftl_scale_status before;
    uint8_t bytes[8192], output[8192];
    uint64_t start, sequence;
    uint32_t phase, i;
    const uint32_t commands = 4096; /* 32 MiB, includes journal recycling. */
    const char *names[] = {"aligned-write-8k", "aligned-read-8k",
                          "rmw-write-512", "sequential-write-32m",
                          "sequential-read-32m"};

    fill_pattern(bytes, 0, 11); /* Reusable nonzero payload, outside timing. */
    command(f, 1, 0, 16, bytes, NULL, 0, 0); /* Open the DATA head. */
    wait_idle(f);
    if (f->use_media_v2) COST_SAVE_ATTACH(f->media_v2);
    else COST_SAVE_ATTACH(f->media);
    for (phase = 0; phase < 5; ++phase) {
        uint64_t host_bytes = phase < 2 ? 8192u :
                              (phase == 2 ? 512u : (uint64_t)commands * 8192u);
        stage_start(f, names[phase], host_bytes);
        CHECK(scale_storage_query(f->runtime, &before) == FWLAB_SPINE_V0_OK);
        sequence = media_sequence(f);
        cost_reset(&c);
        start = cost_nanoseconds();
        if (phase == 0)
            command(f, 1, 16, 16, bytes, NULL, 0, 0);
        else if (phase == 1) {
            command(f, 2, 16, 16, NULL, output, 0, 0);
            CHECK(memcmp(bytes, output, sizeof(bytes)) == 0);
        } else if (phase == 2)
            command(f, 1, 1, 1, bytes, NULL, 1, 0);
        else {
            for (i = 0; i < commands; ++i) {
                command(f, phase == 3 ? 1 : 2, (uint64_t)i * 16u, 16,
                        phase == 3 ? bytes : NULL,
                        phase == 3 ? NULL : output, 0, 0);
                if (phase == 4)
                    CHECK(memcmp(bytes, output, sizeof(bytes)) == 0);
                f->stage_done += 8192u;
            }
        }
        wait_idle(f);
        cost_report(f, names[phase], &c, host_bytes, start, sequence, &before);
    }
    if (f->use_media_v2) COST_RESTORE(f->media_v2);
    else COST_RESTORE(f->media);
    /* SELF was required for every write; reopen before any later Flush can
     * mask a completion bug. Existing format/recovery, no fake media. */
    reopen(f);
    for (i = 0; i < commands; ++i) {
        command(f, 2, (uint64_t)i * 16u, 16, NULL, output, 0, 0);
        CHECK(memcmp(bytes, output, sizeof(bytes)) == 0);
    }
    puts("SCALE_COST_PASS|path=Linux-profile-lifecycle-FTL-NFC-compact-NAND|SELF=1|reopen_readback=1|no_disk_durability_claim=1");
}
#undef COST_SAVE_ATTACH
#undef COST_RESTORE
