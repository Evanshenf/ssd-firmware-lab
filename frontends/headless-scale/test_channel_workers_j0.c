/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* B's actual Linux-profile/format3 journey with a construction-time worker
 * transport. The A/B entries included for fixture reuse are never invoked. */
#define CHANNEL_J0_ENTRY retained_channel_fixture_entry
#define CHANNEL_MEDIA_PREFIX "fwlab-d214-j0"
#include "test_channel_j0.c"
#include "nfc_channel_workers.h"

static struct fwlab_nfc_channel_workers *workers;
static struct fwlab_nfc_channel_executor executor;
static struct fixture *current_fixture;

enum fwlab_spine_result_v0 __real_fwlab_ftl_scale_step(struct fwlab_ftl_scale *, uint32_t, uint32_t *);
enum fwlab_spine_result_v0 __wrap_fwlab_ftl_scale_step(struct fwlab_ftl_scale *f, uint32_t n, uint32_t *used)
{
    enum fwlab_spine_result_v0 r = __real_fwlab_ftl_scale_step(f, n, used);
    if (workers && current_fixture && current_fixture->runtime && r == FWLAB_SPINE_V0_OK) {
        const struct fwlab_nfc_channel_v2 *h = scale_storage_channel_hub(current_fixture->runtime);
        bool notified;
        CHECK(h && fwlab_nfc_channel_workers_wait(workers, h, 50, &notified) == FWLAB_NFC_API_OK);
    }
    return r;
}
static void start_workers(struct fixture *f, unsigned n)
{
    struct fwlab_nfc_channel_workers_config c = { .channels = 4, .workers = n };
    CHECK(!workers && fwlab_nfc_channel_workers_create(&c, &workers) == FWLAB_NFC_API_OK);
    executor = fwlab_nfc_channel_workers_executor(workers); CHECK(executor.ops);
    f->options.channel_executor = &executor; current_fixture = f;
}
static void end_workers(unsigned n)
{
    struct fwlab_nfc_channel_workers_stats s;
    CHECK(fwlab_nfc_channel_workers_snapshot(workers, &s) == FWLAB_NFC_API_OK &&
        s.created_workers == n && s.joined_workers == n && !s.occupied_mailboxes && !s.failed);
    CHECK(fwlab_nfc_channel_workers_destroy(workers) == FWLAB_NFC_API_OK);
    workers = NULL; current_fixture = NULL;
}
static void release_before_step(void)
{
    struct channel_fixture *c = channel_create_blocks(8); struct fixture *f = &c->f;
    struct j0_controller_buffer *buffer = calloc(1, sizeof(*buffer)); CHECK(buffer);
    struct fwlab_block_namespace_ref_v0 ns = {{0x44323134, 0}};
    struct j0_storage_runner runner = {0}; struct fwlab_block_service_v0 service;
    struct j0_runtime_config config = { .generation = 1, .execution_epoch = 1,
        .media_mode = J0_MEDIA_FORMAT, .format_lba_count = 2048, .media_binding = &f->media_binding };
    memcpy(config.media_uuid, c->config.media_uuid, 16);
    j0_controller_buffer_init(buffer, J0_BUFFER_ISSUER_NONCE, 1);
    scale_storage_multihead_lab_factory_init(&f->factory, &f->options);
    uint64_t before = c->assembly.aggregate.ops->hash(c->assembly.aggregate.context); CHECK(before);
    start_workers(f, 4);
    CHECK(f->factory.bind(f->factory.context, &config, &buffer->port, &ns,
        J0_LIFECYCLE_NONCE, J0_M3P_INSTANCE_NONCE, J0_NFC_INSTANCE_NONCE,
        &runner, &service) == FWLAB_SPINE_V0_OK && runner.context);
    runner.release(runner.context); end_workers(4);
    CHECK(c->assembly.aggregate.ops->hash(c->assembly.aggregate.context) == before);
    CHECK(j0_controller_buffer_storage_fini(buffer)); free(buffer);
    channel_destroy(c);
    puts("WORKERS_FACTORY_RELEASE|before_first_step=1|no_media_effect=1|all_four_joined_before_release=1");
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
static void worker_journey(unsigned n)
{
    struct channel_fixture *c = channel_create_blocks(8); struct fixture *f = &c->f;
    uint8_t old[8192], fresh[8192], actual[8192], expected[8192];
    scale_storage_multihead_lab_factory_init(&f->factory, &f->options);
    start_workers(f, n); runtime_start(f, 1, 0); wait_ready(f); identify(f); wait_idle(f);
    struct fwlab_ftl_scale *ftl = f->runtime->block.context;
    CHECK(ftl->disk_format == SF_MULTIHEAD_FORMAT_VERSION && ftl->writes);
    fill_pattern(old, 0, 0x21); command(f, 1, 0, 16, old, NULL, 1, 0); wait_idle(f);
    fill_pattern(fresh, 7, 0x52); command(f, 1, 7, 16, fresh, NULL, 1, 0); wait_idle(f);
    command(f, 0, 0, 0, NULL, NULL, 0, 0); wait_idle(f);
    CHECK(ftl->durable_frontier == 2);
    command(f, 2, 0, 16, NULL, actual, 0, 0); wait_idle(f);
    memcpy(expected, old, sizeof(expected)); memcpy(expected + 7 * 512, fresh, 9 * 512);
    CHECK(!memcmp(actual, expected, sizeof(actual)));
    runtime_close(f); end_workers(n); channel_close(c); channel_open(c, 0);
    start_workers(f, n); runtime_start(f, 0, f->lbas); wait_ready(f); wait_idle(f);
    ftl = f->runtime->block.context; CHECK(ftl->durable_frontier == 2 && sf_heads_empty(ftl));
    command(f, 2, 0, 16, NULL, actual, 0, 0); wait_idle(f); CHECK(!memcmp(actual, expected, sizeof(actual)));
    fill_pattern(fresh, 0, 0x63); admit_write(f, fresh);
    struct fwlab_nfc_channel_v2_stats s; unsigned guard;
    for (guard = 0; guard < 200000; ++guard) {
        CHECK(scale_storage_channel_snapshot(f->runtime, &s) == FWLAB_SPINE_V0_OK);
        /* Actual accepted two-head DATA is still waiting at the hub RUN/JOIN
         * boundary. Closing must drain this group and its MAP, not cancel it. */
        if (sf_write_pool_busy(ftl) && s.phase == FWLAB_NFC_CHANNEL_V2_RUN &&
            s.batch_requests[0] + s.batch_requests[1] + s.batch_requests[2] + s.batch_requests[3] == 2) break;
        uint32_t used; CHECK(j0_runtime_step(f->runtime, 1, &used) == FWLAB_SPINE_V0_OK);
    }
    CHECK(guard < 200000 && ftl->durable_frontier == 2);
    runtime_close(f); end_workers(n); channel_close(c); channel_open(c, 0);
    start_workers(f, n); runtime_start(f, 0, f->lbas); wait_ready(f); wait_idle(f);
    ftl = f->runtime->block.context; CHECK(ftl->durable_frontier == 3);
    command(f, 2, 0, 16, NULL, actual, 0, 0); wait_idle(f); CHECK(!memcmp(actual, fresh, sizeof(actual)));
    runtime_close(f); end_workers(n); channel_destroy(c);
    printf("WORKERS_J0|workers=%u|Linux_profile_lifecycle_format3=1|Identify_RMW_FUA_Flush_reopen=1|accepted_DATA_close_MAP_drain=1|zero_and_actual_joins=1\n", n);
}
int main(void)
{
    release_before_step(); worker_journey(1); worker_journey(4);
    puts("WORKERS_J0_PASS|local_tmpfs_ordinary_POSIX=1|no_native_or_disk_powerloss_claim=1");
    return 0;
}
