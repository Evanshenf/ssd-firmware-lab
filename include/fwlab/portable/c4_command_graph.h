/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PORTABLE_C4_COMMAND_GRAPH_H
#define FWLAB_PORTABLE_C4_COMMAND_GRAPH_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/contracts/block_action_port.h"
#include "fwlab/contracts/hif_command_port.h"
#include "fwlab/contracts/hif_queue_effect_port.h"
#include "fwlab/contracts/hif_target_resolver_port.h"
#include "fwlab/portable/nvme_policy.h"

#define FWLAB_C43_GRAPH_VERSION 1u
#define FWLAB_C43_PROVIDER_BUNDLE_VERSION 1u
#define FWLAB_C43_ADMIT_RESULT_VERSION 1u
#define FWLAB_C43_MAX_COMMANDS 4u
#define FWLAB_C43_ACTIONS_PER_COMMAND 8u
#define FWLAB_C43_MAX_ACTIONS 32u
#define FWLAB_C43_MAILBOX_CAPACITY 32u
#define FWLAB_C43_SERVICE_GAP_MAXIMUM 64u
#define FWLAB_C43_PROGRESS_MAXIMUM 128u
#define FWLAB_C43_CONTROL_PROGRESS_MAXIMUM 256u

enum fwlab_c43_graph_result {
    FWLAB_C43_GRAPH_OK = 0,
    FWLAB_C43_GRAPH_INVALID = 1,
    FWLAB_C43_GRAPH_WRONG_STATE = 2,
    FWLAB_C43_GRAPH_STALE = 3,
    FWLAB_C43_GRAPH_NO_CAPACITY = 4,
    FWLAB_C43_GRAPH_IN_PROGRESS = 5,
    FWLAB_C43_GRAPH_SUPERSEDED = 6,
    FWLAB_C43_GRAPH_POISONED = 7,
    FWLAB_C43_GRAPH_COUNTER_EXHAUSTED = 8,
    FWLAB_C43_GRAPH_NOT_IMPLEMENTED = 9
};

enum fwlab_c43_graph_phase {
    FWLAB_C43_PHASE_FREE = 0,
    FWLAB_C43_PHASE_PREPARED = 1,
    FWLAB_C43_PHASE_ADMITTED_POLICY = 2,
    FWLAB_C43_PHASE_RESOLVE_WAIT = 3,
    FWLAB_C43_PHASE_ACTION_WAIT = 4,
    FWLAB_C43_PHASE_CANCEL_PENDING = 5,
    FWLAB_C43_PHASE_TERMINAL_LATCHED_DRAIN = 6,
    FWLAB_C43_PHASE_INTENT_READY = 7,
    FWLAB_C43_PHASE_LEASED = 8,
    FWLAB_C43_PHASE_CONSUME_PREPARED = 9,
    FWLAB_C43_PHASE_COMMIT_UNKNOWN = 10,
    FWLAB_C43_PHASE_CLEANUP_PENDING = 11,
    FWLAB_C43_PHASE_RESET_DRAIN = 12,
    FWLAB_C43_PHASE_RETIRED_TOMBSTONE = 13
};

enum fwlab_c43_terminal_winner {
    FWLAB_C43_WINNER_NONE = 0,
    FWLAB_C43_WINNER_NORMAL = 1,
    FWLAB_C43_WINNER_ABORT = 2,
    FWLAB_C43_WINNER_RESET = 3,
    FWLAB_C43_WINNER_FAULT = 4
};

enum fwlab_c43_publication_state {
    FWLAB_C43_PUBLICATION_ELIGIBLE = 0,
    FWLAB_C43_PUBLICATION_LEASED = 1,
    FWLAB_C43_PUBLICATION_CONSUME_PREPARED = 2,
    FWLAB_C43_PUBLICATION_CONSUMED = 3,
    FWLAB_C43_PUBLICATION_SUPPRESSED = 4
};

enum fwlab_c43_provider_kind {
    FWLAB_C43_PROVIDER_QUEUE = 1,
    FWLAB_C43_PROVIDER_DMA = 2,
    FWLAB_C43_PROVIDER_BLOCK = 3,
    FWLAB_C43_PROVIDER_TARGET = 4
};

enum fwlab_c43_reservation_credit_bit {
    FWLAB_C43_CREDIT_POLICY_SCRATCH = 1u << 0,
    FWLAB_C43_CREDIT_INTENT = 1u << 1,
    FWLAB_C43_CREDIT_READY = 1u << 2,
    FWLAB_C43_CREDIT_LEASE = 1u << 3,
    FWLAB_C43_CREDIT_CONSUME = 1u << 4,
    FWLAB_C43_CREDIT_FINALIZER = 1u << 5,
    FWLAB_C43_CREDIT_ABORT = 1u << 6,
    FWLAB_C43_CREDIT_TARGET = 1u << 7,
    FWLAB_C43_CREDIT_QUEUE_TRANSACTION = 1u << 8,
    FWLAB_C43_CREDIT_BLOCK_INTENT = 1u << 9
};

#define FWLAB_C43_CREDIT_ALL ((uint32_t)0x03ffu)

enum fwlab_c43_action_domain {
    FWLAB_C43_ACTION_DOMAIN_NONE = 0,
    FWLAB_C43_ACTION_DOMAIN_QUEUE = 1,
    FWLAB_C43_ACTION_DOMAIN_TARGET = 2,
    FWLAB_C43_ACTION_DOMAIN_BLOCK = 3
};

enum fwlab_c43_action_state {
    FWLAB_C43_ACTION_STATE_NONE = 0,
    FWLAB_C43_ACTION_STATE_SUBMIT_READY = 1,
    FWLAB_C43_ACTION_STATE_PREPARE_QUERY = 2,
    FWLAB_C43_ACTION_STATE_DECIDE = 3,
    FWLAB_C43_ACTION_STATE_FINISH_READY = 4,
    FWLAB_C43_ACTION_STATE_FINISH_QUERY = 5,
    FWLAB_C43_ACTION_STATE_APPLY_COMMIT = 6,
    FWLAB_C43_ACTION_STATE_APPLY_ABORT = 7,
    FWLAB_C43_ACTION_STATE_RETIRE = 8,
    FWLAB_C43_ACTION_STATE_TERMINAL_HELD = 9,
    FWLAB_C43_ACTION_STATE_REJECTED_NO_EFFECT = 10,
    FWLAB_C43_ACTION_STATE_FAULT = 11
};

enum fwlab_c43_nq_state {
    FWLAB_C43_NQ_UNNEGOTIATED = 0,
    FWLAB_C43_NQ_NEGOTIATED = 1
};

struct fwlab_c43_counter_seed {
    uint64_t next;
    uint64_t maximum;
};

struct fwlab_c43_graph_config {
    uint16_t version;
    uint16_t size;
    uint32_t reserved_header;
    struct fwlab_nvme_profile profile;
    uint16_t command_capacity;
    uint16_t actions_per_command;
    uint16_t queue_mailbox_capacity;
    uint16_t target_mailbox_capacity;
    uint16_t block_mailbox_capacity;
    uint16_t dma_mailbox_capacity;
    uint32_t service_gap_maximum;
    uint32_t ordinary_progress_maximum;
    uint32_t control_progress_maximum;
    uint32_t safety_generation;
    uint32_t reserved_alignment;
    uint64_t instance_nonce;
    uint32_t controller_epoch;
    uint32_t reserved0;
    struct fwlab_c43_counter_seed command_uid;
    struct fwlab_c43_counter_seed action_uid;
    struct fwlab_c43_counter_seed transaction_uid;
    struct fwlab_c43_counter_seed lease_uid;
    struct fwlab_c43_counter_seed consume_uid;
    struct fwlab_c43_counter_seed finalizer_uid;
    uint32_t reserved1[4];
};

struct fwlab_c43_graph_providers {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_c43_queue_effect_port queue;
    struct fwlab_c43_target_resolver_port target;
    struct fwlab_c43_block_action_port block;
    uint64_t dma_generation;
    uint32_t dma_bound;
    uint32_t reserved[3];
};

struct fwlab_c43_command_observer {
    struct fwlab_nvme_command_handle handle;
    struct fwlab_nvme_origin_token origin;
    uint64_t transaction_uid;
    uint32_t phase;
    uint32_t terminal_winner;
    uint32_t publication;
    uint32_t required_witness_mask;
    uint32_t satisfied_witness_mask;
    uint16_t action_count;
    uint8_t in_use;
    uint8_t success_eligible;
    uint8_t provider_generation_current;
    uint8_t action_domain;
    uint8_t action_state;
    uint8_t resolution_valid;
    uint32_t reservation_credit_mask;
    uint64_t first_action_uid;
    uint32_t action_generation;
    uint32_t resolved_status;
};

struct fwlab_c43_graph_observer {
    uint16_t version;
    uint16_t size;
    uint32_t controller_epoch;
    uint64_t instance_nonce;
    uint32_t active_commands;
    uint32_t active_actions;
    uint32_t ready_count;
    uint32_t cleanup_count;
    uint64_t provider_generation[4];
    uint8_t admission_closed;
    uint8_t resetting;
    uint8_t tearing_down;
    uint8_t dead;
    uint32_t reserved_intent_credits;
    struct fwlab_c43_command_observer commands[FWLAB_C43_MAX_COMMANDS];
    uint32_t reserved_ready_credits;
    uint32_t reserved_lease_credits;
    uint32_t reserved_consume_credits;
    uint32_t reserved_finalizer_credits;
    uint32_t reserved_abort_credits;
    uint32_t reserved_target_credits;
    uint32_t reserved_queue_transaction_credits;
    uint32_t reserved_block_intent_credits;
    uint32_t nq_state;
    uint16_t queue_owner_slot_plus_one;
    uint8_t queue_txn_active;
    uint8_t io_cq_present;
    uint8_t io_sq_present;
    uint8_t reserved_queue_flags[7];
    struct fwlab_c43_queue_live_ref io_cq;
    struct fwlab_c43_queue_live_ref io_sq;
    struct fwlab_c43_queue_live_ref sq_associated_cq;
    uint32_t reserved_queue[4];
};

struct fwlab_c43_step_result {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint32_t requested_budget;
    uint32_t units_executed;
    uint32_t transitions;
    uint32_t ready_events;
    uint32_t service_gap_maximum;
    uint32_t reserved[3];
};

struct fwlab_c43_admit_result {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint32_t state;
    uint32_t reserved1;
    struct fwlab_hif_command_ticket ticket;
    uint32_t reserved2[6];
};

struct fwlab_c43_graph;

int fwlab_c43_graph_config_valid(
    const struct fwlab_c43_graph_config *config
);
int fwlab_c43_graph_observer_valid(
    const struct fwlab_c43_graph_observer *observer
);
size_t fwlab_c43_graph_arena_size(
    const struct fwlab_c43_graph_config *config
);
size_t fwlab_c43_graph_arena_alignment(void);
enum fwlab_c43_graph_result fwlab_c43_graph_init(
    void *arena,
    size_t arena_size,
    const struct fwlab_c43_graph_config *config,
    const struct fwlab_c43_graph_providers *providers,
    struct fwlab_c43_graph **graph
);
/*
 * Caller-serialized direct reservation seam.  key/result must not overlap one
 * another or the graph arena.  A non-OK graph result leaves caller outputs and
 * graph state unchanged.  Capacity pressure is an OK call with a
 * FWLAB_HIF_PREPARE_BACKPRESSURE result and also leaves graph state unchanged.
 * An exact repeated start returns IN_PROGRESS; query recovers the original
 * immutable prepared token without minting another identity.
 */
enum fwlab_c43_graph_result fwlab_c43_graph_prepare_start(
    struct fwlab_c43_graph *graph,
    const struct fwlab_hif_prepare_key *key,
    struct fwlab_hif_prepare_result *result
);
enum fwlab_c43_graph_result fwlab_c43_graph_prepare_query(
    const struct fwlab_c43_graph *graph,
    const struct fwlab_hif_prepare_key *key,
    struct fwlab_hif_prepare_result *result
);
int fwlab_c43_admit_result_valid(
    const struct fwlab_c43_admit_result *result
);
/*
 * Admit stores only the exact sanitized request and its immutable policy plan.
 * A successful start is the graph ownership LP and returns one stable ticket;
 * a repeated start cannot remint it, while query recovers it after response
 * loss.  prepared/request/result must be pairwise disjoint and outside the
 * graph arena.  Non-OK returns preserve graph state and caller output.
 */
enum fwlab_c43_graph_result fwlab_c43_graph_admit_start(
    struct fwlab_c43_graph *graph,
    const struct fwlab_hif_prepared_token *prepared,
    const struct fwlab_c43_policy_request *request,
    struct fwlab_c43_admit_result *result
);
enum fwlab_c43_graph_result fwlab_c43_graph_admit_query(
    const struct fwlab_c43_graph *graph,
    const struct fwlab_hif_prepared_token *prepared,
    const struct fwlab_c43_policy_request *request,
    struct fwlab_c43_admit_result *result
);
enum fwlab_c43_graph_result fwlab_c43_graph_step(
    struct fwlab_c43_graph *graph,
    uint32_t budget,
    struct fwlab_c43_step_result *result
);
enum fwlab_c43_graph_result fwlab_c43_graph_observer_read(
    const struct fwlab_c43_graph *graph,
    struct fwlab_c43_graph_observer *observer
);

#endif /* FWLAB_PORTABLE_C4_COMMAND_GRAPH_H */
