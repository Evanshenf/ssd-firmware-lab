/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* D215: existing J0 commands/controller buffer and unchanged format2 read pool.
 * Only the two fixed policy episodes below run; no old capacity-test entry. */
#define main(...) retained_plane_scale_fixture_entry(__VA_ARGS__)
#include "test_ftl.c"
#undef main
#include "channel_volume.h"
#include <sys/resource.h>

struct plane_fixture {
    struct fixture f;
    struct fwlab_nand_channel_volume_config config;
    struct fwlab_nand_channel_volume *volume;
    struct fwlab_nand_channel_v2 assembly;
    struct fwlab_nfc_page_v2_lab_mutation_config timing;
    struct j0_storage_factory underlying;
    struct fwlab_controller_buffer_port_v0 buffer;
    struct fwlab_controller_buffer_ops_v0 buffer_ops;
    struct stat directory_identity, file_identity[3];
    void *volume_arena;
    uint32_t published, publications, peak_runs;
    uint8_t observe;
};
struct plane_result { uint64_t prepared_hash, recovered_hash, different_ns, same_ns, frontier; };
static struct plane_fixture *active_plane;

static enum fwlab_controller_buffer_result_v0 plane_publish(void *context,
    const struct fwlab_controller_buffer_lease_v0 *lease,
    const struct fwlab_controller_buffer_span_v0 *span, const void *bytes, size_t count)
{
    struct plane_fixture *p = active_plane;
    CHECK(p && context == p->buffer.context);
    if (p->observe) CHECK(span->offset == p->published && span->length == count && count == 4096);
    enum fwlab_controller_buffer_result_v0 result = p->buffer.ops->write(context, lease, span, bytes, count);
    if (p->observe && result == FWLAB_CONTROLLER_BUFFER_V0_OK) {
        p->published += (uint32_t)count; ++p->publications;
    }
    return result;
}
static enum fwlab_spine_result_v0 plane_bind(void *opaque,
    const struct j0_runtime_config *config, const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_block_namespace_ref_v0 *namespace_ref, uint64_t lifecycle_nonce,
    uint64_t ftl_nonce, uint64_t nfc_nonce, struct j0_storage_runner *runner, struct fwlab_block_service_v0 *service)
{
    struct plane_fixture *p = opaque;
    struct fwlab_controller_buffer_port_v0 observed = *buffer;
    p->buffer = *buffer; p->buffer_ops = *buffer->ops;
    p->buffer_ops.write = plane_publish; observed.ops = &p->buffer_ops;
    return p->underlying.bind(p->underlying.context, config, &observed, namespace_ref,
        lifecycle_nonce, ftl_nonce, nfc_nonce, runner, service);
}
static void plane_observe(struct fixture *f)
{
    struct plane_fixture *p = active_plane;
    struct fwlab_ftl_scale *ftl = f->runtime->block.context;
    uint32_t runs = 0;
    CHECK(p && f == &p->f && ftl->reads && !ftl->writes);
    if (!p->observe || !sf_read_pool_busy(ftl)) return;
    for (unsigned i = 0; i < SF_READ_RUNS; ++i) runs += ftl->reads->run[i].state != SF_READ_EMPTY;
    if (runs > p->peak_runs) p->peak_runs = runs;
}
static struct fwlab_nfc_channel_v2_stats plane_stats(struct plane_fixture *p)
{
    struct fwlab_nfc_channel_v2_stats s;
    CHECK(scale_storage_channel_snapshot(p->f.runtime, &s) == FWLAB_SPINE_V0_OK &&
        !s.quarantined && !s.poisoned && !s.counters_saturated &&
        !s.channel[0].quarantined && !s.channel[0].counters_saturated);
    return s;
}
static uint64_t plane_media_sequence(const struct plane_fixture *p)
{ return fwlab_file_nand_v2_sequence(p->assembly.channel[0].scalar.context); }
static const char *plane_name(unsigned i)
{
    return i == 0 ? fwlab_nand_channel_volume_shard_name(0) :
        i == 1 ? FWLAB_NAND_CHANNEL_VOLUME_MANIFEST : FWLAB_NAND_CHANNEL_VOLUME_LOCK;
}
static void plane_media_open(struct plane_fixture *p, bool format)
{
    size_t bytes = fwlab_nand_channel_volume_arena_size();
    p->volume_arena = aligned_alloc(fwlab_nand_channel_volume_arena_alignment(), bytes);
    CHECK(p->volume_arena);
    CHECK((format ? fwlab_nand_channel_volume_posix_format(p->volume_arena, bytes, p->f.directory_fd,
        &p->config, &p->volume) : fwlab_nand_channel_volume_posix_restart(p->volume_arena, bytes,
        p->f.directory_fd, p->config.media_uuid, &p->volume)) == FWLAB_NFC_API_OK);
    p->assembly = fwlab_nand_channel_volume_binding(p->volume);
    CHECK(p->assembly.aggregate.ops && p->assembly.geometry.channels == 1);
    p->f.media_binding.media = p->assembly.aggregate; p->f.media_binding.geometry = p->assembly.geometry;
    memcpy(p->f.media_binding.media_uuid, p->assembly.media_uuid, 16);
    p->f.options.channel_media = &p->assembly;
    if (format) for (unsigned i = 0; i < 3; ++i)
        CHECK(fstatat(p->f.directory_fd, plane_name(i), &p->file_identity[i], AT_SYMLINK_NOFOLLOW) == 0);
}
static void plane_media_close(struct plane_fixture *p)
{
    CHECK(!p->f.runtime && fwlab_nand_channel_volume_close(p->volume) == FWLAB_NFC_API_OK);
    free(p->volume_arena); p->volume_arena = NULL; p->volume = NULL;
}
static struct plane_fixture *plane_create(enum fwlab_nfc_page_v2_lab_read_policy policy)
{
    const char *root = getenv("FWLAB_TEST_MEDIA_DIR");
    struct plane_fixture *p = calloc(1, sizeof(*p));
    int n;
    CHECK(p && !active_plane && geteuid() == 1000 && root && root[0] == '/');
    active_plane = p;
    p->f.started = now_seconds(); p->f.medium_is_tmpfs = 1;
    p->f.use_media_v2 = p->f.use_window_v2 = 1; p->f.lbas = 2048;
    p->config.version = FWLAB_NAND_CHANNEL_VOLUME_VERSION; p->config.size = sizeof(p->config);
    p->config.geometry = (struct fwlab_nfc_geometry){
        .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(struct fwlab_nfc_geometry),
        .channels = 1, .luns_per_channel = 1, .planes_per_lun = 2, .blocks_per_plane = 16,
        .pages_per_block = 64, .plane_parallelism_per_lun = 2, .main_bytes_per_page = 4096,
        .oob_bytes_per_page = 128, .max_programs_per_erase = 1, .program_order = FWLAB_NFC_PROGRAM_ASCENDING
    };
    memcpy(p->config.media_uuid, "D215-PLANE-J001", 16);
    memcpy(p->config.child_uuid[0], "D215-PLANE-C001", 16);
    p->f.media_config.geometry = p->config.geometry;
    memcpy(p->f.media_config.media_uuid, p->config.media_uuid, 16);
    media_preflight(root, 1, &p->f.media_config, 1);
    n = snprintf(p->f.directory, sizeof(p->f.directory), "%s/fwlab-plane-j0.XXXXXX", root);
    CHECK(n > 0 && (size_t)n < sizeof(p->f.directory) && mkdtemp(p->f.directory));
    p->f.directory_fd = open(p->f.directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(p->f.directory_fd >= 0 && fstat(p->f.directory_fd, &p->directory_identity) == 0);
    p->timing.version = FWLAB_NFC_PAGE_V2_LAB_MUTATION_VERSION; p->timing.size = sizeof(p->timing);
    p->timing.read.version = FWLAB_NFC_PAGE_V2_LAB_VERSION; p->timing.read.size = sizeof(p->timing.read);
    p->timing.read.command_ns = 1000; p->timing.read.array_read_ns = 10000;
    p->timing.read.channel_bytes_per_second = UINT64_C(1000000000);
    p->timing.read.virtual_ns_limit = UINT64_C(10000000000);
    p->timing.program_confirm_ns = p->timing.erase_command_ns = p->timing.status_command_ns = 1000;
    p->timing.array_program_ns = 100000; p->timing.array_erase_ns = 1000000; p->timing.status_response_bytes = 1;
    p->f.options.mapping_slots = 256; p->f.options.mutation_lab_config = &p->timing; p->f.options.read_policy = policy;
    scale_storage_parallel_channel_lab_factory_init(&p->underlying, &p->f.options);
    p->f.factory = (struct j0_storage_factory){ .bind = plane_bind, .context = p };
    printf("PLANE_J0_BEGIN|policy=%u|geometry=1ch_1lun_2planes_16blocks_64pages|parallelism=2|namespace_bytes=1048576|directory=%s|failure_preserves_media=1\n",
        (unsigned)policy, p->f.directory);
    fflush(stdout);
    plane_media_open(p, true); runtime_start(&p->f, 1, 0); wait_ready(&p->f);
    struct fwlab_ftl_scale *f = p->f.runtime->block.context;
    CHECK(f->disk_format == SF_WINDOW_FORMAT_VERSION && f->reads && !f->writes && !f->read_only &&
        f->root.layout.data_first_block == 12 && plane_stats(p).channel[0].read_policy == (uint32_t)policy);
    p->f.cut_observer = plane_observe;
    return p;
}
static void plane_upper_idle(struct plane_fixture *p)
{
    wait_idle(&p->f);
    unsigned i;
    for (i = 0; i < 10000 && p->f.runtime->active_admissions; ++i) tick(&p->f);
    CHECK(i < 10000 && sf_parent_clean_boundary(p->f.runtime->block.context));
}
static void plane_begin_read_only(struct plane_fixture *p)
{
    struct fwlab_ftl_scale *f = p->f.runtime->block.context;
    unsigned retries = 0;
    enum fwlab_spine_result_v0 result;
    plane_upper_idle(p);
    uint64_t uid = f->io.next_uid, record = f->record_sequence, sequence = plane_media_sequence(p);
    struct fwlab_nfc_page_v2_provider provider = f->page_nfc;
    do {
        result = scale_storage_begin_parallel_read(p->f.runtime);
        CHECK(result == FWLAB_SPINE_V0_OK || result == FWLAB_SPINE_V0_IN_PROGRESS);
        CHECK(++retries < 10000);
    } while (result == FWLAB_SPINE_V0_IN_PROGRESS);
    bool idle = false;
    CHECK(f->read_only && f->io.next_uid == uid && f->record_sequence == record &&
        plane_media_sequence(p) == sequence && f->page_nfc.ops == provider.ops && f->page_nfc.context == provider.context &&
        fwlab_nfc_channel_v2_live_idle(scale_storage_channel_hub(p->f.runtime), &idle) == FWLAB_NFC_API_OK && idle);
    printf("PLANE_J0_READ_ONLY|policy=%u|transition_calls=%u|same_provider_UID_media_sequence=1|frontier=%llu\n",
        (unsigned)p->f.options.read_policy, retries, (unsigned long long)f->durable_frontier);
}
static void plane_drain_retirement(struct plane_fixture *p)
{
    struct fwlab_ftl_scale *f = p->f.runtime->block.context;
    bool idle = false;
    unsigned i;
    plane_upper_idle(p);
    for (i = 0; i < 10000; ++i) {
        CHECK(fwlab_nfc_channel_v2_live_idle(scale_storage_channel_hub(p->f.runtime), &idle) == FWLAB_NFC_API_OK);
        if (idle) break;
        struct fwlab_nfc_page_v2_step_result step = {0};
        CHECK(f->page_nfc.ops->step(f->page_nfc.context, 1, &step) == FWLAB_NFC_API_OK && step.units_used <= 1);
    }
    CHECK(i < 10000);
}
static void plane_verify_mapping(struct plane_fixture *p)
{
    const struct fwlab_ftl_scale *f = p->f.runtime->block.context;
    CHECK(f->map[0].ppa == 1024 && f->map[1].ppa == 769 && f->map[63].ppa == 831 && f->map[64].ppa == 832);
    CHECK(sf_ppa(f, f->map[0].ppa).plane == 1 && sf_ppa(f, f->map[1].ppa).plane == 0 &&
        sf_ppa(f, f->map[63].ppa).plane == 0 && sf_ppa(f, f->map[64].ppa).plane == 0);
}
static uint64_t plane_read(struct plane_fixture *p, bool different)
{
    struct fwlab_ftl_scale *f = p->f.runtime->block.context;
    uint8_t expected[8192], actual[8192], overwritten[8192];
    uint64_t lba = different ? 0 : 504;
    plane_drain_retirement(p);
    struct fwlab_nfc_channel_v2_stats before = plane_stats(p);
    uint64_t children = f->nfc_children, sequence = plane_media_sequence(p), frontier = f->durable_frontier;
    uint64_t hash = p->assembly.aggregate.ops->hash(p->assembly.aggregate.context);
    fill_pattern(expected, lba, 0x31);
    if (different) { fill_pattern(overwritten, 0, 0x52); memcpy(expected, overwritten, 4096); }
    p->published = p->publications = p->peak_runs = 0; p->observe = 1;
    command(&p->f, 2, lba, 16, NULL, actual, 0, 0);
    plane_upper_idle(p); p->observe = 0;
    struct fwlab_nfc_channel_v2_stats after = plane_stats(p);
    uint64_t elapsed = after.now_ns - before.now_ns;
    CHECK(!memcmp(actual, expected, sizeof(actual)) && p->published == 8192 && p->publications == 2 && p->peak_runs == 2);
    CHECK(f->nfc_children == children + 2 && f->durable_frontier == frontier && plane_media_sequence(p) == sequence &&
        after.accepted_requests == before.accepted_requests + 2 && after.joined_batches == before.joined_batches + 1);
    CHECK(after.channel[0].accepted_reads == before.channel[0].accepted_reads + 2 &&
        after.channel[0].materialized_pages == before.channel[0].materialized_pages + 2 &&
        after.channel[0].data_out_main_bytes == before.channel[0].data_out_main_bytes + 8192 &&
        after.channel[0].data_out_oob_bytes == before.channel[0].data_out_oob_bytes + 256 &&
        after.channel[0].channel_busy_ns[0] == before.channel[0].channel_busy_ns[0] + 10448 &&
        after.channel[0].array_busy_ns[0] == before.channel[0].array_busy_ns[0] + 20000 &&
        !after.channel[0].held_read_planes && !after.channel[0].active_read_arrays && !after.channel[0].held_luns);
    CHECK(after.channel[0].plane_array_busy_ns[0][0] == before.channel[0].plane_array_busy_ns[0][0] + (different ? 10000 : 20000) &&
        after.channel[0].plane_array_busy_ns[0][1] == before.channel[0].plane_array_busy_ns[0][1] + (different ? 10000 : 0));
    CHECK(elapsed == (different && p->f.options.read_policy == FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE ? 19448u : 30448u));
    plane_drain_retirement(p);
    after = plane_stats(p);
    CHECK(!after.occupied_credits && !after.retirement_pending &&
        after.retired_acks == before.retired_acks + 2 &&
        p->assembly.aggregate.ops->hash(p->assembly.aggregate.context) == hash);
    printf("PLANE_J0_READ|policy=%u|case=%s|actual_PPA=%u,%u|READ_runs=2|pages=2|main_bytes=8192|OOB_bytes=256|contiguous_publications=2|model_ns=%llu|media_frontier_unchanged=1\n",
        (unsigned)p->f.options.read_policy, different ? "different_plane" : "same_plane", f->map[lba / 8].ppa,
        f->map[lba / 8 + 1].ppa, (unsigned long long)elapsed);
    return elapsed;
}
static void plane_destroy(struct plane_fixture *p)
{
    uint64_t sequence = plane_media_sequence(p);
    p->f.cut_observer = NULL;
    runtime_close(&p->f); CHECK(plane_media_sequence(p) == sequence);
    plane_media_close(p);
    for (unsigned i = 0; i < 3; ++i) {
        struct stat s;
        CHECK(fstatat(p->f.directory_fd, plane_name(i), &s, AT_SYMLINK_NOFOLLOW) == 0 &&
            s.st_dev == p->file_identity[i].st_dev && s.st_ino == p->file_identity[i].st_ino &&
            s.st_size == p->file_identity[i].st_size && S_ISREG(s.st_mode) && s.st_nlink == 1 && s.st_uid == geteuid());
        CHECK(unlinkat(p->f.directory_fd, plane_name(i), 0) == 0);
    }
    struct stat s;
    CHECK(fstatat(AT_FDCWD, p->f.directory, &s, AT_SYMLINK_NOFOLLOW) == 0 &&
        s.st_dev == p->directory_identity.st_dev && s.st_ino == p->directory_identity.st_ino);
    CHECK(close(p->f.directory_fd) == 0 && rmdir(p->f.directory) == 0);
    free(p); active_plane = NULL;
}
static struct plane_result plane_episode(enum fwlab_nfc_page_v2_lab_read_policy policy)
{
    struct plane_fixture *p = plane_create(policy);
    struct plane_result result = {0};
    uint8_t data[8192];
    for (uint64_t lba = 0; lba < p->f.lbas; lba += 16) {
        fill_pattern(data, lba, 0x31); command(&p->f, 1, lba, 16, data, NULL, 0, 0);
    }
    command(&p->f, 0, 0, 0, NULL, NULL, 0, 0);
    fill_pattern(data, 0, 0x52); command(&p->f, 1, 0, 8, data, NULL, 1, 0);
    plane_begin_read_only(p); plane_verify_mapping(p);
    struct fwlab_nfc_channel_v2_stats prepared = plane_stats(p);
    CHECK(prepared.channel[0].trace_count == FWLAB_NFC_PAGE_V2_LAB_TRACE_CAPACITY && prepared.channel[0].trace_dropped);
    result.prepared_hash = p->assembly.aggregate.ops->hash(p->assembly.aggregate.context);
    result.different_ns = plane_read(p, true); result.same_ns = plane_read(p, false);
    struct fwlab_ftl_scale *f = p->f.runtime->block.context;
    CHECK(f->durable_frontier == 129); result.frontier = f->durable_frontier;
    uint64_t sequence = plane_media_sequence(p);
    p->f.cut_observer = NULL; runtime_close(&p->f); CHECK(plane_media_sequence(p) == sequence);
    plane_media_close(p); plane_media_open(p, false);
    runtime_start(&p->f, 0, p->f.lbas); wait_ready(&p->f); p->f.cut_observer = plane_observe;
    plane_begin_read_only(p); plane_verify_mapping(p);
    f = p->f.runtime->block.context;
    CHECK(f->disk_format == SF_WINDOW_FORMAT_VERSION && f->reads && !f->writes && f->durable_frontier == result.frontier);
    result.recovered_hash = p->assembly.aggregate.ops->hash(p->assembly.aggregate.context);
    CHECK(plane_read(p, true) == result.different_ns && plane_read(p, false) == result.same_ns);
    printf("PLANE_J0_EPISODE_PASS|policy=%u|ordinary_fill_writes=128|overwrite_LPN0=1|format=2|frontier=129|same_format_reopen_exact=1|trace_not_reset=1\n", (unsigned)policy);
    plane_destroy(p); return result;
}
int main(void)
{
    struct plane_result exclusive = plane_episode(FWLAB_NFC_PAGE_V2_LAB_LUN_EXCLUSIVE);
    struct plane_result independent = plane_episode(FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE);
    struct rusage usage;
    CHECK(exclusive.prepared_hash && exclusive.prepared_hash == independent.prepared_hash &&
        exclusive.recovered_hash == independent.recovered_hash && exclusive.frontier == independent.frontier &&
        exclusive.same_ns == independent.same_ns && independent.different_ns < exclusive.different_ns);
    CHECK(getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss < 256 * 1024);
    printf("PLANE_J0_PASS|policies=2|physical_model_hashes_equal=1|all_owned_files_closed_and_verified_cleanup=1|peak_RSS_KiB=%ld|RSS_budget_KiB=262144|not_native_workers_16KiB_4TB_or_bandwidth=1\n", usage.ru_maxrss);
    return 0;
}
