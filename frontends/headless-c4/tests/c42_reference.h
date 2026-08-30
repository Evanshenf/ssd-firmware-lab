/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C42_REFERENCE_H
#define FWLAB_C42_REFERENCE_H

#include <stddef.h>
#include <stdint.h>

#define C42_REFERENCE_CQE_BYTES 16u

void c42_reference_build_cqe(
    uint32_t result_dword0,
    uint16_t submission_queue_head,
    uint16_t submission_queue_id,
    uint16_t command_id,
    uint8_t phase,
    uint8_t status_code,
    uint8_t status_code_type,
    uint8_t command_retry_delay,
    uint8_t more,
    uint8_t do_not_retry,
    uint8_t output[C42_REFERENCE_CQE_BYTES]
);
int c42_reference_bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size
);

#endif
