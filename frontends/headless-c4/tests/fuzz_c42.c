/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_support.h"

#include <inttypes.h>
#include <stdio.h>

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static uint64_t hash_value(uint64_t hash, uint64_t value, uint8_t bytes)
{
    uint8_t index;

    for (index = 0; index < bytes; ++index) {
        hash ^= (uint8_t)value;
        hash *= UINT64_C(1099511628211);
        value >>= 8;
    }
    return hash;
}

static uint64_t hash_snapshot(
    uint64_t hash,
    const struct c42_snapshot *snapshot)
{
    hash = hash_value(hash, snapshot->controller_epoch, 4);
    hash = hash_value(hash, snapshot->instance_nonce, 8);
    hash = hash_value(hash, snapshot->phase, 4);
    hash = hash_value(hash, snapshot->active_commands, 4);
    hash = hash_value(hash, snapshot->sq[0].ring_generation, 4);
    hash = hash_value(hash, snapshot->sq[0].host_index, 2);
    hash = hash_value(hash, snapshot->sq[0].device_index, 2);
    hash = hash_value(hash, snapshot->cq[0].ring_generation, 4);
    hash = hash_value(hash, snapshot->cq[0].host_index, 2);
    hash = hash_value(hash, snapshot->cq[0].device_index, 2);
    hash = hash_value(hash, snapshot->cq[0].phase, 1);
    return hash;
}

static int fuzz_fail(uint32_t execution, uint32_t action, int line)
{
    fprintf(stderr, "C4.2 fuzz failure execution=%u action=%u line=%d\n",
            execution, action, line);
    return 1;
}

static int snapshot_valid(
    const struct c42_snapshot *snapshot,
    uint16_t depth)
{
    return snapshot->phase == C42_CONTROLLER_READY &&
           snapshot->sq[0].host_index < depth &&
           snapshot->sq[0].device_index < depth &&
           snapshot->sq[0].pending_or_unacked < depth &&
           snapshot->cq[0].host_index < depth &&
           snapshot->cq[0].device_index < depth &&
           snapshot->cq[0].pending_or_unacked < depth &&
           snapshot->cq[0].phase <= 1 && snapshot->active_commands <= 8;
}

static int drain_notification(struct c42_test_fixture *fixture)
{
    struct c42_notification notification = {0};
    enum c42_result result = c42_notification_acquire(
        fixture->controller, &notification
    );

    if (result == C42_NOT_FOUND) {
        return 1;
    }
    return result == C42_OK &&
           c42_notification_consume(
               fixture->controller, &notification.token) == C42_OK &&
           c42_notification_retire(
               fixture->controller, &notification.token) == C42_OK;
}

int main(void)
{
    struct c42_test_fixture fixture;
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t execution;
    uint32_t command_id = 1;

    for (execution = 0; execution < 64; ++execution) {
        uint16_t depth = (uint16_t)(2u + execution % 3u);
        uint32_t random = UINT32_C(0x9e3779b9) ^ execution;
        uint32_t action;

        if (!c42_test_fixture_init(&fixture, depth, (int)(execution & 1u))) {
            return fuzz_fail(execution, 0, __LINE__);
        }
        for (action = 0; action < 64; ++action) {
            struct c42_snapshot snapshot = {0};
            uint32_t choice = next_random(&random) % 5u;

            if (c42_snapshot_read(fixture.controller, &snapshot) != C42_OK ||
                !snapshot_valid(&snapshot, depth)) {
                return fuzz_fail(execution, action, __LINE__);
            }
            if (choice <= 1 &&
                snapshot.sq[0].pending_or_unacked < depth - 1u) {
                uint16_t slot = snapshot.sq[0].host_index;
                uint16_t tail = (uint16_t)((slot + 1u) % depth);

                if (!c42_test_submit(
                        &fixture, 0, slot, tail,
                        (uint16_t)command_id)) {
                    return fuzz_fail(execution, action, __LINE__);
                }
                command_id++;
            } else if (choice == 2 &&
                       snapshot.cq[0].pending_or_unacked != 0) {
                struct c42_cq_head_event ack = {0};

                ack.instance_nonce = fixture.config.instance_nonce;
                ack.controller_epoch = fixture.config.initial_controller_epoch;
                ack.ring_generation = fixture.cq_cap[0].ring_generation;
                ack.queue_id = 0;
                ack.new_head = (uint16_t)(
                    (snapshot.cq[0].host_index + 1u) % depth
                );
                {
                    enum c42_result ack_result = c42_cq_head_event_apply(
                        fixture.controller, &ack
                    );

                    if (ack_result != C42_OK && ack_result != C42_NO_EFFECT) {
                        return fuzz_fail(execution, action, __LINE__);
                    }
                }
            } else if (!c42_test_run(
                           &fixture, 1, 1u + next_random(&random) % 4u)) {
                return fuzz_fail(execution, action, __LINE__);
            }
            if (!drain_notification(&fixture)) {
                return fuzz_fail(execution, action, __LINE__);
            }
        }
        for (action = 0; action < 256; ++action) {
            struct c42_snapshot snapshot = {0};

            if (!c42_test_run(&fixture, 1, 4)) {
                return fuzz_fail(execution, action, __LINE__);
            }
            if (c42_snapshot_read(
                    fixture.controller, &snapshot) != C42_OK) {
                return fuzz_fail(execution, action, __LINE__);
            }
            if (!snapshot_valid(&snapshot, depth)) {
                fprintf(stderr,
                        "snapshot phase=%u cause=%u sq=%u/%u/%u cq=%u/%u/%u "
                        "p=%u active=%u events=%u overflow=%u\n",
                        snapshot.phase, snapshot.fault_cause,
                        snapshot.sq[0].host_index,
                        snapshot.sq[0].device_index,
                        snapshot.sq[0].pending_or_unacked,
                        snapshot.cq[0].host_index,
                        snapshot.cq[0].device_index,
                        snapshot.cq[0].pending_or_unacked,
                        snapshot.cq[0].phase, snapshot.active_commands,
                        fixture.event_log.count, fixture.event_log.overflow);
                return fuzz_fail(execution, action, __LINE__);
            }
            if (!drain_notification(&fixture)) {
                return fuzz_fail(execution, action, __LINE__);
            }
            if (snapshot.cq[0].pending_or_unacked != 0) {
                struct c42_cq_head_event ack = {0};

                ack.instance_nonce = fixture.config.instance_nonce;
                ack.controller_epoch = fixture.config.initial_controller_epoch;
                ack.ring_generation = fixture.cq_cap[0].ring_generation;
                ack.queue_id = 0;
                ack.new_head = (uint16_t)(
                    (snapshot.cq[0].host_index + 1u) % depth
                );
                {
                    enum c42_result ack_result = c42_cq_head_event_apply(
                        fixture.controller, &ack
                    );

                    if (ack_result != C42_OK && ack_result != C42_NO_EFFECT) {
                        return fuzz_fail(execution, action, __LINE__);
                    }
                }
            }
            if (snapshot.active_commands == 0 &&
                snapshot.sq[0].pending_or_unacked == 0 &&
                snapshot.cq[0].pending_or_unacked == 0) {
                hash = hash_snapshot(hash, &snapshot);
                break;
            }
        }
        if (action == 256) {
            return fuzz_fail(execution, action, __LINE__);
        }
    }
    printf("C4.2 deterministic fuzz: PASS executions=64 actions=4096 "
           "hash=%016" PRIx64 "\n", hash);
    return 0;
}
