/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_support.h"

#include <stdio.h>
#include <string.h>

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

static void test_admission_closed_matrix(void)
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

static void test_memory_status_matrix(void)
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

static void test_completion_consume_closed_matrix(void)
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

static void test_direct_memory_and_bool_outputs(void)
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

static void test_reset_exported_api_cuts(void)
{
    struct c42_test_fixture fixture;
    struct c42_queue_memory_cap cap;
    struct c42_queue_descriptor descriptor;
    struct c42_operation_token candidate = {0};
    struct c42_operation_token deletion = {0};
    struct c42_operation_token reset = {0};
    struct c42_candidate_status candidate_status = {0};
    struct c42_control_status control_status = {0};
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

static void test_notification_and_raw_reset_cut(void)
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

static void test_sq_candidate_cq_delete(void)
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

static void test_scrub_retire_and_recreate(void)
{
    struct c42_test_fixture fixture;
    struct c42_queue_memory_cap cap;
    struct c42_queue_memory_cap replacement;
    struct c42_queue_descriptor descriptor;
    struct c42_operation_token candidate = {0};
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
          c42_candidate_progress(
              fixture.controller, &candidate, 4) == C42_OK &&
          c42_candidate_commit(
              fixture.controller, &candidate) == C42_OK &&
          c42_test_candidate_retire(fixture.controller, &candidate),
          "F16 fresh generation recreates after provider retirement");
}

static uint16_t get_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static void test_reserve_sqhd_and_ready_fairness(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_snapshot snapshot = {0};
    struct c42_observer_v2 observer;
    uint8_t cqe[C42_CQE_BYTES];
    uint32_t step;
    int found_a = 0;

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0xa178000000000001)),
          "F17/F18 fixture");
    script.inject_operation = C42_FAKE_COMMAND_CONSUME_PREPARE;
    script.inject_result = FWLAB_HIF_PORT_IN_PROGRESS;
    script.inject_count = 128;
    script.inject_omit_outputs = 1;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_test_submit(&fixture, 0, 0, 1, 171) &&
          c42_test_submit(&fixture, 0, 1, 2, 172),
          "F17/F18 submit two same-SQ commands");
    for (step = 0; step < 128; ++step) {
        check(c42_test_run(&fixture, 1, 1),
              "F17/F18 bounded one-unit progress");
        check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK,
              "F17/F18 snapshot");
        if (snapshot.sq[0].device_index == 2 &&
            fixture.command.acquire_count == 2) {
            break;
        }
    }
    if (step == 128 || fixture.command.acquire_count != 2) {
        fprintf(stderr,
                "F18 debug: step=%u acquire=%u sqhd=%u phase=%u inject_left=%u\n",
                step, fixture.command.acquire_count,
                snapshot.sq[0].device_index, snapshot.phase,
                fixture.command.script.inject_count);
    }
    check(step < 128 && fixture.command.acquire_count == 2,
          "F18 permanent consume delay cannot starve second READY lease");
    memset(&script, 0, sizeof(script));
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_test_run(&fixture, 128, 4),
          "F17 release consume delay and publish");
    memset(&observer, 0xa5, sizeof(observer));
    check(c42_observer_read_v2(
              fixture.controller, &observer) == C42_OK &&
          observer.version == C42_OBSERVER_VERSION &&
          observer.size == sizeof(observer) &&
          observer.instance_nonce == fixture.config.instance_nonce &&
          observer.command_capacity == fixture.config.command_capacity &&
          observer.cq[0].ring_generation == 1 &&
          observer.cq[0].unacked_count == 2,
          "F17 observer v2 normalized state");
    for (step = 0; step < fixture.depth; ++step) {
        check(c42_fake_memory_read_cqe(
                  &fixture.memory, 0, (uint16_t)step, cqe) == C42_OK,
              "F17 read CQE");
        if (get_u16(cqe + 12) == 171) {
            found_a = 1;
            check(get_u16(cqe + 8) == 2,
                  "F17 SQHD sampled at actual slot reserve");
            check(observer.cq[0].slots[step].command_id == 171 &&
                  observer.cq[0].slots[step].submission_queue_head == 2 &&
                  memcmp(
                      observer.cq[0].slots[step].wire,
                      cqe, sizeof(cqe)) == 0,
                  "F17 observer slot matches provider-owned CQ bytes");
            break;
        }
    }
    check(found_a, "F17 delayed command CQE found");
}

int main(void)
{
    test_admission_closed_matrix();
    test_memory_status_matrix();
    test_completion_consume_closed_matrix();
    test_direct_memory_and_bool_outputs();
    test_reset_exported_api_cuts();
    test_notification_and_raw_reset_cut();
    test_sq_candidate_cq_delete();
    test_scrub_retire_and_recreate();
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
