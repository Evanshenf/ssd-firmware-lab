/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#define _GNU_SOURCE

#include "scale_storage.h"
#include "ftl_scale_internal.h"
#include "compact_nand.h"
#include "compact_nand_internal.h"
#include "fwlab/portable/nvme_codec.h"

#include <fcntl.h>
#include <linux/magic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "SCALE FTL %s:%d: %s\n", __FILE__, __LINE__, #x); \
    exit(EXIT_FAILURE); } } while (0)
#define STEP_LIMIT UINT32_C(20000000)

struct fixture {
    char directory[64];
    int directory_fd;
    void *media_arena;
    struct fwlab_file_nand_v1 *media;
    struct fwlab_file_nand_holder_v1 holder;
    struct fwlab_file_nand_v1_config media_config;
    struct j0_media_binding media_binding;
    struct scale_storage_options options;
    struct j0_storage_factory factory;
    struct j0_runtime *runtime;
    uint64_t lbas;
    uint64_t uid;
    uint64_t incarnation;
    int medium_is_tmpfs;
    const char *stage;
    uint64_t stage_done;
    uint64_t stage_total;
    uint64_t started;
    uint64_t last_report;
    uint32_t progress_ticks;
    void (*cut_observer)(struct fixture *fixture);
    uint32_t cut_kind;
};

static uint64_t now_seconds(void)
{
    struct timespec now;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec;
}

static uint32_t step_limit(const struct fixture *f)
{
    /* Construction/recovery stream capacity-sized metadata. This changes the
     * test's iteration allowance, never a firmware budget or transition. */
    return f->lbas > UINT64_C(256) * 2048u ? UINT32_C(1000000000) : STEP_LIMIT;
}

static void progress(struct fixture *f, int force)
{
    uint64_t now = now_seconds();
    struct fwlab_ftl_scale_status s = {0};
    struct rusage usage;
    uint32_t work = 0, meta = 0, erase = 0, ordinal = 0;
    if (!force && now - f->last_report < 30u)
        return;
    if (f->runtime && f->runtime->block.context) {
        const struct fwlab_ftl_scale *lower = f->runtime->block.context;
        CHECK(scale_storage_query(f->runtime, &s) == FWLAB_SPINE_V0_OK);
        work = lower->work.phase;
        meta = lower->meta.phase;
        erase = lower->meta.erase_index;
        ordinal = lower->meta.ordinal;
    }
    CHECK(getrusage(RUSAGE_SELF, &usage) == 0);
    fprintf(stderr, "SCALE_PROGRESS|pid=%ld|lbas=%llu|stage=%s|done_bytes=%llu|total_bytes=%llu|elapsed_s=%llu|gc=%llu|cp=%llu|nfc_started=%llu|work=%u|meta=%u|erase_cursor=%u|page_cursor=%u|maxrss_kib=%ld\n",
            (long)getpid(), (unsigned long long)f->lbas,
            f->stage ? f->stage : "startup",
            (unsigned long long)f->stage_done,
            (unsigned long long)f->stage_total,
            (unsigned long long)(now - f->started),
            (unsigned long long)s.garbage_collections,
            (unsigned long long)s.checkpoints,
            (unsigned long long)s.nfc_children,
            work, meta, erase, ordinal, usage.ru_maxrss);
    f->last_report = now;
}

static void stage_start(struct fixture *f, const char *name, uint64_t total)
{
    f->stage = name;
    f->stage_done = 0;
    f->stage_total = total;
    progress(f, 1);
}

static uint64_t available_memory(void)
{
    FILE *stream = fopen("/proc/meminfo", "r");
    char line[256];
    unsigned long long kib = 0;
    CHECK(stream);
    while (fgets(line, sizeof(line), stream))
        if (sscanf(line, "MemAvailable: %llu kB", &kib) == 1)
            break;
    CHECK(fclose(stream) == 0 && kib && kib <= UINT64_MAX / 1024u);
    return (uint64_t)kib * 1024u;
}

static void media_preflight(const char *directory, uint32_t mib,
                             const struct fwlab_file_nand_v1_config *config)
{
    struct statfs fs;
    uint64_t image_bytes = fwlab_file_nand_v1_image_bytes(config);
    uint64_t available, total, ram = available_memory();
    uint64_t reserve = mib > 256 ? UINT64_C(2) << 30 : UINT64_C(128) << 20;
    int fd;
    if (!directory || directory[0] != '/') {
        fputs("SCALE_PREFLIGHT_ERROR|absolute_FWLAB_TEST_MEDIA_DIR_required|no_disk_fallback=1\n", stderr);
        exit(EXIT_FAILURE);
    }
    fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        fprintf(stderr, "SCALE_PREFLIGHT_ERROR|missing_media_directory=%s|no_disk_fallback=1\n", directory);
        exit(EXIT_FAILURE);
    }
    CHECK(fstatfs(fd, &fs) == 0 && fs.f_bsize > 0);
    CHECK(close(fd) == 0);
    total = (uint64_t)fs.f_blocks * (uint64_t)fs.f_bsize;
    available = (uint64_t)fs.f_bavail * (uint64_t)fs.f_bsize;
    /* This entry is explicitly functional/tmpfs-only. An unmounted directory
     * must never silently become a slow disk qualification. */
    if (fs.f_type != TMPFS_MAGIC || !image_bytes ||
        available < image_bytes + (UINT64_C(64) << 20) ||
        ram < image_bytes + reserve ||
        total > (mib > 256 ? UINT64_C(90) << 30 : UINT64_C(1) << 30)) {
        fprintf(stderr, "SCALE_PREFLIGHT_ERROR|fs_type=%lx|image_bytes=%llu|fs_available=%llu|mem_available=%llu|fs_limit=%llu|no_disk_fallback=1\n",
                (unsigned long)fs.f_type, (unsigned long long)image_bytes,
                (unsigned long long)available, (unsigned long long)ram,
                (unsigned long long)total);
        exit(EXIT_FAILURE);
    }
    printf("SCALE_PREFLIGHT_OK|medium=tmpfs|logical_mib=%u|image_bytes=%llu|fs_available=%llu|mem_available=%llu|fs_limit=%llu\n",
           mib, (unsigned long long)image_bytes, (unsigned long long)available,
           (unsigned long long)ram, (unsigned long long)total);
}

static const char *medium_name(const struct fixture *f)
{
    return f->medium_is_tmpfs ? "tmpfs" : "non-tmpfs";
}

static void medium_report(struct fixture *f, int before_removal)
{
    struct statfs fs;
    uint64_t block_bytes, total, used;
    CHECK(fstatfs(f->directory_fd, &fs) == 0 && fs.f_bsize > 0);
    f->medium_is_tmpfs = fs.f_type == TMPFS_MAGIC;
    block_bytes = (uint64_t)fs.f_bsize;
    total = (uint64_t)fs.f_blocks * block_bytes;
    used = (uint64_t)(fs.f_blocks - fs.f_bfree) * block_bytes;
    if (!before_removal) {
        printf("SCALE_TEST_MEDIUM|medium=%s|fs_type=%lx|volume_bytes=%llu|directory=%s\n",
               medium_name(f), (unsigned long)fs.f_type,
               (unsigned long long)total, f->directory);
    } else if (f->medium_is_tmpfs) {
        /* One serial image; compact-NAND allocates homes without punching
         * holes. On a dedicated mount, allocation is monotone until unlink.
         * This is filesystem RAM usage, separate from process maximum RSS. */
        printf("SCALE_TEST_MEDIUM_PEAK|medium=tmpfs|used_bytes=%llu|limit_bytes=%llu|capture=before_owned_image_removal\n",
               (unsigned long long)used, (unsigned long long)total);
    }
}

static struct fwlab_nfc_geometry geometry(uint32_t logical_mib)
{
    struct fwlab_nfc_geometry g = {0};
    g.version = FWLAB_NFC_CONTRACT_VERSION;
    g.size = (uint16_t)sizeof(g);
    g.channels = logical_mib == 64 ? 1 : 2;
    g.luns_per_channel = g.channels;
    g.planes_per_lun = g.channels;
    CHECK(logical_mib == 64 || logical_mib == 256 || logical_mib == 65536);
    g.blocks_per_plane = logical_mib == 64 ? 320 :
                        (logical_mib == 256 ? 160 : 40960);
    g.pages_per_block = 64;
    g.plane_parallelism_per_lun = g.planes_per_lun;
    g.main_bytes_per_page = SF_PAGE_BYTES;
    g.oob_bytes_per_page = SF_OOB_BYTES;
    g.max_programs_per_erase = 1;
    g.program_order = FWLAB_NFC_PROGRAM_ASCENDING;
    return g;
}

static void media_open(struct fixture *f, int format)
{
    size_t bytes = fwlab_file_nand_v1_arena_size();
    f->media_arena = calloc(1, bytes);
    CHECK(f->media_arena);
    if (format)
        CHECK(fwlab_file_nand_v1_posix_format(f->media_arena, bytes,
            f->directory_fd, "nand.bin", &f->media_config, &f->media,
            &f->holder) == FWLAB_NFC_API_OK);
    else
        CHECK(fwlab_file_nand_v1_posix_restart(f->media_arena, bytes,
            f->directory_fd, "nand.bin", &f->media_config, &f->holder,
            &f->media) == FWLAB_NFC_API_OK);
    f->media_binding.media = fwlab_file_nand_v1_media(f->media);
    f->media_binding.geometry = f->media_config.geometry;
    memcpy(f->media_binding.media_uuid, f->media_config.media_uuid, 16);
}

static void media_close(struct fixture *f)
{
    CHECK(fwlab_file_nand_v1_close(f->media) == FWLAB_NFC_API_OK);
    free(f->media_arena);
    f->media = NULL;
    f->media_arena = NULL;
}

static void tick(struct fixture *f)
{
    uint32_t used;
    enum fwlab_spine_result_v0 result = j0_runtime_step(f->runtime, 3, &used);
    if (result != FWLAB_SPINE_V0_OK) {
        struct fwlab_ftl_scale_status s = {0};
        (void)scale_storage_query(f->runtime, &s);
        fprintf(stderr, "step result=%u FTL fault=%08x rec=%llu map=%llu free=%u\n",
                (unsigned)result, s.fault_code,
                (unsigned long long)s.record_sequence,
                (unsigned long long)s.map_sequence, s.free_blocks);
    }
    CHECK(result == FWLAB_SPINE_V0_OK && used == 3);
    if ((++f->progress_ticks & 1023u) == 0)
        progress(f, 0);
    if (f->cut_observer)
        f->cut_observer(f);
}

static void wait_ready(struct fixture *f)
{
    uint32_t i;
    for (i = 0; i < step_limit(f) && !f->runtime->ready; ++i)
        tick(f);
    CHECK(i < step_limit(f) && f->runtime->namespace_bound);
    CHECK(f->runtime->m3p == NULL && f->runtime->nfc_model == NULL);
    CHECK(f->runtime->volume.lba_count == f->lbas);
    CHECK(f->runtime->block.context != NULL);
}

static void runtime_start(struct fixture *f, int format, uint64_t expectation)
{
    struct j0_runtime_config c = {0};
    c.version = J0_RUNTIME_VERSION;
    c.size = (uint16_t)sizeof(c);
    memcpy(c.media_uuid, f->media_config.media_uuid, 16);
    c.media_binding = &f->media_binding;
    c.media_mode = format ? J0_MEDIA_FORMAT : J0_MEDIA_RECOVER;
    c.generation = 1;
    c.execution_epoch = 1;
    c.volatile_nonce_seed = ++f->incarnation * 100u;
    c.budget_profile = J0_BUDGET_SCALE;
    c.storage_factory = &f->factory;
    c.format_lba_count = format ? f->lbas : 0;
    c.expected_lba_count = format ? 0 : expectation;
    f->runtime = calloc(1, sizeof(*f->runtime));
    CHECK(f->runtime);
    CHECK(j0_runtime_init(f->runtime, &c) == FWLAB_SPINE_V0_OK);
    stage_start(f, format ? "format" : "recovery", 0);
}

static void runtime_close(struct fixture *f)
{
    struct j0_close_status s;
    uint32_t i;
    int unbound = !f->runtime->namespace_bound;
    CHECK(j0_runtime_close_start(f->runtime) == FWLAB_SPINE_V0_OK);
    stage_start(f, "close", 0);
    for (i = 0; i < step_limit(f); ++i) {
        CHECK(j0_runtime_close_query(f->runtime, &s) == FWLAB_SPINE_V0_OK);
        if (s.quiescent)
            break;
        tick(f);
        if (unbound)
            CHECK(!f->runtime->namespace_bound && !f->runtime->linux_adapter.ops);
    }
    CHECK(i < step_limit(f));
    /* Effectful quiescence can precede the existing bounded profile-record
     * retirement. fini explicitly reports IN_PROGRESS for that bookkeeping. */
    for (i = 0; i < 256; ++i) {
        enum fwlab_spine_result_v0 result = j0_runtime_fini(f->runtime);
        if (result == FWLAB_SPINE_V0_OK)
            break;
        CHECK(result == FWLAB_SPINE_V0_IN_PROGRESS);
    }
    CHECK(i < 256);
    CHECK(j0_runtime_close_query(f->runtime, &s) == FWLAB_SPINE_V0_OK);
    CHECK(s.quiescent && s.profiles_retired && !s.host_authorities &&
          !s.dma_operations && !s.buffers && !s.block_operations &&
          !s.nfc_operations && !s.pending && !s.pinned);
    free(f->runtime);
    f->runtime = NULL;
}

static void reopen(struct fixture *f)
{
    runtime_close(f);
    media_close(f);
    media_open(f, 0);
    runtime_start(f, 0, 0);
    wait_ready(f);
}

static void command_profile(struct fixture *f, uint32_t profile,
                     uint8_t opcode, uint64_t lba,
                     uint32_t lbas, const uint8_t *input, uint8_t *output,
                     int fua, uint8_t expected_status)
{
    struct j0_runtime *r = f->runtime;
    struct fwlab_nvme_command c = {0};
    struct j0_host_transfer transfer = {0};
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_nvme_completion_intent intent;
    struct fwlab_completion_lease_v0 lease;
    uint32_t i;
    c.version = FWLAB_NVME_COMMAND_VERSION;
    c.size = (uint16_t)sizeof(c);
    c.handle.instance_nonce = UINT64_C(0x53464232484f0000) + f->incarnation;
    c.handle.command_uid = ++f->uid;
    c.handle.controller_epoch = 1;
    c.handle.generation = 1;
    c.origin.word[0] = c.handle.instance_nonce ^ UINT64_C(0x4f52494700000000);
    c.origin.word[1] = f->uid;
    c.trace_cookie = f->uid;
    c.safety_generation = 1;
    c.namespace_id = 1;
    c.opcode = opcode;
    c.queue_class = opcode == 6 ? FWLAB_NVME_QUEUE_ADMIN : FWLAB_NVME_QUEUE_IO;
    c.fuse = FWLAB_NVME_FUSE_NONE;
    c.data_pointer_format = FWLAB_NVME_DATA_POINTER_PRP;
    c.data_address_present = (uint8_t)(opcode != 0);
    transfer.version = J0_RUNTIME_VERSION;
    transfer.size = (uint16_t)sizeof(transfer);
    if (opcode == 6) {
        transfer.direction = FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST;
        transfer.exact_bytes = 4096;
    } else if (opcode) {
        c.command_dword10_15[0] = (uint32_t)lba;
        c.command_dword10_15[1] = (uint32_t)(lba >> 32);
        c.command_dword10_15[2] = lbas - 1u;
        if (fua)
            c.command_dword10_15[2] |= UINT32_C(1) << 30;
        transfer.direction = opcode == 1 ? FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER
                                         : FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST;
        transfer.exact_bytes = lbas * 512u;
        transfer.input = input;
    }
    if (expected_status) {
        transfer.direction = 0;
        transfer.exact_bytes = 0;
        transfer.input = NULL;
    }
    CHECK(j0_runtime_admit_start(r, profile, &c, &transfer,
                                 &ticket) == FWLAB_SPINE_V0_OK);
    for (i = 0; i < step_limit(f); ++i) {
        enum fwlab_spine_result_v0 result =
            j0_runtime_intent_read(r, &ticket, &intent);
        if (result == FWLAB_SPINE_V0_OK)
            break;
        CHECK(result == FWLAB_SPINE_V0_IN_PROGRESS);
        tick(f);
    }
    CHECK(i < step_limit(f));
    if (intent.status_code != expected_status)
        fprintf(stderr, "command uid=%llu op=%u lba=%llu status=%u expected=%u\n",
                (unsigned long long)f->uid, opcode, (unsigned long long)lba,
                intent.status_code, expected_status);
    CHECK(intent.status_code == expected_status && intent.status_code_type == 0);
    if (opcode == 1 && !expected_status) {
        const struct j0_admission_record *admitted = NULL;
        for (i = 0; i < J0_MAX_COMMANDS; ++i)
            if (r->admission[i].occupied && fwlab_spine_command_ticket_v0_equal(
                    &r->admission[i].ticket, &ticket))
                admitted = &r->admission[i];
        CHECK(admitted && admitted->program.action_count == 2);
        /* Linux v1 advertises no volatile write cache and requests SELF for
         * ALL Writes. The independent C43 reference requests VOLATILE_ALLOWED.
         * Never derive the lower durability contract solely from SQE.FUA. */
        CHECK(admitted->action[1].block_request.durability ==
              (profile == J0_PROFILE_LINUX_V1 ? FWLAB_BLOCK_V0_DURABILITY_SELF
                                             : FWLAB_BLOCK_V0_DURABILITY_VOLATILE_ALLOWED));
        CHECK(admitted->action[1].block_status.durability_witness ==
              (profile == J0_PROFILE_LINUX_V1 ? FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE
                                             : FWLAB_BLOCK_V0_WITNESS_VOLATILE));
    }
    if (output)
        CHECK(j0_runtime_host_read(r, &ticket, output, transfer.exact_bytes) ==
              FWLAB_SPINE_V0_OK);
    CHECK(j0_runtime_publication_acquire(r, &ticket, &lease, &intent) == FWLAB_SPINE_V0_OK);
    CHECK(j0_runtime_publication_finish(r, &ticket, &lease,
          FWLAB_SPINE_PUBLICATION_V1_COMMITTED) == FWLAB_SPINE_V0_OK);
}

static void command(struct fixture *f, uint8_t opcode, uint64_t lba,
                     uint32_t lbas, const uint8_t *input, uint8_t *output,
                     int fua, uint8_t expected_status)
{
    command_profile(f, J0_PROFILE_LINUX_V1, opcode, lba, lbas,
                    input, output, fua, expected_status);
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t n = 0;
    unsigned i;
    for (i = 0; i < 8; ++i)
        n |= (uint64_t)p[i] << (8u * i);
    return n;
}

static void identify(struct fixture *f)
{
    uint8_t bytes[4096];
    command(f, 6, 0, 0, NULL, bytes, 0, 0);
    CHECK(get64(bytes) == f->lbas && get64(bytes + 8) == f->lbas &&
          get64(bytes + 16) == f->lbas);
}

static void wait_idle(struct fixture *f)
{
    uint32_t i;
    struct fwlab_ftl_scale_status s;
    for (i = 0; i < step_limit(f); ++i) {
        CHECK(scale_storage_query(f->runtime, &s) == FWLAB_SPINE_V0_OK);
        if (!s.busy)
            break;
        tick(f);
    }
    CHECK(i < step_limit(f) && !s.quarantined);
}

static void fill_pattern(uint8_t bytes[8192], uint64_t lba, uint8_t version)
{
    uint32_t i;
    for (i = 0; i < 8192; ++i) {
        uint64_t sector = lba + i / 512u;
        bytes[i] = (uint8_t)(sector * 37u + (sector >> 8) +
                            (i % 512u) * 13u + version * 19u);
    }
}

static void full_journey(struct fixture *f)
{
    uint8_t bytes[8192], output[8192];
    uint64_t lba, overwritten = f->lbas / 2u;
    struct fwlab_ftl_scale_status s;
    stage_start(f, "fill", f->lbas * 512u);
    fprintf(stderr, "full-fill %llu bytes\n", (unsigned long long)f->lbas * 512u);
    for (lba = 0; lba < f->lbas; lba += 16) {
        fill_pattern(bytes, lba, 1);
        command(f, 1, lba, 16, bytes, NULL, 0, 0);
        f->stage_done = (lba + 16u) * 512u;
        progress(f, 0);
    }
    progress(f, 1);
    /* Interleave replaced and retained8KiB groups. Sequential half-volume
     * overwrite can yield zero-live victims and legitimately need no copies. */
    fprintf(stderr, "full-overwrite interleaved %llu bytes\n",
            (unsigned long long)overwritten * 512u);
    stage_start(f, "overwrite", overwritten * 512u);
    for (lba = 0; lba < f->lbas; lba += 32) {
        fill_pattern(bytes, lba, 2);
        command(f, 1, lba, 16, bytes, NULL, 1, 0);
        f->stage_done += 8192u;
        progress(f, 0);
    }
    progress(f, 1);
    stage_start(f, "flush", 0);
    command(f, 0, 0, 0, NULL, NULL, 0, 0);
    CHECK(scale_storage_query(f->runtime, &s) == FWLAB_SPINE_V0_OK);
    fprintf(stderr, "full-reclaim gc=%llu cp=%llu\n",
            (unsigned long long)s.garbage_collections,
            (unsigned long long)s.checkpoints);
    CHECK(s.garbage_collections > 0 && s.checkpoints > 1);
    reopen(f);
    stage_start(f, "readback", f->lbas * 512u);
    for (lba = 0; lba < f->lbas; lba += 16) {
        fill_pattern(bytes, lba, lba % 32u == 0 ? 2 : 1);
        command(f, 2, lba, 16, NULL, output, 0, 0);
        CHECK(memcmp(bytes, output, sizeof(bytes)) == 0);
        f->stage_done = (lba + 16u) * 512u;
        progress(f, 0);
    }
    progress(f, 1);
    printf("SCALE_FTL_FULL_PASS|lbas=%llu|fill_bytes=%llu|overwrite_bytes=%llu|readback_bytes=%llu|gc=%llu|cp=%llu|medium=%s\n",
           (unsigned long long)f->lbas, (unsigned long long)f->lbas * 512u,
           (unsigned long long)overwritten * 512u,
           (unsigned long long)f->lbas * 512u,
           (unsigned long long)s.garbage_collections,
           (unsigned long long)s.checkpoints, medium_name(f));
}

/* Named process-cut cases share this real fixture; no production observer or
 * synthetic Block/NFC executor is introduced. */
#include "ftl_cuts.inc"
#include "ftl_cost.inc"

static void journey(uint32_t mib, int full, int cuts, int cost)
{
    struct fixture f = {0};
    struct fwlab_ftl_scale_status s;
    uint8_t expected[65536] = {0};
    uint8_t bytes[8192], output[8192], last[512];
    uint64_t media_sequence, cp;
    uint32_t i;
    const char *media_parent = getenv("FWLAB_TEST_MEDIA_DIR");
    int length;
    f.started = now_seconds();
    f.media_config.geometry = geometry(mib);
    memcpy(f.media_config.media_uuid, "SCALE-B2-NAND-001", 16);
    f.media_config.media_uuid[15] = (uint8_t)(mib / 64u);
    f.lbas = (uint64_t)mib * 2048u;
    if (!media_parent || !*media_parent) {
        fputs("SCALE_PREFLIGHT_ERROR|FWLAB_TEST_MEDIA_DIR_required|no_disk_fallback=1\n", stderr);
        exit(EXIT_FAILURE);
    }
    media_preflight(media_parent, mib, &f.media_config);
    CHECK(media_parent[0] == '/');
    length = snprintf(f.directory, sizeof(f.directory),
                      "%s/fwlab-scale-ftl.XXXXXX", media_parent);
    CHECK(length > 0 && (size_t)length < sizeof(f.directory));
    CHECK(mkdtemp(f.directory));
    fprintf(stderr, "SCALE FTL %u MiB: %s/nand.bin\n", mib, f.directory);
    f.directory_fd = open(f.directory, O_DIRECTORY | O_CLOEXEC | O_RDONLY);
    CHECK(f.directory_fd >= 0);
    medium_report(&f, 0);
    scale_storage_factory_init(&f.factory, &f.options);
    media_open(&f, 1);
    runtime_start(&f, 1, 0);
    media_sequence = fwlab_file_nand_v1_sequence(f.media);
    CHECK(media_sequence == 0); /* Construction and starts did not perform IO. */
    wait_ready(&f);
    identify(&f);
    if (cost) {
        cost_journey(&f);
        goto finish;
    }
    memset(bytes, 0x31, 4096);
    command_profile(&f, J0_PROFILE_C43_P1, 1, 0, 8, bytes, NULL, 0, 0);
    memcpy(expected, bytes, 4096);
    memset(last, 0xb7, sizeof(last));
    command(&f, 1, f.lbas - 1, 1, last, NULL, 1, 0);
    command(&f, 2, f.lbas - 1, 1, NULL, output, 0, 0);
    CHECK(memcmp(last, output, sizeof(last)) == 0);
    media_sequence = fwlab_file_nand_v1_sequence(f.media);
    command(&f, 1, f.lbas, 1, last, NULL, 0, 0x80);
    command(&f, 2, f.lbas, 1, NULL, NULL, 0, 0x80);
    CHECK(fwlab_file_nand_v1_sequence(f.media) == media_sequence);
    /* One unaligned8KiB write really spans three NAND logical pages. */
    memset(bytes, 0x5a, sizeof(bytes));
    command(&f, 1, 7, 16, bytes, NULL, 0, 0);
    memcpy(expected + 7u * 512u, bytes, sizeof(bytes));
    if (mib > 256) {
        /* Full-size qualification itself drives reclamation and bank reuse;
         * do not pretend1100 tiny writes fill a64K-slot large journal. */
        CHECK(full && !cuts);
        for (i = 0; i < 2; ++i) {
            command(&f, 2, i * 16u, 16, NULL, output, 0, 0);
            CHECK(memcmp(expected + i * 8192u, output, sizeof(output)) == 0);
        }
        full_journey(&f);
        goto finish;
    }
    stage_start(&f, "small-regression", 0);
    for (i = 0; i < 70; ++i) {
        uint32_t page = i % 16u;
        memset(bytes, (int)(i + 1u), 4096);
        command(&f, 1, page * 8u, 8, bytes, NULL, i == 69, 0);
        memcpy(expected + page * 4096u, bytes, 4096);
    }
    wait_idle(&f);
    CHECK(scale_storage_gc(f.runtime, 3) == FWLAB_SPINE_V0_OK);
    wait_idle(&f);
    for (i = 0; i < sizeof(expected) / sizeof(output); ++i) {
        command(&f, 2, i * 16u, 16, NULL, output, 0, 0);
        CHECK(memcmp(expected + i * 8192u, output, sizeof(output)) == 0);
    }
    CHECK(scale_storage_query(f.runtime, &s) == FWLAB_SPINE_V0_OK);
    CHECK(s.garbage_collections > 0);
    cp = s.checkpoints;
    /* Force actual journal-bank recycling independently of total DATA fill. */
    for (i = 0; i < 1100; ++i) {
        memset(bytes, (int)(i % 239u + 1u), 512);
        command(&f, 1, 0, 1, bytes, NULL, 0, 0);
        memcpy(expected, bytes, 512);
    }
    command(&f, 0, 0, 0, NULL, NULL, 0, 0);
    CHECK(scale_storage_query(f.runtime, &s) == FWLAB_SPINE_V0_OK);
    CHECK(s.checkpoints > cp);
    printf("SCALE_FTL_LIVE|mib=%u|arena=%llu|nfc_children=%llu|gc=%llu|cp=%llu\n",
           mib, (unsigned long long)s.arena_bytes, (unsigned long long)s.nfc_children,
           (unsigned long long)s.garbage_collections, (unsigned long long)s.checkpoints);
    reopen(&f);
    identify(&f);
    for (i = 0; i < sizeof(expected) / sizeof(output); ++i) {
        command(&f, 2, i * 16u, 16, NULL, output, 0, 0);
        CHECK(memcmp(expected + i * 8192u, output, sizeof(output)) == 0);
    }
    command(&f, 2, f.lbas - 1, 1, NULL, output, 0, 0);
    CHECK(memcmp(last, output, sizeof(last)) == 0);
    if (full)
        full_journey(&f);
    if (cuts)
        cuts_journey(&f);
finish:
    runtime_close(&f);
    media_close(&f);
    media_open(&f, 0);
    runtime_start(&f, 0, 0);
    media_sequence = fwlab_file_nand_v1_sequence(f.media);
    runtime_close(&f); /* Close before volume/profile readiness. */
    CHECK(fwlab_file_nand_v1_sequence(f.media) == media_sequence);
    media_close(&f);
    medium_report(&f, 1);
    CHECK(unlinkat(f.directory_fd, "nand.bin", 0) == 0);
    CHECK(close(f.directory_fd) == 0 && rmdir(f.directory) == 0);
    if (cost)
        puts("SCALE_COST_CLOSED|early_close=1|owned_image_removed=1");
    else
        printf("SCALE_FTL_PASS|logical_mib=%u|identify=1|edge=1|range_no_effect=1|rmw3=1|witness=1|gc=1|checkpoint_recycle=1|recovery=1|early_close=1|full=%d|medium=%s\n",
               mib, full, medium_name(&f));
}

int main(int argc, char **argv)
{
    int full = argc == 2 && strcmp(argv[1], "--full") == 0;
    int cuts = argc == 2 && strcmp(argv[1], "--cuts") == 0;
    int large = argc == 2 && strcmp(argv[1], "--full-64g") == 0;
    int plan = argc == 2 && strcmp(argv[1], "--plan-64g") == 0;
    int cost = argc == 2 && strcmp(argv[1], "--cost") == 0;
    CHECK(setvbuf(stdout, NULL, _IOLBF, 0) == 0);
    CHECK(setvbuf(stderr, NULL, _IOLBF, 0) == 0);
    CHECK(argc == 1 || full || cuts || large || plan || cost);
    if (plan) {
        struct fwlab_file_nand_v1_config config = {0};
        config.geometry = geometry(65536);
        memcpy(config.media_uuid, "SCALE-B2-NAND-001", 16);
        media_preflight(getenv("FWLAB_TEST_MEDIA_DIR"), 65536, &config);
        puts("SCALE_PLAN_ONLY|no_NAND_io=1|no_runtime_pass_claim=1");
        return 0;
    }
    if (large) {
        journey(65536, 1, 0, 0);
        return 0;
    }
    journey(64, full, cuts, cost);
    if (!cost)
        journey(256, full, 0, 0);
    return 0;
}
