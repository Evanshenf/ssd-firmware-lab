/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fakes/c42_command.h"
#include "fakes/c42_memory.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int failures;
static uint32_t executions;

static void expect_event(
    const struct c42_fake_event_log *log,
    const struct c42_fake_event *expected,
    const char *label
);

static void check(int condition, const char *label)
{
    if (!condition) {
        fprintf(stderr, "provider matrix FAIL: %s\n", label);
        failures++;
    }
}

static struct c42_queue_memory_cap memory_cap(
    uint64_t instance_nonce,
    uint64_t owner_epoch,
    uint32_t controller_epoch,
    uint16_t queue_id,
    uint8_t role,
    uint16_t depth)
{
    struct c42_queue_memory_cap cap = {0};

    cap.instance_nonce = instance_nonce;
    cap.owner_epoch = owner_epoch;
    cap.memory_uid = UINT64_C(0xc420000000000000) + queue_id + role;
    cap.controller_epoch = controller_epoch;
    cap.ring_generation = 1;
    cap.mapping_generation = 1;
    cap.exact_bytes = (uint32_t)depth *
        (role == C42_MEMORY_SQ_READ ? C42_SQE_BYTES : C42_CQE_BYTES);
    cap.queue_id = queue_id;
    cap.role = role;
    return cap;
}

static struct c42_memory_token memory_token(
    uint64_t instance_nonce,
    uint64_t uid,
    uint16_t kind)
{
    struct c42_memory_token token = {0};

    token.instance_nonce = instance_nonce;
    token.uid = uid;
    token.generation = 1;
    token.kind = kind;
    return token;
}

static void push_memory_direct(
    struct c42_fake_memory *memory,
    uint8_t operation,
    uint8_t result,
    uint8_t write_status,
    uint8_t apply_effect,
    uint8_t logical_effect,
    uint8_t applied_effect,
    uint8_t prefix,
    uint8_t committed,
    uint8_t quiescent)
{
    struct c42_fake_memory_direct_injection direct = {0};

    direct.operation = operation;
    direct.result = result;
    direct.omit_status = (uint8_t)(write_status == 0);
    direct.write_status = write_status;
    direct.apply_effect = apply_effect;
    direct.logical_effect = logical_effect;
    direct.applied_effect = applied_effect;
    direct.prefix = prefix;
    direct.committed = committed;
    direct.quiescent = quiescent;
    check(c42_fake_memory_direct_push(memory, &direct) == C42_OK,
          "memory direct FIFO push");
}

static void test_memory_direct_fifo_and_events(void)
{
    static const uint8_t expected_operation[] = {
        C42_FAKE_MEMORY_VALIDATE, C42_FAKE_MEMORY_CAPTURE,
        C42_FAKE_MEMORY_SCRUB, C42_FAKE_MEMORY_SCRUB,
        C42_FAKE_MEMORY_SCRUB_RETIRE, C42_FAKE_MEMORY_SCRUB_RETIRE,
        C42_FAKE_MEMORY_BODY, C42_FAKE_MEMORY_BODY,
        C42_FAKE_MEMORY_MARKER, C42_FAKE_MEMORY_MARKER,
        C42_FAKE_MEMORY_RESET_BEGIN, C42_FAKE_MEMORY_RESET_QUIESCENT,
        C42_FAKE_MEMORY_TEARDOWN_BEGIN,
        C42_FAKE_MEMORY_TEARDOWN_QUIESCENT,
    };
    static const uint8_t expected_call[] = {
        C42_FAKE_CALL_ACTION, C42_FAKE_CALL_ACTION,
        C42_FAKE_CALL_START, C42_FAKE_CALL_QUERY,
        C42_FAKE_CALL_START, C42_FAKE_CALL_QUERY,
        C42_FAKE_CALL_START, C42_FAKE_CALL_QUERY,
        C42_FAKE_CALL_START, C42_FAKE_CALL_QUERY,
        C42_FAKE_CALL_START, C42_FAKE_CALL_QUERY,
        C42_FAKE_CALL_START, C42_FAKE_CALL_QUERY,
    };
    static const uint8_t expected_write[] = {
        0, C42_FAKE_EVENT_WRITE_OBJECT,
        0, C42_FAKE_EVENT_WRITE_OBJECT,
        C42_FAKE_EVENT_WRITE_OBJECT, C42_FAKE_EVENT_WRITE_OBJECT,
        0, C42_FAKE_EVENT_WRITE_OBJECT,
        0, C42_FAKE_EVENT_WRITE_OBJECT,
        0, C42_FAKE_EVENT_WRITE_VALUE,
        0, C42_FAKE_EVENT_WRITE_VALUE,
    };
    struct c42_fake_event_log log;
    struct c42_fake_memory memory;
    struct c42_memory_port port;
    struct c42_queue_memory_cap sq;
    struct c42_queue_memory_cap cq;
    struct c42_memory_token scrub;
    struct c42_memory_token body;
    struct c42_memory_token marker;
    struct c42_memory_status status;
    uint8_t sqe[C42_SQE_BYTES];
    uint8_t cqe[C42_CQE_BYTES];
    bool quiescent;
    uint32_t index;
    struct c42_fake_event expected[14] = {{0}};

    executions++;
    c42_fake_event_log_init(&log);
    c42_fake_memory_init(
        &memory, UINT64_C(0x4200000000000001),
        UINT64_C(0x4200000000000002), 11
    );
    c42_fake_memory_bind_event_log(&memory, &log);
    port = c42_fake_memory_port(&memory);
    sq = memory_cap(
        memory.instance_nonce, memory.owner_epoch, 11, 0,
        C42_MEMORY_SQ_READ, 4
    );
    cq = memory_cap(
        memory.instance_nonce, memory.owner_epoch, 11, 0,
        C42_MEMORY_CQ_PUBLISH, 4
    );
    memset(sqe, 0x3c, sizeof(sqe));
    memset(cqe, 0, sizeof(cqe));
    cqe[14] = 1;
    check(c42_fake_memory_map(&memory, &sq, 4) == C42_OK &&
          c42_fake_memory_map(&memory, &cq, 4) == C42_OK &&
          c42_fake_memory_write_sqe(&memory, 0, 0, sqe) == C42_OK,
          "memory matrix mappings");
    scrub = memory_token(memory.instance_nonce, 7001, 1);
    body = memory_token(memory.instance_nonce, 7002, 2);
    marker = memory_token(memory.instance_nonce, 7003, 3);

    push_memory_direct(&memory, C42_FAKE_MEMORY_VALIDATE,
        C42_MEMORY_STALE, 0, 0, C42_MEMORY_NO_EFFECT,
        C42_MEMORY_NO_EFFECT, 0, 0, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_CAPTURE,
        C42_MEMORY_OK, 1, 0, C42_MEMORY_FULL,
        C42_MEMORY_NO_EFFECT, 0, 0, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_SCRUB,
        C42_MEMORY_IN_PROGRESS, 0, 0, C42_MEMORY_IN_PROGRESS,
        C42_MEMORY_NO_EFFECT, 0, 0, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_SCRUB,
        C42_MEMORY_OK, 1, 1, C42_MEMORY_FULL,
        C42_MEMORY_FULL, 4, 1, 1);
    push_memory_direct(&memory, C42_FAKE_MEMORY_SCRUB_RETIRE,
        C42_MEMORY_OK, 1, 1, C42_MEMORY_UNKNOWN,
        C42_MEMORY_RETIRED, 4, 1, 1);
    push_memory_direct(&memory, C42_FAKE_MEMORY_SCRUB_RETIRE,
        C42_MEMORY_OK, 1, 0, C42_MEMORY_RETIRED,
        C42_MEMORY_RETIRED, 4, 1, 1);
    push_memory_direct(&memory, C42_FAKE_MEMORY_BODY,
        C42_MEMORY_IN_PROGRESS, 0, 0, C42_MEMORY_IN_PROGRESS,
        C42_MEMORY_NO_EFFECT, 0, 0, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_BODY,
        C42_MEMORY_OK, 1, 1, C42_MEMORY_FULL,
        C42_MEMORY_FULL, 15, 1, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_MARKER,
        C42_MEMORY_IN_PROGRESS, 0, 0, C42_MEMORY_IN_PROGRESS,
        C42_MEMORY_NO_EFFECT, 0, 0, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_MARKER,
        C42_MEMORY_OK, 1, 1, C42_MEMORY_FULL,
        C42_MEMORY_FULL, 0, 1, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_RESET_BEGIN,
        C42_MEMORY_OK, 0, 1, C42_MEMORY_NO_EFFECT,
        C42_MEMORY_OK, 0, 0, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_RESET_QUIESCENT,
        C42_MEMORY_OK, 1, 0, C42_MEMORY_OK,
        C42_MEMORY_OK, 0, 0, 1);
    push_memory_direct(&memory, C42_FAKE_MEMORY_TEARDOWN_BEGIN,
        C42_MEMORY_OK, 0, 1, C42_MEMORY_NO_EFFECT,
        C42_MEMORY_OK, 0, 0, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_TEARDOWN_QUIESCENT,
        C42_MEMORY_OK, 1, 0, C42_MEMORY_OK,
        C42_MEMORY_OK, 0, 0, 1);

    check(port.ops->validate(
              port.context, &sq, C42_MEMORY_SQ_READ,
              sq.exact_bytes) == C42_MEMORY_STALE,
          "memory validate direct result");
    memset(sqe, 0xa5, sizeof(sqe));
    check(port.ops->capture(
              port.context, &sq, 0, sqe, sizeof(sqe)) == C42_MEMORY_OK &&
          sqe[0] == 0x3c,
          "memory capture explicit output mask");
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->scrub_start(
              port.context, &cq, 4, 0, &scrub,
              &status) == C42_MEMORY_IN_PROGRESS,
          "memory scrub start direct");
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->scrub_query(
              port.context, &cq, 4, 0, &scrub, &status) == C42_MEMORY_OK &&
          status.result == C42_MEMORY_FULL && status.committed == 1 &&
          status.quiescent == 1,
          "memory scrub query direct status");
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->scrub_retire_start(
              port.context, &cq, 4, 0, &scrub,
              &status) == C42_MEMORY_OK &&
          status.result == C42_MEMORY_UNKNOWN,
          "memory retire reported unknown after actual retire");
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->scrub_retire_query(
              port.context, &cq, 4, 0, &scrub, &status) == C42_MEMORY_OK &&
          status.result == C42_MEMORY_RETIRED,
          "memory retire query status");
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->body_start(
              port.context, &cq, 0, cqe, &body,
              &status) == C42_MEMORY_IN_PROGRESS,
          "memory body direct in-progress has no effect");
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->body_query(
              port.context, &cq, 0, cqe, &body, &status) == C42_MEMORY_OK &&
          status.result == C42_MEMORY_FULL && status.prefix == 15,
          "memory body query status");
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->marker_start(
              port.context, &cq, 0, 1, &marker,
              &status) == C42_MEMORY_IN_PROGRESS,
          "memory marker direct in-progress has no effect");
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->marker_query(
              port.context, &cq, 0, 1, &marker, &status) == C42_MEMORY_OK &&
          status.result == C42_MEMORY_FULL && status.committed == 1,
          "memory marker query status");
    check(port.ops->reset_begin(
              port.context, memory.instance_nonce, 11) ==
              C42_MEMORY_OK,
          "memory reset begin applied result");
    quiescent = false;
    check(port.ops->reset_quiescent(
              port.context, memory.instance_nonce, 11,
              &quiescent) == C42_MEMORY_OK && quiescent,
          "memory reset quiescent output");
    check(port.ops->teardown_begin(
              port.context, memory.instance_nonce, 12) ==
              C42_MEMORY_OK,
          "memory teardown begin applied result");
    quiescent = false;
    check(port.ops->teardown_quiescent(
              port.context, memory.instance_nonce, 12,
              &quiescent) == C42_MEMORY_OK && quiescent,
          "memory teardown quiescent output");

    check(memory.direct_index == memory.direct_count &&
          log.count == sizeof(expected_operation) /
                       sizeof(expected_operation[0]) &&
          log.overflow == 0,
          "memory FIFO fully consumed without overflow");
    for (index = 0; index < log.count; ++index) {
        const struct c42_fake_event *event = &log.events[index];
        int exact = event->sequence == index + 1u &&
            event->provider == C42_FAKE_EVENT_MEMORY &&
            event->operation == expected_operation[index] &&
            event->call_kind == expected_call[index] &&
            event->output_write_mask == expected_write[index] &&
            event->input_structural_valid != 0;

        if (!exact ||
            (event->flags & C42_FAKE_EVENT_RESPONSE_LOST) != 0) {
            fprintf(stderr,
                "memory event[%u]: op=%u/%u call=%u/%u mask=%u/%u "
                "in=%u/%u out=%u/%u req/report/apply=%u/%u/%u flags=%u\n",
                index, event->operation, expected_operation[index],
                event->call_kind, expected_call[index],
                event->output_write_mask, expected_write[index],
                event->input_structural_valid, event->input_record_match,
                event->output_structural_valid, event->output_record_match,
                event->requested_effect, event->reported_effect,
                event->applied_effect, event->flags);
        }
        check(exact,
              "memory event exact order/call/write-mask");
        check((event->flags & C42_FAKE_EVENT_RESPONSE_LOST) == 0,
              "memory direct in-progress never hides an effect");
    }
    for (index = 0; index < 14; ++index) {
        expected[index].sequence = index + 1u;
        expected[index].operation = expected_operation[index];
        expected[index].provider = C42_FAKE_EVENT_MEMORY;
        expected[index].call_kind = expected_call[index];
        expected[index].output_write_mask = expected_write[index];
        expected[index].input_structural_valid = 1;
        expected[index].input_record_match = 1;
        expected[index].output_structural_valid =
            (uint8_t)(expected_write[index] != 0);
        expected[index].output_record_match =
            (uint8_t)(expected_write[index] != 0);
        expected[index].requested_effect = C42_MEMORY_NO_EFFECT;
        expected[index].applied_effect = C42_MEMORY_NO_EFFECT;
    }
    expected[0].token_uid = sq.memory_uid;
    expected[0].direct_result = C42_MEMORY_STALE;
    expected[0].reported_effect = C42_MEMORY_NO_EFFECT;
    expected[0].parameter0 = C42_MEMORY_SQ_READ;
    expected[0].parameter1 = sq.exact_bytes;
    expected[1].token_uid = sq.memory_uid;
    expected[1].object_uid = sq.memory_uid;
    expected[1].direct_result = C42_MEMORY_OK;
    expected[1].reported_effect = C42_MEMORY_FULL;
    expected[1].parameter1 = C42_SQE_BYTES;
    expected[2].token_uid = scrub.uid;
    expected[2].direct_result = C42_MEMORY_IN_PROGRESS;
    expected[2].reported_effect = C42_MEMORY_IN_PROGRESS;
    expected[2].parameter0 = 4;
    expected[2].input_record_match = 0;
    expected[3].token_uid = scrub.uid;
    expected[3].object_uid = scrub.uid;
    expected[3].direct_result = C42_MEMORY_OK;
    expected[3].output_value = C42_MEMORY_FULL;
    expected[3].requested_effect = C42_MEMORY_FULL;
    expected[3].reported_effect = C42_MEMORY_FULL;
    expected[3].applied_effect = C42_MEMORY_FULL;
    expected[3].parameter0 = 4;
    expected[3].committed = 1;
    expected[3].quiescent = 1;
    expected[3].flags = C42_FAKE_EVENT_EFFECT_APPLIED;
    expected[4].token_uid = scrub.uid;
    expected[4].object_uid = scrub.uid;
    expected[4].direct_result = C42_MEMORY_OK;
    expected[4].output_value = C42_MEMORY_UNKNOWN;
    expected[4].requested_effect = C42_MEMORY_RETIRED;
    expected[4].reported_effect = C42_MEMORY_UNKNOWN;
    expected[4].applied_effect = C42_MEMORY_RETIRED;
    expected[4].parameter0 = 4;
    expected[4].committed = 1;
    expected[4].quiescent = 1;
    expected[4].flags = C42_FAKE_EVENT_EFFECT_APPLIED;
    expected[5].token_uid = scrub.uid;
    expected[5].object_uid = scrub.uid;
    expected[5].direct_result = C42_MEMORY_OK;
    expected[5].output_value = C42_MEMORY_RETIRED;
    expected[5].reported_effect = C42_MEMORY_RETIRED;
    expected[5].parameter0 = 4;
    expected[5].committed = 1;
    expected[5].quiescent = 1;
    expected[6].token_uid = body.uid;
    expected[6].direct_result = C42_MEMORY_IN_PROGRESS;
    expected[6].reported_effect = C42_MEMORY_IN_PROGRESS;
    expected[6].parameter1 = C42_CQE_BYTES;
    expected[6].input_record_match = 0;
    expected[7].token_uid = body.uid;
    expected[7].object_uid = body.uid;
    expected[7].direct_result = C42_MEMORY_OK;
    expected[7].output_value = C42_MEMORY_FULL;
    expected[7].requested_effect = C42_MEMORY_FULL;
    expected[7].reported_effect = C42_MEMORY_FULL;
    expected[7].applied_effect = C42_MEMORY_FULL;
    expected[7].parameter1 = C42_CQE_BYTES;
    expected[7].committed = 1;
    expected[7].flags = C42_FAKE_EVENT_EFFECT_APPLIED;
    expected[8].token_uid = marker.uid;
    expected[8].direct_result = C42_MEMORY_IN_PROGRESS;
    expected[8].reported_effect = C42_MEMORY_IN_PROGRESS;
    expected[8].parameter1 = 1;
    expected[9].token_uid = marker.uid;
    expected[9].object_uid = marker.uid;
    expected[9].direct_result = C42_MEMORY_OK;
    expected[9].output_value = C42_MEMORY_FULL;
    expected[9].requested_effect = C42_MEMORY_FULL;
    expected[9].reported_effect = C42_MEMORY_FULL;
    expected[9].applied_effect = C42_MEMORY_FULL;
    expected[9].parameter1 = 1;
    expected[9].committed = 1;
    expected[9].flags = C42_FAKE_EVENT_EFFECT_APPLIED;
    expected[10].token_uid = 11;
    expected[10].direct_result = C42_MEMORY_OK;
    expected[10].requested_effect = C42_MEMORY_OK;
    expected[10].reported_effect = C42_MEMORY_NO_EFFECT;
    expected[10].applied_effect = C42_MEMORY_OK;
    expected[10].parameter0 = 11;
    expected[10].flags = C42_FAKE_EVENT_EFFECT_APPLIED;
    expected[11].token_uid = 11;
    expected[11].direct_result = C42_MEMORY_OK;
    expected[11].output_value = 1;
    expected[11].reported_effect = C42_MEMORY_OK;
    expected[11].parameter0 = 11;
    expected[11].quiescent = 1;
    expected[12].token_uid = 12;
    expected[12].direct_result = C42_MEMORY_OK;
    expected[12].requested_effect = C42_MEMORY_OK;
    expected[12].reported_effect = C42_MEMORY_NO_EFFECT;
    expected[12].applied_effect = C42_MEMORY_OK;
    expected[12].parameter0 = 12;
    expected[12].flags = C42_FAKE_EVENT_EFFECT_APPLIED;
    expected[13].token_uid = 12;
    expected[13].direct_result = C42_MEMORY_OK;
    expected[13].output_value = 1;
    expected[13].reported_effect = C42_MEMORY_OK;
    expected[13].parameter0 = 12;
    expected[13].quiescent = 1;
    for (index = 0; index < 14; ++index) {
        expect_event(&log, &expected[index], "memory event all fields exact");
    }
}

static void test_memory_abort_event(void)
{
    struct c42_fake_event_log log;
    struct c42_fake_memory memory;
    struct c42_memory_port port;
    struct c42_queue_memory_cap cq;
    struct c42_memory_token scrub;
    struct c42_memory_token bad_token;
    struct c42_memory_status status;
    struct c42_fake_memory_direct_injection illegal = {0};

    executions++;
    c42_fake_event_log_init(&log);
    c42_fake_memory_init(
        &memory, UINT64_C(0x4200000000000011),
        UINT64_C(0x4200000000000012), 21
    );
    c42_fake_memory_bind_event_log(&memory, &log);
    port = c42_fake_memory_port(&memory);
    cq = memory_cap(
        memory.instance_nonce, memory.owner_epoch, 21, 0,
        C42_MEMORY_CQ_PUBLISH, 4
    );
    scrub = memory_token(memory.instance_nonce, 7101, 1);
    illegal.operation = C42_FAKE_MEMORY_SCRUB_ABORT;
    illegal.result = C42_MEMORY_IN_PROGRESS;
    illegal.omit_status = 1;
    illegal.apply_effect = 1;
    illegal.logical_effect = C42_MEMORY_UNKNOWN;
    illegal.applied_effect = C42_MEMORY_RETIRED;
    check(c42_fake_memory_direct_push(&memory, &illegal) == C42_INVALID &&
          memory.direct_count == 0,
          "memory rejects direct in-progress hidden effect");
    check(c42_fake_memory_map(&memory, &cq, 4) == C42_OK &&
          port.ops->scrub_start(
              port.context, &cq, 4, 0, &scrub, &status) == C42_MEMORY_OK,
          "abort matrix committed scrub setup");
    c42_fake_event_log_init(&log);
    push_memory_direct(&memory, C42_FAKE_MEMORY_SCRUB_ABORT,
        C42_MEMORY_OK, 1, 1, C42_MEMORY_UNKNOWN,
        C42_MEMORY_RETIRED, 4, 1, 1);
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->scrub_abort(
              port.context, &cq, 4, 0, &scrub,
              &status) == C42_MEMORY_OK &&
          log.count == 1 &&
          log.events[0].operation == C42_FAKE_MEMORY_SCRUB_ABORT &&
          log.events[0].call_kind == C42_FAKE_CALL_ACTION &&
          log.events[0].output_write_mask == C42_FAKE_EVENT_WRITE_OBJECT &&
          log.events[0].requested_effect == C42_MEMORY_RETIRED &&
          log.events[0].reported_effect == C42_MEMORY_UNKNOWN &&
          log.events[0].applied_effect == C42_MEMORY_RETIRED &&
          log.events[0].flags == C42_FAKE_EVENT_EFFECT_APPLIED,
              "abort event separates reported and applied effects");
    {
        struct c42_fake_memory_direct_injection direct = {0};

        direct.operation = C42_FAKE_MEMORY_SCRUB;
        direct.result = C42_MEMORY_OK;
        direct.write_status = 1;
        direct.logical_effect = C42_MEMORY_FULL;
        direct.prefix = 4;
        direct.committed = 1;
        direct.quiescent = 1;
        direct.token_variant = C42_FAKE_MEMORY_TOKEN_ZERO;
        memset(&status, 0xa5, sizeof(status));
        check(c42_fake_memory_direct_push(
                  &memory, &direct) == C42_OK &&
              port.ops->scrub_query(
                  port.context, &cq, 4, 0, &scrub,
                  &status) == C42_MEMORY_OK &&
              status.token.uid == 0 &&
              log.count == 2 &&
              log.events[1].operation == C42_FAKE_MEMORY_SCRUB &&
              log.events[1].call_kind == C42_FAKE_CALL_QUERY &&
              log.events[1].output_write_mask ==
                  C42_FAKE_EVENT_WRITE_OBJECT &&
              log.events[1].object_uid == 0 &&
              log.events[1].input_structural_valid == 1 &&
              log.events[1].input_record_match == 1 &&
              log.events[1].output_structural_valid == 0 &&
              log.events[1].output_record_match == 0,
              "memory event exposes zero returned token");
        direct.token_variant = C42_FAKE_MEMORY_TOKEN_MISMATCH;
        memset(&status, 0xa5, sizeof(status));
        check(c42_fake_memory_direct_push(
                  &memory, &direct) == C42_OK &&
              port.ops->scrub_query(
                  port.context, &cq, 4, 0, &scrub,
                  &status) == C42_MEMORY_OK &&
              status.token.uid == scrub.uid + 1u &&
              log.count == 3 &&
              log.events[2].operation == C42_FAKE_MEMORY_SCRUB &&
              log.events[2].call_kind == C42_FAKE_CALL_QUERY &&
              log.events[2].output_write_mask ==
                  C42_FAKE_EVENT_WRITE_OBJECT &&
              log.events[2].object_uid == scrub.uid + 1u &&
              log.events[2].input_structural_valid == 1 &&
              log.events[2].input_record_match == 1 &&
              log.events[2].output_structural_valid == 1 &&
              log.events[2].output_record_match == 0,
              "memory event exposes mismatched returned token");
    }
    bad_token = scrub;
    bad_token.uid++;
    memset(&status, 0, sizeof(status));
    check(port.ops->scrub_query(
              port.context, &cq, 4, 0, &bad_token,
              &status) == C42_MEMORY_STALE && log.count == 4 &&
          log.events[3].input_structural_valid == 1 &&
          log.events[3].input_record_match == 0 &&
          log.events[3].output_write_mask == 0,
          "memory valid mismatched input token is explicit");
    memset(&bad_token, 0, sizeof(bad_token));
    memset(&status, 0, sizeof(status));
    {
        enum c42_memory_result zero_result = port.ops->scrub_query(
            port.context, &cq, 4, 0, &bad_token, &status
        );

        if (!(zero_result == C42_MEMORY_STALE && log.count == 5 &&
              log.events[4].input_structural_valid == 0 &&
              log.events[4].input_record_match == 0 &&
              log.events[4].output_write_mask == 0)) {
            fprintf(stderr,
                "zero memory input: result=%u count=%u in=%u/%u mask=%u\n",
                zero_result, log.count,
                log.count > 4 ? log.events[4].input_structural_valid : 9,
                log.count > 4 ? log.events[4].input_record_match : 9,
                log.count > 4 ? log.events[4].output_write_mask : 9);
        }
        check(zero_result == C42_MEMORY_STALE && log.count == 5 &&
          log.events[4].input_structural_valid == 0 &&
          log.events[4].input_record_match == 0 &&
          log.events[4].output_write_mask == 0,
          "memory zero input token is explicit");
    }
}

static struct fwlab_hif_prepare_key prepare_key(uint64_t instance_nonce)
{
    struct fwlab_hif_prepare_key key = {0};

    key.version = FWLAB_HIF_COMMAND_PORT_VERSION;
    key.size = sizeof(key);
    key.origin.word[0] = UINT64_C(0x1111222233334444);
    key.origin.word[1] = UINT64_C(0x5555666677778888);
    key.client_uid = 91;
    key.instance_nonce = instance_nonce;
    key.controller_epoch = 31;
    key.client_generation = 1;
    key.queue_class = FWLAB_NVME_QUEUE_IO;
    key.worst_case_actions = 1;
    return key;
}

struct command_case {
    struct c42_fake_command command;
    struct fwlab_hif_command_port port;
    struct fwlab_hif_prepare_key key;
    struct fwlab_hif_prepared_token prepared;
    struct fwlab_hif_admission_key admission;
    struct fwlab_nvme_command canonical;
    struct fwlab_hif_command_ticket ticket;
    struct fwlab_hif_completion_lease lease;
    struct fwlab_hif_consume_token consume;
};

static void command_case_init(struct command_case *test, uint64_t nonce)
{
    memset(test, 0, sizeof(*test));
    c42_fake_command_init(&test->command, nonce, 31, 8);
    test->port = c42_fake_command_port(&test->command);
    test->key = prepare_key(nonce);
}

static int command_prepare(struct command_case *test)
{
    struct fwlab_hif_prepare_result result = {0};

    if (test->port.ops->prepare_start(
            test->port.context, &test->key, &result) != FWLAB_HIF_PORT_OK ||
        result.disposition != FWLAB_HIF_PREPARE_RESERVED) {
        return 0;
    }
    test->prepared = result.prepared;
    test->admission.prepared = test->prepared;
    test->admission.client_uid = test->key.client_uid;
    test->admission.generation = 1;
    test->canonical.version = FWLAB_NVME_COMMAND_VERSION;
    test->canonical.size = sizeof(test->canonical);
    test->canonical.handle = test->prepared.handle;
    test->canonical.origin = test->prepared.origin;
    test->canonical.trace_cookie = 1;
    test->canonical.safety_generation = 1;
    test->canonical.namespace_id = 1;
    test->canonical.opcode = 2;
    test->canonical.queue_class = FWLAB_NVME_QUEUE_IO;
    test->canonical.fuse = FWLAB_NVME_FUSE_NONE;
    test->canonical.data_pointer_format = FWLAB_NVME_DATA_POINTER_PRP;
    return 1;
}

static int command_admit(struct command_case *test)
{
    enum fwlab_hif_admission_state state = FWLAB_HIF_ADMISSION_NOT_STARTED;

    return command_prepare(test) &&
        test->port.ops->admit_start(
            test->port.context, &test->admission, &test->canonical,
            &state, &test->ticket) == FWLAB_HIF_PORT_OK &&
        state == FWLAB_HIF_ADMISSION_COMMITTED;
}

static int command_ready(struct command_case *test)
{
    struct fwlab_hif_ready_event event = {0};
    uint32_t count = 0;

    return command_admit(test) &&
        test->port.ops->poll(
            test->port.context, 1, &event, 1, &count) == FWLAB_HIF_PORT_OK &&
        count == 1 && event.ticket.ticket_uid == test->ticket.ticket_uid;
}

static int command_lease(struct command_case *test)
{
    struct fwlab_nvme_completion_intent intent = {0};

    return command_ready(test) &&
        test->port.ops->completion_acquire(
            test->port.context, &test->ticket, &intent,
            &test->lease) == FWLAB_HIF_PORT_OK;
}

static int command_consume(struct command_case *test)
{
    enum fwlab_hif_consume_state state = FWLAB_HIF_CONSUME_NOT_STARTED;

    return command_lease(test) &&
        test->port.ops->consume_prepare(
            test->port.context, &test->lease, 7001,
            &test->consume, &state) == FWLAB_HIF_PORT_OK &&
        state == FWLAB_HIF_CONSUME_PREPARED;
}

static void expect_event(
    const struct c42_fake_event_log *log,
    const struct c42_fake_event *expected,
    const char *label)
{
    const struct c42_fake_event *actual;

    if (expected->sequence == 0 || expected->sequence > log->count) {
        check(0, label);
        return;
    }
    actual = &log->events[expected->sequence - 1u];
    if (!(actual->sequence == expected->sequence &&
          actual->token_uid == expected->token_uid &&
          actual->object_uid == expected->object_uid &&
          actual->operation == expected->operation &&
          actual->direct_result == expected->direct_result &&
          actual->output_value == expected->output_value &&
          actual->requested_effect == expected->requested_effect &&
          actual->reported_effect == expected->reported_effect &&
          actual->applied_effect == expected->applied_effect &&
          actual->parameter0 == expected->parameter0 &&
          actual->parameter1 == expected->parameter1 &&
          actual->provider == expected->provider &&
          actual->call_kind == expected->call_kind &&
          actual->output_write_mask == expected->output_write_mask &&
          actual->input_structural_valid ==
              expected->input_structural_valid &&
          actual->input_record_match == expected->input_record_match &&
          actual->output_structural_valid ==
              expected->output_structural_valid &&
          actual->output_record_match == expected->output_record_match &&
          actual->committed == expected->committed &&
          actual->quiescent == expected->quiescent &&
          actual->flags == expected->flags)) {
        fprintf(stderr,
            "provider event mismatch %s: seq=%llu op=%u call=%u result=%u "
            "mask=%u in=%u/%u out=%u/%u token=%llu object=%llu "
            "value=%u req/report/apply=%u/%u/%u p=%u/%u flags=%u\n",
            label, (unsigned long long)actual->sequence,
            actual->operation, actual->call_kind, actual->direct_result,
            actual->output_write_mask, actual->input_structural_valid,
            actual->input_record_match, actual->output_structural_valid,
            actual->output_record_match,
            (unsigned long long)actual->token_uid,
            (unsigned long long)actual->object_uid, actual->output_value,
            actual->requested_effect, actual->reported_effect,
            actual->applied_effect, actual->parameter0, actual->parameter1,
            actual->flags);
        check(0, label);
    }
}

static struct c42_fake_event command_expected(
    const struct c42_fake_event_log *log,
    uint32_t operation,
    uint8_t call_kind,
    uint32_t result,
    uint32_t output,
    uint8_t mask,
    uint8_t input_valid,
    uint8_t input_match,
    uint8_t output_valid,
    uint8_t output_match,
    uint64_t token_uid,
    uint64_t object_uid,
    uint32_t parameter0,
    uint32_t parameter1)
{
    struct c42_fake_event event = {0};

    event.sequence = log->count;
    event.token_uid = token_uid;
    event.object_uid = object_uid;
    event.operation = operation;
    event.direct_result = result;
    event.output_value = output;
    event.reported_effect = output;
    event.parameter0 = parameter0;
    event.parameter1 = parameter1;
    event.provider = C42_FAKE_EVENT_COMMAND;
    event.call_kind = call_kind;
    event.output_write_mask = mask;
    event.input_structural_valid = input_valid;
    event.input_record_match = input_match;
    event.output_structural_valid = output_valid;
    event.output_record_match = output_match;
    return event;
}

static void test_command_all_entrypoints(void)
{
    struct c42_fake_event_log log;
    struct command_case test;
    struct c42_fake_event expected;
    struct c42_fake_command_script script = {0};
    struct c42_fake_command_injection injection = {0};
    struct fwlab_hif_prepare_result prepare = {0};
    struct fwlab_hif_ready_event ready = {0};
    struct fwlab_nvme_completion_intent intent = {0};
    enum fwlab_hif_admission_state admission;
    enum fwlab_hif_consume_state consume;
    bool boolean = false;
    uint32_t count = 0;
    uint64_t nonce = UINT64_C(0x4300000000000100);

    executions++;
    c42_fake_event_log_init(&log);

    command_case_init(&test, ++nonce);
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->prepare_start(
              test.port.context, &test.key, &prepare) == FWLAB_HIF_PORT_OK,
          "invoke prepare_start");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_PREPARE, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_OK, FWLAB_HIF_PREPARE_RESERVED, 3, 1, 1, 1, 1,
        test.key.client_uid, prepare.prepared.reservation_uid, 31, 1
    );
    expect_event(&log, &expected, "prepare_start exact event");

    command_case_init(&test, ++nonce);
    script.prepare_delay = 2;
    c42_fake_command_set_script(&test.command, &script);
    check(test.port.ops->prepare_start(
              test.port.context, &test.key, &prepare) ==
              FWLAB_HIF_PORT_IN_PROGRESS,
          "prepare query setup");
    memset(&prepare, 0, sizeof(prepare));
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->prepare_query(
              test.port.context, &test.key, &prepare) ==
              FWLAB_HIF_PORT_IN_PROGRESS,
          "invoke prepare_query");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_PREPARE, C42_FAKE_CALL_QUERY,
        FWLAB_HIF_PORT_IN_PROGRESS, 0, 0, 1, 1, 0, 0,
        test.key.client_uid, 0, 31, 1
    );
    expect_event(&log, &expected, "prepare_query exact event");

    command_case_init(&test, ++nonce);
    check(command_prepare(&test), "prepare abort setup");
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->prepare_abort(
              test.port.context, &test.prepared, &boolean) ==
              FWLAB_HIF_PORT_OK && boolean,
          "invoke prepare_abort");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_PREPARE_ABORT, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_OK, 1, 1, 1, 1, 1, 1,
        test.prepared.reservation_uid, 0, test.prepared.generation, 0
    );
    expect_event(&log, &expected, "prepare_abort exact event");

    command_case_init(&test, ++nonce);
    check(command_prepare(&test), "prepare abort query setup");
    injection.operation = C42_FAKE_COMMAND_PREPARE_ABORT;
    injection.result = FWLAB_HIF_PORT_IN_PROGRESS;
    injection.omit_outputs = 1;
    check(c42_fake_command_injection_push(&test.command, &injection) == C42_OK &&
          test.port.ops->prepare_abort(
              test.port.context, &test.prepared, &boolean) ==
              FWLAB_HIF_PORT_IN_PROGRESS,
          "prepare abort query delayed start");
    boolean = false;
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->prepare_abort_query(
              test.port.context, &test.prepared, &boolean) ==
              FWLAB_HIF_PORT_OK && boolean,
          "invoke prepare_abort_query");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_PREPARE_ABORT, C42_FAKE_CALL_QUERY,
        FWLAB_HIF_PORT_OK, 1, 1, 1, 1, 1, 1,
        test.prepared.reservation_uid, 0, test.prepared.generation, 0
    );
    expect_event(&log, &expected, "prepare_abort_query exact event");

    command_case_init(&test, ++nonce);
    check(command_prepare(&test), "admit start setup");
    admission = FWLAB_HIF_ADMISSION_NOT_STARTED;
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->admit_start(
              test.port.context, &test.admission, &test.canonical,
              &admission, &test.ticket) == FWLAB_HIF_PORT_OK &&
          admission == FWLAB_HIF_ADMISSION_COMMITTED,
          "invoke admit_start");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_ADMIT, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_OK, FWLAB_HIF_ADMISSION_COMMITTED, 3, 1, 1, 1, 1,
        test.admission.client_uid, test.ticket.ticket_uid,
        test.admission.generation, test.canonical.handle.generation
    );
    expect_event(&log, &expected, "admit_start exact event");

    command_case_init(&test, ++nonce);
    check(command_prepare(&test), "admit query setup");
    memset(&script, 0, sizeof(script));
    script.admit_delay = 1;
    c42_fake_command_set_script(&test.command, &script);
    admission = FWLAB_HIF_ADMISSION_NOT_STARTED;
    check(test.port.ops->admit_start(
              test.port.context, &test.admission, &test.canonical,
              &admission, &test.ticket) == FWLAB_HIF_PORT_IN_PROGRESS,
          "admit query delayed start");
    memset(&test.ticket, 0, sizeof(test.ticket));
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->admit_query(
              test.port.context, &test.admission, &test.canonical,
              &admission, &test.ticket) == FWLAB_HIF_PORT_OK,
          "invoke admit_query");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_ADMIT, C42_FAKE_CALL_QUERY,
        FWLAB_HIF_PORT_OK, FWLAB_HIF_ADMISSION_COMMITTED, 3, 1, 1, 1, 1,
        test.admission.client_uid, test.ticket.ticket_uid,
        test.admission.generation, test.canonical.handle.generation
    );
    expect_event(&log, &expected, "admit_query exact event");

    command_case_init(&test, ++nonce);
    check(command_admit(&test), "poll setup");
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->poll(
              test.port.context, 1, &ready, 1, &count) == FWLAB_HIF_PORT_OK &&
          count == 1,
          "invoke poll");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_POLL, C42_FAKE_CALL_ACTION,
        FWLAB_HIF_PORT_OK, 1, 3, 1, 1, 1, 1,
        ready.ticket.ticket_uid, ready.sequence, 1, 1
    );
    expect_event(&log, &expected, "poll exact event");

    command_case_init(&test, ++nonce);
    check(command_ready(&test), "completion acquire setup");
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->completion_acquire(
              test.port.context, &test.ticket, &intent,
              &test.lease) == FWLAB_HIF_PORT_OK,
          "invoke completion_acquire");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_COMPLETION_ACQUIRE, C42_FAKE_CALL_ACTION,
        FWLAB_HIF_PORT_OK, (uint32_t)test.lease.lease_uid, 2, 1, 1, 1, 1,
        test.ticket.ticket_uid, test.lease.lease_uid,
        test.lease.generation, intent.result_dword0
    );
    expect_event(&log, &expected, "completion_acquire exact event");

    command_case_init(&test, ++nonce);
    check(command_lease(&test), "release start setup");
    boolean = false;
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->completion_release_start(
              test.port.context, &test.lease, 8001, &boolean) ==
              FWLAB_HIF_PORT_OK && boolean,
          "invoke completion_release_start");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_COMPLETION_RELEASE, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_OK, 1, 1, 1, 1, 1, 1,
        test.lease.lease_uid, 8001, test.lease.generation, 0
    );
    expect_event(&log, &expected, "completion_release_start exact event");

    command_case_init(&test, ++nonce);
    check(command_lease(&test), "release query setup");
    memset(&injection, 0, sizeof(injection));
    injection.operation = C42_FAKE_COMMAND_COMPLETION_RELEASE;
    injection.result = FWLAB_HIF_PORT_IN_PROGRESS;
    injection.omit_outputs = 1;
    check(c42_fake_command_injection_push(&test.command, &injection) == C42_OK &&
          test.port.ops->completion_release_start(
              test.port.context, &test.lease, 8002, &boolean) ==
              FWLAB_HIF_PORT_IN_PROGRESS,
          "release query delayed start");
    boolean = false;
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->completion_release_query(
              test.port.context, &test.lease, 8002, &boolean) ==
              FWLAB_HIF_PORT_OK && boolean,
          "invoke completion_release_query");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_COMPLETION_RELEASE, C42_FAKE_CALL_QUERY,
        FWLAB_HIF_PORT_OK, 1, 1, 1, 1, 1, 1,
        test.lease.lease_uid, 8002, test.lease.generation, 0
    );
    expect_event(&log, &expected, "completion_release_query exact event");

    command_case_init(&test, ++nonce);
    check(command_lease(&test), "consume prepare setup");
    consume = FWLAB_HIF_CONSUME_NOT_STARTED;
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->consume_prepare(
              test.port.context, &test.lease, 7001,
              &test.consume, &consume) == FWLAB_HIF_PORT_OK,
          "invoke consume_prepare");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_CONSUME_PREPARE, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_OK, FWLAB_HIF_CONSUME_PREPARED, 3, 1, 1, 1, 1,
        test.lease.lease_uid, test.consume.consume_uid, 7001,
        test.lease.generation
    );
    expect_event(&log, &expected, "consume_prepare exact event");

    command_case_init(&test, ++nonce);
    check(command_consume(&test), "consume abort setup");
    consume = FWLAB_HIF_CONSUME_NOT_STARTED;
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->consume_abort(
              test.port.context, &test.consume, &consume) == FWLAB_HIF_PORT_OK,
          "invoke consume_abort");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_CONSUME_ABORT, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_OK, FWLAB_HIF_CONSUME_ABORTED, 1, 1, 1, 1, 1,
        test.consume.consume_uid, 0, test.consume.generation, 0
    );
    expect_event(&log, &expected, "consume_abort exact event");

    command_case_init(&test, ++nonce);
    check(command_consume(&test), "consume abort query setup");
    memset(&injection, 0, sizeof(injection));
    injection.operation = C42_FAKE_COMMAND_CONSUME_ABORT;
    injection.result = FWLAB_HIF_PORT_IN_PROGRESS;
    injection.omit_outputs = 1;
    check(c42_fake_command_injection_push(&test.command, &injection) == C42_OK &&
          test.port.ops->consume_abort(
              test.port.context, &test.consume, &consume) ==
              FWLAB_HIF_PORT_IN_PROGRESS,
          "consume abort query delayed start");
    consume = FWLAB_HIF_CONSUME_NOT_STARTED;
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->consume_abort_query(
              test.port.context, &test.consume, &consume) == FWLAB_HIF_PORT_OK,
          "invoke consume_abort_query");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_CONSUME_ABORT, C42_FAKE_CALL_QUERY,
        FWLAB_HIF_PORT_OK, FWLAB_HIF_CONSUME_ABORTED, 1, 1, 1, 1, 1,
        test.consume.consume_uid, 0, test.consume.generation, 0
    );
    expect_event(&log, &expected, "consume_abort_query exact event");

    command_case_init(&test, ++nonce);
    check(command_consume(&test), "consume commit setup");
    consume = FWLAB_HIF_CONSUME_NOT_STARTED;
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->consume_commit(
              test.port.context, &test.consume, &consume) == FWLAB_HIF_PORT_OK,
          "invoke consume_commit");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_CONSUME_COMMIT, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_OK, FWLAB_HIF_CONSUME_COMMITTED, 1, 1, 1, 1, 1,
        test.consume.consume_uid, 0, test.consume.generation, 0
    );
    expect_event(&log, &expected, "consume_commit exact event");

    command_case_init(&test, ++nonce);
    check(command_consume(&test), "consume query setup");
    memset(&script, 0, sizeof(script));
    script.consume_commit_delay = 1;
    c42_fake_command_set_script(&test.command, &script);
    consume = FWLAB_HIF_CONSUME_NOT_STARTED;
    check(test.port.ops->consume_commit(
              test.port.context, &test.consume, &consume) ==
              FWLAB_HIF_PORT_IN_PROGRESS,
          "consume query delayed commit");
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->consume_query(
              test.port.context, &test.consume, &consume) == FWLAB_HIF_PORT_OK,
          "invoke consume_query");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_CONSUME_QUERY, C42_FAKE_CALL_QUERY,
        FWLAB_HIF_PORT_OK, FWLAB_HIF_CONSUME_COMMITTED, 1, 1, 1, 1, 1,
        test.consume.consume_uid, 0, test.consume.generation, 0
    );
    expect_event(&log, &expected, "consume_query exact event");

    command_case_init(&test, ++nonce);
    check(command_consume(&test), "consume retire setup");
    consume = FWLAB_HIF_CONSUME_NOT_STARTED;
    check(test.port.ops->consume_commit(
              test.port.context, &test.consume, &consume) == FWLAB_HIF_PORT_OK,
          "consume retire commit setup");
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->consume_retire(
              test.port.context, &test.consume, &consume) == FWLAB_HIF_PORT_OK,
          "invoke consume_retire");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_CONSUME_RETIRE, C42_FAKE_CALL_ACTION,
        FWLAB_HIF_PORT_OK, FWLAB_HIF_CONSUME_RETIRED, 1, 1, 1, 1, 1,
        test.consume.consume_uid, 0, test.consume.generation, 0
    );
    expect_event(&log, &expected, "consume_retire exact event");

    command_case_init(&test, ++nonce);
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->reset_begin(
              test.port.context, test.command.instance_nonce, 31) ==
              FWLAB_HIF_PORT_OK,
          "invoke reset_begin");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_RESET_BEGIN, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_OK, 0, 0, 1, 1, 0, 0,
        test.command.instance_nonce, 0, 31, 0
    );
    expected.reported_effect = 31;
    expect_event(&log, &expected, "reset_begin exact event");

    command_case_init(&test, ++nonce);
    check(test.port.ops->reset_begin(
              test.port.context, test.command.instance_nonce, 31) ==
              FWLAB_HIF_PORT_OK,
          "reset query setup");
    boolean = false;
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->reset_quiescent(
              test.port.context, test.command.instance_nonce, 31,
              &boolean) == FWLAB_HIF_PORT_OK && boolean,
          "invoke reset_quiescent");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_RESET_QUIESCENT, C42_FAKE_CALL_QUERY,
        FWLAB_HIF_PORT_OK, 1, 1, 1, 1, 1, 1,
        test.command.instance_nonce, 0, 31, 0
    );
    expect_event(&log, &expected, "reset_quiescent exact event");

    command_case_init(&test, ++nonce);
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->teardown_begin(
              test.port.context, test.command.instance_nonce, 31) ==
              FWLAB_HIF_PORT_OK,
          "invoke teardown_begin");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_TEARDOWN_BEGIN, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_OK, 0, 0, 1, 1, 0, 0,
        test.command.instance_nonce, 0, 31, 0
    );
    expected.reported_effect = 31;
    expect_event(&log, &expected, "teardown_begin exact event");

    command_case_init(&test, ++nonce);
    check(test.port.ops->teardown_begin(
              test.port.context, test.command.instance_nonce, 31) ==
              FWLAB_HIF_PORT_OK,
          "teardown query setup");
    boolean = false;
    c42_fake_command_bind_event_log(&test.command, &log);
    check(test.port.ops->teardown_quiescent(
              test.port.context, test.command.instance_nonce, 31,
              &boolean) == FWLAB_HIF_PORT_OK && boolean,
          "invoke teardown_quiescent");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_TEARDOWN_QUIESCENT, C42_FAKE_CALL_QUERY,
        FWLAB_HIF_PORT_OK, 1, 1, 1, 1, 1, 1,
        test.command.instance_nonce, 0, 31, 0
    );
    expect_event(&log, &expected, "teardown_quiescent exact event");

    check(log.count == 20 && log.overflow == 0,
          "all twenty command provider entrypoints executed");
}

static void test_command_injection_truth(void)
{
    struct c42_fake_event_log log;
    struct command_case test;
    struct c42_fake_event expected;
    struct c42_fake_command_injection injection = {0};
    struct fwlab_hif_prepare_result result;
    struct fwlab_hif_prepared_token mismatched;
    bool aborted = false;

    executions++;
    c42_fake_event_log_init(&log);
    command_case_init(&test, UINT64_C(0x4300000000000201));
    injection.operation = C42_FAKE_COMMAND_PREPARE;
    injection.result = FWLAB_HIF_PORT_IN_PROGRESS;
    injection.value = FWLAB_HIF_PREPARE_BACKPRESSURE;
    injection.omit_outputs = 1;
    injection.flags = C42_FAKE_APPLY_EFFECT;
    injection.requested_effect = C42_FAKE_COMMAND_EFFECT_PREPARED;
    check(c42_fake_command_injection_push(&test.command, &injection) == C42_OK,
          "command injected hidden prepare accepted");
    c42_fake_command_bind_event_log(&test.command, &log);
    memset(&result, 0xa5, sizeof(result));
    check(test.port.ops->prepare_start(
              test.port.context, &test.key, &result) ==
              FWLAB_HIF_PORT_IN_PROGRESS,
          "command injected hidden prepare invoked");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_PREPARE, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_IN_PROGRESS, 0, 0, 1, 1, 0, 0,
        test.key.client_uid, 0, 31, 1
    );
    expected.requested_effect = C42_FAKE_COMMAND_EFFECT_PREPARED;
    expected.reported_effect = FWLAB_HIF_PREPARE_BACKPRESSURE;
    expected.applied_effect = C42_FAKE_COMMAND_EFFECT_PREPARED;
    expected.flags = C42_FAKE_EVENT_EFFECT_APPLIED |
                     C42_FAKE_EVENT_RESPONSE_LOST;
    expect_event(&log, &expected, "command requested/reported/applied exact");
    check(test.command.records[0].prepared.reservation_uid != 0,
          "command event applied effect matches provider state");

    test.prepared = test.command.records[0].prepared;
    mismatched = test.prepared;
    mismatched.reservation_uid++;
    c42_fake_event_log_init(&log);
    check(test.port.ops->prepare_abort(
              test.port.context, &mismatched, &aborted) == FWLAB_HIF_PORT_STALE,
          "valid-but-mismatched command token rejected");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_PREPARE_ABORT, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_STALE, 0, 0, 1, 0, 0, 0,
        mismatched.reservation_uid, 0, mismatched.generation, 0
    );
    expect_event(&log, &expected, "command structural/match facts separated");

    command_case_init(&test, UINT64_C(0x4300000000000202));
    memset(&injection, 0, sizeof(injection));
    injection.operation = C42_FAKE_COMMAND_PREPARE;
    injection.result = FWLAB_HIF_PORT_OK;
    injection.value = FWLAB_HIF_PREPARE_RESERVED;
    injection.write_mask = C42_FAKE_WRITE_OBJECT;
    injection.flags = C42_FAKE_APPLY_EFFECT;
    injection.requested_effect = C42_FAKE_COMMAND_EFFECT_PREPARED;
    injection.object_variant = C42_FAKE_OBJECT_ZERO;
    check(c42_fake_command_injection_push(&test.command, &injection) == C42_OK,
          "command zero output injection accepted");
    c42_fake_event_log_init(&log);
    c42_fake_command_bind_event_log(&test.command, &log);
    memset(&result, 0xa5, sizeof(result));
    check(test.port.ops->prepare_start(
              test.port.context, &test.key, &result) == FWLAB_HIF_PORT_OK &&
          result.prepared.reservation_uid == 0,
          "command zero output injection invoked");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_PREPARE, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_OK, 0, 2, 1, 1, 0, 0,
        test.key.client_uid, 0, 31, 1
    );
    expected.requested_effect = C42_FAKE_COMMAND_EFFECT_PREPARED;
    expected.reported_effect = FWLAB_HIF_PREPARE_RESERVED;
    expected.applied_effect = C42_FAKE_COMMAND_EFFECT_PREPARED;
    expected.flags = C42_FAKE_EVENT_EFFECT_APPLIED;
    expect_event(&log, &expected, "command zero output token exact event");

    command_case_init(&test, UINT64_C(0x4300000000000203));
    injection.object_variant = C42_FAKE_OBJECT_MISMATCH;
    check(c42_fake_command_injection_push(&test.command, &injection) == C42_OK,
          "command mismatch output injection accepted");
    c42_fake_event_log_init(&log);
    c42_fake_command_bind_event_log(&test.command, &log);
    memset(&result, 0xa5, sizeof(result));
    check(test.port.ops->prepare_start(
              test.port.context, &test.key, &result) == FWLAB_HIF_PORT_OK &&
          result.prepared.reservation_uid != 0,
          "command mismatch output injection invoked");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_PREPARE, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_OK, 0, 2, 1, 1, 1, 0,
        test.key.client_uid, result.prepared.reservation_uid, 31, 1
    );
    expected.requested_effect = C42_FAKE_COMMAND_EFFECT_PREPARED;
    expected.reported_effect = FWLAB_HIF_PREPARE_RESERVED;
    expected.applied_effect = C42_FAKE_COMMAND_EFFECT_PREPARED;
    expected.flags = C42_FAKE_EVENT_EFFECT_APPLIED;
    expect_event(&log, &expected, "command mismatch output token exact event");

    memset(&mismatched, 0, sizeof(mismatched));
    c42_fake_event_log_init(&log);
    aborted = false;
    check(test.port.ops->prepare_abort(
              test.port.context, &mismatched, &aborted) ==
              FWLAB_HIF_PORT_STALE,
          "zero command input token rejected");
    expected = command_expected(
        &log, C42_FAKE_COMMAND_PREPARE_ABORT, C42_FAKE_CALL_START,
        FWLAB_HIF_PORT_STALE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    );
    expect_event(&log, &expected, "command zero input token exact event");
}

int main(void)
{
    test_memory_direct_fifo_and_events();
    test_memory_abort_event();
    test_command_all_entrypoints();
    test_command_injection_truth();
    if (failures != 0) {
        return 1;
    }
    printf(
        "C4.2 provider matrix: PASS executions=%u command_entrypoints=20 "
        "command_ops=15 memory_entrypoints=15 start-query-distinct=yes "
        "write-mask=exact identity-facts=separate effects=truthful\n",
        executions
    );
    return 0;
}
