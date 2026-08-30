/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C42_REFERENCE_H
#define FWLAB_C42_REFERENCE_H

#include <stddef.h>
#include <stdint.h>

#define C42_REFERENCE_CQE_BYTES 16u
#define C42_REFERENCE_SQE_BYTES 64u
#define C42_REFERENCE_FAMILIES 12u
#define C42_REFERENCE_MAX_ACTIONS 8u

enum c42_reference_family {
    C42_REF_F01_CREATE = 0,
    C42_REF_F02_BATCH = 1,
    C42_REF_F03_CAPTURE = 2,
    C42_REF_F04_INVALID_CID = 3,
    C42_REF_F05_DELAYED = 4,
    C42_REF_F06_PHASE = 5,
    C42_REF_F07_FULL = 6,
    C42_REF_F08_IDENTITY = 7,
    C42_REF_F09_PUBLICATION = 8,
    C42_REF_F10_DELETE = 9,
    C42_REF_F11_RESET = 10,
    C42_REF_F12_ISOLATION = 11
};

enum c42_reference_ordinal {
    C42_REF_HOST_SQ_EVENT = 0,
    C42_REF_HOST_MUTATE = 1,
    C42_REF_CAPTURE_STEP = 2,
    C42_REF_PORT_STEP = 3,
    C42_REF_CQ_RESERVE = 4,
    C42_REF_PUBLICATION_STEP = 5,
    C42_REF_CQ_HEAD_EVENT = 6,
    C42_REF_TARGET_STEP = 7,
    C42_REF_QUEUE_CONTROL_STEP = 8,
    C42_REF_MAP_EVENT = 9,
    C42_REF_RESET_STEP = 10,
    C42_REF_TEARDOWN_STEP = 11,
    C42_REF_NOTIFY_STEP = 12
};

enum c42_reference_action_id {
    C42_REF_CREATE_PREPARE = 1,
    C42_REF_CREATE_PROGRESS,
    C42_REF_CREATE_COMMIT,
    C42_REF_BATCH_SUBMIT,
    C42_REF_BATCH_ACTIVE,
    C42_REF_BATCH_PUBLISH,
    C42_REF_CAPTURE_SUBMIT,
    C42_REF_CAPTURE_ONCE,
    C42_REF_CAPTURE_MUTATE,
    C42_REF_CAPTURE_BACKPRESSURE,
    C42_REF_CAPTURE_PUBLISH,
    C42_REF_INVALID_TAIL,
    C42_REF_STALE_TAIL,
    C42_REF_DUPLICATE_CID,
    C42_REF_DELAY_SUBMIT,
    C42_REF_DELAY_ACTIVE,
    C42_REF_DELAY_PUBLISH,
    C42_REF_PHASE_FIRST,
    C42_REF_PHASE_ACK_ONE,
    C42_REF_PHASE_SECOND,
    C42_REF_PHASE_ACK_TWO,
    C42_REF_PHASE_THIRD,
    C42_REF_FULL_FILL,
    C42_REF_FULL_SUBMIT,
    C42_REF_FULL_PROBE,
    C42_REF_FULL_ACK_RESUME,
    C42_REF_IDENTITY_MARKER,
    C42_REF_IDENTITY_TARGET,
    C42_REF_IDENTITY_DUPLICATE,
    C42_REF_IDENTITY_CROSS_COMMIT,
    C42_REF_IDENTITY_TARGET_RELEASE,
    C42_REF_PUBLICATION_SUBMIT,
    C42_REF_PUBLICATION_BODY_PREFIX,
    C42_REF_PUBLICATION_BODY_FULL,
    C42_REF_PUBLICATION_MARKER_UNKNOWN,
    C42_REF_PUBLICATION_MARKER_FULL,
    C42_REF_PUBLICATION_CROSS_COMMIT,
    C42_REF_DELETE_PUBLISH,
    C42_REF_DELETE_SQ,
    C42_REF_DELETE_CQ_PROBE,
    C42_REF_DELETE_RECREATE_PROBE,
    C42_REF_RESET_ACTIVE,
    C42_REF_RESET_START,
    C42_REF_RESET_COLD,
    C42_REF_ISOLATION_LEFT,
    C42_REF_ISOLATION_RIGHT,
    C42_REF_ISOLATION_CROSS_REJECT,
    C42_REF_ADMISSION_POISON,
    C42_REF_ACK_NONCOMMITTED,
    C42_REF_TARGET_GENERATION_MISMATCH,
    C42_REF_DELETE_PENDING_PROBE,
    C42_REF_SQHD_DELAY
};

struct c42_reference_action {
    const char *name;
    uint16_t prerequisites;
    uint16_t forbidden;
    uint8_t id;
    uint8_t ordinal;
};

/*
 * Compact semantic state used by the independent reference/BFS.  Token values
 * are deliberately normalized into validity/equality facts, while raw SQE and
 * CQE bytes remain exact.
 */
struct c42_reference_state {
    uint32_t family;
    uint32_t phase;
    uint32_t controller_epoch;
    uint32_t fault_cause;
    uint32_t last_result;
    uint32_t capture_count;
    uint32_t acquire_count;
    uint32_t order_mask;
    uint32_t event_count;
    uint32_t sq_ring_generation[2];
    uint32_t sq_mapping_generation[2];
    uint32_t cq_ring_generation[2];
    uint32_t cq_mapping_generation[2];
    uint16_t sq_host_tail[2];
    uint16_t sq_device_head[2];
    uint16_t sq_pending[2];
    uint16_t cq_host_head[2];
    uint16_t cq_device_tail[2];
    uint16_t cq_unacked[2];
    uint16_t cq_reserved[2];
    uint8_t sq_life[2];
    uint8_t cq_life[2];
    uint8_t cq_phase[2];
    uint8_t candidate_state;
    uint8_t control_state;
    uint8_t command_count;
    uint8_t active_identity_count;
    uint8_t command_state[2];
    uint16_t command_id[2];
    uint16_t command_sqhd[2];
    uint8_t command_identity_ok[2];
    uint8_t slot_state[2];
    uint8_t body_prefix[2];
    uint8_t marker_visible[2];
    uint8_t reconcile_state[2];
    uint8_t target_count;
    uint8_t target_identity_ok;
    uint8_t notification_count;
    uint8_t notification_state;
    uint8_t port_records;
    uint8_t port_admitted;
    uint8_t port_ready;
    uint8_t port_leased;
    uint8_t port_consume_prepared;
    uint8_t port_consume_committed;
    uint8_t port_retired;
    uint8_t other_active;
    uint8_t cross_effect;
    uint8_t raw_valid[2];
    uint8_t cqe_valid[2];
    uint8_t media_cqe_valid[2];
    uint8_t raw[2][C42_REFERENCE_SQE_BYTES];
    uint8_t cqe[2][C42_REFERENCE_CQE_BYTES];
    uint8_t media_cqe[2][C42_REFERENCE_CQE_BYTES];
    uint32_t reserved[4];
};

void c42_reference_build_cqe(
    uint32_t result_dword0,
    uint16_t submission_queue_head,
    uint16_t submission_queue_id,
    uint16_t command_id,
    uint8_t phase,
    uint8_t status_code,
    uint8_t status_code_type,
    uint8_t command_retry_delay,
    uint8_t more,
    uint8_t do_not_retry,
    uint8_t output[C42_REFERENCE_CQE_BYTES]
);
int c42_reference_bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size
);
const char *c42_reference_family_name(uint32_t family);
uint32_t c42_reference_action_count(uint32_t family);
const struct c42_reference_action *c42_reference_action_at(
    uint32_t family,
    uint32_t index
);
int c42_reference_initial(
    uint32_t family,
    struct c42_reference_state *state
);
int c42_reference_transition(
    const struct c42_reference_state *before,
    uint8_t action_id,
    struct c42_reference_state *after
);
int c42_reference_state_equal(
    const struct c42_reference_state *left,
    const struct c42_reference_state *right
);

#endif
