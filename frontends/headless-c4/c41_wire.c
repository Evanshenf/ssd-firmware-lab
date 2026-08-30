/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c41_wire.h"

#include "fwlab/portable/nvme_codec.h"

static uint16_t get_u16(const uint8_t *input)
{
    return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8));
}

static uint32_t get_u32(const uint8_t *input)
{
    return (uint32_t)input[0] |
           ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) |
           ((uint32_t)input[3] << 24);
}

static uint64_t get_u64(const uint8_t *input)
{
    return (uint64_t)get_u32(input) | ((uint64_t)get_u32(input + 4) << 32);
}

static void put_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static void clear_bytes(uint8_t *output, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = 0;
    }
}

static int bytes_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

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

enum c41_wire_result c41_sqe_decode(
    const uint8_t *input,
    size_t input_size,
    struct c41_raw_command *command)
{
    struct c41_raw_command local = {0};
    size_t index;
    uint8_t flags;

    if (input == NULL || command == NULL || input_size != C41_SQE_BYTES) {
        return C41_WIRE_INVALID_ARGUMENT;
    }
    flags = input[1];
    local.opcode = input[0];
    local.fuse = flags & 0x03u;
    local.command_flags_reserved = (flags >> 2) & 0x0fu;
    local.data_pointer_format = flags >> 6;
    local.command_id = get_u16(input + 2);
    local.namespace_id = get_u32(input + 4);
    local.command_dword2 = get_u32(input + 8);
    local.command_dword3 = get_u32(input + 12);
    local.metadata_pointer = get_u64(input + 16);
    local.data_pointer1 = get_u64(input + 24);
    local.data_pointer2 = get_u64(input + 32);
    for (index = 0; index < 6; ++index) {
        local.command_dword10_15[index] = get_u32(input + 40 + index * 4);
    }
    *command = local;
    return C41_WIRE_OK;
}

enum c41_wire_result c41_cqe_encode(
    const struct c41_raw_completion *completion,
    uint8_t *output,
    size_t output_size)
{
    uint8_t wire[C41_CQE_BYTES];
    uint16_t status;
    size_t index;

    if (completion == NULL || output == NULL || output_size != C41_CQE_BYTES) {
        return C41_WIRE_INVALID_ARGUMENT;
    }
    if (completion->phase > 1 || completion->status_code_type > 0x7u ||
        completion->command_retry_delay > 0x3u || completion->more > 1 ||
        completion->do_not_retry > 1) {
        return C41_WIRE_INVALID_VALUE;
    }
    status = (uint16_t)completion->phase |
             ((uint16_t)completion->status_code << 1) |
             ((uint16_t)completion->status_code_type << 9) |
             ((uint16_t)completion->command_retry_delay << 12) |
             ((uint16_t)completion->more << 14) |
             ((uint16_t)completion->do_not_retry << 15);
    clear_bytes(wire, sizeof(wire));
    put_u32(wire, completion->result_dword0);
    put_u16(wire + 8, completion->submission_queue_head);
    put_u16(wire + 10, completion->submission_queue_id);
    put_u16(wire + 12, completion->command_id);
    put_u16(wire + 14, status);
    for (index = 0; index < sizeof(wire); ++index) {
        output[index] = wire[index];
    }
    return C41_WIRE_OK;
}

enum c41_wire_result c41_cqe_decode(
    const uint8_t *input,
    size_t input_size,
    struct c41_raw_completion *completion)
{
    struct c41_raw_completion local = {0};
    uint16_t status;

    if (input == NULL || completion == NULL || input_size != C41_CQE_BYTES) {
        return C41_WIRE_INVALID_ARGUMENT;
    }
    if (!bytes_zero(input + 4, 4)) {
        return C41_WIRE_BAD_ENCODING;
    }
    status = get_u16(input + 14);
    local.result_dword0 = get_u32(input);
    local.submission_queue_head = get_u16(input + 8);
    local.submission_queue_id = get_u16(input + 10);
    local.command_id = get_u16(input + 12);
    local.phase = status & 0x01u;
    local.status_code = (uint8_t)(status >> 1);
    local.status_code_type = (uint8_t)((status >> 9) & 0x07u);
    local.command_retry_delay = (uint8_t)((status >> 12) & 0x03u);
    local.more = (uint8_t)((status >> 14) & 0x01u);
    local.do_not_retry = (uint8_t)((status >> 15) & 0x01u);
    *completion = local;
    return C41_WIRE_OK;
}

enum c41_wire_result c41_capture_command(
    const struct c41_raw_command *raw,
    const struct c41_capture_context *context,
    struct fwlab_nvme_command *command)
{
    struct fwlab_nvme_command local = {0};
    size_t index;

    if (raw == NULL || context == NULL || command == NULL ||
        !bytes_zero(context->reserved, sizeof(context->reserved))) {
        return C41_WIRE_INVALID_ARGUMENT;
    }
    local.version = FWLAB_NVME_COMMAND_VERSION;
    local.size = sizeof(local);
    local.handle = context->handle;
    local.origin = context->origin;
    local.trace_cookie = context->trace_cookie;
    local.safety_generation = context->safety_generation;
    local.namespace_id = raw->namespace_id;
    local.command_dword2 = raw->command_dword2;
    local.command_dword3 = raw->command_dword3;
    for (index = 0; index < 6; ++index) {
        local.command_dword10_15[index] = raw->command_dword10_15[index];
    }
    local.transport_fault = context->transport_fault;
    local.opcode = raw->opcode;
    local.queue_class = context->queue_class;
    local.fuse = raw->fuse;
    local.data_pointer_format = raw->data_pointer_format;
    local.data_address_present =
        (uint8_t)(raw->data_pointer1 != 0 || raw->data_pointer2 != 0);
    local.metadata_address_present = (uint8_t)(raw->metadata_pointer != 0);
    local.command_flags_reserved = raw->command_flags_reserved;
    if (!fwlab_nvme_command_valid(&local)) {
        return C41_WIRE_INVALID_VALUE;
    }
    *command = local;
    return C41_WIRE_OK;
}

enum c41_wire_result c41_completion_publish(
    const struct fwlab_nvme_completion_intent *intent,
    const struct c41_publication_context *context,
    uint8_t *output,
    size_t output_size)
{
    struct c41_raw_completion raw = {0};

    if (intent == NULL || context == NULL || output == NULL ||
        output_size != C41_CQE_BYTES || context->reserved != 0) {
        return C41_WIRE_INVALID_ARGUMENT;
    }
    if (!fwlab_nvme_completion_valid(intent)) {
        return C41_WIRE_INVALID_VALUE;
    }
    if (!handle_equal(&intent->handle, &context->handle) ||
        !origin_equal(&intent->origin, &context->origin)) {
        return C41_WIRE_IDENTITY_MISMATCH;
    }
    raw.result_dword0 = intent->result_dword0;
    raw.submission_queue_head = context->submission_queue_head;
    raw.submission_queue_id = context->submission_queue_id;
    raw.command_id = context->command_id;
    raw.phase = context->phase;
    raw.status_code = (uint8_t)intent->status_code;
    raw.status_code_type = intent->status_code_type;
    raw.command_retry_delay = intent->command_retry_delay;
    raw.more = intent->more;
    raw.do_not_retry = intent->do_not_retry;
    return c41_cqe_encode(&raw, output, output_size);
}
