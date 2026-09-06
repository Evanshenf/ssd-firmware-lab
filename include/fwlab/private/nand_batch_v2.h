/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_PRIVATE_NAND_BATCH_V2_H
#define FWLAB_PRIVATE_NAND_BATCH_V2_H

#include <stddef.h>
#include "fwlab/contracts/nand_media.h"

#define FWLAB_NAND_BATCH_V2_VERSION 2u

/* These callbacks and scalar are constructed from the same exclusively owned
 * physical instance. Context is scalar.context. Descriptors/ops/context remain
 * live and immutable until the NFC construction has drained. No file types or
 * offsets cross this boundary. Non-OK mutation results are not authoritative.
 * Read outputs may be partially overwritten on error; only API_OK publishes
 * all page/block facts. API_OK may describe physical TORN/ERASED cells. */
struct fwlab_nand_batch_v2_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    enum fwlab_nfc_api_result (*read_pages)(void *,
        const struct fwlab_nfc_ppa *, uint32_t,
        uint8_t *, size_t, uint8_t *, size_t,
        struct fwlab_nand_page_info *, size_t,
        struct fwlab_nand_block_info *);
    enum fwlab_nfc_api_result (*program_pages)(void *,
        const struct fwlab_nfc_ppa *, uint32_t,
        const uint8_t *, size_t, const uint8_t *, size_t,
        struct fwlab_nand_media_result *, size_t);
};
struct fwlab_nand_batch_v2 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    const struct fwlab_nand_batch_v2_ops *ops;
    struct fwlab_nand_media scalar;
    struct fwlab_nfc_geometry geometry;
    uint8_t media_uuid[16];
};

#endif
