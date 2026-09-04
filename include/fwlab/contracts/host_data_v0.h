/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CONTRACTS_HOST_DATA_V0_H
#define FWLAB_CONTRACTS_HOST_DATA_V0_H

#include <stdint.h>

#include "fwlab/contracts/controller_buffer_v0.h"
#include "fwlab/portable/host_action_program_v0.h"

#define FWLAB_HOST_DATA_V0_VERSION 1u
#define FWLAB_HOST_DMA_AUTHORITY_V0_TAG UINT32_C(0x48444152)
#define FWLAB_DMA_OP_TOKEN_V0_TAG UINT32_C(0x444d4f50)

enum fwlab_host_data_direction_v0 {
    FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER = 1,
    FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST = 2
};

enum fwlab_dma_state_v0 {
    FWLAB_DMA_V0_STATE_ACCEPTED = 1,
    FWLAB_DMA_V0_STATE_TERMINAL = 2,
    FWLAB_DMA_V0_STATE_DRAINING = 3,
    FWLAB_DMA_V0_STATE_DRAINED = 4,
    FWLAB_DMA_V0_STATE_RETIRED = 5,
    FWLAB_DMA_V0_STATE_QUARANTINED = 6
};

enum fwlab_dma_terminal_kind_v0 {
    FWLAB_DMA_V0_SUCCEEDED = 1,
    FWLAB_DMA_V0_CANCELLED = 2,
    FWLAB_DMA_V0_FAILED = 3,
    FWLAB_DMA_V0_TERMINAL_QUARANTINED = 4
};

struct fwlab_host_dma_authority_ref_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t type_tag;
    uint64_t issuer_nonce;
    uint64_t authority_uid;
    uint32_t generation;
    uint32_t exact_bytes;
    uint8_t direction;
    uint8_t reserved0[7];
    uint32_t reserved1[2];
};

struct fwlab_host_dma_mint_request_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nvme_command_handle command;
    struct fwlab_nvme_origin_token origin;
    uint64_t client_uid;
    uint32_t execution_epoch;
    uint32_t exact_bytes;
    uint8_t direction;
    uint8_t reserved1[3];
    uint32_t reserved2[4];
};

struct fwlab_dma_op_token_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t type_tag;
    struct fwlab_host_action_token_v0 action;
    uint64_t issuer_nonce;
    uint64_t operation_uid;
    uint32_t generation;
    uint32_t reserved[3];
};

struct fwlab_dma_request_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_dma_op_token_v0 operation;
    struct fwlab_host_dma_authority_ref_v0 authority;
    struct fwlab_controller_buffer_lease_v0 buffer;
    struct fwlab_controller_buffer_span_v0 span;
    uint32_t execution_epoch;
    uint32_t exact_bytes;
    uint8_t direction;
    uint8_t reserved1[3];
    uint32_t reserved2[4];
};

/*
 * Before any DMA effect, submit must resolve authority in the HIF-private
 * registry and match its command, origin, execution epoch, direction/bytes
 * and current buffer generation to this immutable request.
 */

struct fwlab_dma_submit_result_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_dma_op_token_v0 operation;
    uint32_t disposition;
    uint32_t fault_domain;
    uint32_t fault_code;
    uint32_t reserved1[3];
};

struct fwlab_dma_status_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_dma_op_token_v0 operation;
    uint32_t state;
    uint32_t terminal_kind;
    uint32_t effect;
    uint32_t bytes_completed;
    uint32_t fault_domain;
    uint32_t fault_code;
    uint32_t reserved1[4];
};

struct fwlab_host_data_epoch_status_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint64_t lifecycle_instance_nonce;
    uint32_t execution_epoch;
    uint32_t authority_refs;
    uint32_t buffer_leases;
    uint32_t dma_operations;
    uint8_t admission_closed;
    uint8_t quiescent;
    uint8_t reserved1[6];
    uint32_t reserved2[4];
};

typedef enum fwlab_spine_result_v0
(*fwlab_host_dma_authority_mint_fn_v0)(
    void *context,
    const struct fwlab_host_dma_mint_request_v0 *request,
    struct fwlab_host_dma_authority_ref_v0 *authority
);

typedef enum fwlab_spine_result_v0
(*fwlab_host_dma_authority_release_fn_v0)(
    void *context,
    const struct fwlab_host_dma_authority_ref_v0 *authority
);

/* token_reserve is synchronous and has no DMA or Host-memory effect. */
typedef enum fwlab_spine_result_v0
(*fwlab_dma_token_reserve_fn_v0)(
    void *context,
    const struct fwlab_host_action_token_v0 *action,
    struct fwlab_dma_op_token_v0 *operation
);

typedef enum fwlab_spine_result_v0
(*fwlab_dma_submit_fn_v0)(
    void *context,
    const struct fwlab_dma_request_v0 *request,
    struct fwlab_dma_submit_result_v0 *result
);

typedef enum fwlab_spine_result_v0
(*fwlab_dma_query_fn_v0)(
    void *context,
    const struct fwlab_dma_op_token_v0 *operation,
    struct fwlab_dma_status_v0 *status
);

typedef enum fwlab_spine_result_v0
(*fwlab_dma_control_fn_v0)(
    void *context,
    const struct fwlab_dma_op_token_v0 *operation
);

typedef enum fwlab_spine_result_v0
(*fwlab_dma_retire_query_fn_v0)(
    void *context,
    const struct fwlab_dma_op_token_v0 *operation,
    struct fwlab_dma_status_v0 *status
);

typedef enum fwlab_spine_result_v0
(*fwlab_host_data_epoch_close_fn_v0)(
    void *context,
    uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch
);

typedef enum fwlab_spine_result_v0
(*fwlab_host_data_epoch_quiescent_fn_v0)(
    void *context,
    uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch,
    struct fwlab_host_data_epoch_status_v0 *status
);

struct fwlab_host_data_ops_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    fwlab_host_dma_authority_mint_fn_v0 authority_mint;
    fwlab_host_dma_authority_release_fn_v0 authority_release;
    fwlab_dma_token_reserve_fn_v0 token_reserve;
    fwlab_dma_submit_fn_v0 submit;
    fwlab_dma_query_fn_v0 query;
    fwlab_dma_control_fn_v0 cancel;
    fwlab_dma_control_fn_v0 retire_start;
    fwlab_dma_retire_query_fn_v0 retire_query;
    fwlab_host_data_epoch_close_fn_v0 epoch_close;
    fwlab_host_data_epoch_quiescent_fn_v0 epoch_quiescent;
    uint32_t reserved1[4];
};

struct fwlab_host_data_port_v0 {
    const struct fwlab_host_data_ops_v0 *ops;
    void *context;
    struct fwlab_controller_buffer_port_v0 buffer;
    uint64_t authority_issuer_nonce;
    uint64_t dma_issuer_nonce;
    uint32_t generation;
    uint32_t reserved[3];
};

/*
 * Revoke/reset code receives only this narrow view.  It deliberately has no
 * authority_mint, buffer acquire, token_reserve or DMA submit operation.
 */
struct fwlab_host_data_reconcile_ops_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    fwlab_host_data_epoch_close_fn_v0 epoch_close;
    fwlab_dma_query_fn_v0 dma_query;
    fwlab_dma_control_fn_v0 dma_cancel;
    fwlab_dma_control_fn_v0 dma_retire_start;
    fwlab_dma_retire_query_fn_v0 dma_retire_query;
    fwlab_controller_buffer_read_fn_v0 buffer_read;
    fwlab_controller_buffer_write_fn_v0 buffer_write;
    fwlab_controller_buffer_copy_fn_v0 buffer_copy;
    fwlab_controller_buffer_release_fn_v0 buffer_release;
    fwlab_host_dma_authority_release_fn_v0 authority_release;
    fwlab_host_data_epoch_quiescent_fn_v0 epoch_quiescent;
    uint32_t reserved1[4];
};

struct fwlab_host_data_reconcile_port_v0 {
    const struct fwlab_host_data_reconcile_ops_v0 *ops;
    void *context;
    uint64_t authority_issuer_nonce;
    uint64_t buffer_issuer_nonce;
    uint64_t dma_issuer_nonce;
    uint32_t generation;
    uint32_t reserved[3];
};

int fwlab_host_dma_authority_ref_v0_valid(
    const struct fwlab_host_dma_authority_ref_v0 *authority
);
int fwlab_host_dma_mint_request_v0_valid(
    const struct fwlab_host_dma_mint_request_v0 *request
);
int fwlab_dma_op_token_v0_valid(
    const struct fwlab_dma_op_token_v0 *operation
);
int fwlab_dma_request_v0_valid(
    const struct fwlab_dma_request_v0 *request
);
int fwlab_dma_submit_result_v0_valid(
    const struct fwlab_dma_submit_result_v0 *result
);
int fwlab_dma_submit_result_v0_matches_request(
    const struct fwlab_dma_submit_result_v0 *result,
    const struct fwlab_dma_request_v0 *request
);
int fwlab_dma_status_v0_valid(
    const struct fwlab_dma_status_v0 *status
);
int fwlab_dma_status_v0_matches_request(
    const struct fwlab_dma_status_v0 *status,
    const struct fwlab_dma_request_v0 *request
);
int fwlab_host_data_epoch_status_v0_valid(
    const struct fwlab_host_data_epoch_status_v0 *status
);
int fwlab_host_data_ops_v0_valid(
    const struct fwlab_host_data_ops_v0 *ops
);
int fwlab_host_data_port_v0_valid(
    const struct fwlab_host_data_port_v0 *port
);
int fwlab_host_data_reconcile_ops_v0_valid(
    const struct fwlab_host_data_reconcile_ops_v0 *ops
);
int fwlab_host_data_reconcile_port_v0_valid(
    const struct fwlab_host_data_reconcile_port_v0 *port
);

#endif /* FWLAB_CONTRACTS_HOST_DATA_V0_H */
