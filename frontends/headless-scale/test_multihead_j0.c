/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* D213 B uses the existing actual channel/J0 fixture with an explicit
 * format3 constructor. No alternate Block/NAND or native profile is created. */
#define CHANNEL_J0_ENTRY existing_multihead_channel_fixture_main
#define CHANNEL_MEDIA_PREFIX "fwlab-d213"
#include "test_channel_j0.c"

enum hook_mode { HOOK_NONE, HOOK_TRACK, HOOK_LIMIT_ONE, HOOK_DATA_UNCERTAIN, HOOK_MAP_UNCERTAIN };
static struct {
    struct fwlab_nfc_page_v2_provider real;
    struct fwlab_nfc_page_v2_provider_ops ops;
    struct fwlab_ftl_scale *ftl;
    struct fwlab_nfc_operation_token data_key[4];
    uint8_t taken[4];
    uint32_t mode, accepted, inflight, consumed, bp, cancels, map_admissions;
    uint32_t injected, successful_sibling;
} hooks;
struct fwlab_nfc_page_v2_provider __real_fwlab_nfc_channel_v2_provider(struct fwlab_nfc_channel_v2 *);
static int data_request(const struct fwlab_nfc_page_v2_request *r)
{
    uint32_t lpn;
    return r && r->kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP && r->oob &&
        r->oob_bytes >= SF_OOB_BYTES && sf_data_oob_lpn(r->oob, &lpn);
}
static int tracked_key(const struct fwlab_nfc_operation_token *key)
{
    for (unsigned i = 0; i < hooks.accepted; ++i)
        if (!memcmp(key, &hooks.data_key[i], sizeof(*key))) return (int)i;
    return -1;
}
static struct fwlab_nfc_submit_result wrapped_submit(void *context, const struct fwlab_nfc_page_v2_request *r)
{
    CHECK(context == &hooks);
    int data = hooks.mode && data_request(r), known = data ? tracked_key(&r->operation) : -1;
    if (data && known < 0 && hooks.mode == HOOK_LIMIT_ONE && hooks.inflight) {
        ++hooks.bp;
        return (struct fwlab_nfc_submit_result){FWLAB_NFC_BACKPRESSURE, FWLAB_NFC_REASON_NONE};
    }
    struct fwlab_nfc_submit_result result = hooks.real.ops->try_submit(hooks.real.context, r);
    if (result.disposition == FWLAB_NFC_ACCEPTED) {
        if (data && known < 0) {
            CHECK(hooks.accepted < 4);
            hooks.data_key[hooks.accepted++] = r->operation; ++hooks.inflight;
        } else if (hooks.mode && hooks.ftl && r->kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP &&
            hooks.ftl->meta.record.kind == SF_MAP_WINDOW &&
            (hooks.ftl->meta.phase == SF_M_JOURNAL_A || hooks.ftl->meta.phase == SF_M_JOURNAL_B))
            ++hooks.map_admissions;
    }
    return result;
}
static enum fwlab_nfc_api_result wrapped_take(void *context, const struct fwlab_nfc_operation_token *key,
    struct fwlab_nfc_page_v2_result *out, const struct fwlab_nfc_page_v2_output *output)
{
    CHECK(context == &hooks);
    enum fwlab_nfc_api_result r = hooks.real.ops->take_result(hooks.real.context, key, out, output);
    int i = tracked_key(key);
    if (r == FWLAB_NFC_API_OK && i >= 0 && !hooks.taken[i]) {
        CHECK(hooks.inflight); --hooks.inflight; ++hooks.consumed; hooks.taken[i] = 1;
    }
    return r;
}
static enum fwlab_nfc_api_result wrapped_cancel(void *context, const struct fwlab_nfc_operation_token *key)
{
    CHECK(context == &hooks);
    if (tracked_key(key) >= 0) ++hooks.cancels;
    return hooks.real.ops->cancel(hooks.real.context, key);
}
static enum fwlab_nfc_api_result wrapped_step(void *context, uint32_t budget, struct fwlab_nfc_page_v2_step_result *out)
{ CHECK(context == &hooks); return hooks.real.ops->step(hooks.real.context, budget, out); }
static enum fwlab_nfc_api_result wrapped_reset(void *context, uint64_t nonce, uint32_t epoch)
{ CHECK(context == &hooks); return hooks.real.ops->reset_begin(hooks.real.context, nonce, epoch); }
static enum fwlab_nfc_api_result wrapped_quiet(void *context, uint64_t nonce, uint32_t epoch, bool *out)
{ CHECK(context == &hooks); return hooks.real.ops->quiescent(hooks.real.context, nonce, epoch, out); }
struct fwlab_nfc_page_v2_provider __wrap_fwlab_nfc_channel_v2_provider(struct fwlab_nfc_channel_v2 *hub)
{
    /* Construction-time lawful readiness wrapper, never a live provider swap.
     * All accepted requests/results and media bytes use the actual A hub. */
    memset(&hooks, 0, sizeof(hooks)); hooks.real = __real_fwlab_nfc_channel_v2_provider(hub);
    CHECK(hooks.real.ops);
    hooks.ops = *hooks.real.ops;
    hooks.ops.try_submit = wrapped_submit; hooks.ops.take_result = wrapped_take;
    hooks.ops.cancel = wrapped_cancel; hooks.ops.step = wrapped_step;
    hooks.ops.reset_begin = wrapped_reset; hooks.ops.quiescent = wrapped_quiet;
    return (struct fwlab_nfc_page_v2_provider){&hooks.ops, &hooks};
}
enum fwlab_nfc_api_result __real_fwlab_file_nand_v2_program_pages(struct fwlab_file_nand_v2 *,
    const struct fwlab_nfc_ppa *, uint32_t, const uint8_t *, size_t, const uint8_t *, size_t,
    struct fwlab_nand_media_result *, size_t);
enum fwlab_nfc_api_result __wrap_fwlab_file_nand_v2_program_pages(struct fwlab_file_nand_v2 *media,
    const struct fwlab_nfc_ppa *ppa, uint32_t pages, const uint8_t *main, size_t main_bytes,
    const uint8_t *oob, size_t oob_bytes, struct fwlab_nand_media_result *out, size_t count)
{
    enum fwlab_nfc_api_result r = __real_fwlab_file_nand_v2_program_pages(media, ppa, pages,
        main, main_bytes, oob, oob_bytes, out, count);
    uint32_t lpn;
    if (r != FWLAB_NFC_API_OK || !hooks.ftl) return r;
    if (hooks.mode == HOOK_DATA_UNCERTAIN && sf_data_oob_lpn(oob, &lpn)) {
        if (lpn == 1) ++hooks.successful_sibling;
        if (lpn == 0 && !hooks.injected) {
            ++hooks.injected; return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
    }
    if (hooks.mode == HOOK_MAP_UNCERTAIN && !hooks.injected &&
        hooks.ftl->meta.phase == SF_M_JOURNAL_A && hooks.ftl->meta.record.kind == SF_MAP_WINDOW &&
        hooks.ftl->meta.record.delta[0].lpn == 1) {
        ++hooks.injected; return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    return r;
}
static void arm_hooks(struct fixture *f, uint32_t mode)
{
    hooks.ftl = f->runtime->block.context; hooks.mode = mode;
    hooks.accepted = hooks.inflight = hooks.consumed = hooks.bp = hooks.cancels = 0;
    hooks.map_admissions = hooks.injected = hooks.successful_sibling = 0;
    memset(hooks.data_key, 0, sizeof(hooks.data_key)); memset(hooks.taken, 0, sizeof(hooks.taken));
}
static void admit_write(struct fixture *f, const uint8_t input[8192])
{
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
    transfer.exact_bytes = 8192; transfer.input = input; transfer.direction = FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER;
    CHECK(j0_runtime_admit_start(f->runtime, J0_PROFILE_LINUX_V1, &cmd, &transfer, &ticket) == FWLAB_SPINE_V0_OK);
}

static struct fixture *observed_fixture;
static uint32_t observed_width;
static void observe_wave(struct fixture *f)
{
    struct fwlab_nfc_channel_v2_stats s;
    uint32_t width = 0;
    CHECK(f == observed_fixture && scale_storage_channel_snapshot(f->runtime, &s) == FWLAB_SPINE_V0_OK);
    for (unsigned i = 0; i < 4; ++i) width += s.batch_requests[i];
    if (width > observed_width) observed_width = width;
}
static struct channel_fixture *multihead_create(uint16_t blocks_per_plane)
{
    struct channel_fixture *c = channel_create_blocks(blocks_per_plane);
    scale_storage_multihead_lab_factory_init(&c->f.factory, &c->f.options);
    runtime_start(&c->f, 1, 0); wait_ready(&c->f); wait_idle(&c->f);
    struct fwlab_ftl_scale *f = c->f.runtime->block.context;
    CHECK(f->disk_format == SF_MULTIHEAD_FORMAT_VERSION && f->writes && f->heads.count == 4);
    return c;
}
static void two_head_journey(uint16_t blocks_per_plane)
{
    struct channel_fixture *c = multihead_create(blocks_per_plane); struct fixture *f = &c->f;
    uint8_t old[8192], fresh[8192], actual[8192], expected[8192];
    struct fwlab_ftl_scale *ftl = f->runtime->block.context;
    observed_fixture = f; observed_width = 0; f->cut_observer = observe_wave;
    fill_pattern(old, 0, 0x21); command(f, 1, 0, 16, old, NULL, 1, 0); wait_idle(f);
    CHECK(observed_width == 2);
    uint32_t d0 = sf_head_domain(ftl, ftl->map[0].ppa / 64);
    uint32_t d1 = sf_head_domain(ftl, ftl->map[1].ppa / 64);
    CHECK(d0 != SF_NONE && d1 != SF_NONE && d0 != d1 && ftl->durable_frontier == 1);
    if (blocks_per_plane == 4) CHECK(d0 != 0 && d1 != 0); /* channel0 is metadata-only */
    command(f, 2, 0, 16, NULL, actual, 0, 0); wait_idle(f); CHECK(!memcmp(actual, old, sizeof(actual)));
    fill_pattern(fresh, 7, 0x52); command(f, 1, 7, 16, fresh, NULL, 1, 0); wait_idle(f);
    command(f, 0, 0, 0, NULL, NULL, 0, 0); wait_idle(f);
    CHECK(ftl->durable_frontier == 2);
    command(f, 2, 7, 16, NULL, actual, 0, 0); wait_idle(f); CHECK(!memcmp(actual, fresh, sizeof(actual)));
    CHECK(observed_width >= 2 && observed_width <= 4);
    f->cut_observer = NULL; observed_fixture = NULL;
    runtime_close(f); channel_close(c); channel_open(c, 0);
    runtime_start(f, 0, f->lbas); wait_ready(f); wait_idle(f);
    ftl = f->runtime->block.context;
    CHECK(ftl->disk_format == SF_MULTIHEAD_FORMAT_VERSION && ftl->durable_frontier == 2 && sf_heads_empty(ftl));
    command(f, 2, 0, 16, NULL, actual, 0, 0); wait_idle(f);
    memcpy(expected, old, sizeof(expected)); memcpy(expected + 7 * 512, fresh, 9 * 512);
    CHECK(!memcmp(actual, expected, sizeof(actual)));
    command(f, 2, 7, 16, NULL, actual, 0, 0); wait_idle(f); CHECK(!memcmp(actual, fresh, sizeof(actual)));
    printf("MULTIHEAD_J0|format=3|blocks_per_plane=%u|legal_8KiB_twohead=1|different_DATA_domains=%u,%u|max_DATA_batch=%u|RMW_FUA_Flush_reopen=1\n",
        (unsigned)blocks_per_plane, d0, d1, observed_width);
    channel_destroy(c);
}
static void close_wave(int partial)
{
    struct channel_fixture *c = multihead_create(8); struct fixture *f = &c->f;
    uint8_t old[8192], fresh[8192], actual[8192], expected[8192];
    fill_pattern(old, 0, 0x21); fill_pattern(fresh, 0, 0x52);
    command(f, 1, 0, 16, old, NULL, 0, 0); wait_idle(f);
    arm_hooks(f, partial ? HOOK_LIMIT_ONE : HOOK_TRACK); admit_write(f, fresh);
    unsigned guard = 0;
    while (hooks.accepted != (uint32_t)(partial ? 1 : 2)) {
        uint32_t used; CHECK(++guard < 10000);
        CHECK(j0_runtime_step(f->runtime, 1, &used) == FWLAB_SPINE_V0_OK && used == 1);
    }
    CHECK(hooks.ftl->durable_frontier == 1 && (!partial || hooks.bp));
    runtime_close(f);
    CHECK(hooks.consumed == (uint32_t)(partial ? 1 : 2) && !hooks.inflight && !hooks.cancels &&
        hooks.map_admissions == (uint32_t)(partial ? 2 : 4));
    hooks.mode = HOOK_NONE; hooks.ftl = NULL;
    channel_close(c); channel_open(c, 0); runtime_start(f, 0, f->lbas); wait_ready(f); wait_idle(f);
    CHECK(((struct fwlab_ftl_scale *)f->runtime->block.context)->durable_frontier == (uint64_t)(partial ? 1 : 2));
    command(f, 2, 0, 16, NULL, actual, 0, 0); wait_idle(f);
    memcpy(expected, fresh, sizeof(expected)); if (partial) memcpy(expected + 4096, old + 4096, 4096);
    CHECK(!memcmp(actual, expected, sizeof(actual)));
    printf("MULTIHEAD_CLOSE|accepted_runs=%u|stop_unaccepted_suffix=1|accepted_cancel_calls=0|ordered_MAP=1|frontier_and_reopen=1|zero_certificate=1\n", partial ? 1u : 2u);
    channel_destroy(c);
}
static void uncertain_wave(int map_failure)
{
    struct channel_fixture *c = multihead_create(8); struct fixture *f = &c->f;
    uint8_t old[8192], fresh[8192], actual[8192];
    fill_pattern(old, 0, 0x21); fill_pattern(fresh, 0, 0x52);
    command(f, 1, 0, 16, old, NULL, 0, 0); wait_idle(f);
    runtime_close(f); hooks.ftl = NULL; channel_close(c); fflush(NULL);
    pid_t child = fork(); CHECK(child >= 0);
    if (!child) {
        channel_open(c, 0); runtime_start(f, 0, f->lbas); wait_ready(f); wait_idle(f);
        struct fwlab_ftl_scale *ftl = f->runtime->block.context;
        uint64_t maps = ftl->map_sequence;
        struct sf_map_entry before[2] = {ftl->map[0], ftl->map[1]};
        arm_hooks(f, map_failure ? HOOK_MAP_UNCERTAIN : HOOK_DATA_UNCERTAIN); admit_write(f, fresh);
        for (unsigned guard = 0; guard < 100000; ++guard) {
            uint32_t used;
            enum fwlab_spine_result_v0 r = j0_runtime_step(f->runtime, 1, &used);
            if (r == FWLAB_SPINE_V0_OK) continue;
            CHECK(ftl->quarantined && hooks.injected == 1 && hooks.accepted == 2 && hooks.consumed == 2 && !hooks.cancels);
            CHECK(ftl->parent.status.durability_witness == FWLAB_BLOCK_V0_WITNESS_NONE && ftl->durable_frontier == 1);
            if (!map_failure) {
                CHECK(hooks.successful_sibling == 1 && !hooks.map_admissions && ftl->map_sequence == maps &&
                    !memcmp(before, ftl->map, sizeof(before)) && ftl->nfc_close_started && ftl->nfc_quiescent);
            } else {
                CHECK(hooks.map_admissions == 3 && ftl->map_sequence == maps + 1 &&
                    ftl->parent.status.completed_lbas == 8 && ftl->parent.status.effect == FWLAB_BLOCK_V0_EFFECT_UNKNOWN_PREFIX);
            }
            printf("MULTIHEAD_UNCERTAINTY|kind=%s|actual_program_then_lost_certainty=1|accepted_siblings_drained=1|MAP_rail_program_admissions=%u|no_SELF=1\n",
                map_failure ? "second_MAP_A" : "first_DATA", hooks.map_admissions);
            fflush(stdout); _exit(0); /* Explicit recovery-required process boundary, not a zero grant. */
        }
        _exit(3);
    }
    int status; CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    channel_open(c, 0); runtime_start(f, 0, f->lbas); wait_ready(f); wait_idle(f);
    CHECK(((struct fwlab_ftl_scale *)f->runtime->block.context)->durable_frontier == (uint64_t)(map_failure ? 2 : 1));
    command(f, 2, 0, 16, NULL, actual, 0, 0); wait_idle(f);
    CHECK(!memcmp(actual, map_failure ? fresh : old, sizeof(actual)));
    printf("MULTIHEAD_UNCERTAINTY_REOPEN|kind=%s|exact_data=1|no_rollback_claim=1|orphan_heads_sealed=1\n", map_failure ? "MAP" : "DATA");
    channel_destroy(c);
}
int main(void)
{
    two_head_journey(8); two_head_journey(4);
    close_wave(1); close_wave(0);
    uncertain_wave(0); uncertain_wave(1);
    puts("MULTIHEAD_J0_PASS|real_shards_serial_controller_cooperative_NFC|not_native_threads_or_capacity_qualification");
    return 0;
}
