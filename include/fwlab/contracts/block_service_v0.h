/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CONTRACTS_BLOCK_SERVICE_V0_H
#define FWLAB_CONTRACTS_BLOCK_SERVICE_V0_H

#include <stdint.h>

#include "fwlab/contracts/controller_buffer_v0.h"
#include "fwlab/portable/host_action_program_v0.h"

#define FWLAB_BLOCK_SERVICE_V0_VERSION 1u
#define FWLAB_BLOCK_OP_TOKEN_V0_TAG UINT32_C(0x424f5054)

enum fwlab_block_operation_v0 {
    FWLAB_BLOCK_V0_READ = 1,
    FWLAB_BLOCK_V0_WRITE = 2,
    FWLAB_BLOCK_V0_FLUSH = 3,
    FWLAB_BLOCK_V0_TRIM = 4
};

enum fwlab_block_durability_v0 {
    FWLAB_BLOCK_V0_DURABILITY_NONE = 0,
    FWLAB_BLOCK_V0_DURABILITY_VOLATILE_ALLOWED = 1,
    FWLAB_BLOCK_V0_DURABILITY_SELF = 2,
    FWLAB_BLOCK_V0_DURABILITY_FRONTIER = 3
};

enum fwlab_block_state_v0 {
    FWLAB_BLOCK_V0_STATE_ACCEPTED = 1,
    FWLAB_BLOCK_V0_STATE_TERMINAL = 2,
    FWLAB_BLOCK_V0_STATE_DRAINING = 3,
    FWLAB_BLOCK_V0_STATE_DRAINED = 4,
    FWLAB_BLOCK_V0_STATE_RETIRED = 5,
    FWLAB_BLOCK_V0_STATE_QUARANTINED = 6
};

enum fwlab_block_outcome_v0 {
    FWLAB_BLOCK_V0_SUCCEEDED = 1,
    FWLAB_BLOCK_V0_CANCELLED = 2,
    FWLAB_BLOCK_V0_FAILED = 3,
    FWLAB_BLOCK_V0_OUTCOME_QUARANTINED = 4
};

enum fwlab_block_effect_v0 {
    FWLAB_BLOCK_V0_EFFECT_NONE = 0,
    FWLAB_BLOCK_V0_EFFECT_FULL = 1,
    FWLAB_BLOCK_V0_EFFECT_EXACT_PREFIX = 2,
    FWLAB_BLOCK_V0_EFFECT_UNKNOWN_PREFIX = 3
};

enum fwlab_block_durability_witness_v0 {
    FWLAB_BLOCK_V0_WITNESS_NONE = 0,
    FWLAB_BLOCK_V0_WITNESS_VOLATILE = 1,
    FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE = 2,
    FWLAB_BLOCK_V0_WITNESS_FRONTIER_DURABLE = 3
};

struct fwlab_block_namespace_ref_v0 {
    uint64_t word[2];
};

struct fwlab_block_frontier_ref_v0 {
    uint64_t word[2];
};

/* One aggregate token only; private FTL/NFC child tokens never cross here. */
struct fwlab_block_op_token_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t type_tag;
    struct fwlab_host_action_token_v0 action;
    uint64_t provider_nonce;
    uint32_t generation;
    uint32_t reserved[3];
};

struct fwlab_block_request_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_block_op_token_v0 operation_token;
    struct fwlab_block_namespace_ref_v0 namespace_ref;
    uint64_t lba;
    uint32_t lba_count;
    uint32_t operation;
    uint32_t durability;
    uint8_t buffer_present;
    uint8_t reserved1[3];
    struct fwlab_controller_buffer_lease_v0 buffer;
    struct fwlab_controller_buffer_span_v0 buffer_span;
    uint32_t reserved2[4];
};

struct fwlab_block_submit_result_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_block_op_token_v0 operation_token;
    uint32_t disposition;
    uint32_t fault_domain;
    uint32_t fault_code;
    uint32_t reserved1[3];
};

struct fwlab_block_status_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_block_op_token_v0 operation_token;
    uint32_t state;
    uint32_t outcome;
    uint32_t effect;
    uint32_t completed_lbas;
    uint32_t data_bytes;
    uint32_t durability_witness;
    struct fwlab_block_frontier_ref_v0 frontier;
    uint32_t fault_domain;
    uint32_t fault_code;
    uint32_t reserved1[4];
};

struct fwlab_block_epoch_status_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint64_t lifecycle_instance_nonce;
    uint32_t execution_epoch;
    uint32_t aggregate_operations;
    uint8_t admission_closed;
    uint8_t quiescent;
    uint8_t reserved1[6];
    uint64_t aggregate_proof[2];
    uint32_t reserved2[4];
};

typedef enum fwlab_spine_result_v0
(*fwlab_block_submit_fn_v0)(
    void *context,
    const struct fwlab_block_request_v0 *request,
    struct fwlab_block_submit_result_v0 *result
);

typedef enum fwlab_spine_result_v0
(*fwlab_block_query_fn_v0)(
    void *context,
    const struct fwlab_block_op_token_v0 *operation,
    struct fwlab_block_status_v0 *status
);

typedef enum fwlab_spine_result_v0
(*fwlab_block_control_fn_v0)(
    void *context,
    const struct fwlab_block_op_token_v0 *operation
);

typedef enum fwlab_spine_result_v0
(*fwlab_block_retire_query_fn_v0)(
    void *context,
    const struct fwlab_block_op_token_v0 *operation,
    struct fwlab_block_status_v0 *status
);

typedef enum fwlab_spine_result_v0
(*fwlab_block_epoch_close_fn_v0)(
    void *context,
    uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch
);

typedef enum fwlab_spine_result_v0
(*fwlab_block_epoch_quiescent_fn_v0)(
    void *context,
    uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch,
    struct fwlab_block_epoch_status_v0 *status
);

struct fwlab_block_service_ops_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    fwlab_block_submit_fn_v0 submit;
    fwlab_block_query_fn_v0 query;
    fwlab_block_control_fn_v0 cancel;
    fwlab_block_control_fn_v0 retire_start;
    fwlab_block_retire_query_fn_v0 retire_query;
    fwlab_block_epoch_close_fn_v0 epoch_close;
    fwlab_block_epoch_quiescent_fn_v0 epoch_quiescent;
    uint32_t reserved1[4];
};

struct fwlab_block_service_v0 {
    const struct fwlab_block_service_ops_v0 *ops;
    void *context;
    uint64_t provider_nonce;
    uint32_t generation;
    uint32_t reserved[3];
};

int fwlab_block_op_token_v0_valid(
    const struct fwlab_block_op_token_v0 *operation
);
int fwlab_block_request_v0_valid(
    const struct fwlab_block_request_v0 *request
);
int fwlab_block_submit_result_v0_valid(
    const struct fwlab_block_submit_result_v0 *result
);
int fwlab_block_submit_result_v0_matches_request(
    const struct fwlab_block_submit_result_v0 *result,
    const struct fwlab_block_request_v0 *request
);
int fwlab_block_status_v0_valid(
    const struct fwlab_block_status_v0 *status
);
int fwlab_block_status_v0_matches_request(
    const struct fwlab_block_status_v0 *status,
    const struct fwlab_block_request_v0 *request
);
int fwlab_block_epoch_status_v0_valid(
    const struct fwlab_block_epoch_status_v0 *status
);
int fwlab_block_service_ops_v0_valid(
    const struct fwlab_block_service_ops_v0 *ops
);
int fwlab_block_service_v0_valid(
    const struct fwlab_block_service_v0 *service
);

#endif /* FWLAB_CONTRACTS_BLOCK_SERVICE_V0_H */
