/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _GNU_SOURCE

#include "../m3p_internal.h"
#include "../fakes/m3p_fake_adjacent.h"
#include "../../../media/file-nand-v0/file_nand_internal.h"
#include "fwlab/portable/nfc_model.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "J0-A check failed at %s:%d: %s\n", __FILE__, __LINE__, \
            #expression); return 0; } } while (0)
#define CHECK_MAIN(expression) do { if (!(expression)) { \
    fprintf(stderr, "J0-A check failed at %s:%d: %s\n", __FILE__, __LINE__, \
            #expression); return 1; } } while (0)

struct test_environment {
    void *m3p_arena;
    void *nfc_arena;
    struct fwlab_m3p *m3p;
    struct fwlab_nfc_model *nfc_model;
    struct fwlab_nfc_provider nfc_provider;
    struct m3p_fake_nfc scripted;
    struct m3p_fake_controller_buffer buffer;
    struct fwlab_controller_buffer_port_v0 buffer_port;
    struct fwlab_controller_buffer_lease_v0 lease;
    struct fwlab_block_service_v0 block;
    uint64_t next_action_uid;
};

struct posix_image {
    char directory[128];
    const char *name;
    int directory_fd;
    void *file_arena;
    struct fwlab_file_nand_v0 *file;
    struct fwlab_file_nand_holder_v0 holder;
};

static void *arena_allocate(size_t alignment, size_t size)
{
    size_t rounded = (size + alignment - 1u) & ~(alignment - 1u);

    return aligned_alloc(alignment, rounded);
}

static struct fwlab_nfc_model_config nfc_config(void)
{
    struct fwlab_nfc_model_config config;

    memset(&config, 0, sizeof(config));
    config.version = FWLAB_NFC_CONTRACT_VERSION;
    config.size = (uint16_t)sizeof(config);
    config.geometry = fwlab_file_nand_v0_geometry();
    config.ecc.version = FWLAB_NFC_CONTRACT_VERSION;
    config.ecc.size = (uint16_t)sizeof(config.ecc);
    config.ecc.main_covered_bytes = M3P_PAGE_BYTES;
    config.ecc.oob_covered_bytes = M3P_OOB_BYTES;
    config.ecc.main_step_bytes = 512;
    config.ecc.oob_step_bytes = 16;
    config.ecc.main_strength_bits = 8;
    config.ecc.oob_strength_bits = 4;
    config.ecc.max_retry_step = 3;
    config.timing.version = FWLAB_NFC_CONTRACT_VERSION;
    config.timing.size = (uint16_t)sizeof(config.timing);
    config.timing.command_ticks = 1;
    config.timing.transfer_ticks_per_unit = 1;
    config.timing.read_array_ticks = 8;
    config.timing.program_setup_ticks = 2;
    config.timing.program_ticks_per_unit = 4;
    config.timing.program_status_ticks = 1;
    config.timing.erase_setup_ticks = 2;
    config.timing.erase_ticks_per_page = 2;
    config.timing.erase_status_ticks = 1;
    config.timing.status_ticks = 1;
    config.fault.version = FWLAB_NFC_FAULT_PROFILE_VERSION;
    config.fault.size = (uint16_t)sizeof(config.fault);
    config.fault.profile_version = 1;
    config.fault.seed = UINT64_C(0x4a304e4643563031);
    config.capacity.version = FWLAB_NFC_CONTRACT_VERSION;
    config.capacity.size = (uint16_t)sizeof(config.capacity);
    config.capacity.operations = 4;
    config.capacity.request_registry = 4;
    config.capacity.terminal_events = 4;
    config.capacity.result_slots = 4;
    config.capacity.trace_entries = M3P_TRACE_BOUND;
    config.capacity.scratch_main_bytes = M3P_PAGE_BYTES;
    config.capacity.scratch_oob_bytes = M3P_OOB_BYTES;
    config.capacity.operation_generation_limit = 2048;
    config.capacity.cache_generation_limit = 2048;
    config.capacity.controller_epoch_limit = 64;
    config.capacity.submit_sequence_limit = 2048;
    config.capacity.operation_uid_limit = 2048;
    config.capacity.virtual_tick_limit = UINT64_C(1000000);
    config.successful_erase_limit = 64;
    return config;
}

static struct fwlab_m3p_config m3p_config(const uint8_t uuid[16],
                                          uint64_t nonce)
{
    struct fwlab_m3p_config config;

    memset(&config, 0, sizeof(config));
    config.version = FWLAB_M3P_VERSION;
    config.size = (uint16_t)sizeof(config);
    memcpy(config.media_uuid, uuid, 16);
    config.namespace_ref.word[0] = UINT64_C(0x4a304e5330303031);
    config.namespace_ref.word[1] = UINT64_C(0x4d33504e53303031);
    config.instance_nonce = nonce;
    config.provider_nonce = nonce + 1u;
    config.nfc_instance_nonce = nonce + 2u;
    config.next_nfc_operation_uid = 1;
    config.generation = 1;
    config.execution_epoch = 1;
    config.nfc_epoch = 1;
    config.nfc_operation_uid_limit = 2048;
    config.host_sequence_limit = 512;
    config.record_sequence_limit = 2048;
    return config;
}

static int environment_start(struct test_environment *environment,
                             struct fwlab_file_nand_v0 *file,
                             const uint8_t uuid[16], uint64_t nonce,
                             int scripted)
{
    struct fwlab_m3p_config config = m3p_config(uuid, nonce);
    struct fwlab_nfc_model_config nfc = nfc_config();
    struct fwlab_nfc_buffer_provider staging;
    struct fwlab_nand_media media = fwlab_file_nand_v0_media(file);
    struct fwlab_controller_buffer_acquire_v0 acquire;
    size_t m3p_bytes = fwlab_m3p_arena_size(&config);
    size_t nfc_bytes = fwlab_nfc_model_arena_size(&nfc);
    struct fwlab_nfc_provider provider;

    memset(environment, 0, sizeof(*environment));
    CHECK(media.context != NULL && m3p_bytes != 0 && nfc_bytes != 0);
    environment->m3p_arena = arena_allocate(fwlab_m3p_arena_alignment(),
                                             m3p_bytes);
    CHECK(environment->m3p_arena != NULL);
    staging = m3p_staging_provider(environment->m3p_arena);
    if (scripted) {
        m3p_fake_nfc_init(&environment->scripted, staging, media,
                          config.nfc_instance_nonce, config.nfc_epoch);
        provider = m3p_fake_nfc_provider(&environment->scripted);
    } else {
        environment->nfc_arena = arena_allocate(
            fwlab_nfc_model_arena_alignment(), nfc_bytes);
        CHECK(environment->nfc_arena != NULL);
        CHECK(fwlab_nfc_model_init(environment->nfc_arena, nfc_bytes, &nfc,
            config.nfc_instance_nonce, &staging, &media,
            &environment->nfc_model) == FWLAB_NFC_API_OK);
        provider = fwlab_nfc_model_provider(environment->nfc_model);
    }
    environment->nfc_provider = provider;
    m3p_fake_controller_buffer_init(&environment->buffer, nonce + 3u, 1);
    environment->buffer_port =
        m3p_fake_controller_buffer_port(&environment->buffer);
    CHECK(fwlab_m3p_init(environment->m3p_arena, m3p_bytes, &config,
        &environment->buffer_port, &provider, &environment->m3p) ==
        FWLAB_SPINE_V0_OK);
    memset(&acquire, 0, sizeof(acquire));
    acquire.version = FWLAB_CONTROLLER_BUFFER_V0_VERSION;
    acquire.size = (uint16_t)sizeof(acquire);
    acquire.command.instance_nonce = nonce + 4u;
    acquire.command.command_uid = 1;
    acquire.command.controller_epoch = 1;
    acquire.command.generation = 1;
    acquire.origin.word[0] = nonce + 5u;
    acquire.client_uid = 1;
    acquire.execution_epoch = 1;
    acquire.capacity_bytes = M3P_FAKE_BUFFER_BYTES;
    acquire.rights = FWLAB_CONTROLLER_BUFFER_RIGHT_V0_ALL;
    CHECK(environment->buffer_port.ops->acquire(
        environment->buffer_port.context, &acquire, &environment->lease) ==
        FWLAB_CONTROLLER_BUFFER_V0_OK);
    environment->block = fwlab_m3p_block_service(environment->m3p);
    CHECK(environment->block.context != NULL);
    environment->next_action_uid = 1;
    return 1;
}

static void environment_release(struct test_environment *environment)
{
    free(environment->nfc_arena);
    free(environment->m3p_arena);
    memset(environment, 0, sizeof(*environment));
}

static int pump_until_ready(struct test_environment *environment,
                            int recovery)
{
    unsigned int iteration;

    for (iteration = 0; iteration < 200000; ++iteration) {
        struct fwlab_m3p_step_result step;
        enum fwlab_spine_result_v0 result =
            fwlab_m3p_step(environment->m3p, 3, &step);

        if (result != FWLAB_SPINE_V0_OK) {
            struct m3p_oob debug_oob;
            int decoded = m3p_decode_oob(environment->m3p->frame_oob[0],
                                         &debug_oob);
            fprintf(stderr, "maintenance failed: state=%u page=%u maps=%u "
                    "quarantine=%u child=%u reason=%u decode=%d ogen=%u "
                    "bgen=%u map=%u\n",
                    environment->m3p->work_state,
                    environment->m3p->recovery_page,
                    environment->m3p->recovery_map_count,
                    environment->m3p->quarantined,
                    environment->m3p->child.state,
                    environment->m3p->recovery_fault_code, decoded,
                    decoded ? debug_oob.erase_generation : 0,
                    environment->m3p->block_erase_generation[
                        environment->m3p->recovery_page /
                        M3P_PAGES_PER_BLOCK],
                    decoded ? debug_oob.resulting_map_sequence : 0);
        }
        CHECK(result == FWLAB_SPINE_V0_OK);
        if (environment->m3p->ready &&
            environment->m3p->work_kind == FWLAB_M3P_MAINTENANCE_NONE) {
            if (recovery) {
                struct fwlab_m3p_recovery_status status;

                CHECK(fwlab_m3p_recovery_query(environment->m3p, &status) ==
                      FWLAB_SPINE_V0_OK);
                CHECK(status.state == FWLAB_M3P_MAINTENANCE_SUCCEEDED);
            }
            return 1;
        }
    }
    return 0;
}

static int environment_format(struct test_environment *environment)
{
    CHECK(fwlab_m3p_format_start(environment->m3p) == FWLAB_SPINE_V0_OK);
    return pump_until_ready(environment, 0);
}

static int environment_recover(struct test_environment *environment)
{
    CHECK(fwlab_m3p_recover_start(environment->m3p) == FWLAB_SPINE_V0_OK);
    return pump_until_ready(environment, 1);
}

static struct fwlab_block_request_v0 request_make(
    struct test_environment *environment, uint32_t operation, uint64_t lba,
    uint32_t lba_count, uint32_t durability)
{
    struct fwlab_block_request_v0 request;
    uint16_t kind = operation == FWLAB_BLOCK_V0_READ ?
        FWLAB_HOST_ACTION_V0_BLOCK_READ :
        operation == FWLAB_BLOCK_V0_WRITE ? FWLAB_HOST_ACTION_V0_BLOCK_WRITE :
        operation == FWLAB_BLOCK_V0_FLUSH ? FWLAB_HOST_ACTION_V0_BLOCK_FLUSH :
        FWLAB_HOST_ACTION_V0_BLOCK_TRIM;
    uint64_t uid = environment->next_action_uid++;

    memset(&request, 0, sizeof(request));
    request.version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    request.size = (uint16_t)sizeof(request);
    request.operation_token.version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    request.operation_token.size =
        (uint16_t)sizeof(request.operation_token);
    request.operation_token.type_tag = FWLAB_BLOCK_OP_TOKEN_V0_TAG;
    request.operation_token.action.version =
        FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    request.operation_token.action.size =
        (uint16_t)sizeof(request.operation_token.action);
    request.operation_token.action.type_tag = FWLAB_HOST_ACTION_TOKEN_V0_TAG;
    request.operation_token.action.command.instance_nonce =
        UINT64_C(0x434f4d4d414e4400) + uid;
    request.operation_token.action.command.command_uid = uid;
    request.operation_token.action.command.controller_epoch = 1;
    request.operation_token.action.command.generation = 1;
    request.operation_token.action.origin.word[0] =
        UINT64_C(0x4f524947494e0000) + uid;
    request.operation_token.action.origin.word[1] = uid;
    request.operation_token.action.action_uid = uid;
    request.operation_token.action.generation = 1;
    request.operation_token.action.ordinal = 0;
    request.operation_token.action.kind = kind;
    request.operation_token.provider_nonce =
        environment->block.provider_nonce;
    request.operation_token.generation = environment->block.generation;
    request.namespace_ref = environment->m3p->config.namespace_ref;
    request.lba = lba;
    request.lba_count = lba_count;
    request.operation = operation;
    request.durability = durability;
    if (operation == FWLAB_BLOCK_V0_READ ||
        operation == FWLAB_BLOCK_V0_WRITE) {
        request.buffer_present = 1;
        request.buffer = environment->lease;
        request.buffer_span.version = FWLAB_CONTROLLER_BUFFER_V0_VERSION;
        request.buffer_span.size = (uint16_t)sizeof(request.buffer_span);
        request.buffer_span.length = lba_count * FWLAB_M3P_LBA_BYTES;
    }
    return request;
}

static int run_request(struct test_environment *environment,
                       const struct fwlab_block_request_v0 *request,
                       struct fwlab_block_status_v0 *terminal)
{
    struct fwlab_block_submit_result_v0 submit;
    unsigned int iteration;

    CHECK(environment->block.ops->submit(environment->block.context, request,
        &submit) == FWLAB_SPINE_V0_OK);
    if (submit.disposition != FWLAB_HOST_ACTION_V0_ACCEPTED) {
        fprintf(stderr, "J0-A submit rejected: disposition=%u fault=%u "
                "ready=%u work=%u operation=%u pending=%u journal=%u "
                "host=%u record=%u child_uid=%" PRIu64 "\n",
                submit.disposition, submit.fault_code,
                environment->m3p->ready,
                environment->m3p->work_kind,
                environment->m3p->operation.state,
                environment->m3p->pending_count,
                environment->m3p->journal_page,
                environment->m3p->host_sequence,
                environment->m3p->record_sequence,
                environment->m3p->next_child_uid);
    }
    CHECK(submit.disposition == FWLAB_HOST_ACTION_V0_ACCEPTED);
    for (iteration = 0; iteration < 200000; ++iteration) {
        struct fwlab_m3p_step_result step;
        struct fwlab_block_status_v0 status;

        CHECK(fwlab_m3p_step(environment->m3p, 3, &step) ==
              FWLAB_SPINE_V0_OK);
        CHECK(environment->block.ops->query(environment->block.context,
            &request->operation_token, &status) == FWLAB_SPINE_V0_OK);
        if (status.state == FWLAB_BLOCK_V0_STATE_TERMINAL) {
            if (status.outcome != FWLAB_BLOCK_V0_SUCCEEDED) {
                fprintf(stderr, "J0-A terminal failure: op=%u fault=%u:%u "
                        "effect=%u checkpoint=%u cp_page=%u cp=%u/%u "
                        "journal=%u/%u:%u\n", request->operation,
                        status.fault_domain, status.fault_code,
                        status.effect, environment->m3p->checkpoint_flow,
                        environment->m3p->checkpoint_page,
                        environment->m3p->active_checkpoint_block,
                        environment->m3p->inactive_checkpoint_block,
                        environment->m3p->active_journal_block,
                        environment->m3p->inactive_journal_block,
                        environment->m3p->journal_page);
            }
            CHECK(status.outcome == FWLAB_BLOCK_V0_SUCCEEDED);
            *terminal = status;
            break;
        }
    }
    CHECK(iteration < 200000);
    CHECK(environment->block.ops->retire_start(environment->block.context,
        &request->operation_token) == FWLAB_SPINE_V0_OK);
    for (iteration = 0; iteration < 64; ++iteration) {
        struct fwlab_m3p_step_result step;
        struct fwlab_block_status_v0 status;
        enum fwlab_spine_result_v0 result;

        CHECK(fwlab_m3p_step(environment->m3p, 3, &step) ==
              FWLAB_SPINE_V0_OK);
        result = environment->block.ops->retire_query(
            environment->block.context, &request->operation_token, &status);
        if (result == FWLAB_SPINE_V0_OK) {
            CHECK(status.state == FWLAB_BLOCK_V0_STATE_RETIRED);
            return 1;
        }
        CHECK(result == FWLAB_SPINE_V0_IN_PROGRESS);
    }
    return 0;
}

static int block_write(struct test_environment *environment, uint64_t lba,
                       uint32_t count, uint32_t durability,
                       const uint8_t *bytes,
                       struct fwlab_block_status_v0 *status)
{
    struct fwlab_block_request_v0 request = request_make(
        environment, FWLAB_BLOCK_V0_WRITE, lba, count, durability);

    memcpy(environment->buffer.bytes, bytes,
           (size_t)count * FWLAB_M3P_LBA_BYTES);
    return run_request(environment, &request, status);
}

static int block_read(struct test_environment *environment, uint64_t lba,
                      uint32_t count, uint8_t *bytes)
{
    struct fwlab_block_request_v0 request = request_make(
        environment, FWLAB_BLOCK_V0_READ, lba, count,
        FWLAB_BLOCK_V0_DURABILITY_NONE);
    struct fwlab_block_status_v0 status;

    memset(environment->buffer.bytes, 0xa5,
           (size_t)count * FWLAB_M3P_LBA_BYTES);
    CHECK(run_request(environment, &request, &status));
    memcpy(bytes, environment->buffer.bytes,
           (size_t)count * FWLAB_M3P_LBA_BYTES);
    return 1;
}

static int block_trim(struct test_environment *environment, uint64_t lba,
                      uint32_t count)
{
    struct fwlab_block_request_v0 request = request_make(
        environment, FWLAB_BLOCK_V0_TRIM, lba, count,
        FWLAB_BLOCK_V0_DURABILITY_NONE);
    struct fwlab_block_status_v0 status;

    return run_request(environment, &request, &status);
}

static int block_flush(struct test_environment *environment,
                       struct fwlab_block_status_v0 *status)
{
    struct fwlab_block_request_v0 request = request_make(
        environment, FWLAB_BLOCK_V0_FLUSH, 0, 0,
        FWLAB_BLOCK_V0_DURABILITY_FRONTIER);

    return run_request(environment, &request, status);
}

static int posix_image_create(struct posix_image *image,
                              const uint8_t uuid[16], const char *name)
{
    memset(image, 0, sizeof(*image));
    (void)strcpy(image->directory, "/tmp/fwlab-j0a-XXXXXX");
    CHECK(mkdtemp(image->directory) != NULL);
    CHECK(chmod(image->directory, 0700) == 0);
    image->directory_fd = open(image->directory,
                               O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(image->directory_fd >= 0);
    image->name = name;
    image->file_arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                       fwlab_file_nand_v0_arena_size());
    CHECK(image->file_arena != NULL);
    CHECK(fwlab_file_nand_v0_posix_format(image->file_arena,
        fwlab_file_nand_v0_arena_size(), image->directory_fd, image->name,
        uuid, &image->file, &image->holder) == FWLAB_NFC_API_OK);
    return 1;
}

static int posix_image_restart(struct posix_image *image)
{
    enum fwlab_nfc_api_result result;

    free(image->file_arena);
    image->file_arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                       fwlab_file_nand_v0_arena_size());
    CHECK(image->file_arena != NULL);
    result = fwlab_file_nand_v0_posix_restart(image->file_arena,
        fwlab_file_nand_v0_arena_size(), image->directory_fd, image->name,
        &image->holder, &image->file);
    if (result != FWLAB_NFC_API_OK) {
        fprintf(stderr, "file restart failed: %u\n", (unsigned int)result);
    }
    CHECK(result == FWLAB_NFC_API_OK);
    return 1;
}

static int posix_image_close_for_restart(struct posix_image *image)
{
    CHECK(fwlab_file_nand_v0_close(image->file) == FWLAB_NFC_API_OK);
    return 1;
}

static int posix_image_destroy(struct posix_image *image)
{
    if (image->file != NULL && !image->file->closed) {
        CHECK(fwlab_file_nand_v0_close(image->file) == FWLAB_NFC_API_OK);
    }
    CHECK(unlinkat(image->directory_fd, image->name, 0) == 0);
    CHECK(fsync(image->directory_fd) == 0);
    CHECK(close(image->directory_fd) == 0);
    CHECK(rmdir(image->directory) == 0);
    free(image->file_arena);
    memset(image, 0, sizeof(*image));
    return 1;
}

static int memory_image_restart(
    uint8_t *bytes, const struct fwlab_file_nand_holder_v0 *holder,
    struct m3p_fake_file_substrate *substrate, void **file_arena,
    struct fwlab_file_nand_v0 **file)
{
    m3p_fake_file_substrate_init(substrate, bytes,
        FWLAB_FILE_NAND_V0_IMAGE_BYTES, holder->device, holder->inode);
    substrate->size = FWLAB_FILE_NAND_V0_IMAGE_BYTES;
    *file_arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                 fwlab_file_nand_v0_arena_size());
    CHECK(*file_arena != NULL);
    CHECK(m3p_fake_file_restart(*file_arena,
        fwlab_file_nand_v0_arena_size(), substrate, holder, file) ==
        FWLAB_NFC_API_OK);
    return 1;
}

static int recovery_reaches_quarantine(struct test_environment *environment)
{
    unsigned int iteration;

    CHECK(fwlab_m3p_recover_start(environment->m3p) == FWLAB_SPINE_V0_OK);
    for (iteration = 0; iteration < 200000; ++iteration) {
        struct fwlab_m3p_step_result step;
        struct fwlab_m3p_recovery_status status;

        CHECK(fwlab_m3p_step(environment->m3p, 3, &step) ==
              FWLAB_SPINE_V0_OK);
        CHECK(fwlab_m3p_recovery_query(environment->m3p, &status) ==
              FWLAB_SPINE_V0_OK);
        if (status.state == FWLAB_M3P_MAINTENANCE_QUARANTINED) {
            return 1;
        }
        CHECK(status.state != FWLAB_M3P_MAINTENANCE_SUCCEEDED);
    }
    return 0;
}

static int test_metadata_tail_classification(const uint8_t uuid[16])
{
    uint8_t *bytes = calloc(1, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    uint8_t *tail_bytes = malloc(FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    void *file_arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                      fwlab_file_nand_v0_arena_size());
    void *tail_file_arena = NULL;
    struct m3p_fake_file_substrate substrate;
    struct m3p_fake_file_substrate tail_substrate;
    struct fwlab_file_nand_v0 *file;
    struct fwlab_file_nand_v0 *tail_file;
    struct fwlab_file_nand_holder_v0 holder;
    struct fwlab_nand_media media;
    struct fwlab_nand_media_result result;
    struct test_environment environment;
    struct fwlab_nfc_ppa ppa = {0, 0, 0, 10, 0, 0};
    uint8_t main[M3P_PAGE_BYTES];
    uint8_t oob[M3P_OOB_BYTES];

    CHECK(bytes != NULL && tail_bytes != NULL && file_arena != NULL);
    m3p_fake_file_substrate_init(&substrate, bytes,
        FWLAB_FILE_NAND_V0_IMAGE_BYTES, 37, 41);
    CHECK(m3p_fake_file_format(file_arena,
        fwlab_file_nand_v0_arena_size(), &substrate, uuid, &file, &holder) ==
        FWLAB_NFC_API_OK);
    CHECK(environment_start(&environment, file, uuid,
                            UINT64_C(0xa00000), 0));
    CHECK(environment_format(&environment));
    environment_release(&environment);
    media = fwlab_file_nand_v0_media(file);
    memset(main, 0x35, sizeof(main));
    memset(oob, 0xff, sizeof(oob));
    CHECK(media.ops->program(media.context, &ppa, main, sizeof(main), oob,
        sizeof(oob), sizeof(main) / 2u, 0, FWLAB_NFC_INTEGRITY_TORN,
        &result) == FWLAB_NFC_API_OK);
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_APPLIED &&
          result.integrity == FWLAB_NFC_INTEGRITY_TORN);

    memcpy(tail_bytes, bytes, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    CHECK(memory_image_restart(tail_bytes, &holder, &tail_substrate,
                               &tail_file_arena, &tail_file));
    CHECK(environment_start(&environment, tail_file, uuid,
                            UINT64_C(0xa10000), 0));
    CHECK(environment_recover(&environment));
    CHECK(environment.m3p->map_sequence == 0 &&
          environment.m3p->journal_page == 1);
    environment_release(&environment);
    CHECK(fwlab_file_nand_v0_close(tail_file) == FWLAB_NFC_API_OK);
    free(tail_file_arena);

    ppa.page = 1;
    memset(main, 0x22, sizeof(main));
    memset(oob, 0, sizeof(oob));
    CHECK(media.ops->program(media.context, &ppa, main, sizeof(main), oob,
        sizeof(oob), sizeof(main), sizeof(oob),
        FWLAB_NFC_INTEGRITY_COMPLETE, &result) == FWLAB_NFC_API_OK);
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_APPLIED);
    CHECK(environment_start(&environment, file, uuid,
                            UINT64_C(0xa20000), 0));
    CHECK(recovery_reaches_quarantine(&environment));
    CHECK(environment.m3p->recovery_fault_code == 11);
    environment_release(&environment);
    CHECK(fwlab_file_nand_v0_close(file) == FWLAB_NFC_API_OK);
    free(file_arena);
    free(tail_bytes);
    free(bytes);
    return 1;
}

static int test_checkpoint_rotation(const uint8_t uuid[16],
                                    uint32_t *checkpoint_generation,
                                    uint32_t *checkpoint_covered)
{
    uint8_t *bytes = calloc(1, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    void *file_arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                      fwlab_file_nand_v0_arena_size());
    void *restart_arena = NULL;
    struct m3p_fake_file_substrate substrate;
    struct m3p_fake_file_substrate restart_substrate;
    struct fwlab_file_nand_v0 *file;
    struct fwlab_file_nand_v0 *restarted;
    struct fwlab_file_nand_holder_v0 holder;
    struct test_environment environment;
    struct test_environment recovered;
    struct fwlab_block_request_v0 request;
    struct fwlab_block_submit_result_v0 submit;
    struct fwlab_block_status_v0 status;
    unsigned int iteration;
    uint32_t index;

    CHECK(bytes != NULL && file_arena != NULL);
    m3p_fake_file_substrate_init(&substrate, bytes,
        FWLAB_FILE_NAND_V0_IMAGE_BYTES, 43, 47);
    CHECK(m3p_fake_file_format(file_arena,
        fwlab_file_nand_v0_arena_size(), &substrate, uuid, &file, &holder) ==
        FWLAB_NFC_API_OK);
    CHECK(environment_start(&environment, file, uuid,
                            UINT64_C(0xb00000), 0));
    CHECK(environment_format(&environment));
    for (index = 0; index < 383; ++index) {
        CHECK(block_trim(&environment,
            (uint64_t)(index % M3P_LPN_COUNT) * M3P_SECTORS_PER_PAGE,
            M3P_SECTORS_PER_PAGE));
        CHECK(block_flush(&environment, &status));
    }
    CHECK(block_trim(&environment,
        (uint64_t)(383u % M3P_LPN_COUNT) * M3P_SECTORS_PER_PAGE,
        M3P_SECTORS_PER_PAGE));
    request = request_make(&environment, FWLAB_BLOCK_V0_FLUSH, 0, 0,
                           FWLAB_BLOCK_V0_DURABILITY_FRONTIER);
    CHECK(environment.block.ops->submit(environment.block.context, &request,
        &submit) == FWLAB_SPINE_V0_OK &&
        submit.disposition == FWLAB_HOST_ACTION_V0_ACCEPTED);
    for (iteration = 0; iteration < 200000; ++iteration) {
        struct fwlab_m3p_step_result step;

        CHECK(fwlab_m3p_step(environment.m3p, 1, &step) ==
              FWLAB_SPINE_V0_OK);
        if (environment.m3p->checkpoint_flow == 4 &&
            environment.m3p->child.state == M3P_CHILD_IDLE) {
            break;
        }
    }
    CHECK(iteration < 200000 &&
          environment.m3p->checkpoint_generation == 17 &&
          environment.m3p->active_checkpoint_block == 13 &&
          environment.m3p->inactive_checkpoint_block == 12 &&
          environment.m3p->checkpoint_page == 2 &&
          environment.m3p->block_next_page[12] == M3P_PAGES_PER_BLOCK);
    environment_release(&environment);
    CHECK(memory_image_restart(bytes, &holder, &restart_substrate,
                               &restart_arena, &restarted));
    CHECK(environment_start(&recovered, restarted, uuid,
                            UINT64_C(0xb10000), 0));
    CHECK(environment_recover(&recovered));
    CHECK(recovered.m3p->checkpoint_generation == 17 &&
          recovered.m3p->active_checkpoint_block == 13 &&
          recovered.m3p->inactive_checkpoint_block == 12 &&
          recovered.m3p->host_sequence == 384 &&
          recovered.m3p->block_next_page[12] == 0 &&
          recovered.m3p->block_erase_count[12] == 1);
    *checkpoint_generation = recovered.m3p->checkpoint_generation;
    *checkpoint_covered = recovered.m3p->checkpoint_covered_sequence;
    CHECK(*checkpoint_covered == 384);
    environment_release(&recovered);
    free(restart_arena);
    free(file_arena);
    free(bytes);
    return 1;
}

static int test_checkpoint_journal_handoff(const uint8_t uuid[16])
{
    uint8_t *bytes = calloc(1, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    uint8_t *snapshot = malloc(FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    void *file_arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                      fwlab_file_nand_v0_arena_size());
    void *restart_arena = NULL;
    struct m3p_fake_file_substrate substrate;
    struct m3p_fake_file_substrate restart_substrate;
    struct fwlab_file_nand_v0 *file;
    struct fwlab_file_nand_v0 *restarted;
    struct fwlab_file_nand_holder_v0 holder;
    struct test_environment environment;
    struct test_environment recovered;
    struct fwlab_block_request_v0 request;
    struct fwlab_block_submit_result_v0 submit;
    struct fwlab_block_status_v0 status;
    struct fwlab_block_status_v0 write_status;
    uint8_t page[M3P_PAGE_BYTES];
    uint8_t readback[M3P_PAGE_BYTES];
    unsigned int iteration;
    uint32_t index;

    CHECK(bytes != NULL && snapshot != NULL && file_arena != NULL);
    m3p_fake_file_substrate_init(&substrate, bytes,
        FWLAB_FILE_NAND_V0_IMAGE_BYTES, 53, 59);
    CHECK(m3p_fake_file_format(file_arena,
        fwlab_file_nand_v0_arena_size(), &substrate, uuid, &file, &holder) ==
        FWLAB_NFC_API_OK);
    CHECK(environment_start(&environment, file, uuid,
                            UINT64_C(0xc00000), 0));
    CHECK(environment_format(&environment));
    for (index = 0; index < 23; ++index) {
        CHECK(block_trim(&environment,
            (uint64_t)index * M3P_SECTORS_PER_PAGE,
            M3P_SECTORS_PER_PAGE));
        CHECK(block_flush(&environment, &status));
    }
    CHECK(block_trim(&environment, 23u * M3P_SECTORS_PER_PAGE,
                     M3P_SECTORS_PER_PAGE));
    request = request_make(&environment, FWLAB_BLOCK_V0_FLUSH, 0, 0,
                           FWLAB_BLOCK_V0_DURABILITY_FRONTIER);
    CHECK(environment.block.ops->submit(environment.block.context, &request,
        &submit) == FWLAB_SPINE_V0_OK &&
        submit.disposition == FWLAB_HOST_ACTION_V0_ACCEPTED);
    for (iteration = 0; iteration < 200000; ++iteration) {
        struct fwlab_m3p_step_result step;

        CHECK(fwlab_m3p_step(environment.m3p, 1, &step) ==
              FWLAB_SPINE_V0_OK);
        if (environment.m3p->checkpoint_flow == 3 &&
            environment.m3p->child.state == M3P_CHILD_IDLE) {
            break;
        }
    }
    CHECK(iteration < 200000 && environment.m3p->checkpoint_generation == 2 &&
          environment.m3p->active_journal_block == 11 &&
          environment.m3p->inactive_journal_block == 10);
    memcpy(snapshot, bytes, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    environment_release(&environment);

    CHECK(memory_image_restart(snapshot, &holder, &restart_substrate,
                               &restart_arena, &restarted));
    CHECK(environment_start(&recovered, restarted, uuid,
                            UINT64_C(0xc10000), 0));
    CHECK(environment_recover(&recovered));
    CHECK(recovered.m3p->checkpoint_generation == 2 &&
          recovered.m3p->active_journal_block == 11 &&
          recovered.m3p->journal_page == 0 &&
          recovered.m3p->block_next_page[10] == 0 &&
          recovered.m3p->block_erase_count[10] == 1);
    memset(page, 0x5e, sizeof(page));
    CHECK(block_write(&recovered, 0, M3P_SECTORS_PER_PAGE,
                      FWLAB_BLOCK_V0_DURABILITY_SELF, page, &write_status));
    CHECK(recovered.m3p->active_journal_block == 11 &&
          recovered.m3p->journal_page == 1);
    CHECK(block_read(&recovered, 0, M3P_SECTORS_PER_PAGE, readback));
    CHECK(memcmp(page, readback, sizeof(page)) == 0);
    for (index = 0; index < 23; ++index) {
        CHECK(block_trim(&recovered,
            (uint64_t)(32u + index) * M3P_SECTORS_PER_PAGE,
            M3P_SECTORS_PER_PAGE));
        CHECK(block_flush(&recovered, &status));
    }
    CHECK(recovered.m3p->checkpoint_generation == 3 &&
          recovered.m3p->active_journal_block == 10 &&
          recovered.m3p->journal_page == 0);
    memset(page, 0x6f, sizeof(page));
    CHECK(block_write(&recovered, 100u * M3P_SECTORS_PER_PAGE,
                      M3P_SECTORS_PER_PAGE,
                      FWLAB_BLOCK_V0_DURABILITY_SELF, page, &write_status));
    CHECK(block_read(&recovered, 100u * M3P_SECTORS_PER_PAGE,
                     M3P_SECTORS_PER_PAGE, readback));
    CHECK(memcmp(page, readback, sizeof(page)) == 0);
    environment_release(&recovered);
    free(restart_arena);
    free(file_arena);
    free(snapshot);
    free(bytes);
    return 1;
}

static int test_failed_child_close(const uint8_t uuid[16])
{
    uint8_t *bytes = calloc(1, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    void *file_arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                      fwlab_file_nand_v0_arena_size());
    struct m3p_fake_file_substrate substrate;
    struct fwlab_file_nand_v0 *file;
    struct fwlab_file_nand_holder_v0 holder;
    struct test_environment environment;
    struct fwlab_nand_media media;
    struct fwlab_nfc_ppa bad = {0, 0, 0, 0, 0, 0};
    struct fwlab_block_request_v0 request;
    struct fwlab_block_submit_result_v0 submit;
    struct fwlab_block_status_v0 status;
    struct fwlab_block_epoch_status_v0 block_epoch;
    uint32_t active_leases;
    uint8_t quiescent;
    unsigned int iteration;

    CHECK(bytes != NULL && file_arena != NULL);
    m3p_fake_file_substrate_init(&substrate, bytes,
        FWLAB_FILE_NAND_V0_IMAGE_BYTES, 61, 67);
    CHECK(m3p_fake_file_format(file_arena,
        fwlab_file_nand_v0_arena_size(), &substrate, uuid, &file, &holder) ==
        FWLAB_NFC_API_OK);
    CHECK(environment_start(&environment, file, uuid,
                            UINT64_C(0xd00000), 0));
    CHECK(environment_format(&environment));
    media = fwlab_file_nand_v0_media(file);
    CHECK(media.ops->mark_runtime_bad(media.context, &bad) ==
          FWLAB_NFC_API_OK);
    request = request_make(&environment, FWLAB_BLOCK_V0_WRITE, 0,
                           M3P_SECTORS_PER_PAGE,
                           FWLAB_BLOCK_V0_DURABILITY_SELF);
    memset(environment.buffer.bytes, 0x71, M3P_PAGE_BYTES);
    CHECK(environment.block.ops->submit(environment.block.context, &request,
        &submit) == FWLAB_SPINE_V0_OK &&
        submit.disposition == FWLAB_HOST_ACTION_V0_ACCEPTED);
    for (iteration = 0; iteration < 200000; ++iteration) {
        struct fwlab_m3p_step_result step;

        CHECK(fwlab_m3p_step(environment.m3p, 3, &step) ==
              FWLAB_SPINE_V0_OK);
        CHECK(environment.block.ops->query(environment.block.context,
            &request.operation_token, &status) == FWLAB_SPINE_V0_OK);
        if (status.state == FWLAB_BLOCK_V0_STATE_TERMINAL) {
            break;
        }
    }
    CHECK(iteration < 200000 && status.outcome == FWLAB_BLOCK_V0_FAILED &&
          environment.m3p->child.state == M3P_CHILD_IDLE);
    CHECK(environment.block.ops->retire_start(environment.block.context,
        &request.operation_token) == FWLAB_SPINE_V0_OK);
    for (iteration = 0; iteration < 64; ++iteration) {
        struct fwlab_m3p_step_result step;
        enum fwlab_spine_result_v0 query;

        CHECK(fwlab_m3p_step(environment.m3p, 3, &step) ==
              FWLAB_SPINE_V0_OK);
        query = environment.block.ops->retire_query(
            environment.block.context, &request.operation_token, &status);
        if (query == FWLAB_SPINE_V0_OK) {
            break;
        }
        CHECK(query == FWLAB_SPINE_V0_IN_PROGRESS);
    }
    CHECK(iteration < 64 && status.state == FWLAB_BLOCK_V0_STATE_RETIRED);
    CHECK(environment.buffer_port.ops->release(
        environment.buffer_port.context, &environment.lease) ==
        FWLAB_CONTROLLER_BUFFER_V0_OK);
    CHECK(environment.block.ops->epoch_close(environment.block.context,
        UINT64_C(0xd0c10e), 1) == FWLAB_SPINE_V0_OK);
    CHECK(environment.buffer_port.ops->epoch_close(
        environment.buffer_port.context, UINT64_C(0xd0c10e), 1) ==
        FWLAB_CONTROLLER_BUFFER_V0_OK);
    for (iteration = 0; iteration < 256; ++iteration) {
        struct fwlab_m3p_step_result step;

        CHECK(fwlab_m3p_step(environment.m3p, 3, &step) ==
              FWLAB_SPINE_V0_OK);
        CHECK(fwlab_m3p_epoch_query(environment.m3p, &block_epoch) ==
              FWLAB_SPINE_V0_OK);
        if (block_epoch.quiescent) {
            break;
        }
    }
    CHECK(iteration < 256 && block_epoch.aggregate_operations == 0 &&
          environment.m3p->child.state == M3P_CHILD_IDLE &&
          environment.m3p->nfc_quiescent);
    CHECK(environment.buffer_port.ops->epoch_quiescent(
        environment.buffer_port.context, UINT64_C(0xd0c10e), 1,
        &active_leases, &quiescent) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    CHECK(active_leases == 0 && quiescent == 1);
    CHECK(fwlab_m3p_fini(environment.m3p) == FWLAB_SPINE_V0_OK);
    environment_release(&environment);
    CHECK(fwlab_file_nand_v0_close(file) == FWLAB_NFC_API_OK);
    free(file_arena);
    free(bytes);
    return 1;
}

static int test_basic_and_durability(const uint8_t uuid[16],
                                     uint64_t *ppa_digest,
                                     uint32_t *frontier)
{
    struct posix_image image;
    struct test_environment environment;
    struct fwlab_block_status_v0 status;
    uint8_t input[8192];
    uint8_t output[8192];
    uint8_t sector[512];
    unsigned int index;

    CHECK(posix_image_create(&image, uuid, "media.img"));
    CHECK(environment_start(&environment, image.file, uuid,
                            UINT64_C(0x100000), 0));
    CHECK(environment_format(&environment));
    for (index = 0; index < sizeof(input); ++index) {
        input[index] = (uint8_t)(index * 17u + 3u);
    }
    CHECK(block_write(&environment, 7, 16, FWLAB_BLOCK_V0_DURABILITY_SELF,
                      input, &status));
    CHECK(status.durability_witness == FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE);
    CHECK(block_read(&environment, 7, 16, output));
    CHECK(memcmp(input, output, sizeof(input)) == 0);
    *ppa_digest = m3p_hash64((const uint8_t *)environment.m3p->durable,
                             3 * sizeof(environment.m3p->durable[0]));
    memset(sector, 0x6d, sizeof(sector));
    CHECK(block_write(&environment, 8, 1, FWLAB_BLOCK_V0_DURABILITY_SELF,
                      sector, &status));
    CHECK(block_read(&environment, 7, 16, output));
    CHECK(memcmp(output, input, 512) == 0);
    CHECK(memcmp(&output[512], sector, 512) == 0);
    CHECK(memcmp(&output[1024], &input[1024], sizeof(input) - 1024) == 0);
    CHECK(block_trim(&environment, 9, 1));
    CHECK(block_trim(&environment, 16, 8));
    CHECK(block_flush(&environment, &status));
    CHECK(status.durability_witness ==
          FWLAB_BLOCK_V0_WITNESS_FRONTIER_DURABLE);
    *frontier = (uint32_t)status.frontier.word[1];
    CHECK(*frontier != 0);
    CHECK(block_read(&environment, 7, 16, output));
    CHECK(memcmp(output, input, 512) == 0);
    CHECK(memcmp(&output[512], sector, 512) == 0);
    CHECK(m3p_bytes_zero(&output[1024], 512));
    CHECK(memcmp(&output[1536], &input[1536], 5 * 512) == 0);
    CHECK(m3p_bytes_zero(&output[4096 + 512], 7 * 512));

    memset(sector, 0x33, sizeof(sector));
    CHECK(block_write(&environment, 100, 1,
        FWLAB_BLOCK_V0_DURABILITY_VOLATILE_ALLOWED, sector, &status));
    environment_release(&environment);
    CHECK(posix_image_close_for_restart(&image));
    CHECK(posix_image_restart(&image));
    CHECK(environment_start(&environment, image.file, uuid,
                            UINT64_C(0x200000), 0));
    CHECK(environment_recover(&environment));
    CHECK(block_read(&environment, 100, 1, output));
    CHECK(m3p_bytes_zero(output, 512));

    memset(sector, 0x44, sizeof(sector));
    CHECK(block_write(&environment, 104, 1,
                      FWLAB_BLOCK_V0_DURABILITY_SELF, sector, &status));
    memset(sector, 0x55, sizeof(sector));
    CHECK(block_write(&environment, 105, 1,
        FWLAB_BLOCK_V0_DURABILITY_VOLATILE_ALLOWED, sector, &status));
    environment_release(&environment);
    CHECK(posix_image_close_for_restart(&image));
    CHECK(posix_image_restart(&image));
    CHECK(environment_start(&environment, image.file, uuid,
                            UINT64_C(0x300000), 0));
    CHECK(environment_recover(&environment));
    CHECK(block_read(&environment, 104, 2, output));
    for (index = 0; index < 512; ++index) {
        CHECK(output[index] == 0x44);
    }
    CHECK(m3p_bytes_zero(&output[512], 512));
    environment_release(&environment);
    CHECK(posix_image_destroy(&image));
    return 1;
}

static int test_file_faults(const uint8_t uuid[16])
{
    uint8_t *bytes;
    uint8_t *tail_bytes;
    void *arena;
    void *restart_arena;
    void *tail_arena;
    struct m3p_fake_file_substrate substrate;
    struct m3p_fake_file_substrate tail_substrate;
    struct fwlab_file_nand_v0 *file;
    struct fwlab_file_nand_v0 *restarted;
    struct fwlab_file_nand_holder_v0 holder;
    struct fwlab_nand_media media;
    struct fwlab_nand_media_result result;
    struct fwlab_file_nand_receipt_v0 before;
    struct fwlab_file_nand_receipt_v0 after;
    struct fwlab_nfc_ppa ppa = {0, 0, 0, 0, 0, 0};
    uint8_t main[4096];
    uint8_t oob[128];
    uint64_t corrupt_offset;
    uint64_t tail_marker;

    bytes = calloc(1, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                           fwlab_file_nand_v0_arena_size());
    restart_arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                   fwlab_file_nand_v0_arena_size());
    CHECK(bytes != NULL && arena != NULL && restart_arena != NULL);
    m3p_fake_file_substrate_init(&substrate, bytes,
        FWLAB_FILE_NAND_V0_IMAGE_BYTES, 7, 11);
    CHECK(m3p_fake_file_format(arena, fwlab_file_nand_v0_arena_size(),
        &substrate, uuid, &file, &holder) == FWLAB_NFC_API_OK);
    media = fwlab_file_nand_v0_media(file);
    memset(main, 0xa6, sizeof(main));
    memset(oob, 0x5a, sizeof(oob));
    CHECK(media.ops->program(media.context, &ppa, main, sizeof(main), oob,
        sizeof(oob), sizeof(main), sizeof(oob),
        FWLAB_NFC_INTEGRITY_COMPLETE, &result) == FWLAB_NFC_API_OK);
    tail_bytes = malloc(FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    tail_arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                fwlab_file_nand_v0_arena_size());
    CHECK(tail_bytes != NULL && tail_arena != NULL);
    memcpy(tail_bytes, bytes, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    m3p_fake_file_substrate_init(&tail_substrate, tail_bytes,
        FWLAB_FILE_NAND_V0_IMAGE_BYTES, holder.device, holder.inode);
    tail_substrate.size = FWLAB_FILE_NAND_V0_IMAGE_BYTES;
    tail_marker = FILE_NAND_WAL0 + FILE_NAND_WAL_RECORD_BYTES +
                  2u * FILE_NAND_WAL_RECORD_BYTES + 508u;
    memset(&tail_bytes[tail_marker], 0, 4);
    CHECK(m3p_fake_file_restart(tail_arena,
        fwlab_file_nand_v0_arena_size(), &tail_substrate, &holder,
        &restarted) == FWLAB_NFC_API_OK);
    free(tail_arena);
    free(tail_bytes);

    corrupt_offset = FILE_NAND_WAL0 + FILE_NAND_WAL_RECORD_BYTES + 508u;
    CHECK(m3p_fake_file_corrupt(&substrate, corrupt_offset, 1));
    CHECK(m3p_fake_file_restart(restart_arena,
        fwlab_file_nand_v0_arena_size(), &substrate, &holder, &restarted) ==
        FWLAB_NFC_API_INVARIANT_FAILURE);
    free(restart_arena);
    free(arena);
    free(bytes);

    bytes = calloc(1, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                           fwlab_file_nand_v0_arena_size());
    CHECK(bytes != NULL && arena != NULL);
    m3p_fake_file_substrate_init(&substrate, bytes,
        FWLAB_FILE_NAND_V0_IMAGE_BYTES, 13, 17);
    CHECK(m3p_fake_file_format(arena, fwlab_file_nand_v0_arena_size(),
        &substrate, uuid, &file, &holder) == FWLAB_NFC_API_OK);
    CHECK(fwlab_file_nand_v0_receipt(file, &before) == FWLAB_NFC_API_OK);
    substrate.fail_barrier = substrate.barriers + 1u;
    media = fwlab_file_nand_v0_media(file);
    CHECK(media.ops->program(media.context, &ppa, main, sizeof(main), oob,
        sizeof(oob), sizeof(main), sizeof(oob),
        FWLAB_NFC_INTEGRITY_COMPLETE, &result) ==
        FWLAB_NFC_API_INVARIANT_FAILURE);
    substrate.fail_barrier = 0;
    CHECK(fwlab_file_nand_v0_receipt(file, &after) == FWLAB_NFC_API_OK);
    CHECK(after.physical_generation == before.physical_generation);
    free(arena);
    free(bytes);
    return 1;
}

static int test_scripted_nfc(const uint8_t uuid[16])
{
    uint8_t *bytes = calloc(1, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    void *file_arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                      fwlab_file_nand_v0_arena_size());
    struct m3p_fake_file_substrate substrate;
    struct fwlab_file_nand_v0 *file;
    struct fwlab_file_nand_holder_v0 holder;
    struct test_environment environment;
    struct fwlab_block_status_v0 status;
    uint8_t page[4096];
    uint8_t readback[4096];

    CHECK(bytes != NULL && file_arena != NULL);
    m3p_fake_file_substrate_init(&substrate, bytes,
        FWLAB_FILE_NAND_V0_IMAGE_BYTES, 29, 31);
    CHECK(m3p_fake_file_format(file_arena,
        fwlab_file_nand_v0_arena_size(), &substrate, uuid, &file, &holder) ==
        FWLAB_NFC_API_OK);
    CHECK(environment_start(&environment, file, uuid,
                            UINT64_C(0x3a0000), 1));
    CHECK(environment_format(&environment));
    memset(page, 0x9c, sizeof(page));
    CHECK(block_write(&environment, 0, M3P_SECTORS_PER_PAGE,
                      FWLAB_BLOCK_V0_DURABILITY_SELF, page, &status));
    CHECK(block_read(&environment, 0, M3P_SECTORS_PER_PAGE, readback));
    CHECK(memcmp(page, readback, sizeof(page)) == 0);
    CHECK(environment.scripted.submissions >= 10 &&
          environment.scripted.steps != 0 &&
          environment.scripted.polls != 0 &&
          environment.nfc_model == NULL && environment.nfc_arena == NULL);
    environment_release(&environment);
    free(file_arena);
    free(bytes);
    return 1;
}

static int test_gc_and_scripted(const uint8_t uuid[16],
                                struct fwlab_m3p_gc_status *gc)
{
    uint8_t *bytes = calloc(1, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    uint8_t *staged_bytes = malloc(FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    uint8_t *switched_bytes = malloc(FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    uint8_t *b15_bytes = malloc(FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    void *file_arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                      fwlab_file_nand_v0_arena_size());
    void *restart_file_arena;
    void *staged_file_arena = NULL;
    void *switched_file_arena = NULL;
    void *b15_file_arena = NULL;
    void *b15_verify_arena = NULL;
    struct m3p_fake_file_substrate substrate;
    struct m3p_fake_file_substrate staged_substrate;
    struct m3p_fake_file_substrate switched_substrate;
    struct m3p_fake_file_substrate b15_substrate;
    struct fwlab_file_nand_v0 *file;
    struct fwlab_file_nand_v0 *restarted;
    struct fwlab_file_nand_v0 *staged_file;
    struct fwlab_file_nand_v0 *switched_file;
    struct fwlab_file_nand_v0 *b15_file;
    struct fwlab_file_nand_v0 *b15_verify_file;
    struct fwlab_file_nand_holder_v0 holder;
    struct fwlab_nand_media media;
    struct fwlab_nfc_ppa failed_erase = {0, 0, 0, 0, 0, 0};
    struct test_environment environment;
    struct test_environment recovered;
    struct test_environment staged_recovered;
    struct test_environment switched_recovered;
    struct test_environment b15_recovered;
    struct test_environment b15_verified;
    struct fwlab_block_status_v0 status;
    uint8_t page[4096];
    uint8_t readback[4096];
    uint8_t allocation_block;
    uint8_t allocation_page;
    uint32_t checkpoint_generation;
    uint32_t checkpoint_covered;
    int staged_captured = 0;
    int switched_captured = 0;
    int b15_triggered = 0;
    uint32_t lpn;
    uint32_t overwrite;
    unsigned int iteration;

    CHECK(bytes != NULL && staged_bytes != NULL && switched_bytes != NULL &&
          b15_bytes != NULL && file_arena != NULL);
    m3p_fake_file_substrate_init(&substrate, bytes,
        FWLAB_FILE_NAND_V0_IMAGE_BYTES, 19, 23);
    CHECK(m3p_fake_file_format(file_arena,
        fwlab_file_nand_v0_arena_size(), &substrate, uuid, &file, &holder) ==
        FWLAB_NFC_API_OK);
    CHECK(environment_start(&environment, file, uuid,
                            UINT64_C(0x400000), 0));
    CHECK(environment_format(&environment));
    for (lpn = 0; lpn < M3P_LPN_COUNT; ++lpn) {
        memset(page, (int)(lpn + 1u), sizeof(page));
        CHECK(block_write(&environment,
            (uint64_t)lpn * M3P_SECTORS_PER_PAGE, M3P_SECTORS_PER_PAGE,
            FWLAB_BLOCK_V0_DURABILITY_SELF, page, &status));
    }
    for (overwrite = 0; overwrite < 64; ++overwrite) {
        lpn = (overwrite % 8u) * 32u + overwrite / 8u;
        memset(page, (int)(0x80u + overwrite), sizeof(page));
        CHECK(block_write(&environment,
            (uint64_t)lpn * M3P_SECTORS_PER_PAGE, M3P_SECTORS_PER_PAGE,
            FWLAB_BLOCK_V0_DURABILITY_SELF, page, &status));
    }
    CHECK(fwlab_m3p_force_gc_start(environment.m3p) == FWLAB_SPINE_V0_OK);
    for (iteration = 0; iteration < 300000; ++iteration) {
        struct fwlab_m3p_step_result step;

        CHECK(fwlab_m3p_step(environment.m3p, 1, &step) ==
              FWLAB_SPINE_V0_OK);
        if (!staged_captured && environment.m3p->gc_moved == 1 &&
            environment.m3p->work_state == M3P_GC_READ &&
            environment.m3p->child.state == M3P_CHILD_IDLE) {
            memcpy(staged_bytes, bytes, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
            staged_captured = 1;
        }
        if (!switched_captured &&
            environment.m3p->work_state == M3P_GC_ERASE &&
            environment.m3p->child.state == M3P_CHILD_IDLE) {
            memcpy(switched_bytes, bytes, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
            memcpy(b15_bytes, bytes, FWLAB_FILE_NAND_V0_IMAGE_BYTES);
            switched_captured = 1;
            failed_erase.block = environment.m3p->gc_victim;
            media = fwlab_file_nand_v0_media(file);
            CHECK(media.ops->mark_runtime_bad(media.context, &failed_erase) ==
                  FWLAB_NFC_API_OK);
            b15_triggered = 1;
        }
        CHECK(fwlab_m3p_force_gc_query(environment.m3p, gc) ==
              FWLAB_SPINE_V0_OK);
        if (gc->state == FWLAB_M3P_MAINTENANCE_SUCCEEDED) {
            break;
        }
    }
    CHECK(iteration < 300000 && staged_captured && switched_captured &&
          b15_triggered && environment.m3p->replacement_used == 1 &&
          environment.m3p->reserve_block == 15 &&
          environment.m3p->block_role[environment.m3p->gc_victim] ==
              M3P_ROLE_UNAVAILABLE &&
          gc->victim_block <= 9 &&
          gc->live_pages <= 25 && gc->reclaimable_pages >= 7 &&
          gc->gc_uid != 0 && gc->switch_map_sequence != 0);
    environment_release(&environment);

    CHECK(memory_image_restart(staged_bytes, &holder, &staged_substrate,
                               &staged_file_arena, &staged_file));
    CHECK(environment_start(&staged_recovered, staged_file, uuid,
                            UINT64_C(0x410000), 0));
    CHECK(environment_recover(&staged_recovered));
    CHECK(staged_recovered.m3p->gc_moved ==
              staged_recovered.m3p->gc_live_count &&
          staged_recovered.m3p->gc_switch_sequence != 0 &&
          staged_recovered.m3p->block_next_page[
              staged_recovered.m3p->gc_victim] == 0);
    memset(page, 0x80, sizeof(page));
    CHECK(block_read(&staged_recovered, 0, M3P_SECTORS_PER_PAGE, readback));
    CHECK(memcmp(page, readback, sizeof(page)) == 0);
    environment_release(&staged_recovered);

    CHECK(memory_image_restart(switched_bytes, &holder, &switched_substrate,
                               &switched_file_arena, &switched_file));
    CHECK(environment_start(&switched_recovered, switched_file, uuid,
                            UINT64_C(0x420000), 0));
    CHECK(environment_recover(&switched_recovered));
    CHECK(switched_recovered.m3p->gc_switch_sequence != 0 &&
          switched_recovered.m3p->block_next_page[
              switched_recovered.m3p->gc_victim] == 0);
    CHECK(fwlab_m3p_force_gc_query(switched_recovered.m3p, gc) ==
          FWLAB_SPINE_V0_OK);
    CHECK(gc->state == FWLAB_M3P_MAINTENANCE_SUCCEEDED &&
          gc->successful_erase_count != 0);
    memset(page, 0x80, sizeof(page));
    CHECK(block_read(&switched_recovered, 0, M3P_SECTORS_PER_PAGE, readback));
    CHECK(memcmp(page, readback, sizeof(page)) == 0);
    allocation_block = gc->destination_block;
    allocation_page =
        switched_recovered.m3p->block_next_page[allocation_block];
    CHECK(allocation_page == gc->live_pages);
    memset(page, 0x6a, sizeof(page));
    CHECK(block_write(&switched_recovered,
        (uint64_t)(M3P_LPN_COUNT - 1u) * M3P_SECTORS_PER_PAGE,
        M3P_SECTORS_PER_PAGE, FWLAB_BLOCK_V0_DURABILITY_SELF, page, &status));
    CHECK(switched_recovered.m3p->durable[M3P_LPN_COUNT - 1u].block ==
              allocation_block &&
          switched_recovered.m3p->durable[M3P_LPN_COUNT - 1u].page ==
              allocation_page);
    checkpoint_generation = switched_recovered.m3p->checkpoint_generation;
    checkpoint_covered =
        switched_recovered.m3p->checkpoint_covered_sequence;
    CHECK(checkpoint_generation != 0 && checkpoint_covered != 0);
    environment_release(&switched_recovered);

    CHECK(memory_image_restart(b15_bytes, &holder, &b15_substrate,
                               &b15_file_arena, &b15_file));
    CHECK(environment_start(&b15_recovered, b15_file, uuid,
                            UINT64_C(0x430000), 0));
    CHECK(fwlab_m3p_recover_start(b15_recovered.m3p) == FWLAB_SPINE_V0_OK);
    for (iteration = 0; iteration < 200000; ++iteration) {
        struct fwlab_m3p_step_result step;

        CHECK(fwlab_m3p_step(b15_recovered.m3p, 1, &step) ==
              FWLAB_SPINE_V0_OK);
        if (!b15_recovered.m3p->ready &&
            b15_recovered.m3p->work_kind == FWLAB_M3P_MAINTENANCE_GC &&
            b15_recovered.m3p->work_state == M3P_GC_ERASE &&
            b15_recovered.m3p->child.state == M3P_CHILD_IDLE) {
            break;
        }
    }
    CHECK(iteration < 200000);
    failed_erase.block = b15_recovered.m3p->gc_victim;
    media = fwlab_file_nand_v0_media(b15_file);
    CHECK(media.ops->mark_runtime_bad(media.context, &failed_erase) ==
          FWLAB_NFC_API_OK);
    CHECK(pump_until_ready(&b15_recovered, 1));
    CHECK(b15_recovered.m3p->replacement_used == 1 &&
          b15_recovered.m3p->reserve_block == 15 &&
          b15_recovered.m3p->block_role[
              b15_recovered.m3p->gc_victim] == M3P_ROLE_UNAVAILABLE &&
          b15_recovered.m3p->block_health[
              b15_recovered.m3p->gc_victim] ==
                  FWLAB_NFC_BLOCK_RUNTIME_BAD);
    environment_release(&b15_recovered);
    b15_verify_arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                      fwlab_file_nand_v0_arena_size());
    CHECK(b15_verify_arena != NULL);
    CHECK(m3p_fake_file_restart(b15_verify_arena,
        fwlab_file_nand_v0_arena_size(), &b15_substrate, &holder,
        &b15_verify_file) == FWLAB_NFC_API_OK);
    CHECK(environment_start(&b15_verified, b15_verify_file, uuid,
                            UINT64_C(0x440000), 0));
    CHECK(environment_recover(&b15_verified));
    CHECK(b15_verified.m3p->replacement_used == 1 &&
          b15_verified.m3p->reserve_block == 15 &&
          b15_verified.m3p->block_role[failed_erase.block] ==
              M3P_ROLE_UNAVAILABLE);
    memset(page, 0x80, sizeof(page));
    CHECK(block_read(&b15_verified, 0, M3P_SECTORS_PER_PAGE, readback));
    CHECK(memcmp(page, readback, sizeof(page)) == 0);
    environment_release(&b15_verified);

    restart_file_arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                                        fwlab_file_nand_v0_arena_size());
    CHECK(restart_file_arena != NULL);
    CHECK(m3p_fake_file_restart(restart_file_arena,
        fwlab_file_nand_v0_arena_size(), &switched_substrate, &holder,
        &restarted) == FWLAB_NFC_API_OK);
    CHECK(environment_start(&recovered, restarted, uuid,
                            UINT64_C(0x500000), 0));
    CHECK(environment_recover(&recovered));
    lpn = 0;
    memset(page, 0x80, sizeof(page));
    CHECK(block_read(&recovered, 0, M3P_SECTORS_PER_PAGE, readback));
    CHECK(memcmp(page, readback, sizeof(page)) == 0);
    memset(page, 0x6a, sizeof(page));
    CHECK(block_read(&recovered,
        (uint64_t)(M3P_LPN_COUNT - 1u) * M3P_SECTORS_PER_PAGE,
        M3P_SECTORS_PER_PAGE, readback));
    CHECK(memcmp(page, readback, sizeof(page)) == 0);
    environment_release(&recovered);
    free(b15_verify_arena);
    free(b15_file_arena);
    free(switched_file_arena);
    free(staged_file_arena);
    free(restart_file_arena);
    free(file_arena);
    free(b15_bytes);
    free(switched_bytes);
    free(staged_bytes);
    free(bytes);
    return 1;
}

static int parse_uuid(const char *text, uint8_t uuid[16])
{
    unsigned int index;

    if (text == NULL || strlen(text) != 32) {
        return 0;
    }
    for (index = 0; index < 16; ++index) {
        unsigned int value;

        if (sscanf(&text[index * 2u], "%2x", &value) != 1) {
            return 0;
        }
        uuid[index] = (uint8_t)value;
    }
    return 1;
}

static void uuid_text(const uint8_t uuid[16], char text[33])
{
    unsigned int index;

    for (index = 0; index < 16; ++index) {
        (void)snprintf(&text[index * 2u], 3, "%02x", uuid[index]);
    }
    text[32] = '\0';
}

static int cut_worker(int argc, char **argv)
{
    uint8_t uuid[16];
    uint8_t main[4096];
    uint8_t oob_bytes[128];
    struct m3p_oob oob;
    struct fwlab_file_nand_holder_v0 holder;
    struct fwlab_file_nand_v0 *file;
    struct fwlab_nand_media media;
    struct fwlab_nand_media_result result;
    struct fwlab_file_nand_receipt_v0 receipt;
    struct fwlab_file_nand_cut_key_v0 key;
    struct fwlab_file_nand_cut_status_v0 cut;
    struct fwlab_nfc_ppa ppa = {0, 0, 0, 0, 0, 0};
    struct stat executable;
    void *arena;
    int directory_fd;
    int phase;
    enum fwlab_nfc_api_result call_result;

    if (argc != 10 || !parse_uuid(argv[7], uuid)) {
        _exit(92);
    }
    phase = strcmp(argv[2], "before") == 0 ?
        FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0 :
        strcmp(argv[2], "after") == 0 ?
            FWLAB_FILE_NAND_CUT_AFTER_EFFECT_V0 : 0;
    memset(&holder, 0, sizeof(holder));
    holder.device = strtoull(argv[5], NULL, 10);
    holder.inode = strtoull(argv[6], NULL, 10);
    memcpy(holder.media_uuid, uuid, 16);
    directory_fd = open(argv[3], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                           fwlab_file_nand_v0_arena_size());
    if (phase == 0 || directory_fd < 0 || arena == NULL ||
        stat("/proc/self/exe", &executable) != 0 ||
        (uint64_t)executable.st_dev != strtoull(argv[8], NULL, 10) ||
        (uint64_t)executable.st_ino != strtoull(argv[9], NULL, 10) ||
        fwlab_file_nand_v0_posix_restart(arena,
            fwlab_file_nand_v0_arena_size(), directory_fd, argv[4], &holder,
            &file) != FWLAB_NFC_API_OK ||
        fwlab_file_nand_v0_receipt(file, &receipt) != FWLAB_NFC_API_OK) {
        _exit(92);
    }
    memset(&key, 0, sizeof(key));
    key.version = FWLAB_FILE_NAND_V0_VERSION;
    key.size = (uint16_t)sizeof(key);
    key.expected_transaction_uid = receipt.next_transaction_uid;
    key.phase = (uint8_t)phase;
    key.one_shot = 1;
    if (fwlab_file_nand_v0_cut_arm(file, &key) != FWLAB_NFC_API_OK) {
        _exit(92);
    }
    memset(main, 0x7c, sizeof(main));
    memset(&oob, 0, sizeof(oob));
    oob.page_type = M3P_PAGE_DATA;
    oob.flags = 1;
    oob.copy_kind = M3P_COPY_HOST;
    oob.valid_mask = UINT8_MAX;
    oob.namespace_id = 1;
    oob.lpn = 0;
    oob.record_sequence = 101;
    oob.transaction_sequence = 1;
    oob.referenced_data_sequence = 101;
    oob.main_length = M3P_PAGE_BYTES;
    oob.main_crc = m3p_crc32c(main, sizeof(main));
    memcpy(oob.media_uuid, uuid, 16);
    oob.source_block = UINT8_MAX;
    oob.source_page = UINT8_MAX;
    m3p_encode_oob(oob_bytes, &oob);
    media = fwlab_file_nand_v0_media(file);
    call_result = media.ops->program(media.context, &ppa, main, sizeof(main),
        oob_bytes, sizeof(oob_bytes), sizeof(main), sizeof(oob_bytes),
        FWLAB_NFC_INTEGRITY_COMPLETE, &result);
    if (call_result != FWLAB_NFC_API_INVARIANT_FAILURE ||
        fwlab_file_nand_v0_cut_query(file, &cut) != FWLAB_NFC_API_OK ||
        cut.state != FWLAB_FILE_NAND_CUT_FIRED_V0 ||
        cut.key.expected_transaction_uid != receipt.next_transaction_uid) {
        _exit(92);
    }
    _exit(phase == FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0 ? 90 : 91);
}

static int recovery_worker(int argc, char **argv)
{
    uint8_t uuid[16];
    struct fwlab_file_nand_holder_v0 holder;
    struct fwlab_file_nand_v0 *file;
    struct fwlab_file_nand_receipt_v0 receipt;
    struct test_environment environment;
    struct stat executable;
    void *arena;
    uint64_t baseline_generation;
    uint64_t physical_delta;
    int directory_fd;
    int expected_orphan;
    int orphan;
    int phase;
    int exit_code = 92;

    if (argc != 11 || !parse_uuid(argv[7], uuid)) {
        return 92;
    }
    phase = strcmp(argv[2], "before") == 0 ?
        FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0 :
        strcmp(argv[2], "after") == 0 ?
            FWLAB_FILE_NAND_CUT_AFTER_EFFECT_V0 : 0;
    expected_orphan =
        phase == FWLAB_FILE_NAND_CUT_AFTER_EFFECT_V0 ? 1 : 0;
    baseline_generation = strtoull(argv[10], NULL, 10);
    memset(&holder, 0, sizeof(holder));
    holder.device = strtoull(argv[5], NULL, 10);
    holder.inode = strtoull(argv[6], NULL, 10);
    memcpy(holder.media_uuid, uuid, sizeof(holder.media_uuid));
    directory_fd = open(argv[3], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    arena = arena_allocate(fwlab_file_nand_v0_arena_alignment(),
                           fwlab_file_nand_v0_arena_size());
    if (phase == 0 || directory_fd < 0 || arena == NULL ||
        stat("/proc/self/exe", &executable) != 0 ||
        (uint64_t)executable.st_dev != strtoull(argv[8], NULL, 10) ||
        (uint64_t)executable.st_ino != strtoull(argv[9], NULL, 10) ||
        fwlab_file_nand_v0_posix_restart(arena,
            fwlab_file_nand_v0_arena_size(), directory_fd, argv[4], &holder,
            &file) != FWLAB_NFC_API_OK ||
        fwlab_file_nand_v0_receipt(file, &receipt) != FWLAB_NFC_API_OK ||
        receipt.physical_generation < baseline_generation) {
        free(arena);
        if (directory_fd >= 0) {
            (void)close(directory_fd);
        }
        return 92;
    }
    physical_delta = receipt.physical_generation - baseline_generation;
    if (physical_delta !=
            (phase == FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0 ? 0u : 1u) ||
        !environment_start(&environment, file, uuid,
            phase == FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0 ?
                UINT64_C(0x800000) : UINT64_C(0x900000), 0) ||
        !environment_recover(&environment) ||
        environment.m3p->durable[0].state != M3P_L2P_UNMAPPED) {
        (void)fwlab_file_nand_v0_close(file);
        free(arena);
        (void)close(directory_fd);
        return 92;
    }
    orphan = environment.m3p->p2l[0] == M3P_P2L_ORPHAN;
    if (orphan == expected_orphan) {
        exit_code = 93 + (int)(physical_delta * 2u) + orphan;
    }
    environment_release(&environment);
    if (fwlab_file_nand_v0_close(file) != FWLAB_NFC_API_OK ||
        close(directory_fd) != 0) {
        exit_code = 92;
    }
    free(arena);
    return exit_code;
}

static int run_cut_case(const uint8_t uuid[16], int phase, int executable_fd,
                        const struct stat *executable, uint64_t *physical_delta,
                        int *orphan)
{
    struct posix_image image;
    struct test_environment environment;
    struct fwlab_file_nand_receipt_v0 before;
    char device[32];
    char inode[32];
    char exec_device[32];
    char exec_inode[32];
    char baseline_generation[32];
    char uuid_string[33];
    char *cut_argv[11];
    char *recovery_argv[12];
    pid_t child;
    int wait_status;
    int recovery_status;

    CHECK(posix_image_create(&image, uuid,
        phase == FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0 ?
            "before.img" : "after.img"));
    CHECK(environment_start(&environment, image.file, uuid,
        phase == FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0 ?
            UINT64_C(0x600000) : UINT64_C(0x700000), 0));
    CHECK(environment_format(&environment));
    CHECK(fwlab_file_nand_v0_receipt(image.file, &before) ==
          FWLAB_NFC_API_OK);
    environment_release(&environment);
    CHECK(posix_image_close_for_restart(&image));
    (void)snprintf(device, sizeof(device), "%" PRIu64, image.holder.device);
    (void)snprintf(inode, sizeof(inode), "%" PRIu64, image.holder.inode);
    (void)snprintf(exec_device, sizeof(exec_device), "%" PRIu64,
                   (uint64_t)executable->st_dev);
    (void)snprintf(exec_inode, sizeof(exec_inode), "%" PRIu64,
                   (uint64_t)executable->st_ino);
    (void)snprintf(baseline_generation, sizeof(baseline_generation),
                   "%" PRIu64, before.physical_generation);
    uuid_text(uuid, uuid_string);
    cut_argv[0] = (char *)"j0a_lower_matrix";
    cut_argv[1] = (char *)"--cut-worker";
    cut_argv[2] = phase == FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0 ?
        (char *)"before" : (char *)"after";
    cut_argv[3] = image.directory;
    cut_argv[4] = (char *)image.name;
    cut_argv[5] = device;
    cut_argv[6] = inode;
    cut_argv[7] = uuid_string;
    cut_argv[8] = exec_device;
    cut_argv[9] = exec_inode;
    cut_argv[10] = NULL;
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        execveat(executable_fd, "", cut_argv, environ, AT_EMPTY_PATH);
        _exit(92);
    }
    CHECK(waitpid(child, &wait_status, 0) == child && WIFEXITED(wait_status));
    CHECK(WEXITSTATUS(wait_status) ==
        (phase == FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0 ? 90 : 91));
    recovery_argv[0] = (char *)"j0a_lower_matrix";
    recovery_argv[1] = (char *)"--recovery-worker";
    recovery_argv[2] = cut_argv[2];
    recovery_argv[3] = image.directory;
    recovery_argv[4] = (char *)image.name;
    recovery_argv[5] = device;
    recovery_argv[6] = inode;
    recovery_argv[7] = uuid_string;
    recovery_argv[8] = exec_device;
    recovery_argv[9] = exec_inode;
    recovery_argv[10] = baseline_generation;
    recovery_argv[11] = NULL;
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        execveat(executable_fd, "", recovery_argv, environ, AT_EMPTY_PATH);
        _exit(92);
    }
    CHECK(waitpid(child, &wait_status, 0) == child && WIFEXITED(wait_status));
    recovery_status = WEXITSTATUS(wait_status);
    CHECK(recovery_status ==
        (phase == FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0 ? 93 : 96));
    *physical_delta = (uint64_t)(recovery_status - 93) / 2u;
    *orphan = (recovery_status - 93) & 1;
    CHECK(posix_image_destroy(&image));
    return 1;
}

static int digest_valid(const char *digest)
{
    size_t index;

    if (digest == NULL || strlen(digest) != 64) {
        return 0;
    }
    for (index = 0; index < 64; ++index) {
        if (!((digest[index] >= '0' && digest[index] <= '9') ||
              (digest[index] >= 'a' && digest[index] <= 'f'))) {
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    static const uint8_t uuid[16] = {
        0x4a, 0x30, 0x41, 0x2d, 0x4d, 0x33, 0x50, 0x2d,
        0x46, 0x49, 0x4c, 0x45, 0x2d, 0x30, 0x30, 0x31
    };
    const char *m3p_sha = NULL;
    const char *nfc_sha = NULL;
    const char *file_sha = NULL;
    const char *elf_sha = NULL;
    struct fwlab_m3p_gc_status gc;
    struct stat executable;
    uint64_t ppa_digest;
    uint64_t before_delta;
    uint64_t after_delta;
    uint32_t frontier;
    uint32_t checkpoint_generation;
    uint32_t checkpoint_covered;
    char uuid_string[33];
    int before_orphan;
    int after_orphan;
    int executable_fd;
    int index;

    if (argc > 1 && strcmp(argv[1], "--cut-worker") == 0) {
        return cut_worker(argc, argv);
    }
    if (argc > 1 && strcmp(argv[1], "--recovery-worker") == 0) {
        _exit(recovery_worker(argc, argv));
    }
    CHECK_MAIN(argc == 9);
    for (index = 1; index < argc; index += 2) {
        if (strcmp(argv[index], "--m3p-sha") == 0) {
            m3p_sha = argv[index + 1];
        } else if (strcmp(argv[index], "--nfc-sha") == 0) {
            nfc_sha = argv[index + 1];
        } else if (strcmp(argv[index], "--file-sha") == 0) {
            file_sha = argv[index + 1];
        } else if (strcmp(argv[index], "--elf-sha") == 0) {
            elf_sha = argv[index + 1];
        } else {
            return 1;
        }
    }
    CHECK_MAIN(digest_valid(m3p_sha) && digest_valid(nfc_sha) &&
               digest_valid(file_sha) && digest_valid(elf_sha));
    executable_fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    CHECK_MAIN(executable_fd >= 0 && fstat(executable_fd, &executable) == 0);
    CHECK_MAIN(test_basic_and_durability(uuid, &ppa_digest, &frontier));
    CHECK_MAIN(test_file_faults(uuid));
    CHECK_MAIN(test_metadata_tail_classification(uuid));
    CHECK_MAIN(test_scripted_nfc(uuid));
    CHECK_MAIN(test_checkpoint_rotation(uuid, &checkpoint_generation,
                                        &checkpoint_covered));
    CHECK_MAIN(test_checkpoint_journal_handoff(uuid));
    CHECK_MAIN(test_failed_child_close(uuid));
    CHECK_MAIN(test_gc_and_scripted(uuid, &gc));
    CHECK_MAIN(run_cut_case(uuid, FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0,
                            executable_fd, &executable, &before_delta,
                            &before_orphan));
    CHECK_MAIN(run_cut_case(uuid, FWLAB_FILE_NAND_CUT_AFTER_EFFECT_V0,
                            executable_fd, &executable, &after_delta,
                            &after_orphan));
    CHECK_MAIN(close(executable_fd) == 0);
    uuid_text(uuid, uuid_string);
    printf("J0A_BINDING|m3p=%s|nfc=%s|file=%s|elf=%s|uuid=%s|"
           "geometry=1x1x1x16x32x4096+128\n",
           m3p_sha, nfc_sha, file_sha, elf_sha, uuid_string);
    printf("J0A_ADJACENCY|block_upper=1|scripted_nfc_lower=1|"
           "file_memory_lower=1|barrier_fault=1\n");
    printf("J0A_RMW|one_sector=1|unaligned_16_lba=1|lpn_span=3|"
           "data_ppas=%016" PRIx64 "\n", ppa_digest);
    printf("J0A_TRIM|partial_mask=1|whole_tombstone=1|flush_survives=1\n");
    printf("J0A_DURABILITY|volatile_lost=1|self_survives=1|frontier=%u|"
           "later_uncovered=0\n", frontier);
    printf("J0A_CHECKPOINT|generation=%u|covered=%u|tail_ignored=1|"
           "interior_quarantine=1\n", checkpoint_generation,
           checkpoint_covered);
    printf("J0A_GC|victim=%u|live=%u|reclaimable=%u|gc_uid=%u|switch=%u|"
           "erase_count=%u\n", gc.victim_block, gc.live_pages,
           gc.reclaimable_pages, gc.gc_uid, gc.switch_map_sequence,
           gc.successful_erase_count);
    printf("J0A_CUT|phase=BEFORE|exit=90|physical_delta=%" PRIu64
           "|logical=old|elf_devino=%" PRIx64 ":%" PRIx64 "|elf=%s\n",
           before_delta, (uint64_t)executable.st_dev,
           (uint64_t)executable.st_ino, elf_sha);
    printf("J0A_CUT|phase=AFTER|exit=91|physical_delta=%" PRIu64
           "|logical=old|p2l=ORPHAN|elf_devino=%" PRIx64 ":%" PRIx64
           "|elf=%s\n", after_delta, (uint64_t)executable.st_dev,
           (uint64_t)executable.st_ino, elf_sha);
    printf("J0-A lower matrix: PASS (rows=9 artifacts=1)\n");
    return 0;
}
