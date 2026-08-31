/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_reference.h"

#include <string.h>

static void put_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

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
    uint8_t output[C42_REFERENCE_CQE_BYTES])
{
    uint16_t status;
    size_t index;

    for (index = 0; index < C42_REFERENCE_CQE_BYTES; ++index) {
        output[index] = 0;
    }
    status = (uint16_t)(phase & 1u) |
             (uint16_t)((uint16_t)status_code << 1) |
             (uint16_t)((uint16_t)(status_code_type & 7u) << 9) |
             (uint16_t)((uint16_t)(command_retry_delay & 3u) << 12) |
             (uint16_t)((uint16_t)(more & 1u) << 14) |
             (uint16_t)((uint16_t)(do_not_retry & 1u) << 15);
    put_u32(output, result_dword0);
    put_u16(output + 8, submission_queue_head);
    put_u16(output + 10, submission_queue_id);
    put_u16(output + 12, command_id);
    put_u16(output + 14, status);
}

int c42_reference_bytes_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size)
{
    size_t index;

    if (left == NULL || right == NULL) {
        return 0;
    }
    for (index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

#define REF_BIT(index) ((uint16_t)(1u << (index)))
#define REF_ACTION(label, dependencies, exclusions, order, action_id) \
    {label, dependencies, exclusions, action_id, order}

static const struct c42_reference_action ref_f01[] = {
    REF_ACTION("prepare-cq1", 0, 0, C42_REF_MAP_EVENT,
               C42_REF_CREATE_PREPARE),
    REF_ACTION("commit-before-scrub", REF_BIT(0), REF_BIT(2),
               C42_REF_QUEUE_CONTROL_STEP, C42_REF_CREATE_EARLY_COMMIT),
    REF_ACTION("scrub-unknown", REF_BIT(0), 0, C42_REF_MAP_EVENT,
               C42_REF_CREATE_PROGRESS),
    REF_ACTION("commit-while-unknown", REF_BIT(2), REF_BIT(4),
               C42_REF_QUEUE_CONTROL_STEP, C42_REF_CREATE_UNKNOWN_COMMIT),
    REF_ACTION("scrub-proof", REF_BIT(2), 0, C42_REF_MAP_EVENT,
               C42_REF_CREATE_RECOVER),
    REF_ACTION("commit-cq1", REF_BIT(4), 0, C42_REF_QUEUE_CONTROL_STEP,
               C42_REF_CREATE_COMMIT),
};
static const struct c42_reference_action ref_f02[] = {
    REF_ACTION("batch-tail", 0, 0, C42_REF_HOST_SQ_EVENT,
               C42_REF_BATCH_SUBMIT),
    REF_ACTION("admit-two", REF_BIT(0), 0, C42_REF_PORT_STEP,
               C42_REF_BATCH_ACTIVE),
    REF_ACTION("publish-two", REF_BIT(1), 0, C42_REF_PUBLICATION_STEP,
               C42_REF_BATCH_PUBLISH),
};
static const struct c42_reference_action ref_f03[] = {
    REF_ACTION("tail", 0, 0, C42_REF_HOST_SQ_EVENT,
               C42_REF_CAPTURE_SUBMIT),
    REF_ACTION("capture", REF_BIT(0), 0, C42_REF_CAPTURE_STEP,
               C42_REF_CAPTURE_ONCE),
    REF_ACTION("host-mutate", REF_BIT(1), 0, C42_REF_HOST_MUTATE,
               C42_REF_CAPTURE_MUTATE),
    REF_ACTION("backpressure", REF_BIT(2), 0, C42_REF_PORT_STEP,
               C42_REF_CAPTURE_BACKPRESSURE),
    REF_ACTION("prepare-reserved", REF_BIT(3), 0, C42_REF_PORT_STEP,
               C42_REF_CAPTURE_PORT_RESERVED),
    REF_ACTION("admit-query", REF_BIT(4), 0, C42_REF_PORT_STEP,
               C42_REF_CAPTURE_ADMIT_QUERY),
    REF_ACTION("admit-query-retry", REF_BIT(5), 0, C42_REF_PORT_STEP,
               C42_REF_CAPTURE_ADMIT_QUERY),
    REF_ACTION("admit-committed", REF_BIT(6), 0, C42_REF_PORT_STEP,
               C42_REF_CAPTURE_PORT_COMMITTED),
    REF_ACTION("hif-head-active", REF_BIT(7), 0, C42_REF_PORT_STEP,
               C42_REF_CAPTURE_HIF_COMMITTED),
    REF_ACTION("publish-stable", REF_BIT(8), 0, C42_REF_PUBLICATION_STEP,
               C42_REF_CAPTURE_PUBLISH),
};
static const struct c42_reference_action ref_f04[] = {
    REF_ACTION("invalid-tail", 0,
               REF_BIT(1) | REF_BIT(2) | REF_BIT(3) | REF_BIT(4),
               C42_REF_HOST_SQ_EVENT, C42_REF_INVALID_TAIL),
    REF_ACTION("stale-tail", 0,
               REF_BIT(0) | REF_BIT(2) | REF_BIT(3) | REF_BIT(4),
               C42_REF_HOST_SQ_EVENT, C42_REF_STALE_TAIL),
    REF_ACTION("duplicate-cid", 0,
               REF_BIT(0) | REF_BIT(1) | REF_BIT(3) | REF_BIT(4),
               C42_REF_CAPTURE_STEP, C42_REF_DUPLICATE_CID),
    REF_ACTION("admission-poison", 0,
               REF_BIT(0) | REF_BIT(1) | REF_BIT(2) | REF_BIT(4),
               C42_REF_PORT_STEP, C42_REF_ADMISSION_POISON),
    REF_ACTION("ack-noncommitted", 0,
               REF_BIT(0) | REF_BIT(1) | REF_BIT(2) | REF_BIT(3),
               C42_REF_CQ_HEAD_EVENT, C42_REF_ACK_NONCOMMITTED),
};
static const struct c42_reference_action ref_f05[] = {
    REF_ACTION("submit-two", 0, REF_BIT(3), C42_REF_HOST_SQ_EVENT,
               C42_REF_DELAY_SUBMIT),
    REF_ACTION("admit-two", REF_BIT(0), 0, C42_REF_PORT_STEP,
               C42_REF_DELAY_ACTIVE),
    REF_ACTION("reverse-publish", REF_BIT(1), 0,
               C42_REF_PUBLICATION_STEP, C42_REF_DELAY_PUBLISH),
    REF_ACTION("reserve-sqhd-delay", 0,
               REF_BIT(0) | REF_BIT(1) | REF_BIT(2),
               C42_REF_CQ_RESERVE, C42_REF_SQHD_DELAY),
};
static const struct c42_reference_action ref_f06[] = {
    REF_ACTION("first", 0, 0, C42_REF_PUBLICATION_STEP,
               C42_REF_PHASE_FIRST),
    REF_ACTION("ack-one", REF_BIT(0), 0, C42_REF_CQ_HEAD_EVENT,
               C42_REF_PHASE_ACK_ONE),
    REF_ACTION("second", REF_BIT(1), 0, C42_REF_PUBLICATION_STEP,
               C42_REF_PHASE_SECOND),
    REF_ACTION("ack-two", REF_BIT(2), 0, C42_REF_CQ_HEAD_EVENT,
               C42_REF_PHASE_ACK_TWO),
    REF_ACTION("third", REF_BIT(3), 0, C42_REF_PUBLICATION_STEP,
               C42_REF_PHASE_THIRD),
};
static const struct c42_reference_action ref_f07[] = {
    REF_ACTION("fill", 0, 0, C42_REF_PUBLICATION_STEP,
               C42_REF_FULL_FILL),
    REF_ACTION("submit-blocked", REF_BIT(0), 0, C42_REF_HOST_SQ_EVENT,
               C42_REF_FULL_SUBMIT),
    REF_ACTION("probe-full", REF_BIT(1), 0, C42_REF_CQ_RESERVE,
               C42_REF_FULL_PROBE),
    REF_ACTION("ack-resume", REF_BIT(2), 0, C42_REF_CQ_HEAD_EVENT,
               C42_REF_FULL_ACK_RESUME),
};
static const struct c42_reference_action ref_f08[] = {
    REF_ACTION("old-active", 0, REF_BIT(9), C42_REF_PORT_STEP,
               C42_REF_IDENTITY_OLD_ACTIVE),
    REF_ACTION("old-target", REF_BIT(0), 0, C42_REF_TARGET_STEP,
               C42_REF_IDENTITY_OLD_TARGET),
    REF_ACTION("drive-marker", REF_BIT(1), 0,
               C42_REF_PUBLICATION_STEP,
               C42_REF_IDENTITY_DRIVE_MARKER),
    REF_ACTION("cross-commit", REF_BIT(2), 0,
               C42_REF_PUBLICATION_STEP, C42_REF_IDENTITY_CROSS_COMMIT),
    REF_ACTION("same-cid-tail", REF_BIT(3), 0,
               C42_REF_HOST_SQ_EVENT, C42_REF_IDENTITY_REUSE_TAIL),
    REF_ACTION("same-cid-new-active", REF_BIT(4), 0,
               C42_REF_PORT_STEP, C42_REF_IDENTITY_REUSE_ACTIVE),
    REF_ACTION("new-target", REF_BIT(5), 0, C42_REF_TARGET_STEP,
               C42_REF_IDENTITY_NEW_TARGET),
    REF_ACTION("release-old", REF_BIT(6), 0, C42_REF_TARGET_STEP,
               C42_REF_IDENTITY_RELEASE_OLD),
    REF_ACTION("release-new", REF_BIT(7), 0, C42_REF_TARGET_STEP,
               C42_REF_IDENTITY_RELEASE_NEW),
    REF_ACTION("generation-mismatch", 0,
               REF_BIT(0) | REF_BIT(1) | REF_BIT(2) | REF_BIT(3) |
               REF_BIT(4) | REF_BIT(5) | REF_BIT(6) | REF_BIT(7) |
               REF_BIT(8),
               C42_REF_TARGET_STEP, C42_REF_TARGET_GENERATION_MISMATCH),
};
static const struct c42_reference_action ref_f09[] = {
    REF_ACTION("reserve", 0, 0, C42_REF_CQ_RESERVE,
               C42_REF_PUBLICATION_SUBMIT),
    REF_ACTION("body-prefix", REF_BIT(0), 0, C42_REF_PUBLICATION_STEP,
               C42_REF_PUBLICATION_BODY_PREFIX),
    REF_ACTION("body-full", REF_BIT(1), 0, C42_REF_PUBLICATION_STEP,
               C42_REF_PUBLICATION_BODY_FULL),
    REF_ACTION("marker-unknown", REF_BIT(2), 0,
               C42_REF_PUBLICATION_STEP,
               C42_REF_PUBLICATION_MARKER_UNKNOWN),
    REF_ACTION("marker-query", REF_BIT(3), 0,
               C42_REF_PUBLICATION_STEP,
               C42_REF_PUBLICATION_MARKER_FULL),
    REF_ACTION("cross-commit", REF_BIT(4), 0,
               C42_REF_PUBLICATION_STEP,
               C42_REF_PUBLICATION_CROSS_COMMIT),
};
static const struct c42_reference_action ref_f10[] = {
    REF_ACTION("publish-unacked", 0, REF_BIT(4), C42_REF_PUBLICATION_STEP,
               C42_REF_DELETE_PUBLISH),
    REF_ACTION("delete-sq", REF_BIT(0), REF_BIT(2),
               C42_REF_QUEUE_CONTROL_STEP, C42_REF_DELETE_SQ),
    REF_ACTION("delete-cq-probe", REF_BIT(0), REF_BIT(1) | REF_BIT(3),
               C42_REF_QUEUE_CONTROL_STEP, C42_REF_DELETE_CQ_PROBE),
    REF_ACTION("recreate-probe", REF_BIT(1), REF_BIT(2), C42_REF_MAP_EVENT,
               C42_REF_DELETE_RECREATE_PROBE),
    REF_ACTION("delete-pending-probe", 0,
               REF_BIT(0) | REF_BIT(1) | REF_BIT(2) | REF_BIT(3),
               C42_REF_QUEUE_CONTROL_STEP, C42_REF_DELETE_PENDING_PROBE),
};
static const struct c42_reference_action ref_f11[] = {
    REF_ACTION("active", 0, REF_BIT(3) | REF_BIT(4), C42_REF_PORT_STEP,
               C42_REF_RESET_ACTIVE),
    REF_ACTION("reset-lp", REF_BIT(0), 0, C42_REF_RESET_STEP,
               C42_REF_RESET_START),
    REF_ACTION("reset-cold", REF_BIT(1), 0, C42_REF_RESET_STEP,
               C42_REF_RESET_COLD),
    REF_ACTION("retire-unknown", 0,
               REF_BIT(0) | REF_BIT(1) | REF_BIT(2), C42_REF_MAP_EVENT,
               C42_REF_RESET_RETIRE_UNKNOWN),
    REF_ACTION("candidate-reset-lp", REF_BIT(3),
               REF_BIT(0) | REF_BIT(1) | REF_BIT(2), C42_REF_RESET_STEP,
               C42_REF_RESET_CANDIDATE_LP),
};
static const struct c42_reference_action ref_f12[] = {
    REF_ACTION("left-active", 0, 0, C42_REF_PORT_STEP,
               C42_REF_ISOLATION_LEFT),
    REF_ACTION("right-active", 0, 0, C42_REF_PORT_STEP,
               C42_REF_ISOLATION_RIGHT),
    REF_ACTION("cross-reject", REF_BIT(0) | REF_BIT(1), 0,
               C42_REF_TARGET_STEP, C42_REF_ISOLATION_CROSS_REJECT),
};

struct reference_family_spec {
    const char *name;
    const struct c42_reference_action *actions;
    uint8_t count;
    uint8_t depth;
};

#define REF_FAMILY(label, values, queue_depth) \
    {label, values, (uint8_t)(sizeof(values) / sizeof(values[0])), queue_depth}

static const struct reference_family_spec reference_families[] = {
    REF_FAMILY("F01-create-contract", ref_f01, 4),
    REF_FAMILY("F02-sq-single-batch-wrap", ref_f02, 3),
    REF_FAMILY("F03-capture-backpressure", ref_f03, 4),
    REF_FAMILY("F04-sq-invalid-cid", ref_f04, 4),
    REF_FAMILY("F05-delayed-out-of-order", ref_f05, 4),
    REF_FAMILY("F06-cq-phase-ack", ref_f06, 2),
    REF_FAMILY("F07-cq-full-lease", ref_f07, 2),
    REF_FAMILY("F08-cid-reuse-target", ref_f08, 4),
    REF_FAMILY("F09-publication-faults", ref_f09, 4),
    REF_FAMILY("F10-delete-tombstone", ref_f10, 4),
    REF_FAMILY("F11-reset-teardown", ref_f11, 4),
    REF_FAMILY("F12-isolation", ref_f12, 4),
};

static void reference_sqe(
    uint16_t command_id,
    uint8_t output[C42_REFERENCE_SQE_BYTES])
{
    size_t index;

    for (index = 0; index < C42_REFERENCE_SQE_BYTES; ++index) {
        output[index] = 0;
    }
    output[0] = 0x02;
    put_u16(output + 2, command_id);
    put_u32(output + 4, 1);
    put_u32(output + 40, command_id);
}

static void reference_command(
    struct c42_reference_state *state,
    uint8_t index,
    uint16_t command_id,
    uint8_t command_state)
{
    state->command_id[index] = command_id;
    state->command_state[index] = command_state;
    state->command_identity_ok[index] = 1;
}

static void reference_raw(
    struct c42_reference_state *state,
    uint8_t index,
    uint16_t command_id)
{
    state->raw_valid[index] = 1;
    reference_sqe(command_id, state->raw[index]);
}

static void reference_cqe(
    struct c42_reference_state *state,
    uint8_t index,
    uint16_t command_id,
    uint16_t sqhd,
    uint8_t phase)
{
    state->cqe_valid[index] = 1;
    c42_reference_build_cqe(
        0, sqhd, 0, command_id, phase, 0, 0, 0, 0, 0,
        state->cqe[index]
    );
}

static void reference_media_full(
    struct c42_reference_state *state,
    uint8_t index)
{
    state->media_cqe_valid[index] = state->cqe_valid[index];
    memcpy(
        state->media_cqe[index], state->cqe[index],
        sizeof(state->media_cqe[index])
    );
}

static void reference_published_port(
    struct c42_reference_state *state,
    uint8_t count)
{
    state->port_records = count;
    state->port_admitted = count;
    state->port_ready = count;
    state->port_leased = count;
    state->port_consume_prepared = count;
    state->port_consume_committed = count;
    state->port_retired = count;
}

const char *c42_reference_family_name(uint32_t family)
{
    return family < C42_REFERENCE_FAMILIES ?
           reference_families[family].name : NULL;
}

uint32_t c42_reference_action_count(uint32_t family)
{
    return family < C42_REFERENCE_FAMILIES ?
           reference_families[family].count : 0;
}

const struct c42_reference_action *c42_reference_action_at(
    uint32_t family,
    uint32_t index)
{
    if (family >= C42_REFERENCE_FAMILIES ||
        index >= reference_families[family].count) {
        return NULL;
    }
    return &reference_families[family].actions[index];
}

int c42_reference_initial(
    uint32_t family,
    struct c42_reference_state *state)
{
    if (state == NULL || family >= C42_REFERENCE_FAMILIES) {
        return 0;
    }
    memset(state, 0, sizeof(*state));
    state->family = family;
    state->phase = 1;
    state->controller_epoch = 11;
    state->sq_ring_generation[0] = 1;
    state->sq_mapping_generation[0] = 1;
    state->cq_ring_generation[0] = 1;
    state->cq_mapping_generation[0] = 1;
    state->sq_life[0] = 2;
    state->cq_life[0] = 2;
    state->cq_phase[0] = 1;
    return 1;
}

int c42_reference_transition(
    const struct c42_reference_state *before,
    uint8_t action_id,
    struct c42_reference_state *after)
{
    if (before == NULL || after == NULL ||
        before->family >= C42_REFERENCE_FAMILIES) {
        return 0;
    }
    *after = *before;
    after->last_result = 0;
    switch (action_id) {
    case C42_REF_CREATE_PREPARE:
        after->cq_life[1] = 1;
        after->candidate_state = 1;
        break;
    case C42_REF_CREATE_EARLY_COMMIT:
        after->last_result = 2;
        break;
    case C42_REF_CREATE_PROGRESS:
        after->candidate_state = 2;
        break;
    case C42_REF_CREATE_UNKNOWN_COMMIT:
        after->last_result = 2;
        break;
    case C42_REF_CREATE_RECOVER:
        after->candidate_state = 3;
        break;
    case C42_REF_CREATE_COMMIT:
        after->cq_life[1] = 2;
        after->cq_ring_generation[1] = 1;
        after->cq_mapping_generation[1] = 1;
        after->cq_phase[1] = 1;
        after->candidate_state = 9;
        break;
    case C42_REF_BATCH_SUBMIT:
        after->sq_host_tail[0] = 2;
        after->sq_pending[0] = 2;
        break;
    case C42_REF_BATCH_ACTIVE:
        after->sq_device_head[0] = 2;
        after->sq_pending[0] = 0;
        after->capture_count = 2;
        after->command_count = 2;
        after->active_identity_count = 2;
        after->notification_count = 2;
        reference_command(after, 0, 301, 6);
        reference_command(after, 1, 302, 6);
        reference_raw(after, 0, 301);
        reference_raw(after, 1, 302);
        after->port_records = 2;
        after->port_admitted = 2;
        break;
    case C42_REF_BATCH_PUBLISH:
        after->command_count = 0;
        after->active_identity_count = 0;
        memset(after->command_state, 0, sizeof(after->command_state));
        after->acquire_count = 2;
        after->cq_device_tail[0] = 2;
        after->cq_unacked[0] = 2;
        after->slot_state[0] = 2;
        after->slot_state[1] = 2;
        after->notification_count = 2;
        after->notification_state = 1;
        reference_cqe(after, 0, 301, 2, 1);
        reference_cqe(after, 1, 302, 2, 1);
        reference_published_port(after, 2);
        after->order_mask = UINT32_C(0x1ff);
        break;
    case C42_REF_CAPTURE_SUBMIT:
        after->sq_host_tail[0] = 1;
        after->sq_pending[0] = 1;
        break;
    case C42_REF_CAPTURE_ONCE:
        after->capture_count = 1;
        after->command_count = 1;
        after->notification_count = 1;
        reference_command(after, 0, 303, 1);
        break;
    case C42_REF_CAPTURE_MUTATE:
        break;
    case C42_REF_CAPTURE_BACKPRESSURE:
        after->command_state[0] = 1;
        break;
    case C42_REF_CAPTURE_PORT_RESERVED:
        after->command_state[0] = 3;
        after->command_identity_ok[0] = 1;
        after->port_records = 1;
        break;
    case C42_REF_CAPTURE_ADMIT_QUERY:
        after->command_state[0] = 4;
        after->command_identity_ok[0] = 1;
        break;
    case C42_REF_CAPTURE_PORT_COMMITTED:
        after->command_state[0] = 5;
        after->command_identity_ok[0] = 1;
        after->port_admitted = 1;
        break;
    case C42_REF_CAPTURE_HIF_COMMITTED:
        after->sq_device_head[0] = 1;
        after->sq_pending[0] = 0;
        after->command_state[0] = 6;
        after->active_identity_count = 1;
        reference_raw(after, 0, 303);
        break;
    case C42_REF_CAPTURE_PUBLISH:
        after->sq_device_head[0] = 1;
        after->sq_pending[0] = 0;
        after->command_count = 0;
        after->active_identity_count = 0;
        memset(after->command_state, 0, sizeof(after->command_state));
        memset(after->command_sqhd, 0, sizeof(after->command_sqhd));
        reference_raw(after, 0, 303);
        after->acquire_count = 1;
        after->cq_device_tail[0] = 1;
        after->cq_unacked[0] = 1;
        after->slot_state[0] = 2;
        after->notification_count = 1;
        after->notification_state = 1;
        reference_cqe(after, 0, 303, 1, 1);
        reference_published_port(after, 1);
        after->order_mask = UINT32_C(0x1ff);
        break;
    case C42_REF_INVALID_TAIL:
        after->phase = 3;
        after->fault_cause = 1;
        after->sq_life[0] = 6;
        after->last_result = 9;
        break;
    case C42_REF_STALE_TAIL:
        after->last_result = 3;
        break;
    case C42_REF_DUPLICATE_CID:
        after->phase = 3;
        after->fault_cause = 3;
        after->sq_life[0] = 6;
        after->sq_host_tail[0] = 2;
        after->sq_device_head[0] = 1;
        after->sq_pending[0] = 1;
        after->capture_count = 2;
        after->command_count = 1;
        after->active_identity_count = 1;
        after->notification_count = 1;
        reference_command(after, 0, 304, 6);
        reference_raw(after, 0, 304);
        after->port_records = 1;
        after->port_admitted = 1;
        after->last_result = 9;
        break;
    case C42_REF_DELAY_SUBMIT:
        after->sq_host_tail[0] = 2;
        after->sq_pending[0] = 2;
        break;
    case C42_REF_DELAY_ACTIVE:
        after->sq_device_head[0] = 2;
        after->sq_pending[0] = 0;
        after->capture_count = 2;
        after->command_count = 2;
        after->active_identity_count = 2;
        after->notification_count = 2;
        reference_command(after, 0, 305, 6);
        reference_command(after, 1, 306, 6);
        reference_raw(after, 0, 305);
        reference_raw(after, 1, 306);
        after->port_records = 2;
        after->port_admitted = 2;
        break;
    case C42_REF_DELAY_PUBLISH:
        after->command_count = 0;
        after->active_identity_count = 0;
        memset(after->command_state, 0, sizeof(after->command_state));
        memset(after->command_sqhd, 0, sizeof(after->command_sqhd));
        after->acquire_count = 2;
        after->cq_device_tail[0] = 2;
        after->cq_unacked[0] = 2;
        after->slot_state[0] = 2;
        after->slot_state[1] = 2;
        after->notification_count = 2;
        after->notification_state = 1;
        reference_cqe(after, 0, 306, 2, 1);
        reference_cqe(after, 1, 305, 2, 1);
        reference_published_port(after, 2);
        after->order_mask = UINT32_C(0x1ff);
        break;
    case C42_REF_PHASE_FIRST:
        after->sq_host_tail[0] = 1;
        after->sq_device_head[0] = 1;
        after->capture_count = 1;
        after->acquire_count = 1;
        after->cq_device_tail[0] = 1;
        after->cq_unacked[0] = 1;
        after->slot_state[0] = 2;
        after->notification_count = 1;
        after->notification_state = 1;
        reference_cqe(after, 0, 307, 1, 1);
        reference_published_port(after, 1);
        after->order_mask = UINT32_C(0x1ff);
        break;
    case C42_REF_PHASE_ACK_ONE:
        after->cq_host_head[0] = 1;
        after->cq_unacked[0] = 0;
        after->slot_state[0] = 0;
        after->cqe_valid[0] = 0;
        memset(after->cqe[0], 0, sizeof(after->cqe[0]));
        break;
    case C42_REF_PHASE_SECOND:
        after->sq_host_tail[0] = 0;
        after->sq_device_head[0] = 0;
        after->capture_count = 2;
        after->acquire_count = 2;
        after->cq_device_tail[0] = 0;
        after->cq_unacked[0] = 1;
        after->slot_state[1] = 2;
        after->cq_phase[0] = 0;
        after->notification_count = 2;
        after->notification_state = 1;
        reference_cqe(after, 1, 308, 0, 1);
        reference_published_port(after, 1);
        after->order_mask = UINT32_C(0x1ff);
        break;
    case C42_REF_PHASE_ACK_TWO:
        after->cq_host_head[0] = 0;
        after->cq_unacked[0] = 0;
        after->slot_state[1] = 0;
        after->cqe_valid[1] = 0;
        memset(after->cqe[1], 0, sizeof(after->cqe[1]));
        break;
    case C42_REF_PHASE_THIRD:
        after->sq_host_tail[0] = 1;
        after->sq_device_head[0] = 1;
        after->capture_count = 3;
        after->acquire_count = 3;
        after->cq_device_tail[0] = 1;
        after->cq_unacked[0] = 1;
        after->slot_state[0] = 2;
        after->notification_count = 3;
        after->notification_state = 1;
        reference_cqe(after, 0, 316, 1, 0);
        reference_published_port(after, 1);
        after->order_mask = UINT32_C(0x1ff);
        break;
    case C42_REF_FULL_FILL:
        after->sq_host_tail[0] = 1;
        after->sq_device_head[0] = 1;
        after->capture_count = 1;
        after->acquire_count = 1;
        after->cq_device_tail[0] = 1;
        after->cq_unacked[0] = 1;
        after->slot_state[0] = 2;
        after->notification_count = 1;
        after->notification_state = 1;
        reference_cqe(after, 0, 309, 1, 1);
        reference_published_port(after, 1);
        after->order_mask = UINT32_C(0x1ff);
        break;
    case C42_REF_FULL_SUBMIT:
        after->sq_host_tail[0] = 0;
        after->sq_device_head[0] = 0;
        after->capture_count = 2;
        after->command_count = 1;
        after->active_identity_count = 1;
        after->notification_count = 2;
        reference_command(after, 0, 310, 7);
        reference_raw(after, 0, 310);
        after->port_records = 1;
        after->port_admitted = 1;
        after->port_ready = 1;
        after->port_leased = 0;
        after->port_consume_prepared = 0;
        after->port_consume_committed = 0;
        after->port_retired = 0;
        break;
    case C42_REF_FULL_PROBE:
        break;
    case C42_REF_FULL_ACK_RESUME:
        after->cq_host_head[0] = 1;
        after->cq_device_tail[0] = 0;
        after->cq_unacked[0] = 1;
        after->cq_phase[0] = 0;
        after->acquire_count = 2;
        after->command_count = 0;
        after->active_identity_count = 0;
        memset(after->command_state, 0, sizeof(after->command_state));
        memset(after->command_sqhd, 0, sizeof(after->command_sqhd));
        after->notification_count = 2;
        after->notification_state = 1;
        after->slot_state[0] = 0;
        after->slot_state[1] = 2;
        after->cqe_valid[0] = 0;
        memset(after->cqe[0], 0, sizeof(after->cqe[0]));
        reference_cqe(after, 1, 310, 0, 1);
        reference_published_port(after, 1);
        after->order_mask = UINT32_C(0x1ff);
        break;
    case C42_REF_IDENTITY_MARKER:
        after->sq_host_tail[0] = 1;
        after->sq_device_head[0] = 1;
        after->capture_count = 1;
        after->acquire_count = 1;
        after->command_count = 1;
        after->active_identity_count = 1;
        after->notification_count = 1;
        reference_command(after, 0, 311, 11);
        reference_raw(after, 0, 311);
        after->command_sqhd[0] = 1;
        after->cq_reserved[0] = 1;
        after->slot_state[0] = 11;
        after->body_prefix[0] = 15;
        after->marker_visible[0] = 1;
        after->reconcile_state[0] = 1;
        after->port_records = 1;
        after->port_admitted = 1;
        after->port_ready = 1;
        after->port_leased = 1;
        after->port_consume_prepared = 1;
        reference_cqe(after, 0, 311, 1, 1);
        reference_media_full(after, 0);
        break;
    case C42_REF_IDENTITY_OLD_ACTIVE:
        after->sq_host_tail[0] = 1;
        after->sq_device_head[0] = 1;
        after->capture_count = 1;
        after->command_count = 1;
        after->active_identity_count = 1;
        after->notification_count = 1;
        reference_command(after, 0, 311, 6);
        reference_raw(after, 0, 311);
        after->port_records = 1;
        after->port_admitted = 1;
        break;
    case C42_REF_IDENTITY_OLD_TARGET:
        after->target_count = 1;
        after->target_identity_ok = 1;
        break;
    case C42_REF_IDENTITY_DRIVE_MARKER:
        after->acquire_count = 1;
        after->command_state[0] = 11;
        after->command_sqhd[0] = 1;
        after->cq_reserved[0] = 1;
        after->slot_state[0] = 11;
        after->body_prefix[0] = 15;
        after->marker_visible[0] = 1;
        after->reconcile_state[0] = 1;
        after->port_ready = 1;
        after->port_leased = 1;
        after->port_consume_prepared = 1;
        reference_cqe(after, 0, 311, 1, 1);
        reference_media_full(after, 0);
        break;
    case C42_REF_IDENTITY_REUSE_TAIL:
        after->sq_host_tail[0] = 2;
        after->sq_pending[0] = 1;
        break;
    case C42_REF_IDENTITY_TARGET:
        after->last_result = 8;
        break;
    case C42_REF_IDENTITY_TARGET_ACTIVE:
        after->sq_host_tail[0] = 1;
        after->sq_device_head[0] = 1;
        after->capture_count = 1;
        after->command_count = 1;
        after->active_identity_count = 1;
        after->notification_count = 1;
        reference_command(after, 0, 333, 6);
        reference_raw(after, 0, 333);
        after->port_records = 1;
        after->port_admitted = 1;
        break;
    case C42_REF_IDENTITY_TARGET_ACQUIRE:
        after->target_count = 1;
        after->target_identity_ok = 1;
        break;
    case C42_REF_IDENTITY_DUPLICATE:
        after->sq_host_tail[0] = 2;
        after->sq_pending[0] = 1;
        break;
    case C42_REF_IDENTITY_CROSS_COMMIT:
        after->command_count = 0;
        after->active_identity_count = 0;
        memset(after->command_state, 0, sizeof(after->command_state));
        memset(after->command_sqhd, 0, sizeof(after->command_sqhd));
        memset(after->body_prefix, 0, sizeof(after->body_prefix));
        memset(after->marker_visible, 0, sizeof(after->marker_visible));
        memset(after->reconcile_state, 0, sizeof(after->reconcile_state));
        after->cq_reserved[0] = 0;
        after->cq_unacked[0] = 1;
        after->cq_device_tail[0] = 1;
        after->slot_state[0] = 2;
        after->notification_count = 1;
        after->notification_state = 1;
        after->target_identity_ok = 0;
        after->port_consume_committed = 1;
        after->order_mask = UINT32_C(0x1ff);
        break;
    case C42_REF_IDENTITY_REUSE_ACTIVE:
        after->sq_device_head[0] = 2;
        after->sq_pending[0] = 0;
        after->capture_count = 2;
        after->command_count = 1;
        after->active_identity_count = 1;
        after->notification_count = 2;
        reference_command(after, 0, 311, 6);
        reference_raw(after, 0, 311);
        after->port_records = 1;
        after->port_admitted = 1;
        after->port_ready = 0;
        after->port_leased = 0;
        after->port_consume_prepared = 0;
        after->port_consume_committed = 0;
        after->port_retired = 0;
        break;
    case C42_REF_IDENTITY_NEW_TARGET:
        after->target_count = 2;
        after->target_identity_ok = 1;
        after->target_tokens_distinct = 1;
        after->target_handles_distinct = 1;
        after->target_origins_distinct = 1;
        after->target_generations_distinct = 1;
        break;
    case C42_REF_IDENTITY_RELEASE_OLD:
        after->target_count = 1;
        after->target_identity_ok = 1;
        break;
    case C42_REF_IDENTITY_RELEASE_NEW:
        after->target_count = 0;
        after->target_identity_ok = 0;
        break;
    case C42_REF_IDENTITY_TARGET_RELEASE:
        after->target_count = 0;
        after->target_identity_ok = 0;
        break;
    case C42_REF_PUBLICATION_SUBMIT:
        after->sq_host_tail[0] = 1;
        after->sq_device_head[0] = 1;
        after->capture_count = 1;
        after->acquire_count = 1;
        after->command_count = 1;
        after->active_identity_count = 1;
        after->notification_count = 1;
        reference_command(after, 0, 312, 10);
        reference_raw(after, 0, 312);
        after->command_sqhd[0] = 1;
        after->cq_reserved[0] = 1;
        after->slot_state[0] = 1;
        after->reconcile_state[0] = 1;
        after->port_records = 1;
        after->port_admitted = 1;
        after->port_ready = 1;
        after->port_leased = 1;
        after->port_consume_prepared = 1;
        after->cqe_valid[0] = 1;
        c42_reference_build_cqe(
            UINT32_C(0x44332211), 1, 0, 312, 1,
            0x5a, 3, 2, 1, 1, after->cqe[0]
        );
        after->media_cqe_valid[0] = 1;
        memset(after->media_cqe[0], 0, sizeof(after->media_cqe[0]));
        break;
    case C42_REF_PUBLICATION_BODY_PREFIX:
        after->body_prefix[0] = 7;
        memcpy(after->media_cqe[0], after->cqe[0], 7);
        break;
    case C42_REF_PUBLICATION_BODY_FULL:
        after->body_prefix[0] = 15;
        after->slot_state[0] = 10;
        reference_media_full(after, 0);
        after->media_cqe[0][14] = 0;
        break;
    case C42_REF_PUBLICATION_MARKER_UNKNOWN:
        after->command_state[0] = 11;
        after->slot_state[0] = 11;
        after->marker_visible[0] = 0;
        reference_media_full(after, 0);
        break;
    case C42_REF_PUBLICATION_MARKER_FULL:
        after->marker_visible[0] = 1;
        break;
    case C42_REF_PUBLICATION_CROSS_COMMIT:
        after->command_count = 0;
        after->active_identity_count = 0;
        memset(after->command_state, 0, sizeof(after->command_state));
        memset(after->command_sqhd, 0, sizeof(after->command_sqhd));
        memset(after->body_prefix, 0, sizeof(after->body_prefix));
        memset(after->marker_visible, 0, sizeof(after->marker_visible));
        memset(after->reconcile_state, 0, sizeof(after->reconcile_state));
        after->cq_reserved[0] = 0;
        after->cq_unacked[0] = 1;
        after->cq_device_tail[0] = 1;
        after->slot_state[0] = 2;
        after->notification_count = 1;
        after->notification_state = 1;
        after->port_consume_committed = 1;
        after->order_mask = UINT32_C(0x1ff);
        break;
    case C42_REF_DELETE_PUBLISH:
        after->sq_host_tail[0] = 1;
        after->sq_device_head[0] = 1;
        after->capture_count = 1;
        after->acquire_count = 1;
        after->cq_device_tail[0] = 1;
        after->cq_unacked[0] = 1;
        after->slot_state[0] = 2;
        after->notification_count = 1;
        after->notification_state = 1;
        reference_cqe(after, 0, 313, 1, 1);
        reference_published_port(after, 1);
        after->order_mask = UINT32_C(0x1ff);
        break;
    case C42_REF_DELETE_SQ:
        after->sq_life[0] = 5;
        after->control_state = 3;
        break;
    case C42_REF_DELETE_CQ_PROBE:
        after->last_result = 2;
        break;
    case C42_REF_DELETE_RECREATE_PROBE:
        after->last_result = 2;
        break;
    case C42_REF_RESET_ACTIVE:
        after->sq_host_tail[0] = 1;
        after->sq_device_head[0] = 1;
        after->capture_count = 1;
        after->command_count = 1;
        after->active_identity_count = 1;
        after->notification_count = 1;
        reference_command(after, 0, 314, 6);
        reference_raw(after, 0, 314);
        after->port_records = 1;
        after->port_admitted = 1;
        break;
    case C42_REF_RESET_START:
        after->phase = 2;
        after->controller_epoch = 12;
        after->control_state = 1;
        after->notification_state = 4;
        break;
    case C42_REF_RESET_COLD:
        memset(after, 0, sizeof(*after));
        after->family = before->family;
        after->controller_epoch = 12;
        after->control_state = 3;
        after->capture_count = 1;
        after->port_records = 1;
        after->port_admitted = 1;
        after->port_retired = 1;
        break;
    case C42_REF_RESET_RETIRE_UNKNOWN:
        after->cq_life[1] = 2;
        after->cq_ring_generation[1] = 1;
        after->cq_mapping_generation[1] = 1;
        after->cq_phase[1] = 1;
        after->candidate_state = 10;
        break;
    case C42_REF_RESET_CANDIDATE_LP:
        after->phase = 2;
        after->controller_epoch = 12;
        after->control_state = 1;
        after->candidate_state = 8;
        break;
    case C42_REF_ISOLATION_LEFT:
        after->sq_host_tail[0] = 1;
        after->sq_device_head[0] = 1;
        after->capture_count = 1;
        after->command_count = 1;
        after->active_identity_count = 1;
        after->notification_count = 1;
        reference_command(after, 0, 315, 6);
        reference_raw(after, 0, 315);
        after->port_records = 1;
        after->port_admitted = 1;
        break;
    case C42_REF_ISOLATION_RIGHT:
        after->other_active = 1;
        break;
    case C42_REF_ISOLATION_CROSS_REJECT:
        after->target_count = 1;
        after->target_identity_ok = 1;
        after->cross_effect = 0;
        after->last_result = 1;
        break;
    case C42_REF_ADMISSION_POISON:
        after->phase = 3;
        after->fault_cause = 4;
        after->sq_life[0] = 6;
        after->sq_host_tail[0] = 1;
        after->sq_pending[0] = 1;
        after->capture_count = 1;
        after->command_count = 1;
        after->command_state[0] = 14;
        after->command_id[0] = 320;
        after->command_identity_ok[0] = 0;
        after->notification_count = 1;
        after->port_records = 1;
        after->last_result = 9;
        break;
    case C42_REF_ACK_NONCOMMITTED:
        after->sq_host_tail[0] = 1;
        after->sq_device_head[0] = 1;
        after->capture_count = 1;
        after->acquire_count = 1;
        after->command_count = 1;
        after->active_identity_count = 1;
        after->notification_count = 1;
        reference_command(after, 0, 321, 10);
        reference_raw(after, 0, 321);
        after->command_sqhd[0] = 1;
        after->cq_reserved[0] = 1;
        after->slot_state[0] = 1;
        after->reconcile_state[0] = 1;
        after->port_records = 1;
        after->port_admitted = 1;
        after->port_ready = 1;
        after->port_leased = 1;
        after->port_consume_prepared = 1;
        reference_cqe(after, 0, 321, 1, 1);
        after->last_result = 1;
        break;
    case C42_REF_TARGET_GENERATION_MISMATCH:
        after->sq_host_tail[0] = 1;
        after->sq_device_head[0] = 1;
        after->capture_count = 1;
        after->command_count = 1;
        after->active_identity_count = 1;
        after->notification_count = 1;
        reference_command(after, 0, 322, 6);
        reference_raw(after, 0, 322);
        after->port_records = 1;
        after->port_admitted = 1;
        after->last_result = 11;
        break;
    case C42_REF_DELETE_PENDING_PROBE:
        after->sq_host_tail[0] = 2;
        after->sq_pending[0] = 2;
        after->sq_life[0] = 3;
        after->control_state = 1;
        break;
    case C42_REF_SQHD_DELAY:
        after->sq_host_tail[0] = 2;
        after->sq_device_head[0] = 2;
        after->capture_count = 2;
        after->acquire_count = 2;
        after->cq_device_tail[0] = 2;
        after->cq_unacked[0] = 2;
        after->slot_state[0] = 2;
        after->slot_state[1] = 2;
        after->notification_count = 2;
        after->notification_state = 1;
        reference_cqe(after, 0, 330, 2, 1);
        reference_cqe(after, 1, 331, 2, 1);
        reference_published_port(after, 2);
        after->order_mask = UINT32_C(0x1ff);
        break;
    default:
        return 0;
    }
    after->event_count = after->order_mask == UINT32_C(0x1ff) ? 9u : 0u;
    return 1;
}

int c42_reference_state_equal(
    const struct c42_reference_state *left,
    const struct c42_reference_state *right)
{
    return left != NULL && right != NULL &&
           memcmp(left, right, sizeof(*left)) == 0;
}
