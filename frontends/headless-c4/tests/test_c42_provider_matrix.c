/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fakes/c42_command.h"
#include "fakes/c42_memory.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int failures;
static uint32_t executions;

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
        0, C42_FAKE_EVENT_WRITE_OBJECT,
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
        C42_MEMORY_IN_PROGRESS, 0, 1, C42_MEMORY_RETIRED,
        C42_MEMORY_RETIRED, 4, 1, 1);
    push_memory_direct(&memory, C42_FAKE_MEMORY_SCRUB_RETIRE,
        C42_MEMORY_OK, 1, 0, C42_MEMORY_RETIRED,
        C42_MEMORY_RETIRED, 4, 1, 1);
    push_memory_direct(&memory, C42_FAKE_MEMORY_BODY,
        C42_MEMORY_IN_PROGRESS, 0, 1, C42_MEMORY_EXACT_PREFIX,
        C42_MEMORY_EXACT_PREFIX, 7, 0, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_BODY,
        C42_MEMORY_OK, 1, 1, C42_MEMORY_FULL,
        C42_MEMORY_FULL, 15, 1, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_MARKER,
        C42_MEMORY_IN_PROGRESS, 0, 1, C42_MEMORY_UNKNOWN,
        C42_MEMORY_UNKNOWN, 0, 1, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_MARKER,
        C42_MEMORY_OK, 1, 1, C42_MEMORY_FULL,
        C42_MEMORY_FULL, 0, 1, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_RESET_BEGIN,
        C42_MEMORY_IN_PROGRESS, 0, 1, C42_MEMORY_NO_EFFECT,
        C42_MEMORY_OK, 0, 0, 0);
    push_memory_direct(&memory, C42_FAKE_MEMORY_RESET_QUIESCENT,
        C42_MEMORY_OK, 1, 0, C42_MEMORY_OK,
        C42_MEMORY_OK, 0, 0, 1);
    push_memory_direct(&memory, C42_FAKE_MEMORY_TEARDOWN_BEGIN,
        C42_MEMORY_IN_PROGRESS, 0, 1, C42_MEMORY_NO_EFFECT,
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
              &status) == C42_MEMORY_IN_PROGRESS,
          "memory retire response loss");
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->scrub_retire_query(
              port.context, &cq, 4, 0, &scrub, &status) == C42_MEMORY_OK &&
          status.result == C42_MEMORY_RETIRED,
          "memory retire query status");
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->body_start(
              port.context, &cq, 0, cqe, &body,
              &status) == C42_MEMORY_IN_PROGRESS,
          "memory body response loss");
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->body_query(
              port.context, &cq, 0, cqe, &body, &status) == C42_MEMORY_OK &&
          status.result == C42_MEMORY_FULL && status.prefix == 15,
          "memory body query status");
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->marker_start(
              port.context, &cq, 0, 1, &marker,
              &status) == C42_MEMORY_IN_PROGRESS,
          "memory marker response loss");
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->marker_query(
              port.context, &cq, 0, 1, &marker, &status) == C42_MEMORY_OK &&
          status.result == C42_MEMORY_FULL && status.committed == 1,
          "memory marker query status");
    check(port.ops->reset_begin(
              port.context, memory.instance_nonce, 11) ==
              C42_MEMORY_IN_PROGRESS,
          "memory reset begin response loss");
    quiescent = false;
    check(port.ops->reset_quiescent(
              port.context, memory.instance_nonce, 11,
              &quiescent) == C42_MEMORY_OK && quiescent,
          "memory reset quiescent output");
    check(port.ops->teardown_begin(
              port.context, memory.instance_nonce, 12) ==
              C42_MEMORY_IN_PROGRESS,
          "memory teardown begin response loss");
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

        check(event->sequence == index + 1u &&
              event->provider == C42_FAKE_EVENT_MEMORY &&
              event->operation == expected_operation[index] &&
              event->call_kind == expected_call[index] &&
              event->output_write_mask == expected_write[index] &&
              event->identity_valid != 0,
              "memory event exact order/call/write-mask");
        if ((event->flags & C42_FAKE_EVENT_RESPONSE_LOST) != 0) {
            check(event->output_write_mask == 0 &&
                  (event->flags & C42_FAKE_EVENT_EFFECT_APPLIED) != 0,
                  "memory response loss binds hidden effect to zero writes");
        }
    }
}

static void test_memory_abort_event(void)
{
    struct c42_fake_event_log log;
    struct c42_fake_memory memory;
    struct c42_memory_port port;
    struct c42_queue_memory_cap cq;
    struct c42_memory_token scrub;
    struct c42_memory_status status;

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
    check(c42_fake_memory_map(&memory, &cq, 4) == C42_OK &&
          port.ops->scrub_start(
              port.context, &cq, 4, 0, &scrub, &status) == C42_MEMORY_OK,
          "abort matrix committed scrub setup");
    c42_fake_event_log_init(&log);
    push_memory_direct(&memory, C42_FAKE_MEMORY_SCRUB_ABORT,
        C42_MEMORY_IN_PROGRESS, 0, 1, C42_MEMORY_RETIRED,
        C42_MEMORY_RETIRED, 4, 1, 1);
    memset(&status, 0xa5, sizeof(status));
    check(port.ops->scrub_abort(
              port.context, &cq, 4, 0, &scrub,
              &status) == C42_MEMORY_IN_PROGRESS &&
          log.count == 1 &&
          log.events[0].operation == C42_FAKE_MEMORY_SCRUB_ABORT &&
          log.events[0].call_kind == C42_FAKE_CALL_ACTION &&
          log.events[0].output_write_mask == 0 &&
          log.events[0].flags ==
              (C42_FAKE_EVENT_EFFECT_APPLIED |
               C42_FAKE_EVENT_RESPONSE_LOST),
              "abort event records exact response loss");
    {
        struct c42_fake_memory_direct_injection direct = {0};

        direct.operation = C42_FAKE_MEMORY_SCRUB;
        direct.result = C42_MEMORY_OK;
        direct.write_status = 1;
        direct.logical_effect = C42_MEMORY_FULL;
        direct.prefix = 4;
        direct.committed = 1;
        direct.quiescent = 1;
        direct.token_variant = C42_FAKE_MEMORY_TOKEN_MISMATCH;
        memset(&status, 0xa5, sizeof(status));
        check(c42_fake_memory_direct_push(
                  &memory, &direct) == C42_OK &&
              port.ops->scrub_query(
                  port.context, &cq, 4, 0, &scrub,
                  &status) == C42_MEMORY_OK &&
              status.token.uid == scrub.uid + 1u &&
              log.count == 2 &&
              log.events[1].operation == C42_FAKE_MEMORY_SCRUB &&
              log.events[1].call_kind == C42_FAKE_CALL_QUERY &&
              log.events[1].output_write_mask ==
                  C42_FAKE_EVENT_WRITE_OBJECT &&
              log.events[1].object_uid == scrub.uid + 1u &&
              log.events[1].identity_valid == 0,
              "memory event exposes mismatched returned token");
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

static void test_command_fifo_masks_and_call_kinds(void)
{
    struct c42_fake_command catalog;
    struct c42_fake_command command;
    struct c42_fake_event_log log;
    struct fwlab_hif_command_port port;
    struct fwlab_hif_prepare_key key;
    struct fwlab_hif_prepare_result result;
    struct c42_fake_command_injection injection = {0};
    uint32_t operation;

    executions++;
    c42_fake_command_init(
        &catalog, UINT64_C(0x4300000000000001), 31, 8
    );
    for (operation = C42_FAKE_COMMAND_PREPARE;
         operation <= C42_FAKE_COMMAND_TEARDOWN_QUIESCENT;
         ++operation) {
        memset(&injection, 0, sizeof(injection));
        injection.operation = operation;
        injection.result = FWLAB_HIF_PORT_IN_PROGRESS;
        injection.value = operation;
        injection.omit_outputs = 1;
        injection.write_mask = 0;
        injection.flags = (uint8_t)(operation % 2u);
        injection.object_variant = C42_FAKE_OBJECT_MISMATCH;
        check(c42_fake_command_injection_push(
                  &catalog, &injection) == C42_OK,
              "command operation accepts FIFO injection");
    }
    check(catalog.injection_count == C42_FAKE_COMMAND_TEARDOWN_QUIESCENT,
          "command FIFO contains every operation exactly once");
    for (operation = 0; operation < catalog.injection_count; ++operation) {
        check(catalog.injections[operation].operation == operation + 1u,
              "command FIFO preserves operation order");
    }

    c42_fake_event_log_init(&log);
    c42_fake_command_init(
        &command, UINT64_C(0x4300000000000011), 31, 8
    );
    c42_fake_command_bind_event_log(&command, &log);
    port = c42_fake_command_port(&command);
    key = prepare_key(command.instance_nonce);

    memset(&injection, 0, sizeof(injection));
    injection.operation = C42_FAKE_COMMAND_PREPARE;
    injection.result = FWLAB_HIF_PORT_IN_PROGRESS;
    injection.value = FWLAB_HIF_PREPARE_BACKPRESSURE;
    injection.write_mask = C42_FAKE_WRITE_VALUE;
    injection.flags = C42_FAKE_APPLY_EFFECT;
    injection.object_variant = C42_FAKE_OBJECT_EXACT;
    check(c42_fake_command_injection_push(
              &command, &injection) == C42_OK,
          "command start partial injection");
    injection.result = FWLAB_HIF_PORT_OK;
    injection.value = FWLAB_HIF_PREPARE_RESERVED;
    injection.write_mask = C42_FAKE_WRITE_OBJECT;
    injection.flags = 0;
    injection.object_variant = C42_FAKE_OBJECT_MISMATCH;
    check(c42_fake_command_injection_push(
              &command, &injection) == C42_OK,
          "command query object-only injection");
    injection.result = FWLAB_HIF_PORT_IN_PROGRESS;
    injection.write_mask = 0;
    injection.omit_outputs = 1;
    injection.flags = C42_FAKE_APPLY_EFFECT;
    injection.object_variant = C42_FAKE_OBJECT_ZERO;
    check(c42_fake_command_injection_push(
              &command, &injection) == C42_OK,
          "command response-loss injection");

    memset(&result, 0xa5, sizeof(result));
    check(port.ops->prepare_start(
              port.context, &key, &result) == FWLAB_HIF_PORT_IN_PROGRESS &&
          result.disposition == FWLAB_HIF_PREPARE_BACKPRESSURE,
          "command start writes only value fields");
    memset(&result, 0xa5, sizeof(result));
    check(port.ops->prepare_query(
              port.context, &key, &result) == FWLAB_HIF_PORT_OK &&
          result.disposition == UINT32_C(0xa5a5a5a5) &&
          result.prepared.reservation_uid != 0,
          "command query writes only object fields");
    memset(&result, 0xa5, sizeof(result));
    check(port.ops->prepare_query(
              port.context, &key, &result) == FWLAB_HIF_PORT_IN_PROGRESS,
          "command response loss call");
    if (!(command.injection_index == command.injection_count &&
          log.count == 3 && log.overflow == 0 &&
          log.events[0].call_kind == C42_FAKE_CALL_START &&
          log.events[0].output_write_mask == C42_FAKE_EVENT_WRITE_VALUE &&
          log.events[0].object_uid == 0 &&
          log.events[1].call_kind == C42_FAKE_CALL_QUERY &&
          log.events[1].output_write_mask == C42_FAKE_EVENT_WRITE_OBJECT &&
          log.events[1].object_uid != 0 &&
          log.events[1].identity_valid == 0 &&
          log.events[2].call_kind == C42_FAKE_CALL_QUERY &&
          log.events[2].output_write_mask == 0 &&
          log.events[2].flags ==
              (C42_FAKE_EVENT_EFFECT_APPLIED |
               C42_FAKE_EVENT_RESPONSE_LOST) &&
          log.events[0].token_uid == key.client_uid &&
          log.events[0].parameter0 == key.controller_epoch &&
          log.events[0].parameter1 == key.client_generation)) {
        uint32_t index;

        for (index = 0; index < log.count; ++index) {
            const struct c42_fake_event *event = &log.events[index];

            fprintf(stderr,
                "command event[%u]: call=%u mask=%u flags=%u token=%llu "
                "object=%llu p0=%u p1=%u\n",
                index, event->call_kind, event->output_write_mask,
                event->flags, (unsigned long long)event->token_uid,
                (unsigned long long)event->object_uid,
                event->parameter0, event->parameter1);
        }
    }
    check(command.injection_index == command.injection_count &&
          log.count == 3 && log.overflow == 0 &&
          log.events[0].call_kind == C42_FAKE_CALL_START &&
          log.events[0].output_write_mask == C42_FAKE_EVENT_WRITE_VALUE &&
          log.events[0].object_uid == 0 &&
          log.events[1].call_kind == C42_FAKE_CALL_QUERY &&
          log.events[1].output_write_mask == C42_FAKE_EVENT_WRITE_OBJECT &&
          log.events[1].object_uid != 0 &&
          log.events[1].identity_valid == 0 &&
          log.events[2].call_kind == C42_FAKE_CALL_QUERY &&
          log.events[2].output_write_mask == 0 &&
          log.events[2].flags ==
              (C42_FAKE_EVENT_EFFECT_APPLIED |
               C42_FAKE_EVENT_RESPONSE_LOST) &&
          log.events[0].token_uid == key.client_uid &&
          log.events[0].parameter0 == key.controller_epoch &&
          log.events[0].parameter1 == key.client_generation,
          "command event log preserves call/mask/token/effect facts");
}

int main(void)
{
    test_memory_direct_fifo_and_events();
    test_memory_abort_event();
    test_command_fifo_masks_and_call_kinds();
    if (failures != 0) {
        return 1;
    }
    printf(
        "C4.2 provider matrix: PASS executions=%u command_ops=15 "
        "memory_calls=16 start-query-distinct=yes write-mask=exact "
        "response-loss=explicit\n",
        executions
    );
    return 0;
}
