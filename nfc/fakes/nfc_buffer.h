/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_NFC_FAKES_BUFFER_H
#define FWLAB_NFC_FAKES_BUFFER_H

#include <stdint.h>

#include "fwlab/contracts/nfc_provider.h"

#define C33_FAKE_BUFFER_REGIONS 4u
#define C33_FAKE_BUFFER_BYTES 4096u

struct c33_fake_buffer_region {
    uint32_t identifier;
    uint32_t base;
    uint32_t length;
};

struct c33_fake_buffer {
    struct c33_fake_buffer_region region[C33_FAKE_BUFFER_REGIONS];
    uint8_t bytes[C33_FAKE_BUFFER_BYTES];
    uint32_t region_count;
};

void c33_fake_buffer_init(struct c33_fake_buffer *buffer);

int c33_fake_buffer_add(
    struct c33_fake_buffer *buffer,
    uint32_t identifier,
    uint32_t base,
    uint32_t length
);

struct fwlab_nfc_buffer_provider c33_fake_buffer_provider(
    struct c33_fake_buffer *buffer
);

#endif
