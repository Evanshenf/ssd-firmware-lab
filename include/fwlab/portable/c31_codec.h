/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PORTABLE_C31_CODEC_H
#define FWLAB_PORTABLE_C31_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/portable/c31_types.h"

#define FWLAB_C31_DESCRIPTOR_WIRE_SIZE 96u

enum fwlab_c31_api_result fwlab_c31_descriptor_encode(
    const struct fwlab_c31_command_descriptor *descriptor,
    uint8_t *wire,
    size_t wire_size
);

enum fwlab_c31_api_result fwlab_c31_descriptor_decode(
    const uint8_t *wire,
    size_t wire_size,
    struct fwlab_c31_command_descriptor *descriptor
);

#endif
