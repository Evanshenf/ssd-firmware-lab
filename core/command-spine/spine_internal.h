/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_SPINE_INTERNAL_H
#define FWLAB_SPINE_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/portable/host_action_program_v0.h"

#define FWLAB_SPINE_LIFECYCLE_V0_VERSION 1u
#define FWLAB_SPINE_LIFECYCLE_V0_MAX_COMMANDS 32u
#define FWLAB_SPINE_LIFECYCLE_V0_MAX_ABORTS 32u

enum fwlab_spine_command_role_v0 {
    FWLAB_SPINE_ROLE_V0_NORMAL = 1,
    FWLAB_SPINE_ROLE_V0_ABORT = 2
};

enum fwlab_spine_abort_report_kind_v0 {
    FWLAB_SPINE_ABORT_REPORT_V0_NOT_FOUND = 1,
    FWLAB_SPINE_ABORT_REPORT_V0_FOUND = 2,
    FWLAB_SPINE_ABORT_REPORT_V0_STALE = 3,
    FWLAB_SPINE_ABORT_REPORT_V0_SUPERSEDED = 4
};

enum fwlab_spine_abort_decision_v0 {
    FWLAB_SPINE_ABORT_DECISION_V0_ABORT_WON = 1,
    FWLAB_SPINE_ABORT_DECISION_V0_NOT_ABORTED = 2
};

enum fwlab_spine_profile_semantic_v0 {
    FWLAB_SPINE_SEMANTIC_V0_IDENTIFY_CONTROLLER = 1,
    FWLAB_SPINE_SEMANTIC_V0_IDENTIFY_NAMESPACE = 2,
    FWLAB_SPINE_SEMANTIC_V0_ACTIVE_NAMESPACE_LIST = 3,
    FWLAB_SPINE_SEMANTIC_V0_NAMESPACE_DESCRIPTOR_LIST = 4,
    FWLAB_SPINE_SEMANTIC_V0_SMART = 5,
    FWLAB_SPINE_SEMANTIC_V0_TINY_PAYLOAD = 6,
    FWLAB_SPINE_SEMANTIC_V0_SET_NUMBER_OF_QUEUES = 10,
    FWLAB_SPINE_SEMANTIC_V0_CREATE_CQ = 11,
    FWLAB_SPINE_SEMANTIC_V0_CREATE_SQ = 12,
    FWLAB_SPINE_SEMANTIC_V0_DELETE_CQ = 13,
    FWLAB_SPINE_SEMANTIC_V0_DELETE_SQ = 14,
    FWLAB_SPINE_SEMANTIC_V0_READ = 20,
    FWLAB_SPINE_SEMANTIC_V0_WRITE = 21,
    FWLAB_SPINE_SEMANTIC_V0_FLUSH = 22,
    FWLAB_SPINE_SEMANTIC_V0_ABORT = 30
};

enum fwlab_spine_profile_durability_v0 {
    FWLAB_SPINE_DURABILITY_V0_NONE = 0,
    FWLAB_SPINE_DURABILITY_V0_VOLATILE_ALLOWED = 1,
    FWLAB_SPINE_DURABILITY_V0_SELF = 2,
    FWLAB_SPINE_DURABILITY_V0_FRONTIER = 3
};

enum fwlab_spine_provider_outcome_v0 {
    FWLAB_SPINE_PROVIDER_V0_SUCCESS = 1,
    FWLAB_SPINE_PROVIDER_V0_CANCELLED = 2,
    FWLAB_SPINE_PROVIDER_V0_TRANSFER_FAILURE = 3,
    FWLAB_SPINE_PROVIDER_V0_MEDIA_READ = 4,
    FWLAB_SPINE_PROVIDER_V0_MEDIA_WRITE = 5,
    FWLAB_SPINE_PROVIDER_V0_RESOURCE_FAILURE = 6,
    FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE = 7,
    FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE_SIZE = 8,
    FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE_DELETE = 9
};

struct fwlab_spine_command_ticket_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nvme_command_handle command;
    struct fwlab_nvme_origin_token origin;
    uint64_t lifecycle_instance_nonce;
    uint64_t ticket_uid;
    uint64_t relation_uid;
    uint32_t execution_epoch;
    uint32_t generation;
    uint32_t reserved1[4];
};

struct fwlab_spine_profile_argument_v0;
struct fwlab_spine_abort_candidate_v0;

typedef enum fwlab_spine_result_v0
(*fwlab_spine_profile_argument_read_fn_v0)(
    void *context,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    struct fwlab_spine_profile_argument_v0 *argument
);

typedef enum fwlab_spine_result_v0
(*fwlab_spine_profile_payload_read_fn_v0)(
    void *context,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    void *output,
    size_t output_size,
    uint32_t *actual_bytes
);

typedef enum fwlab_spine_result_v0
(*fwlab_spine_profile_result_latch_fn_v0)(
    void *context,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    const struct fwlab_host_action_status_v0 *status,
    uint32_t normalized_outcome,
    uint32_t result_dword0
);

typedef enum fwlab_spine_result_v0
(*fwlab_spine_relation_candidate_read_fn_v0)(
    void *context,
    uint64_t abort_uid,
    const struct fwlab_host_action_token_v0 *resolver,
    struct fwlab_spine_abort_candidate_v0 *candidate,
    uint8_t *present
);

typedef enum fwlab_spine_result_v0
(*fwlab_spine_relation_decision_sink_fn_v0)(
    void *context,
    const struct fwlab_host_action_program_v0 *program,
    uint64_t abort_uid,
    uint32_t decision
);

struct fwlab_spine_profile_argument_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_host_action_argument_ref_v0 reference;
    uint32_t semantic;
    uint32_t namespace_id;
    uint64_t lba;
    uint32_t lba_count;
    uint32_t exact_bytes;
    uint32_t durability;
    uint32_t payload_bytes;
    uint32_t queue_id;
    uint32_t queue_entries;
    uint32_t associated_queue_id;
    uint32_t interrupt_vector;
    uint32_t requested_cq_count;
    uint32_t requested_sq_count;
    uint16_t target_sqid;
    uint16_t target_cid;
    uint32_t reserved1[4];
};

struct fwlab_spine_profile_result_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_host_action_argument_ref_v0 reference;
    struct fwlab_host_action_status_v0 status;
    uint32_t normalized_outcome;
    uint32_t result_dword0;
    uint32_t reserved1[4];
};

struct fwlab_spine_profile_binding_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_host_profile_adapter_v0 adapter;
    uint64_t adapter_instance_nonce;
    uint32_t generation;
    uint32_t reserved1;
    fwlab_spine_profile_argument_read_fn_v0 argument_read;
    fwlab_spine_profile_payload_read_fn_v0 payload_read;
    fwlab_spine_profile_result_latch_fn_v0 result_latch;
    fwlab_spine_relation_candidate_read_fn_v0 relation_source;
    void *relation_source_context;
    fwlab_spine_relation_decision_sink_fn_v0 relation_sink;
    void *relation_context;
    uint32_t reserved2[4];
};

struct fwlab_spine_abort_candidate_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint64_t abort_uid;
    struct fwlab_host_action_token_v0 resolver;
    uint32_t report;
    uint8_t target_present;
    uint8_t reserved1[3];
    struct fwlab_spine_command_ticket_v0 target;
    uint32_t reserved2[4];
};

struct fwlab_spine_epoch_status_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint64_t lifecycle_instance_nonce;
    uint32_t execution_epoch;
    uint32_t active_commands;
    uint32_t retained_intents;
    uint8_t admission_closed;
    uint8_t effectful_quiescent;
    uint8_t reserved1[6];
    uint32_t reserved2[4];
};

extern const uint64_t fwlab_spine_lifecycle_v0_symbol_owner;

size_t fwlab_spine_lifecycle_v0_arena_size(void);
size_t fwlab_spine_lifecycle_v0_arena_alignment(void);

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_init(
    void *arena,
    size_t arena_size,
    const struct fwlab_host_lifecycle_config_v0 *config,
    const struct fwlab_host_action_driver_table_v0 *drivers
);

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_admit_start(
    void *arena,
    const struct fwlab_spine_profile_binding_v0 *binding,
    const struct fwlab_host_action_program_v0 *program,
    uint32_t role,
    struct fwlab_spine_command_ticket_v0 *ticket
);

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_admit_query(
    void *arena,
    const struct fwlab_spine_profile_binding_v0 *binding,
    const struct fwlab_host_action_program_v0 *program,
    uint32_t role,
    struct fwlab_spine_command_ticket_v0 *ticket
);

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_step(
    void *arena,
    uint32_t budget,
    uint32_t *units,
    uint32_t *transitions
);

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_intent_read(
    void *arena,
    const struct fwlab_spine_command_ticket_v0 *ticket,
    struct fwlab_nvme_completion_intent *intent
);

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_epoch_close_start(
    void *arena,
    uint32_t old_execution_epoch
);

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_epoch_query(
    void *arena,
    struct fwlab_spine_epoch_status_v0 *status
);

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_fini(void *arena);

int fwlab_spine_command_ticket_v0_valid(
    const struct fwlab_spine_command_ticket_v0 *ticket
);
int fwlab_spine_command_ticket_v0_equal(
    const struct fwlab_spine_command_ticket_v0 *left,
    const struct fwlab_spine_command_ticket_v0 *right
);
int fwlab_spine_profile_binding_v0_valid(
    const struct fwlab_spine_profile_binding_v0 *binding
);
int fwlab_spine_abort_candidate_v0_valid(
    const struct fwlab_spine_abort_candidate_v0 *candidate
);

#endif /* FWLAB_SPINE_INTERNAL_H */
