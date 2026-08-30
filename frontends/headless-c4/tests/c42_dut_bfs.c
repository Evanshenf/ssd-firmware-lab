/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_dut_bfs.h"

#include "c42_reference.h"
#include "c42_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DUT_STATE_CAP 32768u
#define DUT_TRANSITION_CAP 262144u
#define DUT_DEPTH_CAP 20u
#define DUT_SUCCESSOR_CAP 8u
#define DUT_FAMILY_NODE_CAP 256u

struct dut_context {
    struct c42_test_fixture *fixture;
    struct c42_test_fixture *other;
    struct c42_operation_token candidate;
    struct c42_operation_token control;
    struct c42_target_ref target;
    struct c42_queue_memory_cap cap;
    struct c42_queue_descriptor descriptor;
    uint32_t family;
    uint32_t last_result;
    uint8_t cross_effect;
    uint8_t raw_cache_count;
    uint16_t raw_cache_id[2];
    uint8_t raw_cache[2][C42_SQE_BYTES];
};

struct dut_node {
    struct c42_reference_state state;
    uint16_t done;
    uint8_t depth;
    uint8_t action_index;
    uint32_t parent;
};

static int cache_active_commands(struct dut_context *context);

static const uint8_t family_depth[C42_REFERENCE_FAMILIES] = {
    4, 3, 4, 4, 4, 2, 2, 4, 4, 4, 4, 4
};

static int handle_equal(
    const struct fwlab_nvme_command_handle *left,
    const struct fwlab_nvme_command_handle *right)
{
    return left->instance_nonce == right->instance_nonce &&
           left->command_uid == right->command_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->generation == right->generation;
}

static struct c42_queue_memory_cap fresh_cq_cap(
    const struct c42_test_fixture *fixture,
    uint16_t queue_id)
{
    struct c42_queue_memory_cap cap = {0};

    cap.instance_nonce = fixture->config.instance_nonce;
    cap.owner_epoch = fixture->config.owner_epoch;
    cap.memory_uid = UINT64_C(0xb100000000000000) + queue_id;
    cap.controller_epoch = fixture->config.initial_controller_epoch;
    cap.ring_generation = 1;
    cap.mapping_generation = 1;
    cap.exact_bytes = (uint32_t)fixture->depth * C42_CQE_BYTES;
    cap.queue_id = queue_id;
    cap.role = C42_MEMORY_CQ_PUBLISH;
    return cap;
}

static struct c42_queue_descriptor cq_descriptor(
    const struct c42_test_fixture *fixture,
    const struct c42_queue_memory_cap *cap)
{
    struct c42_queue_descriptor descriptor = {0};

    descriptor.version = C42_COMPONENT_VERSION;
    descriptor.size = sizeof(descriptor);
    descriptor.queue_id = cap->queue_id;
    descriptor.associated_cq_id = cap->queue_id;
    descriptor.depth = fixture->depth;
    descriptor.kind = C42_QUEUE_CQ;
    descriptor.queue_class = FWLAB_NVME_QUEUE_IO;
    descriptor.memory = *cap;
    return descriptor;
}

static void reset_scenario_log(struct c42_test_fixture *fixture)
{
    c42_fake_event_log_init(&fixture->event_log);
}

static int dut_context_init(uint32_t family, struct dut_context *context)
{
    struct c42_fake_command_script script = {0};

    memset(context, 0, sizeof(*context));
    if (family >= C42_REFERENCE_FAMILIES) {
        return 0;
    }
    context->family = family;
    context->fixture = calloc(1, sizeof(*context->fixture));
    if (context->fixture == NULL ||
        !c42_test_fixture_init_with_nonce(
            context->fixture, family_depth[family], 0,
            UINT64_C(0xd000000000000000) + family + 1u)) {
        return 0;
    }
    reset_scenario_log(context->fixture);
    if (family == C42_REF_F02_BATCH || family == C42_REF_F05_DELAYED ||
        family == C42_REF_F11_RESET) {
        script.poll_delay = 100;
        c42_fake_command_set_script(&context->fixture->command, &script);
    } else if (family == C42_REF_F03_CAPTURE) {
        script.prepare_backpressure = 1;
        c42_fake_command_set_script(&context->fixture->command, &script);
    } else if (family == C42_REF_F09_PUBLICATION) {
        struct c42_fake_memory_outcome outcome = {0};

        outcome.operation = C42_FAKE_MEMORY_BODY;
        outcome.effect = C42_MEMORY_EXACT_PREFIX;
        outcome.prefix = 7;
        if (c42_fake_memory_script_push(
                &context->fixture->memory, &outcome) != C42_OK) {
            return 0;
        }
        outcome.effect = C42_MEMORY_FULL;
        outcome.prefix = 15;
        outcome.committed = 1;
        if (c42_fake_memory_script_push(
                &context->fixture->memory, &outcome) != C42_OK) {
            return 0;
        }
        memset(&outcome, 0, sizeof(outcome));
        outcome.operation = C42_FAKE_MEMORY_MARKER;
        outcome.effect = C42_MEMORY_UNKNOWN;
        outcome.committed = 1;
        if (c42_fake_memory_script_push(
                &context->fixture->memory, &outcome) != C42_OK) {
            return 0;
        }
        outcome.effect = C42_MEMORY_FULL;
        if (c42_fake_memory_script_push(
                &context->fixture->memory, &outcome) != C42_OK) {
            return 0;
        }
    } else if (family == C42_REF_F12_ISOLATION) {
        context->other = calloc(1, sizeof(*context->other));
        if (context->other == NULL ||
            !c42_test_fixture_init_with_nonce(
                context->other, family_depth[family], 0,
                UINT64_C(0xe000000000000000) + family + 1u)) {
            return 0;
        }
        script.poll_delay = 100;
        c42_fake_command_set_script(&context->fixture->command, &script);
        c42_fake_command_set_script(&context->other->command, &script);
        reset_scenario_log(context->other);
    }
    return 1;
}

static void dut_context_destroy(struct dut_context *context)
{
    free(context->other);
    free(context->fixture);
    memset(context, 0, sizeof(*context));
}

static enum c42_result step_one(struct c42_test_fixture *fixture)
{
    struct c42_step_result result = {0};

    return c42_step(fixture->controller, 1, &result);
}

static int run_to_active(
    struct c42_test_fixture *fixture,
    uint32_t active,
    uint16_t head)
{
    uint32_t step;

    for (step = 0; step < 256; ++step) {
        struct c42_snapshot snapshot = {0};
        enum c42_result result = step_one(fixture);

        if ((result != C42_OK && result != C42_FAULTED) ||
            c42_snapshot_read(fixture->controller, &snapshot) != C42_OK) {
            return 0;
        }
        if (snapshot.active_commands == active &&
            snapshot.sq[0].device_index == head) {
            return 1;
        }
    }
    return 0;
}

static int run_to_command_state(
    struct c42_test_fixture *fixture,
    uint16_t command_id,
    uint8_t state)
{
    uint32_t step;

    for (step = 0; step < 256; ++step) {
        struct c42_observer_v2 observer;
        uint16_t index;
        enum c42_result result = step_one(fixture);

        if ((result != C42_OK && result != C42_FAULTED) ||
            c42_observer_read_v2(fixture->controller, &observer) != C42_OK) {
            return 0;
        }
        for (index = 0; index < observer.command_capacity; ++index) {
            if (observer.commands[index].state == state &&
                observer.commands[index].command_id == command_id) {
                return 1;
            }
        }
    }
    return 0;
}

static enum c42_result apply_tail(
    struct c42_test_fixture *fixture,
    uint16_t new_tail,
    uint32_t epoch,
    uint32_t generation)
{
    struct c42_sq_tail_event event = {0};

    event.instance_nonce = fixture->config.instance_nonce;
    event.controller_epoch = epoch;
    event.ring_generation = generation;
    event.queue_id = 0;
    event.new_tail = new_tail;
    return c42_sq_tail_event_apply(fixture->controller, &event);
}

static int submit_batch(
    struct c42_test_fixture *fixture,
    uint16_t first,
    uint16_t second)
{
    uint8_t bytes[C42_SQE_BYTES];

    c42_test_sqe(bytes, 0x02, first, 1, first);
    if (c42_fake_memory_write_sqe(
            &fixture->memory, 0, 0, bytes) != C42_OK) {
        return 0;
    }
    c42_test_sqe(bytes, 0x02, second, 1, second);
    return c42_fake_memory_write_sqe(
               &fixture->memory, 0, 1, bytes) == C42_OK &&
           apply_tail(
               fixture, 2, fixture->config.initial_controller_epoch,
               fixture->sq_cap[0].ring_generation) == C42_OK;
}

static enum c42_result apply_ack(
    struct c42_test_fixture *fixture,
    uint16_t new_head)
{
    struct c42_cq_head_event event = {0};

    event.instance_nonce = fixture->config.instance_nonce;
    event.controller_epoch = fixture->config.initial_controller_epoch;
    event.ring_generation = fixture->cq_cap[0].ring_generation;
    event.queue_id = 0;
    event.new_head = new_head;
    return c42_cq_head_event_apply(fixture->controller, &event);
}

static int run_to_fault(struct c42_test_fixture *fixture)
{
    uint32_t step;

    for (step = 0; step < 128; ++step) {
        struct c42_snapshot snapshot = {0};
        enum c42_result result = step_one(fixture);

        if (result != C42_OK && result != C42_FAULTED) {
            return 0;
        }
        if (c42_snapshot_read(fixture->controller, &snapshot) != C42_OK) {
            return 0;
        }
        if (snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED) {
            return 1;
        }
    }
    return 0;
}

static int execute_action(struct dut_context *context, uint8_t action)
{
    struct c42_test_fixture *fixture = context->fixture;
    enum c42_result result = C42_OK;

    switch (action) {
    case C42_REF_CREATE_PREPARE:
        context->cap = fresh_cq_cap(fixture, 1);
        context->descriptor = cq_descriptor(fixture, &context->cap);
        if (c42_fake_memory_map(
                &fixture->memory, &context->cap, fixture->depth) != C42_OK) {
            return 0;
        }
        result = c42_candidate_prepare(
            fixture->controller, &context->descriptor, &context->candidate
        );
        break;
    case C42_REF_CREATE_PROGRESS:
        result = c42_candidate_progress(
            fixture->controller, &context->candidate, 1
        );
        break;
    case C42_REF_CREATE_COMMIT:
        result = c42_candidate_commit(
            fixture->controller, &context->candidate
        );
        break;
    case C42_REF_BATCH_SUBMIT:
        if (!submit_batch(fixture, 301, 302)) return 0;
        break;
    case C42_REF_BATCH_ACTIVE:
        if (!run_to_active(fixture, 2, 2)) return 0;
        break;
    case C42_REF_BATCH_PUBLISH: {
        struct c42_fake_command_script script = {0};

        c42_fake_command_set_script(&fixture->command, &script);
        if (!c42_test_run(fixture, 256, 4)) return 0;
        break;
    }
    case C42_REF_CAPTURE_SUBMIT:
        if (!c42_test_submit(fixture, 0, 0, 1, 303)) return 0;
        break;
    case C42_REF_CAPTURE_ONCE:
        result = step_one(fixture);
        break;
    case C42_REF_CAPTURE_MUTATE: {
        uint8_t bytes[C42_SQE_BYTES];

        c42_test_sqe(bytes, 0x01, 399, 2, 399);
        if (c42_fake_memory_write_sqe(
                &fixture->memory, 0, 0, bytes) != C42_OK) return 0;
        break;
    }
    case C42_REF_CAPTURE_BACKPRESSURE:
        result = step_one(fixture);
        break;
    case C42_REF_CAPTURE_PUBLISH:
        if (!run_to_active(fixture, 1, 1) ||
            !cache_active_commands(context) ||
            !c42_test_run(fixture, 256, 4)) return 0;
        break;
    case C42_REF_INVALID_TAIL:
        result = apply_tail(
            fixture, fixture->depth,
            fixture->config.initial_controller_epoch,
            fixture->sq_cap[0].ring_generation
        );
        break;
    case C42_REF_STALE_TAIL:
        result = apply_tail(
            fixture, 1, fixture->config.initial_controller_epoch + 1u,
            fixture->sq_cap[0].ring_generation
        );
        break;
    case C42_REF_DUPLICATE_CID: {
        struct c42_fake_command_script script = {0};

        script.poll_delay = 100;
        c42_fake_command_set_script(&fixture->command, &script);
        if (!c42_test_submit(fixture, 0, 0, 1, 304) ||
            !run_to_active(fixture, 1, 1) ||
            !cache_active_commands(context) ||
            !c42_test_submit(fixture, 0, 1, 2, 304) ||
            !c42_test_run(fixture, 32, 1)) return 0;
        {
            struct c42_snapshot snapshot = {0};

            if (c42_snapshot_read(
                    fixture->controller, &snapshot) != C42_OK) return 0;
            result = snapshot.phase ==
                     C42_CONTROLLER_FAULTED_RESET_REQUIRED ?
                     C42_FAULTED : C42_OK;
        }
        break;
    }
    case C42_REF_DELAY_SUBMIT:
        if (!submit_batch(fixture, 305, 306)) return 0;
        break;
    case C42_REF_DELAY_ACTIVE:
        if (!run_to_active(fixture, 2, 2)) return 0;
        break;
    case C42_REF_DELAY_PUBLISH: {
        struct c42_fake_command_script script = {0};

        script.reverse_ready = 1;
        c42_fake_command_set_script(&fixture->command, &script);
        if (!c42_test_run(fixture, 256, 4)) return 0;
        break;
    }
    case C42_REF_PHASE_FIRST:
        if (!c42_test_submit(fixture, 0, 0, 1, 307) ||
            !c42_test_run(fixture, 128, 4)) return 0;
        break;
    case C42_REF_PHASE_ACK_ONE:
        result = apply_ack(fixture, 1);
        break;
    case C42_REF_PHASE_SECOND:
        if (!c42_test_submit(fixture, 0, 1, 0, 308) ||
            !c42_test_run(fixture, 128, 4)) return 0;
        break;
    case C42_REF_PHASE_ACK_TWO:
        result = apply_ack(fixture, 0);
        break;
    case C42_REF_PHASE_THIRD:
        if (!c42_test_submit(fixture, 0, 0, 1, 316) ||
            !c42_test_run(fixture, 128, 4)) return 0;
        break;
    case C42_REF_FULL_FILL:
        if (!c42_test_submit(fixture, 0, 0, 1, 309) ||
            !c42_test_run(fixture, 128, 4)) return 0;
        break;
    case C42_REF_FULL_SUBMIT:
        if (!c42_test_submit(fixture, 0, 1, 0, 310) ||
            !c42_test_run(fixture, 32, 1)) return 0;
        break;
    case C42_REF_FULL_PROBE:
        if (!c42_test_run(fixture, 16, 1)) return 0;
        break;
    case C42_REF_FULL_ACK_RESUME:
        if (apply_ack(fixture, 1) != C42_OK ||
            !c42_test_run(fixture, 128, 4)) return 0;
        break;
    case C42_REF_IDENTITY_MARKER:
        if (!c42_test_submit(fixture, 0, 0, 1, 311) ||
            !run_to_command_state(
                fixture, 311, C42_OBSERVER_COMMAND_MARKER_RECONCILE)) return 0;
        break;
    case C42_REF_IDENTITY_TARGET:
        result = c42_target_prepare(
            fixture->controller, 0, fixture->sq_cap[0].ring_generation,
            311, &context->target
        );
        break;
    case C42_REF_IDENTITY_DUPLICATE:
        if (!c42_test_submit(fixture, 0, 1, 2, 311)) return 0;
        result = C42_OK;
        break;
    case C42_REF_IDENTITY_CROSS_COMMIT:
        result = step_one(fixture);
        break;
    case C42_REF_IDENTITY_TARGET_RELEASE:
        result = c42_target_release(
            fixture->controller, &context->target.token
        );
        break;
    case C42_REF_PUBLICATION_SUBMIT:
        if (!c42_test_submit(fixture, 0, 0, 1, 312) ||
            !run_to_command_state(
                fixture, 312, C42_OBSERVER_COMMAND_PUB_RESERVED)) return 0;
        break;
    case C42_REF_PUBLICATION_BODY_PREFIX:
    case C42_REF_PUBLICATION_BODY_FULL:
    case C42_REF_PUBLICATION_MARKER_UNKNOWN:
    case C42_REF_PUBLICATION_MARKER_FULL:
    case C42_REF_PUBLICATION_CROSS_COMMIT:
        result = step_one(fixture);
        break;
    case C42_REF_DELETE_PUBLISH:
        if (!c42_test_submit(fixture, 0, 0, 1, 313) ||
            !c42_test_run(fixture, 128, 4)) return 0;
        break;
    case C42_REF_DELETE_SQ: {
        struct c42_control_status status = {0};

        if (c42_delete_start(
                fixture->controller, C42_QUEUE_SQ, 0,
                &context->control) != C42_OK ||
            c42_control_progress(
                fixture->controller, &context->control, 16) != C42_OK ||
            c42_control_query(
                fixture->controller, &context->control, &status) != C42_OK ||
            status.state != C42_CONTROL_COMMITTED) return 0;
        break;
    }
    case C42_REF_DELETE_CQ_PROBE:
        result = c42_delete_start(
            fixture->controller, C42_QUEUE_CQ, 0, &context->control
        );
        break;
    case C42_REF_DELETE_RECREATE_PROBE: {
        struct c42_queue_descriptor descriptor = {0};
        struct c42_queue_memory_cap cap = fixture->sq_cap[0];
        struct c42_operation_token token = {0};

        cap.memory_uid++;
        cap.ring_generation = 2;
        cap.mapping_generation = 2;
        descriptor.version = C42_COMPONENT_VERSION;
        descriptor.size = sizeof(descriptor);
        descriptor.queue_id = 0;
        descriptor.associated_cq_id = 0;
        descriptor.depth = fixture->depth;
        descriptor.kind = C42_QUEUE_SQ;
        descriptor.queue_class = FWLAB_NVME_QUEUE_ADMIN;
        descriptor.memory = cap;
        if (c42_fake_memory_map(
                &fixture->memory, &cap, fixture->depth) != C42_OK) return 0;
        result = c42_candidate_prepare(
            fixture->controller, &descriptor, &token
        );
        break;
    }
    case C42_REF_RESET_ACTIVE:
        if (!c42_test_submit(fixture, 0, 0, 1, 314) ||
            !run_to_active(fixture, 1, 1)) return 0;
        break;
    case C42_REF_RESET_START:
        result = c42_reset_start(
            fixture->controller, &context->control
        );
        break;
    case C42_REF_RESET_COLD: {
        struct c42_control_status status = {0};

        if (c42_control_progress(
                fixture->controller, &context->control, 32) != C42_OK ||
            c42_control_query(
                fixture->controller, &context->control, &status) != C42_OK ||
            status.state != C42_CONTROL_COMMITTED) return 0;
        break;
    }
    case C42_REF_ISOLATION_LEFT:
        if (!c42_test_submit(fixture, 0, 0, 1, 315) ||
            !run_to_active(fixture, 1, 1)) return 0;
        break;
    case C42_REF_ISOLATION_RIGHT:
        if (context->other == NULL ||
            !c42_test_submit(context->other, 0, 0, 1, 315) ||
            !run_to_active(context->other, 1, 1)) return 0;
        break;
    case C42_REF_ISOLATION_CROSS_REJECT:
        if (c42_target_prepare(
                fixture->controller, 0,
                fixture->sq_cap[0].ring_generation,
                315, &context->target) != C42_OK) return 0;
        result = c42_target_release(
            context->other->controller, &context->target.token
        );
        context->cross_effect = 0;
        break;
    case C42_REF_ADMISSION_POISON: {
        struct c42_fake_command_injection injection = {0};

        injection.operation = C42_FAKE_COMMAND_ADMIT;
        injection.result = FWLAB_HIF_PORT_COUNTER_EXHAUSTED + 1u;
        injection.omit_outputs = 1;
        if (c42_fake_command_injection_push(
                &fixture->command, &injection) != C42_OK ||
            !c42_test_submit(fixture, 0, 0, 1, 320) ||
            !run_to_fault(fixture)) return 0;
        result = C42_FAULTED;
        break;
    }
    case C42_REF_ACK_NONCOMMITTED:
        if (!c42_test_submit(fixture, 0, 0, 1, 321) ||
            !run_to_command_state(
                fixture, 321, C42_OBSERVER_COMMAND_PUB_RESERVED)) return 0;
        result = apply_ack(fixture, 1);
        break;
    case C42_REF_TARGET_GENERATION_MISMATCH: {
        struct c42_fake_command_script script = {0};

        script.poll_delay = 100;
        c42_fake_command_set_script(&fixture->command, &script);
        if (!c42_test_submit(fixture, 0, 0, 1, 322) ||
            !run_to_active(fixture, 1, 1) ||
            !cache_active_commands(context)) return 0;
        result = c42_target_prepare(
            fixture->controller, 0,
            fixture->sq_cap[0].ring_generation + 1u,
            322, &context->target
        );
        break;
    }
    case C42_REF_DELETE_PENDING_PROBE: {
        struct c42_control_status status = {0};

        if (!submit_batch(fixture, 323, 324) ||
            c42_delete_start(
                fixture->controller, C42_QUEUE_SQ, 0,
                &context->control) != C42_OK ||
            c42_control_progress(
                fixture->controller, &context->control, 2) != C42_OK ||
            c42_control_query(
                fixture->controller, &context->control, &status) != C42_OK)
            return 0;
        break;
    }
    case C42_REF_SQHD_DELAY: {
        struct c42_fake_command_script script = {0};

        script.inject_operation = C42_FAKE_COMMAND_CONSUME_PREPARE;
        script.inject_result = FWLAB_HIF_PORT_IN_PROGRESS;
        script.inject_count = 8;
        script.inject_omit_outputs = 1;
        c42_fake_command_set_script(&fixture->command, &script);
        if (!submit_batch(fixture, 330, 331) ||
            !c42_test_run(fixture, 512, 1)) return 0;
        break;
    }
    default:
        return 0;
    }
    context->last_result = (uint32_t)result;
    return result == C42_OK || result == C42_FAULTED ||
           result == C42_STALE || result == C42_WRONG_STATE ||
           result == C42_INVALID || result == C42_TOO_LATE ||
           result == C42_NOT_FOUND;
}

static uint8_t command_identity_ok(
    const struct c42_observer_command_v2 *command)
{
    uint8_t valid = 1;

    if (command->state >= C42_OBSERVER_COMMAND_PORT_RESERVED) {
        valid &= command->prepared_origin_matches;
    }
    if (command->state >= C42_OBSERVER_COMMAND_PORT_COMMITTED) {
        valid &= command->ticket_identity_matches;
    }
    if (command->state >= C42_OBSERVER_COMMAND_READY) {
        valid &= command->ready_ticket_matches;
    }
    if (command->state >= C42_OBSERVER_COMMAND_LEASED) {
        valid &= command->lease_ticket_matches;
    }
    if (command->state >= C42_OBSERVER_COMMAND_CONSUME_PREPARE) {
        valid &= command->consume_known;
    }
    return valid;
}

static void cache_raw(
    struct dut_context *context,
    const struct c42_observer_command_v2 *command)
{
    uint32_t record_index;
    uint8_t cache_index;

    if (command->state < C42_OBSERVER_COMMAND_HIF_COMMITTED) {
        return;
    }
    for (cache_index = 0; cache_index < context->raw_cache_count;
         ++cache_index) {
        if (context->raw_cache_id[cache_index] == command->command_id) {
            return;
        }
    }
    if (context->raw_cache_count >= 2) {
        return;
    }
    for (record_index = 0; record_index < C42_FAKE_COMMAND_RECORDS;
         ++record_index) {
        const struct c42_fake_command_record *record =
            &context->fixture->command.records[record_index];

        if (record->in_use != 0 &&
            handle_equal(&record->ticket.handle, &command->handle)) {
            cache_index = context->raw_cache_count;
            if (c42_raw_snapshot_copy(
                    context->fixture->controller, &command->handle,
                    &record->ticket.origin,
                    context->raw_cache[cache_index]) == C42_OK) {
                context->raw_cache_id[cache_index] = command->command_id;
                context->raw_cache_count++;
            }
            return;
        }
    }
}

static int cache_active_commands(struct dut_context *context)
{
    struct c42_observer_v2 observer;
    uint16_t index;

    if (c42_observer_read_v2(
            context->fixture->controller, &observer) != C42_OK) return 0;
    for (index = 0; index < observer.command_capacity; ++index) {
        if (observer.commands[index].state != C42_OBSERVER_COMMAND_FREE) {
            cache_raw(context, &observer.commands[index]);
        }
    }
    return 1;
}

static uint32_t provider_order_mask(const struct c42_fake_event_log *log)
{
    static const uint8_t providers[] = {
        C42_FAKE_EVENT_MEMORY, C42_FAKE_EVENT_COMMAND,
        C42_FAKE_EVENT_COMMAND, C42_FAKE_EVENT_COMMAND,
        C42_FAKE_EVENT_COMMAND, C42_FAKE_EVENT_COMMAND,
        C42_FAKE_EVENT_MEMORY, C42_FAKE_EVENT_MEMORY,
        C42_FAKE_EVENT_COMMAND,
    };
    static const uint32_t operations[] = {
        C42_FAKE_MEMORY_CAPTURE, C42_FAKE_COMMAND_PREPARE,
        C42_FAKE_COMMAND_ADMIT, C42_FAKE_COMMAND_POLL,
        C42_FAKE_COMMAND_COMPLETION_ACQUIRE,
        C42_FAKE_COMMAND_CONSUME_PREPARE,
        C42_FAKE_MEMORY_BODY, C42_FAKE_MEMORY_MARKER,
        C42_FAKE_COMMAND_CONSUME_COMMIT,
    };
    uint32_t expected = 0;
    uint32_t index;
    uint32_t mask = 0;

    for (index = 0; index < log->count && expected < 9; ++index) {
        if (log->events[index].provider == providers[expected] &&
            log->events[index].operation == operations[expected]) {
            mask |= UINT32_C(1) << expected;
            expected++;
        }
    }
    return mask;
}

static int normalize(
    struct dut_context *context,
    struct c42_reference_state *state)
{
    struct c42_observer_v2 observer;
    struct c42_snapshot snapshot;
    uint16_t index;
    uint8_t command_slot = 0;
    uint8_t candidate_seen = 0;
    uint8_t control_seen = 0;
    uint8_t notification_seen = 0;
    uint8_t cache_index;

    memset(state, 0, sizeof(*state));
    if (c42_observer_read_v2(
            context->fixture->controller, &observer) != C42_OK ||
        c42_snapshot_read(
            context->fixture->controller, &snapshot) != C42_OK) {
        return 0;
    }
    state->family = context->family;
    state->phase = observer.phase;
    state->controller_epoch = observer.controller_epoch;
    state->fault_cause = observer.fault_cause;
    state->last_result = context->last_result;
    state->active_identity_count = (uint8_t)snapshot.active_commands;
    state->capture_count = context->fixture->memory.capture_count;
    state->acquire_count = context->fixture->command.acquire_count;
    for (index = 0; index < C42_MAX_QUEUE_PAIRS; ++index) {
        state->sq_ring_generation[index] = observer.sq[index].ring_generation;
        state->sq_mapping_generation[index] =
            observer.sq[index].mapping_generation;
        state->cq_ring_generation[index] = observer.cq[index].ring_generation;
        state->cq_mapping_generation[index] =
            observer.cq[index].mapping_generation;
        state->sq_host_tail[index] = observer.sq[index].host_index;
        state->sq_device_head[index] = observer.sq[index].device_index;
        state->sq_pending[index] = observer.sq[index].pending;
        state->cq_host_head[index] = observer.cq[index].host_index;
        state->cq_device_tail[index] = observer.cq[index].device_index;
        state->cq_unacked[index] = observer.cq[index].unacked_count;
        state->cq_reserved[index] = observer.cq[index].reserved_count;
        state->sq_life[index] = observer.sq[index].life;
        state->cq_life[index] = observer.cq[index].life;
        state->cq_phase[index] = observer.cq[index].phase;
    }
    for (index = 0; index < C42_MAX_QUEUE_PAIRS * 2u; ++index) {
        if (observer.candidates[index].in_use != 0 && candidate_seen == 0) {
            state->candidate_state = (uint8_t)observer.candidates[index].state;
            candidate_seen = 1;
        }
    }
    for (index = 0; index < 4; ++index) {
        if (observer.controls[index].in_use != 0 && control_seen == 0) {
            state->control_state = (uint8_t)observer.controls[index].state;
            control_seen = 1;
        }
    }
    for (index = 0; index < observer.command_capacity; ++index) {
        if (observer.commands[index].state != C42_OBSERVER_COMMAND_FREE) {
            cache_raw(context, &observer.commands[index]);
        }
    }
    for (;;) {
        uint16_t selected = UINT16_MAX;
        uint16_t selected_cid = UINT16_MAX;

        for (index = 0; index < observer.command_capacity; ++index) {
            uint16_t cid = observer.commands[index].command_id;
            uint8_t already = 0;
            uint8_t prior;

            if (observer.commands[index].state == C42_OBSERVER_COMMAND_FREE) {
                continue;
            }
            for (prior = 0; prior < command_slot; ++prior) {
                if (state->command_id[prior] == cid) already = 1;
            }
            if (already == 0 && cid < selected_cid) {
                selected = index;
                selected_cid = cid;
            }
        }
        if (selected == UINT16_MAX) break;
        state->command_count++;
        if (command_slot < 2) {
            const struct c42_observer_command_v2 *command =
                &observer.commands[selected];
            uint16_t cq_slot = command->cq_slot;

            state->command_id[command_slot] = command->command_id;
            state->command_state[command_slot] = command->state;
            state->command_sqhd[command_slot] = command->sqhd_snapshot;
            state->command_identity_ok[command_slot] =
                command_identity_ok(command);
            if (cq_slot < 2) {
                state->body_prefix[cq_slot] =
                    (uint8_t)observer.publications[selected].body_prefix;
                state->marker_visible[cq_slot] =
                    observer.publications[selected].marker_visible;
                state->reconcile_state[cq_slot] =
                    observer.reconciles[selected].state;
            }
            command_slot++;
        }
        if (command_slot >= 2) break;
    }
    for (cache_index = 0;
         state->phase != C42_CONTROLLER_COLD_NO_QUEUES &&
         state->phase != C42_CONTROLLER_DEAD &&
         cache_index < context->raw_cache_count;
         ++cache_index) {
        state->raw_valid[cache_index] = 1;
        memcpy(
            state->raw[cache_index], context->raw_cache[cache_index],
            C42_SQE_BYTES
        );
        if (state->command_id[cache_index] == 0) {
            state->command_id[cache_index] =
                context->raw_cache_id[cache_index];
            state->command_identity_ok[cache_index] = 1;
        }
    }
    for (index = 0; index < 2; ++index) {
        state->slot_state[index] = observer.cq[0].slots[index].state;
        if (observer.cq[0].slots[index].state != C42_OBSERVER_SLOT_FREE) {
            state->cqe_valid[index] = 1;
            memcpy(
                state->cqe[index], observer.cq[0].slots[index].wire,
                C42_CQE_BYTES
            );
        }
        if ((context->family == C42_REF_F08_IDENTITY ||
             context->family == C42_REF_F09_PUBLICATION) &&
            observer.cq[0].slots[index].state != C42_OBSERVER_SLOT_FREE) {
            state->media_cqe_valid[index] = 1;
            memcpy(
                state->media_cqe[index], context->fixture->memory.cq[0][index],
                C42_CQE_BYTES
            );
        }
    }
    for (index = 0; index < observer.target_capacity; ++index) {
        if (observer.targets[index].in_use != 0) {
            state->target_count++;
            state->target_identity_ok |=
                observer.targets[index].identity_matches_active;
        }
    }
    for (index = 0; index < observer.command_capacity; ++index) {
        if (observer.notifications[index].in_use != 0) {
            state->notification_count++;
            if (notification_seen == 0) {
                state->notification_state =
                    observer.notifications[index].state;
                notification_seen = 1;
            }
        }
    }
    for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
        const struct c42_fake_command_record *record =
            &context->fixture->command.records[index];

        if (record->in_use != 0) {
            state->port_records++;
            if (record->admitted != 0) state->port_admitted++;
            if (record->ready_sent != 0) state->port_ready++;
            if (record->leased != 0) state->port_leased++;
            if (record->consume_prepared != 0)
                state->port_consume_prepared++;
            if (record->consume_committed != 0)
                state->port_consume_committed++;
            if (record->retired != 0) state->port_retired++;
        }
    }
    state->order_mask = provider_order_mask(&context->fixture->event_log);
    if (state->order_mask != UINT32_C(0x1ff)) state->order_mask = 0;
    if (context->other != NULL) {
        struct c42_snapshot other = {0};

        if (c42_snapshot_read(
                context->other->controller, &other) != C42_OK) return 0;
        state->other_active = (uint8_t)other.active_commands;
    }
    state->cross_effect = context->cross_effect;
    return 1;
}

static int replay_path(
    uint32_t family,
    const uint8_t *actions,
    uint8_t depth,
    struct c42_reference_state *actual)
{
    struct dut_context context;
    uint8_t index;
    int ok = 0;

    if (!dut_context_init(family, &context)) {
        fprintf(stderr,
                "C4.2 DUT reference FAIL: family=%s path=<bootstrap> "
                "reason=fixture-init\n",
                c42_reference_family_name(family));
        dut_context_destroy(&context);
        return 0;
    }
    for (index = 0; index < depth; ++index) {
        struct c42_reference_state intermediate;

        if (!execute_action(&context, actions[index]) ||
            !normalize(&context, &intermediate)) {
            fprintf(stderr,
                    "C4.2 DUT reference FAIL: family=%s action=%u step=%u\n",
                    c42_reference_family_name(family), actions[index], index);
            goto done;
        }
    }
    if (!normalize(&context, actual)) {
        fprintf(stderr, "C4.2 DUT reference FAIL: family=%s final-observe\n",
                c42_reference_family_name(family));
        goto done;
    }
    ok = 1;
done:
    dut_context_destroy(&context);
    return ok;
}

static void print_state_difference(
    const char *family,
    const char *path,
    const struct c42_reference_state *expected,
    const struct c42_reference_state *actual)
{
    const uint8_t *left = (const uint8_t *)expected;
    const uint8_t *right = (const uint8_t *)actual;
    size_t offset;

    for (offset = 0; offset < sizeof(*expected); ++offset) {
        if (left[offset] != right[offset]) break;
    }
    fprintf(stderr,
            "C4.2 DUT reference FAIL: family=%s path=%s offset=%zu "
            "expected=%u actual=%u phase=%u/%u head=%u/%u "
            "active=%u/%u slot=%u/%u port=%u,%u,%u,%u,%u,%u,%u/"
            "%u,%u,%u,%u,%u,%u,%u notify=%u/%u\n",
            family, path, offset,
            offset < sizeof(*expected) ? left[offset] : 0,
            offset < sizeof(*actual) ? right[offset] : 0,
            expected->phase, actual->phase,
            expected->sq_device_head[0], actual->sq_device_head[0],
            expected->command_count, actual->command_count,
            expected->slot_state[0], actual->slot_state[0],
            expected->port_records, expected->port_admitted,
            expected->port_ready, expected->port_leased,
            expected->port_consume_prepared,
            expected->port_consume_committed, expected->port_retired,
            actual->port_records, actual->port_admitted,
            actual->port_ready, actual->port_leased,
            actual->port_consume_prepared,
            actual->port_consume_committed, actual->port_retired,
            expected->notification_count, actual->notification_count);
}

static int action_before(
    const struct c42_reference_action *left,
    const struct c42_reference_action *right)
{
    if (left->ordinal != right->ordinal) {
        return left->ordinal < right->ordinal;
    }
    return strcmp(left->name, right->name) < 0;
}

static int node_seen(
    const struct dut_node *nodes,
    uint32_t count,
    const struct dut_node *candidate)
{
    uint32_t index;

    for (index = 0; index < count; ++index) {
        if (nodes[index].done == candidate->done &&
            c42_reference_state_equal(
                &nodes[index].state, &candidate->state)) return 1;
    }
    return 0;
}

static uint8_t build_path(
    const struct dut_node *nodes,
    uint32_t parent,
    uint8_t selected,
    uint8_t output[DUT_DEPTH_CAP],
    char *text,
    size_t text_size,
    uint32_t family)
{
    uint8_t reverse[DUT_DEPTH_CAP];
    uint8_t count = 0;
    uint8_t index;
    size_t used = 0;

    reverse[count++] = selected;
    while (nodes[parent].depth != 0 && count < DUT_DEPTH_CAP) {
        reverse[count++] = nodes[parent].action_index;
        parent = nodes[parent].parent;
    }
    for (index = 0; index < count; ++index) {
        uint8_t action_index = reverse[count - index - 1u];
        const struct c42_reference_action *action =
            c42_reference_action_at(family, action_index);
        int written;

        output[index] = action->id;
        written = snprintf(
            text + used, used < text_size ? text_size - used : 0,
            "%s%s", index == 0 ? "" : ">", action->name
        );
        if (written < 0 || (size_t)written >= text_size - used) return 0;
        used += (size_t)written;
    }
    return count;
}

static int explore_family(
    uint32_t family,
    struct c42_dut_bfs_summary *summary)
{
    struct dut_node *nodes = calloc(DUT_FAMILY_NODE_CAP, sizeof(*nodes));
    struct c42_reference_state actual;
    uint32_t head = 0;
    uint32_t count = 1;
    int ok = 0;

    if (nodes == NULL ||
        !c42_reference_initial(family, &nodes[0].state)) goto done;
    if (!replay_path(family, NULL, 0, &actual)) goto done;
    if (!c42_reference_state_equal(&nodes[0].state, &actual)) {
        print_state_difference(
            c42_reference_family_name(family), "<root>",
            &nodes[0].state, &actual
        );
        goto done;
    }
    summary->states++;
    summary->comparisons++;
    while (head < count) {
        uint8_t order[C42_REFERENCE_MAX_ACTIONS];
        uint8_t enabled = 0;
        uint8_t action_index;
        uint32_t action_count = c42_reference_action_count(family);

        for (action_index = 0; action_index < action_count; ++action_index) {
            const struct c42_reference_action *action =
                c42_reference_action_at(family, action_index);

            if ((nodes[head].done & (uint16_t)(1u << action_index)) == 0 &&
                (nodes[head].done & action->prerequisites) ==
                    action->prerequisites &&
                (nodes[head].done & action->forbidden) == 0) {
                uint8_t position = enabled;

                while (position != 0 && action_before(
                           action,
                           c42_reference_action_at(
                               family, order[position - 1u]))) {
                    order[position] = order[position - 1u];
                    position--;
                }
                order[position] = action_index;
                enabled++;
            }
        }
        if (enabled > DUT_SUCCESSOR_CAP) goto done;
        if (enabled > summary->maximum_successors) {
            summary->maximum_successors = enabled;
        }
        for (action_index = 0; action_index < enabled; ++action_index) {
            uint8_t selected = order[action_index];
            const struct c42_reference_action *action =
                c42_reference_action_at(family, selected);
            struct dut_node next = nodes[head];
            uint8_t path[DUT_DEPTH_CAP];
            char path_text[512] = {0};
            uint8_t depth;

            if (next.depth >= DUT_DEPTH_CAP) goto done;
            summary->transitions++;
            if (summary->transitions > DUT_TRANSITION_CAP ||
                !c42_reference_transition(
                    &nodes[head].state, action->id, &next.state)) goto done;
            next.done |= (uint16_t)(1u << selected);
            next.depth++;
            next.parent = head;
            next.action_index = selected;
            depth = build_path(
                nodes, head, selected, path, path_text,
                sizeof(path_text), family
            );
            if (depth != next.depth ||
                !replay_path(family, path, depth, &actual)) goto done;
            summary->comparisons++;
            if (!c42_reference_state_equal(&next.state, &actual)) {
                print_state_difference(
                    c42_reference_family_name(family), path_text,
                    &next.state, &actual
                );
                goto done;
            }
            if (next.depth > summary->maximum_depth) {
                summary->maximum_depth = next.depth;
            }
            if (!node_seen(nodes, count, &next)) {
                if (count >= DUT_FAMILY_NODE_CAP ||
                    summary->states >= DUT_STATE_CAP) goto done;
                nodes[count++] = next;
                summary->states++;
            }
        }
        head++;
    }
    ok = 1;
done:
    free(nodes);
    return ok;
}

int c42_dut_bfs_run(
    const char *only_family,
    struct c42_dut_bfs_summary *summary)
{
    struct c42_dut_bfs_summary local = {0};
    uint32_t family;

    if (summary == NULL) return 0;
    for (family = 0; family < C42_REFERENCE_FAMILIES; ++family) {
        const char *name = c42_reference_family_name(family);

        if (only_family != NULL && strcmp(only_family, name) != 0) continue;
        if (!explore_family(family, &local)) return 0;
        local.families++;
    }
    if (local.families == 0 || local.states > DUT_STATE_CAP ||
        local.transitions > DUT_TRANSITION_CAP ||
        local.maximum_depth > DUT_DEPTH_CAP ||
        local.maximum_successors > DUT_SUCCESSOR_CAP) return 0;
    *summary = local;
    return 1;
}
