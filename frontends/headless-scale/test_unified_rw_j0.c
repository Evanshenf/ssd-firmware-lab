/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* D216: reuse only legal Linux-profile commands and adjacent fixture helpers.
 * The old capacity entry is never called. No direct mapping/media producer. */
#define main(...) retained_unified_scale_fixture_entry(__VA_ARGS__)
#include "test_ftl.c"
#undef main
#include "channel_volume.h"
#include "nfc_channel_workers.h"

#define RW_FILES 6u
struct rw_fixture {
    struct fixture f;
    struct fwlab_nand_channel_volume_config config;
    struct fwlab_nand_channel_volume *volume;
    struct fwlab_nand_channel_v2 assembly;
    struct fwlab_nfc_page_v2_lab_mutation_config timing;
    struct fwlab_nfc_channel_workers *workers;
    struct fwlab_nfc_channel_executor executor;
    struct fwlab_nfc_page_v2_provider actual;
    struct fwlab_nfc_channel_v2 *hub;
    struct stat directory_identity, file_identity[RW_FILES];
    void *volume_arena;
    uint8_t oracle[1048576];
    struct fwlab_nfc_page_v2_request read[4];
    uint64_t last_uid, retry_uid, ack_backpressure, read_oob_bytes;
    uint32_t read_count, read_results, peak_runs;
    unsigned worker_count;
    bool observe;
};
struct rw_outcome { uint64_t gc_hash, final_hash, frontier, plane_ns; };
static struct rw_fixture *rw_current;

static struct fwlab_nfc_channel_v2_stats rw_stats(struct rw_fixture *u)
{
    struct fwlab_nfc_channel_v2_stats s;
    CHECK(scale_storage_channel_snapshot(u->f.runtime, &s) == FWLAB_SPINE_V0_OK &&
        !s.quarantined && !s.poisoned && !s.counters_saturated);
    for (unsigned c = 0; c < 4; ++c)
        CHECK(s.channel[c].read_policy == FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE &&
            !s.channel[c].quarantined && !s.channel[c].counters_saturated);
    return s;
}
static struct fwlab_nfc_submit_result rw_submit(void *opaque, const struct fwlab_nfc_page_v2_request *r)
{
    struct rw_fixture *u = opaque;
    if (u->retry_uid) CHECK(r->operation.operation_uid == u->retry_uid);
    struct fwlab_nfc_submit_result result = u->actual.ops->try_submit(u->actual.context, r);
    if (result.disposition == FWLAB_NFC_BACKPRESSURE) {
        struct fwlab_nfc_channel_v2_stats s;
        CHECK(fwlab_nfc_channel_v2_snapshot(u->hub, &s) == FWLAB_NFC_API_OK);
        u->ack_backpressure += s.retirement_pending != 0;
        u->retry_uid = r->operation.operation_uid;
    } else if (result.disposition == FWLAB_NFC_ACCEPTED) {
        CHECK(r->operation.operation_uid > u->last_uid);
        u->last_uid = r->operation.operation_uid; u->retry_uid = 0;
        if (u->observe && r->kind == FWLAB_NFC_PAGE_V2_READ_GROUP) {
            CHECK(u->read_count < 4); u->read[u->read_count++] = *r;
        }
    }
    return result;
}
static enum fwlab_nfc_api_result rw_take(void *opaque, const struct fwlab_nfc_operation_token *token,
    struct fwlab_nfc_page_v2_result *r, const struct fwlab_nfc_page_v2_output *out)
{
    struct rw_fixture *u = opaque;
    enum fwlab_nfc_api_result result = u->actual.ops->take_result(u->actual.context, token, r, out);
    if (u->observe && result == FWLAB_NFC_API_OK && r->kind == FWLAB_NFC_PAGE_V2_READ_GROUP) {
        CHECK(out && out->main && out->oob && r->read_valid && r->delivered_pages == r->page_count &&
            out->main_bytes == r->page_count * 4096u && out->oob_bytes == r->page_count * 128u);
        for (uint32_t i = 0; i < r->page_count; ++i)
            CHECK((r->page[i].facts_valid & (FWLAB_NFC_PAGE_V2_FACT_CELL | FWLAB_NFC_PAGE_V2_FACT_ECC |
                    FWLAB_NFC_PAGE_V2_FACT_GENERATION)) ==
                  (FWLAB_NFC_PAGE_V2_FACT_CELL | FWLAB_NFC_PAGE_V2_FACT_ECC | FWLAB_NFC_PAGE_V2_FACT_GENERATION) &&
                  r->page[i].valid_region_mask == FWLAB_NFC_REGION_MASK);
        ++u->read_results; u->read_oob_bytes += out->oob_bytes;
    }
    return result;
}
static enum fwlab_nfc_api_result rw_step(void *opaque, uint32_t budget, struct fwlab_nfc_page_v2_step_result *r)
{ struct rw_fixture *u = opaque; return u->actual.ops->step(u->actual.context, budget, r); }
static enum fwlab_nfc_api_result rw_cancel(void *opaque, const struct fwlab_nfc_operation_token *t)
{ struct rw_fixture *u = opaque; return u->actual.ops->cancel(u->actual.context, t); }
static enum fwlab_nfc_api_result rw_reset(void *opaque, uint64_t nonce, uint32_t epoch)
{ struct rw_fixture *u = opaque; return u->actual.ops->reset_begin(u->actual.context, nonce, epoch); }
static enum fwlab_nfc_api_result rw_quiet(void *opaque, uint64_t nonce, uint32_t epoch, bool *out)
{ struct rw_fixture *u = opaque; return u->actual.ops->quiescent(u->actual.context, nonce, epoch, out); }
static const struct fwlab_nfc_page_v2_provider_ops rw_ops = {
    FWLAB_NFC_PAGE_V2_VERSION, sizeof(struct fwlab_nfc_page_v2_provider_ops), 0,
    rw_submit, rw_cancel, rw_step, rw_take, rw_reset, rw_quiet
};
struct fwlab_nfc_page_v2_provider __real_fwlab_nfc_channel_v2_provider(struct fwlab_nfc_channel_v2 *);
struct fwlab_nfc_page_v2_provider __wrap_fwlab_nfc_channel_v2_provider(struct fwlab_nfc_channel_v2 *hub)
{
    struct rw_fixture *u = rw_current;
    CHECK(u && hub);
    u->actual = __real_fwlab_nfc_channel_v2_provider(hub); u->hub = hub;
    u->last_uid = u->retry_uid = 0;
    return (struct fwlab_nfc_page_v2_provider){&rw_ops, u};
}
enum fwlab_spine_result_v0 __real_fwlab_ftl_scale_step(struct fwlab_ftl_scale *, uint32_t, uint32_t *);
enum fwlab_spine_result_v0 __wrap_fwlab_ftl_scale_step(struct fwlab_ftl_scale *f, uint32_t n, uint32_t *used)
{
    enum fwlab_spine_result_v0 result = __real_fwlab_ftl_scale_step(f, n, used);
    struct rw_fixture *u = rw_current;
    if (u && u->workers && u->f.runtime && result == FWLAB_SPINE_V0_OK) {
        const struct fwlab_nfc_channel_v2 *hub = scale_storage_channel_hub(u->f.runtime);
        bool notified;
        CHECK(hub && fwlab_nfc_channel_workers_wait(u->workers, hub, 50, &notified) == FWLAB_NFC_API_OK);
    }
    return result;
}
static void rw_observe(struct fixture *f)
{
    struct rw_fixture *u = rw_current;
    struct fwlab_ftl_scale *ftl = f->runtime->block.context;
    CHECK(u && &u->f == f && ftl->reads && ftl->writes && ftl->parallel_reads && !ftl->read_only);
    if (u->observe && sf_read_pool_busy(ftl)) {
        uint32_t runs = 0;
        for (unsigned i = 0; i < SF_READ_RUNS; ++i) runs += ftl->reads->run[i].state != SF_READ_EMPTY;
        if (u->peak_runs < runs) u->peak_runs = runs;
    }
}
static void rw_idle(struct rw_fixture *u)
{
    wait_idle(&u->f);
    unsigned i;
    for (i = 0; i < STEP_LIMIT && u->f.runtime->active_admissions; ++i) tick(&u->f);
    struct fwlab_ftl_scale *f = u->f.runtime->block.context;
    CHECK(i < STEP_LIMIT && sf_parent_clean_boundary(f) && !f->read_only && f->parallel_reads &&
        f->page_nfc.context == u && f->page_nfc.ops == &rw_ops);
    /* Deliberately no hub live-idle query or manual provider step here. */
}
static const char *rw_name(unsigned i)
{ return i < 4 ? fwlab_nand_channel_volume_shard_name(i) :
    i == 4 ? FWLAB_NAND_CHANNEL_VOLUME_MANIFEST : FWLAB_NAND_CHANNEL_VOLUME_LOCK; }
static void rw_media_open(struct rw_fixture *u, bool format)
{
    size_t bytes = fwlab_nand_channel_volume_arena_size();
    u->volume_arena = aligned_alloc(fwlab_nand_channel_volume_arena_alignment(), bytes); CHECK(u->volume_arena);
    CHECK((format ? fwlab_nand_channel_volume_posix_format(u->volume_arena, bytes, u->f.directory_fd,
        &u->config, &u->volume) : fwlab_nand_channel_volume_posix_restart(u->volume_arena, bytes,
        u->f.directory_fd, u->config.media_uuid, &u->volume)) == FWLAB_NFC_API_OK);
    u->assembly = fwlab_nand_channel_volume_binding(u->volume);
    u->f.media_binding.media = u->assembly.aggregate; u->f.media_binding.geometry = u->assembly.geometry;
    memcpy(u->f.media_binding.media_uuid, u->assembly.media_uuid, 16);
    u->f.options.channel_media = &u->assembly;
    if (format) for (unsigned i = 0; i < RW_FILES; ++i)
        CHECK(fstatat(u->f.directory_fd, rw_name(i), &u->file_identity[i], AT_SYMLINK_NOFOLLOW) == 0);
}
static void rw_media_close(struct rw_fixture *u)
{
    CHECK(!u->f.runtime && fwlab_nand_channel_volume_close(u->volume) == FWLAB_NFC_API_OK);
    free(u->volume_arena); u->volume_arena = NULL; u->volume = NULL;
}
static void rw_workers_start(struct rw_fixture *u)
{
    if (!u->worker_count) return;
    struct fwlab_nfc_channel_workers_config c = {.channels = 4, .workers = 1};
    CHECK(!u->workers && fwlab_nfc_channel_workers_create(&c, &u->workers) == FWLAB_NFC_API_OK);
    u->executor = fwlab_nfc_channel_workers_executor(u->workers);
    u->f.options.channel_executor = &u->executor;
}
static void rw_workers_end(struct rw_fixture *u)
{
    if (!u->worker_count) return;
    struct fwlab_nfc_channel_workers_stats s;
    CHECK(fwlab_nfc_channel_workers_snapshot(u->workers, &s) == FWLAB_NFC_API_OK &&
        s.created_workers == 1 && s.joined_workers == 1 && !s.occupied_mailboxes && !s.failed);
    CHECK(fwlab_nfc_channel_workers_destroy(u->workers) == FWLAB_NFC_API_OK);
    u->workers = NULL; u->f.options.channel_executor = NULL;
}
static struct rw_fixture *rw_create(unsigned workers)
{
    const char *root = getenv("FWLAB_TEST_MEDIA_DIR");
    struct rw_fixture *u = calloc(1, sizeof(*u));
    CHECK(u && !rw_current && workers <= 1 && geteuid() == 1000 && root && root[0] == '/');
    rw_current = u; u->worker_count = workers;
    u->f.started = now_seconds(); u->f.medium_is_tmpfs = 1;
    u->f.use_media_v2 = u->f.use_window_v2 = 1; u->f.lbas = 2048;
    u->config.version = FWLAB_NAND_CHANNEL_VOLUME_VERSION; u->config.size = sizeof(u->config);
    u->config.geometry = (struct fwlab_nfc_geometry){
        .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(struct fwlab_nfc_geometry),
        .channels = 4, .luns_per_channel = 1, .planes_per_lun = 2, .blocks_per_plane = 3,
        .pages_per_block = 64, .plane_parallelism_per_lun = 2, .main_bytes_per_page = 4096,
        .oob_bytes_per_page = 128, .max_programs_per_erase = 1, .program_order = FWLAB_NFC_PROGRAM_ASCENDING
    };
    memcpy(u->config.media_uuid, "D216-RW-J0-V001", 16);
    for (unsigned c = 0; c < 4; ++c) {
        memcpy(u->config.child_uuid[c], "D216-RW-CH-0001", 16); u->config.child_uuid[c][14] = (uint8_t)('1' + c);
        u->timing.read.lun[c].package = u->timing.read.lun[c].die = (uint16_t)c;
    }
    u->f.media_config.geometry = u->config.geometry; memcpy(u->f.media_config.media_uuid, u->config.media_uuid, 16);
    media_preflight(root, 1, &u->f.media_config, 1);
    int n = snprintf(u->f.directory, sizeof(u->f.directory), "%s/fwlab-rw-j0.XXXXXX", root);
    CHECK(n > 0 && (size_t)n < sizeof(u->f.directory) && mkdtemp(u->f.directory));
    u->f.directory_fd = open(u->f.directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(u->f.directory_fd >= 0 && fstat(u->f.directory_fd, &u->directory_identity) == 0);
    u->timing.version = FWLAB_NFC_PAGE_V2_LAB_MUTATION_VERSION; u->timing.size = sizeof(u->timing);
    u->timing.read.version = FWLAB_NFC_PAGE_V2_LAB_VERSION; u->timing.read.size = sizeof(u->timing.read);
    u->timing.read.command_ns = 1000; u->timing.read.array_read_ns = 10000;
    u->timing.read.channel_bytes_per_second = UINT64_C(1000000000);
    u->timing.read.virtual_ns_limit = UINT64_C(10000000000);
    u->timing.program_confirm_ns = u->timing.erase_command_ns = u->timing.status_command_ns = 1000;
    u->timing.array_program_ns = 100000; u->timing.array_erase_ns = 1000000; u->timing.status_response_bytes = 1;
    u->f.options.mapping_slots = 256; u->f.options.mutation_lab_config = &u->timing;
    u->f.options.read_policy = FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE;
    u->f.options.multihead_read_schedule = SCALE_STORAGE_READ_PARALLEL;
    scale_storage_multihead_lab_factory_init(&u->f.factory, &u->f.options);
    printf("UNIFIED_J0_BEGIN|workers=%u|geometry=4ch_1lun_2planes_3blocks_64pages|DATA_domains=2,3|namespace_bytes=1048576|directory=%s|failure_preserves_media=1\n", workers, u->f.directory);
    fflush(stdout);
    rw_media_open(u, true); rw_workers_start(u); runtime_start(&u->f, 1, 0); wait_ready(&u->f); rw_idle(u);
    struct fwlab_ftl_scale *f = u->f.runtime->block.context;
    CHECK(f->disk_format == SF_MULTIHEAD_FORMAT_VERSION && f->reads && f->writes && f->parallel_reads && !f->read_only &&
        f->root.layout.data_first_block == 12 && scale_storage_begin_parallel_read(u->f.runtime) == FWLAB_SPINE_V0_INVALID);
    (void)rw_stats(u); u->f.cut_observer = rw_observe;
    return u;
}
static uint64_t rw_read(struct rw_fixture *u, uint64_t lba, uint32_t count, bool plane_pair)
{
    uint8_t actual[8192];
    struct fwlab_ftl_scale *f = u->f.runtime->block.context;
    struct fwlab_nfc_channel_v2_stats before = rw_stats(u);
    uint64_t children = f->nfc_children, frontier = f->durable_frontier;
    uint64_t hash = u->assembly.aggregate.ops->hash(u->assembly.aggregate.context);
    u->read_count = u->read_results = u->peak_runs = 0; u->read_oob_bytes = 0; u->observe = count == 16;
    command(&u->f, 2, lba, count, NULL, actual, 0, 0); rw_idle(u); u->observe = false;
    CHECK(!memcmp(actual, u->oracle + lba * 512u, count * 512u) && f->durable_frontier == frontier &&
        u->assembly.aggregate.ops->hash(u->assembly.aggregate.context) == hash);
    struct fwlab_nfc_channel_v2_stats after = rw_stats(u);
    if (count == 16) {
        CHECK(u->read_count == 2 && u->read_results == 2 && u->peak_runs == 2 && u->read_oob_bytes == 256 &&
            u->read[0].page_count == 1 && u->read[1].page_count == 1 && f->nfc_children == children + 2 &&
            after.accepted_requests == before.accepted_requests + 2 && after.joined_batches == before.joined_batches + 1);
    }
    if (plane_pair) {
        CHECK(f->map[1].ppa == 897 && f->map[2].ppa == 960 &&
            u->read[0].first.channel == 2 && u->read[1].first.channel == 2 &&
            u->read[0].first.lun == 0 && u->read[1].first.lun == 0 &&
            u->read[0].first.plane == 0 && u->read[1].first.plane == 1);
        CHECK(after.now_ns - before.now_ns == 19448 &&
            after.channel[2].plane_array_busy_ns[0][0] == before.channel[2].plane_array_busy_ns[0][0] + 10000 &&
            after.channel[2].plane_array_busy_ns[0][1] == before.channel[2].plane_array_busy_ns[0][1] + 10000 &&
            after.channel[2].materialized_pages == before.channel[2].materialized_pages + 2);
    }
    return after.now_ns - before.now_ns;
}
static void rw_write(struct rw_fixture *u, uint64_t lba, uint8_t version)
{
    uint8_t data[8192]; fill_pattern(data, lba, version);
    command(&u->f, 1, lba, 16, data, NULL, 1, 0); rw_idle(u);
    memcpy(u->oracle + lba * 512u, data, sizeof(data));
}
static void rw_destroy(struct rw_fixture *u)
{
    u->f.cut_observer = NULL; runtime_close(&u->f); rw_workers_end(u); rw_media_close(u);
    for (unsigned i = 0; i < RW_FILES; ++i) {
        struct stat s;
        CHECK(fstatat(u->f.directory_fd, rw_name(i), &s, AT_SYMLINK_NOFOLLOW) == 0 &&
            s.st_dev == u->file_identity[i].st_dev && s.st_ino == u->file_identity[i].st_ino &&
            s.st_size == u->file_identity[i].st_size && S_ISREG(s.st_mode) && s.st_nlink == 1 && s.st_uid == geteuid());
        CHECK(unlinkat(u->f.directory_fd, rw_name(i), 0) == 0);
    }
    struct stat s;
    CHECK(fstatat(AT_FDCWD, u->f.directory, &s, AT_SYMLINK_NOFOLLOW) == 0 &&
        s.st_dev == u->directory_identity.st_dev && s.st_ino == u->directory_identity.st_ino);
    CHECK(close(u->f.directory_fd) == 0 && rmdir(u->f.directory) == 0);
    free(u); rw_current = NULL;
}
static struct rw_outcome rw_episode(unsigned workers)
{
    struct rw_fixture *u = rw_create(workers);
    struct rw_outcome result = {0};
    for (uint64_t lba = 0; lba < u->f.lbas; lba += 16) rw_write(u, lba, 0x31);
    struct fwlab_ftl_scale *f = u->f.runtime->block.context;
    CHECK(f->durable_frontier == 128 && f->blocks[12].disk.allocation_end == 64 &&
        f->blocks[13].disk.allocation_end == 64 && f->blocks[18].disk.allocation_end == 64 &&
        f->blocks[19].disk.allocation_end == 64);
    (void)rw_read(u, 8, 16, false);
    uint64_t ack = u->ack_backpressure;
    rw_write(u, 7, 0x52);
    CHECK(u->ack_backpressure > ack && f->map[0].ppa == 896 && f->map[1].ppa == 897 && f->map[2].ppa == 1280);
    command(&u->f, 0, 0, 0, NULL, NULL, 0, 0); rw_idle(u);
    CHECK(f->durable_frontier == 129);
    (void)rw_read(u, 8, 16, false);
    (void)rw_read(u, 0, 8, false);  /* LBA0..6 old, LBA7 replacement. */
    (void)rw_read(u, 16, 8, false); /* LBA16..22 replacement, LBA23 old. */
    uint64_t gc = f->garbage_collections;
    CHECK(scale_storage_gc(u->f.runtime, 1) == FWLAB_SPINE_V0_OK); rw_idle(u);
    CHECK(f->garbage_collections == gc + 1 && f->durable_frontier == 129 &&
        f->map[2].ppa == 960 && f->blocks[20].disk.role == SF_FREE);
    uint64_t cp = f->checkpoints;
    CHECK(scale_storage_checkpoint(u->f.runtime) == FWLAB_SPINE_V0_OK); rw_idle(u);
    CHECK(f->checkpoints == cp + 1);
    result.plane_ns = rw_read(u, 8, 16, true);
    result.gc_hash = u->assembly.aggregate.ops->hash(u->assembly.aggregate.context);
    (void)rw_read(u, 0, 8, false); (void)rw_read(u, 16, 8, false);
    rw_write(u, 64, 0x63);
    command(&u->f, 0, 0, 0, NULL, NULL, 0, 0); rw_idle(u);
    (void)rw_read(u, 64, 16, false);
    CHECK(f->durable_frontier == 130);
    u->f.cut_observer = NULL; runtime_close(&u->f); rw_workers_end(u); rw_media_close(u);
    rw_media_open(u, false); rw_workers_start(u); runtime_start(&u->f, 0, u->f.lbas); wait_ready(&u->f); rw_idle(u);
    f = u->f.runtime->block.context; u->f.cut_observer = rw_observe;
    CHECK(f->disk_format == SF_MULTIHEAD_FORMAT_VERSION && f->durable_frontier == 130 &&
        f->parallel_reads && !f->read_only && f->reads && f->writes && sf_heads_empty(f));
    CHECK(rw_read(u, 8, 16, true) == result.plane_ns);
    (void)rw_read(u, 0, 8, false); (void)rw_read(u, 16, 8, false); (void)rw_read(u, 64, 16, false);
    rw_write(u, 96, 0x74);
    command(&u->f, 0, 0, 0, NULL, NULL, 0, 0); rw_idle(u);
    (void)rw_read(u, 96, 16, false);
    CHECK(f->durable_frontier == 131 && u->ack_backpressure);
    result.frontier = f->durable_frontier;
    result.final_hash = u->assembly.aggregate.ops->hash(u->assembly.aggregate.context);
    printf("UNIFIED_J0_EPISODE_PASS|workers=%u|format=3|ordinary_fill_8KiB=128|same_instance_mutable=1|RMW_neighbors_0to6_and23=exact|GC20to15=1|plane_PPA=897,960|model_ns=%llu|CP=1|recovery_continued_WRITE=1|frontier=%llu|normal_ACK_BP=%llu|manual_ACK_drain=0\n",
        workers, (unsigned long long)result.plane_ns, (unsigned long long)result.frontier,
        (unsigned long long)u->ack_backpressure);
    rw_destroy(u); return result;
}
int main(void)
{
    struct rw_outcome cooperative = rw_episode(0), worker = rw_episode(1);
    CHECK(cooperative.gc_hash && cooperative.gc_hash == worker.gc_hash &&
        cooperative.final_hash == worker.final_hash && cooperative.frontier == worker.frontier &&
        cooperative.plane_ns == worker.plane_ns);
    struct rusage usage;
    CHECK(getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss < 256 * 1024);
    printf("UNIFIED_J0_PASS|cooperative_and_one_worker=matched|actual_media_model_hashes_equal=1|all_owned_files_verified_cleanup=1|peak_RSS_KiB=%ld|not_native_NUMA_vendor_or_bandwidth=1\n", usage.ru_maxrss);
    return 0;
}
