/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C42_INTERNAL_H
#define FWLAB_C42_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c42.h"
#include "../c41_wire.h"

#define C42_INTERNAL_MAGIC UINT64_C(0x4334324849465631)
#define C42_QUEUE_SLOTS C42_MAX_QUEUE_PAIRS
#define C42_CANDIDATE_SLOTS (C42_MAX_QUEUE_PAIRS * 2u)
#define C42_BUSINESS_CONTROL_SLOTS 2u

enum c42_command_state {
    C42_COMMAND_FREE = 0,
    C42_COMMAND_CAPTURED = 1,
    C42_COMMAND_PREPARE_QUERY = 2,
    C42_COMMAND_PORT_RESERVED = 3,
    C42_COMMAND_ADMIT_QUERY = 4,
    C42_COMMAND_PORT_COMMITTED = 5,
    C42_COMMAND_HIF_COMMITTED = 6,
    C42_COMMAND_READY = 7,
    C42_COMMAND_LEASED = 8,
    C42_COMMAND_CONSUME_PREPARE = 9,
    C42_COMMAND_PUB_RESERVED = 10,
    C42_COMMAND_MARKER_RECONCILE = 11,
    C42_COMMAND_RELEASE_RECONCILE = 12,
    C42_COMMAND_ABORT_RECONCILE = 13,
    C42_COMMAND_ADMIT_POISON_HOLD = 14,
    C42_COMMAND_CONSUME_POISON_HOLD = 15
};

enum c42_slot_state {
    C42_SLOT_FREE = 0,
    C42_SLOT_RESERVED = 1,
    C42_SLOT_BODY_STAGED = 2,
    C42_SLOT_MARKER_VISIBLE_RECONCILE = 3,
    C42_SLOT_CQE_COMMITTED = 4
};

enum c42_reconcile_state {
    C42_RECONCILE_RESERVED = 0,
    C42_RECONCILE_PREPARED = 1,
    C42_RECONCILE_COMMIT_UNKNOWN = 2,
    C42_RECONCILE_CLEANUP_PENDING = 3,
    C42_RECONCILE_RETIRE_READY = 4
};

enum c42_notification_internal_state {
    C42_NOTIFY_RESERVED = 0,
    C42_NOTIFY_READY = 1,
    C42_NOTIFY_ACQUIRED = 2,
    C42_NOTIFY_CONSUMED = 3,
    C42_NOTIFY_SUPPRESSED = 4
};

struct c42_counter {
    uint64_t next;
    uint64_t maximum;
};

struct c42_sq_record {
    struct c42_queue_memory_cap memory;
    uint32_t ring_generation;
    uint32_t mapping_generation;
    uint32_t last_ring_generation;
    uint32_t last_mapping_generation;
    uint16_t queue_id;
    uint16_t associated_cq_id;
    uint16_t depth;
    uint16_t host_tail;
    uint16_t device_head;
    uint16_t pending;
    uint16_t frozen_tail;
    uint8_t queue_class;
    uint8_t life;
    uint8_t reserved[2];
};

struct c42_cq_slot {
    struct fwlab_nvme_origin_token origin;
    uint64_t publication_uid;
    uint64_t notification_uid;
    uint32_t cq_ring_generation;
    uint32_t source_sq_generation;
    uint32_t slot_generation;
    uint16_t source_sq_id;
    uint16_t command_id;
    uint16_t submission_queue_head;
    uint16_t ordinal;
    uint8_t phase;
    uint8_t state;
    uint8_t wire[C42_CQE_BYTES];
};

struct c42_pending_ack {
    uint16_t base_head;
    uint16_t new_head;
    uint16_t delta;
    uint8_t valid;
    uint8_t reserved;
};

struct c42_cq_record {
    struct c42_queue_memory_cap memory;
    uint32_t ring_generation;
    uint32_t mapping_generation;
    uint32_t last_ring_generation;
    uint32_t last_mapping_generation;
    uint32_t next_slot_generation;
    uint16_t queue_id;
    uint16_t depth;
    uint16_t host_head;
    uint16_t device_tail;
    uint16_t unacked_count;
    uint16_t reserved_count;
    uint8_t queue_class;
    uint8_t life;
    uint8_t device_phase;
    uint8_t create_scrub_retired;
    struct c42_pending_ack pending_ack;
    struct c42_cq_slot slots[C42_MAX_QUEUE_DEPTH];
};

struct c42_candidate_record {
    struct c42_operation_token token;
    struct c42_queue_descriptor descriptor;
    struct c42_memory_token scrub_token;
    uint32_t state;
    uint32_t cause;
    uint32_t retry;
    uint32_t controller_epoch;
    uint32_t associated_cq_ring_generation;
    uint32_t associated_cq_mapping_generation;
    uint8_t in_use;
    uint8_t scrub_started;
    uint8_t scrub_abort_started;
    uint8_t retire_started;
    uint8_t provider_retired;
    uint8_t reserved[3];
};

struct c42_candidate_tombstone {
    struct c42_operation_token token;
    uint16_t queue_id;
    uint8_t kind;
    uint8_t valid;
};

struct c42_command_record {
    struct c41_raw_command raw;
    struct fwlab_nvme_command command;
    struct fwlab_nvme_origin_token origin;
    struct fwlab_hif_prepared_token prepared;
    struct fwlab_hif_command_ticket ticket;
    struct fwlab_hif_ready_event ready;
    struct fwlab_nvme_completion_intent intent;
    struct fwlab_hif_completion_lease lease;
    uint64_t client_uid;
    uint64_t release_uid;
    uint64_t trace_cookie;
    uint64_t publication_uid;
    uint64_t notification_uid;
    uint32_t sq_ring_generation;
    uint32_t active_generation;
    uint16_t command_id;
    uint16_t sq_index;
    uint16_t cq_index;
    uint16_t cq_slot;
    uint16_t sqhd_snapshot;
    uint8_t state;
    uint8_t queue_class;
    uint8_t prepare_started;
    uint8_t admit_started;
    uint8_t consume_started;
    uint8_t release_started;
    uint8_t reserved[2];
    uint8_t raw_bytes[C42_SQE_BYTES];
    uint8_t cqe_bytes[C42_CQE_BYTES];
};

struct c42_publication_record {
    struct c42_memory_token body_token;
    struct c42_memory_token marker_token;
    uint64_t publication_uid;
    uint16_t command_index;
    uint16_t body_prefix;
    uint8_t in_use;
    uint8_t body_started;
    uint8_t marker_started;
    uint8_t marker_visible;
};

struct c42_reconcile_record {
    struct fwlab_hif_completion_lease lease;
    struct fwlab_hif_consume_token consume;
    uint64_t publication_uid;
    uint16_t command_index;
    uint8_t in_use;
    uint8_t state;
    uint8_t consume_known;
    uint8_t reserved[3];
};

struct c42_notification_record {
    struct c42_operation_token token;
    uint64_t publication_uid;
    uint32_t cq_ring_generation;
    uint32_t controller_epoch;
    uint16_t completion_queue_id;
    uint16_t slot_ordinal;
    uint8_t in_use;
    uint8_t state;
    uint8_t reserved[6];
};

struct c42_target_record {
    struct c42_target_ref value;
    uint32_t sq_ring_generation;
    uint16_t sq_index;
    uint8_t in_use;
    uint8_t reserved;
};

struct c42_control_record {
    struct c42_operation_token token;
    uint32_t state;
    uint32_t cause;
    uint32_t retry;
    uint32_t old_epoch;
    uint32_t controller_epoch;
    uint16_t queue_id;
    uint8_t kind;
    uint8_t in_use;
    uint8_t port_started;
    uint8_t memory_started;
    uint8_t reserved[6];
};

struct c42_controller {
    uint64_t magic;
    struct c42_config config;
    struct c42_providers providers;
    uint32_t controller_epoch;
    uint32_t next_active_generation;
    uint32_t next_candidate_generation;
    uint32_t next_target_generation;
    uint32_t next_control_generation;
    uint32_t next_notification_generation;
    uint32_t next_reset_generation;
    uint32_t next_teardown_generation;
    uint32_t fault_cause;
    uint8_t phase;
    uint8_t admission_paused;
    uint8_t scheduler_cursor;
    uint8_t admission_cursor;
    uint8_t publication_cursor;
    uint8_t reconcile_cursor;
    uint8_t sq_cursor;
    uint8_t ready_cursor;
    struct c42_counter origin_uid;
    struct c42_counter client_uid;
    struct c42_counter release_uid;
    struct c42_counter trace_uid;
    struct c42_counter publication_uid;
    struct c42_counter notification_uid;
    struct c42_counter candidate_uid;
    struct c42_counter target_uid;
    struct c42_counter control_uid;
    struct c42_counter reset_uid;
    struct c42_counter teardown_uid;
    struct c42_sq_record sq[C42_QUEUE_SLOTS];
    struct c42_cq_record cq[C42_QUEUE_SLOTS];
    struct c42_candidate_record candidates[C42_CANDIDATE_SLOTS];
    struct c42_candidate_tombstone candidate_tombstones[C42_CANDIDATE_SLOTS];
    struct c42_command_record commands[C42_MAX_COMMANDS];
    struct c42_publication_record publications[C42_MAX_COMMANDS];
    struct c42_reconcile_record reconciles[C42_MAX_COMMANDS];
    struct c42_notification_record notifications[C42_MAX_COMMANDS];
    struct c42_target_record targets[C42_MAX_TARGET_REFS];
    struct c42_control_record business_controls[C42_BUSINESS_CONTROL_SLOTS];
    struct c42_control_record reset_control;
    struct c42_control_record teardown_control;
};

int c42_bytes_zero(const void *value, size_t size);
int c42_handle_equal(
    const struct fwlab_nvme_command_handle *left,
    const struct fwlab_nvme_command_handle *right
);
int c42_origin_equal(
    const struct fwlab_nvme_origin_token *left,
    const struct fwlab_nvme_origin_token *right
);
int c42_operation_token_equal(
    const struct c42_operation_token *left,
    const struct c42_operation_token *right
);
int c42_controller_valid(const struct c42_controller *controller);
int c42_command_record_active(const struct c42_command_record *command);
enum c42_result c42_counter_take(
    struct c42_counter *counter,
    uint64_t *value
);
enum c42_result c42_generation_take(uint32_t *counter, uint32_t *value);
int c42_queue_index(uint16_t queue_id, uint16_t *index);
void c42_fault_controller(
    struct c42_controller *controller,
    uint32_t cause
);
void c42_fault_sq(
    struct c42_controller *controller,
    uint16_t sq_index,
    uint32_t cause
);
struct c42_command_record *c42_find_active(
    struct c42_controller *controller,
    uint16_t sq_index,
    uint32_t sq_generation,
    uint16_t command_id
);
const struct c42_command_record *c42_find_active_const(
    const struct c42_controller *controller,
    uint16_t sq_index,
    uint32_t sq_generation,
    uint16_t command_id
);
uint32_t c42_count_active(const struct c42_controller *controller);
uint32_t c42_count_targets(const struct c42_controller *controller);
uint32_t c42_count_publications(const struct c42_controller *controller);
uint32_t c42_count_notifications(const struct c42_controller *controller);

int c42_progress_admission(struct c42_controller *controller);
int c42_progress_queue_controls(struct c42_controller *controller);
int c42_poll_ready(struct c42_controller *controller);
int c42_progress_publication(struct c42_controller *controller);
int c42_progress_reconcile(struct c42_controller *controller);
void c42_try_finish_tombstones(struct c42_controller *controller);
void c42_release_command_record(
    struct c42_controller *controller,
    uint16_t command_index
);

#endif
