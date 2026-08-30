/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C42_H
#define FWLAB_C42_H

#include <stddef.h>
#include <stdint.h>

#include "c42_memory_port.h"
#include "fwlab/contracts/hif_command_port.h"

#define C42_COMPONENT_VERSION 1u
#define C42_MAX_QUEUE_PAIRS 2u
#define C42_MAX_QUEUE_DEPTH 32u
#define C42_MAX_COMMANDS 64u
#define C42_MAX_TARGET_REFS 64u
#define C42_SQE_BYTES 64u
#define C42_CQE_BYTES 16u

struct c42_controller;

enum c42_result {
    C42_OK = 0,
    C42_INVALID = 1,
    C42_WRONG_STATE = 2,
    C42_STALE = 3,
    C42_NO_EFFECT = 4,
    C42_BACKPRESSURE = 5,
    C42_NO_CAPACITY = 6,
    C42_IN_PROGRESS = 7,
    C42_TOO_LATE = 8,
    C42_FAULTED = 9,
    C42_COUNTER_EXHAUSTED = 10,
    C42_NOT_FOUND = 11,
    C42_POISONED = 12
};

enum c42_controller_phase {
    C42_CONTROLLER_COLD_NO_QUEUES = 0,
    C42_CONTROLLER_READY = 1,
    C42_CONTROLLER_RESETTING = 2,
    C42_CONTROLLER_FAULTED_RESET_REQUIRED = 3,
    C42_CONTROLLER_TEARING_DOWN = 4,
    C42_CONTROLLER_DEAD = 5
};

enum c42_queue_kind {
    C42_QUEUE_SQ = 1,
    C42_QUEUE_CQ = 2
};

enum c42_queue_life {
    C42_QUEUE_ABSENT = 0,
    C42_QUEUE_PREPARED = 1,
    C42_QUEUE_LIVE = 2,
    C42_QUEUE_PREQUIESCE = 3,
    C42_QUEUE_QUIESCING = 4,
    C42_QUEUE_TOMBSTONED = 5,
    C42_QUEUE_FAULTED_RESET_REQUIRED = 6
};

enum c42_candidate_state {
    C42_CANDIDATE_PREPARED = 1,
    C42_CANDIDATE_SCRUB_UNKNOWN = 2,
    C42_CANDIDATE_READY = 3,
    C42_CANDIDATE_ABORTING = 4,
    C42_CANDIDATE_COMMITTED = 5,
    C42_CANDIDATE_ABORTED = 6,
    C42_CANDIDATE_POISONED = 7
};

enum c42_control_kind {
    C42_CONTROL_DELETE_SQ = 1,
    C42_CONTROL_DELETE_CQ = 2,
    C42_CONTROL_RESET = 3,
    C42_CONTROL_TEARDOWN = 4
};

enum c42_control_state {
    C42_CONTROL_STARTED = 1,
    C42_CONTROL_WAITING = 2,
    C42_CONTROL_COMMITTED = 3,
    C42_CONTROL_CLEANUP_PENDING = 4,
    C42_CONTROL_RETIRED = 5,
    C42_CONTROL_POISONED = 6
};

enum c42_public_slot_state {
    C42_PUBLIC_SLOT_FREE = 0,
    C42_PUBLIC_SLOT_RESERVED = 1,
    C42_PUBLIC_SLOT_CQE_COMMITTED = 2
};

enum c42_notification_state {
    C42_NOTIFICATION_READY = 1,
    C42_NOTIFICATION_ACQUIRED = 2,
    C42_NOTIFICATION_CONSUMED = 3,
    C42_NOTIFICATION_SUPPRESSED = 4
};

struct c42_counter_seed {
    uint64_t next;
    uint64_t maximum;
};

struct c42_config {
    uint16_t version;
    uint16_t size;
    uint16_t maximum_queue_depth;
    uint16_t command_capacity;
    uint16_t target_capacity;
    uint16_t worst_case_actions;
    uint32_t safety_generation;
    uint64_t instance_nonce;
    uint64_t owner_epoch;
    uint64_t origin_domain_nonce;
    struct c42_counter_seed origin_uid;
    struct c42_counter_seed client_uid;
    struct c42_counter_seed release_uid;
    struct c42_counter_seed trace_uid;
    struct c42_counter_seed publication_uid;
    struct c42_counter_seed notification_uid;
    struct c42_counter_seed candidate_uid;
    struct c42_counter_seed target_uid;
    struct c42_counter_seed control_uid;
    struct c42_counter_seed reset_uid;
    struct c42_counter_seed teardown_uid;
    uint32_t initial_controller_epoch;
    uint32_t initial_active_generation;
    uint32_t reserved[2];
};

struct c42_providers {
    struct c42_memory_port memory;
    struct fwlab_hif_command_port command;
};

struct c42_operation_token {
    uint64_t instance_nonce;
    uint64_t uid;
    uint32_t generation;
    uint16_t kind;
    uint16_t reserved;
};

struct c42_queue_descriptor {
    uint16_t version;
    uint16_t size;
    uint16_t queue_id;
    uint16_t associated_cq_id;
    uint16_t depth;
    uint8_t kind;
    uint8_t queue_class;
    uint32_t reserved0;
    struct c42_queue_memory_cap memory;
    uint32_t reserved[4];
};

struct c42_candidate_status {
    struct c42_operation_token token;
    uint32_t state;
    uint32_t cause;
    uint32_t retry;
    uint32_t reserved[3];
};

struct c42_control_status {
    struct c42_operation_token token;
    uint32_t state;
    uint32_t cause;
    uint32_t retry;
    uint32_t reserved[3];
};

struct c42_sq_tail_event {
    uint64_t instance_nonce;
    uint32_t controller_epoch;
    uint32_t ring_generation;
    uint16_t queue_id;
    uint16_t new_tail;
    uint32_t reserved;
};

struct c42_cq_head_event {
    uint64_t instance_nonce;
    uint32_t controller_epoch;
    uint32_t ring_generation;
    uint16_t queue_id;
    uint16_t new_head;
    uint32_t reserved;
};

struct c42_target_ref {
    struct c42_operation_token token;
    struct fwlab_nvme_command_handle handle;
    struct fwlab_nvme_origin_token origin;
    struct fwlab_hif_command_ticket ticket;
};

struct c42_notification {
    struct c42_operation_token token;
    uint64_t publication_uid;
    uint32_t cq_ring_generation;
    uint16_t completion_queue_id;
    uint16_t slot_ordinal;
    uint32_t state;
    uint32_t reserved;
};

struct c42_step_result {
    uint32_t requested_budget;
    uint32_t units_executed;
    uint32_t transitions;
    uint32_t notifications_ready;
};

struct c42_queue_snapshot {
    uint32_t ring_generation;
    uint32_t mapping_generation;
    uint16_t queue_id;
    uint16_t depth;
    uint16_t host_index;
    uint16_t device_index;
    uint16_t pending_or_unacked;
    uint8_t kind;
    uint8_t life;
    uint8_t phase;
    uint8_t reserved;
};

struct c42_snapshot {
    uint16_t version;
    uint16_t size;
    uint32_t controller_epoch;
    uint64_t instance_nonce;
    uint64_t owner_epoch;
    uint32_t phase;
    uint32_t fault_cause;
    uint32_t active_commands;
    uint32_t target_refs;
    uint32_t pending_publications;
    uint32_t pending_notifications;
    struct c42_queue_snapshot sq[C42_MAX_QUEUE_PAIRS];
    struct c42_queue_snapshot cq[C42_MAX_QUEUE_PAIRS];
    uint32_t reserved[8];
};

size_t c42_arena_size(const struct c42_config *config);
enum c42_result c42_init(
    void *arena,
    size_t arena_size,
    const struct c42_config *config,
    const struct c42_providers *providers,
    struct c42_controller **controller
);

enum c42_result c42_candidate_prepare(
    struct c42_controller *controller,
    const struct c42_queue_descriptor *descriptor,
    struct c42_operation_token *token
);
enum c42_result c42_candidate_progress(
    struct c42_controller *controller,
    const struct c42_operation_token *token,
    uint32_t budget
);
enum c42_result c42_candidate_query(
    const struct c42_controller *controller,
    const struct c42_operation_token *token,
    struct c42_candidate_status *status
);
enum c42_result c42_candidate_commit(
    struct c42_controller *controller,
    const struct c42_operation_token *token
);
enum c42_result c42_candidate_abort(
    struct c42_controller *controller,
    const struct c42_operation_token *token
);
enum c42_result c42_candidate_retire(
    struct c42_controller *controller,
    const struct c42_operation_token *token
);

enum c42_result c42_enable(struct c42_controller *controller);
enum c42_result c42_sq_tail_event_apply(
    struct c42_controller *controller,
    const struct c42_sq_tail_event *event
);
enum c42_result c42_cq_head_event_apply(
    struct c42_controller *controller,
    const struct c42_cq_head_event *event
);
enum c42_result c42_step(
    struct c42_controller *controller,
    uint32_t budget,
    struct c42_step_result *result
);

enum c42_result c42_target_prepare(
    struct c42_controller *controller,
    uint16_t submission_queue_id,
    uint32_t sq_ring_generation,
    uint16_t command_id,
    struct c42_target_ref *target
);
enum c42_result c42_target_release(
    struct c42_controller *controller,
    const struct c42_operation_token *token
);

enum c42_result c42_delete_start(
    struct c42_controller *controller,
    uint8_t kind,
    uint16_t queue_id,
    struct c42_operation_token *token
);
enum c42_result c42_reset_start(
    struct c42_controller *controller,
    struct c42_operation_token *token
);
enum c42_result c42_teardown_start(
    struct c42_controller *controller,
    struct c42_operation_token *token
);
enum c42_result c42_control_progress(
    struct c42_controller *controller,
    const struct c42_operation_token *token,
    uint32_t budget
);
enum c42_result c42_control_query(
    const struct c42_controller *controller,
    const struct c42_operation_token *token,
    struct c42_control_status *status
);
enum c42_result c42_control_retire(
    struct c42_controller *controller,
    const struct c42_operation_token *token
);

enum c42_result c42_notification_acquire(
    struct c42_controller *controller,
    struct c42_notification *notification
);
enum c42_result c42_notification_consume(
    struct c42_controller *controller,
    const struct c42_operation_token *token
);
enum c42_result c42_notification_query(
    const struct c42_controller *controller,
    const struct c42_operation_token *token,
    struct c42_notification *notification
);
enum c42_result c42_notification_retire(
    struct c42_controller *controller,
    const struct c42_operation_token *token
);

enum c42_result c42_raw_snapshot_copy(
    const struct c42_controller *controller,
    const struct fwlab_nvme_command_handle *handle,
    const struct fwlab_nvme_origin_token *origin,
    uint8_t output[C42_SQE_BYTES]
);
enum c42_result c42_snapshot_read(
    const struct c42_controller *controller,
    struct c42_snapshot *snapshot
);

#endif
