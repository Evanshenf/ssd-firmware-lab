/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_memory_media.h"

#include <stdio.h>

int main(void)
{
    struct c34_memory_media media;
    struct fwlab_nand_media provider;

    c34_memory_media_init(&media);
    provider = c34_memory_media_provider(&media);
    if (provider.ops == NULL || provider.context != &media) {
        return 1;
    }
    puts("C3.4 firmware fake link: PASS");
    return 0;
}
