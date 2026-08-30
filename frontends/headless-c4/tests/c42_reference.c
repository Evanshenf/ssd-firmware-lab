/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_reference.h"

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
    uint8_t output[C42_REFERENCE_CQE_BYTES])
{
    uint16_t status;
    size_t index;

    for (index = 0; index < C42_REFERENCE_CQE_BYTES; ++index) {
        output[index] = 0;
    }
    status = (uint16_t)(phase & 1u) |
             (uint16_t)((uint16_t)status_code << 1) |
             (uint16_t)((uint16_t)(status_code_type & 7u) << 9) |
             (uint16_t)((uint16_t)(command_retry_delay & 3u) << 12) |
             (uint16_t)((uint16_t)(more & 1u) << 14) |
             (uint16_t)((uint16_t)(do_not_retry & 1u) << 15);
    put_u32(output, result_dword0);
    put_u16(output + 8, submission_queue_head);
    put_u16(output + 10, submission_queue_id);
    put_u16(output + 12, command_id);
    put_u16(output + 14, status);
}

int c42_reference_bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size)
{
    size_t index;

    if (left == NULL || right == NULL) {
        return 0;
    }
    for (index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}
