/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C34_FAKE_BUFFER_H
#define FWLAB_C34_FAKE_BUFFER_H

#include <stdint.h>

#include "fwlab/contracts/nfc_provider.h"

#define C34_FAKE_BUFFER_BYTES 512u

struct c34_fake_buffer {
    uint32_t region;
    uint8_t bytes[C34_FAKE_BUFFER_BYTES];
};

void c34_fake_buffer_init(struct c34_fake_buffer *buffer, uint32_t region);
struct fwlab_nfc_buffer_provider c34_fake_buffer_provider(
    struct c34_fake_buffer *buffer
);

#endif
