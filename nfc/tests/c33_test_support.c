/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c33_test_support.h"

#include <string.h>

struct fwlab_nfc_model_config c33_test_config(void)
{
    struct fwlab_nfc_model_config config;

    memset(&config, 0, sizeof(config));
    config.version = FWLAB_NFC_CONTRACT_VERSION;
    config.size = (uint16_t)sizeof(config);
    config.geometry.version = FWLAB_NFC_CONTRACT_VERSION;
    config.geometry.size = (uint16_t)sizeof(config.geometry);
    config.geometry.channels = 1;
    config.geometry.luns_per_channel = 1;
    config.geometry.planes_per_lun = 2;
    config.geometry.blocks_per_plane = 2;
    config.geometry.pages_per_block = 2;
    config.geometry.plane_parallelism_per_lun = 1;
    config.geometry.main_bytes_per_page = 2;
    config.geometry.oob_bytes_per_page = 1;
    config.geometry.max_programs_per_erase = 1;
    config.geometry.program_order = FWLAB_NFC_PROGRAM_ANY_ORDER;
    config.ecc.version = FWLAB_NFC_CONTRACT_VERSION;
    config.ecc.size = (uint16_t)sizeof(config.ecc);
    config.ecc.main_covered_bytes = 2;
    config.ecc.oob_covered_bytes = 1;
    config.ecc.main_step_bytes = 1;
    config.ecc.oob_step_bytes = 1;
    config.ecc.main_strength_bits = 1;
    config.ecc.oob_strength_bits = 1;
    config.ecc.max_retry_step = 1;
    config.timing.version = FWLAB_NFC_CONTRACT_VERSION;
    config.timing.size = (uint16_t)sizeof(config.timing);
    config.timing.command_ticks = 1;
    config.timing.transfer_ticks_per_unit = 1;
    config.timing.read_array_ticks = 2;
    config.timing.program_setup_ticks = 1;
    config.timing.program_ticks_per_unit = 1;
    config.timing.program_status_ticks = 1;
    config.timing.erase_setup_ticks = 1;
    config.timing.erase_ticks_per_page = 1;
    config.timing.erase_status_ticks = 1;
    config.timing.status_ticks = 1;
    config.fault.version = FWLAB_NFC_FAULT_PROFILE_VERSION;
    config.fault.size = (uint16_t)sizeof(config.fault);
    config.fault.profile_version = 1;
    config.fault.seed = UINT64_C(0x9b6d3e7a4c2158f1);
    config.capacity.version = FWLAB_NFC_CONTRACT_VERSION;
    config.capacity.size = (uint16_t)sizeof(config.capacity);
    config.capacity.operations = 2;
    config.capacity.request_registry = 2;
    config.capacity.terminal_events = 2;
    config.capacity.result_slots = 2;
    config.capacity.trace_entries = 256;
    config.capacity.scratch_main_bytes = 2;
    config.capacity.scratch_oob_bytes = 1;
    config.capacity.operation_generation_limit = 32;
    config.capacity.cache_generation_limit = 32;
    config.capacity.controller_epoch_limit = 8;
    config.capacity.submit_sequence_limit = 256;
    config.capacity.operation_uid_limit = 256;
    config.capacity.virtual_tick_limit = UINT64_C(1000000);
    config.successful_erase_limit = 2;
    return config;
}

int c33_test_init(
    struct c33_test_environment *environment,
    const struct fwlab_nfc_model_config *config,
    const struct fwlab_nfc_factory_bad *factory_bad,
    size_t factory_bad_count,
    uint64_t instance_nonce
)
{
    struct fwlab_nfc_buffer_provider buffers;
    struct fwlab_nand_media media;

    memset(environment, 0, sizeof(*environment));
    c33_fake_buffer_init(&environment->buffer);
    if (!c33_fake_buffer_add(&environment->buffer, 1, 0, 1024) ||
        !c33_fake_buffer_add(&environment->buffer, 2, 1024, 1024) ||
        c33_memory_media_arena_size(&config->geometry) >
            sizeof(environment->media_arena.bytes) ||
        fwlab_nfc_model_arena_size(config) >
            sizeof(environment->model_arena.bytes) ||
        c33_memory_media_init(
            environment->media_arena.bytes,
            sizeof(environment->media_arena.bytes), &config->geometry,
            factory_bad, factory_bad_count, &environment->memory) !=
            FWLAB_NFC_API_OK) {
        return 0;
    }
    buffers = c33_fake_buffer_provider(&environment->buffer);
    media = c33_memory_media_provider(environment->memory);
    if (fwlab_nfc_model_init(
            environment->model_arena.bytes,
            sizeof(environment->model_arena.bytes), config, instance_nonce,
            &buffers, &media, &environment->model) != FWLAB_NFC_API_OK) {
        return 0;
    }
    environment->provider = fwlab_nfc_model_provider(environment->model);
    environment->next_uid = 1;
    environment->instance_nonce = instance_nonce;
    environment->current_epoch = 1;
    return environment->provider.context != NULL;
}

struct fwlab_nfc_request c33_test_request(
    struct c33_test_environment *environment,
    uint8_t kind,
    struct fwlab_nfc_ppa ppa
)
{
    struct fwlab_nfc_request request;

    memset(&request, 0, sizeof(request));
    request.version = FWLAB_NFC_CONTRACT_VERSION;
    request.size = (uint16_t)sizeof(request);
    request.operation.instance_nonce = environment->instance_nonce;
    request.operation.operation_uid = environment->next_uid;
    request.operation.controller_epoch = environment->current_epoch;
    request.operation.generation = (uint32_t)environment->next_uid;
    ++environment->next_uid;
    request.ppa = ppa;
    request.kind = kind;
    request.cookie = UINT64_C(0xabc00000) +
                     request.operation.operation_uid;
    return request;
}

int c33_test_run_event(
    struct c33_test_environment *environment,
    const struct fwlab_nfc_request *request,
    struct fwlab_nfc_completion *completion
)
{
    struct fwlab_nfc_submit_result submit =
        environment->provider.ops->try_submit(
            environment->provider.context, request);
    unsigned int iteration;

    if (submit.disposition != FWLAB_NFC_ACCEPTED) {
        return 0;
    }
    for (iteration = 0; iteration < 512; ++iteration) {
        struct fwlab_nfc_step_result step;
        uint32_t count = 0;

        if (environment->provider.ops->step(
                environment->provider.context, 1, &step) !=
            FWLAB_NFC_API_OK ||
            environment->provider.ops->poll(
                environment->provider.context, 1, completion, 1, &count) !=
            FWLAB_NFC_API_OK) {
            return 0;
        }
        if (count == 1) {
            return 1;
        }
    }
    return 0;
}

int c33_test_program(
    struct c33_test_environment *environment,
    struct fwlab_nfc_ppa ppa,
    const uint8_t main[2],
    uint8_t oob,
    struct fwlab_nfc_completion *completion
)
{
    struct fwlab_nfc_request transfer = c33_test_request(
        environment, FWLAB_NFC_PROGRAM_TRANSFER, ppa);
    struct fwlab_nfc_request execute;

    memcpy(&environment->buffer.bytes[0], main, 2);
    environment->buffer.bytes[16] = oob;
    transfer.region_mask = FWLAB_NFC_REGION_MASK;
    transfer.main = (struct fwlab_nfc_buffer_ref){1, 0, 2, 0};
    transfer.oob = (struct fwlab_nfc_buffer_ref){1, 16, 1, 0};
    if (!c33_test_run_event(environment, &transfer, completion) ||
        completion->terminal != FWLAB_NFC_TERMINAL_SUCCESS) {
        return 0;
    }
    execute = c33_test_request(environment, FWLAB_NFC_PROGRAM_EXECUTE, ppa);
    execute.region_mask = FWLAB_NFC_REGION_MASK;
    execute.cache = completion->cache;
    return c33_test_run_event(environment, &execute, completion);
}

int c33_test_read(
    struct c33_test_environment *environment,
    struct fwlab_nfc_ppa ppa,
    uint8_t retry_step,
    uint8_t main[2],
    uint8_t *oob,
    struct fwlab_nfc_completion *completion
)
{
    struct fwlab_nfc_request trigger = c33_test_request(
        environment, FWLAB_NFC_READ_TRIGGER, ppa);
    struct fwlab_nfc_request transfer;

    trigger.region_mask = FWLAB_NFC_REGION_MASK;
    trigger.retry_step = retry_step;
    if (!c33_test_run_event(environment, &trigger, completion) ||
        completion->terminal != FWLAB_NFC_TERMINAL_SUCCESS) {
        return 0;
    }
    transfer = c33_test_request(environment, FWLAB_NFC_READ_TRANSFER, ppa);
    transfer.region_mask = FWLAB_NFC_REGION_MASK;
    transfer.retry_step = retry_step;
    transfer.cache = completion->cache;
    transfer.main = (struct fwlab_nfc_buffer_ref){2, 0, 2, 0};
    transfer.oob = (struct fwlab_nfc_buffer_ref){2, 16, 1, 0};
    memset(&environment->buffer.bytes[1024], 0xa5, 32);
    if (!c33_test_run_event(environment, &transfer, completion)) {
        return 0;
    }
    memcpy(main, &environment->buffer.bytes[1024], 2);
    *oob = environment->buffer.bytes[1040];
    return 1;
}

int c33_test_erase(
    struct c33_test_environment *environment,
    struct fwlab_nfc_ppa ppa,
    struct fwlab_nfc_completion *completion
)
{
    struct fwlab_nfc_request request;

    ppa.page = 0;
    request = c33_test_request(environment, FWLAB_NFC_ERASE, ppa);
    return c33_test_run_event(environment, &request, completion);
}
