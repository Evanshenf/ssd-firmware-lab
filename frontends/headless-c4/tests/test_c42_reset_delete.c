/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_support.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        fprintf(stderr, "C4.2 reset/delete FAIL: %s\n", name);
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

static int create_one(
    struct c42_test_fixture *fixture,
    const struct c42_queue_memory_cap *cap,
    uint8_t kind)
{
    struct c42_queue_descriptor descriptor = {0};
    struct c42_operation_token token = {0};
    struct c42_candidate_status status = {0};

    descriptor.version = C42_COMPONENT_VERSION;
    descriptor.size = sizeof(descriptor);
    descriptor.queue_id = cap->queue_id;
    descriptor.associated_cq_id = cap->queue_id;
    descriptor.depth = fixture->depth;
    descriptor.kind = kind;
    descriptor.queue_class = cap->queue_id == 0 ?
                             FWLAB_NVME_QUEUE_ADMIN : FWLAB_NVME_QUEUE_IO;
    descriptor.memory = *cap;
    if (c42_candidate_prepare(
            fixture->controller, &descriptor, &token) != C42_OK) {
        return 0;
    }
    if (kind == C42_QUEUE_CQ &&
        c42_candidate_progress(fixture->controller, &token, 4) != C42_OK) {
        return 0;
    }
    if (c42_candidate_query(
            fixture->controller, &token, &status) != C42_OK ||
        status.state != C42_CANDIDATE_READY ||
        c42_candidate_commit(fixture->controller, &token) != C42_OK ||
        c42_candidate_retire(fixture->controller, &token) != C42_OK) {
        return 0;
    }
    return 1;
}

static void test_prequiesce_and_tombstone(void)
{
    struct c42_test_fixture fixture;
    struct c42_operation_token deletion = {0};
    struct c42_operation_token cq_deletion = {0};
    struct c42_control_status status = {0};
    struct c42_snapshot snapshot = {0};
    struct c42_sq_tail_event closed_tail = {0};
    struct c42_cq_head_event ack;
    struct c42_queue_memory_cap replacement;
    uint8_t sqe0[C42_SQE_BYTES];
    uint8_t sqe1[C42_SQE_BYTES];

    check(c42_test_fixture_init(&fixture, 4, 0), "delete fixture");
    c42_test_sqe(sqe0, 0x02, 41, 1, 41);
    c42_test_sqe(sqe1, 0x02, 42, 1, 42);
    check(c42_fake_memory_write_sqe(
              &fixture.memory, 0, 0, sqe0) == C42_OK &&
          c42_fake_memory_write_sqe(
              &fixture.memory, 0, 1, sqe1) == C42_OK,
          "write doorbelled batch");
    closed_tail.instance_nonce = fixture.config.instance_nonce;
    closed_tail.controller_epoch = fixture.config.initial_controller_epoch;
    closed_tail.ring_generation = fixture.sq_cap[0].ring_generation;
    closed_tail.queue_id = 0;
    closed_tail.new_tail = 2;
    check(c42_sq_tail_event_apply(
              fixture.controller, &closed_tail) == C42_OK,
          "doorbell batch");
    check(c42_delete_start(
              fixture.controller, C42_QUEUE_SQ, 0, &deletion) == C42_OK,
          "delete SQ starts PREQUIESCE");
    closed_tail.new_tail = 3;
    check(c42_sq_tail_event_apply(
              fixture.controller, &closed_tail) == C42_WRONG_STATE,
          "PREQUIESCE closes new tail");
    check(c42_test_run(&fixture, 64, 4), "drain frozen tail");
    check(c42_control_query(
              fixture.controller, &deletion, &status) == C42_OK &&
          status.state == C42_CONTROL_COMMITTED,
          "delete commits after drain");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.sq[0].life == C42_QUEUE_TOMBSTONED &&
          snapshot.cq[0].pending_or_unacked == 2,
          "old SQ tombstoned behind CQEs");
    replacement = fixture.sq_cap[0];
    replacement.memory_uid++;
    replacement.ring_generation = 2;
    replacement.mapping_generation = 2;
    check(c42_fake_memory_map(
              &fixture.memory, &replacement, fixture.depth) == C42_OK,
          "map replacement SQ");
    check(!create_one(&fixture, &replacement, C42_QUEUE_SQ),
          "tombstone blocks same-QID recreate");
    ack = ack_event(&fixture, 2);
    check(c42_cq_head_event_apply(fixture.controller, &ack) == C42_OK,
          "ACK old generation CQEs");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.sq[0].life == C42_QUEUE_ABSENT,
          "last old ACK clears tombstone");
    check(create_one(&fixture, &replacement, C42_QUEUE_SQ),
          "fresh SQ generation after tombstone");
    check(c42_control_retire(fixture.controller, &deletion) == C42_OK,
          "retire first delete token");
    check(c42_delete_start(
              fixture.controller, C42_QUEUE_SQ, 0, &deletion) == C42_OK,
          "delete replacement SQ");
    check(c42_control_progress(
              fixture.controller, &deletion, 4) == C42_OK &&
          c42_control_query(
              fixture.controller, &deletion, &status) == C42_OK &&
          status.state == C42_CONTROL_COMMITTED,
          "replacement SQ absent");
    check(c42_delete_start(
              fixture.controller, C42_QUEUE_CQ, 0, &cq_deletion) == C42_OK,
          "delete empty unassociated CQ");
    check(c42_control_progress(
              fixture.controller, &cq_deletion, 2) == C42_OK &&
          c42_control_query(
              fixture.controller, &cq_deletion, &status) == C42_OK &&
          status.state == C42_CONTROL_COMMITTED,
          "CQ delete committed");
}

static void refresh_cap(
    struct c42_queue_memory_cap *cap,
    uint32_t controller_epoch,
    uint32_t generation)
{
    cap->memory_uid += UINT64_C(0x100000);
    cap->controller_epoch = controller_epoch;
    cap->ring_generation = generation;
    cap->mapping_generation = generation;
}

static void test_reset_cold_recreate_and_teardown(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_operation_token reset = {0};
    struct c42_operation_token teardown = {0};
    struct c42_control_status status = {0};
    struct c42_snapshot snapshot = {0};

    check(c42_test_fixture_init(&fixture, 4, 0), "reset fixture");
    script.prepare_delay = 8;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_test_submit(&fixture, 0, 0, 1, 51), "reset pending submit");
    check(c42_test_run(&fixture, 1, 2), "reach provider-owned prepare");
    check(c42_reset_start(fixture.controller, &reset) == C42_OK,
          "reset start");
    check(c42_control_progress(fixture.controller, &reset, 8) == C42_OK &&
          c42_control_query(fixture.controller, &reset, &status) == C42_OK &&
          status.state == C42_CONTROL_COMMITTED,
          "reset quiescent proof");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.phase == C42_CONTROLLER_COLD_NO_QUEUES &&
          snapshot.active_commands == 0 &&
          snapshot.sq[0].life == C42_QUEUE_ABSENT &&
          snapshot.cq[0].life == C42_QUEUE_ABSENT,
          "reset ends cold without implicit Admin queues");
    check(c42_enable(fixture.controller) == C42_WRONG_STATE,
          "reset cannot reopen before explicit create");
    refresh_cap(&fixture.cq_cap[0], 12, 2);
    refresh_cap(&fixture.sq_cap[0], 12, 2);
    check(c42_fake_memory_map(
              &fixture.memory, &fixture.cq_cap[0], fixture.depth) == C42_OK &&
          c42_fake_memory_map(
              &fixture.memory, &fixture.sq_cap[0], fixture.depth) == C42_OK,
          "map fresh Admin caps");
    check(create_one(&fixture, &fixture.cq_cap[0], C42_QUEUE_CQ) &&
          create_one(&fixture, &fixture.sq_cap[0], C42_QUEUE_SQ) &&
          c42_enable(fixture.controller) == C42_OK,
          "caller recreates and enables Admin queues");
    check(c42_teardown_start(fixture.controller, &teardown) == C42_OK,
          "teardown start");
    check(c42_control_progress(
              fixture.controller, &teardown, 8) == C42_OK &&
          c42_control_query(
              fixture.controller, &teardown, &status) == C42_OK &&
          status.state == C42_CONTROL_COMMITTED,
          "teardown quiescent proof");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.phase == C42_CONTROLLER_DEAD,
          "teardown reaches DEAD without destroying port context");
}

static void test_reset_every_executor_cut(void)
{
    struct c42_test_fixture fixture;
    uint32_t cut;

    for (cut = 0; cut <= 32; ++cut) {
        struct c42_operation_token reset = {0};
        struct c42_control_status status = {0};
        struct c42_snapshot snapshot = {0};
        uint32_t unit;

        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0x7000000000000000) + cut + 1u),
              "reset-cut fixture");
        check(c42_test_submit(&fixture, 0, 0, 1, (uint16_t)(200u + cut)),
              "reset-cut submit");
        for (unit = 0; unit < cut; ++unit) {
            struct c42_step_result step = {0};

            check(c42_step(fixture.controller, 1, &step) == C42_OK,
                  "reset-cut step");
        }
        check(c42_reset_start(fixture.controller, &reset) == C42_OK,
              "reset-cut start");
        check(c42_control_progress(fixture.controller, &reset, 8) == C42_OK &&
              c42_control_query(
                  fixture.controller, &reset, &status) == C42_OK &&
              status.state == C42_CONTROL_COMMITTED,
              "reset-cut quiescent");
        check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
              snapshot.phase == C42_CONTROLLER_COLD_NO_QUEUES &&
              snapshot.active_commands == 0 &&
              snapshot.pending_notifications == 0,
              "reset-cut clean cold state");
    }
}

static void test_teardown_supersedes_reset_and_epoch_exhaustion(void)
{
    struct c42_test_fixture fixture;
    struct c42_operation_token reset = {0};
    struct c42_operation_token teardown = {0};
    struct c42_control_status status = {0};
    struct c42_snapshot snapshot = {0};

    check(c42_test_fixture_init(&fixture, 4, 0), "supersede fixture");
    check(c42_reset_start(fixture.controller, &reset) == C42_OK,
          "supersede reset start");
    check(c42_teardown_start(fixture.controller, &teardown) == C42_OK,
          "teardown takes RESETTING ownership");
    check(c42_control_progress(
              fixture.controller, &teardown, 16) == C42_OK &&
          c42_control_query(
              fixture.controller, &teardown, &status) == C42_OK &&
          status.state == C42_CONTROL_COMMITTED,
          "teardown first starts old reset providers then quiesces instance");
    check(c42_control_query(
              fixture.controller, &reset, &status) == C42_STALE,
          "superseded reset record retired by teardown");
    check(c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
          snapshot.phase == C42_CONTROLLER_DEAD,
          "superseded reset ends DEAD");

    check(c42_test_fixture_init_with_identity(
              &fixture, 4, 0, UINT64_C(0x8f00000000000001), UINT32_MAX),
          "epoch exhausted fixture");
    check(c42_reset_start(fixture.controller, &reset) ==
              C42_COUNTER_EXHAUSTED,
          "reset fails closed at epoch exhaustion");
    check(c42_teardown_start(fixture.controller, &teardown) == C42_OK,
          "protected teardown survives epoch exhaustion");
    check(c42_control_progress(
              fixture.controller, &teardown, 8) == C42_OK &&
          c42_control_query(
              fixture.controller, &teardown, &status) == C42_OK &&
          status.state == C42_CONTROL_COMMITTED,
          "epoch exhausted teardown reaches quiescent DEAD");
}

int main(void)
{
    test_prequiesce_and_tombstone();
    test_reset_cold_recreate_and_teardown();
    test_reset_every_executor_cut();
    test_teardown_supersedes_reset_and_epoch_exhaustion();
    if (failures != 0) {
        return 1;
    }
    printf("C4.2 reset/delete unit: PASS (prequiesce/tombstone; 33 reset cuts)\n");
    return 0;
}
