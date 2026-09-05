/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

/* Reuse the existing real file-media and command fixture, without a second
 * media model or a copied command executor. This is a runtime prerequisite;
 * it does not claim native PCI/HIF execution. */
#define main j0b_existing_matrix_main
#include "../../headless-j0/tests/test_j0b.c"
#undef main
#include "fwlab/private/nfc_trace_window.h"

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

static int empty_flush(struct j0_runtime *runtime, uint64_t uid)
{
    struct fwlab_nvme_command command = command_make(
        UINT64_C(0x4a31454d50545946), uid, J0_PROFILE_LINUX_V1, 0, 0, 0);
    struct j0_host_transfer transfer = transfer_make(0, 0, NULL);
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_completion_lease_v0 lease;
    struct fwlab_nvme_completion_intent intent;
    uint64_t child_uid = runtime->m3p->next_child_uid;

    CHECK(runtime->m3p->host_sequence == 0);
    CHECK(command_run(runtime, J0_PROFILE_LINUX_V1, &command, &transfer,
                      &ticket, &intent));
    CHECK(runtime->m3p->host_sequence == 0);
    CHECK(runtime->m3p->durable_frontier == 0);
    CHECK(runtime->m3p->next_child_uid == child_uid);
    CHECK(j0_runtime_publication_acquire(runtime, &ticket, &lease, &intent) ==
          FWLAB_SPINE_V0_OK);
    CHECK(finish_publication(runtime, &ticket, &lease,
                             FWLAB_SPINE_PUBLICATION_V1_COMMITTED));
    puts("EMPTY_FLUSH_PASS status=success frontier=0 nand_operations=0");
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
    CHECK(empty_flush(runtime, 900));
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

static int lab_runtime_open(struct j0_runtime **out, struct image_context *image,
                             uint32_t mode, uint64_t salt)
{
    struct j0_runtime_config config = {0};
    struct j0_runtime *runtime = calloc(1, sizeof(*runtime));
    CHECK(runtime != NULL);
    config.version = J0_RUNTIME_VERSION;
    config.size = (uint16_t)sizeof(config);
    memcpy(config.media_uuid, image->uuid, sizeof(config.media_uuid));
    config.file = image->file;
    config.media_mode = mode;
    config.generation = config.execution_epoch = 1;
    config.volatile_nonce_seed = salt;
    config.budget_profile = J0_BUDGET_LAB;
    CHECK(j0_runtime_init(runtime, &config) == FWLAB_SPINE_V0_OK);
    if (!runtime_ready(runtime)) {
        fprintf(stderr, "LAB_RECOVERY_FAILURE mode=%u work=%u state=%u quarantine=%u gc_fault=%u gc_live=%u gc_switch=%u record=%u path=%s\n",
                mode, runtime->m3p->work_kind, runtime->m3p->work_state,
                runtime->m3p->quarantined, runtime->m3p->gc_fault_code,
                runtime->m3p->gc_live_count, runtime->m3p->gc_switch_sequence,
                runtime->m3p->record_sequence, image->directory);
        return 0;
    }
    *out = runtime;
    return 1;
}

static int storage_io(struct j0_runtime *runtime, uint64_t uid, uint8_t opcode,
                       uint64_t lba, uint32_t count, uint8_t *bytes,
                       int expect_resource_error)
{
    struct fwlab_nvme_command command = command_make(
        UINT64_C(0x4c414253544f5245), uid, J0_PROFILE_LINUX_V1,
        opcode, lba, count);
    struct j0_host_transfer transfer = transfer_make(
        opcode == 1 ? FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER :
        opcode == 2 ? FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST : 0,
        count * 512u, opcode == 1 ? bytes : NULL);
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_completion_lease_v0 lease;
    struct fwlab_nvme_completion_intent intent;
    enum fwlab_spine_result_v0 result = FWLAB_SPINE_V0_IN_PROGRESS;
    uint32_t iteration;

    if (opcode == 1 && (uid & 1u)) command.command_dword10_15[2] |= UINT32_C(0x40000000);
    CHECK(j0_runtime_admit_start(runtime, J0_PROFILE_LINUX_V1, &command,
                                  &transfer, &ticket) == FWLAB_SPINE_V0_OK);
    for (iteration = 0; iteration < 200000; ++iteration) {
        uint32_t units;
        result = j0_runtime_intent_read(runtime, &ticket, &intent);
        if (result == FWLAB_SPINE_V0_OK) break;
        CHECK(result == FWLAB_SPINE_V0_IN_PROGRESS);
        CHECK(j0_runtime_step(runtime, 3, &units) == FWLAB_SPINE_V0_OK);
    }
    CHECK(result == FWLAB_SPINE_V0_OK);
    if (expect_resource_error) {
        CHECK(intent.status_code_type == 0 && intent.status_code == 6);
        CHECK(intent.do_not_retry == 1);
    } else {
        if (intent.status_code_type || intent.status_code)
            fprintf(stderr, "STORAGE_FAILURE uid=%" PRIu64 " op=%u status=%u:%u dnr=%u nfc_uid=%" PRIu64 " trace=%u host=%u record=%u fault=%u\n",
                    uid, opcode, intent.status_code_type, intent.status_code,
                    intent.do_not_retry, runtime->m3p->next_child_uid,
                    fwlab_nfc_model_trace_count(runtime->nfc_model),
                    runtime->m3p->host_sequence, runtime->m3p->record_sequence,
                    runtime->m3p->operation.status.fault_code);
        CHECK(intent.status_code_type == 0 && intent.status_code == 0);
        if (opcode == 2)
            CHECK(j0_runtime_host_read(runtime, &ticket, bytes, count * 512u) == FWLAB_SPINE_V0_OK);
    }
    CHECK(j0_runtime_publication_acquire(runtime, &ticket, &lease, &intent) == FWLAB_SPINE_V0_OK);
    CHECK(finish_publication(runtime, &ticket, &lease, FWLAB_SPINE_PUBLICATION_V1_COMMITTED));
    return 1;
}

static uint32_t storage_free_pages(const struct j0_runtime *runtime)
{
    uint32_t pages = 0, block;
    for (block = 0; block < M3P_BLOCKS; ++block)
        if (runtime->m3p->block_role[block] == M3P_ROLE_DATA)
            pages += M3P_PAGES_PER_BLOCK - runtime->m3p->block_next_page[block];
    return pages;
}

static int recovered_roles_valid(const struct j0_runtime *runtime)
{
    uint32_t block, lpn, reserves = 0;
    for (block = 0; block < M3P_BLOCKS; ++block)
        reserves += runtime->m3p->block_role[block] == M3P_ROLE_RESERVE;
    CHECK(reserves == 1);
    block = runtime->m3p->reserve_block;
    CHECK(block < M3P_BLOCKS);
    CHECK(runtime->m3p->block_role[block] == M3P_ROLE_RESERVE);
    CHECK(runtime->m3p->block_health[block] == FWLAB_NFC_BLOCK_GOOD);
    CHECK(runtime->m3p->block_next_page[block] == 0);
    for (lpn = 0; lpn < M3P_LPN_COUNT; ++lpn)
        if (runtime->m3p->durable[lpn].state == M3P_L2P_VALUE)
            CHECK(runtime->m3p->block_role[runtime->m3p->durable[lpn].block] == M3P_ROLE_DATA);
    return 1;
}

static int storage_open_image(struct image_context *image)
{
    CHECK(image_create(image));
    image->arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(), fwlab_file_nand_v0_arena_size());
    CHECK(image->arena != NULL);
    CHECK(fwlab_file_nand_v0_posix_restart(image->arena, fwlab_file_nand_v0_arena_size(),
        image->directory_fd, image->name, &image->holder, &image->file) == FWLAB_NFC_API_OK);
    return 1;
}

static int storage_reopen_image(struct image_context *image)
{
    CHECK(fwlab_file_nand_v0_close(image->file) == FWLAB_NFC_API_OK);
    free(image->arena);
    image->arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                  fwlab_file_nand_v0_arena_size());
    CHECK(image->arena != NULL);
    CHECK(fwlab_file_nand_v0_posix_restart(image->arena,
        fwlab_file_nand_v0_arena_size(), image->directory_fd, image->name,
        &image->holder, &image->file) == FWLAB_NFC_API_OK);
    return 1;
}

static int trace_window_sequence(struct j0_runtime *runtime, uint64_t uid)
{
    struct fwlab_nfc_trace_entry last, first;
    uint8_t bytes[4096];
    uint32_t count = fwlab_nfc_model_trace_count(runtime->nfc_model), retired = 0;
    uint64_t child_uid = runtime->m3p->next_child_uid;

    CHECK(count != 0);
    CHECK(fwlab_nfc_model_trace_read(runtime->nfc_model, count - 1u, &last) == FWLAB_NFC_API_OK);
    CHECK(fwlab_nfc_trace_window_retire(runtime->nfc_model, &retired) == FWLAB_NFC_API_OK);
    CHECK(retired == count && fwlab_nfc_model_trace_count(runtime->nfc_model) == 0);
    CHECK(runtime->m3p->next_child_uid == child_uid);
    CHECK(storage_io(runtime, uid, 2, 0, 8, bytes, 0));
    CHECK(fwlab_nfc_model_trace_read(runtime->nfc_model, 0, &first) == FWLAB_NFC_API_OK);
    CHECK(first.sequence > last.sequence);
    puts("TRACE_WINDOW_PASS idle_only=1 sequence_monotonic=1 operation_uid_not_reset=1");
    return 1;
}

static int practical_storage_journey(void)
{
    struct image_context image;
    struct j0_runtime *runtime;
    struct j0_close_status closed;
    uint8_t *expected = calloc(1, FWLAB_M3P_NAMESPACE_LBAS * 512u);
    uint8_t bytes[8192];
    uint64_t uid = 1;
    uint32_t pass, lpn, iteration, boundary;

    CHECK(expected != NULL);
    CHECK(storage_open_image(&image));
    CHECK(lab_runtime_open(&runtime, &image, J0_MEDIA_FORMAT, 31));
    CHECK(empty_flush(runtime, 900));
    CHECK(empty_flush(runtime, 901));
    CHECK(runtime_close(runtime, &closed));
    free(runtime);
    CHECK(storage_reopen_image(&image));
    CHECK(lab_runtime_open(&runtime, &image, J0_MEDIA_RECOVER, 33));
    CHECK(empty_flush(runtime, 902));
    CHECK(empty_flush(runtime, 903));
    for (pass = 0; pass < 3; ++pass) {
        for (lpn = 0; lpn < M3P_LPN_COUNT; ++lpn) {
            memset(bytes, (int)((lpn + pass * 31u) % 251u + 1u), 4096);
            CHECK(storage_io(runtime, uid++, 1, lpn * 8u, 8, bytes, 0));
            memcpy(expected + lpn * 4096u, bytes, 4096);
            if (pass == 0 && lpn == 0)
                CHECK(storage_io(runtime, uid++, 0, 0, 0, NULL, 0));
        }
        printf("PRACTICAL_WRITE_PASS round=%u pages=256 free=%u trace=%u\n", pass,
               storage_free_pages(runtime), fwlab_nfc_model_trace_count(runtime->nfc_model));
    }
    /* Reach both shortages using legal writes, not patched internal counters. */
    for (boundary = 2; boundary >= 1; --boundary) {
        for (iteration = 0; storage_free_pages(runtime) != boundary && iteration < 128; ++iteration) {
            memset(bytes, 0xa6, 4096);
            CHECK(storage_io(runtime, uid++, 1, 0, 8, bytes, 0));
            memcpy(expected, bytes, 4096);
        }
        CHECK(storage_free_pages(runtime) == boundary);
        memset(bytes, (int)(0xc0u + boundary), sizeof(bytes));
        CHECK(storage_io(runtime, uid++, 1, 1, 16, bytes, 0));
        memcpy(expected + 512, bytes, sizeof(bytes));
        CHECK(storage_io(runtime, uid++, 2, 1, 16, bytes, 0));
        CHECK(!memcmp(bytes, expected + 512, sizeof(bytes)));
        printf("PRACTICAL_GC_BOUNDARY_PASS initial_free=%u requested_pages=3\n", boundary);
    }
    CHECK(storage_io(runtime, uid++, 0, 0, 0, NULL, 0));
    for (iteration = 0; iteration < 4096; ++iteration) {
        lpn = iteration % M3P_LPN_COUNT;
        CHECK(storage_io(runtime, uid++, 2, lpn * 8u, 8, bytes, 0));
        CHECK(!memcmp(bytes, expected + lpn * 4096u, 4096));
    }
    CHECK(runtime->nfc_trace_windows > 0);
    printf("PRACTICAL_TRACE_PASS retired_windows=%u current_entries=%u\n",
           runtime->nfc_trace_windows, fwlab_nfc_model_trace_count(runtime->nfc_model));
    CHECK(runtime_close(runtime, &closed));
    free(runtime);
    CHECK(storage_reopen_image(&image));
    CHECK(lab_runtime_open(&runtime, &image, J0_MEDIA_RECOVER, 32));
    CHECK(recovered_roles_valid(runtime));
    for (lpn = 0; lpn < M3P_LPN_COUNT; ++lpn) {
        CHECK(storage_io(runtime, uid++, 2, lpn * 8u, 8, bytes, 0));
        CHECK(!memcmp(bytes, expected + lpn * 4096u, 4096));
    }
    uid += uid & 1u; /* Ordinary write, without FUA. */
    memset(bytes, 0xd9, 4096);
    CHECK(storage_io(runtime, uid++, 1, 0, 8, bytes, 0));
    memcpy(expected, bytes, 4096);
    CHECK(storage_io(runtime, uid++, 0, 0, 0, NULL, 0));
    CHECK(storage_io(runtime, uid++, 2, 0, 8, bytes, 0));
    CHECK(!memcmp(bytes, expected, 4096));
    /* Normal writes, not a private checkpoint trigger, cover the last switch. */
    for (iteration = 0;
         !(runtime->m3p->journal_page == 0 &&
           runtime->m3p->checkpoint_covered_sequence == runtime->m3p->map_sequence) &&
         iteration < 64; ++iteration) {
        uid += uid & 1u;
        memset(bytes, (int)(0x80u + iteration), 4096);
        CHECK(storage_io(runtime, uid++, 1, 0, 8, bytes, 0));
        memcpy(expected, bytes, 4096);
    }
    CHECK(runtime->m3p->journal_page == 0 &&
          runtime->m3p->checkpoint_covered_sequence == runtime->m3p->map_sequence);
    CHECK(runtime_close(runtime, &closed));
    free(runtime);
    CHECK(storage_reopen_image(&image));
    CHECK(lab_runtime_open(&runtime, &image, J0_MEDIA_RECOVER, 34));
    CHECK(recovered_roles_valid(runtime));
    for (iteration = 0; iteration < 64; ++iteration) {
        uid += uid & 1u;
        memset(bytes, (int)(0x40u + iteration), 4096);
        CHECK(storage_io(runtime, uid++, 1, 0, 8, bytes, 0));
        memcpy(expected, bytes, 4096);
        CHECK(storage_io(runtime, uid++, 0, 0, 0, NULL, 0));
        CHECK(storage_io(runtime, uid++, 2, 0, 8, bytes, 0));
        CHECK(!memcmp(bytes, expected, 4096));
    }
    for (lpn = 0; lpn < M3P_LPN_COUNT; ++lpn) {
        CHECK(storage_io(runtime, uid++, 2, lpn * 8u, 8, bytes, 0));
        CHECK(!memcmp(bytes, expected + lpn * 4096u, 4096));
    }
    puts("COVERED_GC_RECOVERY_PASS ordinary_overwrites=64 flush=64 all_page_readback=256 roles=restored");
    CHECK(trace_window_sequence(runtime, uid++));
    CHECK(runtime_close(runtime, &closed));
    free(runtime);
    CHECK(fwlab_file_nand_v0_close(image.file) == FWLAB_NFC_API_OK);
    free(image.arena);
    CHECK(image_destroy(&image));
    free(expected);
    puts("PRACTICAL_STORAGE_PASS fills=1 full_overwrites=2 continued_reads=4096 same_image_restart=1 readback_pages=256");
    return 1;
}

/* These three cuts exercise only the new automatic zero-live reclamation
 * path. The accepted Host lifecycle owns a waiting action, but M3-P has not
 * accepted a Block operation. Child exit models loss of volatile firmware
 * state; the parent reopens the same holder/UUID without formatting it. */
static int automatic_gc_cut_child(struct image_context *image, unsigned cut)
{
    struct j0_runtime *runtime;
    struct j0_close_status closed;
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_nvme_command command;
    struct j0_host_transfer transfer;
    uint8_t bytes[4096];
    uint64_t uid = 1;
    uint32_t lpn, iteration;
    int found = 0;

    CHECK(lab_runtime_open(&runtime, image, J0_MEDIA_FORMAT, 51));
    for (lpn = 0; lpn < 320; ++lpn) {
        memset(bytes, lpn < 256 ? (int)(lpn % 251u + 1u) : 0xa6, sizeof(bytes));
        CHECK(storage_io(runtime, uid++, 1, (lpn % 256u) * 8u, 8, bytes, 0));
    }
    CHECK(storage_io(runtime, uid++, 0, 0, 0, NULL, 0));
    CHECK(storage_free_pages(runtime) == 0);
    memset(bytes, 0xd7, sizeof(bytes));
    command = command_make(UINT64_C(0x4c414253544f5245), uid,
                           J0_PROFILE_LINUX_V1, 1, 0, 8);
    command.command_dword10_15[2] |= UINT32_C(0x40000000);
    transfer = transfer_make(FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER, 4096, bytes);
    CHECK(j0_runtime_admit_start(runtime, J0_PROFILE_LINUX_V1, &command,
                                  &transfer, &ticket) == FWLAB_SPINE_V0_OK);
    for (iteration = 0; iteration < 200000; ++iteration) {
        uint32_t units;
        CHECK(j0_runtime_step(runtime, 1, &units) == FWLAB_SPINE_V0_OK);
        if (runtime->m3p->work_kind != FWLAB_M3P_MAINTENANCE_GC)
            continue;
        CHECK(runtime->m3p->operation.state == M3P_OPERATION_FREE ||
              runtime->m3p->operation.state == M3P_OPERATION_CHECKPOINT);
        CHECK(j0_bytes_zero(&runtime->m3p->operation.request,
                            sizeof(runtime->m3p->operation.request)));
        CHECK(runtime->m3p->gc_live_count == 0);
        if ((cut == 0 && runtime->m3p->child.state == M3P_CHILD_WAIT_FIRST) ||
            (cut == 1 && runtime->m3p->work_state == M3P_GC_SWITCH &&
             runtime->m3p->child.state == M3P_CHILD_IDLE) ||
            (cut == 2 && runtime->m3p->work_state == M3P_GC_ERASE &&
             runtime->m3p->child.state == M3P_CHILD_IDLE)) {
            found = 1;
            break;
        }
    }
    CHECK(found);
    if (cut == 0) {
        uint32_t retired = UINT32_MAX;
        uint64_t hash = fwlab_nfc_model_state_hash(runtime->nfc_model);
        CHECK(fwlab_nfc_trace_window_retire(runtime->nfc_model, &retired) == FWLAB_NFC_API_WRONG_STATE);
        CHECK(retired == UINT32_MAX && fwlab_nfc_model_state_hash(runtime->nfc_model) == hash);
        CHECK(runtime_close(runtime, &closed));
        CHECK(closed.quiescent && closed.profiles_retired);
        free(runtime);
    } else {
        CHECK((runtime->m3p->gc_switch_sequence != 0) == (cut == 2));
        /* Intentionally no close/drain before this disposable process exits. */
    }
    return 1;
}

static int automatic_gc_recovery(void)
{
    unsigned cut;
    for (cut = 0; cut < 3; ++cut) {
        struct image_context image;
        struct j0_runtime *runtime;
        struct j0_close_status closed;
        uint8_t bytes[4096], expected[4096];
        uint32_t lpn;
        int status;
        pid_t child;

        CHECK(storage_open_image(&image));
        child = fork();
        CHECK(child >= 0);
        if (child == 0)
            _exit(automatic_gc_cut_child(&image, cut) ? 0 : 1);
        CHECK(waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        printf("AUTO_GC_CUT_REACHED cut=%u\n", cut);
        CHECK(storage_reopen_image(&image));
        CHECK(lab_runtime_open(&runtime, &image, J0_MEDIA_RECOVER, 52));
        for (lpn = 0; lpn < M3P_LPN_COUNT; ++lpn) {
            memset(expected, lpn < 64 ? 0xa6 : (int)(lpn % 251u + 1u), sizeof(expected));
            CHECK(storage_io(runtime, 1000u + lpn, 2, lpn * 8u, 8, bytes, 0));
            CHECK(memcmp(bytes, expected, sizeof(bytes)) == 0);
        }
        memset(expected, 0x78, sizeof(expected));
        CHECK(storage_io(runtime, 1301, 1, 0, 8, expected, 0));
        CHECK(storage_io(runtime, 1302, 0, 0, 0, NULL, 0));
        CHECK(storage_io(runtime, 1303, 2, 0, 8, bytes, 0));
        CHECK(memcmp(bytes, expected, sizeof(bytes)) == 0);
        CHECK(runtime_close(runtime, &closed));
        free(runtime);
        CHECK(fwlab_file_nand_v0_close(image.file) == FWLAB_NFC_API_OK);
        free(image.arena);
        CHECK(image_destroy(&image));
        printf("AUTO_GC_RECOVERY_PASS cut=%u zero_live=1 host_block_unaccepted=1 readback_pages=256 next_write=success holders=0\n", cut);
    }
    return 1;
}

static int reference_budget_terminal(void)
{
    struct image_context image;
    struct j0_runtime *runtime;
    struct j0_close_status closed;
    uint8_t bytes[4096];
    uint64_t uid = 1;
    uint32_t reads = 0;
    CHECK(storage_open_image(&image));
    CHECK(runtime_open(&runtime, image.file, image.uuid, J0_MEDIA_FORMAT, 41));
    memset(bytes, 0x73, sizeof(bytes));
    CHECK(storage_io(runtime, uid++, 1, 0, 8, bytes, 0));
    /* Observe the unmodified reference budget; no private state is written. */
    while (runtime->m3p->next_child_uid + 37u <= runtime->m3p->config.nfc_operation_uid_limit) {
        CHECK(storage_io(runtime, uid++, 2, 0, 8, bytes, 0));
        CHECK(bytes[0] == 0x73 && bytes[4095] == 0x73);
        CHECK(++reads < 1200);
    }
    CHECK(storage_io(runtime, uid++, 2, 0, 8, bytes, 1));
    CHECK(storage_io(runtime, uid++, 0, 0, 0, NULL, 0));
    CHECK(runtime_close(runtime, &closed));
    free(runtime);
    CHECK(fwlab_file_nand_v0_close(image.file) == FWLAB_NFC_API_OK);
    free(image.arena);
    CHECK(image_destroy(&image));
    printf("REFERENCE_BUDGET_PASS reads=%u exhaustion=terminal DNR=1 final_flush=success holders=0\n", reads);
    return 1;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc == 2 && !strcmp(argv[1], "--practical-storage"))
        return practical_storage_journey() ? 0 : 1;
    if (argc == 2 && !strcmp(argv[1], "--automatic-gc"))
        return automatic_gc_recovery() ? 0 : 1;
    if (argc != 1) return 2;
    CHECK_MAIN(runtime_journey());
    CHECK_MAIN(reference_budget_terminal());
    CHECK_MAIN(practical_storage_journey());
    CHECK_MAIN(automatic_gc_recovery());
    return 0;
}
