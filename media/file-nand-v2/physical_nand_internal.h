/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_PHYSICAL_NAND_V2_INTERNAL_H
#define FWLAB_PHYSICAL_NAND_V2_INTERNAL_H

#include "physical_nand.h"

#define FNV2_SUPER_BYTES 4096u
#define FNV2_INTENT_BYTES 1024u
#define FNV2_TERMINAL_BYTES 512u
#define FNV2_PAGE_RECORD_BYTES 256u
#define FNV2_BLOCK_RECORD_BYTES 64u
#define FNV2_BANK_BASE UINT64_C(8192)
#define FNV2_BANK_BYTES UINT64_C(4096)
#define FNV2_HOME_BASE UINT64_C(16384)

/* Private byte substrate for this versioned media engine, not an FTL API.
 * Complete IO or failure; successful sync orders/persists preceding writes.
 * Failed/short IO may affect its requested range, not unrelated bytes.
 * New size-zero file extension yields zero bytes. No raw-power-loss claim. */
struct fnv2_io {
    void *context;
    enum fwlab_nfc_api_result (*read)(void *, uint64_t, void *, size_t);
    enum fwlab_nfc_api_result (*write)(void *, uint64_t, const void *, size_t);
    enum fwlab_nfc_api_result (*sync)(void *);
    enum fwlab_nfc_api_result (*resize)(void *, uint64_t);
    enum fwlab_nfc_api_result (*size)(void *, uint64_t *);
    enum fwlab_nfc_api_result (*close)(void *);
};

struct fwlab_file_nand_v2 {
    uint64_t magic;
    struct fwlab_file_nand_v2_config config;
    struct fnv2_io io;
    _Alignas(max_align_t) uint8_t io_storage[128];
    uint64_t image_bytes;
    uint64_t page_metadata_offset;
    uint64_t block_metadata_offset;
    uint64_t sequence;
    uint32_t pages;
    uint32_t blocks;
    uint8_t quarantined;
    uint8_t closed;
    uint8_t busy;
    uint8_t intent[FNV2_INTENT_BYTES];
    uint8_t terminal[FNV2_TERMINAL_BYTES];
    uint8_t page_records[FWLAB_FILE_NAND_V2_MAX_BATCH_PAGES][FNV2_PAGE_RECORD_BYTES];
    uint8_t work[FNV2_SUPER_BYTES];
    uint8_t scratch_main[4096];
    uint8_t scratch_oob[128];
};

enum fwlab_nfc_api_result fnv2_engine_open(
    void *arena, size_t arena_size,
    const struct fwlab_file_nand_v2_config *config,
    const struct fnv2_io *io, int format,
    struct fwlab_file_nand_v2 **media);

#endif
