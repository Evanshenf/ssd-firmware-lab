/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* D213 B: reuse only the existing adjacent buffer/Block helpers. The retained
 * 64-MiB test entry is never called. Actual NAND remains the channel assembly. */
#define main(...) retained_parent_fixture_entry(__VA_ARGS__)
#include "test_parent.c"
#undef main
#include "channel_volume.h"
#include "fwlab/private/nfc_channel_v2.h"
#include <sys/resource.h>

#define MH_LBAS 2048u
#define MH_FILES 6u
#ifndef MH_MEDIA_PREFIX
#define MH_MEDIA_PREFIX "fwlab-d213-parent"
#endif
#ifndef MH_LUNS
#define MH_LUNS 2u
#endif
#ifndef MH_PLANES
#define MH_PLANES 1u
#endif
#ifndef MH_BLOCKS
#define MH_BLOCKS 8u
#endif
#ifndef MH_FTL_ARENA_SIZE
#define MH_FTL_ARENA_SIZE fwlab_ftl_scale_multihead_v3_arena_size
#endif
#ifndef MH_FTL_INIT
#define MH_FTL_INIT fwlab_ftl_scale_init_multihead_v3
#endif
#ifndef MH_HUB_INIT
#define MH_HUB_INIT fwlab_nfc_channel_v2_init
#endif

struct mh_run {
    struct fwlab_nfc_operation_token token;
    uint32_t first_ppa, first_lpn, pages, programmed, completed_at;
    uint8_t resolved;
};
struct mh_observation {
    struct mh_run run[4];
    uint32_t accepted, resolved, callbacks, map_callbacks, map_admissions;
    uint64_t base_frontier, host_sequence, map_record_sequence;
    uint8_t enabled;
};
struct mh_credit {
    struct fwlab_nfc_page_v2_provider actual;
    struct fwlab_nfc_channel_v2 *hub;
    struct mh_observation *observation;
    struct fwlab_nfc_operation_token active[4];
    uint8_t used[4];
    uint32_t limit, outstanding, peak, local_bp, ack_bp;
    uint64_t accepted_data, taken_data, retry_uid, last_uid;
    struct fwlab_nfc_geometry geometry;
};
struct mh_fixture {
    struct fixture base;
    struct fwlab_nand_channel_volume_config config;
    struct fwlab_nand_channel_volume *volume;
    struct fwlab_nand_channel_v2 assembly;
    struct fwlab_nfc_channel_v2 *hub;
    struct mh_credit credit;
    struct mh_observation observation;
    struct stat directory_identity, files[MH_FILES];
};
static struct mh_fixture *mh_current;

static uint16_t mh_get16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8)); }
static uint32_t mh_get32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t mh_get64(const uint8_t *p)
{ return mh_get32(p) | (uint64_t)mh_get32(p + 4) << 32; }
static uint32_t mh_linear(const struct fwlab_nfc_geometry *g, const struct fwlab_nfc_ppa *p)
{
    return (((((uint32_t)p->channel * g->luns_per_channel + p->lun) * g->planes_per_lun +
        p->plane) * g->blocks_per_plane + p->block) * g->pages_per_block) + p->page;
}
static bool mh_data(const struct fwlab_nfc_page_v2_request *r)
{ return r->kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP && r->oob && r->oob_bytes >= 128 && mh_get16(r->oob + 6) == 5; }
static bool mh_map(const struct fwlab_nfc_page_v2_request *r)
{
    return r->kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP && r->main && r->main_bytes == 4096 &&
        r->oob && r->oob_bytes == 128 && mh_get16(r->oob + 6) == 4 && r->main[94] == SF_MAP_WINDOW;
}
static struct fwlab_nfc_channel_v2_stats mh_stats(struct mh_fixture *m)
{
    struct fwlab_nfc_channel_v2_stats s;
    CHECK(fwlab_nfc_channel_v2_snapshot(m->hub, &s) == FWLAB_NFC_API_OK &&
        !s.quarantined && !s.poisoned && !s.counters_saturated);
    return s;
}

/* The smaller-credit binding changes only pre-admission BP. Every accepted
 * request, payload snapshot, physical operation, result and ACK belongs to the
 * actual hub. In particular take_result is not treated as a hub ACK. */
static struct fwlab_nfc_submit_result mh_submit(void *opaque,
    const struct fwlab_nfc_page_v2_request *r)
{
    struct mh_credit *c = opaque;
    struct mh_observation *o = c->observation;
    bool data = mh_data(r), duplicate = false;
    struct fwlab_nfc_submit_result result;
    for (unsigned i = 0; i < 4; ++i)
        duplicate |= c->used[i] && !memcmp(&c->active[i], &r->operation, sizeof(r->operation));
    if (data && !duplicate) {
        if (c->retry_uid) CHECK(c->retry_uid == r->operation.operation_uid);
        if (c->outstanding >= c->limit) {
            ++c->local_bp; c->retry_uid = r->operation.operation_uid;
            result = (struct fwlab_nfc_submit_result){0};
            result.disposition = FWLAB_NFC_BACKPRESSURE;
            return result;
        }
    }
    if (o->enabled && mh_map(r)) CHECK(o->accepted == 4 && o->resolved == 4);
    result = c->actual.ops->try_submit(c->actual.context, r);
    if (result.disposition == FWLAB_NFC_BACKPRESSURE) {
        struct fwlab_nfc_channel_v2_stats s;
        CHECK(fwlab_nfc_channel_v2_snapshot(c->hub, &s) == FWLAB_NFC_API_OK);
        c->ack_bp += s.retirement_pending != 0;
        if (data && !duplicate) c->retry_uid = r->operation.operation_uid;
    }
    if (result.disposition != FWLAB_NFC_ACCEPTED || duplicate) return result;
    if (o->enabled && mh_map(r)) {
        struct fwlab_nfc_channel_v2_stats s;
        CHECK(fwlab_nfc_channel_v2_snapshot(c->hub, &s) == FWLAB_NFC_API_OK && !s.retirement_pending);
        ++o->map_admissions;
    }
    if (data) {
        unsigned index = 0;
        while (index < 4 && c->used[index]) ++index;
        CHECK(index < 4 && c->outstanding < c->limit && r->operation.operation_uid > c->last_uid);
        c->active[index] = r->operation; c->used[index] = 1;
        c->last_uid = r->operation.operation_uid; c->retry_uid = 0;
        ++c->outstanding; ++c->accepted_data;
        if (c->outstanding > c->peak) c->peak = c->outstanding;
        if (o->enabled) {
            struct mh_run *run;
            CHECK(o->accepted < 4);
            run = &o->run[o->accepted++]; run->token = r->operation;
            run->first_ppa = mh_linear(&c->geometry, &r->first);
            run->first_lpn = mh_get32(r->oob + 64); run->pages = r->page_count;
        }
    }
    return result;
}
static enum fwlab_nfc_api_result mh_take(void *opaque,
    const struct fwlab_nfc_operation_token *token, struct fwlab_nfc_page_v2_result *result,
    const struct fwlab_nfc_page_v2_output *output)
{
    struct mh_credit *c = opaque;
    enum fwlab_nfc_api_result status = c->actual.ops->take_result(c->actual.context, token, result, output);
    if (status != FWLAB_NFC_API_OK) return status;
    for (unsigned i = 0; i < 4; ++i) {
        if (!c->used[i] || memcmp(&c->active[i], token, sizeof(*token))) continue;
        CHECK(c->outstanding); --c->outstanding; ++c->taken_data; c->used[i] = 0;
        if (c->observation->enabled) {
            unsigned n = 0;
            while (n < c->observation->accepted && memcmp(&c->observation->run[n].token, token, sizeof(*token))) ++n;
            CHECK(n < c->observation->accepted && !c->observation->run[n].resolved &&
                result->effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE);
            c->observation->run[n].resolved = 1; ++c->observation->resolved;
        }
        break;
    }
    return status;
}
static enum fwlab_nfc_api_result mh_step(void *opaque, uint32_t budget,
                                        struct fwlab_nfc_page_v2_step_result *result)
{ struct mh_credit *c = opaque; return c->actual.ops->step(c->actual.context, budget, result); }
static enum fwlab_nfc_api_result mh_cancel(void *opaque, const struct fwlab_nfc_operation_token *token)
{ struct mh_credit *c = opaque; return c->actual.ops->cancel(c->actual.context, token); }
static enum fwlab_nfc_api_result mh_reset(void *opaque, uint64_t nonce, uint32_t epoch)
{ struct mh_credit *c = opaque; return c->actual.ops->reset_begin(c->actual.context, nonce, epoch); }
static enum fwlab_nfc_api_result mh_quiet(void *opaque, uint64_t nonce, uint32_t epoch, bool *quiet)
{ struct mh_credit *c = opaque; return c->actual.ops->quiescent(c->actual.context, nonce, epoch, quiet); }
static const struct fwlab_nfc_page_v2_provider_ops mh_ops = {
    FWLAB_NFC_PAGE_V2_VERSION, sizeof(struct fwlab_nfc_page_v2_provider_ops), 0,
    mh_submit, mh_cancel, mh_step, mh_take, mh_reset, mh_quiet
};

enum fwlab_nfc_api_result __real_fwlab_file_nand_v2_program_pages(
    struct fwlab_file_nand_v2 *, const struct fwlab_nfc_ppa *, uint32_t,
    const uint8_t *, size_t, const uint8_t *, size_t, struct fwlab_nand_media_result *, size_t);
enum fwlab_nfc_api_result __wrap_fwlab_file_nand_v2_program_pages(
    struct fwlab_file_nand_v2 *media, const struct fwlab_nfc_ppa *first, uint32_t count,
    const uint8_t *main, size_t main_bytes, const uint8_t *oob, size_t oob_bytes,
    struct fwlab_nand_media_result *results, size_t capacity)
{
    enum fwlab_nfc_api_result status = __real_fwlab_file_nand_v2_program_pages(media, first, count,
        main, main_bytes, oob, oob_bytes, results, capacity);
    if (!mh_current || !mh_current->observation.enabled) return status;
    struct mh_observation *o = &mh_current->observation;
    CHECK(status == FWLAB_NFC_API_OK && count == 1 && results[0].physical_outcome == FWLAB_NFC_PHYS_APPLIED &&
        results[0].integrity == FWLAB_NFC_INTEGRITY_COMPLETE);
    if (mh_get16(oob + 6) == 5) {
        uint32_t ppa = mh_get32(oob + 32), i = 0;
        while (i < o->accepted && (ppa < o->run[i].first_ppa || ppa >= o->run[i].first_ppa + o->run[i].pages)) ++i;
        CHECK(i < o->accepted && ppa == o->run[i].first_ppa + o->run[i].programmed);
        ++o->callbacks;
        if (++o->run[i].programmed == o->run[i].pages) o->run[i].completed_at = o->callbacks;
    } else if (mh_get16(oob + 6) == 4 && main[94] == SF_MAP_WINDOW) {
        uint32_t run = o->map_callbacks / 2u;
        CHECK(o->accepted == 4 && o->resolved == 4 && o->callbacks == 13 && run < 4);
        CHECK(mh_current->base.ftl->parent.status.state == FWLAB_BLOCK_V0_STATE_ACCEPTED &&
            mh_get32(main + 128) == o->run[run].first_lpn && mh_get16(main + 92) == o->run[run].pages);
        if (!(o->map_callbacks % 2u)) {
            if (run) CHECK(mh_get64(main + 16) == o->map_record_sequence + 1u);
            o->map_record_sequence = mh_get64(main + 16);
        } else CHECK(mh_get64(main + 16) == o->map_record_sequence);
        CHECK(mh_get64(main + 48) == (run == 3 ? o->host_sequence : o->base_frontier));
        ++o->map_callbacks;
    }
    return status;
}

static const char *mh_name(unsigned i)
{
    return i < 4 ? fwlab_nand_channel_volume_shard_name(i) :
        i == 4 ? FWLAB_NAND_CHANNEL_VOLUME_MANIFEST : FWLAB_NAND_CHANNEL_VOLUME_LOCK;
}
static void *mh_arena(size_t alignment, size_t bytes)
{ void *p; CHECK(bytes && alignment && bytes % alignment == 0); p = aligned_alloc(alignment, bytes); CHECK(p); return p; }
static struct mh_fixture *mh_create(unsigned credits)
{
    struct mh_fixture *m = calloc(1, sizeof(*m));
    struct fixture *f = &m->base;
    struct fwlab_file_nand_v2_config child = {0};
    const char *root = getenv("FWLAB_TEST_MEDIA_DIR");
    struct statfs fs;
    int fd, n;
    uint64_t bytes;
    CHECK(m && geteuid() == 1000 && root && root[0] == '/');
    m->config.version = FWLAB_NAND_CHANNEL_VOLUME_VERSION; m->config.size = sizeof(m->config);
    m->config.geometry = (struct fwlab_nfc_geometry){
        .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(struct fwlab_nfc_geometry),
        .channels = 4, .luns_per_channel = MH_LUNS, .planes_per_lun = MH_PLANES, .blocks_per_plane = MH_BLOCKS,
        .pages_per_block = 64, .plane_parallelism_per_lun = MH_PLANES, .main_bytes_per_page = 4096,
        .oob_bytes_per_page = 128, .max_programs_per_erase = 1, .program_order = FWLAB_NFC_PROGRAM_ASCENDING
    };
    memcpy(m->config.media_uuid, "D213-PARENT-001", 16);
    for (unsigned i = 0; i < 4; ++i) {
        memcpy(m->config.child_uuid[i], "D213-P-CHILD0001", 16);
        m->config.child_uuid[i][14] = (uint8_t)('1' + i);
    }
    child.geometry = m->config.geometry; child.geometry.channels = 1;
    memcpy(child.media_uuid, m->config.child_uuid[0], 16);
    bytes = fwlab_file_nand_v2_image_bytes(&child) * 4;
    fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(fd >= 0 && fstatfs(fd, &fs) == 0 && fs.f_type == TMPFS_MAGIC && fs.f_bsize > 0 &&
        (uint64_t)fs.f_blocks * (uint64_t)fs.f_bsize <= (UINT64_C(1) << 30) &&
        bytes && bytes <= (UINT64_C(32) << 20) &&
        (uint64_t)fs.f_bavail * (uint64_t)fs.f_bsize >= bytes + (UINT64_C(64) << 20));
    CHECK(close(fd) == 0);
    n = snprintf(f->directory, sizeof(f->directory), "%s/" MH_MEDIA_PREFIX ".XXXXXX", root);
    CHECK(n > 0 && (size_t)n < sizeof(f->directory) && mkdtemp(f->directory));
    f->directory_fd = open(f->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(f->directory_fd >= 0 && fstat(f->directory_fd, &m->directory_identity) == 0);
    f->buffer.allocation = malloc(BUFFER_BYTES + 2 * GUARD_BYTES); CHECK(f->buffer.allocation);
    f->buffer.bytes = f->buffer.allocation + GUARD_BYTES;
    memset(f->buffer.allocation, 0xa5, GUARD_BYTES); memset(f->buffer.bytes + BUFFER_BYTES, 0x5a, GUARD_BYTES);
    f->use_media_v2 = f->window_v2 = 1; m->credit.limit = credits;
    printf("MULTIHEAD_PARENT_BEGIN|namespace_bytes=1048576|main_bytes=%llu|physical_image_bytes=%llu|DATA_credits=%u|directory=%s|no_disk_fallback=1\n",
        (unsigned long long)((uint64_t)4 * MH_LUNS * MH_PLANES * MH_BLOCKS * 64 * 4096),
        (unsigned long long)bytes, credits, f->directory);
    fflush(stdout); return m;
}
static void mh_open(struct mh_fixture *m, bool format)
{
    struct fixture *f = &m->base;
    struct fwlab_ftl_scale_extended_config config = {0};
    struct fwlab_nfc_page_v2_lab_mutation_config timing = {0};
    struct fwlab_nfc_page_v2_lab_config *read = &timing.read;
    struct fwlab_controller_buffer_port_v0 buffer = buffer_port(f);
    struct fwlab_nfc_page_v2_provider provider = { &mh_ops, &m->credit };
    size_t vb = fwlab_nand_channel_volume_arena_size(), hb = fwlab_nfc_channel_v2_arena_size(), fb;
    uint32_t i, credit_limit = m->credit.limit;
    f->buffer.closed = 0; ++f->incarnation;
    f->media_arena = mh_arena(fwlab_nand_channel_volume_arena_alignment(), vb);
    CHECK((format ? fwlab_nand_channel_volume_posix_format(f->media_arena, vb, f->directory_fd, &m->config, &m->volume) :
        fwlab_nand_channel_volume_posix_restart(f->media_arena, vb, f->directory_fd, m->config.media_uuid, &m->volume)) == FWLAB_NFC_API_OK);
    m->assembly = fwlab_nand_channel_volume_binding(m->volume); CHECK(m->assembly.aggregate.ops);
    if (format) for (i = 0; i < MH_FILES; ++i)
        CHECK(fstatat(f->directory_fd, mh_name(i), &m->files[i], AT_SYMLINK_NOFOLLOW) == 0);
    config.version = FWLAB_FTL_SCALE_EXTENDED_VERSION; config.size = sizeof(config); config.max_transfer_lbas = MH_LBAS;
    config.base.version = FWLAB_FTL_SCALE_VERSION; config.base.size = sizeof(config.base);
    config.base.geometry = m->config.geometry; memcpy(config.base.media_uuid, m->config.media_uuid, 16);
    config.base.namespace_ref.word[0] = 0x44323133; config.base.mapping_slots = MH_LBAS / 8;
    config.base.instance_nonce = UINT64_C(0x4432313300000000) + f->incarnation;
    config.base.provider_nonce = UINT64_C(0x4432313400000000) + f->incarnation;
    config.base.nfc_instance_nonce = UINT64_C(0x4432313500000000) + f->incarnation;
    config.base.nfc_operation_uid_limit = UINT64_MAX;
    config.base.host_sequence_limit = config.base.record_sequence_limit = UINT64_MAX - 1;
    config.base.generation = config.base.execution_epoch = config.base.nfc_epoch = 1;
    timing.version = FWLAB_NFC_PAGE_V2_LAB_MUTATION_VERSION; timing.size = sizeof(timing);
    read->version = FWLAB_NFC_PAGE_V2_LAB_VERSION; read->size = sizeof(*read);
    read->base.version = FWLAB_NFC_PAGE_V2_VERSION; read->base.size = sizeof(read->base);
    read->base.profile = FWLAB_NFC_PAGE_V2_PROFILE_R0; read->base.geometry = m->config.geometry;
    memcpy(read->base.media_uuid, m->config.media_uuid, 16);
    read->base.instance_nonce = config.base.nfc_instance_nonce; read->base.operation_uid_limit = UINT64_MAX;
    read->base.controller_epoch = read->base.generation = 1;
    read->command_ns = 1000; read->array_read_ns = 10000; read->channel_bytes_per_second = UINT64_C(1000000000);
    read->virtual_ns_limit = UINT64_C(10000000000);
    for (i = 0; i < 4 * MH_LUNS; ++i) {
        read->lun[i].target = read->lun[i].ce = (uint16_t)(i % MH_LUNS);
        read->lun[i].package = (uint16_t)(i / MH_LUNS); read->lun[i].die = (uint16_t)i;
    }
    timing.program_confirm_ns = timing.erase_command_ns = timing.status_command_ns = 1000;
    timing.array_program_ns = 100000; timing.array_erase_ns = 1000000; timing.status_response_bytes = 1;
    fb = MH_FTL_ARENA_SIZE(&config);
    CHECK(fb + hb + vb + BUFFER_BYTES < 16u * 1024u * 1024u);
    f->ftl_arena = mh_arena(fwlab_ftl_scale_arena_alignment(), fb);
    f->nfc_arena = mh_arena(fwlab_nfc_channel_v2_arena_alignment(), hb);
    CHECK(MH_HUB_INIT(f->nfc_arena, hb, &timing, &m->assembly, &m->hub) == FWLAB_NFC_API_OK);
    memset(&m->credit, 0, sizeof(m->credit)); m->credit.limit = credit_limit;
    m->credit.actual = fwlab_nfc_channel_v2_provider(m->hub); m->credit.hub = m->hub;
    m->credit.observation = &m->observation; m->credit.geometry = m->config.geometry;
    CHECK(MH_FTL_INIT(f->ftl_arena, fb, &config, &buffer, &provider, &f->ftl) == FWLAB_SPINE_V0_OK);
    CHECK(f->ftl->disk_format == 3 && f->ftl->writes && f->ftl->heads.count == 4);
    f->block = fwlab_ftl_scale_block_service(f->ftl); mh_current = m;
    CHECK((format ? fwlab_ftl_scale_format_start(f->ftl, MH_LBAS) :
        fwlab_ftl_scale_recover_start(f->ftl, MH_LBAS)) == FWLAB_SPINE_V0_OK);
    for (i = 0; i < STEPS && !f->ftl->ready; ++i) step(f, 0);
    CHECK(i < STEPS && f->ftl->root.layout.lba_count == MH_LBAS);
}
static void mh_close(struct mh_fixture *m)
{
    struct fixture *f = &m->base;
    struct fwlab_block_epoch_status_v0 epoch;
    uint32_t i, leases; uint8_t quiet;
    CHECK(!f->buffer.active && !m->observation.enabled);
    CHECK(f->block.ops->epoch_close(f->block.context, CLOSE_NONCE, 1) == FWLAB_SPINE_V0_OK);
    CHECK(buffer_close(&f->buffer, CLOSE_NONCE, 1) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    for (i = 0; i < STEPS; ++i) {
        CHECK(f->block.ops->epoch_quiescent(f->block.context, CLOSE_NONCE, 1, &epoch) == FWLAB_SPINE_V0_OK &&
            fwlab_block_epoch_status_v0_valid(&epoch));
        if (epoch.quiescent) break;
        step(f, 0);
    }
    struct fwlab_nfc_channel_v2_stats s = mh_stats(m);
    CHECK(i < STEPS && !epoch.aggregate_operations && sf_io_idle(f->ftl) && !sf_write_pool_busy(f->ftl) &&
        sf_heads_unreserved(f->ftl) && !s.occupied_credits && !s.results_pending && !s.retirement_pending &&
        m->credit.accepted_data == m->credit.taken_data && !m->credit.outstanding);
    for (i = 0; i < 4; ++i) CHECK(!s.actor_owned[i]);
    CHECK(buffer_quiet(&f->buffer, CLOSE_NONCE, 1, &leases, &quiet) == FWLAB_CONTROLLER_BUFFER_V0_OK && !leases && quiet);
    CHECK(fwlab_ftl_scale_fini(f->ftl) == FWLAB_SPINE_V0_OK);
    CHECK(fwlab_nand_channel_volume_close(m->volume) == FWLAB_NFC_API_OK);
    free(f->ftl_arena); free(f->nfc_arena); free(f->media_arena);
    f->ftl = NULL; m->hub = NULL; m->volume = NULL; mh_current = NULL;
}
static void mh_destroy(struct mh_fixture *m)
{
    struct fixture *f = &m->base;
    struct stat s;
    CHECK(!f->ftl && !m->volume && !f->buffer.active);
    for (unsigned i = 0; i < MH_FILES; ++i) {
        CHECK(fstatat(f->directory_fd, mh_name(i), &s, AT_SYMLINK_NOFOLLOW) == 0 &&
            s.st_dev == m->files[i].st_dev && s.st_ino == m->files[i].st_ino && s.st_size == m->files[i].st_size &&
            S_ISREG(s.st_mode) && s.st_nlink == 1 && s.st_uid == geteuid());
        CHECK(unlinkat(f->directory_fd, mh_name(i), 0) == 0);
    }
    CHECK(fstatat(AT_FDCWD, f->directory, &s, AT_SYMLINK_NOFOLLOW) == 0 &&
        s.st_dev == m->directory_identity.st_dev && s.st_ino == m->directory_identity.st_ino);
    CHECK(close(f->directory_fd) == 0 && rmdir(f->directory) == 0);
    guards(&f->buffer); free(f->buffer.allocation); free(m);
}

static void mh_uneven(unsigned credits)
{
    struct mh_fixture *m = mh_create(credits); struct fixture *f = &m->base;
    struct fwlab_block_request_v0 r;
    struct fwlab_block_status_v0 s;
    uint64_t map_before, batches_before;
    mh_open(m, true); map_before = f->ftl->map_sequence; batches_before = mh_stats(m).joined_batches;
    r = request(f, FWLAB_BLOCK_V0_WRITE, 0, 13 * 8, 0x53);
    admit(f, &r);
    m->observation.enabled = 1;
    m->observation.base_frontier = f->ftl->parent.base_frontier;
    m->observation.host_sequence = f->ftl->parent.host_sequence;
    s = finish(f, &r, 0);
    CHECK(s.outcome == FWLAB_BLOCK_V0_SUCCEEDED && s.effect == FWLAB_BLOCK_V0_EFFECT_FULL &&
        s.completed_lbas == 104 && s.durability_witness == FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE &&
        f->ftl->durable_frontier == 1 && f->ftl->map_sequence == map_before + 4 && f->ftl->parent.completed_groups == 1);
    CHECK(m->observation.accepted == 4 && m->observation.resolved == 4 && m->observation.callbacks == 13 &&
        m->observation.map_callbacks == 8 && m->observation.map_admissions == 8 && m->credit.peak == credits);
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(m->observation.run[i].pages == (i ? 3u : 4u));
        uint32_t domain = sf_head_domain(f->ftl, m->observation.run[i].first_ppa / 64);
        CHECK(domain < 4);
        for (unsigned j = 0; j < i; ++j)
            CHECK(domain != sf_head_domain(f->ftl, m->observation.run[j].first_ppa / 64));
        if (i) CHECK(m->observation.run[i].first_lpn == m->observation.run[i - 1].first_lpn + m->observation.run[i - 1].pages &&
            m->observation.run[i].token.operation_uid == m->observation.run[i - 1].token.operation_uid + 1);
    }
    CHECK(m->observation.run[1].completed_at < m->observation.run[0].completed_at);
    if (credits < 4) CHECK(m->credit.local_bp && m->credit.ack_bp);
    printf("MULTIHEAD_PARENT_WAVE|host_bytes=53248|runs=4,3,3,3|credits=%u|later_run_done_at=%u|first_run_done_at=%u|ordered_MAP_pairs=4|aggregate_groups=1|frontier=1|local_BP=%u|ACK_BP=%u|joined_batches=%llu\n",
        credits, m->observation.run[1].completed_at, m->observation.run[0].completed_at,
        m->credit.local_bp, m->credit.ack_bp, (unsigned long long)(mh_stats(m).joined_batches - batches_before));
    m->observation.enabled = 0; retire(f, &r);
    io(f, FWLAB_BLOCK_V0_READ, 0, 104, 0); check_pattern(f->buffer.bytes, 0, 104, 0x53);
    mh_close(m); mh_destroy(m);
}
static void mh_large_gc(void)
{
    struct mh_fixture *m = mh_create(4); struct fixture *f = &m->base;
    struct sf_map_entry before[256];
    uint64_t gc, uid, groups;
    uint32_t i, moved = 0;
    mh_open(m, true); groups = f->ftl->window.program_groups;
    io(f, FWLAB_BLOCK_V0_WRITE, 0, MH_LBAS, 0x71);
    CHECK(f->ftl->window.program_groups == groups + 4 && f->ftl->durable_frontier == 1 && sf_heads_empty(f->ftl));
    for (i = 0; i < 4; ++i) {
        uint32_t block = f->ftl->map[i * 64].ppa / 64;
        CHECK(f->ftl->blocks[block].disk.role == SF_CLOSED && f->ftl->blocks[block].disk.allocation_end == 64);
    }
    uid = f->ftl->blocks[f->ftl->map[0].ppa / 64].disk.block_uid;
    io(f, FWLAB_BLOCK_V0_WRITE, 0, 24, 0x72);
    CHECK(f->ftl->blocks[f->ftl->map[0].ppa / 64].disk.block_uid > uid && f->ftl->durable_frontier == 2);
    memcpy(before, f->ftl->map, sizeof(before)); gc = f->ftl->garbage_collections;
    CHECK(fwlab_ftl_scale_gc_start(f->ftl, 1) == FWLAB_SPINE_V0_OK);
    for (i = 0; i < STEPS && (f->ftl->work.kind != SF_WORK_NONE || sf_meta_busy(f->ftl) || !sf_io_idle(f->ftl)); ++i) step(f, 0);
    CHECK(i < STEPS && f->ftl->garbage_collections == gc + 1 && f->ftl->durable_frontier == 2);
    for (i = 0; i < 256; ++i) moved += before[i].ppa != f->ftl->map[i].ppa;
    CHECK(moved && moved <= 61);
    io(f, FWLAB_BLOCK_V0_READ, 0, MH_LBAS, 0);
    check_pattern(f->buffer.bytes, 0, 24, 0x72);
    check_pattern(f->buffer.bytes + 24 * 512, 24, MH_LBAS - 24, 0x71);
    mh_close(m); mh_open(m, false);
    CHECK(f->ftl->durable_frontier == 2 && sf_heads_empty(f->ftl));
    io(f, FWLAB_BLOCK_V0_READ, 0, MH_LBAS, 0);
    check_pattern(f->buffer.bytes, 0, 24, 0x72);
    check_pattern(f->buffer.bytes + 24 * 512, 24, MH_LBAS - 24, 0x71);
    printf("MULTIHEAD_PARENT_LARGE|Host_Block_bytes=1048576|DATA_runs=4x64pages|head_rollover=1|GC_transactions=1|relocated_pages=%u|full_read_and_reopen=exact|frontier=2|not_NVMe_profile_expansion=1\n", moved);
    /* Reach the existing global emergency reserve using ordinary whole-volume
     * overwrite, then prove a retained parent can progress at reduced width.
     * This is one finite small-capacity episode, not a large stress framework. */
    uint32_t overwrites = 0;
    while (f->ftl->free_count > 1) {
        CHECK(overwrites < 32);
        io(f, FWLAB_BLOCK_V0_WRITE, 0, MH_LBAS, (uint8_t)(0x80 + overwrites));
        ++overwrites;
    }
    CHECK(f->ftl->free_count == 1);
    uint64_t frontier = f->ftl->durable_frontier;
    struct fwlab_block_request_v0 pressure = request(f, FWLAB_BLOCK_V0_WRITE, 0, MH_LBAS, 0xb1);
    admit(f, &pressure);
    struct fwlab_block_status_v0 done = finish(f, &pressure, 0);
    uint32_t waves = f->ftl->parent.completed_groups;
    CHECK(done.outcome == FWLAB_BLOCK_V0_SUCCEEDED && done.completed_lbas == MH_LBAS &&
        done.durability_witness == FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE && waves > 1 &&
        f->ftl->durable_frontier == frontier + 1 && !f->ftl->quarantined);
    retire(f, &pressure);
    mh_close(m); mh_open(m, false);
    CHECK(f->ftl->durable_frontier == frontier + 1);
    io(f, FWLAB_BLOCK_V0_READ, 0, MH_LBAS, 0); check_pattern(f->buffer.bytes, 0, MH_LBAS, 0xb1);
    printf("MULTIHEAD_PARENT_PRESSURE|bounded_overwrites=%u|reached_global_reserve=1|retained_parent_waves=%u|automatic_space_reclaim=1|full_reopen_read=exact\n", overwrites, waves);
    mh_close(m); mh_destroy(m);
}
#ifndef MULTIHEAD_PARENT_ENTRY
#define MULTIHEAD_PARENT_ENTRY main
#endif
int MULTIHEAD_PARENT_ENTRY(void)
{
    struct rusage usage;
    mh_uneven(4); mh_uneven(2); mh_large_gc();
    CHECK(getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss < 256 * 1024);
    printf("MULTIHEAD_PARENT_PASS|fresh_fixtures=3|all_holders_closed=1|cleanup=verified_own_files_only|peak_RSS_KiB=%ld|RSS_budget_KiB=262144|tmpfs_page_cache_excluded=1|no_bandwidth_or_native_claim=1\n", usage.ru_maxrss);
    return 0;
}
