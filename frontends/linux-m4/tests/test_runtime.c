/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

/* Reuse the existing real file-media and command fixture, without a second
 * media model or a copied command executor. This is a runtime prerequisite;
 * it does not claim native PCI/HIF execution. */
#define main j0b_existing_matrix_main
#include "../../headless-j0/tests/test_j0b.c"
#undef main

static int finish_publication(
    struct j0_runtime *runtime,
    const struct fwlab_spine_command_ticket_v0 *ticket,
    const struct fwlab_completion_lease_v0 *lease, uint32_t decision)
{
    uint32_t iteration;

    for (iteration = 0; iteration < 256; ++iteration) {
        enum fwlab_spine_result_v0 result = j0_runtime_publication_finish(
            runtime, ticket, lease, decision);

        if (result == FWLAB_SPINE_V0_OK) {
            return 1;
        }
        CHECK(result == FWLAB_SPINE_V0_IN_PROGRESS);
    }
    return 0;
}

static int continuous_stream(struct j0_runtime *runtime)
{
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_spine_command_ticket_v0 first_ticket;
    struct fwlab_completion_lease_v0 lease;
    struct fwlab_completion_lease_v0 first_lease;
    struct fwlab_nvme_completion_intent intent;
    uint64_t previous_lease_uid = 0;
    uint32_t iteration;

    for (iteration = 0; iteration < 65; ++iteration) {
        uint32_t profile = (iteration & 1u) ? J0_PROFILE_LINUX_V1
                                          : J0_PROFILE_C43_P1;
        uint64_t lba = profile == J0_PROFILE_C43_P1 ? 1u : 17u;
        struct fwlab_nvme_command command = command_make(
            UINT64_C(0x4a3153545245414d), UINT64_C(0x9000) + iteration,
            profile, 0x02, lba, 1);
        struct j0_host_transfer transfer = transfer_make(
            FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST, 512, NULL);
        struct fwlab_completion_lease_v0 repeated;
        struct fwlab_nvme_completion_intent repeated_intent;
        uint8_t expected[512];
        uint8_t actual[512];

        if (profile == J0_PROFILE_LINUX_V1) {
            command.command_dword10_15[2] |= UINT32_C(0x80000000);
            command.command_dword10_15[3] = 7;
        }
        CHECK(command_run(runtime, profile, &command, &transfer,
                          &ticket, &intent));
        pattern_fill(expected, sizeof(expected),
                     profile == J0_PROFILE_C43_P1 ? 0x31 : 0x71);
        CHECK(j0_runtime_host_read(runtime, &ticket, actual, sizeof(actual)) ==
              FWLAB_SPINE_V0_OK);
        CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
        CHECK(j0_runtime_publication_acquire(runtime, &ticket, &lease,
                                             &repeated_intent) ==
              FWLAB_SPINE_V0_OK);
        CHECK(fwlab_completion_lease_v0_valid(&lease));
        CHECK(lease.lease_uid > previous_lease_uid);
        previous_lease_uid = lease.lease_uid;
        CHECK(memcmp(&intent, &repeated_intent, sizeof(intent)) == 0);
        CHECK(j0_runtime_publication_acquire(runtime, &ticket, &repeated,
                                             &repeated_intent) ==
              FWLAB_SPINE_V0_OK);
        CHECK(memcmp(&lease, &repeated, sizeof(lease)) == 0);
        if (iteration == 0) {
            struct fwlab_completion_lease_v0 wrong = lease;
            uint32_t step;

            first_ticket = ticket;
            first_lease = lease;
            ++wrong.issuer_nonce;
            CHECK(j0_runtime_publication_finish(
                      runtime, &ticket, &wrong,
                      FWLAB_SPINE_PUBLICATION_V1_COMMITTED) ==
                  FWLAB_SPINE_V0_STALE);
            for (step = 0; step < 8; ++step) {
                uint32_t units;

                CHECK(j0_runtime_step(runtime, 3, &units) == FWLAB_SPINE_V0_OK);
                CHECK(record_for_ticket(runtime, &ticket) != NULL);
            }
        }
        if (iteration == 64) {
            CHECK(j0_runtime_publication_finish(
                      runtime, &ticket, &lease,
                      FWLAB_SPINE_PUBLICATION_V1_DISCARDED) ==
                  FWLAB_SPINE_V0_WRONG_STATE);
            CHECK(j0_runtime_close_start(runtime) == FWLAB_SPINE_V0_OK);
            CHECK(j0_runtime_fini(runtime) == FWLAB_SPINE_V0_IN_PROGRESS);
            CHECK(finish_publication(runtime, &ticket, &lease,
                                     FWLAB_SPINE_PUBLICATION_V1_DISCARDED));
            break;
        }
        CHECK(finish_publication(runtime, &ticket, &lease,
                                 FWLAB_SPINE_PUBLICATION_V1_COMMITTED));
        CHECK(record_for_ticket(runtime, &ticket) == NULL);
        CHECK(j0_runtime_publication_finish(
                  runtime, &ticket, &lease,
                  FWLAB_SPINE_PUBLICATION_V1_COMMITTED) == FWLAB_SPINE_V0_OK);
        CHECK(runtime->host.active_authorities == 0);
        CHECK(runtime->host.active_dma == 0);
        CHECK(runtime->buffer.active_leases == 0);
    }
    CHECK(j0_runtime_publication_finish(
              runtime, &first_ticket, &first_lease,
              FWLAB_SPINE_PUBLICATION_V1_COMMITTED) == FWLAB_SPINE_V0_STALE);
    return 1;
}

static enum fwlab_spine_result_v0 referenced_prepare(
    void *context, const struct fwlab_nvme_command_handle *command,
    const struct fwlab_nvme_origin_token *origin, uint32_t direction,
    uint32_t exact_bytes, const uint8_t *input)
{
    struct test_fake_data *host = context;

    return input == NULL && direction == FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER &&
                   exact_bytes == host->exact_bytes &&
                   j0_handle_equal(command, &host->command) &&
                   j0_origin_equal(origin, &host->origin)
               ? FWLAB_SPINE_V0_OK : FWLAB_SPINE_V0_INVALID;
}

static enum fwlab_spine_result_v0 referenced_release(
    void *context, const struct fwlab_nvme_command_handle *command,
    const struct fwlab_nvme_origin_token *origin)
{
    struct test_fake_data *host = context;

    return j0_handle_equal(command, &host->command) &&
                   j0_origin_equal(origin, &host->origin) &&
                   !host->authority_live && host->drained
               ? FWLAB_SPINE_V0_OK : FWLAB_SPINE_V0_WRONG_STATE;
}

static enum fwlab_spine_result_v0 referenced_bind(
    void *context, const struct fwlab_controller_buffer_port_v0 *buffer,
    uint32_t generation, struct j0_host_binding *binding)
{
    struct test_fake_data *host = context;

    host->port.ops = &fake_data_ops;
    host->port.context = host;
    host->port.buffer = *buffer;
    host->port.authority_issuer_nonce = UINT64_C(0x4a3146414b454844);
    host->port.dma_issuer_nonce = UINT64_C(0x4a3146414b45444d);
    host->port.generation = generation;
    memset(binding, 0, sizeof(*binding));
    binding->data = host->port;
    binding->context = host;
    binding->endpoint_prepare = referenced_prepare;
    binding->endpoint_release = referenced_release;
    binding->inline_input = 0;
    return FWLAB_SPINE_V0_OK;
}

static int referenced_host_journey(
    struct fwlab_file_nand_v0 *file, const uint8_t uuid[16])
{
    struct test_fake_data host;
    struct j0_host_factory factory = { referenced_bind, &host };
    struct j0_runtime_config config;
    struct j0_runtime *runtime = calloc(1, sizeof(*runtime));
    struct fwlab_nvme_command command = command_make(
        UINT64_C(0x4a31464143544f52), 501, J0_PROFILE_LINUX_V1, 1, 257, 1);
    struct j0_host_transfer transfer = transfer_make(
        FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER, 512, NULL);
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_completion_lease_v0 lease;
    struct fwlab_nvme_completion_intent intent;
    struct j0_close_status closed;

    CHECK(runtime != NULL);
    memset(&host, 0, sizeof(host));
    command.handle.controller_epoch = 2;
    command.handle.generation = 2;
    command.safety_generation = 2;
    host.command = command.handle;
    host.origin = command.origin;
    host.exact_bytes = 512;
    pattern_fill(host.source, host.exact_bytes, 0xaa);
    memset(&config, 0, sizeof(config));
    config.version = J0_RUNTIME_VERSION;
    config.size = (uint16_t)sizeof(config);
    memcpy(config.media_uuid, uuid, sizeof(config.media_uuid));
    config.file = file;
    config.media_mode = J0_MEDIA_RECOVER;
    config.generation = 2;
    config.execution_epoch = 2;
    config.volatile_nonce_seed = 12;
    config.host_factory = &factory;
    CHECK(j0_runtime_init(runtime, &config) == FWLAB_SPINE_V0_OK);
    CHECK(runtime_ready(runtime));
    CHECK(j0_runtime_admit_referenced(runtime, J0_PROFILE_LINUX_V1, &command,
                                      &ticket) == FWLAB_SPINE_V0_OK);
    CHECK(!host.request_valid);
    CHECK(j0_bytes_zero(record_for_ticket(runtime, &ticket)->transfer_copy,
                        J0_MAX_TRANSFER_BYTES));
    /* Changing the referenced bytes before DMA must change the written data;
     * admission/shape resolution cannot silently snapshot a native Host. */
    pattern_fill(host.source, host.exact_bytes, 0xbb);
    CHECK(command_run(runtime, J0_PROFILE_LINUX_V1, &command, &transfer,
                      &ticket, &intent));
    CHECK(host.request_valid && host.drained && !host.authority_live);
    CHECK(j0_runtime_publication_acquire(runtime, &ticket, &lease, &intent) ==
          FWLAB_SPINE_V0_OK);
    CHECK(finish_publication(runtime, &ticket, &lease,
                             FWLAB_SPINE_PUBLICATION_V1_COMMITTED));
    CHECK(runtime_close(runtime, &closed));
    free(runtime);
    CHECK(runtime_open(&runtime, file, uuid, J0_MEDIA_RECOVER, 13));
    CHECK(recovery_read(runtime, J0_PROFILE_LINUX_V1,
                         UINT64_C(0x4a31464143545244), 601, 257, 1, 0xbb));
    CHECK(runtime_close(runtime, &closed));
    free(runtime);
    return 1;
}

static int runtime_journey(void)
{
    struct image_context image;
    struct j0_runtime *runtime;
    struct profile_receipt c43;
    struct profile_receipt linux_profile;
    struct j0_close_status closed;

    CHECK(image_create(&image));
    image.arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                 fwlab_file_nand_v0_arena_size());
    CHECK(image.arena != NULL);
    CHECK(fwlab_file_nand_v0_posix_restart(
              image.arena, fwlab_file_nand_v0_arena_size(), image.directory_fd,
              image.name, &image.holder, &image.file) == FWLAB_NFC_API_OK);
    CHECK(runtime_open(&runtime, image.file, image.uuid, J0_MEDIA_FORMAT, 11));
    CHECK(profile_journey(runtime, J0_PROFILE_C43_P1,
                          UINT64_C(0x4a31433433000000), 1, 1, 3, 0x31, &c43));
    CHECK(profile_journey(runtime, J0_PROFILE_LINUX_V1,
                          UINT64_C(0x4a314c4e58000000), 101, 17, 16, 0x71,
                          &linux_profile));
    CHECK(continuous_stream(runtime));
    CHECK(runtime_close(runtime, &closed));
    CHECK(closed.quiescent && closed.profiles_retired);
    free(runtime);
    CHECK(referenced_host_journey(image.file, image.uuid));
    CHECK(fwlab_file_nand_v0_close(image.file) == FWLAB_NFC_API_OK);
    free(image.arena);
    CHECK(image_destroy(&image));
    puts("J1 runtime prerequisite: PASS (profiles=2 committed=64 discarded=1 "
         "referenced_host=1 deferred_dma=1 holders=0 native_hif=not_connected)");
    return 1;
}

int main(void)
{
    CHECK_MAIN(runtime_journey());
    return 0;
}
