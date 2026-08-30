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
        fprintf(stderr, "C4.2 identity FAIL: %s\n", name);
        failures++;
    }
}

static int handles_differ(
    const struct fwlab_nvme_command_handle *left,
    const struct fwlab_nvme_command_handle *right)
{
    return left->instance_nonce != right->instance_nonce ||
           left->command_uid != right->command_uid ||
           left->controller_epoch != right->controller_epoch ||
           left->generation != right->generation;
}

static int origins_differ(
    const struct fwlab_nvme_origin_token *left,
    const struct fwlab_nvme_origin_token *right)
{
    return left->word[0] != right->word[0] ||
           left->word[1] != right->word[1];
}

static void test_snapshot_backpressure_and_target(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_target_ref target = {0};
    struct c42_target_ref target_b = {0};
    struct c42_step_result step = {0};
    struct c41_raw_completion completion = {0};
    uint8_t original[C42_SQE_BYTES];
    uint8_t mutation[C42_SQE_BYTES];
    uint8_t copied[C42_SQE_BYTES];
    uint8_t cqe[C42_CQE_BYTES];

    check(c42_test_fixture_init(&fixture, 4, 0), "identity fixture");
    script.prepare_backpressure = 2;
    script.poll_delay = 100;
    c42_fake_command_set_script(&fixture.command, &script);
    c42_test_sqe(original, 0x02, 21, 1, 0x11111111);
    check(c42_fake_memory_write_sqe(
              &fixture.memory, 0, 0, original) == C42_OK,
          "write original");
    {
        struct c42_sq_tail_event event = {0};

        event.instance_nonce = fixture.config.instance_nonce;
        event.controller_epoch = fixture.config.initial_controller_epoch;
        event.ring_generation = fixture.sq_cap[0].ring_generation;
        event.queue_id = 0;
        event.new_tail = 1;
        check(c42_sq_tail_event_apply(fixture.controller, &event) == C42_OK,
              "tail original");
    }
    check(c42_step(fixture.controller, 1, &step) == C42_OK &&
          fixture.memory.capture_count == 1, "capture cut");
    c42_test_sqe(mutation, 0x01, 99, 2, 0x22222222);
    check(c42_fake_memory_write_sqe(
              &fixture.memory, 0, 0, mutation) == C42_OK,
          "mutate after capture");
    check(c42_test_run(&fixture, 32, 4), "admit after backpressure");
    check(fixture.memory.capture_count == 1, "no reread on backpressure");
    check(c42_target_prepare(
              fixture.controller, 0, fixture.sq_cap[0].ring_generation, 21,
              &target) == C42_OK,
          "target exact active CID");
    check(c42_target_prepare(
              fixture.controller, 0, fixture.sq_cap[0].ring_generation, 99,
              &target_b) == C42_NOT_FOUND,
          "mutated CID not admitted");
    check(c42_raw_snapshot_copy(
              fixture.controller, &target.handle, &target.origin, copied) ==
              C42_OK && memcmp(copied, original, sizeof(copied)) == 0,
          "raw seam returns immutable snapshot");
    copied[0] ^= 1u;
    check(c42_raw_snapshot_copy(
              fixture.controller, &target.handle, &target.origin, copied) ==
              C42_OK && memcmp(copied, original, sizeof(copied)) == 0,
          "raw seam returns a copy");
    script.poll_delay = 0;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_test_run(&fixture, 32, 4), "complete original");
    check(c42_fake_memory_read_cqe(&fixture.memory, 0, 0, cqe) == C42_OK &&
          c41_cqe_decode(cqe, sizeof(cqe), &completion) == C41_WIRE_OK &&
          completion.command_id == 21, "CQE preserves captured CID");
    check(c42_raw_snapshot_copy(
              fixture.controller, &target.handle, &target.origin, copied) ==
              C42_NOT_FOUND, "raw seam closes at cross commit");
    check(c42_target_release(fixture.controller, &target.token) == C42_OK,
          "target outlives active lookup");
}

static void test_cid_reuse_and_epoch_revoke(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_target_ref first = {0};
    struct c42_target_ref second = {0};
    struct c42_operation_token reset = {0};
    struct c42_control_status status = {0};
    struct c42_sq_tail_event stale = {0};

    check(c42_test_fixture_init(&fixture, 4, 0), "reuse fixture");
    script.poll_delay = 100;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_test_submit(&fixture, 0, 0, 1, 31), "submit first");
    check(c42_test_run(&fixture, 32, 4), "admit first");
    check(c42_target_prepare(
              fixture.controller, 0, fixture.sq_cap[0].ring_generation, 31,
              &first) == C42_OK,
          "target first");
    script.poll_delay = 0;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_test_run(&fixture, 32, 4), "complete first");
    check(c42_test_submit(&fixture, 0, 1, 2, 31),
          "reuse CID before first ACK");
    script.poll_delay = 100;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_test_run(&fixture, 32, 4), "admit second");
    check(c42_target_prepare(
              fixture.controller, 0, fixture.sq_cap[0].ring_generation, 31,
              &second) == C42_OK &&
          handles_differ(&first.handle, &second.handle) &&
          origins_differ(&first.origin, &second.origin),
          "CID reuse has fresh graph handle and origin");
    check(c42_target_release(fixture.controller, &first.token) == C42_OK,
          "release old pinned target");
    check(c42_reset_start(fixture.controller, &reset) == C42_OK,
          "reset starts epoch-first");
    check(c42_target_release(fixture.controller, &second.token) == C42_STALE,
          "reset revokes target ref");
    check(c42_control_progress(fixture.controller, &reset, 8) == C42_OK &&
          c42_control_query(fixture.controller, &reset, &status) == C42_OK &&
          status.state == C42_CONTROL_COMMITTED,
          "reset reaches cold no queues");
    stale.instance_nonce = fixture.config.instance_nonce;
    stale.controller_epoch = fixture.config.initial_controller_epoch;
    stale.ring_generation = fixture.sq_cap[0].ring_generation;
    stale.queue_id = 0;
    stale.new_tail = 3;
    check(c42_sq_tail_event_apply(fixture.controller, &stale) == C42_STALE,
          "old epoch tail is stale after reset");
}

int main(void)
{
    test_snapshot_backpressure_and_target();
    test_cid_reuse_and_epoch_revoke();
    if (failures != 0) {
        return 1;
    }
    printf("C4.2 identity unit: PASS (snapshot/origin/target/CID/epoch)\n");
    return 0;
}
