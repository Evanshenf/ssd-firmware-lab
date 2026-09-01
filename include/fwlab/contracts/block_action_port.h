/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CONTRACTS_BLOCK_ACTION_PORT_H
#define FWLAB_CONTRACTS_BLOCK_ACTION_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "fwlab/contracts/hif_action.h"
#include "fwlab/portable/nvme_policy.h"

#define FWLAB_C43_BLOCK_ACTION_PORT_VERSION 1u

enum fwlab_c43_block_terminal_kind {
    FWLAB_C43_BLOCK_VALIDATED_ONLY = 1,
    FWLAB_C43_BLOCK_FAILED_NO_EFFECT = 2,
    FWLAB_C43_BLOCK_CANCELLED = 3,
    FWLAB_C43_BLOCK_COMPLETED = 4
};

enum fwlab_c43_block_capability {
    FWLAB_C43_BLOCK_CAP_VALIDATION_ONLY = 1u << 0,
    FWLAB_C43_BLOCK_CAP_DATA_EFFECT = 1u << 1,
    FWLAB_C43_BLOCK_CAP_DURABILITY = 1u << 2
};

#define FWLAB_C43_BLOCK_CAP_ALL ((uint32_t)0x07u)

struct fwlab_c43_block_action_request {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_hif_action_envelope common;
    struct fwlab_c43_block_intent intent;
    struct fwlab_c43_opaque_ref predecessor;
    uint32_t requested_witness_mask;
    uint32_t reserved[5];
};

struct fwlab_c43_block_action_terminal {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_hif_action_terminal common;
    struct fwlab_c43_completion_witness witness;
    uint32_t block_terminal_kind;
    uint32_t reserved[5];
};

typedef enum fwlab_hif_action_disposition
(*fwlab_c43_block_submit_fn)(
    void *context,
    const struct fwlab_c43_block_action_request *request,
    struct fwlab_hif_action_submit_result *result
);
typedef enum fwlab_c43_api_result
(*fwlab_c43_block_query_fn)(
    void *context,
    const struct fwlab_hif_action_token *token,
    struct fwlab_c43_block_action_terminal *terminal,
    bool *ready
);
typedef enum fwlab_c43_api_result
(*fwlab_c43_block_control_fn)(
    void *context,
    const struct fwlab_hif_action_token *token
);
typedef enum fwlab_c43_api_result
(*fwlab_c43_block_epoch_fn)(void *context, uint32_t old_epoch);
typedef enum fwlab_c43_api_result
(*fwlab_c43_block_quiescent_fn)(
    void *context,
    uint32_t epoch,
    bool *quiescent
);

struct fwlab_c43_block_action_port_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    fwlab_c43_block_submit_fn submit;
    fwlab_c43_block_query_fn query;
    fwlab_c43_block_control_fn cancel;
    fwlab_c43_block_control_fn retire;
    fwlab_c43_block_epoch_fn reset_begin;
    fwlab_c43_block_quiescent_fn quiescent;
    uint32_t reserved1[4];
};

struct fwlab_c43_block_action_port {
    const struct fwlab_c43_block_action_port_ops *ops;
    void *context;
    uint64_t generation;
    uint32_t capability_bits;
    uint32_t reserved;
};

int fwlab_c43_block_action_request_valid(
    const struct fwlab_c43_block_action_request *request
);
int fwlab_c43_block_action_terminal_valid(
    const struct fwlab_c43_block_action_terminal *terminal
);
int fwlab_c43_block_action_request_valid_for_port(
    const struct fwlab_c43_block_action_request *request,
    const struct fwlab_c43_block_action_port *port
);
int fwlab_c43_block_action_terminal_matches_request(
    const struct fwlab_c43_block_action_request *request,
    const struct fwlab_c43_block_action_terminal *terminal,
    const struct fwlab_c43_block_action_port *port
);
int fwlab_c43_block_action_port_valid(
    const struct fwlab_c43_block_action_port *port
);

#endif /* FWLAB_CONTRACTS_BLOCK_ACTION_PORT_H */
