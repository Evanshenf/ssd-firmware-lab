/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PRIVATE_BLOCK_VOLUME_V0_H
#define FWLAB_PRIVATE_BLOCK_VOLUME_V0_H

#include <stddef.h>
#include "fwlab/contracts/block_service_v0.h"

/* Provisional construction contract: logical facts from a READY FTL volume.
 * Not NAND geometry, a Host NSID, a file format or a mutable global config. */
#define FWLAB_BLOCK_VOLUME_V0_VERSION 1u

struct fwlab_block_volume_desc_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_block_namespace_ref_v0 namespace_ref;
    uint64_t lba_count;
    uint32_t lba_bytes;
    uint32_t reserved1[3];
};

struct fwlab_block_volume_binding_v0 {
    struct fwlab_block_volume_desc_v0 volume;
    struct fwlab_block_service_v0 service;
};

static inline int fwlab_block_volume_desc_v0_valid(
    const struct fwlab_block_volume_desc_v0 *volume)
{
    return volume != NULL && volume->version == FWLAB_BLOCK_VOLUME_V0_VERSION &&
           volume->size == sizeof(*volume) && volume->reserved0 == 0 &&
           (volume->namespace_ref.word[0] != 0 ||
            volume->namespace_ref.word[1] != 0) && volume->lba_count != 0 &&
           volume->lba_bytes >= 512 && volume->lba_bytes <= 4096 &&
           (volume->lba_bytes & (volume->lba_bytes - 1u)) == 0 &&
           volume->lba_count <= UINT64_MAX / volume->lba_bytes &&
           volume->reserved1[0] == 0 && volume->reserved1[1] == 0 &&
           volume->reserved1[2] == 0;
}

#endif
