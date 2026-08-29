/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CONTRACTS_NAND_MEDIA_H
#define FWLAB_CONTRACTS_NAND_MEDIA_H

#include <stdint.h>

#include "fwlab/portable/nfc_types.h"

enum fwlab_nand_page_state {
    FWLAB_NAND_PAGE_ERASED = 0,
    FWLAB_NAND_PAGE_VALID = 1,
    FWLAB_NAND_PAGE_TORN = 2
};

enum fwlab_nand_erase_state {
    FWLAB_NAND_ERASE_CLEAN = 0,
    FWLAB_NAND_ERASE_TORN = 1
};

struct fwlab_nand_page_info {
    uint16_t version;
    uint16_t size;
    uint16_t erase_generation_seen;
    uint8_t state;
    uint8_t program_count;
    uint32_t reserved[2];
};

struct fwlab_nand_block_info {
    uint16_t version;
    uint16_t size;
    uint16_t erase_generation;
    uint16_t successful_erase_count;
    uint16_t erase_attempt_count;
    uint16_t next_program_page;
    uint8_t health;
    uint8_t erase_state;
    uint16_t reserved0;
    uint32_t reserved1[2];
};

struct fwlab_nand_media_result {
    uint16_t version;
    uint16_t size;
    uint8_t physical_outcome;
    uint8_t integrity;
    uint8_t reason;
    uint8_t block_health;
    uint8_t applied_region_mask;
    uint8_t reserved0[3];
    uint16_t base_erase_generation;
    uint16_t final_erase_generation;
    uint32_t applied_main_bytes;
    uint32_t applied_oob_bytes;
    uint32_t applied_pages;
    uint32_t reserved1;
};

typedef enum fwlab_nfc_api_result
(*fwlab_nand_media_read_page_fn)(
    void *context,
    const struct fwlab_nfc_ppa *ppa,
    uint8_t *main,
    uint32_t main_length,
    uint8_t *oob,
    uint32_t oob_length,
    struct fwlab_nand_page_info *page,
    struct fwlab_nand_block_info *block
);

typedef enum fwlab_nfc_api_result
(*fwlab_nand_media_program_fn)(
    void *context,
    const struct fwlab_nfc_ppa *ppa,
    const uint8_t *main,
    uint32_t main_length,
    const uint8_t *oob,
    uint32_t oob_length,
    uint32_t applied_main_bytes,
    uint32_t applied_oob_bytes,
    uint8_t integrity,
    struct fwlab_nand_media_result *result
);

typedef enum fwlab_nfc_api_result
(*fwlab_nand_media_erase_fn)(
    void *context,
    const struct fwlab_nfc_ppa *block,
    uint32_t applied_pages,
    uint8_t integrity,
    struct fwlab_nand_media_result *result
);

typedef enum fwlab_nfc_api_result
(*fwlab_nand_media_mark_runtime_bad_fn)(
    void *context,
    const struct fwlab_nfc_ppa *block
);

typedef uint64_t (*fwlab_nand_media_hash_fn)(void *context);

struct fwlab_nand_media_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    fwlab_nand_media_read_page_fn read_page;
    fwlab_nand_media_program_fn program;
    fwlab_nand_media_erase_fn erase;
    fwlab_nand_media_mark_runtime_bad_fn mark_runtime_bad;
    fwlab_nand_media_hash_fn hash;
};

struct fwlab_nand_media {
    const struct fwlab_nand_media_ops *ops;
    void *context;
};

#endif
