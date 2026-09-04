/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PORTABLE_HOST_ACTION_PROGRAM_V0_H
#define FWLAB_PORTABLE_HOST_ACTION_PROGRAM_V0_H

#include <stdint.h>

#include "fwlab/portable/nvme_types.h"

#define FWLAB_HOST_ACTION_PROGRAM_V0_VERSION 1u
#define FWLAB_HOST_ACTION_V0_MAX_ACTIONS 8u
#define FWLAB_HOST_ACTION_V0_KIND_COUNT 9u
#define FWLAB_HOST_ACTION_V0_WITNESS_ALL UINT32_C(0x01ff)

#define FWLAB_HOST_ACTION_TOKEN_V0_TAG UINT32_C(0x48414354)
#define FWLAB_COMPLETION_LEASE_V0_TAG UINT32_C(0x43504c53)

enum fwlab_spine_result_v0 {
    FWLAB_SPINE_V0_OK = 0,
    FWLAB_SPINE_V0_INVALID = 1,
    FWLAB_SPINE_V0_WRONG_STATE = 2,
    FWLAB_SPINE_V0_STALE = 3,
    FWLAB_SPINE_V0_NO_CAPACITY = 4,
    FWLAB_SPINE_V0_IN_PROGRESS = 5,
    FWLAB_SPINE_V0_SUPERSEDED = 6,
    FWLAB_SPINE_V0_POISONED = 7,
    FWLAB_SPINE_V0_COUNTER_EXHAUSTED = 8,
    FWLAB_SPINE_V0_QUARANTINED = 9
};

enum fwlab_host_action_kind_v0 {
    FWLAB_HOST_ACTION_V0_PAYLOAD_FILL = 1,
    FWLAB_HOST_ACTION_V0_QUEUE_EFFECT = 2,
    FWLAB_HOST_ACTION_V0_TARGET_RESOLVE = 3,
    FWLAB_HOST_ACTION_V0_DMA_IN = 4,
    FWLAB_HOST_ACTION_V0_DMA_OUT = 5,
    FWLAB_HOST_ACTION_V0_BLOCK_READ = 6,
    FWLAB_HOST_ACTION_V0_BLOCK_WRITE = 7,
    FWLAB_HOST_ACTION_V0_BLOCK_FLUSH = 8,
    FWLAB_HOST_ACTION_V0_BLOCK_TRIM = 9
};

/* Number-of-Queues is an adapter-owned QUEUE_EFFECT, never zero-action. */
enum fwlab_host_action_witness_v0 {
    FWLAB_HOST_WITNESS_V0_PAYLOAD_READY = 1u << 0,
    FWLAB_HOST_WITNESS_V0_QUEUE_EFFECT = 1u << 1,
    FWLAB_HOST_WITNESS_V0_TARGET_RESOLVED = 1u << 2,
    FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE = 1u << 3,
    FWLAB_HOST_WITNESS_V0_DMA_OUT_COMPLETE = 1u << 4,
    FWLAB_HOST_WITNESS_V0_BLOCK_READ_COMPLETE = 1u << 5,
    FWLAB_HOST_WITNESS_V0_BLOCK_WRITE_COMPLETE = 1u << 6,
    FWLAB_HOST_WITNESS_V0_BLOCK_FLUSH_COMPLETE = 1u << 7,
    FWLAB_HOST_WITNESS_V0_BLOCK_TRIM_COMPLETE = 1u << 8
};

enum fwlab_host_action_disposition_v0 {
    FWLAB_HOST_ACTION_V0_ACCEPTED = 1,
    FWLAB_HOST_ACTION_V0_BACKPRESSURE = 2,
    FWLAB_HOST_ACTION_V0_REJECTED = 3
};

enum fwlab_host_action_state_v0 {
    FWLAB_HOST_ACTION_V0_STATE_ACCEPTED = 1,
    FWLAB_HOST_ACTION_V0_STATE_TERMINAL = 2,
    FWLAB_HOST_ACTION_V0_STATE_DRAINING = 3,
    FWLAB_HOST_ACTION_V0_STATE_DRAINED = 4,
    FWLAB_HOST_ACTION_V0_STATE_RETIRED = 5,
    FWLAB_HOST_ACTION_V0_STATE_QUARANTINED = 6
};

enum fwlab_host_action_terminal_kind_v0 {
    FWLAB_HOST_ACTION_V0_SUCCEEDED = 1,
    FWLAB_HOST_ACTION_V0_CANCELLED = 2,
    FWLAB_HOST_ACTION_V0_FAILED = 3,
    FWLAB_HOST_ACTION_V0_TERMINAL_QUARANTINED = 4
};

enum fwlab_host_action_effect_v0 {
    FWLAB_HOST_ACTION_V0_EFFECT_NONE = 0,
    FWLAB_HOST_ACTION_V0_EFFECT_FULL = 1,
    FWLAB_HOST_ACTION_V0_EFFECT_EXACT_PREFIX = 2,
    FWLAB_HOST_ACTION_V0_EFFECT_UNKNOWN_PREFIX = 3
};

struct fwlab_uid_range_v0 {
    uint64_t next;
    uint64_t maximum;
};

struct fwlab_host_lifecycle_config_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint64_t lifecycle_instance_nonce;
    uint32_t execution_epoch;
    uint32_t generation;
    uint16_t command_capacity;
    uint16_t actions_per_command;
    uint32_t reserved1;
    struct fwlab_uid_range_v0 command_uid;
    struct fwlab_uid_range_v0 action_uid;
    struct fwlab_uid_range_v0 abort_uid;
    struct fwlab_uid_range_v0 completion_lease_uid;
    uint32_t reserved2[4];
};

struct fwlab_host_action_argument_ref_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint64_t adapter_instance_nonce;
    uint64_t argument_uid;
    uint32_t generation;
    uint16_t ordinal;
    uint16_t kind;
    uint32_t reserved1[2];
};

struct fwlab_host_completion_recipe_ref_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint64_t adapter_instance_nonce;
    uint64_t recipe_uid;
    uint32_t generation;
    uint32_t reserved1[3];
};

struct fwlab_host_action_desc_v0 {
    uint16_t version;
    uint16_t size;
    uint16_t ordinal;
    uint16_t kind;
    uint32_t dependency_mask;
    uint32_t required_witness_mask;
    uint32_t produced_witness_mask;
    uint32_t reserved0;
    struct fwlab_host_action_argument_ref_v0 argument;
    uint32_t reserved1[4];
};

struct fwlab_host_action_program_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nvme_command_handle command;
    struct fwlab_nvme_origin_token origin;
    uint64_t program_uid;
    uint32_t program_generation;
    uint16_t action_count;
    uint16_t reserved1;
    uint32_t completion_required_witness_mask;
    uint32_t reserved2;
    struct fwlab_host_completion_recipe_ref_v0 completion_recipe;
    struct fwlab_host_action_desc_v0
        action[FWLAB_HOST_ACTION_V0_MAX_ACTIONS];
    uint32_t reserved3[4];
};

struct fwlab_host_action_token_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t type_tag;
    struct fwlab_nvme_command_handle command;
    struct fwlab_nvme_origin_token origin;
    uint64_t action_uid;
    uint32_t generation;
    uint16_t ordinal;
    uint16_t kind;
    uint32_t reserved[2];
};

struct fwlab_host_action_submit_result_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_host_action_token_v0 token;
    uint32_t disposition;
    uint32_t fault_domain;
    uint32_t fault_code;
    uint32_t reserved1[3];
};

struct fwlab_host_action_status_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_host_action_token_v0 token;
    uint32_t state;
    uint32_t terminal_kind;
    uint32_t produced_witness_mask;
    uint32_t effect;
    uint32_t units_completed;
    uint32_t fault_domain;
    uint32_t fault_code;
    uint32_t reserved1[3];
};

struct fwlab_completion_lease_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t type_tag;
    struct fwlab_nvme_command_handle command;
    struct fwlab_nvme_origin_token origin;
    uint64_t issuer_nonce;
    uint64_t lease_uid;
    uint32_t intent_generation;
    uint32_t lease_generation;
    uint32_t reserved[4];
};

typedef enum fwlab_spine_result_v0
(*fwlab_host_action_submit_fn_v0)(
    void *context,
    const struct fwlab_host_action_token_v0 *token,
    const struct fwlab_host_action_argument_ref_v0 *argument,
    struct fwlab_host_action_submit_result_v0 *result
);

typedef enum fwlab_spine_result_v0
(*fwlab_host_action_query_fn_v0)(
    void *context,
    const struct fwlab_host_action_token_v0 *token,
    struct fwlab_host_action_status_v0 *status
);

typedef enum fwlab_spine_result_v0
(*fwlab_host_action_control_fn_v0)(
    void *context,
    const struct fwlab_host_action_token_v0 *token
);

typedef enum fwlab_spine_result_v0
(*fwlab_host_action_retire_query_fn_v0)(
    void *context,
    const struct fwlab_host_action_token_v0 *token,
    struct fwlab_host_action_status_v0 *status
);

typedef enum fwlab_spine_result_v0
(*fwlab_host_action_epoch_close_fn_v0)(
    void *context,
    uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch
);

typedef enum fwlab_spine_result_v0
(*fwlab_host_action_epoch_quiescent_fn_v0)(
    void *context,
    uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch,
    uint8_t *quiescent
);

struct fwlab_host_action_driver_ops_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    fwlab_host_action_submit_fn_v0 submit;
    fwlab_host_action_query_fn_v0 query;
    fwlab_host_action_control_fn_v0 cancel;
    fwlab_host_action_control_fn_v0 retire_start;
    fwlab_host_action_retire_query_fn_v0 retire_query;
    fwlab_host_action_epoch_close_fn_v0 epoch_close;
    fwlab_host_action_epoch_quiescent_fn_v0 epoch_quiescent;
    uint32_t reserved1[4];
};

struct fwlab_host_action_driver_binding_v0 {
    uint16_t version;
    uint16_t size;
    uint16_t kind;
    uint16_t reserved0;
    uint64_t generation;
    const struct fwlab_host_action_driver_ops_v0 *ops;
    void *context;
    uint32_t reserved1[4];
};

struct fwlab_host_action_driver_table_v0 {
    uint16_t version;
    uint16_t size;
    uint16_t entry_count;
    uint16_t reserved0;
    struct fwlab_host_action_driver_binding_v0
        entry[FWLAB_HOST_ACTION_V0_KIND_COUNT];
    uint32_t reserved1[4];
};

typedef enum fwlab_spine_result_v0
(*fwlab_host_profile_plan_fn_v0)(
    void *context,
    const struct fwlab_nvme_command *command,
    struct fwlab_host_action_program_v0 *program
);

typedef enum fwlab_spine_result_v0
(*fwlab_host_profile_complete_fn_v0)(
    void *context,
    const struct fwlab_host_action_program_v0 *program,
    const struct fwlab_host_action_status_v0 *status,
    uint16_t status_count,
    struct fwlab_nvme_completion_intent *intent
);

typedef enum fwlab_spine_result_v0
(*fwlab_host_profile_retire_fn_v0)(
    void *context,
    const struct fwlab_host_action_program_v0 *program
);

struct fwlab_host_profile_ops_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    fwlab_host_profile_plan_fn_v0 plan;
    fwlab_host_profile_complete_fn_v0 complete;
    fwlab_host_profile_retire_fn_v0 retire;
    uint32_t reserved1[4];
};

struct fwlab_host_profile_adapter_v0 {
    const struct fwlab_host_profile_ops_v0 *ops;
    void *context;
    uint64_t generation;
};

int fwlab_host_lifecycle_config_v0_valid(
    const struct fwlab_host_lifecycle_config_v0 *config
);
int fwlab_host_action_argument_ref_v0_valid(
    const struct fwlab_host_action_argument_ref_v0 *argument
);
int fwlab_host_completion_recipe_ref_v0_valid(
    const struct fwlab_host_completion_recipe_ref_v0 *recipe
);
int fwlab_host_action_desc_v0_valid(
    const struct fwlab_host_action_desc_v0 *action
);
int fwlab_host_action_program_v0_valid(
    const struct fwlab_host_action_program_v0 *program
);
int fwlab_host_action_token_v0_valid(
    const struct fwlab_host_action_token_v0 *token
);
int fwlab_host_action_submit_result_v0_valid(
    const struct fwlab_host_action_submit_result_v0 *result
);
int fwlab_host_action_status_v0_valid(
    const struct fwlab_host_action_status_v0 *status
);
int fwlab_completion_lease_v0_valid(
    const struct fwlab_completion_lease_v0 *lease
);
int fwlab_host_completion_intent_v0_valid_for_program(
    const struct fwlab_host_action_program_v0 *program,
    const struct fwlab_host_action_status_v0 *status,
    uint16_t status_count,
    const struct fwlab_nvme_completion_intent *intent
);
int fwlab_host_action_driver_ops_v0_valid(
    const struct fwlab_host_action_driver_ops_v0 *ops
);
int fwlab_host_action_driver_binding_v0_valid(
    const struct fwlab_host_action_driver_binding_v0 *binding
);
int fwlab_host_action_driver_table_v0_valid(
    const struct fwlab_host_action_driver_table_v0 *table
);
int fwlab_host_profile_ops_v0_valid(
    const struct fwlab_host_profile_ops_v0 *ops
);
int fwlab_host_profile_adapter_v0_valid(
    const struct fwlab_host_profile_adapter_v0 *adapter
);

#endif /* FWLAB_PORTABLE_HOST_ACTION_PROGRAM_V0_H */
