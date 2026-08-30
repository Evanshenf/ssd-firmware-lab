/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_BUNDLE_H
#define FWLAB_C35_BUNDLE_H

#include "c35_binding.h"
#include "c35_profile.h"
#include "fwlab/contracts/nand_media.h"
#include "fwlab/private/c34_physical_txn.h"

#define C35_BUNDLE_VERSION 2u
#define C35_ENDPOINT_VERSION 2u
#define C35_RAW_PROJECTION_BYTES 4432u

enum c35_endpoint_role {
    C35_ENDPOINT_RAW_MEDIA = 1,
    C35_ENDPOINT_PHYSICAL_TXN = 2
};

struct c35_endpoint_descriptor {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    uint32_t role;
    uint32_t feature_bits;
    uint32_t media_profile_id;
    uint32_t geometry_id;
    uint64_t coherence_cookie;
    struct c35_profile_descriptor profile;
};

struct c35_media_endpoint {
    struct c35_endpoint_descriptor descriptor;
    struct fwlab_nand_media provider;
};

struct c35_physical_endpoint {
    struct c35_endpoint_descriptor descriptor;
    struct c34_physical_txn_provider provider;
};

struct c35_bundle {
    uint64_t claimant;
    uint64_t last_released_claimant;
    struct c35_profile_descriptor profile;
    struct fwlab_nand_media media;
    struct c34_physical_txn_provider physical;
    uint8_t claimed;
    uint8_t reserved[7];
};

void c35_media_endpoint_make(
    struct c35_media_endpoint *endpoint,
    const struct c35_profile_descriptor *profile,
    uint64_t coherence_cookie,
    const struct fwlab_nand_media *provider
);
void c35_physical_endpoint_make(
    struct c35_physical_endpoint *endpoint,
    const struct c35_profile_descriptor *profile,
    uint64_t coherence_cookie,
    const struct c34_physical_txn_provider *provider
);
enum c35_result c35_bundle_init(
    struct c35_bundle *bundle,
    const struct c35_media_endpoint *media,
    const struct c35_physical_endpoint *physical
);
enum c35_result c35_bundle_claim(struct c35_bundle *bundle, uint64_t claimant);
enum c35_result c35_bundle_release(struct c35_bundle *bundle, uint64_t claimant);
enum c35_result c35_bundle_release_query(
    const struct c35_bundle *bundle,
    uint64_t claimant,
    bool *released
);
enum c35_result c35_bundle_projection(
    const struct c35_bundle *bundle,
    uint8_t bytes[C35_RAW_PROJECTION_BYTES]
);

#endif
