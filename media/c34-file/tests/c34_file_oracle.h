/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C34_FILE_ORACLE_H
#define FWLAB_C34_FILE_ORACLE_H

#include <stdint.h>

#include "../c34_file_internal.h"

struct c34fo_page0 {
    uint8_t state;
    uint8_t program_count;
    uint16_t erase_generation;
    uint8_t main[C34F_MAIN_BYTES];
    uint8_t oob[C34F_OOB_BYTES];
    uint64_t hash;
};

int c34fo_recover_page0(
    const uint8_t image[C34_FILE_IMAGE_BYTES],
    const uint8_t uuid[16],
    struct c34fo_page0 *page
);

#endif
