/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fakes/spine_fake_adjacent.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fwlab/portable/nvme_codec.h"

#define TINY_MAGIC UINT64_C(0x54494e5950524f46)
#define TINY_RECORDS 32u

struct tiny_record {
    struct fwlab_host_action_program_v0 program;
    struct fwlab_spine_profile_argument_v0
        argument[FWLAB_HOST_ACTION_V0_MAX_ACTIONS];
    struct fwlab_spine_profile_result_v0
        result[FWLAB_HOST_ACTION_V0_MAX_ACTIONS];
    uint8_t payload[16];
    uint8_t result_latched[FWLAB_HOST_ACTION_V0_MAX_ACTIONS];
    uint8_t payload_read;
    uint8_t supported;
    uint8_t occupied;
    uint8_t reserved[5];
};

struct tiny_adapter {
    uint64_t magic;
    uint64_t instance_nonce;
    uint64_t next_uid;
    uint32_t generation;
    uint32_t reserved0;
    struct tiny_record record[TINY_RECORDS];
};

static int handle_equal(
    const struct fwlab_nvme_command_handle *left,
    const struct fwlab_nvme_command_handle *right)
{
    return left->instance_nonce == right->instance_nonce &&
           left->command_uid == right->command_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->generation == right->generation;
}

static int origin_equal(
    const struct fwlab_nvme_origin_token *left,
    const struct fwlab_nvme_origin_token *right)
{
    return left->word[0] == right->word[0] &&
           left->word[1] == right->word[1];
}

static int argument_ref_equal(
    const struct fwlab_host_action_argument_ref_v0 *left,
    const struct fwlab_host_action_argument_ref_v0 *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static struct tiny_adapter *adapter_from(void *context)
{
    struct tiny_adapter *adapter = context;

    if (adapter == NULL || adapter->magic != TINY_MAGIC) {
        return NULL;
    }
    return adapter;
}

static struct tiny_record *find_program(
    struct tiny_adapter *adapter,
    const struct fwlab_host_action_program_v0 *program)
{
    uint32_t index;

    for (index = 0; index < TINY_RECORDS; ++index) {
        if (adapter->record[index].occupied &&
            memcmp(&adapter->record[index].program, program,
                   sizeof(*program)) == 0) {
            return &adapter->record[index];
        }
    }
    return NULL;
}

static int command_supported(const struct fwlab_nvme_command *command)
{
    uint32_t index;

    if (command->queue_class != FWLAB_NVME_QUEUE_ADMIN ||
        command->opcode != UINT8_C(0xc0) || command->namespace_id != 0 ||
        command->command_dword2 != 0 || command->command_dword3 != 0 ||
        command->transport_fault != FWLAB_NVME_TRANSPORT_NONE ||
        command->fuse != FWLAB_NVME_FUSE_NONE ||
        command->data_pointer_format != FWLAB_NVME_DATA_POINTER_PRP ||
        command->data_address_present != 1 ||
        command->metadata_address_present != 0 ||
        command->command_flags_reserved != 0) {
        return 0;
    }
    for (index = 0; index < 6; ++index) {
        if (command->command_dword10_15[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static void add_action(
    struct tiny_adapter *adapter,
    struct fwlab_host_action_program_v0 *program,
    uint16_t kind,
    uint32_t dependency,
    uint32_t required,
    uint32_t produced)
{
    const uint16_t ordinal = program->action_count;
    struct fwlab_host_action_desc_v0 *action = &program->action[ordinal];

    action->version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    action->size = sizeof(*action);
    action->ordinal = ordinal;
    action->kind = kind;
    action->dependency_mask = dependency;
    action->required_witness_mask = required;
    action->produced_witness_mask = produced;
    action->argument.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    action->argument.size = sizeof(action->argument);
    action->argument.adapter_instance_nonce = adapter->instance_nonce;
    action->argument.argument_uid = adapter->next_uid++;
    action->argument.generation = adapter->generation;
    action->argument.ordinal = ordinal;
    action->argument.kind = kind;
    ++program->action_count;
}

static enum fwlab_spine_result_v0 tiny_plan(
    void *context,
    const struct fwlab_nvme_command *command,
    struct fwlab_host_action_program_v0 *program)
{
    struct tiny_adapter *adapter = adapter_from(context);
    struct tiny_record *free_record = NULL;
    uint8_t supported;
    uint32_t index;

    if (adapter == NULL || command == NULL || program == NULL ||
        !fwlab_nvme_command_valid(command)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    supported = (uint8_t)command_supported(command);
    for (index = 0; index < TINY_RECORDS; ++index) {
        struct tiny_record *record = &adapter->record[index];

        if (!record->occupied) {
            if (free_record == NULL) {
                free_record = record;
            }
            continue;
        }
        if (handle_equal(&record->program.command, &command->handle) ||
            origin_equal(&record->program.origin, &command->origin)) {
            if (!handle_equal(&record->program.command, &command->handle) ||
                !origin_equal(&record->program.origin, &command->origin) ||
                record->supported != supported) {
                return FWLAB_SPINE_V0_POISONED;
            }
            *program = record->program;
            return FWLAB_SPINE_V0_OK;
        }
    }
    if (free_record == NULL) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    if (adapter->next_uid > UINT64_MAX - (supported ? 3u : 1u)) {
        return FWLAB_SPINE_V0_COUNTER_EXHAUSTED;
    }
    memset(free_record, 0, sizeof(*free_record));
    free_record->occupied = 1;
    free_record->supported = supported;
    free_record->program.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    free_record->program.size = sizeof(free_record->program);
    free_record->program.command = command->handle;
    free_record->program.origin = command->origin;
    free_record->program.program_uid = adapter->next_uid++;
    free_record->program.program_generation = adapter->generation;
    free_record->program.completion_recipe.version =
        FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    free_record->program.completion_recipe.size =
        sizeof(free_record->program.completion_recipe);
    free_record->program.completion_recipe.adapter_instance_nonce =
        adapter->instance_nonce;
    free_record->program.completion_recipe.recipe_uid = adapter->next_uid++;
    free_record->program.completion_recipe.generation = adapter->generation;
    if (supported) {
        uint32_t payload_index;

        add_action(adapter, &free_record->program,
                   FWLAB_HOST_ACTION_V0_PAYLOAD_FILL, 0, 0,
                   FWLAB_HOST_WITNESS_V0_PAYLOAD_READY);
        add_action(adapter, &free_record->program,
                   FWLAB_HOST_ACTION_V0_DMA_OUT, UINT32_C(1),
                   FWLAB_HOST_WITNESS_V0_PAYLOAD_READY,
                   FWLAB_HOST_WITNESS_V0_DMA_OUT_COMPLETE);
        free_record->program.completion_required_witness_mask =
            FWLAB_HOST_WITNESS_V0_PAYLOAD_READY |
            FWLAB_HOST_WITNESS_V0_DMA_OUT_COMPLETE;
        for (payload_index = 0; payload_index < sizeof(free_record->payload);
             ++payload_index) {
            free_record->payload[payload_index] =
                (uint8_t)(UINT8_C(0xa0) + payload_index);
        }
        for (payload_index = 0;
             payload_index < free_record->program.action_count;
             ++payload_index) {
            struct fwlab_spine_profile_argument_v0 *argument =
                &free_record->argument[payload_index];

            memset(argument, 0, sizeof(*argument));
            argument->version = FWLAB_SPINE_LIFECYCLE_V0_VERSION;
            argument->size = sizeof(*argument);
            argument->reference =
                free_record->program.action[payload_index].argument;
            argument->semantic = FWLAB_SPINE_SEMANTIC_V0_TINY_PAYLOAD;
            argument->exact_bytes = sizeof(free_record->payload);
            argument->payload_bytes = sizeof(free_record->payload);
        }
    }
    if (!fwlab_host_action_program_v0_valid(&free_record->program)) {
        memset(free_record, 0, sizeof(*free_record));
        return FWLAB_SPINE_V0_POISONED;
    }
    *program = free_record->program;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 tiny_complete(
    void *context,
    const struct fwlab_host_action_program_v0 *program,
    const struct fwlab_host_action_status_v0 *status,
    uint16_t status_count,
    struct fwlab_nvme_completion_intent *intent)
{
    struct tiny_adapter *adapter = adapter_from(context);
    struct tiny_record *record;
    uint32_t index;

    if (adapter == NULL || program == NULL || intent == NULL ||
        status_count != program->action_count ||
        (status_count != 0 && status == NULL)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = find_program(adapter, program);
    if (record == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    memset(intent, 0, sizeof(*intent));
    intent->version = FWLAB_NVME_COMPLETION_VERSION;
    intent->size = sizeof(*intent);
    intent->handle = program->command;
    intent->origin = program->origin;
    if (!record->supported) {
        intent->status_code = 0x01;
        intent->do_not_retry = 1;
        intent->effect_class = FWLAB_NVME_EFFECT_NONE;
    } else {
        for (index = 0; index < status_count; ++index) {
            struct fwlab_spine_profile_result_v0 *result =
                &record->result[index];

            if (!record->result_latched[index] ||
                memcmp(&result->reference,
                       &record->argument[index].reference,
                       sizeof(result->reference)) != 0 ||
                memcmp(&result->status.token, &status[index].token,
                       sizeof(result->status.token)) != 0 ||
                result->status.terminal_kind !=
                    status[index].terminal_kind ||
                result->status.produced_witness_mask !=
                    status[index].produced_witness_mask ||
                result->status.effect != status[index].effect ||
                result->status.units_completed !=
                    status[index].units_completed ||
                result->normalized_outcome == 0) {
                return FWLAB_SPINE_V0_QUARANTINED;
            }
            if (status[index].terminal_kind ==
                FWLAB_HOST_ACTION_V0_CANCELLED) {
                intent->status_code = 0x07;
                intent->effect_class = FWLAB_NVME_EFFECT_NONE;
                return FWLAB_SPINE_V0_OK;
            }
            if (status[index].terminal_kind !=
                    FWLAB_HOST_ACTION_V0_SUCCEEDED ||
                status[index].effect !=
                    FWLAB_HOST_ACTION_V0_EFFECT_FULL) {
                intent->status_code = 0x06;
                intent->effect_class = FWLAB_NVME_EFFECT_NONE;
                return FWLAB_SPINE_V0_OK;
            }
        }
        if (!record->payload_read) {
            return FWLAB_SPINE_V0_QUARANTINED;
        }
        intent->actual_length = 16;
        intent->effect_class = FWLAB_NVME_EFFECT_FULL;
    }
    return fwlab_nvme_completion_valid(intent) ? FWLAB_SPINE_V0_OK
                                                : FWLAB_SPINE_V0_POISONED;
}

static struct tiny_record *find_argument(
    struct tiny_adapter *adapter,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    uint32_t *ordinal)
{
    uint32_t record_index;

    for (record_index = 0; record_index < TINY_RECORDS; ++record_index) {
        struct tiny_record *record = &adapter->record[record_index];
        uint32_t index;

        if (!record->occupied) {
            continue;
        }
        for (index = 0; index < record->program.action_count; ++index) {
            if (argument_ref_equal(&record->argument[index].reference,
                                   reference)) {
                *ordinal = index;
                return record;
            }
        }
    }
    return NULL;
}

static enum fwlab_spine_result_v0 tiny_argument_read(
    void *context,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    struct fwlab_spine_profile_argument_v0 *argument)
{
    struct tiny_adapter *adapter = adapter_from(context);
    struct tiny_record *record;
    uint32_t ordinal = 0;

    if (adapter == NULL || reference == NULL || argument == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = find_argument(adapter, reference, &ordinal);
    if (record == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    *argument = record->argument[ordinal];
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 tiny_payload_read(
    void *context,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    void *output,
    size_t output_size,
    uint32_t *actual_bytes)
{
    struct tiny_adapter *adapter = adapter_from(context);
    struct tiny_record *record;
    uint32_t ordinal = 0;

    if (adapter == NULL || reference == NULL || output == NULL ||
        actual_bytes == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = find_argument(adapter, reference, &ordinal);
    if (record == NULL || ordinal != 0 ||
        reference->kind != FWLAB_HOST_ACTION_V0_PAYLOAD_FILL ||
        output_size != sizeof(record->payload)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memcpy(output, record->payload, sizeof(record->payload));
    record->payload_read = 1;
    *actual_bytes = sizeof(record->payload);
    return FWLAB_SPINE_V0_OK;
}

static int tiny_result_equal(
    const struct fwlab_spine_profile_result_v0 *left,
    const struct fwlab_spine_profile_result_v0 *right)
{
    return argument_ref_equal(&left->reference, &right->reference) &&
           memcmp(&left->status.token, &right->status.token,
                  sizeof(left->status.token)) == 0 &&
           left->status.terminal_kind == right->status.terminal_kind &&
           left->status.produced_witness_mask ==
               right->status.produced_witness_mask &&
           left->status.effect == right->status.effect &&
           left->status.units_completed == right->status.units_completed &&
           left->status.fault_domain == right->status.fault_domain &&
           left->status.fault_code == right->status.fault_code &&
           left->normalized_outcome == right->normalized_outcome &&
           left->result_dword0 == right->result_dword0;
}

static enum fwlab_spine_result_v0 tiny_result_latch(
    void *context,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    const struct fwlab_host_action_status_v0 *status,
    uint32_t normalized_outcome,
    uint32_t result_dword0)
{
    struct tiny_adapter *adapter = adapter_from(context);
    struct tiny_record *record;
    struct fwlab_spine_profile_result_v0 local;
    uint32_t ordinal = 0;

    if (adapter == NULL || reference == NULL || status == NULL ||
        result_dword0 != 0 ||
        normalized_outcome < FWLAB_SPINE_PROVIDER_V0_SUCCESS ||
        normalized_outcome > FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE_DELETE) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = find_argument(adapter, reference, &ordinal);
    if (record == NULL ||
        !handle_equal(&record->program.command, &status->token.command) ||
        !origin_equal(&record->program.origin, &status->token.origin) ||
        status->token.ordinal != ordinal ||
        status->token.kind != reference->kind) {
        return FWLAB_SPINE_V0_POISONED;
    }
    memset(&local, 0, sizeof(local));
    local.version = FWLAB_SPINE_LIFECYCLE_V0_VERSION;
    local.size = sizeof(local);
    local.reference = *reference;
    local.status = *status;
    local.normalized_outcome = normalized_outcome;
    if (record->result_latched[ordinal]) {
        return tiny_result_equal(&record->result[ordinal], &local)
                   ? FWLAB_SPINE_V0_OK
                   : FWLAB_SPINE_V0_POISONED;
    }
    record->result[ordinal] = local;
    record->result_latched[ordinal] = 1;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 tiny_retire(
    void *context,
    const struct fwlab_host_action_program_v0 *program)
{
    struct tiny_adapter *adapter = adapter_from(context);
    struct tiny_record *record;

    if (adapter == NULL || program == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = find_program(adapter, program);
    if (record == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    memset(record, 0, sizeof(*record));
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_host_profile_ops_v0 tiny_ops = {
    .version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION,
    .size = sizeof(struct fwlab_host_profile_ops_v0),
    .plan = tiny_plan,
    .complete = tiny_complete,
    .retire = tiny_retire,
};

size_t fwlab_tiny_profile_v0_arena_size(void)
{
    return sizeof(struct tiny_adapter);
}

size_t fwlab_tiny_profile_v0_arena_alignment(void)
{
    return _Alignof(struct tiny_adapter);
}

enum fwlab_spine_result_v0 fwlab_tiny_profile_v0_init(
    void *arena,
    size_t arena_size,
    uint64_t instance_nonce,
    uint32_t generation,
    struct fwlab_host_profile_adapter_v0 *adapter)
{
    struct tiny_adapter *context = arena;

    if (arena == NULL || adapter == NULL || arena_size != sizeof(*context) ||
        ((uintptr_t)arena % _Alignof(struct tiny_adapter)) != 0 ||
        instance_nonce == 0 || generation == 0) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(context, 0, sizeof(*context));
    context->magic = TINY_MAGIC;
    context->instance_nonce = instance_nonce;
    context->generation = generation;
    context->next_uid = 1;
    memset(adapter, 0, sizeof(*adapter));
    adapter->ops = &tiny_ops;
    adapter->context = arena;
    adapter->generation = generation;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_tiny_profile_v0_binding(
    const struct fwlab_host_profile_adapter_v0 *adapter,
    struct fwlab_spine_profile_binding_v0 *binding)
{
    struct tiny_adapter *context;

    if (adapter == NULL || binding == NULL || adapter->ops != &tiny_ops ||
        (context = adapter_from(adapter->context)) == NULL ||
        adapter->generation != context->generation) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(binding, 0, sizeof(*binding));
    binding->version = FWLAB_SPINE_LIFECYCLE_V0_VERSION;
    binding->size = sizeof(*binding);
    binding->adapter = *adapter;
    binding->adapter_instance_nonce = context->instance_nonce;
    binding->generation = context->generation;
    binding->argument_read = tiny_argument_read;
    binding->payload_read = tiny_payload_read;
    binding->result_latch = tiny_result_latch;
    return FWLAB_SPINE_V0_OK;
}
