/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "spine_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fwlab/portable/nvme_codec.h"

#define C43_ADAPTER_MAGIC UINT64_C(0x4334335031414450)
#define C43_RECORDS 32u
#define C43_PAYLOAD_BYTES 4096u

enum c43_kind {
    C43_KIND_IDENTIFY_CONTROLLER = 1,
    C43_KIND_IDENTIFY_NAMESPACE = 2,
    C43_KIND_ACTIVE_NAMESPACE_LIST = 3,
    C43_KIND_NAMESPACE_DESCRIPTOR_LIST = 4,
    C43_KIND_SET_NUMBER_OF_QUEUES = 5,
    C43_KIND_CREATE_CQ = 6,
    C43_KIND_CREATE_SQ = 7,
    C43_KIND_DELETE_CQ = 8,
    C43_KIND_DELETE_SQ = 9,
    C43_KIND_ABORT = 10,
    C43_KIND_READ = 11,
    C43_KIND_WRITE = 12,
    C43_KIND_FLUSH = 13,
    C43_KIND_UNSUPPORTED = 14,
    C43_KIND_INVALID_IDENTIFY = 15
};

enum c43_status {
    C43_STATUS_SUCCESS = 0,
    C43_STATUS_UNSUPPORTED = 1,
    C43_STATUS_INVALID_FIELD = 2,
    C43_STATUS_INVALID_NAMESPACE = 3,
    C43_STATUS_LBA_RANGE = 4,
    C43_STATUS_COMMAND_SEQUENCE = 5,
    C43_STATUS_INVALID_QUEUE = 6,
    C43_STATUS_INVALID_QUEUE_SIZE = 7,
    C43_STATUS_INVALID_QUEUE_DELETE = 8,
    C43_STATUS_ABORTED = 9,
    C43_STATUS_TRANSFER_FAILURE = 10,
    C43_STATUS_MEDIA_READ = 11,
    C43_STATUS_MEDIA_WRITE = 12,
    C43_STATUS_RESOURCE_FAILURE = 13
};

struct c43_semantic {
    uint64_t slba;
    uint32_t namespace_id;
    uint32_t lba_count;
    uint32_t data_bytes;
    uint32_t requested_counts;
    uint32_t kind;
    uint32_t status;
    uint16_t target_sqid;
    uint16_t target_cid;
    uint8_t dnr;
    uint8_t fua;
    uint8_t reserved[2];
};

struct c43_slot {
    struct fwlab_spine_profile_argument_v0 argument;
    struct fwlab_spine_profile_result_v0 result;
    uint8_t result_latched;
    uint8_t payload_read;
    uint8_t reserved[6];
};

struct c43_record {
    struct fwlab_host_action_program_v0 program;
    struct c43_semantic semantic;
    struct c43_slot slot[FWLAB_HOST_ACTION_V0_MAX_ACTIONS];
    uint8_t payload[C43_PAYLOAD_BYTES];
    uint32_t payload_bytes;
    uint32_t relation_decision;
    uint64_t relation_abort_uid;
    uint32_t retire_remaining;
    uint8_t occupied;
    uint8_t relation_latched;
    uint8_t reserved[6];
};

struct c43_adapter {
    uint64_t magic;
    uint64_t instance_nonce;
    uint64_t next_uid;
    uint32_t generation;
    uint32_t retire_delay;
    struct c43_record record[C43_RECORDS];
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

static int words_zero(const uint32_t *words, size_t count)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        if (words[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static void put_u32(uint8_t *output, size_t offset, uint32_t value)
{
    output[offset] = (uint8_t)value;
    output[offset + 1] = (uint8_t)(value >> 8);
    output[offset + 2] = (uint8_t)(value >> 16);
    output[offset + 3] = (uint8_t)(value >> 24);
}

static void put_u64(uint8_t *output, size_t offset, uint64_t value)
{
    uint32_t index;

    for (index = 0; index < 8; ++index) {
        output[offset + index] = (uint8_t)(value >> (index * 8));
    }
}

static void space_padded(
    uint8_t *output,
    size_t offset,
    size_t width,
    const char *value,
    size_t value_size)
{
    memset(output + offset, ' ', width);
    memcpy(output + offset, value, value_size);
}

static uint32_t witness_for_kind(uint16_t kind)
{
    switch (kind) {
    case FWLAB_HOST_ACTION_V0_PAYLOAD_FILL:
        return FWLAB_HOST_WITNESS_V0_PAYLOAD_READY;
    case FWLAB_HOST_ACTION_V0_QUEUE_EFFECT:
        return FWLAB_HOST_WITNESS_V0_QUEUE_EFFECT;
    case FWLAB_HOST_ACTION_V0_TARGET_RESOLVE:
        return FWLAB_HOST_WITNESS_V0_TARGET_RESOLVED;
    case FWLAB_HOST_ACTION_V0_DMA_IN:
        return FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE;
    case FWLAB_HOST_ACTION_V0_DMA_OUT:
        return FWLAB_HOST_WITNESS_V0_DMA_OUT_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_READ:
        return FWLAB_HOST_WITNESS_V0_BLOCK_READ_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_WRITE:
        return FWLAB_HOST_WITNESS_V0_BLOCK_WRITE_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_FLUSH:
        return FWLAB_HOST_WITNESS_V0_BLOCK_FLUSH_COMPLETE;
    default:
        return 0;
    }
}

static struct c43_adapter *adapter_from(void *context)
{
    struct c43_adapter *adapter = context;

    if (adapter == NULL || adapter->magic != C43_ADAPTER_MAGIC) {
        return NULL;
    }
    return adapter;
}

static struct c43_record *find_program(
    struct c43_adapter *adapter,
    const struct fwlab_host_action_program_v0 *program)
{
    uint32_t index;

    for (index = 0; index < C43_RECORDS; ++index) {
        if (adapter->record[index].occupied &&
            memcmp(&adapter->record[index].program, program,
                   sizeof(*program)) == 0) {
            return &adapter->record[index];
        }
    }
    return NULL;
}

static struct c43_record *find_identity(
    struct c43_adapter *adapter,
    const struct fwlab_nvme_command *command)
{
    uint32_t index;

    for (index = 0; index < C43_RECORDS; ++index) {
        struct c43_record *record = &adapter->record[index];

        if (record->occupied &&
            (handle_equal(&record->program.command, &command->handle) ||
             origin_equal(&record->program.origin, &command->origin))) {
            return record;
        }
    }
    return NULL;
}

static struct c43_record *find_free(struct c43_adapter *adapter)
{
    uint32_t index;

    for (index = 0; index < C43_RECORDS; ++index) {
        if (!adapter->record[index].occupied) {
            return &adapter->record[index];
        }
    }
    return NULL;
}

static uint32_t classify_kind(const struct fwlab_nvme_command *command)
{
    const uint32_t cdw10 = command->command_dword10_15[0];

    if (command->queue_class == FWLAB_NVME_QUEUE_ADMIN) {
        switch (command->opcode) {
        case 0x06:
            switch (cdw10 & UINT32_C(0xff)) {
            case 0:
                return C43_KIND_IDENTIFY_NAMESPACE;
            case 1:
                return C43_KIND_IDENTIFY_CONTROLLER;
            case 2:
                return C43_KIND_ACTIVE_NAMESPACE_LIST;
            case 3:
                return C43_KIND_NAMESPACE_DESCRIPTOR_LIST;
            default:
                return C43_KIND_INVALID_IDENTIFY;
            }
        case 0x09:
            return C43_KIND_SET_NUMBER_OF_QUEUES;
        case 0x05:
            return C43_KIND_CREATE_CQ;
        case 0x01:
            return C43_KIND_CREATE_SQ;
        case 0x04:
            return C43_KIND_DELETE_CQ;
        case 0x00:
            return C43_KIND_DELETE_SQ;
        case 0x08:
            return C43_KIND_ABORT;
        default:
            return C43_KIND_UNSUPPORTED;
        }
    }
    if (command->queue_class == FWLAB_NVME_QUEUE_IO) {
        switch (command->opcode) {
        case 0x02:
            return C43_KIND_READ;
        case 0x01:
            return C43_KIND_WRITE;
        case 0x00:
            return C43_KIND_FLUSH;
        default:
            return C43_KIND_UNSUPPORTED;
        }
    }
    return C43_KIND_UNSUPPORTED;
}

static int data_required(uint32_t kind)
{
    return kind == C43_KIND_IDENTIFY_CONTROLLER ||
           kind == C43_KIND_IDENTIFY_NAMESPACE ||
           kind == C43_KIND_ACTIVE_NAMESPACE_LIST ||
           kind == C43_KIND_NAMESPACE_DESCRIPTOR_LIST ||
           kind == C43_KIND_CREATE_CQ || kind == C43_KIND_CREATE_SQ ||
           kind == C43_KIND_READ || kind == C43_KIND_WRITE;
}

static int namespace_valid(
    uint32_t kind,
    const struct fwlab_nvme_command *command)
{
    if (kind == C43_KIND_ACTIVE_NAMESPACE_LIST) {
        return 1;
    }
    if (kind == C43_KIND_IDENTIFY_NAMESPACE ||
        kind == C43_KIND_NAMESPACE_DESCRIPTOR_LIST ||
        kind == C43_KIND_READ || kind == C43_KIND_WRITE ||
        kind == C43_KIND_FLUSH) {
        return command->namespace_id == 1;
    }
    return command->namespace_id == 0;
}

static int dwords_valid(
    uint32_t kind,
    const struct fwlab_nvme_command *command,
    struct c43_semantic *semantic)
{
    const uint32_t *dword = command->command_dword10_15;

    switch (kind) {
    case C43_KIND_IDENTIFY_CONTROLLER:
        return dword[0] == 1 && words_zero(dword + 1, 5);
    case C43_KIND_IDENTIFY_NAMESPACE:
        return dword[0] == 0 && words_zero(dword + 1, 5);
    case C43_KIND_ACTIVE_NAMESPACE_LIST:
        return dword[0] == 2 && words_zero(dword + 1, 5);
    case C43_KIND_NAMESPACE_DESCRIPTOR_LIST:
        return dword[0] == 3 && words_zero(dword + 1, 5);
    case C43_KIND_SET_NUMBER_OF_QUEUES:
        if (dword[0] != 7 || !words_zero(dword + 2, 4)) {
            return 0;
        }
        semantic->requested_counts = dword[1];
        return 1;
    case C43_KIND_CREATE_CQ:
        return dword[0] == UINT32_C(0x00030001) &&
               dword[1] == UINT32_C(0x00000003) &&
               words_zero(dword + 2, 4);
    case C43_KIND_CREATE_SQ:
        return dword[0] == UINT32_C(0x00030001) &&
               dword[1] == UINT32_C(0x00010001) &&
               words_zero(dword + 2, 4);
    case C43_KIND_DELETE_CQ:
    case C43_KIND_DELETE_SQ:
        return dword[0] == 1 && words_zero(dword + 1, 5);
    case C43_KIND_ABORT:
        if ((dword[0] & UINT32_C(0xffff)) != 1 ||
            !words_zero(dword + 1, 5)) {
            return 0;
        }
        semantic->target_sqid = (uint16_t)(dword[0] & UINT32_C(0xffff));
        semantic->target_cid = (uint16_t)(dword[0] >> 16);
        return 1;
    case C43_KIND_READ:
    case C43_KIND_WRITE: {
        const uint32_t allowed = kind == C43_KIND_WRITE
                                     ? UINT32_C(0x4000ffff)
                                     : UINT32_C(0x0000ffff);
        const uint64_t lba_count = (uint64_t)(dword[2] & UINT32_C(0xffff)) + 1;
        const uint64_t slba =
            (uint64_t)dword[0] | ((uint64_t)dword[1] << 32);
        uint64_t bytes;

        if ((dword[2] & ~allowed) != 0 || !words_zero(dword + 3, 3)) {
            return 0;
        }
        if (lba_count > 8 || slba >= 8 || slba > UINT64_MAX - lba_count ||
            slba + lba_count > 8) {
            semantic->status = C43_STATUS_LBA_RANGE;
            semantic->dnr = 1;
            return 1;
        }
        bytes = lba_count * UINT64_C(512);
        if (bytes > C43_PAYLOAD_BYTES || bytes > UINT32_MAX) {
            semantic->status = C43_STATUS_LBA_RANGE;
            semantic->dnr = 1;
            return 1;
        }
        semantic->slba = slba;
        semantic->lba_count = (uint32_t)lba_count;
        semantic->data_bytes = (uint32_t)bytes;
        semantic->fua = (uint8_t)((dword[2] >> 30) & 1u);
        return 1;
    }
    case C43_KIND_FLUSH:
        return words_zero(dword, 6);
    default:
        return 1;
    }
}

static void transport_status(
    const struct fwlab_nvme_command *command,
    struct c43_semantic *semantic)
{
    switch (command->transport_fault) {
    case FWLAB_NVME_TRANSPORT_UNSUPPORTED_FORMAT:
        semantic->status = C43_STATUS_INVALID_FIELD;
        semantic->dnr = 1;
        break;
    case FWLAB_NVME_TRANSPORT_STALE_GENERATION:
        semantic->status = C43_STATUS_COMMAND_SEQUENCE;
        semantic->dnr = 1;
        break;
    case FWLAB_NVME_TRANSPORT_QUEUE_MEMORY:
        semantic->status = C43_STATUS_TRANSFER_FAILURE;
        semantic->dnr = 0;
        break;
    default:
        semantic->status = C43_STATUS_TRANSFER_FAILURE;
        semantic->dnr = 1;
        break;
    }
}

static void sanitize(
    const struct fwlab_nvme_command *command,
    struct c43_semantic *semantic)
{
    uint32_t kind;

    memset(semantic, 0, sizeof(*semantic));
    semantic->namespace_id = command->namespace_id;
    if (command->transport_fault != FWLAB_NVME_TRANSPORT_NONE) {
        semantic->kind = C43_KIND_UNSUPPORTED;
        transport_status(command, semantic);
        return;
    }
    kind = classify_kind(command);
    semantic->kind = kind;
    if (kind == C43_KIND_UNSUPPORTED) {
        semantic->status = C43_STATUS_UNSUPPORTED;
        semantic->dnr = 1;
        return;
    }
    if (command->command_dword2 != 0 || command->command_dword3 != 0 ||
        command->command_flags_reserved != 0 ||
        command->fuse != FWLAB_NVME_FUSE_NONE ||
        command->data_pointer_format != FWLAB_NVME_DATA_POINTER_PRP ||
        command->metadata_address_present ||
        command->data_address_present != (uint8_t)data_required(kind)) {
        semantic->status = C43_STATUS_INVALID_FIELD;
        semantic->dnr = 1;
        return;
    }
    if (kind == C43_KIND_INVALID_IDENTIFY) {
        semantic->status = C43_STATUS_INVALID_FIELD;
        semantic->dnr = 1;
        return;
    }
    if (!namespace_valid(kind, command)) {
        semantic->status = C43_STATUS_INVALID_NAMESPACE;
        semantic->dnr = 1;
        return;
    }
    if (!dwords_valid(kind, command, semantic)) {
        semantic->status = C43_STATUS_INVALID_FIELD;
        semantic->dnr = 1;
    }
}

static int semantic_equal(
    const struct c43_semantic *left,
    const struct c43_semantic *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static void program_base(
    struct c43_adapter *adapter,
    struct c43_record *record,
    const struct fwlab_nvme_command *command)
{
    struct fwlab_host_action_program_v0 *program = &record->program;

    memset(program, 0, sizeof(*program));
    program->version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    program->size = sizeof(*program);
    program->command = command->handle;
    program->origin = command->origin;
    program->program_uid = adapter->next_uid++;
    program->program_generation = adapter->generation;
    program->completion_recipe.version =
        FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    program->completion_recipe.size = sizeof(program->completion_recipe);
    program->completion_recipe.adapter_instance_nonce =
        adapter->instance_nonce;
    program->completion_recipe.recipe_uid = adapter->next_uid++;
    program->completion_recipe.generation = adapter->generation;
}

static void add_action(
    struct c43_adapter *adapter,
    struct fwlab_host_action_program_v0 *program,
    uint16_t kind,
    uint32_t dependency_mask,
    uint32_t required_witness_mask)
{
    const uint16_t ordinal = program->action_count;
    struct fwlab_host_action_desc_v0 *action = &program->action[ordinal];

    action->version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    action->size = sizeof(*action);
    action->ordinal = ordinal;
    action->kind = kind;
    action->dependency_mask = dependency_mask;
    action->required_witness_mask = required_witness_mask;
    action->produced_witness_mask = witness_for_kind(kind);
    action->argument.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    action->argument.size = sizeof(action->argument);
    action->argument.adapter_instance_nonce = adapter->instance_nonce;
    action->argument.argument_uid = adapter->next_uid++;
    action->argument.generation = adapter->generation;
    action->argument.ordinal = ordinal;
    action->argument.kind = kind;
    ++program->action_count;
}

static void encode_payload(struct c43_record *record)
{
    static const char serial[] = "FWLABC43P1-000000001";
    static const char model[] = "SSD Firmware Lab C43-P1";
    static const char firmware[] = "C43P1";
    uint8_t *payload = record->payload;

    memset(payload, 0, C43_PAYLOAD_BYTES);
    record->payload_bytes = C43_PAYLOAD_BYTES;
    switch (record->semantic.kind) {
    case C43_KIND_IDENTIFY_CONTROLLER:
        space_padded(payload, 4, 20, serial, sizeof(serial) - 1);
        space_padded(payload, 24, 40, model, sizeof(model) - 1);
        space_padded(payload, 64, 8, firmware, sizeof(firmware) - 1);
        put_u32(payload, 516, 1);
        payload[525] = 1;
        break;
    case C43_KIND_IDENTIFY_NAMESPACE:
        put_u64(payload, 0, 8);
        put_u64(payload, 8, 8);
        put_u64(payload, 16, 8);
        payload[130] = 9;
        break;
    case C43_KIND_ACTIVE_NAMESPACE_LIST:
        if (record->semantic.namespace_id < 1) {
            put_u32(payload, 0, 1);
        }
        break;
    case C43_KIND_NAMESPACE_DESCRIPTOR_LIST:
        break;
    default:
        record->payload_bytes = 0;
        break;
    }
}

static void build_actions(
    struct c43_adapter *adapter,
    struct c43_record *record)
{
    struct fwlab_host_action_program_v0 *program = &record->program;

    if (record->semantic.status != C43_STATUS_SUCCESS) {
        return;
    }
    switch (record->semantic.kind) {
    case C43_KIND_IDENTIFY_CONTROLLER:
    case C43_KIND_IDENTIFY_NAMESPACE:
    case C43_KIND_ACTIVE_NAMESPACE_LIST:
    case C43_KIND_NAMESPACE_DESCRIPTOR_LIST:
        encode_payload(record);
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_PAYLOAD_FILL, 0, 0);
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_DMA_OUT,
                   UINT32_C(1), FWLAB_HOST_WITNESS_V0_PAYLOAD_READY);
        program->completion_required_witness_mask =
            FWLAB_HOST_WITNESS_V0_PAYLOAD_READY |
            FWLAB_HOST_WITNESS_V0_DMA_OUT_COMPLETE;
        break;
    case C43_KIND_SET_NUMBER_OF_QUEUES:
    case C43_KIND_CREATE_CQ:
    case C43_KIND_CREATE_SQ:
    case C43_KIND_DELETE_CQ:
    case C43_KIND_DELETE_SQ:
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_QUEUE_EFFECT, 0, 0);
        program->completion_required_witness_mask =
            FWLAB_HOST_WITNESS_V0_QUEUE_EFFECT;
        break;
    case C43_KIND_ABORT:
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_TARGET_RESOLVE, 0, 0);
        program->completion_required_witness_mask =
            FWLAB_HOST_WITNESS_V0_TARGET_RESOLVED;
        break;
    case C43_KIND_READ:
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_BLOCK_READ, 0, 0);
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_DMA_OUT,
                   UINT32_C(1),
                   FWLAB_HOST_WITNESS_V0_BLOCK_READ_COMPLETE);
        program->completion_required_witness_mask =
            FWLAB_HOST_WITNESS_V0_BLOCK_READ_COMPLETE |
            FWLAB_HOST_WITNESS_V0_DMA_OUT_COMPLETE;
        break;
    case C43_KIND_WRITE:
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_DMA_IN, 0, 0);
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_BLOCK_WRITE,
                   UINT32_C(1), FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE);
        program->completion_required_witness_mask =
            FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE |
            FWLAB_HOST_WITNESS_V0_BLOCK_WRITE_COMPLETE;
        break;
    case C43_KIND_FLUSH:
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_BLOCK_FLUSH, 0, 0);
        program->completion_required_witness_mask =
            FWLAB_HOST_WITNESS_V0_BLOCK_FLUSH_COMPLETE;
        break;
    default:
        break;
    }
}

static uint32_t semantic_tag(uint32_t kind)
{
    switch (kind) {
    case C43_KIND_IDENTIFY_CONTROLLER:
        return FWLAB_SPINE_SEMANTIC_V0_IDENTIFY_CONTROLLER;
    case C43_KIND_IDENTIFY_NAMESPACE:
        return FWLAB_SPINE_SEMANTIC_V0_IDENTIFY_NAMESPACE;
    case C43_KIND_ACTIVE_NAMESPACE_LIST:
        return FWLAB_SPINE_SEMANTIC_V0_ACTIVE_NAMESPACE_LIST;
    case C43_KIND_NAMESPACE_DESCRIPTOR_LIST:
        return FWLAB_SPINE_SEMANTIC_V0_NAMESPACE_DESCRIPTOR_LIST;
    case C43_KIND_SET_NUMBER_OF_QUEUES:
        return FWLAB_SPINE_SEMANTIC_V0_SET_NUMBER_OF_QUEUES;
    case C43_KIND_CREATE_CQ:
        return FWLAB_SPINE_SEMANTIC_V0_CREATE_CQ;
    case C43_KIND_CREATE_SQ:
        return FWLAB_SPINE_SEMANTIC_V0_CREATE_SQ;
    case C43_KIND_DELETE_CQ:
        return FWLAB_SPINE_SEMANTIC_V0_DELETE_CQ;
    case C43_KIND_DELETE_SQ:
        return FWLAB_SPINE_SEMANTIC_V0_DELETE_SQ;
    case C43_KIND_ABORT:
        return FWLAB_SPINE_SEMANTIC_V0_ABORT;
    case C43_KIND_READ:
        return FWLAB_SPINE_SEMANTIC_V0_READ;
    case C43_KIND_WRITE:
        return FWLAB_SPINE_SEMANTIC_V0_WRITE;
    case C43_KIND_FLUSH:
        return FWLAB_SPINE_SEMANTIC_V0_FLUSH;
    default:
        return 0;
    }
}

static void populate_slots(struct c43_record *record)
{
    uint32_t index;

    for (index = 0; index < record->program.action_count; ++index) {
        struct fwlab_spine_profile_argument_v0 *argument =
            &record->slot[index].argument;

        memset(argument, 0, sizeof(*argument));
        argument->version = FWLAB_SPINE_LIFECYCLE_V0_VERSION;
        argument->size = sizeof(*argument);
        argument->reference = record->program.action[index].argument;
        argument->semantic = semantic_tag(record->semantic.kind);
        argument->namespace_id = record->semantic.namespace_id;
        switch (record->semantic.kind) {
        case C43_KIND_IDENTIFY_CONTROLLER:
        case C43_KIND_IDENTIFY_NAMESPACE:
        case C43_KIND_ACTIVE_NAMESPACE_LIST:
        case C43_KIND_NAMESPACE_DESCRIPTOR_LIST:
            argument->payload_bytes = record->payload_bytes;
            argument->exact_bytes = record->payload_bytes;
            break;
        case C43_KIND_SET_NUMBER_OF_QUEUES:
            argument->requested_cq_count =
                (record->semantic.requested_counts & UINT32_C(0xffff)) + 1;
            argument->requested_sq_count =
                (record->semantic.requested_counts >> 16) + 1;
            break;
        case C43_KIND_CREATE_CQ:
            argument->queue_id = 1;
            argument->queue_entries = 4;
            argument->interrupt_vector = 0;
            break;
        case C43_KIND_CREATE_SQ:
            argument->queue_id = 1;
            argument->queue_entries = 4;
            argument->associated_queue_id = 1;
            break;
        case C43_KIND_DELETE_CQ:
        case C43_KIND_DELETE_SQ:
            argument->queue_id = 1;
            break;
        case C43_KIND_ABORT:
            argument->target_sqid = record->semantic.target_sqid;
            argument->target_cid = record->semantic.target_cid;
            break;
        case C43_KIND_READ:
            argument->lba = record->semantic.slba;
            argument->lba_count = record->semantic.lba_count;
            argument->exact_bytes = record->semantic.data_bytes;
            argument->durability = FWLAB_SPINE_DURABILITY_V0_NONE;
            break;
        case C43_KIND_WRITE:
            argument->lba = record->semantic.slba;
            argument->lba_count = record->semantic.lba_count;
            argument->exact_bytes = record->semantic.data_bytes;
            if (argument->reference.kind ==
                FWLAB_HOST_ACTION_V0_BLOCK_WRITE) {
                argument->durability = record->semantic.fua
                                           ? FWLAB_SPINE_DURABILITY_V0_SELF
                                           : FWLAB_SPINE_DURABILITY_V0_VOLATILE_ALLOWED;
            }
            break;
        case C43_KIND_FLUSH:
            argument->durability = FWLAB_SPINE_DURABILITY_V0_FRONTIER;
            break;
        default:
            break;
        }
    }
}

static enum fwlab_spine_result_v0 c43_plan(
    void *context,
    const struct fwlab_nvme_command *command,
    struct fwlab_host_action_program_v0 *program)
{
    struct c43_adapter *adapter = adapter_from(context);
    struct c43_record *record;
    struct c43_semantic semantic;
    uint32_t uid_count;

    if (adapter == NULL || command == NULL || program == NULL ||
        !fwlab_nvme_command_valid(command)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    sanitize(command, &semantic);
    record = find_identity(adapter, command);
    if (record != NULL) {
        if (!handle_equal(&record->program.command, &command->handle) ||
            !origin_equal(&record->program.origin, &command->origin) ||
            !semantic_equal(&record->semantic, &semantic)) {
            return FWLAB_SPINE_V0_POISONED;
        }
        *program = record->program;
        return FWLAB_SPINE_V0_OK;
    }
    record = find_free(adapter);
    if (record == NULL) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    uid_count = 2;
    if (semantic.status == C43_STATUS_SUCCESS) {
        switch (semantic.kind) {
        case C43_KIND_IDENTIFY_CONTROLLER:
        case C43_KIND_IDENTIFY_NAMESPACE:
        case C43_KIND_ACTIVE_NAMESPACE_LIST:
        case C43_KIND_NAMESPACE_DESCRIPTOR_LIST:
        case C43_KIND_READ:
        case C43_KIND_WRITE:
            uid_count += 2;
            break;
        default:
            uid_count += 1;
            break;
        }
    }
    if (adapter->next_uid == 0 ||
        (uint64_t)(uid_count - 1) > UINT64_MAX - adapter->next_uid) {
        return FWLAB_SPINE_V0_COUNTER_EXHAUSTED;
    }
    memset(record, 0, sizeof(*record));
    record->occupied = 1;
    record->semantic = semantic;
    record->retire_remaining = adapter->retire_delay;
    program_base(adapter, record, command);
    build_actions(adapter, record);
    populate_slots(record);
    if (!fwlab_host_action_program_v0_valid(&record->program)) {
        memset(record, 0, sizeof(*record));
        return FWLAB_SPINE_V0_POISONED;
    }
    *program = record->program;
    return FWLAB_SPINE_V0_OK;
}

static void status_tuple(
    uint32_t semantic_status,
    uint16_t *status_code,
    uint8_t *status_code_type)
{
    *status_code_type = 0;
    switch (semantic_status) {
    case C43_STATUS_SUCCESS:
        *status_code = 0x00;
        break;
    case C43_STATUS_UNSUPPORTED:
        *status_code = 0x01;
        break;
    case C43_STATUS_INVALID_FIELD:
        *status_code = 0x02;
        break;
    case C43_STATUS_INVALID_NAMESPACE:
        *status_code = 0x0b;
        break;
    case C43_STATUS_LBA_RANGE:
        *status_code = 0x80;
        break;
    case C43_STATUS_COMMAND_SEQUENCE:
        *status_code = 0x0c;
        break;
    case C43_STATUS_INVALID_QUEUE:
        *status_code_type = 1;
        *status_code = 0x01;
        break;
    case C43_STATUS_INVALID_QUEUE_SIZE:
        *status_code_type = 1;
        *status_code = 0x02;
        break;
    case C43_STATUS_INVALID_QUEUE_DELETE:
        *status_code_type = 1;
        *status_code = 0x0c;
        break;
    case C43_STATUS_ABORTED:
        *status_code = 0x07;
        break;
    case C43_STATUS_TRANSFER_FAILURE:
        *status_code = 0x04;
        break;
    case C43_STATUS_MEDIA_READ:
        *status_code_type = 2;
        *status_code = 0x81;
        break;
    case C43_STATUS_MEDIA_WRITE:
        *status_code_type = 2;
        *status_code = 0x80;
        break;
    default:
        *status_code = 0x06;
        break;
    }
}

static uint32_t provider_status(uint16_t action_kind)
{
    switch (action_kind) {
    case FWLAB_HOST_ACTION_V0_DMA_IN:
    case FWLAB_HOST_ACTION_V0_DMA_OUT:
        return C43_STATUS_TRANSFER_FAILURE;
    case FWLAB_HOST_ACTION_V0_BLOCK_READ:
        return C43_STATUS_MEDIA_READ;
    case FWLAB_HOST_ACTION_V0_BLOCK_WRITE:
    case FWLAB_HOST_ACTION_V0_BLOCK_FLUSH:
        return C43_STATUS_MEDIA_WRITE;
    default:
        return C43_STATUS_RESOURCE_FAILURE;
    }
}

static struct c43_slot *find_slot(
    struct c43_adapter *adapter,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    struct c43_record **owner)
{
    uint32_t record_index;

    for (record_index = 0; record_index < C43_RECORDS; ++record_index) {
        struct c43_record *record = &adapter->record[record_index];
        uint32_t index;

        if (!record->occupied) {
            continue;
        }
        for (index = 0; index < record->program.action_count; ++index) {
            if (argument_ref_equal(&record->slot[index].argument.reference,
                                   reference)) {
                if (owner != NULL) {
                    *owner = record;
                }
                return &record->slot[index];
            }
        }
    }
    return NULL;
}

static enum fwlab_spine_result_v0 c43_argument_read(
    void *context,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    struct fwlab_spine_profile_argument_v0 *argument)
{
    struct c43_adapter *adapter = adapter_from(context);
    struct c43_slot *slot;

    if (adapter == NULL || reference == NULL || argument == NULL ||
        !fwlab_host_action_argument_ref_v0_valid(reference)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    slot = find_slot(adapter, reference, NULL);
    if (slot == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    *argument = slot->argument;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 c43_payload_read(
    void *context,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    void *output,
    size_t output_size,
    uint32_t *actual_bytes)
{
    struct c43_adapter *adapter = adapter_from(context);
    struct c43_record *record = NULL;
    struct c43_slot *slot;

    if (adapter == NULL || reference == NULL || output == NULL ||
        actual_bytes == NULL ||
        !fwlab_host_action_argument_ref_v0_valid(reference)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    slot = find_slot(adapter, reference, &record);
    if (slot == NULL || record == NULL ||
        reference->kind != FWLAB_HOST_ACTION_V0_PAYLOAD_FILL ||
        slot->argument.payload_bytes == 0 ||
        slot->argument.payload_bytes != record->payload_bytes ||
        output_size != record->payload_bytes) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memcpy(output, record->payload, output_size);
    slot->payload_read = 1;
    *actual_bytes = record->payload_bytes;
    return FWLAB_SPINE_V0_OK;
}

static int result_facts_equal(
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

static int outcome_valid_for_kind(uint32_t outcome, uint16_t kind)
{
    if (outcome == FWLAB_SPINE_PROVIDER_V0_SUCCESS ||
        outcome == FWLAB_SPINE_PROVIDER_V0_CANCELLED) {
        return 1;
    }
    if (outcome == FWLAB_SPINE_PROVIDER_V0_TRANSFER_FAILURE) {
        return kind == FWLAB_HOST_ACTION_V0_DMA_IN ||
               kind == FWLAB_HOST_ACTION_V0_DMA_OUT;
    }
    if (outcome == FWLAB_SPINE_PROVIDER_V0_MEDIA_READ) {
        return kind == FWLAB_HOST_ACTION_V0_BLOCK_READ;
    }
    if (outcome == FWLAB_SPINE_PROVIDER_V0_MEDIA_WRITE) {
        return kind == FWLAB_HOST_ACTION_V0_BLOCK_WRITE ||
               kind == FWLAB_HOST_ACTION_V0_BLOCK_FLUSH;
    }
    if (outcome >= FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE &&
        outcome <= FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE_DELETE) {
        return kind == FWLAB_HOST_ACTION_V0_QUEUE_EFFECT;
    }
    return outcome == FWLAB_SPINE_PROVIDER_V0_RESOURCE_FAILURE;
}

static enum fwlab_spine_result_v0 c43_result_latch(
    void *context,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    const struct fwlab_host_action_status_v0 *status,
    uint32_t normalized_outcome,
    uint32_t result_dword0)
{
    struct c43_adapter *adapter = adapter_from(context);
    struct c43_record *record = NULL;
    struct c43_slot *slot;
    struct fwlab_spine_profile_result_v0 local;

    if (adapter == NULL || reference == NULL || status == NULL ||
        !fwlab_host_action_argument_ref_v0_valid(reference) ||
        !fwlab_host_action_status_v0_valid(status) ||
        normalized_outcome < FWLAB_SPINE_PROVIDER_V0_SUCCESS ||
        normalized_outcome > FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE_DELETE ||
        result_dword0 != 0 ||
        !outcome_valid_for_kind(normalized_outcome, reference->kind)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    slot = find_slot(adapter, reference, &record);
    if (slot == NULL || record == NULL ||
        !handle_equal(&record->program.command, &status->token.command) ||
        !origin_equal(&record->program.origin, &status->token.origin) ||
        status->token.ordinal != reference->ordinal ||
        status->token.kind != reference->kind ||
        (status->terminal_kind == FWLAB_HOST_ACTION_V0_SUCCEEDED &&
         normalized_outcome != FWLAB_SPINE_PROVIDER_V0_SUCCESS) ||
        (status->terminal_kind == FWLAB_HOST_ACTION_V0_CANCELLED &&
         normalized_outcome != FWLAB_SPINE_PROVIDER_V0_CANCELLED) ||
        (status->terminal_kind == FWLAB_HOST_ACTION_V0_FAILED &&
         (normalized_outcome == FWLAB_SPINE_PROVIDER_V0_SUCCESS ||
          normalized_outcome == FWLAB_SPINE_PROVIDER_V0_CANCELLED)) ||
        status->terminal_kind ==
            FWLAB_HOST_ACTION_V0_TERMINAL_QUARANTINED) {
        return FWLAB_SPINE_V0_POISONED;
    }
    memset(&local, 0, sizeof(local));
    local.version = FWLAB_SPINE_LIFECYCLE_V0_VERSION;
    local.size = sizeof(local);
    local.reference = *reference;
    local.status = *status;
    local.normalized_outcome = normalized_outcome;
    local.result_dword0 = result_dword0;
    if (slot->result_latched) {
        return result_facts_equal(&slot->result, &local)
                   ? FWLAB_SPINE_V0_OK
                   : FWLAB_SPINE_V0_POISONED;
    }
    slot->result = local;
    slot->result_latched = 1;
    return FWLAB_SPINE_V0_OK;
}

static uint32_t normalized_provider_status(
    uint32_t outcome,
    uint16_t action_kind)
{
    switch (outcome) {
    case FWLAB_SPINE_PROVIDER_V0_TRANSFER_FAILURE:
        return C43_STATUS_TRANSFER_FAILURE;
    case FWLAB_SPINE_PROVIDER_V0_MEDIA_READ:
        return C43_STATUS_MEDIA_READ;
    case FWLAB_SPINE_PROVIDER_V0_MEDIA_WRITE:
        return C43_STATUS_MEDIA_WRITE;
    case FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE:
        return C43_STATUS_INVALID_QUEUE;
    case FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE_SIZE:
        return C43_STATUS_INVALID_QUEUE_SIZE;
    case FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE_DELETE:
        return C43_STATUS_INVALID_QUEUE_DELETE;
    case FWLAB_SPINE_PROVIDER_V0_RESOURCE_FAILURE:
        return C43_STATUS_RESOURCE_FAILURE;
    default:
        return provider_status(action_kind);
    }
}

static enum fwlab_spine_result_v0 c43_complete(
    void *context,
    const struct fwlab_host_action_program_v0 *program,
    const struct fwlab_host_action_status_v0 *status,
    uint16_t status_count,
    struct fwlab_nvme_completion_intent *intent)
{
    struct c43_adapter *adapter = adapter_from(context);
    struct c43_record *record;
    uint32_t semantic_status;
    uint8_t dnr;
    uint8_t effect = FWLAB_NVME_EFFECT_NONE;
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
    semantic_status = record->semantic.status;
    dnr = record->semantic.dnr;
    if (semantic_status == C43_STATUS_SUCCESS) {
        int failed_index = -1;
        int cancelled_index = -1;
        int any_effect = 0;
        uint32_t failed_outcome = 0;

        for (index = 0; index < status_count; ++index) {
            struct c43_slot *slot = &record->slot[index];

            if (slot->result_latched) {
                struct fwlab_spine_profile_result_v0 observed = slot->result;

                observed.status = status[index];
                if (!result_facts_equal(&slot->result, &observed)) {
                    return FWLAB_SPINE_V0_QUARANTINED;
                }
            } else if (status[index].terminal_kind ==
                       FWLAB_HOST_ACTION_V0_SUCCEEDED) {
                return FWLAB_SPINE_V0_QUARANTINED;
            }
            if (status[index].token.kind ==
                    FWLAB_HOST_ACTION_V0_PAYLOAD_FILL &&
                status[index].terminal_kind ==
                    FWLAB_HOST_ACTION_V0_SUCCEEDED &&
                !slot->payload_read) {
                return FWLAB_SPINE_V0_QUARANTINED;
            }
            if (status[index].effect ==
                FWLAB_HOST_ACTION_V0_EFFECT_UNKNOWN_PREFIX) {
                return FWLAB_SPINE_V0_QUARANTINED;
            }
            if (status[index].effect != FWLAB_HOST_ACTION_V0_EFFECT_NONE) {
                any_effect = 1;
            }
            if (status[index].terminal_kind ==
                    FWLAB_HOST_ACTION_V0_FAILED &&
                failed_index < 0) {
                failed_index = (int)index;
                failed_outcome = slot->result_latched
                                     ? slot->result.normalized_outcome
                                     : 0;
            } else if (status[index].terminal_kind ==
                           FWLAB_HOST_ACTION_V0_CANCELLED &&
                       cancelled_index < 0) {
                cancelled_index = (int)index;
            } else if (status[index].terminal_kind ==
                       FWLAB_HOST_ACTION_V0_TERMINAL_QUARANTINED) {
                return FWLAB_SPINE_V0_QUARANTINED;
            }
        }
        if (failed_index >= 0) {
            semantic_status = normalized_provider_status(
                failed_outcome, status[failed_index].token.kind);
            dnr = (uint8_t)(semantic_status == C43_STATUS_INVALID_QUEUE ||
                            semantic_status == C43_STATUS_INVALID_QUEUE_SIZE ||
                            semantic_status ==
                                C43_STATUS_INVALID_QUEUE_DELETE);
            effect = any_effect ? FWLAB_NVME_EFFECT_UNKNOWN_PREFIX
                                : FWLAB_NVME_EFFECT_NONE;
        } else if (cancelled_index >= 0) {
            semantic_status = C43_STATUS_ABORTED;
            dnr = 0;
            effect = any_effect ? FWLAB_NVME_EFFECT_UNKNOWN_PREFIX
                                : FWLAB_NVME_EFFECT_NONE;
        } else if (record->semantic.kind == C43_KIND_ABORT) {
            if (!record->relation_latched || status_count != 1 ||
                status[0].terminal_kind !=
                    FWLAB_HOST_ACTION_V0_SUCCEEDED ||
                status[0].effect != FWLAB_HOST_ACTION_V0_EFFECT_NONE ||
                status[0].units_completed != 0 ||
                !record->slot[0].result_latched ||
                record->slot[0].result.normalized_outcome !=
                    FWLAB_SPINE_PROVIDER_V0_SUCCESS) {
                return FWLAB_SPINE_V0_QUARANTINED;
            }
            effect = FWLAB_NVME_EFFECT_NONE;
        } else if (status_count == 0) {
            effect = FWLAB_NVME_EFFECT_NONE;
        } else {
            for (index = 0; index < status_count; ++index) {
                if (status[index].terminal_kind !=
                        FWLAB_HOST_ACTION_V0_SUCCEEDED ||
                    status[index].effect !=
                        FWLAB_HOST_ACTION_V0_EFFECT_FULL) {
                    return FWLAB_SPINE_V0_QUARANTINED;
                }
            }
            effect = FWLAB_NVME_EFFECT_FULL;
        }
    }

    memset(intent, 0, sizeof(*intent));
    intent->version = FWLAB_NVME_COMPLETION_VERSION;
    intent->size = sizeof(*intent);
    intent->handle = program->command;
    intent->origin = program->origin;
    status_tuple(semantic_status, &intent->status_code,
                 &intent->status_code_type);
    intent->do_not_retry = dnr;
    intent->effect_class = effect;
    if (semantic_status == C43_STATUS_SUCCESS) {
        if (record->semantic.kind == C43_KIND_ABORT) {
            intent->result_dword0 =
                record->relation_decision ==
                        FWLAB_SPINE_ABORT_DECISION_V0_ABORT_WON
                    ? 0
                    : 1;
        } else if (record->program.action_count == 1 &&
                   record->program.action[0].kind ==
                       FWLAB_HOST_ACTION_V0_QUEUE_EFFECT) {
            intent->result_dword0 =
                record->slot[0].result.result_dword0;
        }
        if (record->payload_bytes != 0) {
            intent->actual_length = record->payload_bytes;
        } else if (record->semantic.kind == C43_KIND_READ ||
                   record->semantic.kind == C43_KIND_WRITE) {
            intent->actual_length = record->semantic.data_bytes;
        }
    }
    return fwlab_nvme_completion_valid(intent) ? FWLAB_SPINE_V0_OK
                                                : FWLAB_SPINE_V0_POISONED;
}

static enum fwlab_spine_result_v0 c43_retire(
    void *context,
    const struct fwlab_host_action_program_v0 *program)
{
    struct c43_adapter *adapter = adapter_from(context);
    struct c43_record *record;

    if (adapter == NULL || program == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = find_program(adapter, program);
    if (record == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (record->retire_remaining != 0) {
        --record->retire_remaining;
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    memset(record, 0, sizeof(*record));
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_host_profile_ops_v0 c43_ops = {
    .version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION,
    .size = sizeof(struct fwlab_host_profile_ops_v0),
    .plan = c43_plan,
    .complete = c43_complete,
    .retire = c43_retire,
};

static enum fwlab_spine_result_v0 c43_relation_sink(
    void *context,
    const struct fwlab_host_action_program_v0 *program,
    uint64_t abort_uid,
    uint32_t decision)
{
    struct c43_adapter *adapter = adapter_from(context);
    struct c43_record *record;

    if (adapter == NULL || program == NULL || abort_uid == 0 ||
        (decision != FWLAB_SPINE_ABORT_DECISION_V0_ABORT_WON &&
         decision != FWLAB_SPINE_ABORT_DECISION_V0_NOT_ABORTED)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = find_program(adapter, program);
    if (record == NULL || record->semantic.kind != C43_KIND_ABORT) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (record->relation_latched) {
        return record->relation_abort_uid == abort_uid &&
                       record->relation_decision == decision
                   ? FWLAB_SPINE_V0_OK
                   : FWLAB_SPINE_V0_POISONED;
    }
    record->relation_abort_uid = abort_uid;
    record->relation_decision = decision;
    record->relation_latched = 1;
    return FWLAB_SPINE_V0_OK;
}

size_t fwlab_c43_p1_adapter_v0_arena_size(void)
{
    return sizeof(struct c43_adapter);
}

size_t fwlab_c43_p1_adapter_v0_arena_alignment(void)
{
    return _Alignof(struct c43_adapter);
}

enum fwlab_spine_result_v0 fwlab_c43_p1_adapter_v0_init(
    void *arena,
    size_t arena_size,
    uint64_t instance_nonce,
    uint32_t generation,
    struct fwlab_host_profile_adapter_v0 *adapter)
{
    struct c43_adapter *context = arena;

    if (arena == NULL || adapter == NULL || arena_size != sizeof(*context) ||
        ((uintptr_t)arena % _Alignof(struct c43_adapter)) != 0 ||
        instance_nonce == 0 || generation == 0) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(context, 0, sizeof(*context));
    context->magic = C43_ADAPTER_MAGIC;
    context->instance_nonce = instance_nonce;
    context->generation = generation;
    context->next_uid = 1;
    memset(adapter, 0, sizeof(*adapter));
    adapter->ops = &c43_ops;
    adapter->context = arena;
    adapter->generation = generation;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_c43_p1_binding_v0(
    const struct fwlab_host_profile_adapter_v0 *adapter,
    uint32_t role,
    struct fwlab_spine_profile_binding_v0 *binding)
{
    struct c43_adapter *context;

    if (adapter == NULL || binding == NULL || adapter->ops != &c43_ops ||
        (context = adapter_from(adapter->context)) == NULL ||
        adapter->generation != context->generation ||
        (role != FWLAB_SPINE_ROLE_V0_NORMAL &&
         role != FWLAB_SPINE_ROLE_V0_ABORT)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(binding, 0, sizeof(*binding));
    binding->version = FWLAB_SPINE_LIFECYCLE_V0_VERSION;
    binding->size = sizeof(*binding);
    binding->adapter = *adapter;
    binding->adapter_instance_nonce = context->instance_nonce;
    binding->generation = context->generation;
    binding->argument_read = c43_argument_read;
    binding->payload_read = c43_payload_read;
    binding->result_latch = c43_result_latch;
    if (role == FWLAB_SPINE_ROLE_V0_ABORT) {
        binding->relation_sink = c43_relation_sink;
        binding->relation_context = context;
    }
    return FWLAB_SPINE_V0_OK;
}

void fwlab_c43_p1_retire_delay_v0(
    const struct fwlab_host_profile_adapter_v0 *adapter,
    uint32_t attempts)
{
    struct c43_adapter *context;
    uint32_t index;

    if (adapter == NULL || adapter->ops != &c43_ops ||
        (context = adapter_from(adapter->context)) == NULL) {
        return;
    }
    context->retire_delay = attempts;
    for (index = 0; index < C43_RECORDS; ++index) {
        if (context->record[index].occupied) {
            context->record[index].retire_remaining = attempts;
        }
    }
}
