/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_support.h"

#include <stdio.h>
#include <string.h>

#include "c41_wire.h"

static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "C4.2 publication FAIL: %s\n", name);
        failures++;
    }
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

static void test_marker_reconcile_ack_and_cid_reuse(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script command_script = {0};
    const struct c42_fake_memory_outcome outcomes[] = {
        {C42_FAKE_MEMORY_BODY, C42_MEMORY_EXACT_PREFIX, 7, 0, 0, 0, 0, 0},
        {C42_FAKE_MEMORY_BODY, C42_MEMORY_FULL, 15, 1, 0, 0, 0, 0},
        {C42_FAKE_MEMORY_MARKER, C42_MEMORY_UNKNOWN, 0, 1, 0, 0, 0, 0},
        {C42_FAKE_MEMORY_MARKER, C42_MEMORY_FULL, 1, 1, 0, 0, 0, 0},
    };
    struct c42_snapshot snapshot = {0};
    struct c42_cq_head_event ack;
    struct c42_target_ref target = {0};
    uint8_t cqe[C42_CQE_BYTES];
    uint32_t index;

    check(c42_test_fixture_init(&fixture, 4, 0), "reconcile fixture");
    command_script.completion_result = UINT32_C(0x44332211);
    command_script.consume_commit_delay = 3;
    c42_fake_command_set_script(&fixture.command, &command_script);
    for (index = 0; index < sizeof(outcomes) / sizeof(outcomes[0]); ++index) {
        check(c42_fake_memory_script_push(
                  &fixture.memory, &outcomes[index]) == C42_OK,
              "push memory script");
    }
    check(c42_test_submit(&fixture, 0, 0, 1, 9), "submit A");
    for (index = 0; index < 32 && fixture.memory.body_call_count == 0; ++index) {
        check(c42_test_run(&fixture, 1, 1), "step to body prefix");
    }
    check(c42_fake_memory_read_cqe(&fixture.memory, 0, 0, cqe) == C42_OK,
          "read body prefix");
    check(cqe[0] == 0x11 && cqe[1] == 0x22 && cqe[2] == 0x33 &&
          cqe[3] == 0x44 && cqe[14] == 0,
          "body prefix excludes marker");
    for (index = 0; index < 32 && fixture.memory.marker_call_count == 0;
         ++index) {
        check(c42_test_run(&fixture, 1, 1), "step to marker unknown");
    }
    check(c42_fake_memory_read_cqe(&fixture.memory, 0, 0, cqe) == C42_OK &&
          (cqe[14] & 1u) == 1u, "physical marker visible");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.active_commands == 1 &&
          snapshot.cq[0].pending_or_unacked == 0 &&
          snapshot.pending_notifications == 0,
          "marker visible is not cross committed");
    check(c42_target_prepare(
              fixture.controller, 0, fixture.sq_cap[0].ring_generation, 9,
              &target) == C42_TOO_LATE,
          "target closes at marker reconcile");
    check(c42_test_submit(&fixture, 0, 1, 2, 9),
          "same CID tail accepted while reconcile");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.sq[0].host_index == 2 && snapshot.sq[0].device_index == 1 &&
          snapshot.sq[0].pending_or_unacked == 1,
          "same CID capture paused");
    ack = ack_event(&fixture, 1);
    check(c42_cq_head_event_apply(fixture.controller, &ack) == C42_OK,
          "ACK latched before cross commit");
    check(c42_test_run(&fixture, 64, 4), "finish reconcile and command B");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.sq[0].device_index == 2 &&
          snapshot.sq[0].pending_or_unacked == 0 &&
          snapshot.cq[0].host_index == 1 &&
          snapshot.cq[0].device_index == 2 &&
          snapshot.cq[0].pending_or_unacked == 1,
          "cross commit applies ACK then admits B");
    check(fixture.memory.capture_count == 2, "exactly one capture per command");
}

static void test_cq_full_before_lease(void)
{
    struct c42_test_fixture fixture;
    struct c42_snapshot snapshot = {0};
    struct c42_cq_head_event ack;
    uint8_t cqe[C42_CQE_BYTES];

    check(c42_test_fixture_init(&fixture, 2, 0), "full fixture");
    check(c42_test_submit(&fixture, 0, 0, 1, 10), "full submit A");
    check(c42_test_run(&fixture, 32, 4), "full run A");
    check(fixture.command.acquire_count == 1, "one lease A");
    check(c42_test_submit(&fixture, 0, 1, 0, 11), "full submit B");
    check(c42_test_run(&fixture, 32, 4), "full stall B");
    check(fixture.command.acquire_count == 1,
          "CQ full checked before completion acquire");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.active_commands == 1 &&
          snapshot.cq[0].pending_or_unacked == 1,
          "ready command held while CQ full");
    ack = ack_event(&fixture, 1);
    check(c42_cq_head_event_apply(fixture.controller, &ack) == C42_OK,
          "full ACK A");
    check(c42_test_run(&fixture, 32, 4), "full resume B");
    check(fixture.command.acquire_count == 2, "lease B after ACK");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.cq[0].device_index == 0 && snapshot.cq[0].phase == 0 &&
          snapshot.cq[0].pending_or_unacked == 1,
          "device tail wrap toggles phase");
    check(c42_fake_memory_read_cqe(&fixture.memory, 0, 1, cqe) == C42_OK &&
          (cqe[14] & 1u) == 1u, "B used pre-wrap phase");
}

static void test_invalid_over_ack_no_partial_effect(void)
{
    struct c42_test_fixture fixture;
    struct c42_snapshot before = {0};
    struct c42_snapshot after = {0};
    struct c42_cq_head_event ack;

    check(c42_test_fixture_init(&fixture, 4, 0), "over ACK fixture");
    check(c42_test_submit(&fixture, 0, 0, 1, 12), "over ACK submit");
    check(c42_test_run(&fixture, 32, 4), "over ACK run");
    check(c42_snapshot_read(fixture.controller, &before) == C42_OK,
          "over ACK before");
    ack = ack_event(&fixture, 2);
    check(c42_cq_head_event_apply(fixture.controller, &ack) == C42_INVALID,
          "over ACK rejected");
    check(c42_snapshot_read(fixture.controller, &after) == C42_OK &&
          memcmp(&before, &after, sizeof(before)) == 0,
          "over ACK is atomic no effect");
}

static void test_acquire_without_query_token_fails_closed(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_snapshot snapshot = {0};

    check(c42_test_fixture_init(&fixture, 4, 0), "acquire fixture");
    script.acquire_in_progress = 1;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_test_submit(&fixture, 0, 0, 1, 13), "acquire submit");
    check(c42_test_run(&fixture, 32, 4), "acquire run");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED &&
          fixture.command.acquire_count == 0,
          "unqueryable acquire IN_PROGRESS is contract poison, not blind retry");
}

static void test_delayed_out_of_order_sqhd_sampling(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_sq_tail_event tail = {0};
    struct c41_raw_completion first = {0};
    struct c41_raw_completion second = {0};
    struct c42_snapshot snapshot = {0};
    uint8_t sqe_a[C42_SQE_BYTES];
    uint8_t sqe_b[C42_SQE_BYTES];
    uint8_t cqe_a[C42_CQE_BYTES];
    uint8_t cqe_b[C42_CQE_BYTES];
    uint32_t step;

    check(c42_test_fixture_init(&fixture, 4, 0), "out-of-order fixture");
    script.poll_delay = 100;
    c42_fake_command_set_script(&fixture.command, &script);
    c42_test_sqe(sqe_a, 0x02, 61, 1, 61);
    c42_test_sqe(sqe_b, 0x02, 62, 1, 62);
    check(c42_fake_memory_write_sqe(
              &fixture.memory, 0, 0, sqe_a) == C42_OK &&
          c42_fake_memory_write_sqe(
              &fixture.memory, 0, 1, sqe_b) == C42_OK,
          "out-of-order batch write");
    tail.instance_nonce = fixture.config.instance_nonce;
    tail.controller_epoch = fixture.config.initial_controller_epoch;
    tail.ring_generation = fixture.sq_cap[0].ring_generation;
    tail.queue_id = 0;
    tail.new_tail = 2;
    check(c42_sq_tail_event_apply(fixture.controller, &tail) == C42_OK,
          "out-of-order batch tail");
    for (step = 0; step < 64; ++step) {
        struct c42_step_result result = {0};

        check(c42_step(fixture.controller, 1, &result) == C42_OK,
              "admit both bounded step");
        check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK,
              "admit both bounded snapshot");
        if (snapshot.active_commands == 2 &&
            snapshot.sq[0].device_index == 2) {
            break;
        }
    }
    check(step < 64 && snapshot.active_commands == 2 &&
          snapshot.sq[0].device_index == 2,
          "both commands active at SQ head two");
    script.poll_delay = 0;
    script.reverse_ready = 1;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_test_run(&fixture, 64, 4), "reverse ready completion order");
    check(c42_fake_memory_read_cqe(
              &fixture.memory, 0, 0, cqe_a) == C42_OK &&
          c42_fake_memory_read_cqe(
              &fixture.memory, 0, 1, cqe_b) == C42_OK &&
          c41_cqe_decode(cqe_a, sizeof(cqe_a), &first) == C41_WIRE_OK &&
          c41_cqe_decode(cqe_b, sizeof(cqe_b), &second) == C41_WIRE_OK,
          "decode reversed CQEs");
    check(first.command_id == 62 && second.command_id == 61,
          "port ready order is preserved by CQ publication");
    check(first.submission_queue_head == 2 &&
          second.submission_queue_head == 2,
          "SQHD sampled at each reserve, not at capture");
}

static void test_cumulative_ack_coalesces_during_marker_reconcile(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    const struct c42_fake_memory_outcome marker_unknown = {
        C42_FAKE_MEMORY_MARKER, C42_MEMORY_UNKNOWN, 0, 1, 0, 0, 0, 0
    };
    const struct c42_fake_memory_outcome marker_full = {
        C42_FAKE_MEMORY_MARKER, C42_MEMORY_FULL, 1, 1, 0, 0, 0, 0
    };
    struct c42_cq_head_event ack_one;
    struct c42_cq_head_event ack_two;
    struct c42_snapshot snapshot = {0};
    uint32_t marker_calls;
    uint32_t step;

    check(c42_test_fixture_init(&fixture, 4, 0), "coalesce fixture");
    check(c42_test_submit(&fixture, 0, 0, 1, 71), "coalesce submit A");
    check(c42_test_run(&fixture, 32, 4), "coalesce commit A");
    marker_calls = fixture.memory.marker_call_count;
    script.consume_commit_delay = 3;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_fake_memory_script_push(
              &fixture.memory, &marker_unknown) == C42_OK &&
          c42_fake_memory_script_push(
              &fixture.memory, &marker_full) == C42_OK,
          "coalesce marker script");
    check(c42_test_submit(&fixture, 0, 1, 2, 72), "coalesce submit B");
    for (step = 0; step < 32 &&
         fixture.memory.marker_call_count == marker_calls; ++step) {
        check(c42_test_run(&fixture, 1, 1), "coalesce step marker");
    }
    ack_one = ack_event(&fixture, 1);
    ack_two = ack_event(&fixture, 2);
    check(c42_cq_head_event_apply(fixture.controller, &ack_one) == C42_OK,
          "latch committed-prefix ACK");
    check(c42_cq_head_event_apply(fixture.controller, &ack_two) == C42_OK,
          "coalesce later cumulative ACK including marker");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.cq[0].host_index == 0 &&
          snapshot.cq[0].pending_or_unacked == 1,
          "latched ACK does not free before cross commit");
    check(c42_test_run(&fixture, 32, 4), "coalesce cross commit");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.cq[0].host_index == 2 &&
          snapshot.cq[0].pending_or_unacked == 0,
          "latest cumulative ACK applies atomically after cross commit");
}

static void test_marker_partial_poison_and_cleanup_independence(void)
{
    struct c42_test_fixture fixture;
    const struct c42_fake_memory_outcome partial_marker = {
        C42_FAKE_MEMORY_MARKER, C42_MEMORY_EXACT_PREFIX, 1, 0, 0, 0, 0, 0
    };
    struct c42_fake_command_script script = {0};
    struct c42_snapshot snapshot = {0};
    struct c42_notification notification = {0};
    struct c42_cq_head_event ack;
    uint32_t step;

    check(c42_test_fixture_init(&fixture, 4, 0), "partial marker fixture");
    check(c42_fake_memory_script_push(
              &fixture.memory, &partial_marker) == C42_OK,
          "partial marker script");
    check(c42_test_submit(&fixture, 0, 0, 1, 81), "partial marker submit");
    check(c42_test_run(&fixture, 32, 4), "partial marker run");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED &&
          snapshot.cq[0].pending_or_unacked == 0 &&
          snapshot.pending_notifications == 0,
          "partial marker is poison, never prefix continuation");

    check(c42_test_fixture_init(&fixture, 4, 0), "cleanup fixture");
    script.cleanup_pending = 1;
    script.cleanup_delay = 100;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_test_submit(&fixture, 0, 0, 1, 82), "cleanup submit");
    for (step = 0; step < 32; ++step) {
        struct c42_step_result result = {0};

        check(c42_step(fixture.controller, 1, &result) == C42_OK,
              "cleanup cross step");
        check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK,
              "cleanup cross snapshot");
        if (snapshot.cq[0].pending_or_unacked == 1) {
            break;
        }
    }
    check(step < 32 && snapshot.active_commands == 0 &&
          snapshot.pending_notifications == 1 &&
          fixture.command.records[0].retired == 0,
          "business commit releases CID/notify before cleanup retire");
    check(c42_test_submit(&fixture, 0, 1, 2, 83),
          "independent command while old cleanup pending");
    for (step = 0; step < 64; ++step) {
        struct c42_step_result result = {0};

        check(c42_step(fixture.controller, 1, &result) == C42_OK,
              "cleanup round-robin step");
        check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK,
              "cleanup round-robin snapshot");
        if (snapshot.cq[0].pending_or_unacked == 2) {
            break;
        }
    }
    check(step < 64 && fixture.command.records[0].retired == 0,
          "cleanup ledger cannot starve unrelated admission/publication");
    script.cleanup_delay = 3;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_notification_acquire(
              fixture.controller, &notification) == C42_OK &&
          c42_notification_consume(
              fixture.controller, &notification.token) == C42_OK &&
          c42_notification_retire(
              fixture.controller, &notification.token) == C42_OK,
          "notification independent of cleanup ledger");
    ack = ack_event(&fixture, 1);
    check(c42_cq_head_event_apply(fixture.controller, &ack) == C42_OK,
          "Host ACK independent of cleanup ledger");
    check(c42_test_run(&fixture, 64, 2), "cleanup ledger retire");
    check(fixture.command.records[0].retired != 0,
          "opaque command cleanup eventually retired");
}

int main(void)
{
    test_marker_reconcile_ack_and_cid_reuse();
    test_cq_full_before_lease();
    test_invalid_over_ack_no_partial_effect();
    test_acquire_without_query_token_fails_closed();
    test_delayed_out_of_order_sqhd_sampling();
    test_cumulative_ack_coalesces_during_marker_reconcile();
    test_marker_partial_poison_and_cleanup_independence();
    if (failures != 0) {
        return 1;
    }
    printf("C4.2 publication unit: PASS (prefix/marker/reconcile/full/ACK)\n");
    return 0;
}
