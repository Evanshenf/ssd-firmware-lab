/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_SCRIPTED_BINDING_H
#define FWLAB_C35_SCRIPTED_BINDING_H

#include "../c35_binding.h"
#include "../../../core/fakes/c31_fake_provider.h"

#define C35_SCRIPTED_RESULTS 4u

struct c35_scripted_result {
    uint8_t used;
    uint8_t request_kind;
    uint8_t atom_mask;
    uint8_t atom;
    uint8_t reserved[4];
    struct fwlab_c31_command_handle command;
};

struct c35_scripted_binding {
    struct c31_fake_provider_context *provider;
    uint64_t instance_nonce;
    uint32_t owner_epoch;
    struct c35_scripted_result result[C35_SCRIPTED_RESULTS];
};

enum c35_result c35_scripted_binding_init(
    struct c35_scripted_binding *binding,
    struct c31_fake_provider_context *provider,
    uint64_t instance_nonce,
    uint32_t owner_epoch
);
struct c35_binding c35_scripted_binding_provider(
    struct c35_scripted_binding *binding
);

#endif
