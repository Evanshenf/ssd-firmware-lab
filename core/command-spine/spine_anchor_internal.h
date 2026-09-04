/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_COMMAND_SPINE_ANCHOR_INTERNAL_H
#define FWLAB_COMMAND_SPINE_ANCHOR_INTERNAL_H

#include <stdint.h>

#define FWLAB_SQ_CONSUMER_ANCHOR_V0_TAG UINT32_C(0x53514341)
#define FWLAB_CQE_PUBLISHER_ANCHOR_V0_TAG UINT32_C(0x43515041)

struct fwlab_sq_consumer_anchor_v0 {
    uint32_t type_tag;
};

struct fwlab_cqe_publisher_anchor_v0 {
    uint32_t type_tag;
};

/* Link-only markers.  The real instance-scoped HIF API is deferred to J1. */
extern const struct fwlab_sq_consumer_anchor_v0
    fwlab_authoritative_sq_consumer_v0;
extern const struct fwlab_cqe_publisher_anchor_v0
    fwlab_authoritative_cqe_publisher_v0;

struct fwlab_spine_construction_v0 {
    const struct fwlab_sq_consumer_anchor_v0 *sq_consumer;
    const struct fwlab_cqe_publisher_anchor_v0 *cqe_publisher;
};

extern const struct fwlab_spine_construction_v0
    fwlab_spine_construction_v0;

int fwlab_spine_construction_valid(void);

#endif /* FWLAB_COMMAND_SPINE_ANCHOR_INTERNAL_H */
