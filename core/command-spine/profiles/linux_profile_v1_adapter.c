/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "spine_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fwlab/portable/nvme_codec.h"

#define LINUX_ADAPTER_MAGIC UINT64_C(0x4c4e585631414450)
#define LINUX_RECORDS 32u
#define LINUX_PAYLOAD_BYTES 4096u

enum linux_kind {
    LINUX_KIND_IDENTIFY_CONTROLLER = 1,
    LINUX_KIND_IDENTIFY_NAMESPACE = 2,
    LINUX_KIND_SMART = 3,
    LINUX_KIND_SET_NUMBER_OF_QUEUES = 4,
    LINUX_KIND_CREATE_CQ = 5,
    LINUX_KIND_CREATE_SQ = 6,
    LINUX_KIND_DELETE_CQ = 7,
    LINUX_KIND_DELETE_SQ = 8,
    LINUX_KIND_READ = 9,
    LINUX_KIND_WRITE = 10,
    LINUX_KIND_FLUSH = 11,
    LINUX_KIND_UNSUPPORTED = 12,
    LINUX_KIND_INVALID_IDENTIFY = 13
};

enum linux_status {
    LINUX_STATUS_SUCCESS = 0,
    LINUX_STATUS_UNSUPPORTED = 1,
    LINUX_STATUS_INVALID_FIELD = 2,
    LINUX_STATUS_INVALID_NAMESPACE = 3,
    LINUX_STATUS_LBA_RANGE = 4,
    LINUX_STATUS_COMMAND_SEQUENCE = 5,
    LINUX_STATUS_ABORTED = 6,
    LINUX_STATUS_TRANSFER_FAILURE = 7,
    LINUX_STATUS_MEDIA_READ = 8,
    LINUX_STATUS_MEDIA_WRITE = 9,
    LINUX_STATUS_RESOURCE_FAILURE = 10,
    LINUX_STATUS_INVALID_QUEUE = 11,
    LINUX_STATUS_INVALID_QUEUE_SIZE = 12,
    LINUX_STATUS_INVALID_QUEUE_DELETE = 13
};

struct linux_semantic {
    uint64_t slba;
    uint32_t namespace_id;
    uint32_t lba_count;
    uint32_t data_bytes;
    uint32_t requested_counts;
    uint32_t kind;
    uint32_t status;
    uint8_t dnr;
    uint8_t fua;
    uint8_t reserved[6];
};

struct linux_slot {
    struct fwlab_spine_profile_argument_v0 argument;
    struct fwlab_spine_profile_result_v0 result;
    uint8_t result_latched;
    uint8_t payload_read;
    uint8_t reserved[6];
};

struct linux_record {
    struct fwlab_host_action_program_v0 program;
    struct linux_semantic semantic;
    struct linux_slot slot[FWLAB_HOST_ACTION_V0_MAX_ACTIONS];
    uint8_t payload[LINUX_PAYLOAD_BYTES];
    uint32_t payload_bytes;
    uint32_t retire_remaining;
    uint8_t occupied;
    uint8_t reserved[7];
};

struct linux_adapter {
    uint64_t magic;
    uint64_t instance_nonce;
    uint64_t next_uid;
    uint32_t generation;
    uint32_t retire_delay;
    struct linux_record record[LINUX_RECORDS];
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

static void put_u16(uint8_t *output, size_t offset, uint16_t value)
{
    output[offset] = (uint8_t)value;
    output[offset + 1] = (uint8_t)(value >> 8);
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

static struct linux_adapter *adapter_from(void *context)
{
    struct linux_adapter *adapter = context;

    if (adapter == NULL || adapter->magic != LINUX_ADAPTER_MAGIC) {
        return NULL;
    }
    return adapter;
}

static struct linux_record *find_program(
    struct linux_adapter *adapter,
    const struct fwlab_host_action_program_v0 *program)
{
    uint32_t index;

    for (index = 0; index < LINUX_RECORDS; ++index) {
        if (adapter->record[index].occupied &&
            memcmp(&adapter->record[index].program, program,
                   sizeof(*program)) == 0) {
            return &adapter->record[index];
        }
    }
    return NULL;
}

static struct linux_record *find_identity(
    struct linux_adapter *adapter,
    const struct fwlab_nvme_command *command)
{
    uint32_t index;

    for (index = 0; index < LINUX_RECORDS; ++index) {
        struct linux_record *record = &adapter->record[index];

        if (record->occupied &&
            (handle_equal(&record->program.command, &command->handle) ||
             origin_equal(&record->program.origin, &command->origin))) {
            return record;
        }
    }
    return NULL;
}

static struct linux_record *find_free(struct linux_adapter *adapter)
{
    uint32_t index;

    for (index = 0; index < LINUX_RECORDS; ++index) {
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
            if ((cdw10 & UINT32_C(0xff)) == 1) {
                return LINUX_KIND_IDENTIFY_CONTROLLER;
            }
            if ((cdw10 & UINT32_C(0xff)) == 0) {
                return LINUX_KIND_IDENTIFY_NAMESPACE;
            }
            return LINUX_KIND_INVALID_IDENTIFY;
        case 0x02:
            return LINUX_KIND_SMART;
        case 0x09:
            return LINUX_KIND_SET_NUMBER_OF_QUEUES;
        case 0x05:
            return LINUX_KIND_CREATE_CQ;
        case 0x01:
            return LINUX_KIND_CREATE_SQ;
        case 0x04:
            return LINUX_KIND_DELETE_CQ;
        case 0x00:
            return LINUX_KIND_DELETE_SQ;
        default:
            return LINUX_KIND_UNSUPPORTED;
        }
    }
    if (command->queue_class == FWLAB_NVME_QUEUE_IO) {
        switch (command->opcode) {
        case 0x02:
            return LINUX_KIND_READ;
        case 0x01:
            return LINUX_KIND_WRITE;
        case 0x00:
            return LINUX_KIND_FLUSH;
        default:
            return LINUX_KIND_UNSUPPORTED;
        }
    }
    return LINUX_KIND_UNSUPPORTED;
}

static int data_required(uint32_t kind)
{
    return kind == LINUX_KIND_IDENTIFY_CONTROLLER ||
           kind == LINUX_KIND_IDENTIFY_NAMESPACE ||
           kind == LINUX_KIND_SMART || kind == LINUX_KIND_CREATE_CQ ||
           kind == LINUX_KIND_CREATE_SQ || kind == LINUX_KIND_READ ||
           kind == LINUX_KIND_WRITE;
}

static int namespace_valid(
    uint32_t kind,
    const struct fwlab_nvme_command *command)
{
    if (kind == LINUX_KIND_SMART) {
        return command->namespace_id == UINT32_MAX;
    }
    if (kind == LINUX_KIND_IDENTIFY_NAMESPACE || kind == LINUX_KIND_READ ||
        kind == LINUX_KIND_WRITE || kind == LINUX_KIND_FLUSH) {
        return command->namespace_id == 1;
    }
    return command->namespace_id == 0;
}

static int dwords_valid(
    uint32_t kind,
    const struct fwlab_nvme_command *command,
    struct linux_semantic *semantic)
{
    const uint32_t *dword = command->command_dword10_15;

    switch (kind) {
    case LINUX_KIND_IDENTIFY_CONTROLLER:
        return dword[0] == 1 && words_zero(dword + 1, 5);
    case LINUX_KIND_IDENTIFY_NAMESPACE:
        return dword[0] == 0 && words_zero(dword + 1, 5);
    case LINUX_KIND_SMART:
        return dword[0] == UINT32_C(0x007f0002) &&
               words_zero(dword + 1, 5);
    case LINUX_KIND_SET_NUMBER_OF_QUEUES:
        if (dword[0] != 7 || !words_zero(dword + 2, 4)) {
            return 0;
        }
        semantic->requested_counts = dword[1];
        return 1;
    case LINUX_KIND_CREATE_CQ:
        return dword[0] == UINT32_C(0x001f0001) &&
               dword[1] == UINT32_C(0x00000003) &&
               words_zero(dword + 2, 4);
    case LINUX_KIND_CREATE_SQ:
        return dword[0] == UINT32_C(0x001f0001) &&
               dword[1] == UINT32_C(0x00010001) &&
               words_zero(dword + 2, 4);
    case LINUX_KIND_DELETE_CQ:
    case LINUX_KIND_DELETE_SQ:
        return dword[0] == 1 && words_zero(dword + 1, 5);
    case LINUX_KIND_READ:
    case LINUX_KIND_WRITE: {
        const uint32_t allowed = kind == LINUX_KIND_WRITE
                                     ? UINT32_C(0xc000ffff)
                                     : UINT32_C(0x8000ffff);
        const uint64_t lba_count = (uint64_t)(dword[2] & UINT32_C(0xffff)) + 1;
        const uint64_t slba =
            (uint64_t)dword[0] | ((uint64_t)dword[1] << 32);
        uint64_t bytes;

        /* Linux marks readahead with Limited Retry and the prefetch hint.
         * This profile does no command-level retry or speculative caching;
         * those hints retain the same finite Block read/write semantics. */
        if ((dword[2] & ~allowed) != 0 || !words_zero(dword + 4, 2) ||
            (dword[3] != 0 &&
             !(kind == LINUX_KIND_READ && dword[3] == 7))) {
            return 0;
        }
        if (lba_count > 16 || slba >= 2048 ||
            slba > UINT64_MAX - lba_count || slba + lba_count > 2048) {
            semantic->status = LINUX_STATUS_LBA_RANGE;
            semantic->dnr = 1;
            return 1;
        }
        bytes = lba_count * UINT64_C(512);
        if (bytes > 8192 || bytes > UINT32_MAX) {
            semantic->status = LINUX_STATUS_LBA_RANGE;
            semantic->dnr = 1;
            return 1;
        }
        semantic->slba = slba;
        semantic->lba_count = (uint32_t)lba_count;
        semantic->data_bytes = (uint32_t)bytes;
        semantic->fua = (uint8_t)((dword[2] >> 30) & 1u);
        return 1;
    }
    case LINUX_KIND_FLUSH:
        return words_zero(dword, 6);
    default:
        return 1;
    }
}

static void transport_status(
    const struct fwlab_nvme_command *command,
    struct linux_semantic *semantic)
{
    switch (command->transport_fault) {
    case FWLAB_NVME_TRANSPORT_UNSUPPORTED_FORMAT:
        semantic->status = LINUX_STATUS_INVALID_FIELD;
        semantic->dnr = 1;
        break;
    case FWLAB_NVME_TRANSPORT_STALE_GENERATION:
        semantic->status = LINUX_STATUS_COMMAND_SEQUENCE;
        semantic->dnr = 1;
        break;
    case FWLAB_NVME_TRANSPORT_QUEUE_MEMORY:
        semantic->status = LINUX_STATUS_TRANSFER_FAILURE;
        semantic->dnr = 0;
        break;
    default:
        semantic->status = LINUX_STATUS_TRANSFER_FAILURE;
        semantic->dnr = 1;
        break;
    }
}

static void sanitize(
    const struct fwlab_nvme_command *command,
    struct linux_semantic *semantic)
{
    uint32_t kind;

    memset(semantic, 0, sizeof(*semantic));
    semantic->namespace_id = command->namespace_id;
    if (command->transport_fault != FWLAB_NVME_TRANSPORT_NONE) {
        semantic->kind = LINUX_KIND_UNSUPPORTED;
        transport_status(command, semantic);
        return;
    }
    kind = classify_kind(command);
    semantic->kind = kind;
    if (kind == LINUX_KIND_UNSUPPORTED) {
        semantic->status = LINUX_STATUS_UNSUPPORTED;
        semantic->dnr = 1;
        return;
    }
    if (command->command_dword2 != 0 || command->command_dword3 != 0 ||
        command->command_flags_reserved != 0 ||
        command->fuse != FWLAB_NVME_FUSE_NONE ||
        command->data_pointer_format != FWLAB_NVME_DATA_POINTER_PRP ||
        command->metadata_address_present ||
        command->data_address_present != (uint8_t)data_required(kind)) {
        semantic->status = LINUX_STATUS_INVALID_FIELD;
        semantic->dnr = 1;
        return;
    }
    if (kind == LINUX_KIND_INVALID_IDENTIFY) {
        semantic->status = LINUX_STATUS_INVALID_FIELD;
        semantic->dnr = 1;
        return;
    }
    if (!namespace_valid(kind, command)) {
        semantic->status = LINUX_STATUS_INVALID_NAMESPACE;
        semantic->dnr = 1;
        return;
    }
    if (!dwords_valid(kind, command, semantic)) {
        semantic->status = LINUX_STATUS_INVALID_FIELD;
        semantic->dnr = 1;
    }
}

static void program_base(
    struct linux_adapter *adapter,
    struct linux_record *record,
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
    struct linux_adapter *adapter,
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

static void encode_payload(struct linux_record *record)
{
    static const char serial[] = "FWLABLINUXV1-0000001";
    static const char model[] = "SSD Firmware Lab Linux-profile-v1";
    static const char firmware[] = "LNXV1";
    uint8_t *payload = record->payload;

    memset(payload, 0, LINUX_PAYLOAD_BYTES);
    switch (record->semantic.kind) {
    case LINUX_KIND_IDENTIFY_CONTROLLER:
        record->payload_bytes = LINUX_PAYLOAD_BYTES;
        put_u16(payload, 0, UINT16_C(0xfffa));
        put_u16(payload, 2, UINT16_C(0xfffa));
        space_padded(payload, 4, 20, serial, sizeof(serial) - 1);
        space_padded(payload, 24, 40, model, sizeof(model) - 1);
        space_padded(payload, 64, 8, firmware, sizeof(firmware) - 1);
        payload[77] = 1;
        put_u16(payload, 78, 1);
        put_u32(payload, 80, UINT32_C(0x00010000));
        payload[512] = UINT8_C(0x66);
        payload[513] = UINT8_C(0x44);
        put_u32(payload, 516, 1);
        break;
    case LINUX_KIND_IDENTIFY_NAMESPACE:
        record->payload_bytes = LINUX_PAYLOAD_BYTES;
        put_u64(payload, 0, 2048);
        put_u64(payload, 8, 2048);
        put_u64(payload, 16, 2048);
        payload[130] = 9;
        break;
    case LINUX_KIND_SMART:
        record->payload_bytes = 512;
        put_u16(payload, 1, 300);
        payload[3] = 100;
        payload[4] = 10;
        break;
    default:
        record->payload_bytes = 0;
        break;
    }
}

static void build_actions(
    struct linux_adapter *adapter,
    struct linux_record *record)
{
    struct fwlab_host_action_program_v0 *program = &record->program;

    if (record->semantic.status != LINUX_STATUS_SUCCESS) {
        return;
    }
    switch (record->semantic.kind) {
    case LINUX_KIND_IDENTIFY_CONTROLLER:
    case LINUX_KIND_IDENTIFY_NAMESPACE:
    case LINUX_KIND_SMART:
        encode_payload(record);
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_PAYLOAD_FILL, 0, 0);
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_DMA_OUT,
                   UINT32_C(1), FWLAB_HOST_WITNESS_V0_PAYLOAD_READY);
        program->completion_required_witness_mask =
            FWLAB_HOST_WITNESS_V0_PAYLOAD_READY |
            FWLAB_HOST_WITNESS_V0_DMA_OUT_COMPLETE;
        break;
    case LINUX_KIND_SET_NUMBER_OF_QUEUES:
    case LINUX_KIND_CREATE_CQ:
    case LINUX_KIND_CREATE_SQ:
    case LINUX_KIND_DELETE_CQ:
    case LINUX_KIND_DELETE_SQ:
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_QUEUE_EFFECT, 0, 0);
        program->completion_required_witness_mask =
            FWLAB_HOST_WITNESS_V0_QUEUE_EFFECT;
        break;
    case LINUX_KIND_READ:
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_BLOCK_READ, 0, 0);
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_DMA_OUT,
                   UINT32_C(1),
                   FWLAB_HOST_WITNESS_V0_BLOCK_READ_COMPLETE);
        program->completion_required_witness_mask =
            FWLAB_HOST_WITNESS_V0_BLOCK_READ_COMPLETE |
            FWLAB_HOST_WITNESS_V0_DMA_OUT_COMPLETE;
        break;
    case LINUX_KIND_WRITE:
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_DMA_IN, 0, 0);
        add_action(adapter, program, FWLAB_HOST_ACTION_V0_BLOCK_WRITE,
                   UINT32_C(1), FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE);
        program->completion_required_witness_mask =
            FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE |
            FWLAB_HOST_WITNESS_V0_BLOCK_WRITE_COMPLETE;
        break;
    case LINUX_KIND_FLUSH:
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
    case LINUX_KIND_IDENTIFY_CONTROLLER:
        return FWLAB_SPINE_SEMANTIC_V0_IDENTIFY_CONTROLLER;
    case LINUX_KIND_IDENTIFY_NAMESPACE:
        return FWLAB_SPINE_SEMANTIC_V0_IDENTIFY_NAMESPACE;
    case LINUX_KIND_SMART:
        return FWLAB_SPINE_SEMANTIC_V0_SMART;
    case LINUX_KIND_SET_NUMBER_OF_QUEUES:
        return FWLAB_SPINE_SEMANTIC_V0_SET_NUMBER_OF_QUEUES;
    case LINUX_KIND_CREATE_CQ:
        return FWLAB_SPINE_SEMANTIC_V0_CREATE_CQ;
    case LINUX_KIND_CREATE_SQ:
        return FWLAB_SPINE_SEMANTIC_V0_CREATE_SQ;
    case LINUX_KIND_DELETE_CQ:
        return FWLAB_SPINE_SEMANTIC_V0_DELETE_CQ;
    case LINUX_KIND_DELETE_SQ:
        return FWLAB_SPINE_SEMANTIC_V0_DELETE_SQ;
    case LINUX_KIND_READ:
        return FWLAB_SPINE_SEMANTIC_V0_READ;
    case LINUX_KIND_WRITE:
        return FWLAB_SPINE_SEMANTIC_V0_WRITE;
    case LINUX_KIND_FLUSH:
        return FWLAB_SPINE_SEMANTIC_V0_FLUSH;
    default:
        return 0;
    }
}

static void populate_slots(struct linux_record *record)
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
        case LINUX_KIND_IDENTIFY_CONTROLLER:
        case LINUX_KIND_IDENTIFY_NAMESPACE:
        case LINUX_KIND_SMART:
            argument->payload_bytes = record->payload_bytes;
            argument->exact_bytes = record->payload_bytes;
            break;
        case LINUX_KIND_SET_NUMBER_OF_QUEUES:
            argument->requested_cq_count =
                (record->semantic.requested_counts & UINT32_C(0xffff)) + 1;
            argument->requested_sq_count =
                (record->semantic.requested_counts >> 16) + 1;
            break;
        case LINUX_KIND_CREATE_CQ:
            argument->queue_id = 1;
            argument->queue_entries = 32;
            argument->interrupt_vector = 0;
            break;
        case LINUX_KIND_CREATE_SQ:
            argument->queue_id = 1;
            argument->queue_entries = 32;
            argument->associated_queue_id = 1;
            break;
        case LINUX_KIND_DELETE_CQ:
        case LINUX_KIND_DELETE_SQ:
            argument->queue_id = 1;
            break;
        case LINUX_KIND_READ:
            argument->lba = record->semantic.slba;
            argument->lba_count = record->semantic.lba_count;
            argument->exact_bytes = record->semantic.data_bytes;
            argument->durability = FWLAB_SPINE_DURABILITY_V0_NONE;
            break;
        case LINUX_KIND_WRITE:
            argument->lba = record->semantic.slba;
            argument->lba_count = record->semantic.lba_count;
            argument->exact_bytes = record->semantic.data_bytes;
            if (argument->reference.kind ==
                FWLAB_HOST_ACTION_V0_BLOCK_WRITE) {
                argument->durability = FWLAB_SPINE_DURABILITY_V0_SELF;
            }
            break;
        case LINUX_KIND_FLUSH:
            argument->durability = FWLAB_SPINE_DURABILITY_V0_FRONTIER;
            break;
        default:
            break;
        }
    }
}

static enum fwlab_spine_result_v0 linux_plan(
    void *context,
    const struct fwlab_nvme_command *command,
    struct fwlab_host_action_program_v0 *program)
{
    struct linux_adapter *adapter = adapter_from(context);
    struct linux_record *record;
    struct linux_semantic semantic;
    uint32_t uid_count = 2;

    if (adapter == NULL || command == NULL || program == NULL ||
        !fwlab_nvme_command_valid(command)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    sanitize(command, &semantic);
    record = find_identity(adapter, command);
    if (record != NULL) {
        if (!handle_equal(&record->program.command, &command->handle) ||
            !origin_equal(&record->program.origin, &command->origin) ||
            memcmp(&record->semantic, &semantic, sizeof(semantic)) != 0) {
            return FWLAB_SPINE_V0_POISONED;
        }
        *program = record->program;
        return FWLAB_SPINE_V0_OK;
    }
    record = find_free(adapter);
    if (record == NULL) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    if (semantic.status == LINUX_STATUS_SUCCESS) {
        switch (semantic.kind) {
        case LINUX_KIND_IDENTIFY_CONTROLLER:
        case LINUX_KIND_IDENTIFY_NAMESPACE:
        case LINUX_KIND_SMART:
        case LINUX_KIND_READ:
        case LINUX_KIND_WRITE:
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
    case LINUX_STATUS_SUCCESS:
        *status_code = 0x00;
        break;
    case LINUX_STATUS_UNSUPPORTED:
        *status_code = 0x01;
        break;
    case LINUX_STATUS_INVALID_FIELD:
        *status_code = 0x02;
        break;
    case LINUX_STATUS_INVALID_NAMESPACE:
        *status_code = 0x0b;
        break;
    case LINUX_STATUS_LBA_RANGE:
        *status_code = 0x80;
        break;
    case LINUX_STATUS_COMMAND_SEQUENCE:
        *status_code = 0x0c;
        break;
    case LINUX_STATUS_INVALID_QUEUE:
        *status_code_type = 1;
        *status_code = 0x01;
        break;
    case LINUX_STATUS_INVALID_QUEUE_SIZE:
        *status_code_type = 1;
        *status_code = 0x02;
        break;
    case LINUX_STATUS_INVALID_QUEUE_DELETE:
        *status_code_type = 1;
        *status_code = 0x0c;
        break;
    case LINUX_STATUS_ABORTED:
        *status_code = 0x07;
        break;
    case LINUX_STATUS_TRANSFER_FAILURE:
        *status_code = 0x04;
        break;
    case LINUX_STATUS_MEDIA_READ:
        *status_code_type = 2;
        *status_code = 0x81;
        break;
    case LINUX_STATUS_MEDIA_WRITE:
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
        return LINUX_STATUS_TRANSFER_FAILURE;
    case FWLAB_HOST_ACTION_V0_BLOCK_READ:
        return LINUX_STATUS_MEDIA_READ;
    case FWLAB_HOST_ACTION_V0_BLOCK_WRITE:
    case FWLAB_HOST_ACTION_V0_BLOCK_FLUSH:
        return LINUX_STATUS_MEDIA_WRITE;
    default:
        return LINUX_STATUS_RESOURCE_FAILURE;
    }
}

static struct linux_slot *find_slot(
    struct linux_adapter *adapter,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    struct linux_record **owner)
{
    uint32_t record_index;

    for (record_index = 0; record_index < LINUX_RECORDS; ++record_index) {
        struct linux_record *record = &adapter->record[record_index];
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

static enum fwlab_spine_result_v0 linux_argument_read(
    void *context,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    struct fwlab_spine_profile_argument_v0 *argument)
{
    struct linux_adapter *adapter = adapter_from(context);
    struct linux_slot *slot;

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

static enum fwlab_spine_result_v0 linux_payload_read(
    void *context,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    void *output,
    size_t output_size,
    uint32_t *actual_bytes)
{
    struct linux_adapter *adapter = adapter_from(context);
    struct linux_record *record = NULL;
    struct linux_slot *slot;

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

static enum fwlab_spine_result_v0 linux_result_latch(
    void *context,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    const struct fwlab_host_action_status_v0 *status,
    uint32_t normalized_outcome,
    uint32_t result_dword0)
{
    struct linux_adapter *adapter = adapter_from(context);
    struct linux_record *record = NULL;
    struct linux_slot *slot;
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
        return LINUX_STATUS_TRANSFER_FAILURE;
    case FWLAB_SPINE_PROVIDER_V0_MEDIA_READ:
        return LINUX_STATUS_MEDIA_READ;
    case FWLAB_SPINE_PROVIDER_V0_MEDIA_WRITE:
        return LINUX_STATUS_MEDIA_WRITE;
    case FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE:
        return LINUX_STATUS_INVALID_QUEUE;
    case FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE_SIZE:
        return LINUX_STATUS_INVALID_QUEUE_SIZE;
    case FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE_DELETE:
        return LINUX_STATUS_INVALID_QUEUE_DELETE;
    case FWLAB_SPINE_PROVIDER_V0_RESOURCE_FAILURE:
        return LINUX_STATUS_RESOURCE_FAILURE;
    default:
        return provider_status(action_kind);
    }
}

static enum fwlab_spine_result_v0 linux_complete(
    void *context,
    const struct fwlab_host_action_program_v0 *program,
    const struct fwlab_host_action_status_v0 *status,
    uint16_t status_count,
    struct fwlab_nvme_completion_intent *intent)
{
    struct linux_adapter *adapter = adapter_from(context);
    struct linux_record *record;
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
    if (semantic_status == LINUX_STATUS_SUCCESS) {
        int failed_index = -1;
        int cancelled_index = -1;
        int any_effect = 0;
        uint32_t failed_outcome = 0;

        for (index = 0; index < status_count; ++index) {
            struct linux_slot *slot = &record->slot[index];

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
            dnr = (uint8_t)(semantic_status == LINUX_STATUS_INVALID_QUEUE ||
                            semantic_status ==
                                LINUX_STATUS_INVALID_QUEUE_SIZE ||
                            semantic_status ==
                                LINUX_STATUS_INVALID_QUEUE_DELETE);
            effect = any_effect ? FWLAB_NVME_EFFECT_UNKNOWN_PREFIX
                                : FWLAB_NVME_EFFECT_NONE;
        } else if (cancelled_index >= 0) {
            semantic_status = LINUX_STATUS_ABORTED;
            dnr = 0;
            effect = any_effect ? FWLAB_NVME_EFFECT_UNKNOWN_PREFIX
                                : FWLAB_NVME_EFFECT_NONE;
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
    if (semantic_status == LINUX_STATUS_SUCCESS) {
        if (record->program.action_count == 1 &&
            record->program.action[0].kind ==
                FWLAB_HOST_ACTION_V0_QUEUE_EFFECT) {
            intent->result_dword0 =
                record->slot[0].result.result_dword0;
        }
        if (record->payload_bytes != 0) {
            intent->actual_length = record->payload_bytes;
        } else if (record->semantic.kind == LINUX_KIND_READ ||
                   record->semantic.kind == LINUX_KIND_WRITE) {
            intent->actual_length = record->semantic.data_bytes;
        }
    }
    return fwlab_nvme_completion_valid(intent) ? FWLAB_SPINE_V0_OK
                                                : FWLAB_SPINE_V0_POISONED;
}

static enum fwlab_spine_result_v0 linux_retire(
    void *context,
    const struct fwlab_host_action_program_v0 *program)
{
    struct linux_adapter *adapter = adapter_from(context);
    struct linux_record *record;

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

static const struct fwlab_host_profile_ops_v0 linux_ops = {
    .version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION,
    .size = sizeof(struct fwlab_host_profile_ops_v0),
    .plan = linux_plan,
    .complete = linux_complete,
    .retire = linux_retire,
};

size_t fwlab_linux_profile_v1_adapter_arena_size(void)
{
    return sizeof(struct linux_adapter);
}

size_t fwlab_linux_profile_v1_adapter_arena_alignment(void)
{
    return _Alignof(struct linux_adapter);
}

enum fwlab_spine_result_v0 fwlab_linux_profile_v1_adapter_init(
    void *arena,
    size_t arena_size,
    uint64_t instance_nonce,
    uint32_t generation,
    struct fwlab_host_profile_adapter_v0 *adapter)
{
    struct linux_adapter *context = arena;

    if (arena == NULL || adapter == NULL || arena_size != sizeof(*context) ||
        ((uintptr_t)arena % _Alignof(struct linux_adapter)) != 0 ||
        instance_nonce == 0 || generation == 0) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(context, 0, sizeof(*context));
    context->magic = LINUX_ADAPTER_MAGIC;
    context->instance_nonce = instance_nonce;
    context->generation = generation;
    context->next_uid = 1;
    memset(adapter, 0, sizeof(*adapter));
    adapter->ops = &linux_ops;
    adapter->context = arena;
    adapter->generation = generation;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_linux_profile_v1_binding_v0(
    const struct fwlab_host_profile_adapter_v0 *adapter,
    uint32_t role,
    struct fwlab_spine_profile_binding_v0 *binding)
{
    struct linux_adapter *context;

    if (adapter == NULL || binding == NULL || adapter->ops != &linux_ops ||
        (context = adapter_from(adapter->context)) == NULL ||
        adapter->generation != context->generation ||
        role != FWLAB_SPINE_ROLE_V0_NORMAL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(binding, 0, sizeof(*binding));
    binding->version = FWLAB_SPINE_LIFECYCLE_V0_VERSION;
    binding->size = sizeof(*binding);
    binding->adapter = *adapter;
    binding->adapter_instance_nonce = context->instance_nonce;
    binding->generation = context->generation;
    binding->argument_read = linux_argument_read;
    binding->payload_read = linux_payload_read;
    binding->result_latch = linux_result_latch;
    return FWLAB_SPINE_V0_OK;
}

void fwlab_linux_profile_v1_retire_delay_v0(
    const struct fwlab_host_profile_adapter_v0 *adapter,
    uint32_t attempts)
{
    struct linux_adapter *context;
    uint32_t index;

    if (adapter == NULL || adapter->ops != &linux_ops ||
        (context = adapter_from(adapter->context)) == NULL) {
        return;
    }
    context->retire_delay = attempts;
    for (index = 0; index < LINUX_RECORDS; ++index) {
        if (context->record[index].occupied) {
            context->record[index].retire_remaining = attempts;
        }
    }
}
