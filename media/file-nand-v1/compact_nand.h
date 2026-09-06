/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_COMPACT_NAND_H
#define FWLAB_COMPACT_NAND_H

#include <stddef.h>
#include <stdint.h>
#include "fwlab/contracts/nand_media.h"

struct fwlab_file_nand_v1;

struct fwlab_file_nand_v1_config {
    struct fwlab_nfc_geometry geometry;
    uint8_t media_uuid[16];
};

struct fwlab_file_nand_holder_v1 {
    uint64_t device;
    uint64_t inode;
    uint8_t media_uuid[16];
};

size_t fwlab_file_nand_v1_arena_alignment(void);
size_t fwlab_file_nand_v1_arena_size(void);
/* Zero means invalid/unsupported geometry or overflowing layout. */
uint64_t fwlab_file_nand_v1_image_bytes(
    const struct fwlab_file_nand_v1_config *config);

/* Format creates a NEW file only. Restart never creates or resizes a file.
 * Both acquire exclusive ownership; close releases it. No raw device binding. */
enum fwlab_nfc_api_result fwlab_file_nand_v1_posix_format(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_v1_config *config,
    struct fwlab_file_nand_v1 **media,
    struct fwlab_file_nand_holder_v1 *holder);
enum fwlab_nfc_api_result fwlab_file_nand_v1_posix_restart(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_v1_config *config,
    const struct fwlab_file_nand_holder_v1 *holder,
    struct fwlab_file_nand_v1 **media);
struct fwlab_nand_media fwlab_file_nand_v1_media(
    struct fwlab_file_nand_v1 *media);
uint64_t fwlab_file_nand_v1_sequence(const struct fwlab_file_nand_v1 *media);
enum fwlab_nfc_api_result fwlab_file_nand_v1_close(
    struct fwlab_file_nand_v1 *media);

#endif
