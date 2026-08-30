/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fwlab/portable/nvme_codec.h"

#define COMMAND_MAGIC0 ((uint8_t)'F')
#define COMMAND_MAGIC1 ((uint8_t)'4')
#define COMMAND_MAGIC2 ((uint8_t)'N')
#define COMMAND_MAGIC3 ((uint8_t)'C')
#define COMPLETION_MAGIC2 ((uint8_t)'C')
#define COMPLETION_MAGIC3 ((uint8_t)'I')
#define PROFILE_MAGIC2 ((uint8_t)'P')
#define PROFILE_MAGIC3 ((uint8_t)'F')

_Static_assert(sizeof(struct fwlab_nvme_command) ==
               FWLAB_NVME_COMMAND_WIRE_BYTES,
               "unexpected command native size");
_Static_assert(sizeof(struct fwlab_nvme_completion_intent) ==
               FWLAB_NVME_COMPLETION_WIRE_BYTES,
               "unexpected completion native size");
_Static_assert(sizeof(struct fwlab_nvme_profile) ==
               FWLAB_NVME_PROFILE_WIRE_BYTES,
               "unexpected profile native size");

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

static void put_u64(uint8_t *output, uint64_t value)
{
    put_u32(output, (uint32_t)value);
    put_u32(output + 4, (uint32_t)(value >> 32));
}

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

static void clear_bytes(uint8_t *output, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = 0;
    }
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

static int bytes_zero(const uint8_t *bytes, size_t count)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int handle_valid(const struct fwlab_nvme_command_handle *handle)
{
    return handle->instance_nonce != 0 && handle->command_uid != 0 &&
           handle->controller_epoch != 0 && handle->generation != 0;
}

static int origin_valid(const struct fwlab_nvme_origin_token *origin)
{
    return origin->word[0] != 0 || origin->word[1] != 0;
}

int fwlab_nvme_command_valid(const struct fwlab_nvme_command *command)
{
    if (command == NULL || command->version != FWLAB_NVME_COMMAND_VERSION ||
        command->size != sizeof(*command) || command->reserved0 != 0 ||
        !handle_valid(&command->handle) || !origin_valid(&command->origin) ||
        command->trace_cookie == 0 || command->safety_generation == 0) {
        return 0;
    }
    if (command->queue_class != FWLAB_NVME_QUEUE_ADMIN &&
        command->queue_class != FWLAB_NVME_QUEUE_IO) {
        return 0;
    }
    if (command->fuse > FWLAB_NVME_FUSE_RESERVED ||
        command->data_pointer_format > FWLAB_NVME_DATA_POINTER_RESERVED ||
        command->data_address_present > 1 ||
        command->metadata_address_present > 1 ||
        command->command_flags_reserved > 0x0fu || command->reserved1 != 0 ||
        command->transport_fault > FWLAB_NVME_TRANSPORT_STALE_GENERATION ||
        !words_zero(command->reserved2, 5)) {
        return 0;
    }
    return 1;
}

int fwlab_nvme_completion_valid(
    const struct fwlab_nvme_completion_intent *completion)
{
    if (completion == NULL ||
        completion->version != FWLAB_NVME_COMPLETION_VERSION ||
        completion->size != sizeof(*completion) ||
        completion->reserved0 != 0 ||
        !handle_valid(&completion->handle) ||
        !origin_valid(&completion->origin)) {
        return 0;
    }
    if (completion->status_code > 0xffu ||
        completion->status_code_type > 0x7u ||
        completion->command_retry_delay > 0x3u || completion->more > 1 ||
        completion->do_not_retry > 1 ||
        completion->effect_class > FWLAB_NVME_EFFECT_UNKNOWN_PREFIX ||
        completion->reserved1 != 0) {
        return 0;
    }
    return 1;
}

int fwlab_nvme_profile_valid(const struct fwlab_nvme_profile *profile)
{
    const uint32_t features = FWLAB_NVME_PROFILE_READ |
                              FWLAB_NVME_PROFILE_WRITE |
                              FWLAB_NVME_PROFILE_FLUSH |
                              FWLAB_NVME_PROFILE_FUA |
                              FWLAB_NVME_PROFILE_VOLATILE_WRITE_CACHE |
                              FWLAB_NVME_PROFILE_PRP_DIRECT;

    return profile != NULL &&
           profile->version == FWLAB_NVME_PROFILE_VERSION &&
           profile->size == sizeof(*profile) && profile->reserved0 == 0 &&
           profile->namespace_count == 1 && profile->lba_bytes == 512 &&
           profile->lba_count == 8 && profile->memory_page_bytes == 4096 &&
           profile->maximum_transfer_bytes == 4096 &&
           profile->maximum_io_queue_pairs == 1 &&
           profile->integration_queue_depth == 4 &&
           profile->queue_depth_hard_maximum == 32 &&
           profile->data_segments_hard_maximum == 2 &&
           profile->feature_flags == features &&
           words_zero(profile->reserved1, 6);
}

enum fwlab_nvme_codec_result fwlab_nvme_command_encode(
    const struct fwlab_nvme_command *command,
    uint8_t *output,
    size_t output_size)
{
    uint8_t wire[FWLAB_NVME_COMMAND_WIRE_BYTES];
    size_t index;

    if (command == NULL || output == NULL ||
        output_size != FWLAB_NVME_COMMAND_WIRE_BYTES) {
        return FWLAB_NVME_CODEC_INVALID_ARGUMENT;
    }
    if (!fwlab_nvme_command_valid(command)) {
        return FWLAB_NVME_CODEC_INVALID_VALUE;
    }
    clear_bytes(wire, sizeof(wire));
    wire[0] = COMMAND_MAGIC0;
    wire[1] = COMMAND_MAGIC1;
    wire[2] = COMMAND_MAGIC2;
    wire[3] = COMMAND_MAGIC3;
    put_u16(wire + 4, command->version);
    put_u16(wire + 6, FWLAB_NVME_COMMAND_WIRE_BYTES);
    put_u64(wire + 8, command->handle.instance_nonce);
    put_u64(wire + 16, command->handle.command_uid);
    put_u32(wire + 24, command->handle.controller_epoch);
    put_u32(wire + 28, command->handle.generation);
    put_u64(wire + 32, command->origin.word[0]);
    put_u64(wire + 40, command->origin.word[1]);
    put_u64(wire + 48, command->trace_cookie);
    put_u32(wire + 56, command->safety_generation);
    put_u32(wire + 60, command->namespace_id);
    put_u32(wire + 64, command->command_dword2);
    put_u32(wire + 68, command->command_dword3);
    for (index = 0; index < 6; ++index) {
        put_u32(wire + 72 + index * 4, command->command_dword10_15[index]);
    }
    put_u32(wire + 96, command->transport_fault);
    wire[100] = command->opcode;
    wire[101] = command->queue_class;
    wire[102] = command->fuse;
    wire[103] = command->data_pointer_format;
    wire[104] = command->data_address_present;
    wire[105] = command->metadata_address_present;
    wire[106] = command->command_flags_reserved;
    for (index = 0; index < sizeof(wire); ++index) {
        output[index] = wire[index];
    }
    return FWLAB_NVME_CODEC_OK;
}

enum fwlab_nvme_codec_result fwlab_nvme_command_decode(
    const uint8_t *input,
    size_t input_size,
    struct fwlab_nvme_command *command)
{
    struct fwlab_nvme_command local = {0};
    size_t index;

    if (input == NULL || command == NULL ||
        input_size != FWLAB_NVME_COMMAND_WIRE_BYTES) {
        return FWLAB_NVME_CODEC_INVALID_ARGUMENT;
    }
    if (input[0] != COMMAND_MAGIC0 || input[1] != COMMAND_MAGIC1 ||
        input[2] != COMMAND_MAGIC2 || input[3] != COMMAND_MAGIC3 ||
        get_u16(input + 6) != FWLAB_NVME_COMMAND_WIRE_BYTES ||
        !bytes_zero(input + 107, 21)) {
        return FWLAB_NVME_CODEC_BAD_ENCODING;
    }
    if (get_u16(input + 4) != FWLAB_NVME_COMMAND_VERSION) {
        return FWLAB_NVME_CODEC_UNSUPPORTED_VERSION;
    }
    local.version = get_u16(input + 4);
    local.size = sizeof(local);
    local.handle.instance_nonce = get_u64(input + 8);
    local.handle.command_uid = get_u64(input + 16);
    local.handle.controller_epoch = get_u32(input + 24);
    local.handle.generation = get_u32(input + 28);
    local.origin.word[0] = get_u64(input + 32);
    local.origin.word[1] = get_u64(input + 40);
    local.trace_cookie = get_u64(input + 48);
    local.safety_generation = get_u32(input + 56);
    local.namespace_id = get_u32(input + 60);
    local.command_dword2 = get_u32(input + 64);
    local.command_dword3 = get_u32(input + 68);
    for (index = 0; index < 6; ++index) {
        local.command_dword10_15[index] = get_u32(input + 72 + index * 4);
    }
    local.transport_fault = get_u32(input + 96);
    local.opcode = input[100];
    local.queue_class = input[101];
    local.fuse = input[102];
    local.data_pointer_format = input[103];
    local.data_address_present = input[104];
    local.metadata_address_present = input[105];
    local.command_flags_reserved = input[106];
    if (!fwlab_nvme_command_valid(&local)) {
        return FWLAB_NVME_CODEC_INVALID_VALUE;
    }
    *command = local;
    return FWLAB_NVME_CODEC_OK;
}

enum fwlab_nvme_codec_result fwlab_nvme_completion_encode(
    const struct fwlab_nvme_completion_intent *completion,
    uint8_t *output,
    size_t output_size)
{
    uint8_t wire[FWLAB_NVME_COMPLETION_WIRE_BYTES];
    size_t index;

    if (completion == NULL || output == NULL ||
        output_size != FWLAB_NVME_COMPLETION_WIRE_BYTES) {
        return FWLAB_NVME_CODEC_INVALID_ARGUMENT;
    }
    if (!fwlab_nvme_completion_valid(completion)) {
        return FWLAB_NVME_CODEC_INVALID_VALUE;
    }
    clear_bytes(wire, sizeof(wire));
    wire[0] = COMMAND_MAGIC0;
    wire[1] = COMMAND_MAGIC1;
    wire[2] = COMPLETION_MAGIC2;
    wire[3] = COMPLETION_MAGIC3;
    put_u16(wire + 4, completion->version);
    put_u16(wire + 6, FWLAB_NVME_COMPLETION_WIRE_BYTES);
    put_u64(wire + 8, completion->handle.instance_nonce);
    put_u64(wire + 16, completion->handle.command_uid);
    put_u32(wire + 24, completion->handle.controller_epoch);
    put_u32(wire + 28, completion->handle.generation);
    put_u64(wire + 32, completion->origin.word[0]);
    put_u64(wire + 40, completion->origin.word[1]);
    put_u32(wire + 48, completion->result_dword0);
    put_u32(wire + 52, completion->actual_length);
    put_u16(wire + 56, completion->status_code);
    wire[58] = completion->status_code_type;
    wire[59] = completion->command_retry_delay;
    wire[60] = completion->more;
    wire[61] = completion->do_not_retry;
    wire[62] = completion->effect_class;
    for (index = 0; index < sizeof(wire); ++index) {
        output[index] = wire[index];
    }
    return FWLAB_NVME_CODEC_OK;
}

enum fwlab_nvme_codec_result fwlab_nvme_completion_decode(
    const uint8_t *input,
    size_t input_size,
    struct fwlab_nvme_completion_intent *completion)
{
    struct fwlab_nvme_completion_intent local = {0};

    if (input == NULL || completion == NULL ||
        input_size != FWLAB_NVME_COMPLETION_WIRE_BYTES) {
        return FWLAB_NVME_CODEC_INVALID_ARGUMENT;
    }
    if (input[0] != COMMAND_MAGIC0 || input[1] != COMMAND_MAGIC1 ||
        input[2] != COMPLETION_MAGIC2 || input[3] != COMPLETION_MAGIC3 ||
        get_u16(input + 6) != FWLAB_NVME_COMPLETION_WIRE_BYTES ||
        input[63] != 0) {
        return FWLAB_NVME_CODEC_BAD_ENCODING;
    }
    if (get_u16(input + 4) != FWLAB_NVME_COMPLETION_VERSION) {
        return FWLAB_NVME_CODEC_UNSUPPORTED_VERSION;
    }
    local.version = get_u16(input + 4);
    local.size = sizeof(local);
    local.handle.instance_nonce = get_u64(input + 8);
    local.handle.command_uid = get_u64(input + 16);
    local.handle.controller_epoch = get_u32(input + 24);
    local.handle.generation = get_u32(input + 28);
    local.origin.word[0] = get_u64(input + 32);
    local.origin.word[1] = get_u64(input + 40);
    local.result_dword0 = get_u32(input + 48);
    local.actual_length = get_u32(input + 52);
    local.status_code = get_u16(input + 56);
    local.status_code_type = input[58];
    local.command_retry_delay = input[59];
    local.more = input[60];
    local.do_not_retry = input[61];
    local.effect_class = input[62];
    if (!fwlab_nvme_completion_valid(&local)) {
        return FWLAB_NVME_CODEC_INVALID_VALUE;
    }
    *completion = local;
    return FWLAB_NVME_CODEC_OK;
}

enum fwlab_nvme_codec_result fwlab_nvme_profile_encode(
    const struct fwlab_nvme_profile *profile,
    uint8_t *output,
    size_t output_size)
{
    uint8_t wire[FWLAB_NVME_PROFILE_WIRE_BYTES];
    size_t index;

    if (profile == NULL || output == NULL ||
        output_size != FWLAB_NVME_PROFILE_WIRE_BYTES) {
        return FWLAB_NVME_CODEC_INVALID_ARGUMENT;
    }
    if (!fwlab_nvme_profile_valid(profile)) {
        return FWLAB_NVME_CODEC_INVALID_VALUE;
    }
    clear_bytes(wire, sizeof(wire));
    wire[0] = COMMAND_MAGIC0;
    wire[1] = COMMAND_MAGIC1;
    wire[2] = PROFILE_MAGIC2;
    wire[3] = PROFILE_MAGIC3;
    put_u16(wire + 4, profile->version);
    put_u16(wire + 6, FWLAB_NVME_PROFILE_WIRE_BYTES);
    put_u32(wire + 8, profile->namespace_count);
    put_u32(wire + 12, profile->lba_bytes);
    put_u32(wire + 16, profile->lba_count);
    put_u32(wire + 20, profile->memory_page_bytes);
    put_u32(wire + 24, profile->maximum_transfer_bytes);
    put_u16(wire + 28, profile->maximum_io_queue_pairs);
    put_u16(wire + 30, profile->integration_queue_depth);
    put_u16(wire + 32, profile->queue_depth_hard_maximum);
    put_u16(wire + 34, profile->data_segments_hard_maximum);
    put_u32(wire + 36, profile->feature_flags);
    for (index = 0; index < sizeof(wire); ++index) {
        output[index] = wire[index];
    }
    return FWLAB_NVME_CODEC_OK;
}

enum fwlab_nvme_codec_result fwlab_nvme_profile_decode(
    const uint8_t *input,
    size_t input_size,
    struct fwlab_nvme_profile *profile)
{
    struct fwlab_nvme_profile local = {0};

    if (input == NULL || profile == NULL ||
        input_size != FWLAB_NVME_PROFILE_WIRE_BYTES) {
        return FWLAB_NVME_CODEC_INVALID_ARGUMENT;
    }
    if (input[0] != COMMAND_MAGIC0 || input[1] != COMMAND_MAGIC1 ||
        input[2] != PROFILE_MAGIC2 || input[3] != PROFILE_MAGIC3 ||
        get_u16(input + 6) != FWLAB_NVME_PROFILE_WIRE_BYTES ||
        !bytes_zero(input + 40, 24)) {
        return FWLAB_NVME_CODEC_BAD_ENCODING;
    }
    if (get_u16(input + 4) != FWLAB_NVME_PROFILE_VERSION) {
        return FWLAB_NVME_CODEC_UNSUPPORTED_VERSION;
    }
    local.version = get_u16(input + 4);
    local.size = sizeof(local);
    local.namespace_count = get_u32(input + 8);
    local.lba_bytes = get_u32(input + 12);
    local.lba_count = get_u32(input + 16);
    local.memory_page_bytes = get_u32(input + 20);
    local.maximum_transfer_bytes = get_u32(input + 24);
    local.maximum_io_queue_pairs = get_u16(input + 28);
    local.integration_queue_depth = get_u16(input + 30);
    local.queue_depth_hard_maximum = get_u16(input + 32);
    local.data_segments_hard_maximum = get_u16(input + 34);
    local.feature_flags = get_u32(input + 36);
    if (!fwlab_nvme_profile_valid(&local)) {
        return FWLAB_NVME_CODEC_INVALID_VALUE;
    }
    *profile = local;
    return FWLAB_NVME_CODEC_OK;
}
