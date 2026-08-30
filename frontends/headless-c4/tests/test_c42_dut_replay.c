/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_reference.h"
#include "c42_dut_bfs.h"
#include "c42_support.h"

#include <stdio.h>
#include <setjmp.h>
#include <string.h>

static uint32_t failures;
static uint32_t families;
static uint32_t nodes;
static uint32_t comparisons;
static uint32_t replay_paths;
static uint32_t path_step;
static uint32_t cut_limit = UINT32_MAX;
static jmp_buf cut_jump;

static void verify(int condition, const char *name)
{
    nodes++;
    comparisons++;
    if (!condition) {
        fprintf(stderr, "C4.2 DUT replay FAIL: %s\n", name);
        failures++;
    }
    path_step++;
    if (path_step == cut_limit) {
        longjmp(cut_jump, 1);
    }
}

static int observe(
    struct c42_test_fixture *fixture,
    struct c42_observer_v2 *observer)
{
    return c42_observer_read_v2(fixture->controller, observer) == C42_OK;
}

static int run_to_active(
    struct c42_test_fixture *fixture,
    uint32_t active,
    uint16_t head)
{
    uint32_t step;

    for (step = 0; step < 128; ++step) {
        struct c42_step_result result = {0};
        struct c42_snapshot snapshot = {0};

        if (c42_step(fixture->controller, 1, &result) != C42_OK ||
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

static int exact_cqe(
    struct c42_test_fixture *fixture,
    uint16_t slot,
    uint32_t result,
    uint16_t sqhd,
    uint16_t cid,
    uint8_t phase)
{
    uint8_t actual[C42_CQE_BYTES];
    uint8_t expected[C42_REFERENCE_CQE_BYTES];

    c42_reference_build_cqe(
        result, sqhd, 0, cid, phase, 0, 0, 0, 0, 0, expected
    );
    if (c42_fake_memory_read_cqe(
            &fixture->memory, 0, slot, actual) != C42_OK) {
        return 0;
    }
    if (!c42_reference_bytes_equal(actual, expected, sizeof(actual))) {
        size_t index;

        fprintf(stderr, "C4.2 DUT replay CQE mismatch slot=%u actual=",
                slot);
        for (index = 0; index < sizeof(actual); ++index) {
            fprintf(stderr, "%02x", actual[index]);
        }
        fprintf(stderr, " expected=");
        for (index = 0; index < sizeof(expected); ++index) {
            fprintf(stderr, "%02x", expected[index]);
        }
        fprintf(stderr, "\n");
        return 0;
    }
    return 1;
}

static struct c42_cq_head_event ack_event(
    const struct c42_test_fixture *fixture,
    uint16_t new_head)
{
    struct c42_cq_head_event event = {0};

    event.instance_nonce = fixture->config.instance_nonce;
    event.controller_epoch = fixture->config.initial_controller_epoch;
    event.ring_generation = fixture->cq_cap[0].ring_generation;
    event.queue_id = 0;
    event.new_head = new_head;
    return event;
}

static int event_log_valid(const struct c42_fake_event_log *log)
{
    uint32_t index;

    if (log == NULL || log->count == 0 || log->overflow != 0) {
        return 0;
    }
    for (index = 0; index < log->count; ++index) {
        if (log->events[index].sequence != (uint64_t)index + 1u ||
            (log->events[index].provider != C42_FAKE_EVENT_COMMAND &&
             log->events[index].provider != C42_FAKE_EVENT_MEMORY)) {
            return 0;
        }
    }
    return 1;
}

static int event_after(
    const struct c42_fake_event_log *log,
    uint32_t *cursor,
    uint8_t provider,
    uint32_t operation)
{
    uint32_t index;

    for (index = *cursor; index < log->count; ++index) {
        if (log->events[index].provider == provider &&
            log->events[index].operation == operation) {
            *cursor = index + 1u;
            return 1;
        }
    }
    return 0;
}

static void family_create(void)
{
    struct c42_test_fixture fixture;
    struct c42_observer_v2 observer;

    verify(c42_test_fixture_init(&fixture, 4, 1), "F01 fixture");
    verify(observe(&fixture, &observer), "F01 observe");
    verify(observer.phase == C42_CONTROLLER_READY &&
           observer.sq[0].life == C42_QUEUE_LIVE &&
           observer.cq[0].life == C42_QUEUE_LIVE &&
           observer.sq[1].life == C42_QUEUE_LIVE &&
           observer.cq[1].life == C42_QUEUE_LIVE &&
           observer.cq[0].create_scrub_retired == 1 &&
           observer.cq[1].create_scrub_retired == 1,
           "F01 scrub-retired queue creation");

    {
        struct c42_queue_memory_cap cap = {0};
        struct c42_queue_descriptor descriptor = {0};
        struct c42_operation_token candidate = {0};
        struct c42_candidate_status status = {0};

        verify(c42_test_fixture_init_with_nonce(
                   &fixture, 4, 0, UINT64_C(0xa201000000000001)),
               "F01 private CQ fixture");
        cap.instance_nonce = fixture.config.instance_nonce;
        cap.owner_epoch = fixture.config.owner_epoch;
        cap.memory_uid = UINT64_C(0xa2011001);
        cap.controller_epoch = fixture.config.initial_controller_epoch;
        cap.ring_generation = 1;
        cap.mapping_generation = 1;
        cap.exact_bytes = fixture.depth * C42_CQE_BYTES;
        cap.queue_id = 1;
        cap.role = C42_MEMORY_CQ_PUBLISH;
        descriptor.version = C42_COMPONENT_VERSION;
        descriptor.size = sizeof(descriptor);
        descriptor.queue_id = 1;
        descriptor.associated_cq_id = 1;
        descriptor.depth = fixture.depth;
        descriptor.kind = C42_QUEUE_CQ;
        descriptor.queue_class = FWLAB_NVME_QUEUE_IO;
        descriptor.memory = cap;
        verify(c42_fake_memory_map(
                   &fixture.memory, &cap, fixture.depth) == C42_OK &&
               c42_candidate_prepare(
                   fixture.controller, &descriptor, &candidate) == C42_OK &&
               c42_candidate_query(
                   fixture.controller, &candidate, &status) == C42_OK &&
               status.state == C42_CANDIDATE_PREPARED,
               "F01 CQ remains private before scrub proof");
    }
}

static void family_batch(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_observer_v2 observer;

    verify(c42_test_fixture_init(&fixture, 3, 0), "F02 fixture");
    script.poll_delay = 100;
    c42_fake_command_set_script(&fixture.command, &script);
    verify(c42_test_submit(&fixture, 0, 0, 1, 301) &&
           c42_test_submit(&fixture, 0, 1, 2, 302), "F02 batch tail");
    verify(run_to_active(&fixture, 2, 2), "F02 both admitted");
    script.poll_delay = 0;
    c42_fake_command_set_script(&fixture.command, &script);
    verify(c42_test_run(&fixture, 128, 4), "F02 publish");
    verify(observe(&fixture, &observer) && observer.cq[0].unacked_count == 2,
           "F02 observer counts");
    verify(exact_cqe(&fixture, 0, 0, 2, 301, 1) &&
           exact_cqe(&fixture, 1, 0, 2, 302, 1),
           "F02 independent CQE bytes");
    {
        uint32_t cursor = 0;

        verify(event_log_valid(&fixture.event_log) &&
               event_after(&fixture.event_log, &cursor,
                           C42_FAKE_EVENT_MEMORY,
                           C42_FAKE_MEMORY_CAPTURE) &&
               event_after(&fixture.event_log, &cursor,
                           C42_FAKE_EVENT_COMMAND,
                           C42_FAKE_COMMAND_PREPARE) &&
               event_after(&fixture.event_log, &cursor,
                           C42_FAKE_EVENT_COMMAND,
                           C42_FAKE_COMMAND_ADMIT) &&
               event_after(&fixture.event_log, &cursor,
                           C42_FAKE_EVENT_COMMAND,
                           C42_FAKE_COMMAND_POLL) &&
               event_after(&fixture.event_log, &cursor,
                           C42_FAKE_EVENT_COMMAND,
                           C42_FAKE_COMMAND_COMPLETION_ACQUIRE) &&
               event_after(&fixture.event_log, &cursor,
                           C42_FAKE_EVENT_COMMAND,
                           C42_FAKE_COMMAND_CONSUME_PREPARE) &&
               event_after(&fixture.event_log, &cursor,
                           C42_FAKE_EVENT_MEMORY,
                           C42_FAKE_MEMORY_BODY) &&
               event_after(&fixture.event_log, &cursor,
                           C42_FAKE_EVENT_MEMORY,
                           C42_FAKE_MEMORY_MARKER) &&
               event_after(&fixture.event_log, &cursor,
                           C42_FAKE_EVENT_COMMAND,
                           C42_FAKE_COMMAND_CONSUME_COMMIT),
               "F02 provider-owned cross-port order");
    }
}

static void family_backpressure(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    uint8_t original[C42_SQE_BYTES];
    uint8_t mutation[C42_SQE_BYTES];

    verify(c42_test_fixture_init(&fixture, 4, 0), "F03 fixture");
    script.prepare_backpressure = 2;
    script.poll_delay = 100;
    c42_fake_command_set_script(&fixture.command, &script);
    verify(c42_test_submit(&fixture, 0, 0, 1, 303), "F03 submit");
    {
        struct c42_step_result result = {0};

        verify(c42_step(fixture.controller, 1, &result) == C42_OK,
               "F03 capture once");
    }
    c42_test_sqe(original, 0x02, 303, 1, 303);
    c42_test_sqe(mutation, 0x01, 399, 2, 399);
    (void)original;
    verify(c42_fake_memory_write_sqe(
               &fixture.memory, 0, 0, mutation) == C42_OK,
           "F03 host mutates captured slot");
    verify(run_to_active(&fixture, 1, 1) &&
           fixture.memory.capture_count == 1, "F03 no reread");
    script.poll_delay = 0;
    c42_fake_command_set_script(&fixture.command, &script);
    verify(c42_test_run(&fixture, 64, 4) &&
           exact_cqe(&fixture, 0, 0, 1, 303, 1),
           "F03 captured CID CQE");
}

static void family_invalid(void)
{
    struct c42_test_fixture fixture;
    struct c42_sq_tail_event event = {0};
    struct c42_snapshot snapshot = {0};

    verify(c42_test_fixture_init(&fixture, 4, 0), "F04 fixture");
    event.instance_nonce = fixture.config.instance_nonce;
    event.controller_epoch = fixture.config.initial_controller_epoch;
    event.ring_generation = fixture.sq_cap[0].ring_generation;
    event.queue_id = 0;
    event.new_tail = 4;
    verify(c42_sq_tail_event_apply(
               fixture.controller, &event) == C42_FAULTED,
           "F04 invalid tail faults");
    verify(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
           snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED &&
           snapshot.sq[0].device_index == 0,
           "F04 no head advance");

    {
        struct c42_fake_command_injection injection = {0};

        verify(c42_test_fixture_init_with_nonce(
                   &fixture, 4, 0, UINT64_C(0xa204000000000001)),
               "F04 admission poison fixture");
        injection.operation = C42_FAKE_COMMAND_ADMIT;
        injection.result = FWLAB_HIF_PORT_COUNTER_EXHAUSTED + 1u;
        injection.omit_outputs = 1;
        verify(c42_fake_command_injection_push(
                   &fixture.command, &injection) == C42_OK &&
               c42_test_submit(&fixture, 0, 0, 1, 304) &&
               c42_test_run(&fixture, 12, 1) &&
               c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
               snapshot.sq[0].device_index == 0 &&
               fixture.command.prepare_abort_call_count == 0,
               "F04 ambiguous admission cannot advance head");
    }

    {
        struct c42_fake_command_script script = {0};

        verify(c42_test_fixture_init_with_nonce(
                   &fixture, 4, 0, UINT64_C(0xa204000000000002)),
               "F04 duplicate fixture");
        script.poll_delay = 100;
        c42_fake_command_set_script(&fixture.command, &script);
        verify(c42_test_submit(&fixture, 0, 0, 1, 304) &&
               run_to_active(&fixture, 1, 1) &&
               c42_test_submit(&fixture, 0, 1, 2, 304) &&
               c42_test_run(&fixture, 16, 1) &&
               c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
               snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED,
               "F04 duplicate active CID faults");
    }
}

static void family_out_of_order(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};

    verify(c42_test_fixture_init(&fixture, 4, 0), "F05 fixture");
    script.poll_delay = 100;
    c42_fake_command_set_script(&fixture.command, &script);
    verify(c42_test_submit(&fixture, 0, 0, 1, 305) &&
           c42_test_submit(&fixture, 0, 1, 2, 306) &&
           run_to_active(&fixture, 2, 2), "F05 delay both");
    script.poll_delay = 0;
    script.reverse_ready = 1;
    c42_fake_command_set_script(&fixture.command, &script);
    verify(c42_test_run(&fixture, 128, 4), "F05 reversed ready");
    verify(exact_cqe(&fixture, 0, 0, 2, 306, 1) &&
           exact_cqe(&fixture, 1, 0, 2, 305, 1),
           "F05 reserve SQHD and ready order");
}

static void family_phase(void)
{
    struct c42_test_fixture fixture;
    struct c42_cq_head_event ack;

    verify(c42_test_fixture_init(&fixture, 2, 0), "F06 fixture");
    verify(c42_test_submit(&fixture, 0, 0, 1, 307) &&
           c42_test_run(&fixture, 64, 4) &&
           exact_cqe(&fixture, 0, 0, 1, 307, 1), "F06 first phase");
    ack = ack_event(&fixture, 1);
    verify(c42_cq_head_event_apply(fixture.controller, &ack) == C42_OK,
           "F06 ACK");
    verify(c42_test_submit(&fixture, 0, 1, 0, 308) &&
           c42_test_run(&fixture, 64, 4) &&
           exact_cqe(&fixture, 1, 0, 0, 308, 1), "F06 pre-wrap phase");
    ack = ack_event(&fixture, 0);
    verify(c42_cq_head_event_apply(fixture.controller, &ack) == C42_OK,
           "F06 second ACK");
    verify(c42_test_submit(&fixture, 0, 0, 1, 316) &&
           c42_test_run(&fixture, 64, 4) &&
           exact_cqe(&fixture, 0, 0, 1, 316, 0), "F06 wrapped phase");
}

static void family_full(void)
{
    struct c42_test_fixture fixture;
    struct c42_cq_head_event ack;
    struct c42_observer_v2 observer;
    uint32_t acquired;
    uint32_t step;

    verify(c42_test_fixture_init(&fixture, 2, 0), "F07 fixture");
    verify(c42_test_submit(&fixture, 0, 0, 1, 309) &&
           c42_test_run(&fixture, 64, 4), "F07 fill");
    acquired = fixture.command.acquire_count;
    verify(c42_test_submit(&fixture, 0, 1, 0, 310) &&
           c42_test_run(&fixture, 8, 1) &&
           fixture.command.acquire_count == acquired,
           "F07 no lease while full");
    ack = ack_event(&fixture, 1);
    verify(c42_cq_head_event_apply(fixture.controller, &ack) == C42_OK &&
           c42_test_run(&fixture, 64, 4) &&
           fixture.command.acquire_count == acquired + 1u,
           "F07 ACK permits lease");

    verify(c42_test_fixture_init_with_nonce(
               &fixture, 4, 0, UINT64_C(0xa207000000000001)) &&
           c42_test_submit(&fixture, 0, 0, 1, 317),
           "F07 noncommitted ACK fixture");
    for (step = 0; step < 64; ++step) {
        struct c42_step_result result = {0};

        (void)c42_step(fixture.controller, 1, &result);
        (void)c42_observer_read_v2(fixture.controller, &observer);
        if (observer.cq[0].slots[0].state == C42_OBSERVER_SLOT_RESERVED ||
            observer.cq[0].slots[0].state ==
                C42_OBSERVER_SLOT_BODY_STAGED) {
            break;
        }
    }
    ack = ack_event(&fixture, 1);
    verify(step < 64 &&
           c42_cq_head_event_apply(fixture.controller, &ack) == C42_INVALID,
           "F07 ACK rejects noncommitted slot");
}

static void family_target(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_target_ref target = {0};

    verify(c42_test_fixture_init(&fixture, 4, 0), "F08 fixture");
    script.poll_delay = 100;
    c42_fake_command_set_script(&fixture.command, &script);
    verify(c42_test_submit(&fixture, 0, 0, 1, 311) &&
           run_to_active(&fixture, 1, 1), "F08 active command");
    verify(c42_target_prepare(
               fixture.controller, 0, 2, 311, &target) == C42_NOT_FOUND,
           "F08 generation mismatch");
    verify(c42_target_prepare(
               fixture.controller, 0, 1, 311, &target) == C42_OK,
           "F08 exact target");
}

static void family_publication(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_memory_outcome body = {0};
    struct c42_fake_memory_outcome marker = {0};
    struct c42_snapshot snapshot = {0};
    uint32_t step;

    verify(c42_test_fixture_init(&fixture, 4, 0), "F09 fixture");
    body.operation = C42_FAKE_MEMORY_BODY;
    body.effect = C42_MEMORY_FULL;
    body.prefix = 15;
    body.committed = 1;
    marker.operation = C42_FAKE_MEMORY_MARKER;
    marker.effect = C42_MEMORY_UNKNOWN;
    marker.committed = 1;
    verify(c42_fake_memory_script_push(&fixture.memory, &body) == C42_OK &&
           c42_fake_memory_script_push(&fixture.memory, &marker) == C42_OK &&
           c42_test_submit(&fixture, 0, 0, 1, 312), "F09 script");
    for (step = 0; step < 64 && fixture.memory.marker_call_count == 0; ++step) {
        struct c42_step_result result = {0};

        (void)c42_step(fixture.controller, 1, &result);
    }
    verify(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
           snapshot.cq[0].pending_or_unacked == 0 &&
           snapshot.pending_notifications == 0,
           "F09 marker unknown remains private");
    {
        uint32_t marker_calls = fixture.memory.marker_call_count;

        for (step = 0; step < 16; ++step) {
            struct c42_step_result result = {0};

            (void)c42_step(fixture.controller, 1, &result);
            (void)c42_snapshot_read(fixture.controller, &snapshot);
            if (fixture.memory.marker_call_count > marker_calls ||
                snapshot.cq[0].pending_or_unacked != 0) {
                break;
            }
        }
        verify(fixture.memory.marker_call_count > marker_calls &&
               snapshot.cq[0].pending_or_unacked == 0,
               "F09 marker query precedes consume commit");
    }
}

static void family_delete(void)
{
    struct c42_test_fixture fixture;
    struct c42_operation_token deletion = {0};
    struct c42_control_status status = {0};
    struct c42_snapshot snapshot = {0};

    verify(c42_test_fixture_init(&fixture, 4, 0), "F10 fixture");
    verify(c42_test_submit(&fixture, 0, 0, 1, 313) &&
           c42_test_run(&fixture, 64, 4), "F10 unacked command");
    verify(c42_delete_start(
               fixture.controller, C42_QUEUE_SQ, 0, &deletion) == C42_OK &&
           c42_control_progress(
               fixture.controller, &deletion, 8) == C42_OK &&
           c42_control_query(
               fixture.controller, &deletion, &status) == C42_OK &&
           status.state == C42_CONTROL_COMMITTED,
           "F10 delete SQ");
    verify(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
           snapshot.sq[0].life == C42_QUEUE_TOMBSTONED,
           "F10 tombstone behind CQE");
    {
        struct c42_queue_memory_cap replacement = fixture.sq_cap[0];
        struct c42_queue_descriptor descriptor = {0};
        struct c42_operation_token candidate = {0};

        replacement.memory_uid++;
        replacement.ring_generation = 2;
        replacement.mapping_generation = 2;
        descriptor.version = C42_COMPONENT_VERSION;
        descriptor.size = sizeof(descriptor);
        descriptor.queue_id = 0;
        descriptor.associated_cq_id = 0;
        descriptor.depth = fixture.depth;
        descriptor.kind = C42_QUEUE_SQ;
        descriptor.queue_class = FWLAB_NVME_QUEUE_ADMIN;
        descriptor.memory = replacement;
        verify(c42_fake_memory_map(
                   &fixture.memory, &replacement, fixture.depth) == C42_OK &&
               c42_candidate_prepare(
                   fixture.controller, &descriptor,
                   &candidate) == C42_WRONG_STATE,
               "F10 tombstone blocks recreate before ACK");
    }

    {
        struct c42_queue_memory_cap cap;
        struct c42_queue_descriptor descriptor = {0};
        struct c42_operation_token candidate = {0};
        struct c42_operation_token cq_delete = {0};

        verify(c42_test_fixture_init_with_nonce(
                   &fixture, 4, 1, UINT64_C(0xa210000000000001)),
               "F10 prepared SQ fixture");
        verify(c42_delete_start(
                   fixture.controller, C42_QUEUE_SQ, 1, &deletion) == C42_OK &&
               c42_control_progress(
                   fixture.controller, &deletion, 8) == C42_OK &&
               c42_control_query(
                   fixture.controller, &deletion, &status) == C42_OK &&
               status.state == C42_CONTROL_COMMITTED &&
               c42_control_retire(
                   fixture.controller, &deletion) == C42_OK,
               "F10 remove IO SQ");
        cap = fixture.sq_cap[1];
        cap.memory_uid++;
        cap.ring_generation = 2;
        cap.mapping_generation = 2;
        descriptor.version = C42_COMPONENT_VERSION;
        descriptor.size = sizeof(descriptor);
        descriptor.queue_id = 1;
        descriptor.associated_cq_id = 1;
        descriptor.depth = fixture.depth;
        descriptor.kind = C42_QUEUE_SQ;
        descriptor.queue_class = FWLAB_NVME_QUEUE_IO;
        descriptor.memory = cap;
        verify(c42_fake_memory_map(
                   &fixture.memory, &cap, fixture.depth) == C42_OK &&
               c42_candidate_prepare(
                   fixture.controller, &descriptor, &candidate) == C42_OK &&
               c42_delete_start(
                   fixture.controller, C42_QUEUE_CQ, 1,
                   &cq_delete) == C42_WRONG_STATE,
               "F10 prepared SQ pins CQ delete");
    }

    {
        uint8_t sqe0[C42_SQE_BYTES];
        uint8_t sqe1[C42_SQE_BYTES];
        struct c42_sq_tail_event tail = {0};

        verify(c42_test_fixture_init_with_nonce(
                   &fixture, 4, 0, UINT64_C(0xa210000000000002)),
               "F10 doorbelled delete fixture");
        c42_test_sqe(sqe0, 0x02, 318, 1, 318);
        c42_test_sqe(sqe1, 0x02, 319, 1, 319);
        tail.instance_nonce = fixture.config.instance_nonce;
        tail.controller_epoch = fixture.config.initial_controller_epoch;
        tail.ring_generation = fixture.sq_cap[0].ring_generation;
        tail.queue_id = 0;
        tail.new_tail = 2;
        verify(c42_fake_memory_write_sqe(
                   &fixture.memory, 0, 0, sqe0) == C42_OK &&
               c42_fake_memory_write_sqe(
                   &fixture.memory, 0, 1, sqe1) == C42_OK &&
               c42_sq_tail_event_apply(
                   fixture.controller, &tail) == C42_OK &&
               c42_delete_start(
                   fixture.controller, C42_QUEUE_SQ, 0,
                   &deletion) == C42_OK &&
               c42_control_progress(
                   fixture.controller, &deletion, 2) == C42_OK &&
               c42_control_query(
                   fixture.controller, &deletion, &status) == C42_OK &&
               status.state != C42_CONTROL_COMMITTED,
               "F10 delete cannot drop doorbelled SQEs");
    }
}

static void family_reset(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_operation_token reset = {0};
    struct c42_control_status status = {0};
    struct c42_snapshot snapshot = {0};

    verify(c42_test_fixture_init(&fixture, 4, 0), "F11 fixture");
    script.prepare_delay = 8;
    c42_fake_command_set_script(&fixture.command, &script);
    verify(c42_test_submit(&fixture, 0, 0, 1, 314) &&
           c42_test_run(&fixture, 1, 2), "F11 provider ownership");
    verify(c42_reset_start(fixture.controller, &reset) == C42_OK &&
           c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
           snapshot.phase == C42_CONTROLLER_RESETTING &&
           snapshot.controller_epoch == 12, "F11 reset LP");
    verify(c42_control_progress(fixture.controller, &reset, 16) == C42_OK &&
           c42_control_query(fixture.controller, &reset, &status) == C42_OK &&
           status.state == C42_CONTROL_COMMITTED &&
           c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
           snapshot.phase == C42_CONTROLLER_COLD_NO_QUEUES,
           "F11 quiescent cold");
}

static void family_isolation(void)
{
    struct c42_test_fixture left;
    struct c42_test_fixture right;
    struct c42_fake_command_script script = {0};
    struct c42_target_ref target = {0};

    verify(c42_test_fixture_init_with_nonce(
               &left, 4, 0, UINT64_C(0xa200000000000001)) &&
           c42_test_fixture_init_with_nonce(
               &right, 4, 0, UINT64_C(0xa200000000000002)),
           "F12 fixtures");
    script.poll_delay = 100;
    c42_fake_command_set_script(&left.command, &script);
    verify(c42_test_submit(&left, 0, 0, 1, 315) &&
           run_to_active(&left, 1, 1) &&
           c42_target_prepare(left.controller, 0, 1, 315, &target) == C42_OK,
           "F12 left target");
    verify(c42_target_release(right.controller, &target.token) == C42_INVALID,
           "F12 cross-instance token rejected");
}

typedef void (*family_fn)(void);

static void replay_family(family_fn family)
{
    uint32_t steps;
    uint32_t cut;

    path_step = 0;
    cut_limit = UINT32_MAX;
    family();
    steps = path_step;
    if (steps == 0) {
        failures++;
        return;
    }
    families++;
    for (cut = 1; cut <= steps; ++cut) {
        path_step = 0;
        cut_limit = cut;
        if (setjmp(cut_jump) == 0) {
            family();
            failures++;
            fprintf(stderr,
                    "C4.2 DUT replay FAIL: prefix cut %u was not reached\n",
                    cut);
        } else if (path_step != cut) {
            failures++;
            fprintf(stderr,
                    "C4.2 DUT replay FAIL: prefix cut mismatch %u/%u\n",
                    path_step, cut);
        }
        replay_paths++;
    }
    cut_limit = UINT32_MAX;
}

int main(int argc, char **argv)
{
    struct c42_dut_bfs_summary summary = {0};
    const char *only_family = NULL;

    if (argc == 3 && strcmp(argv[1], "--family") == 0) {
        only_family = argv[2];
    } else if (argc != 1) {
        fprintf(stderr, "C4.2 DUT reference FAIL: invalid arguments\n");
        return 1;
    }
    if (!c42_dut_bfs_run(only_family, &summary)) {
        return 1;
    }
    printf("C4.2 DUT reference BFS: PASS families=%u states=%u "
           "transitions=%u comparisons=%u depth=%u successors=%u "
           "caps=32768/262144/20/8 fresh-action-replay=yes\n",
           summary.families, summary.states, summary.transitions,
           summary.comparisons, summary.maximum_depth,
           summary.maximum_successors);
    if (only_family != NULL) {
        return 0;
    }
    replay_family(family_create);
    replay_family(family_batch);
    replay_family(family_backpressure);
    replay_family(family_invalid);
    replay_family(family_out_of_order);
    replay_family(family_phase);
    replay_family(family_full);
    replay_family(family_target);
    replay_family(family_publication);
    replay_family(family_delete);
    replay_family(family_reset);
    replay_family(family_isolation);
    if (failures != 0 || families != 12) {
        return 1;
    }
    printf("C4.2 fixed-scenario regressions: PASS families=%u "
           "assertion-prefixes=%u assertion-invocations=%u checks=%u "
           "fresh-fixture=yes literal-le-cqe=yes\n",
           families, replay_paths, nodes, comparisons);
    return 0;
}
