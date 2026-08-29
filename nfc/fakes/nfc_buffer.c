/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "nfc_buffer.h"

#include <string.h>

static const struct c33_fake_buffer_region *find_region(
    const struct c33_fake_buffer *buffer,
    uint32_t identifier
)
{
    uint32_t index;

    for (index = 0; index < buffer->region_count; ++index) {
        if (buffer->region[index].identifier == identifier) {
            return &buffer->region[index];
        }
    }
    return NULL;
}

static enum fwlab_nfc_api_result fake_read(
    void *opaque,
    const struct fwlab_nfc_buffer_ref *source,
    uint8_t *destination,
    uint32_t length
)
{
    struct c33_fake_buffer *buffer = opaque;
    const struct c33_fake_buffer_region *region;
    uint64_t end;

    if (buffer == NULL || source == NULL || destination == NULL ||
        source->reserved != 0 || length != source->length) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    region = find_region(buffer, source->controller_region);
    end = (uint64_t)source->offset + length;
    if (region == NULL || end > region->length ||
        (uint64_t)region->base + end > C33_FAKE_BUFFER_BYTES) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memcpy(destination, &buffer->bytes[region->base + source->offset],
           length);
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result fake_write(
    void *opaque,
    const struct fwlab_nfc_buffer_ref *destination,
    const uint8_t *source,
    uint32_t length
)
{
    struct c33_fake_buffer *buffer = opaque;
    const struct c33_fake_buffer_region *region;
    uint64_t end;

    if (buffer == NULL || destination == NULL || source == NULL ||
        destination->reserved != 0 || length != destination->length) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    region = find_region(buffer, destination->controller_region);
    end = (uint64_t)destination->offset + length;
    if (region == NULL || end > region->length ||
        (uint64_t)region->base + end > C33_FAKE_BUFFER_BYTES) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memcpy(&buffer->bytes[region->base + destination->offset], source,
           length);
    return FWLAB_NFC_API_OK;
}

static const struct fwlab_nfc_buffer_ops fake_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_nfc_buffer_ops),
    .reserved = 0,
    .read = fake_read,
    .write = fake_write,
};

void c33_fake_buffer_init(struct c33_fake_buffer *buffer)
{
    if (buffer != NULL) {
        memset(buffer, 0, sizeof(*buffer));
    }
}

int c33_fake_buffer_add(
    struct c33_fake_buffer *buffer,
    uint32_t identifier,
    uint32_t base,
    uint32_t length
)
{
    uint32_t index;

    if (buffer == NULL || identifier == 0 || length == 0 ||
        buffer->region_count >= C33_FAKE_BUFFER_REGIONS ||
        (uint64_t)base + length > C33_FAKE_BUFFER_BYTES) {
        return 0;
    }
    for (index = 0; index < buffer->region_count; ++index) {
        if (buffer->region[index].identifier == identifier) {
            return 0;
        }
    }
    buffer->region[buffer->region_count].identifier = identifier;
    buffer->region[buffer->region_count].base = base;
    buffer->region[buffer->region_count].length = length;
    ++buffer->region_count;
    return 1;
}

struct fwlab_nfc_buffer_provider c33_fake_buffer_provider(
    struct c33_fake_buffer *buffer
)
{
    struct fwlab_nfc_buffer_provider provider;

    provider.ops = &fake_ops;
    provider.context = buffer;
    return provider;
}
