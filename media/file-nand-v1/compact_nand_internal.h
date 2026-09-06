/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_COMPACT_NAND_INTERNAL_H
#define FWLAB_COMPACT_NAND_INTERNAL_H

#include "compact_nand.h"

#define FNV1_SECTOR 4096u
#define FNV1_BANK_BYTES UINT64_C(32768)
#define FNV1_BANK_BASE UINT64_C(16384)
#define FNV1_HOME_BASE UINT64_C(81920)
#define FNV1_MAX_POSTIMAGES 3u

/* Private byte substrate, not an FTL service. Implementations must finish
 * complete I/O or fail, and sync must order/durably persist preceding writes.
 * Extending a newly created size-zero file must provide zero-filled bytes. */
struct fnv1_io {
    void *context;
    enum fwlab_nfc_api_result (*read)(void *, uint64_t, void *, size_t);
    enum fwlab_nfc_api_result (*write)(void *, uint64_t, const void *, size_t);
    enum fwlab_nfc_api_result (*sync)(void *);
    enum fwlab_nfc_api_result (*resize)(void *, uint64_t);
    enum fwlab_nfc_api_result (*size)(void *, uint64_t *);
    enum fwlab_nfc_api_result (*close)(void *);
};

struct fwlab_file_nand_v1 {
    uint64_t magic;
    struct fwlab_file_nand_v1_config config;
    struct fnv1_io io;
    _Alignas(max_align_t) uint8_t io_storage[128];
    uint64_t image_bytes;
    uint64_t page_metadata_offset;
    uint64_t block_metadata_offset;
    uint64_t sequence;
    uint64_t redo_target[FNV1_MAX_POSTIMAGES];
    uint32_t pages;
    uint32_t blocks;
    uint32_t redo_count;
    uint8_t quarantined;
    uint8_t closed;
    uint8_t redo[FNV1_MAX_POSTIMAGES][FNV1_SECTOR];
    uint8_t work[FNV1_SECTOR];
};

/* Private construction seam for POSIX and small durable-byte tests. Format
 * requires size zero. Recovery requires exact format/UUID/layout/length. */
enum fwlab_nfc_api_result fnv1_engine_open(
    void *arena, size_t arena_size,
    const struct fwlab_file_nand_v1_config *config,
    const struct fnv1_io *io, int format,
    struct fwlab_file_nand_v1 **media);

#endif
