/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_BUNDLE_H
#define FWLAB_C35_BUNDLE_H

#include "c35_binding.h"
#include "fwlab/contracts/nand_media.h"
#include "fwlab/private/c34_physical_txn.h"

#define C35_BUNDLE_VERSION 1u
#define C35_RAW_PROJECTION_BYTES 4432u

struct c35_bundle {
    uint64_t owner_cookie;
    uint64_t claimant;
    uint32_t profile_id;
    uint32_t geometry_id;
    struct fwlab_nand_media media;
    struct c34_physical_txn_provider physical;
    uint8_t claimed;
    uint8_t reserved[7];
};

enum c35_result c35_bundle_init(
    struct c35_bundle *bundle,
    uint64_t owner_cookie,
    uint32_t profile_id,
    uint32_t geometry_id,
    const struct fwlab_nand_media *media,
    const struct c34_physical_txn_provider *physical
);
enum c35_result c35_bundle_claim(struct c35_bundle *bundle, uint64_t claimant);
enum c35_result c35_bundle_release(struct c35_bundle *bundle, uint64_t claimant);
enum c35_result c35_bundle_projection(
    const struct c35_bundle *bundle,
    uint8_t bytes[C35_RAW_PROJECTION_BYTES]
);

#endif
