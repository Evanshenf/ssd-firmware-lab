/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_PRIVATE_NFC_PAGE_V2_MODEL_H
#define FWLAB_PRIVATE_NFC_PAGE_V2_MODEL_H

#include "fwlab/contracts/nfc_page_v2_provider.h"
#include "fwlab/private/nand_batch_v2.h"

struct fwlab_nfc_page_v2_model;
/* PAGE2-R0: functional stored-state/CRC checking; no injected bit faults,
 * retry algorithm or NAND timing simulation. Unsupported flags reject init. */
struct fwlab_nfc_page_v2_config {
    uint16_t version;
    uint16_t size;
    uint32_t profile;
    struct fwlab_nfc_geometry geometry;
    uint8_t media_uuid[16];
    uint64_t instance_nonce;
    uint64_t operation_uid_limit;
    uint32_t controller_epoch;
    uint32_t generation;
    uint32_t fault_flags;
    uint32_t timing_flags;
    uint32_t reserved[2];
};
size_t fwlab_nfc_page_v2_arena_size(void);
size_t fwlab_nfc_page_v2_arena_alignment(void);
enum fwlab_nfc_api_result fwlab_nfc_page_v2_init(
    void *arena, size_t arena_bytes,
    const struct fwlab_nfc_page_v2_config *config,
    const struct fwlab_nand_batch_v2 *media,
    struct fwlab_nfc_page_v2_model **model);
struct fwlab_nfc_page_v2_provider fwlab_nfc_page_v2_provider(
    struct fwlab_nfc_page_v2_model *model);
/* Private construction seam; unlike quiescent(), requires an open, healthy,
 * empty instance. No reset, epoch or UID changes. */
enum fwlab_nfc_api_result fwlab_nfc_page_v2_live_idle(
    const struct fwlab_nfc_page_v2_model *model, bool *idle);

#endif
