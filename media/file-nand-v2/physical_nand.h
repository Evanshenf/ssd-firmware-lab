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
    /* Per-open mapped resource budget, not serialized NAND geometry/format.
     * Zero preserves the 600-MiB default; explicit budgets are at most90GiB.
     * The caller provisions sufficient tmpfs/RAM before new-image admission. */
    uint64_t mapped_budget_bytes;
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

/* Explicit Linux/tmpfs BYTE-copy profile, default600MiB for the whole image;
 * config.mapped_budget_bytes permits an explicitly provisioned larger budget.
 * Strict ordinary-I/O format/recovery precedes full preallocation, one fixed
 * shared mapping and writable prefaulting, all before outputs are published.
 * Failure never selects ordinary I/O implicitly. Geometry/format are unchanged.
 * One executor exclusively controls the file/EOF until close: no independent
 * writes, truncate, hole punch or unlink. OFD locks do not enforce that premise.
 * Mapped pointers stay private. Returned errors keep the existing media rules;
 * mapped-access faults and failed unmap terminate the process, not return an
 * in-process error. Close unmaps before consuming the FD. Tmpfs supports only
 * process restart while the filesystem survives, not host power durability. */
enum fwlab_nfc_api_result fwlab_file_nand_v2_posix_mapped_format(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_v2_config *config,
    struct fwlab_file_nand_v2 **media,
    struct fwlab_file_nand_holder_v2 *holder);
enum fwlab_nfc_api_result fwlab_file_nand_v2_posix_mapped_restart(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_v2_config *config,
    const struct fwlab_file_nand_holder_v2 *holder,
    struct fwlab_file_nand_v2 **media);

/* Compatibility consumer: the existing physical NAND interface and C3 NFC.
 * It is not a claim that C3 consumes the new batch entry below. */
struct fwlab_nand_media fwlab_file_nand_v2_media(
    struct fwlab_file_nand_v2 *media);

/* Physical read capability; does not change the format or write protocol.
 * Main/OOB arrays and page facts cover exactly count contiguous same-block
 * pages. API_OK can include ERASED or TORN physical states; the NFC caller
 * determines validity/ECC and must not publish a failed group as valid data.
 * Outputs are unspecified on API error and must remain caller-private. */
enum fwlab_nfc_api_result fwlab_file_nand_v2_read_pages(
    struct fwlab_file_nand_v2 *media, const struct fwlab_nfc_ppa *first,
    uint32_t page_count, uint8_t *main, size_t main_bytes,
    uint8_t *oob, size_t oob_bytes,
    struct fwlab_nand_page_info *pages, size_t page_capacity,
    struct fwlab_nand_block_info *block);

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
