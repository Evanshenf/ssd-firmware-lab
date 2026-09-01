/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C43_INTERNAL_H
#define FWLAB_C43_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/portable/c4_command_graph.h"
#include "fwlab/portable/nvme_codec.h"

#define FWLAB_C43_INTERNAL_MAGIC UINT64_C(0x4334334752415031)

struct fwlab_c43_graph {
    uint64_t magic;
    struct fwlab_c43_graph_config config;
    struct fwlab_c43_queue_effect_port_ops queue_ops;
    struct fwlab_c43_target_resolver_port_ops target_ops;
    struct fwlab_c43_block_action_port_ops block_ops;
    struct fwlab_c43_graph_providers providers;
    struct fwlab_c43_graph_observer observer;
};

int c43_bytes_zero(const void *value, size_t size);
int c43_handle_valid(const struct fwlab_nvme_command_handle *handle);
int c43_origin_valid(const struct fwlab_nvme_origin_token *origin);
int c43_ticket_valid(const struct fwlab_hif_command_ticket *ticket);
int c43_handle_equal(
    const struct fwlab_nvme_command_handle *left,
    const struct fwlab_nvme_command_handle *right
);
int c43_origin_equal(
    const struct fwlab_nvme_origin_token *left,
    const struct fwlab_nvme_origin_token *right
);
int c43_action_token_equal(
    const struct fwlab_hif_action_token *left,
    const struct fwlab_hif_action_token *right
);
int c43_ref_zero(const struct fwlab_c43_opaque_ref *reference);
int c43_ref_equal(
    const struct fwlab_c43_opaque_ref *left,
    const struct fwlab_c43_opaque_ref *right
);
int c43_profile_is_fixed(const struct fwlab_nvme_profile *profile);
void c43_profile_fixed(struct fwlab_nvme_profile *profile);
int c43_graph_valid(const struct fwlab_c43_graph *graph);
int c43_semantic_status_valid(uint32_t status);
int c43_witness_mask_valid(uint32_t mask);

#endif /* FWLAB_C43_INTERNAL_H */
