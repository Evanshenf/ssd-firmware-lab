/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_memory.h"

#include <stdbool.h>
#include <string.h>

static int cap_equal(
    const struct c42_queue_memory_cap *left,
    const struct c42_queue_memory_cap *right)
{
    return left->instance_nonce == right->instance_nonce &&
           left->owner_epoch == right->owner_epoch &&
           left->memory_uid == right->memory_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->ring_generation == right->ring_generation &&
           left->mapping_generation == right->mapping_generation &&
           left->exact_bytes == right->exact_bytes &&
           left->queue_id == right->queue_id && left->role == right->role &&
           left->reserved == right->reserved;
}

static int token_equal(
    const struct c42_memory_token *left,
    const struct c42_memory_token *right)
{
    return left->instance_nonce == right->instance_nonce &&
           left->uid == right->uid && left->generation == right->generation &&
           left->kind == right->kind && left->reserved == right->reserved;
}

static struct c42_fake_memory_mapping *mapping_for(
    struct c42_fake_memory *memory,
    const struct c42_queue_memory_cap *capability)
{
    struct c42_fake_memory_mapping *mapping;

    if (capability->queue_id >= C42_MAX_QUEUE_PAIRS) {
        return NULL;
    }
    mapping = capability->role == C42_MEMORY_SQ_READ ?
              &memory->sq_map[capability->queue_id] :
              &memory->cq_map[capability->queue_id];
    if (mapping->present == 0 ||
        !cap_equal(&mapping->capability, capability)) {
        return NULL;
    }
    return mapping;
}

static const struct c42_fake_memory_mapping *mapping_for_const(
    const struct c42_fake_memory *memory,
    const struct c42_queue_memory_cap *capability)
{
    const struct c42_fake_memory_mapping *mapping;

    if (capability->queue_id >= C42_MAX_QUEUE_PAIRS) {
        return NULL;
    }
    mapping = capability->role == C42_MEMORY_SQ_READ ?
              &memory->sq_map[capability->queue_id] :
              &memory->cq_map[capability->queue_id];
    if (mapping->present == 0 ||
        !cap_equal(&mapping->capability, capability)) {
        return NULL;
    }
    return mapping;
}

static struct c42_fake_memory_outcome next_outcome(
    struct c42_fake_memory *memory,
    uint8_t operation,
    uint8_t full_prefix)
{
    struct c42_fake_memory_outcome outcome = {0};

    outcome.operation = operation;
    outcome.effect =
        (operation == C42_FAKE_MEMORY_SCRUB_RETIRE ||
         operation == C42_FAKE_MEMORY_SCRUB_ABORT) ?
        C42_MEMORY_RETIRED : C42_MEMORY_FULL;
    outcome.prefix = full_prefix;
    outcome.committed = 1;
    if (memory->script_index < memory->script_count &&
        memory->script[memory->script_index].operation == operation) {
        outcome = memory->script[memory->script_index];
        memory->script_index++;
    }
    return outcome;
}

static int direct_outcome_take(
    struct c42_fake_memory *memory,
    uint8_t operation,
    struct c42_fake_memory_outcome *outcome)
{
    if (memory->script_index >= memory->script_count ||
        memory->script[memory->script_index].operation != operation) {
        return 0;
    }
    *outcome = memory->script[memory->script_index];
    memory->script_index++;
    return 1;
}

static void status_fill(
    struct c42_memory_status *status,
    const struct c42_memory_token *token,
    const struct c42_fake_memory_outcome *outcome,
    uint16_t prefix,
    uint8_t quiescent)
{
    memset(status, 0, sizeof(*status));
    status->token = *token;
    status->result = outcome->effect;
    status->prefix = prefix;
    status->committed = outcome->committed;
    status->quiescent = quiescent;
}

static void status_apply_override(
    struct c42_memory_status *status,
    const struct c42_fake_memory_outcome *outcome)
{
    if ((outcome->status_override & 1u) != 0) {
        status->committed = outcome->status_committed;
    }
    if ((outcome->status_override & 2u) != 0) {
        status->quiescent = outcome->status_quiescent;
    }
}

void c42_fake_memory_init(
    struct c42_fake_memory *memory,
    uint64_t instance_nonce,
    uint64_t owner_epoch,
    uint32_t controller_epoch)
{
    if (memory == NULL) {
        return;
    }
    memset(memory, 0, sizeof(*memory));
    memory->instance_nonce = instance_nonce;
    memory->owner_epoch = owner_epoch;
    memory->controller_epoch = controller_epoch;
}

void c42_fake_memory_bind_event_log(
    struct c42_fake_memory *memory,
    struct c42_fake_event_log *log)
{
    if (memory != NULL) {
        memory->event_log = log;
    }
}

enum c42_result c42_fake_memory_map(
    struct c42_fake_memory *memory,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth)
{
    struct c42_fake_memory_mapping *mapping;
    uint32_t expected;

    if (memory == NULL || !c42_queue_memory_cap_valid(capability) ||
        capability->instance_nonce != memory->instance_nonce ||
        capability->owner_epoch != memory->owner_epoch ||
        capability->controller_epoch != memory->controller_epoch || depth < 2 ||
        depth > C42_MAX_QUEUE_DEPTH ||
        capability->queue_id >= C42_MAX_QUEUE_PAIRS) {
        return C42_INVALID;
    }
    expected = (uint32_t)depth *
               (capability->role == C42_MEMORY_SQ_READ ?
                C42_SQE_BYTES : C42_CQE_BYTES);
    if (capability->exact_bytes != expected) {
        return C42_INVALID;
    }
    mapping = capability->role == C42_MEMORY_SQ_READ ?
              &memory->sq_map[capability->queue_id] :
              &memory->cq_map[capability->queue_id];
    mapping->capability = *capability;
    mapping->depth = depth;
    mapping->present = 1;
    return C42_OK;
}

enum c42_result c42_fake_memory_write_sqe(
    struct c42_fake_memory *memory,
    uint16_t queue_id,
    uint16_t slot,
    const uint8_t bytes[C42_SQE_BYTES])
{
    if (memory == NULL || bytes == NULL ||
        queue_id >= C42_MAX_QUEUE_PAIRS ||
        memory->sq_map[queue_id].present == 0 ||
        slot >= memory->sq_map[queue_id].depth) {
        return C42_INVALID;
    }
    memcpy(memory->sq[queue_id][slot], bytes, C42_SQE_BYTES);
    return C42_OK;
}

enum c42_result c42_fake_memory_read_cqe(
    const struct c42_fake_memory *memory,
    uint16_t queue_id,
    uint16_t slot,
    uint8_t bytes[C42_CQE_BYTES])
{
    if (memory == NULL || bytes == NULL ||
        queue_id >= C42_MAX_QUEUE_PAIRS ||
        memory->cq_map[queue_id].present == 0 ||
        slot >= memory->cq_map[queue_id].depth) {
        return C42_INVALID;
    }
    memcpy(bytes, memory->cq[queue_id][slot], C42_CQE_BYTES);
    return C42_OK;
}

enum c42_result c42_fake_memory_script_push(
    struct c42_fake_memory *memory,
    const struct c42_fake_memory_outcome *outcome)
{
    if (memory == NULL || outcome == NULL ||
        memory->script_count >= C42_FAKE_MEMORY_SCRIPT_MAX ||
        (outcome->operation < C42_FAKE_MEMORY_SCRUB ||
         outcome->operation > C42_FAKE_MEMORY_SCRUB_ABORT) ||
        outcome->effect > C42_MEMORY_RETIRED + 2u ||
        outcome->prefix > 15 || outcome->committed > 1 ||
        outcome->status_committed > 2 || outcome->status_quiescent > 2 ||
        outcome->status_override > 3 || outcome->reserved != 0) {
        return C42_INVALID;
    }
    memory->script[memory->script_count] = *outcome;
    memory->script_count++;
    return C42_OK;
}

enum c42_result c42_fake_memory_direct_push(
    struct c42_fake_memory *memory,
    const struct c42_fake_memory_direct_injection *injection)
{
    if (memory == NULL || injection == NULL ||
        memory->direct_count >= C42_FAKE_MEMORY_DIRECT_MAX ||
        injection->operation < C42_FAKE_MEMORY_SCRUB ||
        injection->operation > C42_FAKE_MEMORY_SCRUB_ABORT ||
        injection->result > C42_MEMORY_RETIRED + 2u ||
        injection->omit_status > 1 || injection->write_status > 1 ||
        (injection->omit_status != 0 && injection->write_status != 0) ||
        ((injection->operation == C42_FAKE_MEMORY_VALIDATE ||
          injection->operation == C42_FAKE_MEMORY_RESET_BEGIN ||
          injection->operation == C42_FAKE_MEMORY_TEARDOWN_BEGIN) &&
         injection->write_status != 0) ||
        injection->apply_effect > 1 ||
        (injection->apply_effect != 0 &&
         injection->result != C42_MEMORY_OK) ||
        injection->logical_effect > C42_MEMORY_RETIRED + 2u ||
        injection->applied_effect > C42_MEMORY_RETIRED + 2u ||
        injection->prefix > 15 || injection->committed > 2 ||
        injection->quiescent > 2 ||
        injection->token_variant > C42_FAKE_MEMORY_TOKEN_MISMATCH) {
        return C42_INVALID;
    }
    memory->direct[memory->direct_count] = *injection;
    memory->direct_count++;
    return C42_OK;
}

static int direct_result_take(
    struct c42_fake_memory *memory,
    uint8_t operation,
    const struct c42_fake_memory_direct_injection **selected)
{
    const struct c42_fake_memory_direct_injection *injection;

    if (memory->direct_index >= memory->direct_count) {
        return 0;
    }
    injection = &memory->direct[memory->direct_index];
    if (injection->operation != operation) {
        return 0;
    }
    memory->direct_index++;
    memory->direct_event_active = 1;
    memory->direct_event_write_status =
        (uint8_t)(injection->omit_status == 0 &&
                  injection->write_status != 0);
    memory->direct_event_apply_effect = 0;
    memory->direct_event_logical_effect = injection->logical_effect;
    memory->direct_event_requested_effect = injection->apply_effect != 0 ?
        injection->applied_effect : C42_MEMORY_NO_EFFECT;
    memory->direct_event_applied_effect = C42_MEMORY_NO_EFFECT;
    memory->direct_event_committed = injection->committed;
    memory->direct_event_quiescent = injection->quiescent;
    memory->direct_event_token_variant = injection->token_variant;
    *selected = injection;
    return 1;
}

static void mark_direct_effect(
    struct c42_fake_memory *memory,
    uint8_t effect)
{
    if (memory != NULL && memory->direct_event_active != 0) {
        memory->direct_event_apply_effect = 1;
        memory->direct_event_applied_effect = effect;
    }
}

static void direct_status_fill(
    struct c42_memory_status *status,
    const struct c42_memory_token *token,
    const struct c42_fake_memory_direct_injection *direct)
{
    if (direct->omit_status != 0 || direct->write_status == 0) {
        return;
    }
    memset(status, 0, sizeof(*status));
    if (direct->token_variant != C42_FAKE_MEMORY_TOKEN_ZERO) {
        status->token = *token;
        if (direct->token_variant == C42_FAKE_MEMORY_TOKEN_MISMATCH) {
            status->token.uid++;
        }
    }
    status->result = direct->logical_effect;
    status->prefix = direct->prefix;
    status->committed = direct->committed;
    status->quiescent = direct->quiescent;
}

static enum c42_memory_result fake_validate(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint8_t role,
    uint32_t exact_bytes)
{
    struct c42_fake_memory *memory = context;
    const struct c42_fake_memory_mapping *mapping;
    struct c42_fake_memory_outcome outcome;
    const struct c42_fake_memory_direct_injection *direct;

    if (memory == NULL || capability == NULL || role != capability->role ||
        exact_bytes != capability->exact_bytes) {
        return C42_MEMORY_INVALID;
    }
    if (direct_result_take(
            memory, C42_FAKE_MEMORY_VALIDATE, &direct)) {
        return (enum c42_memory_result)direct->result;
    }
    if (direct_outcome_take(
            memory, C42_FAKE_MEMORY_VALIDATE, &outcome)) {
        return (enum c42_memory_result)outcome.effect;
    }
    mapping = mapping_for_const(memory, capability);
    return mapping == NULL ? C42_MEMORY_STALE : C42_MEMORY_OK;
}

static enum c42_memory_result fake_capture(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    uint8_t *output,
    size_t output_size)
{
    struct c42_fake_memory *memory = context;
    struct c42_fake_memory_mapping *mapping;
    struct c42_fake_memory_outcome outcome;
    const struct c42_fake_memory_direct_injection *direct;

    if (memory == NULL || capability == NULL || output == NULL ||
        output_size != C42_SQE_BYTES ||
        capability->role != C42_MEMORY_SQ_READ) {
        return C42_MEMORY_INVALID;
    }
    mapping = mapping_for(memory, capability);
    if (mapping == NULL || slot >= mapping->depth) {
        return C42_MEMORY_STALE;
    }
    if (direct_result_take(
            memory, C42_FAKE_MEMORY_CAPTURE, &direct)) {
        if (direct->omit_status == 0 && direct->write_status != 0) {
            memcpy(
                output, memory->sq[capability->queue_id][slot],
                C42_SQE_BYTES
            );
        }
        if (direct->apply_effect != 0) {
            memory->capture_count++;
            mark_direct_effect(memory, direct->applied_effect);
        }
        return (enum c42_memory_result)direct->result;
    }
    if (direct_outcome_take(
            memory, C42_FAKE_MEMORY_CAPTURE, &outcome)) {
        return (enum c42_memory_result)outcome.effect;
    }
    memcpy(output, memory->sq[capability->queue_id][slot], C42_SQE_BYTES);
    memory->capture_count++;
    return C42_MEMORY_OK;
}

static void scrub_apply(
    struct c42_fake_memory *memory,
    uint16_t queue_id,
    uint16_t depth,
    uint8_t inverse_phase,
    uint16_t count)
{
    uint16_t slot;

    if (count > depth) {
        count = depth;
    }
    for (slot = 0; slot < count; ++slot) {
        memset(memory->cq[queue_id][slot], 0, C42_CQE_BYTES);
        memory->cq[queue_id][slot][14] = (uint8_t)(inverse_phase & 1u);
    }
}

static enum c42_memory_result scrub_common(
    struct c42_fake_memory *memory,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth,
    uint8_t inverse_phase,
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status,
    int start)
{
    struct c42_fake_memory_operation_record *record;
    struct c42_fake_memory_outcome outcome;
    const struct c42_fake_memory_direct_injection *direct;
    uint16_t applied;

    if (memory == NULL || capability == NULL || client_token == NULL ||
        status == NULL || capability->role != C42_MEMORY_CQ_PUBLISH ||
        mapping_for(memory, capability) == NULL ||
        capability->queue_id >= C42_MAX_QUEUE_PAIRS ||
        depth != memory->cq_map[capability->queue_id].depth ||
        inverse_phase > 1) {
        return C42_MEMORY_INVALID;
    }
    record = &memory->scrub[capability->queue_id];
    if (start != 0) {
        if (record->active != 0 &&
            record->retired == 0 &&
            !token_equal(&record->token, client_token)) {
            return C42_MEMORY_POISONED;
        }
        if (record->active == 0 ||
            (record->retired != 0 &&
             !token_equal(&record->token, client_token))) {
            memset(record, 0, sizeof(*record));
            record->active = 1;
            record->kind = C42_FAKE_MEMORY_SCRUB;
            record->token = *client_token;
            record->capability = *capability;
        }
    } else if (record->active == 0 ||
               !token_equal(&record->token, client_token) ||
               !cap_equal(&record->capability, capability)) {
        return C42_MEMORY_STALE;
    }
    if (direct_result_take(
            memory, C42_FAKE_MEMORY_SCRUB, &direct)) {
        if (direct->apply_effect != 0) {
            applied = record->prefix;
            if (direct->applied_effect == C42_MEMORY_FULL ||
                (direct->applied_effect == C42_MEMORY_UNKNOWN &&
                 direct->committed != 0)) {
                applied = depth;
            } else if (direct->applied_effect == C42_MEMORY_EXACT_PREFIX) {
                applied = direct->prefix > depth ? depth : direct->prefix;
            }
            if (applied > record->prefix) {
                scrub_apply(
                    memory, capability->queue_id, depth,
                    inverse_phase, applied
                );
                record->prefix = applied;
            }
            if (direct->applied_effect == C42_MEMORY_FULL ||
                (direct->applied_effect == C42_MEMORY_UNKNOWN &&
                 direct->committed != 0)) {
                record->committed = 1;
            }
            mark_direct_effect(memory, direct->applied_effect);
        }
        direct_status_fill(status, client_token, direct);
        memory->scrub_call_count++;
        return (enum c42_memory_result)direct->result;
    }
    outcome = next_outcome(
        memory, C42_FAKE_MEMORY_SCRUB, (uint8_t)depth
    );
    applied = record->prefix;
    if (outcome.effect == C42_MEMORY_FULL ||
        (outcome.effect == C42_MEMORY_UNKNOWN && outcome.committed != 0)) {
        applied = depth;
    } else if (outcome.effect == C42_MEMORY_EXACT_PREFIX) {
        applied = outcome.prefix > depth ? depth : outcome.prefix;
    }
    if (applied > record->prefix) {
        scrub_apply(
            memory, capability->queue_id, depth, inverse_phase, applied
        );
        record->prefix = applied;
    }
    if (outcome.effect == C42_MEMORY_FULL ||
        (outcome.effect == C42_MEMORY_UNKNOWN && outcome.committed != 0)) {
        record->committed = 1;
    }
    status_fill(status, client_token, &outcome, record->prefix, 0);
    status->committed = record->committed;
    status->quiescent = record->committed;
    status_apply_override(status, &outcome);
    memory->scrub_call_count++;
    return C42_MEMORY_OK;
}

static enum c42_memory_result fake_scrub_start(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth,
    uint8_t inverse_phase,
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status)
{
    return scrub_common(
        context, capability, depth, inverse_phase, client_token, status, 1
    );
}

static enum c42_memory_result fake_scrub_query(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth,
    uint8_t inverse_phase,
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status)
{
    return scrub_common(
        context, capability, depth, inverse_phase, client_token, status, 0
    );
}

static enum c42_memory_result fake_scrub_abort(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth,
    uint8_t inverse_phase,
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status)
{
    struct c42_fake_memory *memory = context;
    struct c42_fake_memory_operation_record *record;
    struct c42_fake_memory_outcome outcome;
    const struct c42_fake_memory_direct_injection *direct;

    (void)depth;
    (void)inverse_phase;
    if (memory == NULL || capability == NULL || client_token == NULL ||
        status == NULL || capability->queue_id >= C42_MAX_QUEUE_PAIRS) {
        return C42_MEMORY_INVALID;
    }
    record = &memory->scrub[capability->queue_id];
    if (record->active == 0 ||
        !token_equal(&record->token, client_token) ||
        !cap_equal(&record->capability, capability)) {
        return C42_MEMORY_STALE;
    }
    if (direct_result_take(
            memory, C42_FAKE_MEMORY_SCRUB_ABORT, &direct)) {
        direct_status_fill(status, client_token, direct);
        if (direct->apply_effect != 0 &&
            direct->applied_effect == C42_MEMORY_RETIRED) {
            record->retired = 1;
            mark_direct_effect(memory, C42_MEMORY_RETIRED);
        }
        return (enum c42_memory_result)direct->result;
    }
    outcome = next_outcome(memory, C42_FAKE_MEMORY_SCRUB_ABORT, 0);
    status_fill(
        status, client_token, &outcome, record->prefix,
        (uint8_t)(outcome.effect == C42_MEMORY_RETIRED)
    );
    status->committed = record->committed;
    status_apply_override(status, &outcome);
    if (outcome.effect == C42_MEMORY_RETIRED) {
        record->retired = 1;
    }
    return C42_MEMORY_OK;
}

static enum c42_memory_result scrub_retire_common(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth,
    uint8_t inverse_phase,
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status)
{
    struct c42_fake_memory *memory = context;
    struct c42_fake_memory_operation_record *record;
    struct c42_fake_memory_outcome outcome;
    const struct c42_fake_memory_direct_injection *direct;

    (void)inverse_phase;
    if (memory == NULL || capability == NULL || client_token == NULL ||
        status == NULL || capability->queue_id >= C42_MAX_QUEUE_PAIRS ||
        depth != memory->cq_map[capability->queue_id].depth) {
        return C42_MEMORY_INVALID;
    }
    record = &memory->scrub[capability->queue_id];
    if (record->active == 0 ||
        !token_equal(&record->token, client_token) ||
        !cap_equal(&record->capability, capability) ||
        record->committed == 0) {
        return C42_MEMORY_STALE;
    }
    if (direct_result_take(
            memory, C42_FAKE_MEMORY_SCRUB_RETIRE, &direct)) {
        direct_status_fill(status, client_token, direct);
        if (direct->apply_effect != 0 &&
            direct->applied_effect == C42_MEMORY_RETIRED) {
            record->retired = 1;
            mark_direct_effect(memory, C42_MEMORY_RETIRED);
        }
        return (enum c42_memory_result)direct->result;
    }
    if (record->retired != 0) {
        memset(&outcome, 0, sizeof(outcome));
        outcome.operation = C42_FAKE_MEMORY_SCRUB_RETIRE;
        outcome.effect = C42_MEMORY_RETIRED;
        outcome.committed = 1;
    } else {
        outcome = next_outcome(
            memory, C42_FAKE_MEMORY_SCRUB_RETIRE, 0
        );
        if (outcome.effect == C42_MEMORY_RETIRED) {
            record->retired = 1;
        }
    }
    status_fill(status, client_token, &outcome, record->prefix,
                record->retired);
    status->committed = 1;
    status_apply_override(status, &outcome);
    return C42_MEMORY_OK;
}

static enum c42_memory_result fake_scrub_retire_start(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth,
    uint8_t inverse_phase,
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status)
{
    return scrub_retire_common(
        context, capability, depth, inverse_phase, client_token, status
    );
}

static enum c42_memory_result fake_scrub_retire_query(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth,
    uint8_t inverse_phase,
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status)
{
    return scrub_retire_common(
        context, capability, depth, inverse_phase, client_token, status
    );
}

static void body_apply(
    struct c42_fake_memory *memory,
    uint16_t queue_id,
    uint16_t slot,
    const uint8_t expected[C42_CQE_BYTES],
    uint16_t old_prefix,
    uint16_t new_prefix)
{
    uint16_t ordinal;

    for (ordinal = old_prefix; ordinal < new_prefix; ++ordinal) {
        uint16_t byte = ordinal < 14 ? ordinal : 15;

        memory->cq[queue_id][slot][byte] = expected[byte];
    }
}

static enum c42_memory_result body_common(
    struct c42_fake_memory *memory,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    const uint8_t expected[C42_CQE_BYTES],
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status,
    int start)
{
    struct c42_fake_memory_operation_record *record;
    struct c42_fake_memory_outcome outcome;
    const struct c42_fake_memory_direct_injection *direct;
    uint16_t applied;

    if (memory == NULL || capability == NULL || expected == NULL ||
        client_token == NULL || status == NULL ||
        capability->role != C42_MEMORY_CQ_PUBLISH ||
        mapping_for(memory, capability) == NULL ||
        slot >= memory->cq_map[capability->queue_id].depth) {
        return C42_MEMORY_INVALID;
    }
    record = &memory->publication[capability->queue_id];
    if (start != 0) {
        if (record->active != 0 &&
            !token_equal(&record->token, client_token)) {
            return C42_MEMORY_POISONED;
        }
        if (record->active == 0) {
            memset(record, 0, sizeof(*record));
            record->active = 1;
            record->kind = C42_FAKE_MEMORY_BODY;
            record->token = *client_token;
            record->capability = *capability;
            record->slot = slot;
            memcpy(record->expected, expected, sizeof(record->expected));
        }
    } else if (record->active == 0 ||
               record->kind != C42_FAKE_MEMORY_BODY ||
               !token_equal(&record->token, client_token) ||
               !cap_equal(&record->capability, capability) ||
               record->slot != slot ||
               memcmp(record->expected, expected, C42_CQE_BYTES) != 0) {
        return C42_MEMORY_STALE;
    }
    if (direct_result_take(
            memory, C42_FAKE_MEMORY_BODY, &direct)) {
        if (direct->apply_effect != 0) {
            applied = record->prefix;
            if (direct->applied_effect == C42_MEMORY_FULL ||
                (direct->applied_effect == C42_MEMORY_UNKNOWN &&
                 direct->committed != 0)) {
                applied = 15;
            } else if (direct->applied_effect == C42_MEMORY_EXACT_PREFIX) {
                applied = direct->prefix > 15 ? 15 : direct->prefix;
            }
            if (applied > record->prefix) {
                body_apply(
                    memory, capability->queue_id, slot, expected,
                    record->prefix, applied
                );
                record->prefix = applied;
            }
            mark_direct_effect(memory, direct->applied_effect);
        }
        direct_status_fill(status, client_token, direct);
        memory->body_call_count++;
        return (enum c42_memory_result)direct->result;
    }
    outcome = next_outcome(memory, C42_FAKE_MEMORY_BODY, 15);
    applied = record->prefix;
    if (outcome.effect == C42_MEMORY_FULL ||
        (outcome.effect == C42_MEMORY_UNKNOWN && outcome.committed != 0)) {
        applied = 15;
    } else if (outcome.effect == C42_MEMORY_EXACT_PREFIX) {
        applied = outcome.prefix;
    }
    if (applied > 15) {
        applied = 15;
    }
    if (applied > record->prefix) {
        body_apply(
            memory, capability->queue_id, slot, expected,
            record->prefix, applied
        );
        record->prefix = applied;
    }
    status_fill(status, client_token, &outcome, record->prefix, 0);
    status_apply_override(status, &outcome);
    memory->body_call_count++;
    return C42_MEMORY_OK;
}

static enum c42_memory_result fake_body_start(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    const uint8_t expected[C42_CQE_BYTES],
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status)
{
    return body_common(
        context, capability, slot, expected, client_token, status, 1
    );
}

static enum c42_memory_result fake_body_query(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    const uint8_t expected[C42_CQE_BYTES],
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status)
{
    return body_common(
        context, capability, slot, expected, client_token, status, 0
    );
}

static enum c42_memory_result marker_common(
    struct c42_fake_memory *memory,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    uint8_t marker,
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status,
    int start)
{
    struct c42_fake_memory_operation_record *record;
    struct c42_fake_memory_outcome outcome;
    const struct c42_fake_memory_direct_injection *direct;

    if (memory == NULL || capability == NULL || client_token == NULL ||
        status == NULL || capability->role != C42_MEMORY_CQ_PUBLISH ||
        mapping_for(memory, capability) == NULL ||
        slot >= memory->cq_map[capability->queue_id].depth) {
        return C42_MEMORY_INVALID;
    }
    record = &memory->publication[capability->queue_id];
    if (start != 0) {
        if (record->active == 0 ||
            record->kind != C42_FAKE_MEMORY_BODY || record->prefix != 15 ||
            record->slot != slot || record->expected[14] != marker) {
            return C42_MEMORY_POISONED;
        }
        record->kind = C42_FAKE_MEMORY_MARKER;
        record->token = *client_token;
    } else if (record->active == 0 ||
               record->kind != C42_FAKE_MEMORY_MARKER ||
               !token_equal(&record->token, client_token) ||
               record->slot != slot || record->expected[14] != marker) {
        return C42_MEMORY_STALE;
    }
    if (direct_result_take(
            memory, C42_FAKE_MEMORY_MARKER, &direct)) {
        if (direct->apply_effect != 0 &&
            (direct->applied_effect == C42_MEMORY_FULL ||
             (direct->applied_effect == C42_MEMORY_UNKNOWN &&
              direct->committed != 0))) {
            memory->cq[capability->queue_id][slot][14] = marker;
            record->committed = 1;
            if (direct->applied_effect == C42_MEMORY_FULL) {
                record->active = 0;
            }
            mark_direct_effect(memory, direct->applied_effect);
        }
        direct_status_fill(status, client_token, direct);
        memory->marker_call_count++;
        return (enum c42_memory_result)direct->result;
    }
    outcome = next_outcome(memory, C42_FAKE_MEMORY_MARKER, 1);
    if (outcome.effect == C42_MEMORY_FULL ||
        (outcome.effect == C42_MEMORY_UNKNOWN && outcome.committed != 0)) {
        memory->cq[capability->queue_id][slot][14] = marker;
        record->committed = 1;
    }
    status_fill(status, client_token, &outcome, 0, 0);
    status->committed = record->committed;
    status_apply_override(status, &outcome);
    memory->marker_call_count++;
    if (outcome.effect == C42_MEMORY_FULL) {
        record->active = 0;
    }
    return C42_MEMORY_OK;
}

static enum c42_memory_result fake_marker_start(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    uint8_t marker,
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status)
{
    return marker_common(
        context, capability, slot, marker, client_token, status, 1
    );
}

static enum c42_memory_result fake_marker_query(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    uint8_t marker,
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status)
{
    return marker_common(
        context, capability, slot, marker, client_token, status, 0
    );
}

static enum c42_memory_result fake_reset_begin(
    void *context,
    uint64_t instance_nonce,
    uint32_t old_epoch)
{
    struct c42_fake_memory *memory = context;
    struct c42_fake_memory_outcome outcome;
    const struct c42_fake_memory_direct_injection *direct;

    if (memory == NULL || instance_nonce != memory->instance_nonce ||
        old_epoch != memory->controller_epoch || old_epoch == UINT32_MAX) {
        return C42_MEMORY_INVALID;
    }
    if (direct_result_take(
            memory, C42_FAKE_MEMORY_RESET_BEGIN, &direct)) {
        if (direct->apply_effect != 0) {
            memory->reset_active = 1;
            memory->controller_epoch = old_epoch + 1u;
            memset(memory->scrub, 0, sizeof(memory->scrub));
            memset(memory->publication, 0, sizeof(memory->publication));
            mark_direct_effect(memory, direct->applied_effect);
        }
        return (enum c42_memory_result)direct->result;
    }
    if (direct_outcome_take(
            memory, C42_FAKE_MEMORY_RESET_BEGIN, &outcome)) {
        return (enum c42_memory_result)outcome.effect;
    }
    memory->reset_active = 1;
    memory->controller_epoch = old_epoch + 1u;
    memset(memory->scrub, 0, sizeof(memory->scrub));
    memset(memory->publication, 0, sizeof(memory->publication));
    return C42_MEMORY_OK;
}

static enum c42_memory_result fake_reset_quiescent(
    void *context,
    uint64_t instance_nonce,
    uint32_t epoch,
    bool *quiescent)
{
    struct c42_fake_memory *memory = context;
    struct c42_fake_memory_outcome outcome;
    const struct c42_fake_memory_direct_injection *direct;

    if (memory == NULL || quiescent == NULL ||
        instance_nonce != memory->instance_nonce ||
        memory->reset_active == 0 || epoch + 1u != memory->controller_epoch) {
        return C42_MEMORY_INVALID;
    }
    if (direct_result_take(
            memory, C42_FAKE_MEMORY_RESET_QUIESCENT, &direct)) {
        if (direct->omit_status == 0 && direct->write_status != 0) {
            *quiescent = direct->quiescent != 0;
        }
        return (enum c42_memory_result)direct->result;
    }
    if (direct_outcome_take(
            memory, C42_FAKE_MEMORY_RESET_QUIESCENT, &outcome)) {
        if ((outcome.status_override & 2u) != 0) {
            *quiescent = outcome.status_quiescent != 0;
        }
        return (enum c42_memory_result)outcome.effect;
    }
    *quiescent = true;
    return C42_MEMORY_OK;
}

static enum c42_memory_result fake_teardown_begin(
    void *context,
    uint64_t instance_nonce,
    uint32_t old_epoch)
{
    struct c42_fake_memory *memory = context;
    struct c42_fake_memory_outcome outcome;
    const struct c42_fake_memory_direct_injection *direct;

    if (memory == NULL || instance_nonce != memory->instance_nonce ||
        old_epoch != memory->controller_epoch) {
        return C42_MEMORY_INVALID;
    }
    if (direct_result_take(
            memory, C42_FAKE_MEMORY_TEARDOWN_BEGIN, &direct)) {
        if (direct->apply_effect != 0) {
            memory->teardown_active = 1;
            memory->teardown_old_epoch = old_epoch;
            if (old_epoch != UINT32_MAX) {
                memory->controller_epoch = old_epoch + 1u;
            }
            memset(memory->scrub, 0, sizeof(memory->scrub));
            memset(memory->publication, 0, sizeof(memory->publication));
            mark_direct_effect(memory, direct->applied_effect);
        }
        return (enum c42_memory_result)direct->result;
    }
    if (direct_outcome_take(
            memory, C42_FAKE_MEMORY_TEARDOWN_BEGIN, &outcome)) {
        return (enum c42_memory_result)outcome.effect;
    }
    memory->teardown_active = 1;
    memory->teardown_old_epoch = old_epoch;
    if (old_epoch != UINT32_MAX) {
        memory->controller_epoch = old_epoch + 1u;
    }
    memset(memory->scrub, 0, sizeof(memory->scrub));
    memset(memory->publication, 0, sizeof(memory->publication));
    return C42_MEMORY_OK;
}

static enum c42_memory_result fake_teardown_quiescent(
    void *context,
    uint64_t instance_nonce,
    uint32_t epoch,
    bool *quiescent)
{
    struct c42_fake_memory *memory = context;
    struct c42_fake_memory_outcome outcome;
    const struct c42_fake_memory_direct_injection *direct;

    if (memory == NULL || quiescent == NULL ||
        instance_nonce != memory->instance_nonce ||
        memory->teardown_active == 0 || epoch != memory->teardown_old_epoch) {
        return C42_MEMORY_INVALID;
    }
    if (direct_result_take(
            memory, C42_FAKE_MEMORY_TEARDOWN_QUIESCENT, &direct)) {
        if (direct->omit_status == 0 && direct->write_status != 0) {
            *quiescent = direct->quiescent != 0;
        }
        return (enum c42_memory_result)direct->result;
    }
    if (direct_outcome_take(
            memory, C42_FAKE_MEMORY_TEARDOWN_QUIESCENT, &outcome)) {
        if ((outcome.status_override & 2u) != 0) {
            *quiescent = outcome.status_quiescent != 0;
        }
        return (enum c42_memory_result)outcome.effect;
    }
    *quiescent = true;
    return C42_MEMORY_OK;
}

static void log_memory_event(
    struct c42_fake_memory *memory,
    uint32_t operation,
    uint8_t call_kind,
    enum c42_memory_result result,
    uint32_t value,
    uint8_t write_mask,
    int input_structural_valid,
    int input_record_match,
    int output_structural_valid,
    int output_record_match,
    uint64_t token_uid,
    const struct c42_memory_status *status,
    uint32_t parameter0,
    uint32_t parameter1)
{
    struct c42_fake_event event = {0};

    event.token_uid = token_uid;
    event.operation = operation;
    event.direct_result = (uint32_t)result;
    event.output_value = value;
    event.parameter0 = parameter0;
    event.parameter1 = parameter1;
    event.provider = C42_FAKE_EVENT_MEMORY;
    event.call_kind = call_kind;
    event.input_structural_valid =
        (uint8_t)(input_structural_valid != 0);
    event.input_record_match = (uint8_t)(input_record_match != 0);
    event.output_structural_valid =
        (uint8_t)(output_structural_valid != 0);
    event.output_record_match = (uint8_t)(output_record_match != 0);
    event.reported_effect = value;
    if (status != NULL) {
        event.object_uid = status->token.uid;
        event.reported_effect = status->result;
        event.committed = status->committed;
        event.quiescent = status->quiescent;
    }
    if (memory != NULL && memory->direct_event_active != 0) {
        event.output_write_mask = memory->direct_event_write_status != 0 ?
            (operation == C42_FAKE_MEMORY_RESET_QUIESCENT ||
             operation == C42_FAKE_MEMORY_TEARDOWN_QUIESCENT ?
             C42_FAKE_EVENT_WRITE_VALUE : C42_FAKE_EVENT_WRITE_OBJECT) : 0;
        if (event.output_write_mask == 0) {
            event.output_value = 0;
        }
        event.reported_effect = memory->direct_event_logical_effect;
        event.requested_effect = memory->direct_event_requested_effect;
        event.applied_effect = memory->direct_event_applied_effect;
        event.object_uid = memory->direct_event_write_status != 0 ?
                           token_uid : 0;
        if (memory->direct_event_write_status != 0 &&
            memory->direct_event_token_variant ==
                C42_FAKE_MEMORY_TOKEN_ZERO) {
            event.object_uid = 0;
        } else if (memory->direct_event_write_status != 0 &&
                   memory->direct_event_token_variant ==
                       C42_FAKE_MEMORY_TOKEN_MISMATCH) {
            event.object_uid = token_uid + 1u;
        }
        if (operation == C42_FAKE_MEMORY_RESET_QUIESCENT ||
            operation == C42_FAKE_MEMORY_TEARDOWN_QUIESCENT) {
            event.object_uid = 0;
            event.output_structural_valid =
                (uint8_t)(event.output_write_mask != 0);
            event.output_record_match = event.output_structural_valid;
        } else {
            event.output_structural_valid = (uint8_t)(
                event.output_write_mask != 0 && event.object_uid != 0
            );
            event.output_record_match = (uint8_t)(
                event.output_write_mask != 0 && event.object_uid == token_uid
            );
        }
        event.committed = memory->direct_event_write_status != 0 ?
                          memory->direct_event_committed : 0;
        event.quiescent = memory->direct_event_write_status != 0 ?
                          memory->direct_event_quiescent : 0;
        if (memory->direct_event_apply_effect != 0) {
            event.flags |= C42_FAKE_EVENT_EFFECT_APPLIED;
            if (event.output_write_mask == 0 &&
                result == C42_MEMORY_IN_PROGRESS) {
                event.flags |= C42_FAKE_EVENT_RESPONSE_LOST;
            }
        }
    } else {
        event.output_write_mask = write_mask;
    }
    if (event.output_write_mask == 0) {
        event.output_structural_valid = 0;
        event.output_record_match = 0;
        event.output_value = 0;
        event.object_uid = 0;
        event.committed = 0;
        event.quiescent = 0;
    }
    c42_fake_event_append(
        memory == NULL ? NULL : memory->event_log, &event
    );
    if (memory != NULL) {
        memory->direct_event_active = 0;
        memory->direct_event_write_status = 0;
        memory->direct_event_apply_effect = 0;
        memory->direct_event_logical_effect = 0;
        memory->direct_event_requested_effect = 0;
        memory->direct_event_applied_effect = 0;
        memory->direct_event_committed = 0;
        memory->direct_event_quiescent = 0;
        memory->direct_event_token_variant = C42_FAKE_MEMORY_TOKEN_EXACT;
    }
}

static uint8_t memory_status_written(
    const struct c42_memory_status *status,
    const struct c42_memory_token *token)
{
    return status != NULL && token != NULL &&
           c42_memory_token_valid(token) &&
           token_equal(&status->token, token) &&
           status->result <= C42_MEMORY_RETIRED ?
           C42_FAKE_EVENT_WRITE_OBJECT : 0;
}

static int memory_status_structural_valid(
    const struct c42_memory_status *status)
{
    return status != NULL && c42_memory_token_valid(&status->token) &&
           status->result <= C42_MEMORY_RETIRED &&
           status->committed <= 1 && status->quiescent <= 1;
}

static int memory_status_matches_token(
    const struct c42_memory_status *status,
    const struct c42_memory_token *token)
{
    return memory_status_structural_valid(status) && token != NULL &&
           token_equal(&status->token, token);
}

static int memory_record_matches(
    const struct c42_fake_memory_operation_record *record,
    const struct c42_queue_memory_cap *capability,
    const struct c42_memory_token *token)
{
    return record != NULL && capability != NULL && token != NULL &&
           record->active != 0 &&
           cap_equal(&record->capability, capability) &&
           token_equal(&record->token, token);
}

static const struct c42_fake_memory_operation_record *memory_record_for(
    const struct c42_fake_memory *memory,
    const struct c42_queue_memory_cap *capability,
    uint32_t operation)
{
    if (memory == NULL || capability == NULL ||
        capability->queue_id >= C42_MAX_QUEUE_PAIRS) {
        return NULL;
    }
    if (operation == C42_FAKE_MEMORY_SCRUB ||
        operation == C42_FAKE_MEMORY_SCRUB_ABORT ||
        operation == C42_FAKE_MEMORY_SCRUB_RETIRE) {
        return &memory->scrub[capability->queue_id];
    }
    if (operation == C42_FAKE_MEMORY_BODY ||
        operation == C42_FAKE_MEMORY_MARKER) {
        return &memory->publication[capability->queue_id];
    }
    return NULL;
}

static enum c42_memory_result logged_validate(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint8_t role,
    uint32_t exact_bytes)
{
    enum c42_memory_result call =
        fake_validate(context, capability, role, exact_bytes);

    log_memory_event(
        context, C42_FAKE_MEMORY_VALIDATE, C42_FAKE_CALL_ACTION,
        call, exact_bytes, 0,
        c42_queue_memory_cap_valid(capability),
        context != NULL && capability != NULL &&
            mapping_for_const(context, capability) != NULL,
        1, 1,
        capability == NULL ? 0 : capability->memory_uid,
        NULL, role, exact_bytes
    );
    return call;
}

static enum c42_memory_result logged_capture(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    uint8_t *output,
    size_t output_size)
{
    enum c42_memory_result call = fake_capture(
        context, capability, slot, output, output_size
    );

    log_memory_event(
        context, C42_FAKE_MEMORY_CAPTURE, C42_FAKE_CALL_ACTION, call, slot,
        call == C42_MEMORY_OK ? C42_FAKE_EVENT_WRITE_OBJECT : 0,
        c42_queue_memory_cap_valid(capability),
        context != NULL && capability != NULL &&
            mapping_for_const(context, capability) != NULL,
        call == C42_MEMORY_OK && output != NULL &&
            output_size == C42_SQE_BYTES,
        call == C42_MEMORY_OK,
        capability == NULL ? 0 : capability->memory_uid,
        NULL, slot, (uint32_t)output_size
    );
    return call;
}

static enum c42_memory_result logged_scrub_start(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth,
    uint8_t inverse_phase,
    const struct c42_memory_token *token,
    struct c42_memory_status *status)
{
    int input_match = memory_record_matches(
        memory_record_for(context, capability, C42_FAKE_MEMORY_SCRUB),
        capability, token
    );
    enum c42_memory_result call = fake_scrub_start(
        context, capability, depth, inverse_phase, token, status
    );

    log_memory_event(
        context, C42_FAKE_MEMORY_SCRUB, C42_FAKE_CALL_START, call,
        status == NULL ? UINT32_MAX : status->result,
        memory_status_written(status, token), c42_memory_token_valid(token),
        input_match,
        memory_status_structural_valid(status),
        memory_status_matches_token(status, token),
        token == NULL ? 0 : token->uid, status, depth, inverse_phase
    );
    return call;
}

static enum c42_memory_result logged_scrub_query(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth,
    uint8_t inverse_phase,
    const struct c42_memory_token *token,
    struct c42_memory_status *status)
{
    int input_match = memory_record_matches(
        memory_record_for(context, capability, C42_FAKE_MEMORY_SCRUB),
        capability, token
    );
    enum c42_memory_result call = fake_scrub_query(
        context, capability, depth, inverse_phase, token, status
    );

    log_memory_event(
        context, C42_FAKE_MEMORY_SCRUB, C42_FAKE_CALL_QUERY, call,
        status == NULL ? UINT32_MAX : status->result,
        memory_status_written(status, token), c42_memory_token_valid(token),
        input_match,
        memory_status_structural_valid(status),
        memory_status_matches_token(status, token),
        token == NULL ? 0 : token->uid, status, depth, inverse_phase
    );
    return call;
}

static enum c42_memory_result logged_scrub_abort(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth,
    uint8_t inverse_phase,
    const struct c42_memory_token *token,
    struct c42_memory_status *status)
{
    int input_match = memory_record_matches(
        memory_record_for(context, capability, C42_FAKE_MEMORY_SCRUB_ABORT),
        capability, token
    );
    enum c42_memory_result call = fake_scrub_abort(
        context, capability, depth, inverse_phase, token, status
    );

    log_memory_event(
        context, C42_FAKE_MEMORY_SCRUB_ABORT, C42_FAKE_CALL_ACTION, call,
        status == NULL ? UINT32_MAX : status->result,
        memory_status_written(status, token), c42_memory_token_valid(token),
        input_match,
        memory_status_structural_valid(status),
        memory_status_matches_token(status, token),
        token == NULL ? 0 : token->uid, status, depth, inverse_phase
    );
    return call;
}

static enum c42_memory_result logged_scrub_retire_start(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth,
    uint8_t inverse_phase,
    const struct c42_memory_token *token,
    struct c42_memory_status *status)
{
    int input_match = memory_record_matches(
        memory_record_for(
            context, capability, C42_FAKE_MEMORY_SCRUB_RETIRE
        ), capability, token
    );
    enum c42_memory_result call = fake_scrub_retire_start(
        context, capability, depth, inverse_phase, token, status
    );

    log_memory_event(
        context, C42_FAKE_MEMORY_SCRUB_RETIRE, C42_FAKE_CALL_START, call,
        status == NULL ? UINT32_MAX : status->result,
        memory_status_written(status, token), c42_memory_token_valid(token),
        input_match,
        memory_status_structural_valid(status),
        memory_status_matches_token(status, token),
        token == NULL ? 0 : token->uid, status, depth, inverse_phase
    );
    return call;
}

static enum c42_memory_result logged_scrub_retire_query(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth,
    uint8_t inverse_phase,
    const struct c42_memory_token *token,
    struct c42_memory_status *status)
{
    int input_match = memory_record_matches(
        memory_record_for(
            context, capability, C42_FAKE_MEMORY_SCRUB_RETIRE
        ), capability, token
    );
    enum c42_memory_result call = fake_scrub_retire_query(
        context, capability, depth, inverse_phase, token, status
    );

    log_memory_event(
        context, C42_FAKE_MEMORY_SCRUB_RETIRE, C42_FAKE_CALL_QUERY, call,
        status == NULL ? UINT32_MAX : status->result,
        memory_status_written(status, token), c42_memory_token_valid(token),
        input_match,
        memory_status_structural_valid(status),
        memory_status_matches_token(status, token),
        token == NULL ? 0 : token->uid, status, depth, inverse_phase
    );
    return call;
}

static enum c42_memory_result logged_body_start(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    const uint8_t expected[C42_CQE_BYTES],
    const struct c42_memory_token *token,
    struct c42_memory_status *status)
{
    int input_match = memory_record_matches(
        memory_record_for(context, capability, C42_FAKE_MEMORY_BODY),
        capability, token
    );
    enum c42_memory_result call = fake_body_start(
        context, capability, slot, expected, token, status
    );

    log_memory_event(
        context, C42_FAKE_MEMORY_BODY, C42_FAKE_CALL_START, call,
        status == NULL ? UINT32_MAX : status->result,
        memory_status_written(status, token), c42_memory_token_valid(token),
        input_match,
        memory_status_structural_valid(status),
        memory_status_matches_token(status, token),
        token == NULL ? 0 : token->uid, status, slot, C42_CQE_BYTES
    );
    return call;
}

static enum c42_memory_result logged_body_query(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    const uint8_t expected[C42_CQE_BYTES],
    const struct c42_memory_token *token,
    struct c42_memory_status *status)
{
    int input_match = memory_record_matches(
        memory_record_for(context, capability, C42_FAKE_MEMORY_BODY),
        capability, token
    );
    enum c42_memory_result call = fake_body_query(
        context, capability, slot, expected, token, status
    );

    log_memory_event(
        context, C42_FAKE_MEMORY_BODY, C42_FAKE_CALL_QUERY, call,
        status == NULL ? UINT32_MAX : status->result,
        memory_status_written(status, token), c42_memory_token_valid(token),
        input_match,
        memory_status_structural_valid(status),
        memory_status_matches_token(status, token),
        token == NULL ? 0 : token->uid, status, slot, C42_CQE_BYTES
    );
    return call;
}

static enum c42_memory_result logged_marker_start(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    uint8_t marker,
    const struct c42_memory_token *token,
    struct c42_memory_status *status)
{
    const struct c42_fake_memory_operation_record *record =
        memory_record_for(context, capability, C42_FAKE_MEMORY_MARKER);
    int input_match = record != NULL && capability != NULL &&
        record->active != 0 && record->kind == C42_FAKE_MEMORY_BODY &&
        cap_equal(&record->capability, capability) &&
        record->slot == slot && record->prefix == 15 &&
        record->expected[14] == marker;
    enum c42_memory_result call = fake_marker_start(
        context, capability, slot, marker, token, status
    );

    log_memory_event(
        context, C42_FAKE_MEMORY_MARKER, C42_FAKE_CALL_START, call,
        status == NULL ? UINT32_MAX : status->result,
        memory_status_written(status, token), c42_memory_token_valid(token),
        input_match,
        memory_status_structural_valid(status),
        memory_status_matches_token(status, token),
        token == NULL ? 0 : token->uid, status, slot, marker
    );
    return call;
}

static enum c42_memory_result logged_marker_query(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    uint8_t marker,
    const struct c42_memory_token *token,
    struct c42_memory_status *status)
{
    int input_match = memory_record_matches(
        memory_record_for(context, capability, C42_FAKE_MEMORY_MARKER),
        capability, token
    );
    enum c42_memory_result call = fake_marker_query(
        context, capability, slot, marker, token, status
    );

    log_memory_event(
        context, C42_FAKE_MEMORY_MARKER, C42_FAKE_CALL_QUERY, call,
        status == NULL ? UINT32_MAX : status->result,
        memory_status_written(status, token), c42_memory_token_valid(token),
        input_match,
        memory_status_structural_valid(status),
        memory_status_matches_token(status, token),
        token == NULL ? 0 : token->uid, status, slot, marker
    );
    return call;
}

static enum c42_memory_result logged_reset_begin(
    void *context, uint64_t instance_nonce, uint32_t old_epoch)
{
    enum c42_memory_result call =
        fake_reset_begin(context, instance_nonce, old_epoch);

    log_memory_event(
        context, C42_FAKE_MEMORY_RESET_BEGIN, C42_FAKE_CALL_START,
        call, old_epoch, 0,
        instance_nonce != 0,
        context != NULL &&
            instance_nonce == ((struct c42_fake_memory *)context)->instance_nonce &&
            ((struct c42_fake_memory *)context)->reset_active != 0 &&
            old_epoch + 1u ==
                ((struct c42_fake_memory *)context)->controller_epoch,
        1, 1, old_epoch, NULL, old_epoch, 0
    );
    return call;
}

static enum c42_memory_result logged_reset_quiescent(
    void *context, uint64_t instance_nonce, uint32_t epoch, bool *quiescent)
{
    enum c42_memory_result call =
        fake_reset_quiescent(context, instance_nonce, epoch, quiescent);

    log_memory_event(
        context, C42_FAKE_MEMORY_RESET_QUIESCENT, C42_FAKE_CALL_QUERY, call,
        quiescent != NULL && *quiescent ? 1u : 0u,
        call == C42_MEMORY_OK, instance_nonce != 0,
        context != NULL &&
            instance_nonce == ((struct c42_fake_memory *)context)->instance_nonce &&
            ((struct c42_fake_memory *)context)->reset_active != 0 &&
            epoch + 1u == ((struct c42_fake_memory *)context)->controller_epoch,
        quiescent != NULL,
        call == C42_MEMORY_OK && quiescent != NULL && *quiescent,
        epoch,
        NULL, epoch, 0
    );
    return call;
}

static enum c42_memory_result logged_teardown_begin(
    void *context, uint64_t instance_nonce, uint32_t old_epoch)
{
    enum c42_memory_result call =
        fake_teardown_begin(context, instance_nonce, old_epoch);

    log_memory_event(
        context, C42_FAKE_MEMORY_TEARDOWN_BEGIN, C42_FAKE_CALL_START,
        call, old_epoch, 0,
        instance_nonce != 0,
        context != NULL &&
            instance_nonce == ((struct c42_fake_memory *)context)->instance_nonce &&
            ((struct c42_fake_memory *)context)->teardown_active != 0 &&
            ((struct c42_fake_memory *)context)->teardown_old_epoch == old_epoch,
        1, 1, old_epoch, NULL, old_epoch, 0
    );
    return call;
}

static enum c42_memory_result logged_teardown_quiescent(
    void *context, uint64_t instance_nonce, uint32_t epoch, bool *quiescent)
{
    enum c42_memory_result call =
        fake_teardown_quiescent(context, instance_nonce, epoch, quiescent);

    log_memory_event(
        context, C42_FAKE_MEMORY_TEARDOWN_QUIESCENT,
        C42_FAKE_CALL_QUERY, call,
        quiescent != NULL && *quiescent ? 1u : 0u,
        call == C42_MEMORY_OK, instance_nonce != 0,
        context != NULL &&
            instance_nonce == ((struct c42_fake_memory *)context)->instance_nonce &&
            ((struct c42_fake_memory *)context)->teardown_active != 0 &&
            ((struct c42_fake_memory *)context)->teardown_old_epoch == epoch,
        quiescent != NULL,
        call == C42_MEMORY_OK && quiescent != NULL && *quiescent,
        epoch,
        NULL, epoch, 0
    );
    return call;
}

static const struct c42_memory_ops C42_FAKE_MEMORY_OPS = {
    .version = C42_MEMORY_PORT_VERSION,
    .size = sizeof(struct c42_memory_ops),
    .validate = logged_validate,
    .capture = logged_capture,
    .scrub_start = logged_scrub_start,
    .scrub_query = logged_scrub_query,
    .scrub_abort = logged_scrub_abort,
    .scrub_retire_start = logged_scrub_retire_start,
    .scrub_retire_query = logged_scrub_retire_query,
    .body_start = logged_body_start,
    .body_query = logged_body_query,
    .marker_start = logged_marker_start,
    .marker_query = logged_marker_query,
    .reset_begin = logged_reset_begin,
    .reset_quiescent = logged_reset_quiescent,
    .teardown_begin = logged_teardown_begin,
    .teardown_quiescent = logged_teardown_quiescent,
};

struct c42_memory_port c42_fake_memory_port(struct c42_fake_memory *memory)
{
    struct c42_memory_port port = {0};

    port.ops = &C42_FAKE_MEMORY_OPS;
    port.context = memory;
    return port;
}
