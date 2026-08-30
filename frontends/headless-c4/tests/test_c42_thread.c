/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_support.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

struct worker_context {
    struct c42_test_fixture *fixture;
    uint16_t command_id;
    int result;
};

static void *worker(void *argument)
{
    struct worker_context *context = argument;
    struct c42_snapshot snapshot = {0};
    struct c42_notification notification = {0};
    struct c42_cq_head_event ack = {0};

    context->result = 0;
    if (!c42_test_submit(
            context->fixture, 0, 0, 1, context->command_id) ||
        !c42_test_run(context->fixture, 32, 4) ||
        c42_snapshot_read(
            context->fixture->controller, &snapshot) != C42_OK ||
        snapshot.cq[0].pending_or_unacked != 1 ||
        c42_notification_acquire(
            context->fixture->controller, &notification) != C42_OK ||
        c42_notification_consume(
            context->fixture->controller, &notification.token) != C42_OK ||
        c42_notification_retire(
            context->fixture->controller, &notification.token) != C42_OK) {
        return NULL;
    }
    ack.instance_nonce = context->fixture->config.instance_nonce;
    ack.controller_epoch = context->fixture->config.initial_controller_epoch;
    ack.ring_generation = context->fixture->cq_cap[0].ring_generation;
    ack.queue_id = 0;
    ack.new_head = 1;
    if (c42_cq_head_event_apply(
            context->fixture->controller, &ack) != C42_OK) {
        return NULL;
    }
    context->result = 1;
    return NULL;
}

int main(void)
{
    struct c42_test_fixture left;
    struct c42_test_fixture right;
    uint32_t repeat;

    for (repeat = 0; repeat < 64; ++repeat) {
        struct worker_context left_context = {0};
        struct worker_context right_context = {0};
        pthread_t left_thread;
        pthread_t right_thread;

        if (!c42_test_fixture_init_with_nonce(
                &left, 4, 0,
                UINT64_C(0x8100000000000000) + repeat * 2u + 1u) ||
            !c42_test_fixture_init_with_nonce(
                &right, 4, 0,
                UINT64_C(0x8200000000000000) + repeat * 2u + 1u)) {
            return 1;
        }
        left_context.fixture = &left;
        left_context.command_id = (uint16_t)(repeat * 2u + 1u);
        right_context.fixture = &right;
        right_context.command_id = (uint16_t)(repeat * 2u + 2u);
        if (pthread_create(
                &left_thread, NULL, worker, &left_context) != 0) {
            return 1;
        }
        if (pthread_create(
                &right_thread, NULL, worker, &right_context) != 0) {
            (void)pthread_join(left_thread, NULL);
            return 1;
        }
        if (pthread_join(left_thread, NULL) != 0 ||
            pthread_join(right_thread, NULL) != 0 ||
            left_context.result == 0 || right_context.result == 0) {
            return 1;
        }
    }
    printf("C4.2 different-instance thread isolation: PASS repeats=64\n");
    return 0;
}
