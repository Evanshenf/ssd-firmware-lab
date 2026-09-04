// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: BSD-3-Clause

#include "spine_anchor_internal.h"

const struct fwlab_spine_construction_v0 fwlab_spine_construction_v0 = {
    .sq_consumer = &fwlab_authoritative_sq_consumer_v0,
    .cqe_publisher = &fwlab_authoritative_cqe_publisher_v0,
};

int fwlab_spine_construction_valid(void)
{
    return fwlab_spine_construction_v0.sq_consumer ==
               &fwlab_authoritative_sq_consumer_v0 &&
           fwlab_spine_construction_v0.cqe_publisher ==
               &fwlab_authoritative_cqe_publisher_v0;
}
