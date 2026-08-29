/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_buffer.h"

#include <string.h>

static enum fwlab_nfc_api_result buffer_read(
    void *context,
    const struct fwlab_nfc_buffer_ref *source,
    uint8_t *destination,
    uint32_t length
)
{
    struct c34_fake_buffer *buffer = context;

    if (buffer == NULL || source == NULL || destination == NULL ||
        source->reserved != 0 || source->controller_region != buffer->region ||
        source->length != length || source->offset > C34_FAKE_BUFFER_BYTES ||
        length > C34_FAKE_BUFFER_BYTES - source->offset) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memcpy(destination, &buffer->bytes[source->offset], length);
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result buffer_write(
    void *context,
    const struct fwlab_nfc_buffer_ref *destination,
    const uint8_t *source,
    uint32_t length
)
{
    struct c34_fake_buffer *buffer = context;

    if (buffer == NULL || destination == NULL || source == NULL ||
        destination->reserved != 0 ||
        destination->controller_region != buffer->region ||
        destination->length != length ||
        destination->offset > C34_FAKE_BUFFER_BYTES ||
        length > C34_FAKE_BUFFER_BYTES - destination->offset) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memcpy(&buffer->bytes[destination->offset], source, length);
    return FWLAB_NFC_API_OK;
}

static const struct fwlab_nfc_buffer_ops buffer_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_nfc_buffer_ops),
    .reserved = 0,
    .read = buffer_read,
    .write = buffer_write,
};

void c34_fake_buffer_init(struct c34_fake_buffer *buffer, uint32_t region)
{
    memset(buffer, 0, sizeof(*buffer));
    buffer->region = region;
}

struct fwlab_nfc_buffer_provider c34_fake_buffer_provider(
    struct c34_fake_buffer *buffer
)
{
    struct fwlab_nfc_buffer_provider provider;

    provider.ops = &buffer_ops;
    provider.context = buffer;
    return provider;
}
