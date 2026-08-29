/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_NFC_FAKES_MEMORY_MEDIA_H
#define FWLAB_NFC_FAKES_MEMORY_MEDIA_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/contracts/nand_media.h"

struct c33_memory_page {
    uint8_t state;
    uint8_t program_count;
    uint16_t erase_generation_seen;
};

struct c33_memory_block {
    uint8_t health;
    uint8_t erase_state;
    uint16_t erase_generation;
    uint16_t successful_erase_count;
    uint16_t erase_attempt_count;
    uint16_t next_program_page;
    uint16_t reserved;
};

struct c33_memory_media {
    struct fwlab_nfc_geometry geometry;
    uint32_t page_count;
    uint32_t block_count;
    uint8_t *main;
    uint8_t *oob;
    struct c33_memory_page *page;
    struct c33_memory_block *block;
};

size_t c33_memory_media_arena_size(
    const struct fwlab_nfc_geometry *geometry
);

enum fwlab_nfc_api_result c33_memory_media_init(
    void *arena,
    size_t arena_size,
    const struct fwlab_nfc_geometry *geometry,
    const struct fwlab_nfc_factory_bad *factory_bad,
    size_t factory_bad_count,
    struct c33_memory_media **media
);

struct fwlab_nand_media c33_memory_media_provider(
    struct c33_memory_media *media
);

#endif
