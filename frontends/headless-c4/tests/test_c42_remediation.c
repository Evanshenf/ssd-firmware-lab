/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_support.h"

#include <stdio.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define C42_TEST_NOINLINE __attribute__((noinline))
#else
#define C42_TEST_NOINLINE
#endif

static int failures;
static uint32_t executions;

static void check(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "C4.2a remediation FAIL: %s\n", name);
        failures++;
    }
}

static struct c42_queue_memory_cap fresh_cap(
    const struct c42_test_fixture *fixture,
    uint16_t queue_id,
    uint8_t role,
    uint32_t generation,
    uint64_t memory_uid)
{
    struct c42_queue_memory_cap cap = {0};

    cap.instance_nonce = fixture->config.instance_nonce;
    cap.owner_epoch = fixture->config.owner_epoch;
    cap.memory_uid = memory_uid;
    cap.controller_epoch = fixture->config.initial_controller_epoch;
    cap.ring_generation = generation;
    cap.mapping_generation = generation;
    cap.exact_bytes = (uint32_t)fixture->depth *
                      (role == C42_MEMORY_SQ_READ ?
                       C42_SQE_BYTES : C42_CQE_BYTES);
    cap.queue_id = queue_id;
    cap.role = role;
    return cap;
}

static struct c42_queue_descriptor descriptor_for(
    const struct c42_test_fixture *fixture,
    const struct c42_queue_memory_cap *cap,
    uint8_t kind)
{
    struct c42_queue_descriptor descriptor = {0};

    descriptor.version = C42_COMPONENT_VERSION;
    descriptor.size = sizeof(descriptor);
    descriptor.queue_id = cap->queue_id;
    descriptor.associated_cq_id = cap->queue_id;
    descriptor.depth = fixture->depth;
    descriptor.kind = kind;
    descriptor.queue_class = cap->queue_id == 0 ?
                             FWLAB_NVME_QUEUE_ADMIN : FWLAB_NVME_QUEUE_IO;
    descriptor.memory = *cap;
    return descriptor;
}

static int reset_to_cold(struct c42_test_fixture *fixture)
{
    struct c42_operation_token token = {0};
    struct c42_control_status status = {0};

    return c42_reset_start(fixture->controller, &token) == C42_OK &&
           c42_control_progress(fixture->controller, &token, 32) == C42_OK &&
           c42_control_query(
               fixture->controller, &token, &status) == C42_OK &&
           status.state == C42_CONTROL_COMMITTED;
}

static C42_TEST_NOINLINE void test_admission_closed_matrix(void)
{
    static const uint32_t bad_results[] = {1, 2, 3, 4, 6, 7, 8, 9};
    static const uint32_t bad_states[] = {
        FWLAB_HIF_ADMISSION_COMMITTED,
        FWLAB_HIF_ADMISSION_ABORTED,
        FWLAB_HIF_ADMISSION_POISONED,
        FWLAB_HIF_ADMISSION_POISONED + 1u,
        FWLAB_HIF_ADMISSION_POISONED + 2u
    };
    uint32_t index;

    for (index = 0; index < sizeof(bad_results) / sizeof(bad_results[0]);
         ++index) {
        struct c42_test_fixture fixture;
        struct c42_fake_command_script script = {0};
        struct c42_snapshot snapshot = {0};

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0xa130000000000000) + index + 1u),
              "F13 admission result fixture");
        script.inject_operation = C42_FAKE_COMMAND_ADMIT;
        script.inject_result = bad_results[index];
        script.inject_count = 1;
        script.inject_omit_outputs = 1;
        c42_fake_command_set_script(&fixture.command, &script);
        check(c42_test_submit(&fixture, 0, 0, 1, (uint16_t)(100 + index)) &&
              c42_test_run(&fixture, 1, 3),
              "F13 admission result injected");
        check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
              snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED &&
              snapshot.sq[0].device_index == 0 &&
              snapshot.sq[0].pending_or_unacked == 1,
              "F13 ambiguous admission cannot advance SQ head");
        check(reset_to_cold(&fixture),
              "F13 ambiguous admission global reset drain");
    }

    for (index = 0; index < sizeof(bad_states) / sizeof(bad_states[0]);
         ++index) {
        struct c42_test_fixture fixture;
        struct c42_fake_command_script script = {0};
        struct c42_snapshot snapshot = {0};

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0xa131000000000000) + index + 1u),
              "F13 admission state fixture");
        script.inject_operation = C42_FAKE_COMMAND_ADMIT;
        script.inject_result = FWLAB_HIF_PORT_OK;
        script.inject_value = bad_states[index];
        script.inject_count = 1;
        c42_fake_command_set_script(&fixture.command, &script);
        check(c42_test_submit(&fixture, 0, 0, 1, (uint16_t)(120 + index)) &&
              c42_test_run(&fixture, 1, 3),
              "F13 admission state injected");
        check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
              snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED &&
              snapshot.sq[0].device_index == 0,
              "F13 non-committable admission state fails closed");
        check(reset_to_cold(&fixture),
              "F13 admission state reset drain");
    }
}

static C42_TEST_NOINLINE void test_memory_status_matrix(void)
{
    uint32_t variant;

    for (variant = 0; variant < 4; ++variant) {
        struct c42_test_fixture fixture;
        struct c42_queue_memory_cap cap;
        struct c42_queue_descriptor descriptor;
        struct c42_operation_token token = {0};
        struct c42_candidate_status status = {0};
        struct c42_snapshot snapshot = {0};
        struct c42_fake_memory_outcome outcome = {0};

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0xa132000000000000) + variant + 1u),
              "F13 memory status fixture");
        cap = fresh_cap(
            &fixture, 1, C42_MEMORY_CQ_PUBLISH, 1,
            UINT64_C(0xa1321000) + variant
        );
        descriptor = descriptor_for(&fixture, &cap, C42_QUEUE_CQ);
        outcome.operation = C42_FAKE_MEMORY_SCRUB;
        outcome.effect = variant == 0 ?
                         C42_MEMORY_RETIRED + 1u : C42_MEMORY_FULL;
        outcome.committed = 1;
        if (variant == 1) {
            outcome.status_override = 1;
            outcome.status_committed = 2;
        } else if (variant == 2) {
            outcome.status_override = 2;
            outcome.status_quiescent = 2;
        } else if (variant == 3) {
            outcome.status_override = 3;
            outcome.status_committed = 2;
            outcome.status_quiescent = 2;
        }
        check(c42_fake_memory_map(
                  &fixture.memory, &cap, fixture.depth) == C42_OK &&
              c42_fake_memory_script_push(
                  &fixture.memory, &outcome) == C42_OK &&
              c42_candidate_prepare(
                  fixture.controller, &descriptor, &token) == C42_OK,
              "F13 memory status candidate");
        check(c42_candidate_progress(
                  fixture.controller, &token, 1) == C42_POISONED &&
              c42_candidate_query(
                  fixture.controller, &token, &status) == C42_OK &&
              status.state == C42_CANDIDATE_POISONED &&
              c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
              snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED,
              "F13 unknown effect and uint8 booleans poison");
        check(reset_to_cold(&fixture), "F13 memory status reset drain");
    }
}

static C42_TEST_NOINLINE void test_completion_consume_closed_matrix(void)
{
    static const uint32_t acquire_results[] = {
        FWLAB_HIF_PORT_NO_CAPACITY,
        FWLAB_HIF_PORT_IN_PROGRESS,
        FWLAB_HIF_PORT_COUNTER_EXHAUSTED + 1u,
        FWLAB_HIF_PORT_COUNTER_EXHAUSTED + 2u
    };
    static const uint32_t consume_states[] = {
        FWLAB_HIF_CONSUME_COMMITTED,
        FWLAB_HIF_CONSUME_CLEANUP_PENDING,
        FWLAB_HIF_CONSUME_RETIRED,
        FWLAB_HIF_CONSUME_ABORTED,
        FWLAB_HIF_CONSUME_POISONED,
        FWLAB_HIF_CONSUME_POISONED + 1u,
        FWLAB_HIF_CONSUME_POISONED + 2u
    };
    uint32_t index;

    for (index = 0;
         index < sizeof(acquire_results) / sizeof(acquire_results[0]);
         ++index) {
        struct c42_test_fixture fixture;
        struct c42_fake_command_script script = {0};
        struct c42_snapshot snapshot = {0};

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0xa133000000000000) + index + 1u),
              "F13 completion acquire fixture");
        script.inject_operation = C42_FAKE_COMMAND_COMPLETION_ACQUIRE;
        script.inject_result = acquire_results[index];
        script.inject_count = 1;
        script.inject_omit_outputs = 1;
        c42_fake_command_set_script(&fixture.command, &script);
        check(c42_test_submit(&fixture, 0, 0, 1, (uint16_t)(140 + index)) &&
              c42_test_run(&fixture, 32, 4) &&
              c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
              snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED &&
              snapshot.sq[0].device_index == 1 &&
              snapshot.cq[0].pending_or_unacked == 0,
              "F13 acquire is synchronous OK-only");
        check(reset_to_cold(&fixture), "F13 acquire reset drain");
    }

    for (index = 0;
         index < sizeof(consume_states) / sizeof(consume_states[0]); ++index) {
        struct c42_test_fixture fixture;
        struct c42_fake_command_script script = {0};
        struct c42_snapshot snapshot = {0};

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0xa134000000000000) + index + 1u),
              "F13 consume state fixture");
        script.inject_operation = C42_FAKE_COMMAND_CONSUME_PREPARE;
        script.inject_result = FWLAB_HIF_PORT_OK;
        script.inject_value = consume_states[index];
        script.inject_count = 1;
        c42_fake_command_set_script(&fixture.command, &script);
        check(c42_test_submit(&fixture, 0, 0, 1, (uint16_t)(160 + index)) &&
              c42_test_run(&fixture, 32, 4) &&
              c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
              snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED &&
              snapshot.active_commands == 1 &&
              snapshot.cq[0].pending_or_unacked == 0,
              "F13 malformed consume retains ownership for global reset");
        check(reset_to_cold(&fixture), "F13 consume reset drain");
    }
}

static C42_TEST_NOINLINE void test_exact_object_malformed_states(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_injection injection = {0};
    struct c42_snapshot snapshot = {0};

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa134100000000001)),
          "F13 exact admission object fixture");
    injection.operation = C42_FAKE_COMMAND_ADMIT;
    injection.result = FWLAB_HIF_PORT_OK;
    injection.value = FWLAB_HIF_ADMISSION_POISONED + 1u;
    injection.write_mask = C42_FAKE_WRITE_VALUE | C42_FAKE_WRITE_OBJECT;
    injection.object_variant = C42_FAKE_OBJECT_EXACT;
    check(c42_fake_command_injection_push(
              &fixture.command, &injection) == C42_OK &&
          c42_test_submit(&fixture, 0, 0, 1, 175) &&
          c42_test_run(&fixture, 32, 4) &&
          c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED &&
          snapshot.sq[0].device_index == 0,
          "F13 unknown admission state poisons despite exact ticket");
    check(reset_to_cold(&fixture), "F13 exact admission reset drain");

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa134100000000002)),
          "F13 exact consume object fixture");
    memset(&injection, 0, sizeof(injection));
    injection.operation = C42_FAKE_COMMAND_CONSUME_PREPARE;
    injection.result = FWLAB_HIF_PORT_OK;
    injection.value = FWLAB_HIF_CONSUME_POISONED + 1u;
    injection.write_mask = C42_FAKE_WRITE_VALUE | C42_FAKE_WRITE_OBJECT;
    injection.flags = C42_FAKE_APPLY_EFFECT;
    injection.object_variant = C42_FAKE_OBJECT_EXACT;
    check(c42_fake_command_injection_push(
              &fixture.command, &injection) == C42_OK &&
          c42_test_submit(&fixture, 0, 0, 1, 176) &&
          c42_test_run(&fixture, 32, 4) &&
          c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED &&
          snapshot.active_commands == 1 &&
          fixture.command.records[0].consume_prepared == 1,
          "F13 unknown consume state retains exact applied token for reset");
    check(reset_to_cold(&fixture), "F13 exact consume reset drain");
}

static uint32_t cleanup_query_total(
    const struct c42_fake_command *command)
{
    uint32_t total = 0;
    uint32_t index;

    for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
        total += command->records[index].cleanup_queries;
    }
    return total;
}

static C42_TEST_NOINLINE void test_ultra_critical_regressions(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_injection injection = {0};
    struct c42_snapshot snapshot = {0};

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa136000000000001)),
          "C01 poison-query fixture");
    injection.operation = C42_FAKE_COMMAND_ADMIT;
    injection.result = FWLAB_HIF_PORT_COUNTER_EXHAUSTED + 1u;
    injection.omit_outputs = 1;
    check(c42_fake_command_injection_push(
              &fixture.command, &injection) == C42_OK,
          "C01 queue ambiguous admit response");
    injection.result = FWLAB_HIF_PORT_OK;
    check(c42_fake_command_injection_push(
              &fixture.command, &injection) == C42_OK,
          "C01 queue omitted poison-query output");
    check(c42_test_submit(&fixture, 0, 0, 1, 190) &&
          c42_test_run(&fixture, 12, 1) &&
          c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED &&
          snapshot.sq[0].device_index == 0 &&
          fixture.command.prepare_abort_call_count == 0,
          "C01 omitted query output is not abort proof");
    check(reset_to_cold(&fixture), "C01 reset drain");

    {
        struct c42_fake_command_script script = {0};
        struct c42_operation_token reset = {0};
        struct c42_operation_token teardown = {0};
        struct c42_control_status status = {0};
        uint32_t before;
        uint32_t event_count;

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0, UINT64_C(0xa136000000000002)),
              "C02 post-LP reconcile fixture");
        script.cleanup_pending = 1;
        script.cleanup_delay = 1000;
        c42_fake_command_set_script(&fixture.command, &script);
        memset(&injection, 0, sizeof(injection));
        injection.operation = C42_FAKE_COMMAND_RESET_BEGIN;
        injection.result = FWLAB_HIF_PORT_INVALID;
        injection.omit_outputs = 1;
        check(c42_fake_command_injection_push(
                  &fixture.command, &injection) == C42_OK &&
              c42_test_submit(&fixture, 0, 0, 1, 191) &&
              c42_test_run(&fixture, 64, 4),
              "C02 create cleanup-pending consume");
        check(c42_reset_start(fixture.controller, &reset) == C42_OK &&
              c42_control_progress(
                  fixture.controller, &reset, 1) == C42_POISONED,
              "C02 poison reset control after LP");
        before = cleanup_query_total(&fixture.command);
        event_count = fixture.event_log.count;
        check(c42_test_run(&fixture, 8, 1) &&
              cleanup_query_total(&fixture.command) == before &&
              fixture.event_log.count == event_count &&
              fixture.event_log.overflow == 0 &&
              c42_control_query(
                  fixture.controller, &reset, &status) == C42_OK &&
              status.state == C42_CONTROL_POISONED,
              "C02 no old consume calls after poisoned reset LP");
        check(c42_teardown_start(
                  fixture.controller, &teardown) == C42_OK &&
              c42_control_progress(
                  fixture.controller, &teardown, 32) == C42_OK,
              "C02 protected teardown recovery");
    }

    {
        struct c42_queue_memory_cap cap;
        struct c42_queue_descriptor descriptor;
        struct c42_operation_token candidate = {0};
        struct c42_candidate_status status = {0};
        struct c42_fake_memory_direct_injection direct = {0};

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0, UINT64_C(0xa136000000000003)),
              "C03 direct retire fixture");
        cap = fresh_cap(
            &fixture, 1, C42_MEMORY_CQ_PUBLISH, 1,
            UINT64_C(0xa1361003)
        );
        descriptor = descriptor_for(&fixture, &cap, C42_QUEUE_CQ);
        check(c42_fake_memory_map(
                  &fixture.memory, &cap, fixture.depth) == C42_OK &&
              c42_candidate_prepare(
                  fixture.controller, &descriptor, &candidate) == C42_OK &&
              c42_candidate_progress(
                  fixture.controller, &candidate, 4) == C42_OK &&
              c42_candidate_commit(
                  fixture.controller, &candidate) == C42_OK,
              "C03 committed CQ candidate");
        direct.operation = C42_FAKE_MEMORY_SCRUB_RETIRE;
        direct.result = C42_MEMORY_UNKNOWN;
        direct.omit_status = 1;
        check(c42_fake_memory_direct_push(
                  &fixture.memory, &direct) == C42_OK &&
              c42_candidate_retire(
                  fixture.controller, &candidate) == C42_POISONED &&
              c42_candidate_query(
                  fixture.controller, &candidate, &status) == C42_OK &&
              status.state == C42_CANDIDATE_POISONED &&
              c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
              snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED,
              "C03 direct UNKNOWN is provider poison");
        check(reset_to_cold(&fixture), "C03 reset drain");
    }
}

static C42_TEST_NOINLINE void test_observer_state_mapping(void)
{
    struct c42_test_fixture fixture;
    struct c42_observer_v2 observer;
    uint32_t step;
    int saw_reserved = 0;
    int saw_body = 0;
    int saw_marker = 0;
    int saw_committed = 0;

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa137000000000001)),
          "C04 observer mapping fixture");
    check(c42_observer_read_v2(fixture.controller, &observer) == C42_OK &&
          observer.cq[0].slots[0].state == C42_OBSERVER_SLOT_FREE,
          "C04 observer FREE mapping");
    check(c42_test_submit(&fixture, 0, 0, 1, 192),
          "C04 observer command submit");
    for (step = 0; step < 64; ++step) {
        struct c42_step_result result = {0};
        uint8_t state;

        check(c42_step(fixture.controller, 1, &result) == C42_OK,
              "C04 one transition");
        check(c42_observer_read_v2(
                  fixture.controller, &observer) == C42_OK,
              "C04 observer read");
        state = observer.cq[0].slots[0].state;
        saw_reserved |= state == C42_OBSERVER_SLOT_RESERVED;
        saw_body |= state == C42_OBSERVER_SLOT_BODY_STAGED;
        saw_marker |= state == C42_OBSERVER_SLOT_MARKER_RECONCILE;
        saw_committed |= state == C42_OBSERVER_SLOT_CQE_COMMITTED;
        if (saw_committed != 0) {
            break;
        }
    }
    check(saw_reserved && saw_body && saw_marker && saw_committed &&
          (uint32_t)C42_OBSERVER_SLOT_BODY_STAGED !=
              (uint32_t)C42_PUBLIC_SLOT_CQE_COMMITTED &&
          (uint32_t)C42_OBSERVER_SLOT_MARKER_RECONCILE !=
              (uint32_t)C42_PUBLIC_SLOT_CQE_COMMITTED &&
          observer.cq[0].slots[0].state ==
              C42_OBSERVER_SLOT_CQE_COMMITTED,
          "C04 observer never aliases staged/marker with committed");
}

struct observer_coverage {
    uint8_t command[256];
    uint8_t reconcile[256];
    uint8_t notification[256];
    uint8_t command_sequence[32];
    uint8_t reconcile_sequence[16];
    uint8_t notification_sequence[16];
    uint8_t command_length;
    uint8_t reconcile_length;
    uint8_t notification_length;
};

static void append_state(
    uint8_t *sequence,
    uint8_t *length,
    uint8_t capacity,
    uint8_t state)
{
    if (*length != 0 && sequence[*length - 1u] == state) return;
    if (*length < capacity) sequence[(*length)++] = state;
}

static int state_sequence_equal(
    const uint8_t *actual,
    uint8_t actual_length,
    const uint8_t *expected,
    size_t expected_length)
{
    return actual_length == expected_length &&
           memcmp(actual, expected, expected_length) == 0;
}

static int collect_observer_coverage(
    struct c42_test_fixture *fixture,
    struct observer_coverage *coverage,
    struct c42_observer_v2 *observer)
{
    uint16_t index;

    if (c42_observer_read_v2(fixture->controller, observer) != C42_OK) {
        return 0;
    }
    for (index = 0; index < observer->command_capacity; ++index) {
        coverage->command[observer->commands[index].state] = 1;
        if (observer->reconciles[index].in_use != 0) {
            coverage->reconcile[observer->reconciles[index].state] = 1;
        }
        if (observer->notifications[index].in_use != 0) {
            coverage->notification[observer->notifications[index].state] = 1;
        }
    }
    append_state(
        coverage->command_sequence, &coverage->command_length,
        sizeof(coverage->command_sequence), observer->commands[0].state
    );
    if (observer->reconciles[0].in_use != 0) {
        append_state(
            coverage->reconcile_sequence, &coverage->reconcile_length,
            sizeof(coverage->reconcile_sequence),
            observer->reconciles[0].state
        );
    }
    if (observer->notifications[0].in_use != 0) {
        append_state(
            coverage->notification_sequence, &coverage->notification_length,
            sizeof(coverage->notification_sequence),
            observer->notifications[0].state
        );
    }
    return 1;
}

static C42_TEST_NOINLINE void test_observer_reachable_state_coverage(void)
{
    static const uint8_t expected_command[] = {
        C42_OBSERVER_COMMAND_FREE,
        C42_OBSERVER_COMMAND_CAPTURED,
        C42_OBSERVER_COMMAND_PREPARE_QUERY,
        C42_OBSERVER_COMMAND_PORT_RESERVED,
        C42_OBSERVER_COMMAND_ADMIT_QUERY,
        C42_OBSERVER_COMMAND_PORT_COMMITTED,
        C42_OBSERVER_COMMAND_HIF_COMMITTED,
        C42_OBSERVER_COMMAND_READY,
        C42_OBSERVER_COMMAND_LEASED,
        C42_OBSERVER_COMMAND_CONSUME_PREPARE,
        C42_OBSERVER_COMMAND_PUB_RESERVED,
        C42_OBSERVER_COMMAND_MARKER_RECONCILE,
        C42_OBSERVER_COMMAND_ABORT_RECONCILE,
        C42_OBSERVER_COMMAND_ADMIT_POISON_HOLD,
        C42_OBSERVER_COMMAND_CONSUME_POISON_HOLD,
    };
    static const uint8_t expected_reconcile[] = {
        C42_OBSERVER_RECONCILE_RESERVED,
        C42_OBSERVER_RECONCILE_PREPARED,
        C42_OBSERVER_RECONCILE_COMMIT_UNKNOWN,
        C42_OBSERVER_RECONCILE_CLEANUP_PENDING,
        C42_OBSERVER_RECONCILE_RETIRE_READY,
    };
    static const uint8_t expected_notification[] = {
        C42_OBSERVER_NOTIFY_RESERVED,
        C42_OBSERVER_NOTIFY_READY,
        C42_OBSERVER_NOTIFY_ACQUIRED,
        C42_OBSERVER_NOTIFY_CONSUMED,
        C42_OBSERVER_NOTIFY_SUPPRESSED,
    };
    static const uint8_t lifecycle_command[] = {
        C42_OBSERVER_COMMAND_FREE,
        C42_OBSERVER_COMMAND_CAPTURED,
        C42_OBSERVER_COMMAND_PREPARE_QUERY,
        C42_OBSERVER_COMMAND_PORT_RESERVED,
        C42_OBSERVER_COMMAND_ADMIT_QUERY,
        C42_OBSERVER_COMMAND_PORT_COMMITTED,
        C42_OBSERVER_COMMAND_HIF_COMMITTED,
        C42_OBSERVER_COMMAND_READY,
        C42_OBSERVER_COMMAND_LEASED,
        C42_OBSERVER_COMMAND_CONSUME_PREPARE,
        C42_OBSERVER_COMMAND_PUB_RESERVED,
        C42_OBSERVER_COMMAND_MARKER_RECONCILE,
        C42_OBSERVER_COMMAND_FREE,
    };
    static const uint8_t lifecycle_reconcile[] = {
        C42_OBSERVER_RECONCILE_RESERVED,
        C42_OBSERVER_RECONCILE_PREPARED,
        C42_OBSERVER_RECONCILE_COMMIT_UNKNOWN,
        C42_OBSERVER_RECONCILE_CLEANUP_PENDING,
        C42_OBSERVER_RECONCILE_RETIRE_READY,
    };
    static const uint8_t lifecycle_notification[] = {
        C42_OBSERVER_NOTIFY_RESERVED,
        C42_OBSERVER_NOTIFY_READY,
        C42_OBSERVER_NOTIFY_ACQUIRED,
        C42_OBSERVER_NOTIFY_CONSUMED,
    };
    struct observer_coverage coverage = {0};
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_fake_command_injection injection = {0};
    struct c42_observer_v2 observer;
    struct c42_notification notification = {0};
    struct c42_operation_token reset = {0};
    uint32_t step;
    size_t index;

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa137000000000010)),
          "observer lifecycle fixture");
    script.prepare_delay = 2;
    script.admit_delay = 2;
    script.consume_commit_delay = 2;
    script.cleanup_pending = 1;
    script.cleanup_delay = 2;
    c42_fake_command_set_script(&fixture.command, &script);
    injection.operation = C42_FAKE_COMMAND_CONSUME_PREPARE;
    injection.result = FWLAB_HIF_PORT_IN_PROGRESS;
    injection.omit_outputs = 1;
    check(c42_fake_command_injection_push(
              &fixture.command, &injection) == C42_OK &&
          collect_observer_coverage(&fixture, &coverage, &observer) &&
          c42_test_submit(&fixture, 0, 0, 1, 193),
          "observer lifecycle setup");
    for (step = 0; step < 256; ++step) {
        struct c42_step_result result = {0};

        check(c42_step(fixture.controller, 1, &result) == C42_OK &&
              collect_observer_coverage(
                  &fixture, &coverage, &observer),
              "observer lifecycle transition");
        if (observer.notifications[0].state == C42_OBSERVER_NOTIFY_READY &&
            observer.reconciles[0].in_use == 0) {
            break;
        }
    }
    check(step < 256, "observer lifecycle reaches ready notification");
    check(c42_notification_acquire(
              fixture.controller, &notification) == C42_OK &&
          collect_observer_coverage(&fixture, &coverage, &observer) &&
          c42_notification_consume(
              fixture.controller, &notification.token) == C42_OK &&
          collect_observer_coverage(&fixture, &coverage, &observer),
          "observer notification acquired/consumed states");
    check(state_sequence_equal(
              coverage.command_sequence, coverage.command_length,
              lifecycle_command,
              sizeof(lifecycle_command) / sizeof(lifecycle_command[0])) &&
          state_sequence_equal(
              coverage.reconcile_sequence, coverage.reconcile_length,
              lifecycle_reconcile,
              sizeof(lifecycle_reconcile) /
                  sizeof(lifecycle_reconcile[0])) &&
          state_sequence_equal(
              coverage.notification_sequence, coverage.notification_length,
              lifecycle_notification,
              sizeof(lifecycle_notification) /
                  sizeof(lifecycle_notification[0])),
          "observer public states map to exact lifecycle transitions");
    coverage.command_length = 0;
    coverage.reconcile_length = 0;
    coverage.notification_length = 0;

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa137000000000011)) &&
          c42_test_submit(&fixture, 0, 0, 1, 194),
          "observer abort fixture");
    memset(&injection, 0, sizeof(injection));
    injection.operation = C42_FAKE_COMMAND_ADMIT;
    injection.result = FWLAB_HIF_PORT_OK;
    injection.value = FWLAB_HIF_ADMISSION_ABORTED;
    check(c42_fake_command_injection_push(
              &fixture.command, &injection) == C42_OK,
          "observer abort injection");
    for (step = 0; step < 32; ++step) {
        struct c42_step_result result = {0};

        (void)c42_step(fixture.controller, 1, &result);
        check(collect_observer_coverage(
                  &fixture, &coverage, &observer),
              "observer abort transition");
        if (coverage.command[C42_OBSERVER_COMMAND_ABORT_RECONCILE] != 0) {
            break;
        }
    }

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa137000000000012)) &&
          c42_test_submit(&fixture, 0, 0, 1, 195),
          "observer admission poison fixture");
    memset(&injection, 0, sizeof(injection));
    injection.operation = C42_FAKE_COMMAND_ADMIT;
    injection.result = FWLAB_HIF_PORT_COUNTER_EXHAUSTED + 1u;
    injection.omit_outputs = 1;
    check(c42_fake_command_injection_push(
              &fixture.command, &injection) == C42_OK,
          "observer admission poison injection");
    for (step = 0; step < 32; ++step) {
        struct c42_step_result result = {0};

        (void)c42_step(fixture.controller, 1, &result);
        check(collect_observer_coverage(
                  &fixture, &coverage, &observer),
              "observer admission poison transition");
        if (coverage.command[C42_OBSERVER_COMMAND_ADMIT_POISON_HOLD] != 0) {
            break;
        }
    }

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa137000000000013)) &&
          c42_test_submit(&fixture, 0, 0, 1, 196),
          "observer consume poison fixture");
    memset(&injection, 0, sizeof(injection));
    injection.operation = C42_FAKE_COMMAND_CONSUME_PREPARE;
    injection.result = FWLAB_HIF_PORT_OK;
    injection.omit_outputs = 1;
    check(c42_fake_command_injection_push(
              &fixture.command, &injection) == C42_OK,
          "observer consume poison injection");
    for (step = 0; step < 64; ++step) {
        struct c42_step_result result = {0};

        (void)c42_step(fixture.controller, 1, &result);
        check(collect_observer_coverage(
                  &fixture, &coverage, &observer),
              "observer consume poison transition");
        if (coverage.command[C42_OBSERVER_COMMAND_CONSUME_POISON_HOLD] != 0) {
            break;
        }
    }

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa137000000000014)) &&
          c42_test_submit(&fixture, 0, 0, 1, 197),
          "observer notification suppression fixture");
    {
        struct c42_step_result result = {0};

        check(c42_step(fixture.controller, 1, &result) == C42_OK &&
              collect_observer_coverage(
                  &fixture, &coverage, &observer) &&
              c42_reset_start(fixture.controller, &reset) == C42_OK &&
              collect_observer_coverage(
                  &fixture, &coverage, &observer),
              "observer notification suppression transition");
    }

    for (index = 0;
         index < sizeof(expected_command) / sizeof(expected_command[0]);
         ++index) {
        check(coverage.command[expected_command[index]] != 0,
              "observer covers each reachable command state");
    }
    for (index = 0;
         index < sizeof(expected_reconcile) / sizeof(expected_reconcile[0]);
         ++index) {
        check(coverage.reconcile[expected_reconcile[index]] != 0,
              "observer covers each reconcile state");
    }
    for (index = 0;
         index < sizeof(expected_notification) /
                     sizeof(expected_notification[0]);
         ++index) {
        check(coverage.notification[expected_notification[index]] != 0,
              "observer covers each notification state");
    }
}

static C42_TEST_NOINLINE void test_output_sentinels_and_poll(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_injection injection = {0};
    struct c42_snapshot snapshot = {0};
    uint32_t variant;

    for (variant = 0; variant < 2; ++variant) {
        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0xa138000000000000) + variant + 1u),
              "F13 poll result fixture");
        injection.operation = C42_FAKE_COMMAND_POLL;
        injection.result = variant == 0 ?
                           FWLAB_HIF_PORT_IN_PROGRESS : FWLAB_HIF_PORT_OK;
        injection.value = 0;
        injection.omit_outputs = 1;
        check(c42_fake_command_injection_push(
                  &fixture.command, &injection) == C42_OK &&
              c42_test_submit(
                  &fixture, 0, 0, 1, (uint16_t)(200 + variant)) &&
              c42_test_run(&fixture, 32, 4) &&
              c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
              snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED,
              "F13 poll IN_PROGRESS/omitted count is contract poison");
        check(reset_to_cold(&fixture), "F13 poll reset drain");
        memset(&injection, 0, sizeof(injection));
    }

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa138000000000003)),
          "F18 explicit empty poll fixture");
    injection.operation = C42_FAKE_COMMAND_POLL;
    injection.result = FWLAB_HIF_PORT_OK;
    injection.value = 0;
    injection.omit_outputs = 0;
    check(c42_fake_command_injection_push(
              &fixture.command, &injection) == C42_OK &&
          c42_test_submit(&fixture, 0, 0, 1, 202),
          "F18 explicit empty poll script");
    for (variant = 0; variant < 64; ++variant) {
        struct c42_step_result result = {0};

        check(c42_step(fixture.controller, 1, &result) == C42_OK,
              "F18 poll bounded step");
        if (fixture.command.injection_index == 1) {
            check(result.units_executed == 1 &&
                  c42_snapshot_read(
                      fixture.controller, &snapshot) == C42_OK &&
                  snapshot.phase == C42_CONTROLLER_READY &&
                  snapshot.active_commands == 1,
                  "F18 explicit empty poll counts as bounded action");
            break;
        }
    }
    check(variant < 64 && c42_test_run(&fixture, 64, 4),
          "F18 progress after empty poll");

    for (variant = 0; variant < 2; ++variant) {
        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0xa138000000000010) + variant),
              "F13 omitted enum fixture");
        memset(&injection, 0, sizeof(injection));
        injection.operation = variant == 0 ?
                              C42_FAKE_COMMAND_ADMIT :
                              C42_FAKE_COMMAND_CONSUME_PREPARE;
        injection.result = FWLAB_HIF_PORT_OK;
        injection.omit_outputs = 1;
        check(c42_fake_command_injection_push(
                  &fixture.command, &injection) == C42_OK &&
              c42_test_submit(
                  &fixture, 0, 0, 1, (uint16_t)(210 + variant)) &&
              c42_test_run(&fixture, 32, 4) &&
              c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
              snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED,
              "F13 OK with omitted enum fails closed");
        check(reset_to_cold(&fixture), "F13 omitted enum reset drain");
    }
}

static C42_TEST_NOINLINE void test_literal_single_pass_ready_scan(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_observer_v2 observer;
    uint32_t step;
    uint32_t acquire_before;

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa139000000000001)),
          "F18 literal scan fixture");
    script.poll_delay = 100;
    script.reverse_ready = 1;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_test_submit(&fixture, 0, 0, 1, 220) &&
          c42_test_submit(&fixture, 0, 1, 2, 221),
          "F18 literal scan submit");
    for (step = 0; step < 128; ++step) {
        struct c42_step_result result = {0};

        check(c42_step(fixture.controller, 1, &result) == C42_OK &&
              c42_observer_read_v2(
                  fixture.controller, &observer) == C42_OK,
              "F18 reach two HIF records");
        if (observer.commands[0].state ==
                C42_OBSERVER_COMMAND_HIF_COMMITTED &&
            observer.commands[1].state ==
                C42_OBSERVER_COMMAND_HIF_COMMITTED) {
            break;
        }
    }
    check(step < 128, "F18 both records HIF committed");
    script.poll_delay = 0;
    c42_fake_command_set_script(&fixture.command, &script);
    for (step = 0; step < 32; ++step) {
        struct c42_step_result result = {0};

        check(c42_step(fixture.controller, 1, &result) == C42_OK &&
              c42_observer_read_v2(
                  fixture.controller, &observer) == C42_OK,
              "F18 make later record READY");
        if (observer.commands[0].state ==
                C42_OBSERVER_COMMAND_HIF_COMMITTED &&
            observer.commands[1].state == C42_OBSERVER_COMMAND_READY) {
            break;
        }
    }
    check(step < 32 && observer.ready_poll_pending == 0,
          "F18 HIF before later READY setup");
    acquire_before = fixture.command.acquire_count;
    {
        struct c42_step_result result = {0};

        check(c42_step(fixture.controller, 1, &result) == C42_OK &&
              result.units_executed == 1 &&
              c42_observer_read_v2(
                  fixture.controller, &observer) == C42_OK &&
              fixture.command.acquire_count == acquire_before + 1u &&
              observer.commands[1].state == C42_OBSERVER_COMMAND_LEASED &&
              observer.ready_poll_pending == 0 &&
              observer.commands[0].state ==
                  C42_OBSERVER_COMMAND_HIF_COMMITTED,
              "F18 same pass notes demand and leases later READY");
    }
    {
        struct c42_step_result result = {0};

        check(c42_step(fixture.controller, 1, &result) == C42_OK &&
              result.units_executed == 1 &&
              c42_observer_read_v2(
                  fixture.controller, &observer) == C42_OK &&
              observer.ready_poll_pending == 0 &&
              observer.commands[0].state ==
                  C42_OBSERVER_COMMAND_HIF_COMMITTED &&
              observer.commands[1].state == C42_OBSERVER_COMMAND_PUB_RESERVED,
              "F18 local consume remains ahead of provider poll");
    }
}

static C42_TEST_NOINLINE void test_ack_noncommitted_rejected(void)
{
    struct c42_test_fixture fixture;
    struct c42_observer_v2 observer;
    struct c42_cq_head_event ack = {0};
    struct c42_snapshot snapshot = {0};
    uint32_t step;

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa139000000000002)) &&
          c42_test_submit(&fixture, 0, 0, 1, 222),
          "F13 noncommitted ACK fixture");
    for (step = 0; step < 64; ++step) {
        struct c42_step_result result = {0};

        check(c42_step(fixture.controller, 1, &result) == C42_OK &&
              c42_observer_read_v2(
                  fixture.controller, &observer) == C42_OK,
              "F13 reach noncommitted slot");
        if (observer.cq[0].slots[0].state ==
                C42_OBSERVER_SLOT_RESERVED ||
            observer.cq[0].slots[0].state ==
                C42_OBSERVER_SLOT_BODY_STAGED) {
            break;
        }
    }
    ack.instance_nonce = fixture.config.instance_nonce;
    ack.controller_epoch = fixture.config.initial_controller_epoch;
    ack.ring_generation = fixture.cq_cap[0].ring_generation;
    ack.queue_id = 0;
    ack.new_head = 1;
    check(step < 64 &&
          c42_cq_head_event_apply(
              fixture.controller, &ack) == C42_INVALID &&
          c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.cq[0].host_index == 0 &&
          snapshot.cq[0].pending_or_unacked == 0,
          "F13 ACK cannot consume RESERVED/BODY slot");
}

static C42_TEST_NOINLINE void test_direct_memory_and_bool_outputs(void)
{
    struct c42_test_fixture fixture;
    struct c42_queue_memory_cap cap;
    struct c42_queue_descriptor descriptor;
    struct c42_operation_token token = {0};
    struct c42_control_status status = {0};
    struct c42_snapshot snapshot = {0};
    struct c42_fake_memory_outcome outcome = {0};
    struct c42_fake_command_script script = {0};

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa135000000000001)),
          "F13 direct validate fixture");
    cap = fresh_cap(
        &fixture, 1, C42_MEMORY_CQ_PUBLISH, 1, UINT64_C(0xa1351001)
    );
    descriptor = descriptor_for(&fixture, &cap, C42_QUEUE_CQ);
    outcome.operation = C42_FAKE_MEMORY_VALIDATE;
    outcome.effect = C42_MEMORY_RETIRED + 1u;
    check(c42_fake_memory_map(&fixture.memory, &cap, fixture.depth) == C42_OK &&
          c42_fake_memory_script_push(
              &fixture.memory, &outcome) == C42_OK &&
          c42_candidate_prepare(
              fixture.controller, &descriptor, &token) == C42_POISONED &&
          c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED,
          "F13 direct validate unknown result poisons");
    check(reset_to_cold(&fixture), "F13 direct validate reset drain");

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa135000000000002)),
          "F13 direct capture fixture");
    memset(&outcome, 0, sizeof(outcome));
    outcome.operation = C42_FAKE_MEMORY_CAPTURE;
    outcome.effect = C42_MEMORY_RETIRED + 1u;
    check(c42_fake_memory_script_push(
              &fixture.memory, &outcome) == C42_OK &&
          c42_test_submit(&fixture, 0, 0, 1, 181) &&
          c42_test_run(&fixture, 1, 1) &&
          c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED &&
          snapshot.sq[0].device_index == 0,
          "F13 direct capture unknown result cannot advance");
    check(reset_to_cold(&fixture), "F13 direct capture reset drain");

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa135000000000003)),
          "F13 command bool omission fixture");
    script.inject_operation = C42_FAKE_COMMAND_RESET_QUIESCENT;
    script.inject_result = FWLAB_HIF_PORT_OK;
    script.inject_count = 1;
    script.inject_omit_outputs = 1;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_reset_start(fixture.controller, &token) == C42_OK &&
          c42_control_progress(fixture.controller, &token, 3) == C42_OK &&
          c42_control_query(
              fixture.controller, &token, &status) == C42_OK &&
          status.state == C42_CONTROL_WAITING,
          "F13 omitted command bool cannot prove quiescence");
    check(c42_control_progress(fixture.controller, &token, 4) == C42_OK &&
          c42_control_query(
              fixture.controller, &token, &status) == C42_OK &&
          status.state == C42_CONTROL_COMMITTED,
          "F13 command bool omission recovers by same-key query");

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa135000000000004)),
          "F13 memory bool omission fixture");
    memset(&outcome, 0, sizeof(outcome));
    outcome.operation = C42_FAKE_MEMORY_RESET_QUIESCENT;
    outcome.effect = C42_MEMORY_OK;
    check(c42_fake_memory_script_push(
              &fixture.memory, &outcome) == C42_OK &&
          c42_reset_start(fixture.controller, &token) == C42_OK &&
          c42_control_progress(fixture.controller, &token, 3) == C42_OK &&
          c42_control_query(
              fixture.controller, &token, &status) == C42_OK &&
          status.state == C42_CONTROL_WAITING,
          "F13 omitted memory bool cannot prove quiescence");
    check(c42_control_progress(fixture.controller, &token, 4) == C42_OK &&
          c42_control_query(
              fixture.controller, &token, &status) == C42_OK &&
          status.state == C42_CONTROL_COMMITTED,
          "F13 memory bool omission recovers by same-key query");
}

static C42_TEST_NOINLINE void test_reset_exported_api_cuts(void)
{
    struct c42_test_fixture fixture;
    struct c42_queue_memory_cap cap;
    struct c42_queue_descriptor descriptor;
    struct c42_operation_token candidate = {0};
    struct c42_operation_token deletion = {0};
    struct c42_operation_token reset = {0};
    struct c42_candidate_status candidate_status = {0};
    struct c42_control_status control_status = {0};
    struct c42_snapshot snapshot = {0};
    struct c42_fake_memory_outcome unknown = {0};
    uint32_t scrub_calls;

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa140000000000001)),
          "F14 candidate/control fixture");
    cap = fresh_cap(
        &fixture, 1, C42_MEMORY_CQ_PUBLISH, 1, UINT64_C(0xa1401001)
    );
    descriptor = descriptor_for(&fixture, &cap, C42_QUEUE_CQ);
    unknown.operation = C42_FAKE_MEMORY_SCRUB;
    unknown.effect = C42_MEMORY_UNKNOWN;
    unknown.committed = 1;
    check(c42_fake_memory_map(&fixture.memory, &cap, fixture.depth) == C42_OK &&
          c42_fake_memory_script_push(&fixture.memory, &unknown) == C42_OK &&
          c42_candidate_prepare(
              fixture.controller, &descriptor, &candidate) == C42_OK &&
          c42_candidate_progress(
              fixture.controller, &candidate, 1) == C42_OK &&
          c42_delete_start(
              fixture.controller, C42_QUEUE_SQ, 0, &deletion) == C42_OK,
          "F14 create exported old-epoch records");
    scrub_calls = fixture.memory.scrub_call_count;
    check(c42_reset_start(fixture.controller, &reset) == C42_OK,
          "F14 reset linearization point");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.phase == C42_CONTROLLER_RESETTING &&
          snapshot.controller_epoch ==
              fixture.config.initial_controller_epoch + 1u,
          "F14 reset stays closed until provider quiescence");
    check(c42_candidate_query(
              fixture.controller, &candidate, &candidate_status) == C42_OK &&
          candidate_status.state == C42_CANDIDATE_SUPERSEDED &&
          c42_candidate_progress(
              fixture.controller, &candidate, 8) == C42_SUPERSEDED &&
          c42_candidate_commit(
              fixture.controller, &candidate) == C42_SUPERSEDED &&
          c42_candidate_abort(
              fixture.controller, &candidate) == C42_SUPERSEDED &&
          c42_candidate_retire(
              fixture.controller, &candidate) == C42_SUPERSEDED &&
          fixture.memory.scrub_call_count == scrub_calls,
          "F14 old candidate mutators are provider-no-effect");
    check(c42_control_query(
              fixture.controller, &deletion, &control_status) == C42_OK &&
          control_status.state == C42_CONTROL_SUPERSEDED &&
          c42_control_progress(
              fixture.controller, &deletion, 8) == C42_SUPERSEDED,
          "F14 old business control superseded");
    check(c42_control_progress(
              fixture.controller, &reset, 32) == C42_OK &&
          c42_candidate_query(
              fixture.controller, &candidate, &candidate_status) == C42_STALE &&
          c42_control_query(
              fixture.controller, &deletion, &control_status) == C42_STALE,
          "F14 reset ACK clears superseded records");

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa140000000000002)),
          "F14 teardown candidate fixture");
    cap = fresh_cap(
        &fixture, 1, C42_MEMORY_CQ_PUBLISH, 1, UINT64_C(0xa1401002)
    );
    descriptor = descriptor_for(&fixture, &cap, C42_QUEUE_CQ);
    check(c42_fake_memory_map(&fixture.memory, &cap, fixture.depth) == C42_OK &&
          c42_candidate_prepare(
              fixture.controller, &descriptor, &candidate) == C42_OK,
          "F14 teardown pending candidate");
    check(c42_teardown_start(fixture.controller, &reset) == C42_OK &&
          c42_candidate_query(
              fixture.controller, &candidate, &candidate_status) == C42_OK &&
          candidate_status.state == C42_CANDIDATE_SUPERSEDED &&
          c42_candidate_progress(
              fixture.controller, &candidate, 8) == C42_SUPERSEDED &&
          c42_candidate_abort(
              fixture.controller, &candidate) == C42_SUPERSEDED &&
          c42_control_progress(
              fixture.controller, &reset, 32) == C42_OK &&
          c42_candidate_query(
              fixture.controller, &candidate, &candidate_status) == C42_STALE,
          "F14 teardown supersedes then ACK-clears candidate");
}

static C42_TEST_NOINLINE void test_notification_and_raw_reset_cut(void)
{
    struct c42_test_fixture fixture;
    struct c42_notification notification = {0};
    struct c42_notification queried = {0};
    struct c42_operation_token reset = {0};

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa141000000000001)) &&
          c42_test_submit(&fixture, 0, 0, 1, 151) &&
          c42_test_run(&fixture, 64, 4) &&
          c42_notification_acquire(
              fixture.controller, &notification) == C42_OK,
          "F14 acquired notification fixture");
    check(c42_reset_start(fixture.controller, &reset) == C42_OK &&
          c42_notification_query(
              fixture.controller, &notification.token, &queried) == C42_OK &&
          queried.state == C42_NOTIFICATION_SUPPRESSED &&
          c42_notification_consume(
              fixture.controller, &notification.token) == C42_SUPERSEDED &&
          c42_notification_retire(
              fixture.controller, &notification.token) == C42_OK,
          "F14 reset suppresses acquired notification");
    check(c42_control_progress(
              fixture.controller, &reset, 32) == C42_OK,
          "F14 notification reset drain");

    {
        struct c42_fake_command_script script = {0};
        struct c42_target_ref target = {0};
        uint8_t raw[C42_SQE_BYTES];

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0, UINT64_C(0xa141000000000002)),
              "F14 raw target fixture");
        script.poll_delay = 100;
        c42_fake_command_set_script(&fixture.command, &script);
        check(c42_test_submit(&fixture, 0, 0, 1, 152) &&
              c42_test_run(&fixture, 16, 4) &&
              c42_target_prepare(
                  fixture.controller, 0, 1, 152, &target) == C42_OK &&
              c42_raw_snapshot_copy(
                  fixture.controller, &target.handle, &target.origin,
                  raw) == C42_OK,
              "F14 raw copy before reset");
        check(c42_reset_start(fixture.controller, &reset) == C42_OK &&
              c42_raw_snapshot_copy(
                  fixture.controller, &target.handle, &target.origin,
                  raw) == C42_SUPERSEDED &&
              c42_target_release(
                  fixture.controller, &target.token) == C42_STALE,
              "F14 reset closes raw and target APIs");
        check(c42_control_progress(
                  fixture.controller, &reset, 32) == C42_OK,
              "F14 raw reset drain");
    }
}

static int delete_queue(
    struct c42_test_fixture *fixture,
    uint8_t kind,
    uint16_t queue_id,
    struct c42_operation_token *token)
{
    struct c42_control_status status = {0};

    return c42_delete_start(
               fixture->controller, kind, queue_id, token) == C42_OK &&
           c42_control_progress(
               fixture->controller, token, 8) == C42_OK &&
           c42_control_query(
               fixture->controller, token, &status) == C42_OK &&
           status.state == C42_CONTROL_COMMITTED;
}

static C42_TEST_NOINLINE void test_sq_candidate_cq_delete(void)
{
    struct c42_test_fixture fixture;
    struct c42_queue_memory_cap replacement;
    struct c42_queue_descriptor descriptor;
    struct c42_operation_token sq_delete = {0};
    struct c42_operation_token cq_delete = {0};
    struct c42_operation_token candidate = {0};

    executions += 2;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 1, UINT64_C(0xa150000000000001)) &&
          delete_queue(&fixture, C42_QUEUE_SQ, 1, &sq_delete) &&
          c42_control_retire(
              fixture.controller, &sq_delete) == C42_OK,
          "F15 remove live SQ before replacement");
    replacement = fresh_cap(
        &fixture, 1, C42_MEMORY_SQ_READ, 2, UINT64_C(0xa1501001)
    );
    descriptor = descriptor_for(&fixture, &replacement, C42_QUEUE_SQ);
    check(c42_fake_memory_map(
              &fixture.memory, &replacement, fixture.depth) == C42_OK &&
          c42_candidate_prepare(
              fixture.controller, &descriptor, &candidate) == C42_OK &&
          c42_delete_start(
              fixture.controller, C42_QUEUE_CQ, 1,
              &cq_delete) == C42_WRONG_STATE,
          "F15 prepared SQ pins associated CQ");
    check(c42_candidate_commit(
              fixture.controller, &candidate) == C42_OK &&
          c42_test_candidate_retire(fixture.controller, &candidate),
          "F15 pinned candidate commits only against live exact CQ");

    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 1, UINT64_C(0xa150000000000002)) &&
          delete_queue(&fixture, C42_QUEUE_SQ, 1, &sq_delete) &&
          c42_control_retire(
              fixture.controller, &sq_delete) == C42_OK &&
          c42_delete_start(
              fixture.controller, C42_QUEUE_CQ, 1, &cq_delete) == C42_OK,
          "F15 CQ delete acquires ownership first");
    replacement = fresh_cap(
        &fixture, 1, C42_MEMORY_SQ_READ, 2, UINT64_C(0xa1501002)
    );
    descriptor = descriptor_for(&fixture, &replacement, C42_QUEUE_SQ);
    check(c42_fake_memory_map(
              &fixture.memory, &replacement, fixture.depth) == C42_OK &&
          c42_candidate_prepare(
              fixture.controller, &descriptor,
              &candidate) == C42_WRONG_STATE,
          "F15 quiescing CQ rejects new SQ candidate");
}

static C42_TEST_NOINLINE void test_scrub_retire_and_recreate(void)
{
    struct c42_test_fixture fixture;
    struct c42_queue_memory_cap cap;
    struct c42_queue_memory_cap replacement;
    struct c42_queue_descriptor descriptor;
    struct c42_operation_token candidate = {0};
    struct c42_operation_token old_candidate = {0};
    struct c42_operation_token deletion = {0};
    struct c42_candidate_status status = {0};
    struct c42_fake_memory_outcome scrub_wait = {0};
    struct c42_fake_memory_outcome scrub_done = {0};
    struct c42_fake_memory_outcome retire_unknown = {0};
    struct c42_fake_memory_outcome retire_done = {0};

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa160000000000001)),
          "F16 fixture");
    cap = fresh_cap(
        &fixture, 1, C42_MEMORY_CQ_PUBLISH, 1, UINT64_C(0xa1601001)
    );
    descriptor = descriptor_for(&fixture, &cap, C42_QUEUE_CQ);
    scrub_wait.operation = C42_FAKE_MEMORY_SCRUB;
    scrub_wait.effect = C42_MEMORY_FULL;
    scrub_wait.committed = 1;
    scrub_wait.status_override = 2;
    scrub_wait.status_quiescent = 0;
    scrub_done.operation = C42_FAKE_MEMORY_SCRUB;
    scrub_done.effect = C42_MEMORY_FULL;
    scrub_done.committed = 1;
    retire_unknown.operation = C42_FAKE_MEMORY_SCRUB_RETIRE;
    retire_unknown.effect = C42_MEMORY_UNKNOWN;
    retire_unknown.committed = 1;
    retire_done.operation = C42_FAKE_MEMORY_SCRUB_RETIRE;
    retire_done.effect = C42_MEMORY_RETIRED;
    retire_done.committed = 1;
    check(c42_fake_memory_map(&fixture.memory, &cap, fixture.depth) == C42_OK &&
          c42_fake_memory_script_push(
              &fixture.memory, &scrub_wait) == C42_OK &&
          c42_fake_memory_script_push(
              &fixture.memory, &scrub_done) == C42_OK &&
          c42_fake_memory_script_push(
              &fixture.memory, &retire_unknown) == C42_OK &&
          c42_fake_memory_script_push(
              &fixture.memory, &retire_done) == C42_OK &&
          c42_candidate_prepare(
              fixture.controller, &descriptor, &candidate) == C42_OK,
          "F16 scripted candidate");
    check(c42_candidate_progress(
              fixture.controller, &candidate, 1) == C42_OK &&
          c42_candidate_query(
              fixture.controller, &candidate, &status) == C42_OK &&
          status.state == C42_CANDIDATE_SCRUB_UNKNOWN,
          "F16 committed nonquiescent scrub remains private");
    check(c42_candidate_progress(
              fixture.controller, &candidate, 1) == C42_OK &&
          c42_candidate_commit(
              fixture.controller, &candidate) == C42_OK &&
          c42_candidate_query(
              fixture.controller, &candidate, &status) == C42_OK &&
          status.state == C42_CANDIDATE_COMMITTED_AWAIT_RETIRE,
          "F16 committed CQ retains scrub ownership");
    check(c42_delete_start(
              fixture.controller, C42_QUEUE_CQ, 1,
              &deletion) == C42_WRONG_STATE,
          "F16 CQ delete requires retired create scrub");
    check(c42_candidate_retire(
              fixture.controller, &candidate) == C42_IN_PROGRESS &&
          c42_candidate_query(
              fixture.controller, &candidate, &status) == C42_OK &&
          status.state == C42_CANDIDATE_RETIRE_UNKNOWN &&
          c42_candidate_retire(
              fixture.controller, &candidate) == C42_IN_PROGRESS &&
          c42_candidate_query(
              fixture.controller, &candidate, &status) == C42_OK &&
          status.state == C42_CANDIDATE_RETIRE_READY &&
          c42_candidate_retire(
              fixture.controller, &candidate) == C42_OK &&
          c42_candidate_retire(
              fixture.controller, &candidate) == C42_NO_EFFECT,
          "F16 scrub retire query, local ACK and response-loss retry");
    old_candidate = candidate;
    check(delete_queue(
              &fixture, C42_QUEUE_CQ, 1, &deletion),
          "F16 retired scrub permits CQ delete");
    replacement = fresh_cap(
        &fixture, 1, C42_MEMORY_CQ_PUBLISH, 2, UINT64_C(0xa1601002)
    );
    descriptor = descriptor_for(&fixture, &replacement, C42_QUEUE_CQ);
    check(c42_fake_memory_map(
              &fixture.memory, &replacement, fixture.depth) == C42_OK &&
          c42_candidate_prepare(
              fixture.controller, &descriptor, &candidate) == C42_OK &&
          c42_candidate_retire(
              fixture.controller, &old_candidate) == C42_NO_EFFECT &&
          c42_candidate_progress(
              fixture.controller, &candidate, 4) == C42_OK &&
          c42_candidate_commit(
              fixture.controller, &candidate) == C42_OK &&
          c42_test_candidate_retire(fixture.controller, &candidate),
          "F16 fresh generation recreates after provider retirement");
}

static C42_TEST_NOINLINE void test_scrub_abort_paths(void)
{
    struct c42_test_fixture fixture;
    struct c42_queue_memory_cap cap;
    struct c42_queue_descriptor descriptor;
    struct c42_operation_token candidate = {0};
    struct c42_candidate_status status = {0};
    struct c42_snapshot snapshot = {0};
    struct c42_fake_memory_outcome unknown = {0};
    struct c42_fake_memory_outcome retired = {0};

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa161000000000001)),
          "F16 abort-delay fixture");
    cap = fresh_cap(
        &fixture, 1, C42_MEMORY_CQ_PUBLISH, 1, UINT64_C(0xa1611001)
    );
    descriptor = descriptor_for(&fixture, &cap, C42_QUEUE_CQ);
    unknown.operation = C42_FAKE_MEMORY_SCRUB_ABORT;
    unknown.effect = C42_MEMORY_UNKNOWN;
    retired.operation = C42_FAKE_MEMORY_SCRUB_ABORT;
    retired.effect = C42_MEMORY_RETIRED;
    retired.committed = 1;
    check(c42_fake_memory_map(&fixture.memory, &cap, fixture.depth) == C42_OK &&
          c42_fake_memory_script_push(
              &fixture.memory, &unknown) == C42_OK &&
          c42_fake_memory_script_push(
              &fixture.memory, &retired) == C42_OK &&
          c42_candidate_prepare(
              fixture.controller, &descriptor, &candidate) == C42_OK &&
          c42_candidate_progress(
              fixture.controller, &candidate, 4) == C42_OK &&
          c42_candidate_abort(
              fixture.controller, &candidate) == C42_OK &&
          c42_candidate_progress(
              fixture.controller, &candidate, 1) == C42_OK &&
          c42_candidate_query(
              fixture.controller, &candidate, &status) == C42_OK &&
          status.state == C42_CANDIDATE_ABORTING &&
          c42_candidate_progress(
              fixture.controller, &candidate, 1) == C42_OK &&
          c42_candidate_query(
              fixture.controller, &candidate, &status) == C42_OK &&
          status.state == C42_CANDIDATE_ABORTED &&
          c42_candidate_retire(
              fixture.controller, &candidate) == C42_OK,
          "F16 abort remains owned until RETIRED/quiescent");

    {
        struct c42_fake_memory_direct_injection direct = {0};

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0, UINT64_C(0xa161000000000002)),
              "F16 malformed abort fixture");
        cap = fresh_cap(
            &fixture, 1, C42_MEMORY_CQ_PUBLISH, 1,
            UINT64_C(0xa1611002)
        );
        descriptor = descriptor_for(&fixture, &cap, C42_QUEUE_CQ);
        direct.operation = C42_FAKE_MEMORY_SCRUB_ABORT;
        direct.result = C42_MEMORY_UNKNOWN;
        direct.omit_status = 1;
        check(c42_fake_memory_map(
                  &fixture.memory, &cap, fixture.depth) == C42_OK &&
              c42_candidate_prepare(
                  fixture.controller, &descriptor, &candidate) == C42_OK &&
              c42_candidate_progress(
                  fixture.controller, &candidate, 4) == C42_OK &&
              c42_candidate_abort(
                  fixture.controller, &candidate) == C42_OK &&
              c42_fake_memory_direct_push(
                  &fixture.memory, &direct) == C42_OK &&
              c42_candidate_progress(
                  fixture.controller, &candidate, 1) == C42_POISONED &&
              c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
              snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED,
              "F16 direct UNKNOWN abort is provider poison");
        check(reset_to_cold(&fixture), "F16 malformed abort reset drain");
    }

    {
        struct c42_fake_memory_direct_injection direct = {0};

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0, UINT64_C(0xa161000000000003)),
              "F16 abort response-loss fixture");
        cap = fresh_cap(
            &fixture, 1, C42_MEMORY_CQ_PUBLISH, 1,
            UINT64_C(0xa1611003)
        );
        descriptor = descriptor_for(&fixture, &cap, C42_QUEUE_CQ);
        direct.operation = C42_FAKE_MEMORY_SCRUB_ABORT;
        direct.result = C42_MEMORY_OK;
        direct.write_status = 1;
        direct.apply_effect = 1;
        direct.logical_effect = C42_MEMORY_UNKNOWN;
        direct.applied_effect = C42_MEMORY_RETIRED;
        direct.committed = 1;
        check(c42_fake_memory_map(
                  &fixture.memory, &cap, fixture.depth) == C42_OK &&
              c42_candidate_prepare(
                  fixture.controller, &descriptor, &candidate) == C42_OK &&
              c42_candidate_progress(
                  fixture.controller, &candidate, 4) == C42_OK &&
              c42_candidate_abort(
                  fixture.controller, &candidate) == C42_OK &&
              c42_fake_memory_direct_push(
                  &fixture.memory, &direct) == C42_OK &&
              c42_candidate_progress(
                  fixture.controller, &candidate, 1) == C42_OK &&
              c42_candidate_query(
                  fixture.controller, &candidate, &status) == C42_OK &&
              status.state == C42_CANDIDATE_ABORTING &&
              c42_candidate_progress(
                  fixture.controller, &candidate, 1) == C42_OK &&
              c42_candidate_query(
                  fixture.controller, &candidate, &status) == C42_OK &&
              status.state == C42_CANDIDATE_ABORTED,
              "F16 abort response loss keeps same-token ownership");
    }
}

static C42_TEST_NOINLINE void test_scrub_retire_response_loss(void)
{
    struct c42_test_fixture fixture;
    struct c42_queue_memory_cap cap;
    struct c42_queue_descriptor descriptor;
    struct c42_operation_token candidate = {0};
    struct c42_candidate_status status = {0};
    struct c42_fake_memory_direct_injection direct = {0};

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa162000000000001)),
          "F16 retire response-loss fixture");
    cap = fresh_cap(
        &fixture, 1, C42_MEMORY_CQ_PUBLISH, 1, UINT64_C(0xa1621001)
    );
    descriptor = descriptor_for(&fixture, &cap, C42_QUEUE_CQ);
    direct.operation = C42_FAKE_MEMORY_SCRUB_RETIRE;
    direct.result = C42_MEMORY_OK;
    direct.write_status = 1;
    direct.apply_effect = 1;
    direct.logical_effect = C42_MEMORY_UNKNOWN;
    direct.applied_effect = C42_MEMORY_RETIRED;
    direct.committed = 1;
    check(c42_fake_memory_map(&fixture.memory, &cap, fixture.depth) == C42_OK &&
          c42_candidate_prepare(
              fixture.controller, &descriptor, &candidate) == C42_OK &&
          c42_candidate_progress(
              fixture.controller, &candidate, 4) == C42_OK &&
          c42_candidate_commit(
              fixture.controller, &candidate) == C42_OK &&
          c42_fake_memory_direct_push(
              &fixture.memory, &direct) == C42_OK &&
          c42_candidate_retire(
              fixture.controller, &candidate) == C42_IN_PROGRESS &&
          c42_candidate_query(
              fixture.controller, &candidate, &status) == C42_OK &&
          status.state == C42_CANDIDATE_RETIRE_UNKNOWN &&
          c42_candidate_retire(
              fixture.controller, &candidate) == C42_IN_PROGRESS &&
          c42_candidate_query(
              fixture.controller, &candidate, &status) == C42_OK &&
          status.state == C42_CANDIDATE_RETIRE_READY &&
          c42_candidate_retire(
              fixture.controller, &candidate) == C42_OK,
          "F16 retire response loss resolves through provider tombstone");
}

static uint16_t get_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static int bytes_are_zero(const void *value, size_t size)
{
    const uint8_t *bytes = value;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static C42_TEST_NOINLINE void test_v2_abi_and_observer_representation(void)
{
    struct c42_test_fixture fixture;
    union c42_test_arena arena;
    struct c42_config config;
    struct c42_providers providers;
    struct c42_memory_ops memory_ops;
    struct c42_controller *controller = NULL;
    struct c42_observer_v2 first;
    struct c42_observer_v2 second;

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa170000000000001)),
          "ABI v2 fixture");
    config = fixture.config;
    config.version = 1;
    check(c42_arena_size(&config) == 0, "ABI rejects component v1 config");
    config = fixture.config;
    config.size--;
    check(c42_arena_size(&config) == 0, "ABI rejects wrong config size");
    config = fixture.config;
    config.reserved[0] = 1;
    check(c42_arena_size(&config) == 0, "ABI rejects config reserved data");

    config = fixture.config;
    providers = fixture.providers;
    memory_ops = *providers.memory.ops;
    providers.memory.ops = &memory_ops;
    memory_ops.version = 1;
    check(c42_init(
              arena.bytes, sizeof(arena.bytes), &config, &providers,
              &controller) == C42_INVALID,
          "ABI rejects memory port v1");
    memory_ops = *fixture.providers.memory.ops;
    memory_ops.size--;
    check(c42_init(
              arena.bytes, sizeof(arena.bytes), &config, &providers,
              &controller) == C42_INVALID,
          "ABI rejects wrong memory ops size");
    memory_ops = *fixture.providers.memory.ops;
    memory_ops.reserved = 1;
    check(c42_init(
              arena.bytes, sizeof(arena.bytes), &config, &providers,
              &controller) == C42_INVALID,
          "ABI rejects memory ops reserved data");

    memset(&first, 0xa5, sizeof(first));
    memset(&second, 0x5a, sizeof(second));
    check(c42_observer_read_v2(fixture.controller, &first) == C42_OK &&
          c42_observer_read_v2(fixture.controller, &second) == C42_OK &&
          memcmp(&first, &second, sizeof(first)) == 0 &&
          bytes_are_zero(first.reserved, sizeof(first.reserved)) &&
          bytes_are_zero(first.reserved0, sizeof(first.reserved0)),
          "ABI observer object representation is deterministic and reserved-zero");
}

static C42_TEST_NOINLINE void test_reserve_sqhd_and_ready_fairness(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_snapshot snapshot = {0};
    struct c42_observer_v2 observer;
    struct c42_cq_head_event ack = {0};
    uint8_t cqe[C42_CQE_BYTES];
    uint32_t step;
    int found_a = 0;
    uint32_t acquire_before;

    executions++;
    check(c42_test_fixture_init_with_nonce(
               &fixture, 4, 0, UINT64_C(0xa178000000000001)),
          "F17/F18 fixture");
    check(c42_test_submit(&fixture, 0, 0, 1, 160) &&
          c42_test_submit(&fixture, 0, 1, 2, 161) &&
          c42_test_submit(&fixture, 0, 2, 3, 162) &&
          c42_test_run(&fixture, 256, 4) &&
          c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.cq[0].pending_or_unacked == 3,
          "F18 establish full CQ before READY scan");
    acquire_before = fixture.command.acquire_count;
    check(c42_test_submit(&fixture, 0, 3, 0, 171) &&
          c42_test_submit(&fixture, 0, 0, 1, 172),
          "F17/F18 submit two commands while CQ is full");
    for (step = 0; step < 128; ++step) {
        struct c42_step_result result = {0};
        uint16_t index;
        uint8_t ready = 0;

        check(c42_step(fixture.controller, 1, &result) == C42_OK &&
              c42_observer_read_v2(
                  fixture.controller, &observer) == C42_OK,
              "F18 reach two READY records behind full CQ");
        for (index = 0; index < observer.command_capacity; ++index) {
            if (observer.commands[index].state ==
                    C42_OBSERVER_COMMAND_READY &&
                (observer.commands[index].command_id == 171 ||
                 observer.commands[index].command_id == 172)) {
                ready++;
            }
        }
        if (ready == 2) break;
    }
    check(step < 128 && fixture.command.acquire_count == acquire_before,
          "F18 full CQ permits polling but no completion lease");

    ack.instance_nonce = fixture.config.instance_nonce;
    ack.controller_epoch = fixture.config.initial_controller_epoch;
    ack.ring_generation = fixture.cq_cap[0].ring_generation;
    ack.queue_id = 0;
    ack.new_head = 1;
    check(c42_cq_head_event_apply(
              fixture.controller, &ack) == C42_OK,
          "F18 open one CQ slot");
    script.inject_operation = C42_FAKE_COMMAND_CONSUME_PREPARE;
    script.inject_result = FWLAB_HIF_PORT_IN_PROGRESS;
    script.inject_count = 1000;
    script.inject_omit_outputs = 1;
    c42_fake_command_set_script(&fixture.command, &script);
    for (step = 0; step < 128; ++step) {
        check(c42_test_run(&fixture, 1, 1),
              "F17/F18 bounded one-unit progress");
        check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK,
              "F17/F18 snapshot");
        if (snapshot.sq[0].device_index == 1 &&
            fixture.command.acquire_count == acquire_before + 2u) {
            break;
        }
    }
    if (step == 128 ||
        fixture.command.acquire_count != acquire_before + 2u) {
        fprintf(stderr,
            "F18 debug: step=%u acquire=%u sqhd=%u phase=%u inject_left=%u\n",
            step, fixture.command.acquire_count,
            snapshot.sq[0].device_index, snapshot.phase,
            fixture.command.script.inject_count);
    }
    check(step < 128 &&
          fixture.command.acquire_count == acquire_before + 2u,
          "F18 permanent consume delay cannot starve later READY lease");
    memset(&script, 0, sizeof(script));
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_test_run(&fixture, 128, 4),
          "F17 release consume delay and publish one available slot");
    memset(&observer, 0xa5, sizeof(observer));
    check(c42_observer_read_v2(
              fixture.controller, &observer) == C42_OK &&
          observer.version == C42_OBSERVER_VERSION &&
          observer.size == sizeof(observer) &&
          observer.instance_nonce == fixture.config.instance_nonce &&
          observer.command_capacity == fixture.config.command_capacity &&
          observer.cq[0].ring_generation == 1 &&
          observer.cq[0].unacked_count == 3,
          "F17 observer v2 normalized state");
    for (step = 0; step < fixture.depth; ++step) {
        check(c42_fake_memory_read_cqe(
                  &fixture.memory, 0, (uint16_t)step, cqe) == C42_OK,
              "F17 read CQE");
        if (get_u16(cqe + 12) == 171) {
            found_a = 1;
            check(get_u16(cqe + 8) == 1,
                  "F17 SQHD sampled at actual slot reserve");
            check(observer.cq[0].slots[step].command_id == 171 &&
                  observer.cq[0].slots[step].submission_queue_head == 1 &&
                  memcmp(
                      observer.cq[0].slots[step].wire,
                      cqe, sizeof(cqe)) == 0,
                  "F17 observer slot matches provider-owned CQ bytes");
            break;
        }
    }
    check(found_a, "F17 delayed command CQE found");

    executions++;
    memset(&script, 0, sizeof(script));
    found_a = 0;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa178000000000002)),
          "F17 acquire-to-reserve delay fixture");
    script.inject_operation = C42_FAKE_COMMAND_CONSUME_PREPARE;
    script.inject_result = FWLAB_HIF_PORT_IN_PROGRESS;
    script.inject_count = 8;
    script.inject_omit_outputs = 1;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_test_submit(&fixture, 0, 0, 1, 173) &&
          c42_test_submit(&fixture, 0, 1, 2, 174) &&
          c42_test_run(&fixture, 512, 1) &&
          c42_observer_read_v2(
              fixture.controller, &observer) == C42_OK,
          "F17 advance second SQ head during first consume delay");
    for (step = 0; step < fixture.depth; ++step) {
        check(c42_fake_memory_read_cqe(
                  &fixture.memory, 0, (uint16_t)step, cqe) == C42_OK,
              "F17 read acquire-delay CQE");
        if (get_u16(cqe + 12) == 173) {
            found_a = 1;
            check(get_u16(cqe + 8) == 2 &&
                  observer.cq[0].slots[step].submission_queue_head == 2,
                  "F17 acquire snapshot cannot replace reserve-time SQHD");
            break;
        }
    }
    check(found_a, "F17 acquire-delay command CQE found");
}

int main(void)
{
    test_admission_closed_matrix();
    test_memory_status_matrix();
    test_completion_consume_closed_matrix();
    test_exact_object_malformed_states();
    test_ultra_critical_regressions();
    test_observer_state_mapping();
    test_observer_reachable_state_coverage();
    test_output_sentinels_and_poll();
    test_literal_single_pass_ready_scan();
    test_ack_noncommitted_rejected();
    test_direct_memory_and_bool_outputs();
    test_reset_exported_api_cuts();
    test_notification_and_raw_reset_cut();
    test_sq_candidate_cq_delete();
    test_scrub_retire_and_recreate();
    test_scrub_abort_paths();
    test_scrub_retire_response_loss();
    test_v2_abi_and_observer_representation();
    test_reserve_sqhd_and_ready_fairness();
    if (failures != 0) {
        return 1;
    }
    printf(
        "C4.2a targeted remediation: PASS (%u deterministic executions; F13-F18)\n",
        executions
    );
    return 0;
}
