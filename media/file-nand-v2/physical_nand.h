/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_PHYSICAL_NAND_V2_H
#define FWLAB_PHYSICAL_NAND_V2_H

#include <stddef.h>
#include <stdint.h>
#include "fwlab/contracts/nand_media.h"

#define FWLAB_FILE_NAND_V2_MAX_BATCH_PAGES 64u

struct fwlab_file_nand_v2;
struct fwlab_file_nand_v2_config {
    struct fwlab_nfc_geometry geometry;
    uint8_t media_uuid[16];
};
struct fwlab_file_nand_holder_v2 {
    uint64_t device;
    uint64_t inode;
    uint8_t media_uuid[16];
};

size_t fwlab_file_nand_v2_arena_alignment(void);
size_t fwlab_file_nand_v2_arena_size(void);
uint64_t fwlab_file_nand_v2_image_bytes(
    const struct fwlab_file_nand_v2_config *config);

/* New private regular file only; recovery never creates/resizes/converts.
 * Both acquire exclusive OFD ownership. No raw-device binding is implied. */
enum fwlab_nfc_api_result fwlab_file_nand_v2_posix_format(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_v2_config *config,
    struct fwlab_file_nand_v2 **media,
    struct fwlab_file_nand_holder_v2 *holder);
enum fwlab_nfc_api_result fwlab_file_nand_v2_posix_restart(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_v2_config *config,
    const struct fwlab_file_nand_holder_v2 *holder,
    struct fwlab_file_nand_v2 **media);

/* Compatibility consumer: the existing physical NAND interface and C3 NFC.
 * It is not a claim that C3 consumes the new batch entry below. */
struct fwlab_nand_media fwlab_file_nand_v2_media(
    struct fwlab_file_nand_v2 *media);

/* One synchronous transaction of full pages, contiguous within one block.
 * main and oob are separate contiguous arrays of exactly page_count pages.
 * Caller keeps their bytes immutable until return. Results are published only
 * on API_OK; an I/O failure quarantines the instance until restart. An async
 * NFC caller must separately own/retain its frame leases and operation keys. */
enum fwlab_nfc_api_result fwlab_file_nand_v2_program_pages(
    struct fwlab_file_nand_v2 *media, const struct fwlab_nfc_ppa *first,
    uint32_t page_count, const uint8_t *main, size_t main_bytes,
    const uint8_t *oob, size_t oob_bytes,
    struct fwlab_nand_media_result *results, size_t result_count);

uint64_t fwlab_file_nand_v2_sequence(const struct fwlab_file_nand_v2 *media);
enum fwlab_nfc_api_result fwlab_file_nand_v2_close(
    struct fwlab_file_nand_v2 *media);

#endif
