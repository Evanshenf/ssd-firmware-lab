/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CONTRACTS_HIF_QUEUE_EFFECT_PORT_H
#define FWLAB_CONTRACTS_HIF_QUEUE_EFFECT_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "fwlab/contracts/hif_action.h"
#include "fwlab/portable/nvme_policy.h"

#define FWLAB_C43_QUEUE_EFFECT_PORT_VERSION 1u

enum fwlab_c43_queue_effect_operation {
    FWLAB_C43_QUEUE_CREATE_CQ = 1,
    FWLAB_C43_QUEUE_CREATE_SQ = 2,
    FWLAB_C43_QUEUE_DELETE_CQ = 3,
    FWLAB_C43_QUEUE_DELETE_SQ = 4
};

enum fwlab_c43_queue_role {
    FWLAB_C43_QUEUE_ROLE_IO_CQ = 1,
    FWLAB_C43_QUEUE_ROLE_IO_SQ = 2
};

enum fwlab_c43_queue_finish_decision {
    FWLAB_C43_QUEUE_FINISH_COMMIT = 1,
    FWLAB_C43_QUEUE_FINISH_ABORT = 2
};

enum fwlab_c43_queue_effect_state {
    FWLAB_C43_QUEUE_EFFECT_PREPARE_PENDING = 1,
    FWLAB_C43_QUEUE_EFFECT_PREPARED = 2,
    FWLAB_C43_QUEUE_EFFECT_COMMIT_PENDING = 3,
    FWLAB_C43_QUEUE_EFFECT_ABORT_PENDING = 4,
    FWLAB_C43_QUEUE_EFFECT_COMMITTED = 5,
    FWLAB_C43_QUEUE_EFFECT_ABORTED = 6,
    FWLAB_C43_QUEUE_EFFECT_TOO_LATE = 7,
    FWLAB_C43_QUEUE_EFFECT_RESET_SUPERSEDED = 8,
    FWLAB_C43_QUEUE_EFFECT_FAILED = 9,
    FWLAB_C43_QUEUE_EFFECT_POISONED = 10,
    FWLAB_C43_QUEUE_EFFECT_RETIRED = 11
};

struct fwlab_c43_queue_txn_ref {
    uint64_t word[2];
};

struct fwlab_c43_queue_live_ref {
    uint64_t word[2];
};

struct fwlab_c43_queue_facts {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_c43_queue_txn_ref transaction;
    struct fwlab_c43_queue_live_ref queue;
    struct fwlab_c43_queue_live_ref associated_cq;
    uint32_t operation;
    uint32_t role;
    uint32_t queue_entries;
    uint8_t address_present;
    uint8_t queue_exists;
    uint8_t associated_cq_exists;
    uint8_t association_matches;
    uint8_t active_commands_zero;
    uint8_t target_refs_zero;
    uint8_t reserved_publications_zero;
    uint8_t unacked_completions_zero;
    uint8_t current_relation;
    uint8_t reserved1[3];
    uint32_t reserved2[4];
};

struct fwlab_c43_queue_effect_request {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_hif_action_envelope common;
    uint32_t operation;
    uint32_t role;
    uint32_t reserved[4];
};

struct fwlab_c43_queue_finish_request {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_hif_action_token token;
    struct fwlab_c43_queue_txn_ref transaction;
    uint32_t decision;
    uint32_t reserved1[5];
};

struct fwlab_c43_queue_effect_terminal {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_hif_action_terminal common;
    struct fwlab_c43_queue_facts facts;
    uint32_t state;
    uint32_t decision;
    uint32_t reserved[4];
};

typedef enum fwlab_hif_action_disposition
(*fwlab_c43_queue_prepare_start_fn)(
    void *context,
    const struct fwlab_c43_queue_effect_request *request,
    struct fwlab_hif_action_submit_result *result
);
typedef enum fwlab_c43_api_result
(*fwlab_c43_queue_prepare_query_fn)(
    void *context,
    const struct fwlab_hif_action_token *token,
    struct fwlab_c43_queue_effect_terminal *terminal,
    bool *ready
);
typedef enum fwlab_hif_action_disposition
(*fwlab_c43_queue_finish_start_fn)(
    void *context,
    const struct fwlab_c43_queue_finish_request *request,
    struct fwlab_hif_action_submit_result *result
);
typedef enum fwlab_c43_api_result
(*fwlab_c43_queue_finish_query_fn)(
    void *context,
    const struct fwlab_hif_action_token *token,
    struct fwlab_c43_queue_effect_terminal *terminal,
    bool *ready
);
typedef enum fwlab_c43_api_result
(*fwlab_c43_queue_cancel_fn)(
    void *context,
    const struct fwlab_hif_action_token *token
);
typedef enum fwlab_c43_api_result
(*fwlab_c43_queue_retire_fn)(
    void *context,
    const struct fwlab_hif_action_token *token
);
typedef enum fwlab_c43_api_result
(*fwlab_c43_queue_epoch_fn)(void *context, uint32_t old_epoch);
typedef enum fwlab_c43_api_result
(*fwlab_c43_queue_quiescent_fn)(
    void *context,
    uint32_t epoch,
    bool *quiescent
);

struct fwlab_c43_queue_effect_port_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    fwlab_c43_queue_prepare_start_fn prepare_start;
    fwlab_c43_queue_prepare_query_fn prepare_query;
    fwlab_c43_queue_finish_start_fn finish_start;
    fwlab_c43_queue_finish_query_fn finish_query;
    fwlab_c43_queue_cancel_fn cancel;
    fwlab_c43_queue_retire_fn retire;
    fwlab_c43_queue_epoch_fn reset_begin;
    fwlab_c43_queue_quiescent_fn quiescent;
    uint32_t reserved1[4];
};

struct fwlab_c43_queue_effect_port {
    const struct fwlab_c43_queue_effect_port_ops *ops;
    void *context;
    uint64_t generation;
};

int fwlab_c43_queue_facts_valid(const struct fwlab_c43_queue_facts *facts);
int fwlab_c43_queue_effect_request_valid(
    const struct fwlab_c43_queue_effect_request *request
);
int fwlab_c43_queue_finish_request_valid(
    const struct fwlab_c43_queue_finish_request *request
);
int fwlab_c43_queue_effect_terminal_valid(
    const struct fwlab_c43_queue_effect_terminal *terminal
);
int fwlab_c43_queue_effect_terminal_matches_request(
    const struct fwlab_c43_queue_effect_request *request,
    const struct fwlab_c43_queue_effect_terminal *terminal
);
int fwlab_c43_queue_finish_terminal_matches_request(
    const struct fwlab_c43_queue_finish_request *request,
    const struct fwlab_c43_queue_effect_terminal *terminal
);
int fwlab_c43_queue_effect_port_valid(
    const struct fwlab_c43_queue_effect_port *port
);

#endif /* FWLAB_CONTRACTS_HIF_QUEUE_EFFECT_PORT_H */
