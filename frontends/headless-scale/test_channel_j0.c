/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* D212 A: existing Linux-profile/lifecycle/serial format2 on real channel
 * shards. Reuse the existing fixture and build, not a second test framework. */
#define _GNU_SOURCE
/* Parse public member names before renaming the included fixture's entry. */
#include "scale_storage.h"
#define main existing_scale_fixture_main
#include "test_ftl.c"
#undef main
#include "channel_volume.h"

struct channel_fixture {
    struct fixture f;
    struct fwlab_nand_channel_volume_config config;
    struct fwlab_nand_channel_volume *volume;
    struct fwlab_nand_channel_v2 assembly;
    struct fwlab_nfc_page_v2_lab_mutation_config timing;
    void *volume_arena;
};
static void channel_open(struct channel_fixture *c, int format)
{
    size_t n = fwlab_nand_channel_volume_arena_size();
    c->volume_arena = aligned_alloc(fwlab_nand_channel_volume_arena_alignment(), n);
    CHECK(c->volume_arena);
    CHECK((format ? fwlab_nand_channel_volume_posix_format(c->volume_arena, n,
        c->f.directory_fd, &c->config, &c->volume) :
        fwlab_nand_channel_volume_posix_restart(c->volume_arena, n,
        c->f.directory_fd, c->config.media_uuid, &c->volume)) == FWLAB_NFC_API_OK);
    c->assembly = fwlab_nand_channel_volume_binding(c->volume);
    CHECK(c->assembly.aggregate.ops && c->assembly.geometry.channels == 4);
    c->f.media_binding.media = c->assembly.aggregate;
    c->f.media_binding.geometry = c->assembly.geometry;
    memcpy(c->f.media_binding.media_uuid, c->assembly.media_uuid, 16);
    c->f.options.channel_media = &c->assembly;
}
static void channel_close(struct channel_fixture *c)
{
    CHECK(!c->f.runtime && fwlab_nand_channel_volume_close(c->volume) == FWLAB_NFC_API_OK);
    free(c->volume_arena); c->volume_arena = NULL; c->volume = NULL;
}
static struct channel_fixture *channel_create(void)
{
    struct channel_fixture *c = calloc(1, sizeof(*c));
    const char *root = getenv("FWLAB_TEST_MEDIA_DIR");
    int n;
    CHECK(c && root && root[0] == '/');
    c->f.started = now_seconds(); c->f.medium_is_tmpfs = 1;
    c->f.use_media_v2 = c->f.use_window_v2 = 1; c->f.lbas = 2048;
    c->config.version = FWLAB_NAND_CHANNEL_VOLUME_VERSION;
    c->config.size = sizeof(c->config);
    c->config.geometry = (struct fwlab_nfc_geometry){
        .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(struct fwlab_nfc_geometry),
        .channels = 4, .luns_per_channel = 2, .planes_per_lun = 1,
        .blocks_per_plane = 4, .pages_per_block = 64, .plane_parallelism_per_lun = 1,
        .main_bytes_per_page = 4096, .oob_bytes_per_page = 128,
        .max_programs_per_erase = 1, .program_order = FWLAB_NFC_PROGRAM_ASCENDING
    };
    memcpy(c->config.media_uuid, "D212-A-VOLUME001", 16);
    for (unsigned i = 0; i < 4; ++i) {
        memcpy(c->config.child_uuid[i], "D212-A-CHILD0000", 16);
        c->config.child_uuid[i][14] = (uint8_t)('0' + i);
    }
    c->f.media_config.geometry = c->config.geometry;
    memcpy(c->f.media_config.media_uuid, c->config.media_uuid, 16);
    media_preflight(root, 1, &c->f.media_config, 1);
    n = snprintf(c->f.directory, sizeof(c->f.directory), "%s/fwlab-d212.XXXXXX", root);
    CHECK(n > 0 && (size_t)n < sizeof(c->f.directory) && mkdtemp(c->f.directory));
    c->f.directory_fd = open(c->f.directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(c->f.directory_fd >= 0);
    c->timing.version = FWLAB_NFC_PAGE_V2_LAB_MUTATION_VERSION;
    c->timing.size = sizeof(c->timing);
    struct fwlab_nfc_page_v2_lab_config *r = &c->timing.read;
    r->version = FWLAB_NFC_PAGE_V2_LAB_VERSION; r->size = sizeof(*r);
    r->base.version = FWLAB_NFC_PAGE_V2_VERSION; r->base.size = sizeof(r->base);
    r->base.profile = FWLAB_NFC_PAGE_V2_PROFILE_R0; r->base.geometry = c->config.geometry;
    memcpy(r->base.media_uuid, c->config.media_uuid, 16);
    r->base.instance_nonce = UINT64_C(0x4432313248554231);
    r->base.operation_uid_limit = UINT64_MAX; r->base.controller_epoch = r->base.generation = 1;
    r->command_ns = 1000; r->array_read_ns = 10000;
    r->channel_bytes_per_second = UINT64_C(1000000000);
    r->virtual_ns_limit = UINT64_C(10000000000);
    for (unsigned i = 0; i < 8; ++i) {
        r->lun[i].target = r->lun[i].ce = (uint16_t)(i % 2);
        r->lun[i].package = (uint16_t)(i / 2); r->lun[i].die = (uint16_t)i;
    }
    c->timing.program_confirm_ns = c->timing.erase_command_ns = c->timing.status_command_ns = 1000;
    c->timing.array_program_ns = 100000; c->timing.array_erase_ns = 1000000;
    c->timing.status_response_bytes = 1;
    c->f.options.mapping_slots = 256; c->f.options.mutation_lab_config = &c->timing;
    scale_storage_channel_lab_factory_init(&c->f.factory, &c->f.options);
    channel_open(c, 1);
    printf("CHANNEL_A_BEGIN|medium=local_tmpfs|ordinary_POSIX_syncs=1|channels=4|luns_each=2|namespace_bytes=1048576|directory=%s\n", c->f.directory);
    fflush(stdout); return c;
}
static void owned_unlink(struct fixture *f, const char *name)
{
    struct stat st;
    CHECK(fstatat(f->directory_fd, name, &st, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISREG(st.st_mode) && st.st_nlink == 1 && st.st_uid == geteuid());
    CHECK(unlinkat(f->directory_fd, name, 0) == 0);
}
static void channel_destroy(struct channel_fixture *c)
{
    if (c->f.runtime) runtime_close(&c->f);
    channel_close(c);
    for (unsigned i = 0; i < 4; ++i) owned_unlink(&c->f, fwlab_nand_channel_volume_shard_name(i));
    owned_unlink(&c->f, FWLAB_NAND_CHANNEL_VOLUME_MANIFEST);
    owned_unlink(&c->f, FWLAB_NAND_CHANNEL_VOLUME_LOCK);
    CHECK(close(c->f.directory_fd) == 0 && rmdir(c->f.directory) == 0); free(c);
}
static struct fwlab_nfc_channel_v2_stats hub_stats(struct fwlab_nfc_channel_v2 *h)
{
    struct fwlab_nfc_channel_v2_stats s;
    CHECK(fwlab_nfc_channel_v2_snapshot(h, &s) == FWLAB_NFC_API_OK &&
        !s.quarantined && !s.poisoned && !s.counters_saturated); return s;
}
static void hub_step(struct fwlab_nfc_page_v2_provider *p)
{
    struct fwlab_nfc_page_v2_step_result s;
    CHECK(p->ops->step(p->context, 1, &s) == FWLAB_NFC_API_OK && s.units_used <= 1);
}
static void hub_join(struct fwlab_nfc_channel_v2 *h, struct fwlab_nfc_page_v2_provider *p)
{
    unsigned guard = 0;
    while (hub_stats(h).phase != FWLAB_NFC_CHANNEL_V2_JOINED) { CHECK(++guard < 10000); hub_step(p); }
}
static void hub_idle(struct fwlab_nfc_channel_v2 *h, struct fwlab_nfc_page_v2_provider *p)
{
    bool idle = false; unsigned guard = 0;
    do { CHECK(++guard < 10000); hub_step(p);
        CHECK(fwlab_nfc_channel_v2_live_idle(h, &idle) == FWLAB_NFC_API_OK);
    } while (!idle);
}
static struct fwlab_nfc_page_v2_request lower_request(struct channel_fixture *c,
    uint64_t uid, unsigned channel, unsigned lun, uint16_t kind, uint8_t *main, uint8_t *oob)
{
    struct fwlab_nfc_page_v2_request r = {0};
    r.version = FWLAB_NFC_PAGE_V2_VERSION; r.size = sizeof(r); r.kind = kind; r.page_count = 1;
    r.operation = (struct fwlab_nfc_operation_token){c->timing.read.base.instance_nonce, uid, 1, 1};
    r.first.channel = (uint16_t)channel; r.first.lun = (uint16_t)lun;
    if (kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP) { r.main = main; r.main_bytes = 4096; r.oob = oob; r.oob_bytes = 128; }
    return r;
}
static void real_hub_journey(void)
{
    struct channel_fixture *c = channel_create();
    size_t n = fwlab_nfc_channel_v2_arena_size();
    void *arena = aligned_alloc(fwlab_nfc_channel_v2_arena_alignment(), n);
    struct fwlab_nfc_channel_v2 *h; uint8_t main[4096], oob[128];
    struct fwlab_nfc_page_v2_request req[4]; struct fwlab_nfc_page_v2_result result;
    CHECK(arena && fwlab_nfc_channel_v2_init(arena, n, &c->timing, &c->assembly, &h) == FWLAB_NFC_API_OK);
    struct fwlab_nfc_page_v2_provider p = fwlab_nfc_channel_v2_provider(h);
    for (unsigned i = 0; i < 4; ++i) {
        memset(main, (int)(0x41 + i), sizeof(main)); memset(oob, (int)(0x71 + i), sizeof(oob));
        req[i] = lower_request(c, i + 1, i / 2, i % 2, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, main, oob);
        CHECK(p.ops->try_submit(p.context, &req[i]).disposition == FWLAB_NFC_ACCEPTED);
    }
    memset(main, 0, sizeof(main)); memset(oob, 0, sizeof(oob));
    hub_join(h, &p);
    struct fwlab_nfc_channel_v2_stats joined = hub_stats(h);
    CHECK(joined.results_pending == 4 && joined.occupied_credits == 4 && joined.joined_batches == 1);
    CHECK(joined.channel[0].successful_program_pages == 2 && joined.channel[1].successful_program_pages == 2);
    CHECK(joined.channel[0].program_array_busy_ns[0] && joined.channel[0].program_array_busy_ns[1]);
    CHECK(joined.now_ns < UINT64_C(200000)); /* Two independent LUN arrays overlap. */
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(p.ops->take_result(p.context, &req[i].operation, &result, NULL) == FWLAB_NFC_API_OK);
        CHECK(result.effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE &&
            result.first.channel == i / 2 && result.first.lun == i % 2);
    }
    struct fwlab_nfc_page_v2_request idle_read = lower_request(c, 5, 2, 0, FWLAB_NFC_PAGE_V2_READ_GROUP, NULL, NULL);
    CHECK(hub_stats(h).retirement_pending == 4 &&
        p.ops->try_submit(p.context, &idle_read).disposition == FWLAB_NFC_BACKPRESSURE);
    hub_idle(h, &p);
    CHECK(p.ops->try_submit(p.context, &idle_read).disposition == FWLAB_NFC_ACCEPTED);
    hub_join(h, &p);
    struct fwlab_nfc_page_v2_lab_trace t;
    CHECK(fwlab_nfc_channel_v2_trace_at(h, 2, 0, &t) == FWLAB_NFC_API_OK &&
        t.event == FWLAB_NFC_PAGE_V2_LAB_ADMIT && t.now_ns >= joined.now_ns && t.ppa.channel == 2);
    CHECK(p.ops->take_result(p.context, &idle_read.operation, &result, NULL) == FWLAB_NFC_API_OK);
    hub_idle(h, &p);
    for (unsigned i = 0; i < 4; ++i) {
        struct fwlab_nfc_page_v2_request r = lower_request(c, i + 6, i / 2, i % 2, FWLAB_NFC_PAGE_V2_READ_GROUP, NULL, NULL);
        struct fwlab_nfc_page_v2_output output = {main, sizeof(main), oob, sizeof(oob)};
        CHECK(p.ops->try_submit(p.context, &r).disposition == FWLAB_NFC_ACCEPTED); hub_join(h, &p);
        CHECK(p.ops->take_result(p.context, &r.operation, &result, &output) == FWLAB_NFC_API_OK && result.read_valid);
        for (unsigned j = 0; j < sizeof(main); ++j) CHECK(main[j] == 0x41 + i);
        for (unsigned j = 0; j < sizeof(oob); ++j) CHECK(oob[j] == 0x71 + i);
        if (i != 3) hub_idle(h, &p);
    }
    CHECK(hub_stats(h).retirement_pending == 1);
    CHECK(p.ops->reset_begin(p.context, c->timing.read.base.instance_nonce, 1) == FWLAB_NFC_API_OK);
    bool quiet = false; unsigned guard = 0;
    while (!quiet) { CHECK(++guard < 10000); hub_step(&p);
        CHECK(p.ops->quiescent(p.context, c->timing.read.base.instance_nonce, 1, &quiet) == FWLAB_NFC_API_OK); }
    CHECK(!hub_stats(h).occupied_credits && !hub_stats(h).retirement_pending);
    free(arena);
    printf("CHANNEL_A_HUB|snapshot=1|real_main_OOB=1|channel_LUN_overlap=1|idle_floor=1|ACK_BP_finalclose=1|model_ns=%llu|not_wallclock_bandwidth=1\n", (unsigned long long)joined.now_ns);
    channel_destroy(c);
}
static void uninitialized_recovery(struct channel_fixture *c)
{
    uint64_t before = c->assembly.aggregate.ops->hash(c->assembly.aggregate.context);
    CHECK(before); channel_close(c); fflush(NULL);
    pid_t child = fork(); CHECK(child >= 0);
    if (!child) {
        channel_open(c, 0); runtime_start(&c->f, 0, 0);
        for (unsigned i = 0; i < 10000; ++i) {
            uint32_t used;
            enum fwlab_spine_result_v0 r = j0_runtime_step(c->f.runtime, 1, &used);
            struct fwlab_ftl_scale_status s;
            CHECK(!c->f.runtime->ready && !c->f.runtime->namespace_bound);
            CHECK(scale_storage_query(c->f.runtime, &s) == FWLAB_SPINE_V0_OK);
            if (r != FWLAB_SPINE_V0_OK || s.quarantined) {
                CHECK(s.quarantined && s.fault_code == SF_FAULT_METADATA);
                _exit(0); /* Failed construction owns no usable live device. */
            }
        }
        _exit(3);
    }
    int status; CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    channel_open(c, 0);
    CHECK(c->assembly.aggregate.ops->hash(c->assembly.aggregate.context) == before);
    puts("CHANNEL_A_UNINITIALIZED|FTL_metadata_reject=1|no_autoformat=1|media_hash_unchanged=1");
}
static void serial_j0_journey(void)
{
    struct channel_fixture *c = channel_create(); struct fixture *f = &c->f;
    uint8_t expected[8192], replacement[8192], actual[8192];
    uninitialized_recovery(c); runtime_start(f, 1, 0); wait_ready(f); identify(f); wait_idle(f);
    fill_pattern(expected, 0, 0x31); command(f, 1, 0, 16, expected, NULL, 0, 0); wait_idle(f);
    fill_pattern(expected, 16, 0x31); command(f, 1, 16, 16, expected, NULL, 0, 0); wait_idle(f);
    fill_pattern(replacement, 7, 0x72); command(f, 1, 7, 16, replacement, NULL, 1, 0); wait_idle(f);
    command(f, 0, 0, 0, NULL, NULL, 0, 0); wait_idle(f);
    CHECK(((struct fwlab_ftl_scale *)f->runtime->block.context)->durable_frontier == 3);
    command(f, 2, 7, 16, NULL, actual, 0, 0); wait_idle(f); CHECK(!memcmp(actual, replacement, sizeof(actual)));
    struct fwlab_nfc_channel_v2_stats s;
    CHECK(scale_storage_channel_snapshot(f->runtime, &s) == FWLAB_SPINE_V0_OK && s.joined_batches > 1 && s.retired_acks);
    runtime_close(f); channel_close(c); channel_open(c, 0);
    runtime_start(f, 0, f->lbas); wait_ready(f);
    CHECK(((struct fwlab_ftl_scale *)f->runtime->block.context)->durable_frontier == 3);
    command(f, 2, 7, 16, NULL, actual, 0, 0); wait_idle(f); CHECK(!memcmp(actual, replacement, sizeof(actual)));
    command(f, 2, 0, 16, NULL, actual, 0, 0); wait_idle(f); fill_pattern(expected, 0, 0x31);
    memcpy(expected + 7 * 512, replacement, 9 * 512); CHECK(!memcmp(actual, expected, sizeof(actual)));
    puts("CHANNEL_A_J0|Linux_profile_same_lifecycle_serial_format2_FTL=1|RMW_FUA_Flush=1|reopen_exact=1|BP_ACK_progress=1|not_multihead=1");
    channel_destroy(c);
}
static void close_at_data_admission(int accepted)
{
    struct channel_fixture *c = channel_create(); struct fixture *f = &c->f;
    uint8_t old[8192], fresh[8192], actual[8192];
    runtime_start(f, 1, 0); wait_ready(f); wait_idle(f);
    fill_pattern(old, 0, 0x21); fill_pattern(fresh, 0, 0x52);
    command(f, 1, 0, 16, old, NULL, 0, 0); wait_idle(f);
    struct fwlab_nvme_command cmd = {0}; struct j0_host_transfer transfer = {0};
    struct fwlab_spine_command_ticket_v0 ticket;
    cmd.version = FWLAB_NVME_COMMAND_VERSION; cmd.size = sizeof(cmd);
    cmd.handle.instance_nonce = UINT64_C(0x53464232484f0000) + f->incarnation;
    cmd.handle.command_uid = ++f->uid; cmd.handle.controller_epoch = cmd.handle.generation = 1;
    cmd.origin.word[0] = cmd.handle.instance_nonce ^ UINT64_C(0x4f52494700000000);
    cmd.origin.word[1] = f->uid; cmd.trace_cookie = f->uid; cmd.safety_generation = 1;
    cmd.namespace_id = 1; cmd.opcode = 1; cmd.queue_class = FWLAB_NVME_QUEUE_IO;
    cmd.data_pointer_format = FWLAB_NVME_DATA_POINTER_PRP; cmd.data_address_present = 1;
    cmd.command_dword10_15[2] = 15;
    transfer.version = J0_RUNTIME_VERSION; transfer.size = sizeof(transfer);
    transfer.exact_bytes = 8192; transfer.input = fresh;
    transfer.direction = FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER;
    CHECK(j0_runtime_admit_start(f->runtime, J0_PROFILE_LINUX_V1, &cmd, &transfer, &ticket) == FWLAB_SPINE_V0_OK);
    struct fwlab_ftl_scale *ftl = f->runtime->block.context;
    unsigned guard = 0;
    while (!(ftl->io.result.kind == SF_IO_PROGRAM && ftl->io.cancel_allowed &&
        ftl->io.phase == (accepted ? SF_IO_WAIT_FIRST : SF_IO_SUBMIT_FIRST))) {
        uint32_t used; CHECK(++guard < 10000);
        CHECK(j0_runtime_step(f->runtime, 1, &used) == FWLAB_SPINE_V0_OK && used == 1);
    }
    CHECK(ftl->io.lower_owned == accepted && ftl->durable_frontier == 1);
    if (accepted) {
        struct fwlab_nfc_channel_v2_stats s;
        CHECK(scale_storage_channel_snapshot(f->runtime, &s) == FWLAB_SPINE_V0_OK &&
            s.phase == FWLAB_NFC_CHANNEL_V2_BUILD && s.occupied_credits == 1);
    }
    runtime_close(f); channel_close(c); channel_open(c, 0);
    runtime_start(f, 0, f->lbas); wait_ready(f);
    CHECK(((struct fwlab_ftl_scale *)f->runtime->block.context)->durable_frontier == (uint64_t)(accepted ? 2 : 1));
    command(f, 2, 0, 16, NULL, actual, 0, 0); wait_idle(f);
    CHECK(!memcmp(actual, accepted ? fresh : old, sizeof(actual)));
    printf("CHANNEL_A_HOST_CLOSE|accepted=%d|accepted_before_child_admit=%d|durable_readback=1|zero_certificate=1\n", accepted, accepted);
    channel_destroy(c);
}
int main(void)
{
    real_hub_journey(); serial_j0_journey();
    close_at_data_admission(0); close_at_data_admission(1);
    puts("CHANNEL_A_PASS|fresh_local_tmpfs|functional_and_process_reopen_only|no_native_threads_powerloss_or_performance_claim");
    return 0;
}
