/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* D210 group5: existing Linux profile/lifecycle/serial format2 on timed NAND.
 * Reuse the existing fixture, including its strict close/holder checks. */
#define main existing_scale_fixture_main
#include "test_ftl.c"
#undef main

struct mutation_fixture {
    struct fixture f;
    struct fwlab_nfc_page_v2_lab_mutation_config timing;
    struct fwlab_block_op_token_v0 closing_token;
    struct fwlab_block_status_v0 closing_result;
    int closing_seen;
};
static struct mutation_fixture *active_mutation;

static struct fwlab_nfc_page_v2_lab_stats mutation_stats(struct fixture *f)
{
    struct fwlab_nfc_page_v2_lab_stats s;
    CHECK(scale_storage_lab_snapshot(f->runtime, &s) == FWLAB_SPINE_V0_OK);
    CHECK(s.phase == FWLAB_NFC_PAGE_V2_LAB_TIMED_RW && !s.quarantined && !s.counters_saturated);
    /* The bounded trace may fill during timed startup. These checks use exact
     * counters and existing FTL state, not discarded trace or a reset clock. */
    return s;
}
static void mutation_tick(struct fixture *f)
{
    uint32_t used;
    CHECK(j0_runtime_step(f->runtime, 1, &used) == FWLAB_SPINE_V0_OK && used == 1);
}
static struct mutation_fixture *mutation_create(void)
{
    struct mutation_fixture *m = calloc(1, sizeof(*m));
    struct fixture *f;
    struct fwlab_nfc_page_v2_lab_config *r;
    const char *root = getenv("FWLAB_TEST_MEDIA_DIR");
    int n;
    CHECK(m && !active_mutation && root && root[0] == '/');
    f = &m->f; active_mutation = m;
    f->started = now_seconds(); f->lbas = 2048; f->use_media_v2 = f->use_window_v2 = 1;
    f->media_config.geometry = (struct fwlab_nfc_geometry){
        .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(struct fwlab_nfc_geometry),
        .channels = 2, .luns_per_channel = 1, .planes_per_lun = 1,
        .blocks_per_plane = 16, .pages_per_block = 64, .plane_parallelism_per_lun = 1,
        .main_bytes_per_page = 4096, .oob_bytes_per_page = 128,
        .max_programs_per_erase = 1, .program_order = FWLAB_NFC_PROGRAM_ASCENDING
    };
    memcpy(f->media_config.media_uuid, "N2-J0-NAND-00001", 16);
    media_preflight(root, 1, &f->media_config, 1);
    n = snprintf(f->directory, sizeof(f->directory), "%s/fwlab-n2-j0.XXXXXX", root);
    CHECK(n > 0 && (size_t)n < sizeof(f->directory) && mkdtemp(f->directory));
    f->directory_fd = open(f->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(f->directory_fd >= 0);
    m->timing.version = FWLAB_NFC_PAGE_V2_LAB_MUTATION_VERSION;
    m->timing.size = sizeof(m->timing); r = &m->timing.read;
    r->version = FWLAB_NFC_PAGE_V2_LAB_VERSION; r->size = sizeof(*r);
    r->command_ns = 1000; r->array_read_ns = 10000;
    r->channel_bytes_per_second = UINT64_C(1000000000);
    r->virtual_ns_limit = UINT64_C(10000000000);
    r->lun[1].package = r->lun[1].die = 1;
    /* Explicit synthetic fixture costs, NOT vendor/default production values. */
    m->timing.program_confirm_ns = m->timing.erase_command_ns = 1000;
    m->timing.array_program_ns = 100000; m->timing.array_erase_ns = 1000000;
    m->timing.status_command_ns = 1000; m->timing.status_response_bytes = 1;
    f->options.mapping_slots = 256; f->options.mutation_lab_config = &m->timing;
    scale_storage_mutation_lab_factory_init(&f->factory, &f->options);
    printf("N2_J0_BEGIN|medium=local_tmpfs|serial_format2=1|directory=%s\n", f->directory);
    fflush(stdout);
    media_open(f, 1); runtime_start(f, 1, 0); wait_ready(f); identify(f); wait_idle(f);
    struct fwlab_ftl_scale *ftl = f->runtime->block.context;
    CHECK(!ftl->read_only && mutation_stats(f).now_ns > 0);
    CHECK(scale_storage_begin_timed_read(f->runtime) == FWLAB_SPINE_V0_INVALID && !ftl->read_only);
    return m;
}
static void mutation_destroy(struct mutation_fixture *m)
{
    struct fixture *f = &m->f;
    struct stat image;
    if (f->runtime) runtime_close(f);
    media_close(f);
    CHECK(fstatat(f->directory_fd, "nand.bin", &image, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISREG(image.st_mode) && image.st_nlink == 1 &&
        (uint64_t)image.st_dev == f->holder_v2.device && (uint64_t)image.st_ino == f->holder_v2.inode);
    CHECK(unlinkat(f->directory_fd, "nand.bin", 0) == 0 && close(f->directory_fd) == 0 && rmdir(f->directory) == 0);
    free(m); active_mutation = NULL;
}
static void mutation_write(struct fixture *f, uint64_t lba, uint32_t lbas, uint8_t seed, int fua)
{
    uint8_t bytes[8192]; fill_pattern(bytes, lba, seed);
    command(f, 1, lba, lbas, bytes, NULL, fua, 0); wait_idle(f);
}
static void mutation_read(struct fixture *f, uint64_t lba, uint32_t lbas, uint8_t *out)
{ command(f, 2, lba, lbas, NULL, out, 0, 0); wait_idle(f); }

static void rmw_durability_reopen(void)
{
    struct mutation_fixture *m = mutation_create(); struct fixture *f = &m->f;
    struct fwlab_nfc_page_v2_lab_stats before = mutation_stats(f), after;
    uint8_t actual[8192], expected[8192], replacement[8192];
    mutation_write(f, 0, 16, 0x31, 0); mutation_write(f, 16, 16, 0x31, 0);
    mutation_write(f, 7, 16, 0x72, 1); /* partial head/tail with intervening full page */
    command(f, 0, 0, 0, NULL, NULL, 0, 0); wait_idle(f);
    struct fwlab_ftl_scale *ftl = f->runtime->block.context;
    CHECK(ftl->durable_frontier == 3 &&
        ftl->parent.retired.durability_witness == FWLAB_BLOCK_V0_WITNESS_FRONTIER_DURABLE);
    after = mutation_stats(f);
    CHECK(after.now_ns > before.now_ns && after.successful_program_pages > before.successful_program_pages &&
        after.materialized_pages > before.materialized_pages && !after.active_slots && !after.held_luns && !after.busy_channels);
    mutation_read(f, 0, 16, actual); fill_pattern(expected, 0, 0x31); fill_pattern(replacement, 7, 0x72);
    memcpy(expected + 7u * 512u, replacement, 9u * 512u); CHECK(!memcmp(actual, expected, sizeof(actual)));
    mutation_read(f, 16, 16, actual); fill_pattern(expected, 16, 0x31);
    memcpy(expected, replacement + 9u * 512u, 7u * 512u); CHECK(!memcmp(actual, expected, sizeof(actual)));
    uint64_t incarnation = f->incarnation;
    runtime_close(f); media_close(f); media_open(f, 0); runtime_start(f, 0, f->lbas); wait_ready(f);
    CHECK(f->incarnation > incarnation && ((struct fwlab_ftl_scale *)f->runtime->block.context)->durable_frontier == 3);
    mutation_read(f, 16, 16, actual); CHECK(!memcmp(actual, expected, sizeof(actual)));
    mutation_read(f, 7, 16, actual); CHECK(!memcmp(actual, replacement, sizeof(actual)));
    printf("N2_J0_RMW|SELF_FUA_Flush=1|main_OOB_validated=1|same_format_reopen=1|model_ns=%llu|not_bandwidth=1\n",
        (unsigned long long)(after.now_ns - before.now_ns));
    mutation_destroy(m);
}

static struct fwlab_spine_command_ticket_v0 mutation_admit(struct fixture *f, const uint8_t *input)
{
    struct fwlab_nvme_command c = {0}; struct j0_host_transfer t = {0};
    struct fwlab_spine_command_ticket_v0 ticket;
    c.version = FWLAB_NVME_COMMAND_VERSION; c.size = sizeof(c);
    c.handle.instance_nonce = UINT64_C(0x53464232484f0000) + f->incarnation;
    c.handle.command_uid = ++f->uid; c.handle.controller_epoch = c.handle.generation = 1;
    c.origin.word[0] = c.handle.instance_nonce ^ UINT64_C(0x4f52494700000000);
    c.origin.word[1] = f->uid; c.trace_cookie = f->uid; c.safety_generation = 1;
    c.namespace_id = 1; c.opcode = 1; c.queue_class = FWLAB_NVME_QUEUE_IO;
    c.data_pointer_format = FWLAB_NVME_DATA_POINTER_PRP; c.data_address_present = 1;
    c.command_dword10_15[2] = 15; /* LBA0, 8KiB, legitimate Linux-profile shape */
    t.version = J0_RUNTIME_VERSION; t.size = sizeof(t); t.exact_bytes = 8192;
    t.input = input; t.direction = FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER;
    CHECK(j0_runtime_admit_start(f->runtime, J0_PROFILE_LINUX_V1, &c, &t, &ticket) == FWLAB_SPINE_V0_OK);
    return ticket;
}
static void watch_close(struct fixture *f)
{
    struct mutation_fixture *m = active_mutation;
    struct fwlab_ftl_scale *ftl = f->runtime->block.context;
    CHECK(m && f == &m->f);
    if (!memcmp(&ftl->parent.request.operation_token, &m->closing_token, sizeof(m->closing_token)) &&
        (ftl->parent.status.state == FWLAB_BLOCK_V0_STATE_TERMINAL ||
         ftl->parent.status.state == FWLAB_BLOCK_V0_STATE_DRAINING ||
         ftl->parent.status.state == FWLAB_BLOCK_V0_STATE_RETIRED)) {
        m->closing_result = ftl->parent.status; m->closing_seen = 1;
    }
}
static void close_started_group(int multi_group)
{
    struct mutation_fixture *m = mutation_create(); struct fixture *f = &m->f;
    struct fwlab_ftl_scale *ftl = f->runtime->block.context;
    uint8_t bytes[8192], actual[8192], expected[8192];
    if (multi_group) {
        for (uint64_t lba = 0; lba < 62u * 8u; lba += 16) mutation_write(f, lba, 16, 0x31, 0);
        mutation_write(f, 62u * 8u, 8, 0x31, 0);
        CHECK(ftl->host_head != SF_NONE && ftl->blocks[ftl->host_head].disk.allocation_end == 63);
    } else mutation_write(f, 0, 16, 0x31, 0);
    uint64_t frontier = ftl->durable_frontier, confirmed = 0, target_uid = 0;
    unsigned guard = 0;
    fill_pattern(bytes, 0, 0x72); (void)mutation_admit(f, bytes);
    while (!target_uid) {
        CHECK(++guard < 100000); mutation_tick(f);
        if (ftl->io.lower_owned && ftl->io.cancel_allowed && ftl->io.result.kind == SF_IO_PROGRAM) {
            target_uid = ftl->io.page_request.operation.operation_uid;
            confirmed = mutation_stats(f).confirmed_program_groups;
            CHECK(ftl->work.kind == SF_WORK_HOST && ftl->io.result.count == (multi_group ? 1u : 2u));
        }
    }
    while (mutation_stats(f).confirmed_program_groups == confirmed) {
        CHECK(++guard < 100000); mutation_tick(f);
    }
    CHECK(ftl->io.page_request.operation.operation_uid == target_uid && ftl->io.lower_owned);
    m->closing_token = ftl->parent.request.operation_token; f->cut_observer = watch_close;
    runtime_close(f); f->cut_observer = NULL; /* existing all-zero Host/internal certificate */
    CHECK(m->closing_seen && m->closing_result.completed_lbas == (multi_group ? 8u : 16u));
    CHECK(m->closing_result.outcome == (multi_group ? FWLAB_BLOCK_V0_CANCELLED : FWLAB_BLOCK_V0_SUCCEEDED));
    CHECK(m->closing_result.effect == (multi_group ? FWLAB_BLOCK_V0_EFFECT_EXACT_PREFIX : FWLAB_BLOCK_V0_EFFECT_FULL));
    CHECK(m->closing_result.durability_witness == (multi_group ? FWLAB_BLOCK_V0_WITNESS_NONE : FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE));
    media_close(f); media_open(f, 0); runtime_start(f, 0, f->lbas); wait_ready(f);
    ftl = f->runtime->block.context;
    CHECK(ftl->durable_frontier == frontier + (uint64_t)!multi_group);
    mutation_read(f, 0, 16, actual); fill_pattern(expected, 0, 0x31);
    memcpy(expected, bytes, multi_group ? 4096u : 8192u); CHECK(!memcmp(actual, expected, sizeof(actual)));
    printf("N2_J0_CLOSE|multi_group=%d|confirmed_DATA_drained=1|internal_MAP=1|remaining_parent_not_issued=1|all_zero_certificate=1|reopen_exact=1\n", multi_group);
    mutation_destroy(m);
}
int main(void)
{
    rmw_durability_reopen(); close_started_group(0); close_started_group(1);
    puts("N2_J0_PASS|group=5|existing_Linux_profile_lifecycle_Block_serialFTL_NFC_physical_v2=1|not_native_or_powerloss=1");
    return 0;
}
