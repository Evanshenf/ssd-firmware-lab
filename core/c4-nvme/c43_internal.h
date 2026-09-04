/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C43_INTERNAL_H
#define FWLAB_C43_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/portable/c4_command_graph.h"
#include "fwlab/portable/nvme_codec.h"

#define FWLAB_C43_INTERNAL_MAGIC UINT64_C(0x4334334752415031)

enum c43_action_record_state {
    C43_ACTION_RECORD_FREE = 0,
    C43_ACTION_RECORD_RESERVED = 1
};

enum c43_command_record_state {
    C43_COMMAND_RECORD_FREE = 0,
    C43_COMMAND_RECORD_PREPARED = 1,
    C43_COMMAND_RECORD_ADMITTED = 2
};

enum c43_queue_flow {
    C43_QUEUE_FLOW_NONE = 0,
    C43_QUEUE_FLOW_PREPARE_START = 1,
    C43_QUEUE_FLOW_PREPARE_QUERY = 2,
    C43_QUEUE_FLOW_DECIDE = 3,
    C43_QUEUE_FLOW_FINISH_START = 4,
    C43_QUEUE_FLOW_FINISH_QUERY = 5,
    C43_QUEUE_FLOW_APPLY_COMMIT = 6,
    C43_QUEUE_FLOW_APPLY_ABORT = 7,
    C43_QUEUE_FLOW_RETIRE = 8,
    C43_QUEUE_FLOW_DONE = 9,
    C43_QUEUE_FLOW_REJECTED_NO_EFFECT = 10,
    C43_QUEUE_FLOW_FAULT = 11
};

struct c43_queue_authority {
    uint8_t nq_state;
    uint8_t txn_active;
    uint16_t owner_slot_plus_one;
    uint8_t io_cq_present;
    uint8_t io_sq_present;
    uint8_t reserved0[2];
    struct fwlab_c43_queue_live_ref io_cq;
    struct fwlab_c43_queue_live_ref io_sq;
    struct fwlab_c43_queue_live_ref sq_associated_cq;
    uint32_t reserved1[4];
};

struct c43_queue_txn_record {
    uint32_t flow;
    uint32_t decision;
    uint32_t resolved_status;
    uint8_t resolution_valid;
    uint8_t local_effect_applied;
    uint8_t provider_owned;
    uint8_t fault_from_flow;
    uint64_t provider_generation;
    struct fwlab_c43_queue_effect_request prepare_request;
    struct fwlab_c43_queue_facts prepared_facts;
    struct fwlab_c43_queue_finish_request finish_request;
    struct fwlab_c43_queue_effect_terminal terminal;
    uint32_t reserved1[4];
};

enum c43_target_flow {
    C43_TARGET_FLOW_NONE = 0,
    C43_TARGET_FLOW_SUBMIT_START = 1,
    C43_TARGET_FLOW_QUERY = 2,
    C43_TARGET_FLOW_DONE = 3,
    C43_TARGET_FLOW_FAULT = 4
};

struct c43_target_txn_record {
    uint32_t flow;
    uint32_t outcome;
    uint32_t resolved_status;
    uint8_t resolution_valid;
    uint8_t provider_owned;
    uint8_t pin_installed;
    uint8_t fault_from_flow;
    uint16_t target_slot_plus_one;
    uint16_t reserved0;
    uint32_t reserved1;
    uint64_t provider_generation;
    struct fwlab_c43_target_request request;
    struct fwlab_c43_target_terminal terminal;
    uint32_t reserved2[4];
    uint32_t reserved_tail[40];
};

enum c43_block_flow {
    C43_BLOCK_FLOW_NONE = 0,
    C43_BLOCK_FLOW_SUBMIT_START = 1,
    C43_BLOCK_FLOW_QUERY = 2,
    C43_BLOCK_FLOW_DONE = 3,
    C43_BLOCK_FLOW_REJECTED_NO_EFFECT = 4,
    C43_BLOCK_FLOW_FAULT = 5
};

struct c43_block_txn_record {
    uint32_t flow;
    uint32_t terminal_kind;
    uint32_t resolved_status;
    uint8_t resolution_valid;
    uint8_t provider_owned;
    uint8_t fault_from_flow;
    uint8_t reserved0;
    uint32_t reserved1;
    uint32_t reserved_alignment;
    uint64_t provider_generation;
    struct fwlab_c43_block_action_request request;
    struct fwlab_c43_block_action_terminal terminal;
    uint32_t reserved2[4];
    uint32_t reserved_tail[16];
};

struct c43_counter_cursor {
    uint64_t next;
    uint64_t maximum;
    uint8_t exhausted;
    uint8_t reserved[7];
};

struct c43_action_record {
    uint64_t action_uid;
    uint32_t generation;
    uint16_t ordinal;
    uint16_t state;
    uint32_t reserved[4];
};

struct c43_command_record {
    struct fwlab_hif_prepare_key key;
    struct fwlab_hif_prepared_token prepared;
    struct fwlab_hif_command_ticket ticket;
    struct fwlab_c43_policy_request request;
    struct fwlab_c43_policy_plan plan;
    union {
        struct c43_queue_txn_record queue_txn;
        struct c43_target_txn_record target_txn;
        struct c43_block_txn_record block_txn;
    };
    uint64_t transaction_uid;
    uint64_t lease_uid;
    uint64_t consume_uid;
    uint64_t finalizer_uid;
    struct c43_action_record actions[FWLAB_C43_ACTIONS_PER_COMMAND];
    uint32_t reservation_credit_mask;
    uint8_t state;
    uint8_t in_use;
    uint16_t incoming_target_pins;
    uint32_t reserved1[4];
};

struct fwlab_c43_graph {
    uint64_t magic;
    struct fwlab_c43_graph_config config;
    struct fwlab_c43_queue_effect_port_ops queue_ops;
    struct fwlab_c43_target_resolver_port_ops target_ops;
    struct fwlab_c43_block_action_port_ops block_ops;
    struct fwlab_c43_graph_providers providers;
    struct c43_counter_cursor command_uid;
    struct c43_counter_cursor action_uid;
    struct c43_counter_cursor transaction_uid;
    struct c43_counter_cursor lease_uid;
    struct c43_counter_cursor consume_uid;
    struct c43_counter_cursor finalizer_uid;
    struct c43_command_record commands[FWLAB_C43_MAX_COMMANDS];
    struct c43_queue_authority queue_authority;
    uint32_t next_service_slot;
    uint32_t reserved_scheduler[3];
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
int c43_ranges_overlap(
    const void *left,
    size_t left_size,
    const void *right,
    size_t right_size
);
int c43_profile_is_fixed(const struct fwlab_nvme_profile *profile);
void c43_profile_fixed(struct fwlab_nvme_profile *profile);
int c43_prepare_key_valid(
    const struct fwlab_c43_graph_config *config,
    const struct fwlab_hif_prepare_key *key
);
int c43_reservation_state_valid(const struct fwlab_c43_graph *graph);
int c43_phase4_state_valid(const struct fwlab_c43_graph *graph);
int c43_phase4_step(
    struct fwlab_c43_graph *graph,
    uint32_t *transitions
);
int c43_graph_valid(const struct fwlab_c43_graph *graph);
int c43_semantic_status_valid(uint32_t status);
int c43_witness_mask_valid(uint32_t mask);

#endif /* FWLAB_C43_INTERNAL_H */
