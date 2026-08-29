/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C34_FILE_TEST_SUPPORT_H
#define FWLAB_C34_FILE_TEST_SUPPORT_H

#include "../c34_file_internal.h"

#include <stdalign.h>

union c34f_test_arena {
    max_align_t alignment;
    uint8_t bytes[131072];
};

struct c34f_memory_substrate {
    uint8_t working[C34_FILE_IMAGE_BYTES];
    uint8_t stable[C34_FILE_IMAGE_BYTES];
    uint64_t size;
    uint32_t barriers;
    uint32_t cut_after_barrier;
};

void c34f_memory_substrate_init(struct c34f_memory_substrate *substrate);
struct c34_file_substrate c34f_memory_substrate_provider(
    struct c34f_memory_substrate *substrate
);
void c34f_memory_substrate_restart_image(
    const struct c34f_memory_substrate *source,
    struct c34f_memory_substrate *restart
);

uint64_t c34f_test_payload_digest(
    const uint8_t main[C34F_MAIN_BYTES],
    const uint8_t oob[C34F_OOB_BYTES]
);

int c34f_test_bind_program(
    struct c34_file_media *media,
    uint64_t identity,
    struct fwlab_nfc_ppa ppa,
    const uint8_t main[C34F_MAIN_BYTES],
    const uint8_t oob[C34F_OOB_BYTES],
    struct fwlab_nfc_operation_token *inner
);
int c34f_test_program_page(
    struct c34_file_media *media,
    uint64_t identity,
    uint8_t fill,
    struct c34_physical_receipt *receipt
);

#endif
