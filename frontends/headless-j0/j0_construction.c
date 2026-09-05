/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "j0_internal.h"
#include "m3p_internal.h"

#include <stdlib.h>
#include <string.h>

static void *arena_allocate(size_t alignment, size_t size)
{
    size_t rounded;

    if (alignment == 0 || size == 0 || (alignment & (alignment - 1u)) != 0 ||
        size > SIZE_MAX - (alignment - 1u)) {
        return NULL;
    }
    rounded = (size + alignment - 1u) & ~(alignment - 1u);
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

static struct fwlab_m3p_config m3p_config(
    const struct j0_runtime *runtime)
{
    struct fwlab_m3p_config config;

    memset(&config, 0, sizeof(config));
    config.version = FWLAB_M3P_VERSION;
    config.size = (uint16_t)sizeof(config);
    memcpy(config.media_uuid, runtime->config.media_uuid,
           sizeof(config.media_uuid));
    config.namespace_ref = runtime->namespace_ref;
    config.instance_nonce = runtime->m3p_instance_nonce;
    config.provider_nonce = J0_M3P_PROVIDER_NONCE +
                            runtime->config.volatile_nonce_seed;
    config.nfc_instance_nonce = runtime->nfc_instance_nonce;
    config.next_nfc_operation_uid = 1;
    config.generation = runtime->config.generation;
    config.execution_epoch = runtime->config.execution_epoch;
    config.nfc_epoch = runtime->config.execution_epoch;
    config.nfc_operation_uid_limit = 2048;
    config.host_sequence_limit = 512;
    config.record_sequence_limit = 2048;
    return config;
}

static struct fwlab_host_lifecycle_config_v0 lifecycle_config(
    const struct j0_runtime *runtime)
{
    struct fwlab_host_lifecycle_config_v0 config;

    memset(&config, 0, sizeof(config));
    config.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    config.size = (uint16_t)sizeof(config);
    config.lifecycle_instance_nonce = runtime->lifecycle_instance_nonce;
    config.execution_epoch = runtime->config.execution_epoch;
    config.generation = runtime->config.generation;
    config.command_capacity = J0_MAX_COMMANDS;
    config.actions_per_command = FWLAB_HOST_ACTION_V0_MAX_ACTIONS;
    config.command_uid.next = 1;
    config.command_uid.maximum = 4096;
    config.action_uid.next = 101;
    config.action_uid.maximum = 8192;
    config.abort_uid.next = 9001;
    config.abort_uid.maximum = 9100;
    config.completion_lease_uid.next = 12001;
    config.completion_lease_uid.maximum = 12100;
    return config;
}

static int runtime_config_valid(const struct j0_runtime_config *config)
{
    return config != NULL && config->version == J0_RUNTIME_VERSION &&
           config->size == sizeof(*config) && config->reserved0 == 0 &&
           !j0_bytes_zero(config->media_uuid, sizeof(config->media_uuid)) &&
           config->file != NULL &&
           (config->media_mode == J0_MEDIA_FORMAT ||
            config->media_mode == J0_MEDIA_RECOVER) &&
           config->generation != 0 && config->execution_epoch != 0 &&
           config->volatile_nonce_seed != 0 &&
           config->volatile_nonce_seed < UINT64_C(0x100000) &&
           j0_bytes_zero(config->reserved1, sizeof(config->reserved1));
}

static void arena_release(struct j0_runtime *runtime)
{
    free(runtime->m3p_arena);
    free(runtime->nfc_arena);
    free(runtime->linux_arena);
    free(runtime->c43_arena);
    free(runtime->lifecycle_arena);
    runtime->m3p_arena = NULL;
    runtime->nfc_arena = NULL;
    runtime->linux_arena = NULL;
    runtime->c43_arena = NULL;
    runtime->lifecycle_arena = NULL;
}

static const struct fwlab_spine_profile_binding_v0 *profile_binding(
    const struct j0_runtime *runtime, uint32_t profile)
{
    if (profile == J0_PROFILE_C43_P1) {
        return &runtime->c43_binding;
    }
    if (profile == J0_PROFILE_LINUX_V1) {
        return &runtime->linux_binding;
    }
    return NULL;
}

static enum fwlab_spine_result_v0 admission_poison(struct j0_runtime *runtime)
{
    runtime->poisoned = 1;
    runtime->admission_closed = 1;
    return FWLAB_SPINE_V0_POISONED;
}

static int binding_equal(
    const struct fwlab_spine_profile_binding_v0 *left,
    const struct fwlab_spine_profile_binding_v0 *right)
{
    return fwlab_spine_profile_binding_v0_valid(right) &&
           left->adapter.ops == right->adapter.ops &&
           left->adapter.context == right->adapter.context &&
           left->adapter.generation == right->adapter.generation &&
           left->adapter_instance_nonce == right->adapter_instance_nonce &&
           left->generation == right->generation &&
           left->argument_read == right->argument_read &&
           left->payload_read == right->payload_read &&
           left->result_latch == right->result_latch &&
           left->relation_source == right->relation_source &&
           left->relation_source_context == right->relation_source_context &&
           left->relation_sink == right->relation_sink &&
           left->relation_context == right->relation_context;
}

static struct j0_admission_record *admission_identity_find(
    struct j0_runtime *runtime,
    const struct fwlab_nvme_command *command)
{
    struct j0_admission_record *match = NULL;
    uint32_t index;

    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        struct j0_admission_record *record = &runtime->admission[index];

        if (!record->occupied) {
            continue;
        }
        if (j0_handle_equal(&record->command.handle, &command->handle) ||
            j0_origin_equal(&record->command.origin, &command->origin)) {
            if (!j0_handle_equal(&record->command.handle, &command->handle) ||
                !j0_origin_equal(&record->command.origin, &command->origin) ||
                match != NULL) {
                (void)admission_poison(runtime);
                return NULL;
            }
            match = record;
        }
    }
    return match;
}

static struct j0_admission_record *admission_ticket_find(
    struct j0_runtime *runtime,
    const struct fwlab_spine_command_ticket_v0 *ticket)
{
    uint32_t index;

    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        struct j0_admission_record *record = &runtime->admission[index];

        if (record->occupied && record->lifecycle_owned &&
            fwlab_spine_command_ticket_v0_equal(&record->ticket, ticket)) {
            return record;
        }
    }
    return NULL;
}

static struct j0_admission_record *admission_free(
    struct j0_runtime *runtime)
{
    uint32_t index;

    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        if (!runtime->admission[index].occupied) {
            return &runtime->admission[index];
        }
    }
    return NULL;
}

static int transfer_equal(
    const struct j0_admission_record *record,
    const struct j0_host_transfer *transfer)
{
    if (record->transfer_direction != transfer->direction ||
        record->transfer_bytes != transfer->exact_bytes) {
        return 0;
    }
    return transfer->direction != FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER ||
           (transfer->input != NULL &&
            memcmp(record->transfer_copy, transfer->input,
                   transfer->exact_bytes) == 0);
}

static int transfer_contract(
    const struct fwlab_host_action_program_v0 *program,
    const struct fwlab_spine_profile_argument_v0 *argument,
    uint32_t *direction, uint32_t *exact_bytes, uint8_t *buffer_required)
{
    uint32_t index;
    uint32_t found_direction = 0;
    uint32_t found_bytes = 0;

    *buffer_required = 0;
    for (index = 0; index < program->action_count; ++index) {
        const uint16_t kind = program->action[index].kind;

        if (kind == FWLAB_HOST_ACTION_V0_PAYLOAD_FILL ||
            kind == FWLAB_HOST_ACTION_V0_DMA_IN ||
            kind == FWLAB_HOST_ACTION_V0_DMA_OUT ||
            kind == FWLAB_HOST_ACTION_V0_BLOCK_READ ||
            kind == FWLAB_HOST_ACTION_V0_BLOCK_WRITE) {
            if (argument[index].exact_bytes == 0 ||
                argument[index].exact_bytes > J0_MAX_TRANSFER_BYTES ||
                (found_bytes != 0 &&
                 found_bytes != argument[index].exact_bytes)) {
                return 0;
            }
            found_bytes = argument[index].exact_bytes;
            *buffer_required = 1;
        }
        if (kind == FWLAB_HOST_ACTION_V0_DMA_IN ||
            kind == FWLAB_HOST_ACTION_V0_DMA_OUT) {
            const uint32_t current =
                kind == FWLAB_HOST_ACTION_V0_DMA_IN
                    ? FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER
                    : FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST;

            if (found_direction != 0 && found_direction != current) {
                return 0;
            }
            found_direction = current;
        }
    }
    if ((*buffer_required && (found_direction == 0 || found_bytes == 0)) ||
        (!*buffer_required && (found_direction != 0 || found_bytes != 0))) {
        return 0;
    }
    *direction = found_direction;
    *exact_bytes = found_bytes;
    return 1;
}

static int supplied_transfer_valid(
    const struct j0_host_transfer *transfer,
    uint32_t direction, uint32_t exact_bytes)
{
    if (transfer == NULL || transfer->version != J0_RUNTIME_VERSION ||
        transfer->size != sizeof(*transfer) || transfer->reserved0 != 0 ||
        !j0_bytes_zero(transfer->reserved1, sizeof(transfer->reserved1)) ||
        transfer->direction != direction ||
        transfer->exact_bytes != exact_bytes) {
        return 0;
    }
    if (direction == 0) {
        return exact_bytes == 0 && transfer->input == NULL;
    }
    if (direction == FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER) {
        return exact_bytes != 0 && transfer->input != NULL;
    }
    return direction == FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST &&
           exact_bytes != 0 && transfer->input == NULL;
}

static enum fwlab_spine_result_v0 release_resources(
    struct j0_runtime *runtime, struct j0_admission_record *record)
{
    enum fwlab_spine_result_v0 result;

    if (record->authority_held) {
        result = runtime->host.port.ops->authority_release(
            runtime->host.port.context, &record->authority);
        if (result != FWLAB_SPINE_V0_OK) {
            return result;
        }
        record->authority_held = 0;
    }
    if (record->buffer_held) {
        enum fwlab_controller_buffer_result_v0 buffer_result =
            runtime->buffer.port.ops->release(
                runtime->buffer.port.context, &record->buffer);

        if (buffer_result != FWLAB_CONTROLLER_BUFFER_V0_OK) {
            return FWLAB_SPINE_V0_POISONED;
        }
        record->buffer_held = 0;
    }
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 release_endpoint(
    struct j0_runtime *runtime, struct j0_admission_record *record)
{
    enum fwlab_spine_result_v0 result;

    if (!record->endpoint_held) {
        return FWLAB_SPINE_V0_OK;
    }
    result = j0_host_endpoint_release(
        &runtime->host, &record->command.handle, &record->command.origin);
    if (result == FWLAB_SPINE_V0_OK) {
        record->endpoint_held = 0;
    }
    return result;
}

static enum fwlab_spine_result_v0 rollback_drive(
    struct j0_runtime *runtime, struct j0_admission_record *record)
{
    enum fwlab_spine_result_v0 result;
    uint32_t failure = record->original_failure;

    result = record->binding.adapter.ops->retire(
        record->binding.adapter.context, &record->program);
    if (result == FWLAB_SPINE_V0_IN_PROGRESS) {
        return result;
    }
    if (result != FWLAB_SPINE_V0_OK) {
        runtime->poisoned = 1;
        return FWLAB_SPINE_V0_POISONED;
    }
    result = release_resources(runtime, record);
    if (result != FWLAB_SPINE_V0_OK) {
        runtime->poisoned = 1;
        return result;
    }
    if (record->endpoint_held) {
        result = release_endpoint(runtime, record);
        if (result != FWLAB_SPINE_V0_OK) {
            runtime->poisoned = 1;
            return result;
        }
    }
    memset(record, 0, sizeof(*record));
    return (enum fwlab_spine_result_v0)failure;
}

static enum fwlab_spine_result_v0 resources_acquire(
    struct j0_runtime *runtime, struct j0_admission_record *record,
    uint8_t buffer_required)
{
    struct fwlab_controller_buffer_acquire_v0 buffer_request;
    struct fwlab_host_dma_mint_request_v0 authority_request;
    enum fwlab_spine_result_v0 result;
    enum fwlab_controller_buffer_result_v0 buffer_result;

    if (!buffer_required) {
        record->phase = J0_ADMISSION_RESOURCES_HELD;
        return FWLAB_SPINE_V0_OK;
    }
    result = j0_host_endpoint_prepare(
        &runtime->host, &record->command.handle, &record->command.origin,
        record->transfer_direction, record->transfer_bytes,
        record->transfer_direction ==
                FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER
            ? record->transfer_copy
            : NULL);
    if (result != FWLAB_SPINE_V0_OK) {
        return result;
    }
    record->endpoint_held = 1;
    memset(&buffer_request, 0, sizeof(buffer_request));
    buffer_request.version = FWLAB_CONTROLLER_BUFFER_V0_VERSION;
    buffer_request.size = (uint16_t)sizeof(buffer_request);
    buffer_request.command = record->command.handle;
    buffer_request.origin = record->command.origin;
    buffer_request.client_uid = runtime->next_client_uid++;
    buffer_request.execution_epoch = runtime->config.execution_epoch;
    buffer_request.capacity_bytes = record->transfer_bytes;
    buffer_request.rights = FWLAB_CONTROLLER_BUFFER_RIGHT_V0_ALL;
    buffer_result = runtime->buffer.port.ops->acquire(
        runtime->buffer.port.context, &buffer_request, &record->buffer);
    if (buffer_result != FWLAB_CONTROLLER_BUFFER_V0_OK) {
        return buffer_result == FWLAB_CONTROLLER_BUFFER_V0_NO_CAPACITY
                   ? FWLAB_SPINE_V0_NO_CAPACITY
                   : FWLAB_SPINE_V0_POISONED;
    }
    record->buffer_held = 1;
    memset(&record->span, 0, sizeof(record->span));
    record->span.version = FWLAB_CONTROLLER_BUFFER_V0_VERSION;
    record->span.size = (uint16_t)sizeof(record->span);
    record->span.length = record->transfer_bytes;
    memset(&authority_request, 0, sizeof(authority_request));
    authority_request.version = FWLAB_HOST_DATA_V0_VERSION;
    authority_request.size = (uint16_t)sizeof(authority_request);
    authority_request.command = record->command.handle;
    authority_request.origin = record->command.origin;
    authority_request.client_uid = runtime->next_client_uid++;
    authority_request.execution_epoch = runtime->config.execution_epoch;
    authority_request.exact_bytes = record->transfer_bytes;
    authority_request.direction = (uint8_t)record->transfer_direction;
    result = runtime->host.port.ops->authority_mint(
        runtime->host.port.context, &authority_request, &record->authority);
    if (result != FWLAB_SPINE_V0_OK) {
        return result;
    }
    record->authority_held = 1;
    record->phase = J0_ADMISSION_RESOURCES_HELD;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 j0_runtime_init(
    struct j0_runtime *runtime,
    const struct j0_runtime_config *config)
{
    struct fwlab_m3p_config m3p;
    struct fwlab_nfc_model_config nfc;
    struct fwlab_nfc_buffer_provider staging;
    struct fwlab_nand_media media;
    struct fwlab_host_lifecycle_config_v0 lifecycle;
    size_t m3p_size;
    size_t nfc_size;
    enum fwlab_spine_result_v0 result = FWLAB_SPINE_V0_INVALID;

    if (runtime == NULL || !runtime_config_valid(config)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->magic = J0_RUNTIME_MAGIC;
    runtime->config = *config;
    runtime->lifecycle_instance_nonce =
        J0_LIFECYCLE_NONCE + config->volatile_nonce_seed;
    runtime->m3p_instance_nonce =
        J0_M3P_INSTANCE_NONCE + config->volatile_nonce_seed;
    runtime->nfc_instance_nonce =
        J0_NFC_INSTANCE_NONCE + config->volatile_nonce_seed;
    runtime->namespace_ref.word[0] = UINT64_C(0x4a304e5330303031);
    runtime->namespace_ref.word[1] = UINT64_C(0x4d33504e53303031);
    runtime->next_client_uid = UINT64_C(40001);

    j0_controller_buffer_init(
        &runtime->buffer,
        J0_BUFFER_ISSUER_NONCE + config->volatile_nonce_seed,
        config->generation);
    j0_host_data_init(
        &runtime->host, &runtime->buffer,
        J0_AUTHORITY_ISSUER_NONCE + config->volatile_nonce_seed,
        J0_DMA_ISSUER_NONCE + config->volatile_nonce_seed,
        config->generation);
    if (!fwlab_controller_buffer_port_v0_valid(&runtime->buffer.port) ||
        !fwlab_host_data_port_v0_valid(&runtime->host.port)) {
        goto failed;
    }

    runtime->lifecycle_arena = arena_allocate(
        fwlab_spine_lifecycle_v0_arena_alignment(),
        fwlab_spine_lifecycle_v0_arena_size());
    runtime->c43_arena = arena_allocate(
        fwlab_c43_p1_adapter_v0_arena_alignment(),
        fwlab_c43_p1_adapter_v0_arena_size());
    runtime->linux_arena = arena_allocate(
        fwlab_linux_profile_v1_adapter_arena_alignment(),
        fwlab_linux_profile_v1_adapter_arena_size());
    if (runtime->lifecycle_arena == NULL || runtime->c43_arena == NULL ||
        runtime->linux_arena == NULL) {
        result = FWLAB_SPINE_V0_NO_CAPACITY;
        goto failed;
    }
    if (fwlab_c43_p1_adapter_v0_init(
            runtime->c43_arena, fwlab_c43_p1_adapter_v0_arena_size(),
            J0_C43_ADAPTER_NONCE + config->volatile_nonce_seed,
            config->generation, &runtime->c43_adapter) !=
            FWLAB_SPINE_V0_OK ||
        fwlab_linux_profile_v1_adapter_init(
            runtime->linux_arena,
            fwlab_linux_profile_v1_adapter_arena_size(),
            J0_LINUX_ADAPTER_NONCE + config->volatile_nonce_seed,
            config->generation, &runtime->linux_adapter) !=
            FWLAB_SPINE_V0_OK ||
        fwlab_c43_p1_binding_v0(
            &runtime->c43_adapter, FWLAB_SPINE_ROLE_V0_NORMAL,
            &runtime->c43_binding) != FWLAB_SPINE_V0_OK ||
        fwlab_linux_profile_v1_binding_v0(
            &runtime->linux_adapter, FWLAB_SPINE_ROLE_V0_NORMAL,
            &runtime->linux_binding) != FWLAB_SPINE_V0_OK) {
        goto failed;
    }

    m3p = m3p_config(runtime);
    nfc = nfc_config();
    m3p_size = fwlab_m3p_arena_size(&m3p);
    nfc_size = fwlab_nfc_model_arena_size(&nfc);
    runtime->m3p_arena = arena_allocate(fwlab_m3p_arena_alignment(), m3p_size);
    runtime->nfc_arena = arena_allocate(
        fwlab_nfc_model_arena_alignment(), nfc_size);
    if (runtime->m3p_arena == NULL || runtime->nfc_arena == NULL) {
        result = FWLAB_SPINE_V0_NO_CAPACITY;
        goto failed;
    }
    staging = m3p_staging_provider(runtime->m3p_arena);
    media = fwlab_file_nand_v0_media(config->file);
    if (fwlab_nfc_model_init(
            runtime->nfc_arena, nfc_size, &nfc,
            runtime->nfc_instance_nonce, &staging, &media,
            &runtime->nfc_model) != FWLAB_NFC_API_OK) {
        goto failed;
    }
    runtime->nfc_provider = fwlab_nfc_model_provider(runtime->nfc_model);
    if (fwlab_m3p_init(
            runtime->m3p_arena, m3p_size, &m3p, &runtime->buffer.port,
            &runtime->nfc_provider, &runtime->m3p) != FWLAB_SPINE_V0_OK) {
        goto failed;
    }
    runtime->block = fwlab_m3p_block_service(runtime->m3p);
    if (!fwlab_block_service_v0_valid(&runtime->block)) {
        goto failed;
    }
    j0_action_drivers_init(runtime, &runtime->drivers);
    lifecycle = lifecycle_config(runtime);
    if (fwlab_spine_lifecycle_v0_init(
            runtime->lifecycle_arena,
            fwlab_spine_lifecycle_v0_arena_size(), &lifecycle,
            &runtime->drivers) != FWLAB_SPINE_V0_OK) {
        goto failed;
    }
    result = config->media_mode == J0_MEDIA_FORMAT
                 ? fwlab_m3p_format_start(runtime->m3p)
                 : fwlab_m3p_recover_start(runtime->m3p);
    if (result != FWLAB_SPINE_V0_OK) {
        goto failed;
    }
    return FWLAB_SPINE_V0_OK;

failed:
    arena_release(runtime);
    memset(runtime, 0, sizeof(*runtime));
    return result;
}

enum fwlab_spine_result_v0 j0_runtime_admit_start(
    struct j0_runtime *runtime, uint32_t profile,
    const struct fwlab_nvme_command *command,
    const struct j0_host_transfer *transfer,
    struct fwlab_spine_command_ticket_v0 *ticket)
{
    const struct fwlab_spine_profile_binding_v0 *binding;
    struct j0_admission_record *record;
    struct fwlab_host_action_program_v0 program;
    uint32_t direction = 0;
    uint32_t exact_bytes = 0;
    uint8_t buffer_required = 0;
    uint32_t index;
    enum fwlab_spine_result_v0 result;
    int existing;

    if (runtime == NULL || runtime->magic != J0_RUNTIME_MAGIC ||
        command == NULL || transfer == NULL || ticket == NULL ||
        runtime->poisoned || runtime->admission_closed || !runtime->ready) {
        if (runtime != NULL && runtime->poisoned) {
            return FWLAB_SPINE_V0_POISONED;
        }
        return runtime != NULL && runtime->admission_closed
                   ? FWLAB_SPINE_V0_WRONG_STATE
                   : FWLAB_SPINE_V0_INVALID;
    }
    record = admission_identity_find(runtime, command);
    if (runtime->poisoned) {
        return FWLAB_SPINE_V0_POISONED;
    }
    existing = record != NULL;
    binding = profile_binding(runtime, profile);
    if (existing &&
        (record->profile != profile || binding == NULL ||
         !binding_equal(&record->binding, binding) ||
         memcmp(&record->command, command, sizeof(*command)) != 0 ||
         !supplied_transfer_valid(transfer, record->transfer_direction,
                                  record->transfer_bytes) ||
         !transfer_equal(record, transfer))) {
        return admission_poison(runtime);
    }
    if (binding == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (!existing) {
        record = admission_free(runtime);
        if (record == NULL) {
            return FWLAB_SPINE_V0_NO_CAPACITY;
        }
    }
    memset(&program, 0, sizeof(program));
    result = binding->adapter.ops->plan(
        binding->adapter.context, command, &program);
    if (result != FWLAB_SPINE_V0_OK) {
        if (existing || result == FWLAB_SPINE_V0_POISONED) {
            return admission_poison(runtime);
        }
        return result;
    }
    if (!fwlab_host_action_program_v0_valid(&program)) {
        return admission_poison(runtime);
    }
    if (existing) {
        if (memcmp(&record->program, &program, sizeof(program)) != 0) {
            return admission_poison(runtime);
        }
        for (index = 0; index < program.action_count; ++index) {
            struct fwlab_spine_profile_argument_v0 argument;

            memset(&argument, 0, sizeof(argument));
            result = binding->argument_read(
                binding->adapter.context, &program.action[index].argument,
                &argument);
            if (result != FWLAB_SPINE_V0_OK ||
                memcmp(&record->argument[index], &argument,
                       sizeof(argument)) != 0) {
                return admission_poison(runtime);
            }
        }
        if (record->phase == J0_ADMISSION_ROLLBACK) {
            return rollback_drive(runtime, record);
        }
        if (record->phase != J0_ADMISSION_TRANSFERRED) {
            return FWLAB_SPINE_V0_IN_PROGRESS;
        }
        *ticket = record->ticket;
        return FWLAB_SPINE_V0_OK;
    }
    memset(record, 0, sizeof(*record));
    record->occupied = 1;
    record->phase = J0_ADMISSION_PLAN_HELD;
    record->profile = profile;
    record->binding = *binding;
    record->command = *command;
    record->program = program;
    for (index = 0; index < program.action_count; ++index) {
        result = binding->argument_read(
            binding->adapter.context, &program.action[index].argument,
            &record->argument[index]);
        if (result != FWLAB_SPINE_V0_OK ||
            memcmp(&record->argument[index].reference,
                   &program.action[index].argument,
                   sizeof(program.action[index].argument)) != 0) {
            record->original_failure = FWLAB_SPINE_V0_POISONED;
            record->phase = J0_ADMISSION_ROLLBACK;
            return rollback_drive(runtime, record);
        }
    }
    if (!transfer_contract(&program, record->argument, &direction,
                           &exact_bytes, &buffer_required) ||
        !supplied_transfer_valid(transfer, direction, exact_bytes)) {
        record->original_failure = FWLAB_SPINE_V0_INVALID;
        record->phase = J0_ADMISSION_ROLLBACK;
        return rollback_drive(runtime, record);
    }
    record->transfer_direction = direction;
    record->transfer_bytes = exact_bytes;
    if (direction == FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER) {
        memcpy(record->transfer_copy, transfer->input, exact_bytes);
    }
    result = resources_acquire(runtime, record, buffer_required);
    if (result != FWLAB_SPINE_V0_OK) {
        record->original_failure = result;
        record->phase = J0_ADMISSION_ROLLBACK;
        return rollback_drive(runtime, record);
    }
    result = fwlab_spine_lifecycle_v0_admit_start(
        runtime->lifecycle_arena, binding, &record->program,
        FWLAB_SPINE_ROLE_V0_NORMAL, &record->ticket);
    if (result != FWLAB_SPINE_V0_OK) {
        record->original_failure = result;
        record->phase = J0_ADMISSION_ROLLBACK;
        return rollback_drive(runtime, record);
    }
    record->phase = J0_ADMISSION_TRANSFERRED;
    record->lifecycle_owned = 1;
    ++runtime->active_admissions;
    *ticket = record->ticket;
    return FWLAB_SPINE_V0_OK;
}

static int close_reap_one(struct j0_runtime *runtime)
{
    uint32_t index;

    if (!runtime->close_started) {
        return 0;
    }
    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        struct j0_admission_record *record = &runtime->admission[index];
        struct fwlab_nvme_completion_intent intent;
        enum fwlab_spine_result_v0 result;

        if (!record->occupied || !record->lifecycle_owned ||
            record->close_reaped) {
            continue;
        }
        result = fwlab_spine_lifecycle_v0_intent_read(
            runtime->lifecycle_arena, &record->ticket, &intent);
        if (result == FWLAB_SPINE_V0_IN_PROGRESS) {
            continue;
        }
        if (result != FWLAB_SPINE_V0_OK &&
            result != FWLAB_SPINE_V0_QUARANTINED) {
            runtime->poisoned = 1;
            return 1;
        }
        if (result == FWLAB_SPINE_V0_OK) {
            j0_action_close_unaccepted(record);
        }
        if (release_resources(runtime, record) != FWLAB_SPINE_V0_OK) {
            runtime->poisoned = 1;
            return 1;
        }
        if (release_endpoint(runtime, record) != FWLAB_SPINE_V0_OK) {
            runtime->poisoned = 1;
            return 1;
        }
        record->close_reaped = 1;
        if (!record->intent_seen && runtime->active_admissions != 0) {
            --runtime->active_admissions;
        }
        return 1;
    }
    return 0;
}

enum fwlab_spine_result_v0 j0_runtime_step(
    struct j0_runtime *runtime, uint32_t budget, uint32_t *units)
{
    uint32_t used = 0;

    if (runtime == NULL || runtime->magic != J0_RUNTIME_MAGIC ||
        budget == 0 || units == NULL || runtime->lifecycle_finished) {
        return FWLAB_SPINE_V0_INVALID;
    }
    while (used < budget) {
        if (runtime->fair_cursor == 0) {
            uint32_t lifecycle_units = 0;
            uint32_t transitions = 0;
            enum fwlab_spine_result_v0 result =
                fwlab_spine_lifecycle_v0_step(
                    runtime->lifecycle_arena, 1, &lifecycle_units,
                    &transitions);

            if (result != FWLAB_SPINE_V0_OK &&
                result != FWLAB_SPINE_V0_IN_PROGRESS) {
                runtime->poisoned = 1;
            }
        } else if (runtime->fair_cursor == 1) {
            struct fwlab_m3p_step_result step;

            if (fwlab_m3p_step(runtime->m3p, 1, &step) !=
                FWLAB_SPINE_V0_OK) {
                runtime->poisoned = 1;
            }
        } else {
            (void)close_reap_one(runtime);
        }
        runtime->fair_cursor = (runtime->fair_cursor + 1u) % 3u;
        ++used;
        if (!runtime->ready && runtime->m3p->ready &&
            runtime->m3p->work_kind == FWLAB_M3P_MAINTENANCE_NONE) {
            runtime->ready = 1;
        }
        if (runtime->poisoned || runtime->host.poisoned ||
            runtime->buffer.poisoned || runtime->m3p->quarantined) {
            runtime->poisoned = 1;
            break;
        }
    }
    *units = used;
    return runtime->poisoned ? FWLAB_SPINE_V0_POISONED
                             : FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 j0_runtime_intent_read(
    struct j0_runtime *runtime,
    const struct fwlab_spine_command_ticket_v0 *ticket,
    struct fwlab_nvme_completion_intent *intent)
{
    struct j0_admission_record *record;
    enum fwlab_spine_result_v0 result;

    if (runtime == NULL || runtime->magic != J0_RUNTIME_MAGIC ||
        ticket == NULL || intent == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = admission_ticket_find(runtime, ticket);
    if (record == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    result = fwlab_spine_lifecycle_v0_intent_read(
        runtime->lifecycle_arena, ticket, intent);
    if (result != FWLAB_SPINE_V0_OK) {
        return result;
    }
    if (!record->intent_seen) {
        if (release_resources(runtime, record) != FWLAB_SPINE_V0_OK) {
            runtime->poisoned = 1;
            return FWLAB_SPINE_V0_POISONED;
        }
        record->intent_seen = 1;
        ++runtime->retained_intents;
        if (runtime->active_admissions != 0) {
            --runtime->active_admissions;
        }
        if (record->transfer_direction ==
                FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER &&
            release_endpoint(runtime, record) != FWLAB_SPINE_V0_OK) {
            runtime->poisoned = 1;
            return FWLAB_SPINE_V0_POISONED;
        }
    }
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 j0_runtime_host_read(
    struct j0_runtime *runtime,
    const struct fwlab_spine_command_ticket_v0 *ticket,
    void *output, size_t output_size)
{
    struct j0_admission_record *record;

    if (runtime == NULL || runtime->magic != J0_RUNTIME_MAGIC ||
        ticket == NULL || output == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = admission_ticket_find(runtime, ticket);
    if (record == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (!record->intent_seen || record->transfer_direction !=
            FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST ||
        output_size != record->transfer_bytes) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    {
        enum fwlab_spine_result_v0 result = j0_host_endpoint_read(
        &runtime->host, &record->command.handle, &record->command.origin,
        output, output_size);

        if (result != FWLAB_SPINE_V0_OK) {
            return result;
        }
        result = release_endpoint(runtime, record);
        if (result != FWLAB_SPINE_V0_OK) {
            runtime->poisoned = 1;
        }
        return result;
    }
}

enum fwlab_spine_result_v0 j0_runtime_close_start(
    struct j0_runtime *runtime)
{
    enum fwlab_spine_result_v0 result;

    if (runtime == NULL || runtime->magic != J0_RUNTIME_MAGIC ||
        runtime->poisoned) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (runtime->close_started) {
        return FWLAB_SPINE_V0_OK;
    }
    runtime->admission_closed = 1;
    runtime->close_started = 1;
    result = fwlab_spine_lifecycle_v0_epoch_close_start(
        runtime->lifecycle_arena, runtime->config.execution_epoch);
    if (result != FWLAB_SPINE_V0_OK) {
        runtime->poisoned = 1;
        return result;
    }
    result = runtime->host.port.ops->epoch_close(
        runtime->host.port.context, runtime->lifecycle_instance_nonce,
        runtime->config.execution_epoch);
    if (result != FWLAB_SPINE_V0_OK) {
        runtime->poisoned = 1;
        return result;
    }
    runtime->host_close.started = 1;
    runtime->host_close.close_acked = 1;
    runtime->host_close.lifecycle_instance_nonce =
        runtime->lifecycle_instance_nonce;
    runtime->host_close.execution_epoch = runtime->config.execution_epoch;
    result = runtime->block.ops->epoch_close(
        runtime->block.context, runtime->lifecycle_instance_nonce,
        runtime->config.execution_epoch);
    if (result != FWLAB_SPINE_V0_OK) {
        runtime->poisoned = 1;
        return result;
    }
    runtime->block_close.started = 1;
    runtime->block_close.close_acked = 1;
    runtime->block_close.lifecycle_instance_nonce =
        runtime->lifecycle_instance_nonce;
    runtime->block_close.execution_epoch = runtime->config.execution_epoch;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 j0_runtime_close_query(
    struct j0_runtime *runtime, struct j0_close_status *status)
{
    struct fwlab_spine_epoch_status_v0 lifecycle;
    struct fwlab_host_data_epoch_status_v0 host;
    struct fwlab_block_epoch_status_v0 block;
    enum fwlab_spine_result_v0 result;

    if (runtime == NULL || runtime->magic != J0_RUNTIME_MAGIC ||
        status == NULL || !runtime->close_started) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(status, 0, sizeof(*status));
    status->version = J0_RUNTIME_VERSION;
    status->size = (uint16_t)sizeof(*status);
    status->profiles_retired = runtime->profiles_retired;
    if (runtime->lifecycle_finished) {
        status->quiescent = 1;
        return FWLAB_SPINE_V0_OK;
    }
    result = fwlab_spine_lifecycle_v0_epoch_query(
        runtime->lifecycle_arena, &lifecycle);
    if (result != FWLAB_SPINE_V0_OK) {
        return result;
    }
    result = runtime->host.port.ops->epoch_quiescent(
        runtime->host.port.context, runtime->lifecycle_instance_nonce,
        runtime->config.execution_epoch, &host);
    if (result != FWLAB_SPINE_V0_OK) {
        return result;
    }
    result = runtime->block.ops->epoch_quiescent(
        runtime->block.context, runtime->lifecycle_instance_nonce,
        runtime->config.execution_epoch, &block);
    if (result != FWLAB_SPINE_V0_OK) {
        return result;
    }
    status->host_authorities = host.authority_refs;
    status->dma_operations = host.dma_operations;
    status->buffers = host.buffer_leases;
    status->block_operations = block.aggregate_operations;
    status->pending = block.quiescent ? 0 : 1;
    status->pinned = block.quiescent ? 0 : 1;
    status->nfc_operations = block.quiescent ? 0 : 1;
    status->quiescent = (uint8_t)(lifecycle.effectful_quiescent &&
        host.quiescent && block.quiescent &&
        status->host_authorities == 0 && status->dma_operations == 0 &&
        status->buffers == 0 && status->block_operations == 0);
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 j0_runtime_fini(struct j0_runtime *runtime)
{
    struct j0_close_status status;
    enum fwlab_spine_result_v0 result;

    if (runtime == NULL || runtime->magic != J0_RUNTIME_MAGIC ||
        !runtime->close_started) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (runtime->lifecycle_finished && runtime->m3p_finished) {
        return FWLAB_SPINE_V0_OK;
    }
    if (!runtime->lifecycle_finished) {
        result = j0_runtime_close_query(runtime, &status);
        if (result != FWLAB_SPINE_V0_OK || !status.quiescent) {
            return result == FWLAB_SPINE_V0_OK ? FWLAB_SPINE_V0_IN_PROGRESS
                                               : result;
        }
        result = fwlab_spine_lifecycle_v0_fini(runtime->lifecycle_arena);
        if (result == FWLAB_SPINE_V0_IN_PROGRESS) {
            return result;
        }
        if (result != FWLAB_SPINE_V0_OK) {
            runtime->poisoned = 1;
            return result;
        }
        runtime->profiles_retired = 1;
        runtime->lifecycle_finished = 1;
    }
    result = fwlab_m3p_fini(runtime->m3p);
    if (result != FWLAB_SPINE_V0_OK) {
        runtime->poisoned = 1;
        return result;
    }
    runtime->m3p_finished = 1;
    arena_release(runtime);
    return FWLAB_SPINE_V0_OK;
}
