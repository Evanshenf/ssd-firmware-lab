/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CONTRACTS_HIF_TARGET_RESOLVER_PORT_H
#define FWLAB_CONTRACTS_HIF_TARGET_RESOLVER_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "fwlab/contracts/hif_action.h"
#include "fwlab/contracts/hif_command_port.h"
#include "fwlab/portable/nvme_policy.h"

#define FWLAB_C43_TARGET_RESOLVER_PORT_VERSION 1u

enum fwlab_c43_target_operation {
    FWLAB_C43_TARGET_RESOLVE_ABORT = 1
};

enum fwlab_c43_target_outcome {
    FWLAB_C43_TARGET_FOUND = 1,
    FWLAB_C43_TARGET_NOT_FOUND = 2,
    FWLAB_C43_TARGET_TOO_LATE = 3,
    FWLAB_C43_TARGET_STALE = 4,
    FWLAB_C43_TARGET_SUPERSEDED = 5,
    FWLAB_C43_TARGET_FAULT = 6,
    FWLAB_C43_TARGET_RELEASED = 7
};

struct fwlab_c43_abort_target_ref {
    uint64_t word[2];
};

struct fwlab_c43_target_request {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_hif_action_envelope common;
    struct fwlab_hif_command_ticket abort_command;
    uint32_t operation;
    uint32_t reserved[3];
};

struct fwlab_c43_target_terminal {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_hif_action_terminal common;
    struct fwlab_hif_command_ticket target;
    struct fwlab_c43_abort_target_ref reference;
    uint32_t outcome;
    uint32_t reserved[7];
};

typedef enum fwlab_hif_action_disposition
(*fwlab_c43_target_submit_fn)(
    void *context,
    const struct fwlab_c43_target_request *request,
    struct fwlab_hif_action_submit_result *result
);
typedef enum fwlab_c43_api_result
(*fwlab_c43_target_query_fn)(
    void *context,
    const struct fwlab_hif_action_token *token,
    struct fwlab_c43_target_terminal *terminal,
    bool *ready
);
typedef enum fwlab_c43_api_result
(*fwlab_c43_target_control_fn)(
    void *context,
    const struct fwlab_hif_action_token *token
);
typedef enum fwlab_c43_api_result
(*fwlab_c43_target_epoch_fn)(void *context, uint32_t old_epoch);
typedef enum fwlab_c43_api_result
(*fwlab_c43_target_quiescent_fn)(
    void *context,
    uint32_t epoch,
    bool *quiescent
);

struct fwlab_c43_target_resolver_port_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    fwlab_c43_target_submit_fn submit;
    fwlab_c43_target_query_fn query;
    fwlab_c43_target_control_fn cancel;
    fwlab_c43_target_control_fn release;
    fwlab_c43_target_control_fn release_query;
    fwlab_c43_target_epoch_fn reset_begin;
    fwlab_c43_target_quiescent_fn quiescent;
    uint32_t reserved1[4];
};

struct fwlab_c43_target_resolver_port {
    const struct fwlab_c43_target_resolver_port_ops *ops;
    void *context;
    uint64_t generation;
};

int fwlab_c43_target_request_valid(
    const struct fwlab_c43_target_request *request
);
int fwlab_c43_target_terminal_valid(
    const struct fwlab_c43_target_terminal *terminal
);
int fwlab_c43_target_terminal_matches_request(
    const struct fwlab_c43_target_request *request,
    const struct fwlab_c43_target_terminal *terminal
);
int fwlab_c43_target_resolver_port_valid(
    const struct fwlab_c43_target_resolver_port *port
);

#endif /* FWLAB_CONTRACTS_HIF_TARGET_RESOLVER_PORT_H */
