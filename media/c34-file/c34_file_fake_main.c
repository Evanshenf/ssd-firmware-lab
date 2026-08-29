/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_file_media.h"

#include "tests/c34_file_test_support.h"

#include <stdio.h>

int main(void)
{
    static const uint8_t uuid[16] = {0x34, 0x04, 0x01};
    struct c34f_memory_substrate substrate;
    struct c34_file_substrate provider;
    union c34f_test_arena arena;
    struct c34_file_media *media;

    c34f_memory_substrate_init(&substrate);
    provider = c34f_memory_substrate_provider(&substrate);
    if (c34_file_format(arena.bytes, sizeof(arena.bytes), &provider, uuid,
                        &media) != C34_FILE_OK ||
        c34_file_image_hash(media) == 0) {
        return 1;
    }
    puts("C3.4 file-media fake link: PASS");
    return 0;
}
