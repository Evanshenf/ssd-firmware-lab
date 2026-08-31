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
        fprintf(stderr, "C4.2 queue FAIL: %s\n", name);
        failures++;
    }
}

static void test_end_to_end(void)
{
    struct c42_test_fixture fixture;
    struct c42_snapshot snapshot = {0};
    struct c42_notification notification = {0};
    struct c42_cq_head_event ack = {0};
    struct c41_raw_completion completion = {0};
    uint8_t cqe[C42_CQE_BYTES];

    check(c42_test_fixture_init(&fixture, 4, 0), "fixture init");
    check(c42_test_submit(&fixture, 0, 0, 1, 7), "submit");
    check(c42_test_run(&fixture, 32, 8), "run");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK,
          "snapshot");
    check(snapshot.sq[0].device_index == 1 &&
          snapshot.sq[0].pending_or_unacked == 0, "SQ head committed");
    check(snapshot.cq[0].device_index == 1 &&
          snapshot.cq[0].pending_or_unacked == 1 &&
          snapshot.cq[0].phase == 1, "CQ committed");
    check(snapshot.active_commands == 0, "CID released at cross commit");
    check(fixture.memory.capture_count == 1, "single capture");
    check(c42_fake_memory_read_cqe(&fixture.memory, 0, 0, cqe) == C42_OK,
          "read CQE");
    check(c41_cqe_decode(cqe, sizeof(cqe), &completion) == C41_WIRE_OK &&
          completion.command_id == 7 && completion.submission_queue_head == 1 &&
          completion.submission_queue_id == 0 && completion.phase == 1,
          "CQE identity and phase");
    check(c42_notification_acquire(
              fixture.controller, &notification) == C42_OK &&
          notification.state == C42_NOTIFICATION_ACQUIRED,
          "notification acquire");
    check(c42_notification_consume(
              fixture.controller, &notification.token) == C42_OK,
          "notification consume");
    check(c42_notification_retire(
              fixture.controller, &notification.token) == C42_OK,
          "notification retire");
    ack.instance_nonce = fixture.config.instance_nonce;
    ack.controller_epoch = fixture.config.initial_controller_epoch;
    ack.ring_generation = fixture.cq_cap[0].ring_generation;
    ack.queue_id = 0;
    ack.new_head = 1;
    check(c42_cq_head_event_apply(fixture.controller, &ack) == C42_OK,
          "ACK");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.cq[0].host_index == 1 &&
          snapshot.cq[0].pending_or_unacked == 0 &&
          snapshot.cq[0].phase == 1, "ACK does not toggle phase");
}

static void test_stale_and_same_tail(void)
{
    struct c42_test_fixture fixture;
    struct c42_snapshot before = {0};
    struct c42_snapshot after = {0};
    struct c42_sq_tail_event event = {0};

    check(c42_test_fixture_init(&fixture, 4, 0), "stale fixture");
    check(c42_snapshot_read(fixture.controller, &before) == C42_OK,
          "stale before");
    event.instance_nonce = fixture.config.instance_nonce;
    event.controller_epoch = fixture.config.initial_controller_epoch;
    event.ring_generation = 999;
    event.queue_id = 0;
    event.new_tail = 1;
    check(c42_sq_tail_event_apply(fixture.controller, &event) == C42_STALE,
          "stale tail");
    check(c42_snapshot_read(fixture.controller, &after) == C42_OK &&
          memcmp(&before, &after, sizeof(before)) == 0,
          "stale is bitwise no effect");
    event.ring_generation = fixture.sq_cap[0].ring_generation;
    event.new_tail = 0;
    check(c42_sq_tail_event_apply(fixture.controller, &event) == C42_NO_EFFECT,
          "same tail");
}

static void test_overrun_duplicate_and_capture_fault(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_sq_tail_event event = {0};
    uint8_t first[C42_SQE_BYTES];
    uint8_t second[C42_SQE_BYTES];

    check(c42_test_fixture_init(&fixture, 4, 0), "invalid fixture");
    event.instance_nonce = fixture.config.instance_nonce;
    event.controller_epoch = fixture.config.initial_controller_epoch;
    event.ring_generation = fixture.sq_cap[0].ring_generation;
    event.queue_id = 0;
    event.new_tail = 4;
    check(c42_sq_tail_event_apply(fixture.controller, &event) == C42_FAULTED,
          "out-of-range tail faults SQ");

    check(c42_test_fixture_init(&fixture, 4, 0), "overrun fixture");
    event.instance_nonce = fixture.config.instance_nonce;
    event.controller_epoch = fixture.config.initial_controller_epoch;
    event.ring_generation = fixture.sq_cap[0].ring_generation;
    event.queue_id = 0;
    event.new_tail = 2;
    check(c42_sq_tail_event_apply(fixture.controller, &event) == C42_OK,
          "pending two");
    event.new_tail = 0;
    check(c42_sq_tail_event_apply(fixture.controller, &event) == C42_FAULTED,
          "tail overrun faults instead of guessing");

    check(c42_test_fixture_init(&fixture, 4, 0), "duplicate fixture");
    script.poll_delay = 100;
    c42_fake_command_set_script(&fixture.command, &script);
    c42_test_sqe(first, 0x02, 77, 1, 1);
    c42_test_sqe(second, 0x02, 77, 1, 2);
    check(c42_fake_memory_write_sqe(&fixture.memory, 0, 0, first) == C42_OK &&
          c42_fake_memory_write_sqe(&fixture.memory, 0, 1, second) == C42_OK,
          "duplicate SQEs");
    event.instance_nonce = fixture.config.instance_nonce;
    event.controller_epoch = fixture.config.initial_controller_epoch;
    event.ring_generation = fixture.sq_cap[0].ring_generation;
    event.queue_id = 0;
    event.new_tail = 2;
    check(c42_sq_tail_event_apply(fixture.controller, &event) == C42_OK,
          "duplicate batch tail");
    check(c42_test_run(&fixture, 32, 4), "duplicate run");
    {
        struct c42_snapshot snapshot = {0};

        check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
              snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED &&
              snapshot.sq[0].life == C42_QUEUE_FAULTED_RESET_REQUIRED,
              "duplicate active CID is sticky reset-required");
    }

    check(c42_test_fixture_init(&fixture, 4, 0), "capture fault fixture");
    check(c42_test_submit(&fixture, 0, 0, 1, 88), "capture fault submit");
    {
        struct c42_queue_memory_cap replacement = fixture.sq_cap[0];

        replacement.memory_uid++;
        replacement.ring_generation++;
        replacement.mapping_generation++;
        check(c42_fake_memory_map(
                  &fixture.memory, &replacement, fixture.depth) == C42_OK,
              "revoke capture mapping");
    }
    check(c42_test_run(&fixture, 2, 1), "capture fault run");
    {
        struct c42_snapshot snapshot = {0};

        check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
              snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED,
              "queue-memory capture fault closes queue");
    }
}

static void drain_notifications(struct c42_test_fixture *fixture)
{
    for (;;) {
        struct c42_notification notification = {0};

        if (c42_notification_acquire(
                fixture->controller, &notification) != C42_OK) {
            break;
        }
        check(c42_notification_consume(
                  fixture->controller, &notification.token) == C42_OK,
              "boundary notification consume");
        check(c42_notification_retire(
                  fixture->controller, &notification.token) == C42_OK,
              "boundary notification retire");
    }
}

static void test_boundary(uint16_t depth)
{
    struct c42_test_fixture fixture;
    struct c42_snapshot snapshot = {0};
    struct c42_cq_head_event ack = {0};
    uint16_t index;
    uint32_t acquisitions;

    check(c42_test_fixture_init(&fixture, depth, 0), "boundary fixture");
    for (index = 0; index < (uint16_t)(depth - 1u); ++index) {
        check(c42_test_submit(
                  &fixture, 0, index,
                  (uint16_t)((index + 1u) % depth),
                  (uint16_t)(100u + index)), "boundary fill submit");
        check(c42_test_run(&fixture, 32, 4), "boundary fill run");
        drain_notifications(&fixture);
    }
    acquisitions = fixture.command.acquire_count;
    check(acquisitions == (uint32_t)(depth - 1u), "boundary CQ filled");
    index = (uint16_t)(depth - 1u);
    check(c42_test_submit(
              &fixture, 0, index, 0, (uint16_t)(100u + index)),
          "boundary extra submit");
    check(c42_test_run(&fixture, 32, 4), "boundary extra stalls");
    check(fixture.command.acquire_count == acquisitions,
          "boundary full before lease");
    ack.instance_nonce = fixture.config.instance_nonce;
    ack.controller_epoch = fixture.config.initial_controller_epoch;
    ack.ring_generation = fixture.cq_cap[0].ring_generation;
    ack.queue_id = 0;
    ack.new_head = 1;
    check(c42_cq_head_event_apply(fixture.controller, &ack) == C42_OK,
          "boundary ACK one");
    check(c42_test_run(&fixture, 32, 4), "boundary resume one");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          fixture.command.acquire_count == acquisitions + 1u &&
          snapshot.cq[0].device_index == 0 && snapshot.cq[0].phase == 0 &&
          snapshot.cq[0].pending_or_unacked == depth - 1u,
          "boundary wrap and capacity invariant");
}

static struct c42_queue_descriptor io_descriptor(
    const struct c42_test_fixture *fixture,
    uint8_t kind,
    const struct c42_queue_memory_cap *capability)
{
    struct c42_queue_descriptor descriptor = {0};

    descriptor.version = C42_COMPONENT_VERSION;
    descriptor.size = sizeof(descriptor);
    descriptor.queue_id = 1;
    descriptor.associated_cq_id = 1;
    descriptor.depth = fixture->depth;
    descriptor.kind = kind;
    descriptor.queue_class = FWLAB_NVME_QUEUE_IO;
    descriptor.memory = *capability;
    return descriptor;
}

static void test_candidate_contract_and_scrub_unknown(void)
{
    struct c42_test_fixture fixture;
    struct c42_queue_memory_cap cq_cap = {0};
    struct c42_queue_memory_cap sq_cap = {0};
    struct c42_queue_descriptor descriptor;
    struct c42_operation_token token = {0};
    struct c42_candidate_status status = {0};
    struct c42_snapshot before = {0};
    struct c42_snapshot after = {0};
    const struct c42_fake_memory_outcome unknown = {
        C42_FAKE_MEMORY_SCRUB, C42_MEMORY_UNKNOWN, 0, 1, 0, 0, 0, 0
    };
    const struct c42_fake_memory_outcome full = {
        C42_FAKE_MEMORY_SCRUB, C42_MEMORY_FULL, 4, 1, 0, 0, 0, 0
    };

    check(c42_test_fixture_init(&fixture, 4, 0), "candidate fixture");
    cq_cap.instance_nonce = fixture.config.instance_nonce;
    cq_cap.owner_epoch = fixture.config.owner_epoch;
    cq_cap.memory_uid = UINT64_C(0x99110001);
    cq_cap.controller_epoch = fixture.config.initial_controller_epoch;
    cq_cap.ring_generation = 1;
    cq_cap.mapping_generation = 1;
    cq_cap.exact_bytes = fixture.depth * C42_CQE_BYTES;
    cq_cap.queue_id = 1;
    cq_cap.role = C42_MEMORY_CQ_PUBLISH;
    descriptor = io_descriptor(&fixture, C42_QUEUE_CQ, &cq_cap);
    check(c42_snapshot_read(fixture.controller, &before) == C42_OK,
          "candidate before malformed");
    descriptor.depth = 1;
    check(c42_candidate_prepare(
              fixture.controller, &descriptor, &token) == C42_INVALID,
          "candidate rejects depth one");
    descriptor = io_descriptor(&fixture, C42_QUEUE_SQ, &cq_cap);
    check(c42_candidate_prepare(
              fixture.controller, &descriptor, &token) == C42_INVALID,
          "candidate rejects CQ cap as SQ cap");
    check(c42_snapshot_read(fixture.controller, &after) == C42_OK &&
          memcmp(&before, &after, sizeof(before)) == 0,
          "malformed candidates are bitwise no effect");
    check(c42_fake_memory_map(&fixture.memory, &cq_cap, fixture.depth) == C42_OK,
          "map IO CQ");
    descriptor = io_descriptor(&fixture, C42_QUEUE_CQ, &cq_cap);
    check(c42_fake_memory_script_push(
              &fixture.memory, &unknown) == C42_OK &&
          c42_fake_memory_script_push(&fixture.memory, &full) == C42_OK,
          "candidate scrub script");
    check(c42_candidate_prepare(
              fixture.controller, &descriptor, &token) == C42_OK,
          "candidate prepare IO CQ");
    check(c42_candidate_query(
              fixture.controller, &token, &status) == C42_OK &&
          status.state == C42_CANDIDATE_PREPARED,
          "CQ candidate remains private before scrub proof");
    check(c42_candidate_commit(
              fixture.controller, &token) == C42_WRONG_STATE &&
          c42_candidate_query(
              fixture.controller, &token, &status) == C42_OK &&
          status.state == C42_CANDIDATE_PREPARED &&
          c42_snapshot_read(fixture.controller, &after) == C42_OK &&
          after.cq[1].life == C42_QUEUE_PREPARED,
          "CQ commit before scrub proof is bitwise state preserving");
    check(c42_candidate_progress(fixture.controller, &token, 1) == C42_OK &&
          c42_candidate_query(
              fixture.controller, &token, &status) == C42_OK &&
          status.state == C42_CANDIDATE_SCRUB_UNKNOWN,
          "unknown scrub stays queryable candidate");
    check(c42_snapshot_read(fixture.controller, &before) == C42_OK &&
          before.cq[1].life == C42_QUEUE_PREPARED &&
          c42_candidate_commit(
              fixture.controller, &token) == C42_WRONG_STATE &&
          c42_candidate_query(
              fixture.controller, &token, &status) == C42_OK &&
          status.state == C42_CANDIDATE_SCRUB_UNKNOWN &&
          c42_snapshot_read(fixture.controller, &after) == C42_OK &&
          memcmp(&before, &after, sizeof(before)) == 0,
          "unknown scrub commit rejection is bitwise state preserving");
    check(c42_candidate_progress(fixture.controller, &token, 1) == C42_OK &&
          c42_candidate_query(
              fixture.controller, &token, &status) == C42_OK &&
          status.state == C42_CANDIDATE_READY &&
          c42_candidate_commit(fixture.controller, &token) == C42_OK &&
          c42_test_candidate_retire(fixture.controller, &token),
          "scrub query proof permits CQ commit");
    sq_cap = cq_cap;
    sq_cap.memory_uid++;
    sq_cap.exact_bytes = fixture.depth * C42_SQE_BYTES;
    sq_cap.role = C42_MEMORY_SQ_READ;
    check(c42_fake_memory_map(&fixture.memory, &sq_cap, fixture.depth) == C42_OK,
          "map IO SQ");
    descriptor = io_descriptor(&fixture, C42_QUEUE_SQ, &sq_cap);
    descriptor.associated_cq_id = 0;
    check(c42_candidate_prepare(
              fixture.controller, &descriptor, &token) == C42_INVALID,
          "fixed profile rejects shared CQ association");
    descriptor.associated_cq_id = 1;
    check(c42_candidate_prepare(
              fixture.controller, &descriptor, &token) == C42_OK &&
          c42_candidate_commit(fixture.controller, &token) == C42_OK &&
          c42_test_candidate_retire(fixture.controller, &token),
          "one-to-one IO SQ commit");
}

static void test_candidate_abort_after_unknown_scrub(void)
{
    struct c42_test_fixture fixture;
    struct c42_queue_memory_cap cap = {0};
    struct c42_queue_descriptor descriptor;
    struct c42_operation_token token = {0};
    struct c42_candidate_status status = {0};
    struct c42_snapshot snapshot = {0};
    const struct c42_fake_memory_outcome unknown = {
        C42_FAKE_MEMORY_SCRUB, C42_MEMORY_UNKNOWN, 0, 1, 0, 0, 0, 0
    };

    check(c42_test_fixture_init(&fixture, 4, 0), "abort candidate fixture");
    cap.instance_nonce = fixture.config.instance_nonce;
    cap.owner_epoch = fixture.config.owner_epoch;
    cap.memory_uid = UINT64_C(0x99220001);
    cap.controller_epoch = fixture.config.initial_controller_epoch;
    cap.ring_generation = 1;
    cap.mapping_generation = 1;
    cap.exact_bytes = fixture.depth * C42_CQE_BYTES;
    cap.queue_id = 1;
    cap.role = C42_MEMORY_CQ_PUBLISH;
    descriptor = io_descriptor(&fixture, C42_QUEUE_CQ, &cap);
    check(c42_fake_memory_map(&fixture.memory, &cap, fixture.depth) == C42_OK &&
          c42_fake_memory_script_push(&fixture.memory, &unknown) == C42_OK,
          "abort candidate map/script");
    check(c42_candidate_prepare(
              fixture.controller, &descriptor, &token) == C42_OK &&
          c42_candidate_progress(fixture.controller, &token, 1) == C42_OK &&
          c42_candidate_abort(fixture.controller, &token) == C42_OK &&
          c42_candidate_progress(fixture.controller, &token, 2) == C42_OK &&
          c42_candidate_query(
              fixture.controller, &token, &status) == C42_OK &&
          status.state == C42_CANDIDATE_ABORTED,
          "unknown scrub abort reconciles same token");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.cq[1].life == C42_QUEUE_ABSENT,
          "aborted scrub candidate never becomes LIVE");
    check(c42_candidate_retire(fixture.controller, &token) == C42_OK,
          "retire aborted candidate");
}

static void test_active_generation_exhaustion_fails_closed(void)
{
    struct c42_test_fixture fixture;
    struct c42_cq_head_event ack = {0};
    struct c42_snapshot snapshot = {0};

    check(c42_test_fixture_init_profile(
              &fixture, 4, 0, UINT64_C(0x9933000000000001), 11,
              UINT32_MAX - 1u),
          "counter fixture");
    check(c42_test_submit(&fixture, 0, 0, 1, 91), "counter first submit");
    check(c42_test_run(&fixture, 32, 4), "counter first run");
    drain_notifications(&fixture);
    ack.instance_nonce = fixture.config.instance_nonce;
    ack.controller_epoch = fixture.config.initial_controller_epoch;
    ack.ring_generation = fixture.cq_cap[0].ring_generation;
    ack.queue_id = 0;
    ack.new_head = 1;
    check(c42_cq_head_event_apply(fixture.controller, &ack) == C42_OK,
          "counter first ACK");
    check(c42_test_submit(&fixture, 0, 1, 2, 92), "counter second tail");
    check(c42_test_run(&fixture, 4, 1), "counter exhaustion step");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED &&
          fixture.memory.capture_count == 1,
          "active generation exhaustion faults before a second capture");
}

int main(void)
{
    test_end_to_end();
    test_stale_and_same_tail();
    test_overrun_duplicate_and_capture_fault();
    test_boundary(3);
    test_boundary(4);
    test_boundary(32);
    test_candidate_contract_and_scrub_unknown();
    test_candidate_abort_after_unknown_scrub();
    test_active_generation_exhaustion_fails_closed();
    if (failures != 0) {
        return 1;
    }
    printf("C4.2 queue unit: PASS (capture/admit/faults; depth 3/4/32)\n");
    return 0;
}
