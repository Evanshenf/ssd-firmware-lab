/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PORTABLE_NFC_CODEC_H
#define FWLAB_PORTABLE_NFC_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/portable/nfc_types.h"

#define FWLAB_NFC_REQUEST_WIRE_SIZE 160u
#define FWLAB_NFC_COMPLETION_WIRE_SIZE 256u
#define FWLAB_NFC_TRACE_WIRE_SIZE 112u

enum fwlab_nfc_api_result fwlab_nfc_request_encode(
    const struct fwlab_nfc_request *request,
    uint8_t *wire,
    size_t wire_size
);

enum fwlab_nfc_api_result fwlab_nfc_request_decode(
    const uint8_t *wire,
    size_t wire_size,
    struct fwlab_nfc_request *request
);

enum fwlab_nfc_api_result fwlab_nfc_completion_encode(
    const struct fwlab_nfc_completion *completion,
    uint8_t *wire,
    size_t wire_size
);

enum fwlab_nfc_api_result fwlab_nfc_completion_decode(
    const uint8_t *wire,
    size_t wire_size,
    struct fwlab_nfc_completion *completion
);

enum fwlab_nfc_api_result fwlab_nfc_trace_encode(
    const struct fwlab_nfc_trace_entry *entry,
    uint8_t *wire,
    size_t wire_size
);

enum fwlab_nfc_api_result fwlab_nfc_trace_decode(
    const uint8_t *wire,
    size_t wire_size,
    struct fwlab_nfc_trace_entry *entry
);

#endif
