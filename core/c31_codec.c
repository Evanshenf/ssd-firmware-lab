/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fwlab/portable/c31_codec.h"

#include <string.h>

enum {
    C31_WIRE_MAGIC = 0,
    C31_WIRE_VERSION = 4,
    C31_WIRE_SIZE = 6,
    C31_WIRE_RESERVED0 = 8,
    C31_WIRE_PROVIDER = 12,
    C31_WIRE_DIRECTION = 13,
    C31_WIRE_ORDERING = 14,
    C31_WIRE_ORIGIN0 = 16,
    C31_WIRE_ORIGIN1 = 24,
    C31_WIRE_TRACE = 32,
    C31_WIRE_REQUEST0 = 40,
    C31_WIRE_REQUEST1 = 48,
    C31_WIRE_CAPABILITY0 = 56,
    C31_WIRE_CAPABILITY1 = 64,
    C31_WIRE_CAPABILITY_OFFSET = 72,
    C31_WIRE_CONTROLLER_REGION = 76,
    C31_WIRE_CONTROLLER_OFFSET = 80,
    C31_WIRE_LENGTH = 84,
    C31_WIRE_RESERVED1 = 88
};

static void put_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & UINT16_C(0xff));
    destination[1] = (uint8_t)(value >> 8);
}

static void put_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & UINT32_C(0xff));
    destination[1] = (uint8_t)((value >> 8) & UINT32_C(0xff));
    destination[2] = (uint8_t)((value >> 16) & UINT32_C(0xff));
    destination[3] = (uint8_t)(value >> 24);
}

static void put_u64_le(uint8_t *destination, uint64_t value)
{
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        destination[index] = (uint8_t)(value >> (index * 8));
    }
}

static uint16_t get_u16_le(const uint8_t *source)
{
    return (uint16_t)((uint16_t)source[0] |
                      (uint16_t)((uint16_t)source[1] << 8));
}

static uint32_t get_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static uint64_t get_u64_le(const uint8_t *source)
{
    uint64_t value = 0;
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)source[index] << (index * 8);
    }
    return value;
}

static int token_is_zero(const uint64_t words[2])
{
    return words[0] == 0 && words[1] == 0;
}

static enum fwlab_c31_api_result validate_descriptor(
    const struct fwlab_c31_command_descriptor *descriptor
)
{
    if (descriptor == NULL) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (descriptor->version != FWLAB_C31_CONTRACT_VERSION) {
        return FWLAB_C31_API_UNSUPPORTED_VERSION;
    }
    if (descriptor->size != sizeof(*descriptor)) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (descriptor->reserved0 != 0 || descriptor->reserved1[0] != 0 ||
        descriptor->reserved1[1] != 0 || descriptor->ordering_flags != 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (token_is_zero(descriptor->origin.word)) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    switch (descriptor->provider_kind) {
    case FWLAB_C31_PROVIDER_NONE:
        if (descriptor->dma_direction != FWLAB_C31_DMA_NONE ||
            !token_is_zero(descriptor->provider_request.word) ||
            !token_is_zero(descriptor->capability.word) ||
            descriptor->capability_offset != 0 ||
            descriptor->controller_region != 0 ||
            descriptor->controller_offset != 0 || descriptor->length != 0) {
            return FWLAB_C31_API_INVALID_CONTRACT;
        }
        break;
    case FWLAB_C31_PROVIDER_DMA:
        if ((descriptor->dma_direction != FWLAB_C31_DMA_TO_CONTROLLER &&
             descriptor->dma_direction != FWLAB_C31_DMA_FROM_CONTROLLER) ||
            token_is_zero(descriptor->provider_request.word) ||
            token_is_zero(descriptor->capability.word) ||
            descriptor->length == 0) {
            return FWLAB_C31_API_INVALID_CONTRACT;
        }
        break;
    case FWLAB_C31_PROVIDER_NFC:
        if (descriptor->dma_direction != FWLAB_C31_DMA_NONE ||
            token_is_zero(descriptor->provider_request.word) ||
            !token_is_zero(descriptor->capability.word) ||
            descriptor->capability_offset != 0 ||
            descriptor->controller_region != 0 ||
            descriptor->controller_offset != 0 || descriptor->length != 0) {
            return FWLAB_C31_API_INVALID_CONTRACT;
        }
        break;
    default:
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    return FWLAB_C31_API_OK;
}

enum fwlab_c31_api_result fwlab_c31_descriptor_encode(
    const struct fwlab_c31_command_descriptor *descriptor,
    uint8_t *wire,
    size_t wire_size
)
{
    enum fwlab_c31_api_result result = validate_descriptor(descriptor);

    if (result != FWLAB_C31_API_OK) {
        return result;
    }
    if (wire == NULL || wire_size != FWLAB_C31_DESCRIPTOR_WIRE_SIZE) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    memset(wire, 0, wire_size);
    wire[C31_WIRE_MAGIC + 0] = (uint8_t)'C';
    wire[C31_WIRE_MAGIC + 1] = (uint8_t)'3';
    wire[C31_WIRE_MAGIC + 2] = (uint8_t)'1';
    wire[C31_WIRE_MAGIC + 3] = (uint8_t)'D';
    put_u16_le(wire + C31_WIRE_VERSION, descriptor->version);
    put_u16_le(wire + C31_WIRE_SIZE, FWLAB_C31_DESCRIPTOR_WIRE_SIZE);
    put_u32_le(wire + C31_WIRE_RESERVED0, 0);
    wire[C31_WIRE_PROVIDER] = descriptor->provider_kind;
    wire[C31_WIRE_DIRECTION] = descriptor->dma_direction;
    put_u16_le(wire + C31_WIRE_ORDERING, descriptor->ordering_flags);
    put_u64_le(wire + C31_WIRE_ORIGIN0, descriptor->origin.word[0]);
    put_u64_le(wire + C31_WIRE_ORIGIN1, descriptor->origin.word[1]);
    put_u64_le(wire + C31_WIRE_TRACE, descriptor->trace_cookie);
    put_u64_le(wire + C31_WIRE_REQUEST0,
               descriptor->provider_request.word[0]);
    put_u64_le(wire + C31_WIRE_REQUEST1,
               descriptor->provider_request.word[1]);
    put_u64_le(wire + C31_WIRE_CAPABILITY0,
               descriptor->capability.word[0]);
    put_u64_le(wire + C31_WIRE_CAPABILITY1,
               descriptor->capability.word[1]);
    put_u32_le(wire + C31_WIRE_CAPABILITY_OFFSET,
               descriptor->capability_offset);
    put_u32_le(wire + C31_WIRE_CONTROLLER_REGION,
               descriptor->controller_region);
    put_u32_le(wire + C31_WIRE_CONTROLLER_OFFSET,
               descriptor->controller_offset);
    put_u32_le(wire + C31_WIRE_LENGTH, descriptor->length);
    return FWLAB_C31_API_OK;
}

enum fwlab_c31_api_result fwlab_c31_descriptor_decode(
    const uint8_t *wire,
    size_t wire_size,
    struct fwlab_c31_command_descriptor *descriptor
)
{
    unsigned int index;

    if (wire == NULL || descriptor == NULL ||
        wire_size != FWLAB_C31_DESCRIPTOR_WIRE_SIZE) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (wire[C31_WIRE_MAGIC + 0] != (uint8_t)'C' ||
        wire[C31_WIRE_MAGIC + 1] != (uint8_t)'3' ||
        wire[C31_WIRE_MAGIC + 2] != (uint8_t)'1' ||
        wire[C31_WIRE_MAGIC + 3] != (uint8_t)'D') {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (get_u16_le(wire + C31_WIRE_VERSION) !=
        FWLAB_C31_CONTRACT_VERSION) {
        return FWLAB_C31_API_UNSUPPORTED_VERSION;
    }
    if (get_u16_le(wire + C31_WIRE_SIZE) !=
        FWLAB_C31_DESCRIPTOR_WIRE_SIZE ||
        get_u32_le(wire + C31_WIRE_RESERVED0) != 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    for (index = C31_WIRE_RESERVED1;
         index < FWLAB_C31_DESCRIPTOR_WIRE_SIZE; ++index) {
        if (wire[index] != 0) {
            return FWLAB_C31_API_INVALID_CONTRACT;
        }
    }

    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->version = get_u16_le(wire + C31_WIRE_VERSION);
    descriptor->size = (uint16_t)sizeof(*descriptor);
    descriptor->provider_kind = wire[C31_WIRE_PROVIDER];
    descriptor->dma_direction = wire[C31_WIRE_DIRECTION];
    descriptor->ordering_flags = get_u16_le(wire + C31_WIRE_ORDERING);
    descriptor->origin.word[0] = get_u64_le(wire + C31_WIRE_ORIGIN0);
    descriptor->origin.word[1] = get_u64_le(wire + C31_WIRE_ORIGIN1);
    descriptor->trace_cookie = get_u64_le(wire + C31_WIRE_TRACE);
    descriptor->provider_request.word[0] =
        get_u64_le(wire + C31_WIRE_REQUEST0);
    descriptor->provider_request.word[1] =
        get_u64_le(wire + C31_WIRE_REQUEST1);
    descriptor->capability.word[0] =
        get_u64_le(wire + C31_WIRE_CAPABILITY0);
    descriptor->capability.word[1] =
        get_u64_le(wire + C31_WIRE_CAPABILITY1);
    descriptor->capability_offset =
        get_u32_le(wire + C31_WIRE_CAPABILITY_OFFSET);
    descriptor->controller_region =
        get_u32_le(wire + C31_WIRE_CONTROLLER_REGION);
    descriptor->controller_offset =
        get_u32_le(wire + C31_WIRE_CONTROLLER_OFFSET);
    descriptor->length = get_u32_le(wire + C31_WIRE_LENGTH);
    return validate_descriptor(descriptor);
}
