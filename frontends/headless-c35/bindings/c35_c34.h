/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_C34_BINDING_H
#define FWLAB_C35_C34_BINDING_H

#include "../c35_binding.h"
#include "../../../core/c34/c34.h"

struct c35_c34_binding {
    struct c34 *firmware;
    uint64_t instance_nonce;
    uint32_t owner_epoch;
};

enum c35_result c35_c34_binding_init(
    struct c35_c34_binding *binding,
    struct c34 *firmware,
    uint64_t instance_nonce,
    uint32_t owner_epoch
);
struct c35_binding c35_c34_binding_provider(
    struct c35_c34_binding *binding
);

#endif
