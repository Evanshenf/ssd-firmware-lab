/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _POSIX_C_SOURCE 200809L

#include "c35_test_support.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C35_THREAD_REPETITIONS 64u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

struct thread_result {
    struct c35_trace trace;
    uint8_t raw[C35_RAW_PROJECTION_BYTES];
    int ok;
};

struct thread_argument {
    pthread_barrier_t *barrier;
    enum c35_lane lane;
    uint8_t workload;
    uint8_t actor;
    struct thread_result *result;
};

static void make_uuid(
    uint8_t uuid[16],
    enum c35_lane lane,
    uint8_t workload,
    uint8_t actor
)
{
    unsigned int index;

    for (index = 0; index < 16; ++index) {
        uuid[index] = (uint8_t)(0x35u + index * 13u);
    }
    uuid[0] ^= (uint8_t)lane;
    uuid[1] ^= workload;
    uuid[2] ^= actor;
}

static void make_payload(
    uint8_t payload[C35_ATOM_BYTES],
    uint8_t workload,
    uint8_t actor,
    uint8_t atom
)
{
    unsigned int index;

    for (index = 0; index < C35_ATOM_BYTES; ++index) {
        payload[index] = (uint8_t)(0x31u + 0x30u * workload +
                                   0x20u * actor + 0x10u * atom + index);
    }
}

static int expect(
    const struct c35_semantic_result *result,
    uint8_t atom,
    const uint8_t value[C35_ATOM_BYTES]
)
{
    return (result->present_mask & (uint8_t)(1u << atom)) != 0 &&
           memcmp(result->payload[atom], value, C35_ATOM_BYTES) == 0;
}

static int workload_run(
    struct c35_runtime *runtime,
    uint8_t workload,
    uint8_t actor
)
{
    struct c35_request request;
    struct c35_semantic_result result;
    uint8_t value0[C35_ATOM_BYTES];
    uint8_t value1[C35_ATOM_BYTES];

    make_payload(value0, workload, actor, 0);
    make_payload(value1, workload, actor, 1);
    request = c35_request_write(
        0, FWLAB_PERSIST_SELF_DURABLE, 1, value0);
    if (!c35_run_command(runtime, &request, &result)) return 0;
    if (workload == 0 || actor == 0) {
        request = workload == 0 ? c35_request_read(0) :
            c35_request_write(1, FWLAB_PERSIST_DEFAULT, 2, value1);
        if (!c35_run_command(runtime, &request, &result) ||
            (workload == 0 && !expect(&result, 0, value0))) return 0;
        if (workload == 0) {
            request = c35_request_write(
                1, FWLAB_PERSIST_DEFAULT, 2, value1);
            if (!c35_run_command(runtime, &request, &result)) return 0;
        } else {
            request = c35_request_read(1);
            if (!c35_run_command(runtime, &request, &result) ||
                !expect(&result, 1, value1)) return 0;
        }
        if (c35_headless_pump_quiescent(
                &runtime->headless, 8192) != C35_OK) return 0;
        request = c35_request_fence(2);
        if (!c35_run_command(runtime, &request, &result)) return 0;
        request = c35_request_read(1);
        return c35_run_command(runtime, &request, &result) &&
               expect(&result, 1, value1);
    }
    request = c35_request_read(0);
    if (!c35_run_command(runtime, &request, &result) ||
        !expect(&result, 0, value0)) return 0;
    request = c35_request_trim(0, FWLAB_PERSIST_SELF_DURABLE, 2);
    if (!c35_run_command(runtime, &request, &result)) return 0;
    request = c35_request_read(0);
    if (!c35_run_command(runtime, &request, &result) ||
        result.present_mask != 0) return 0;
    request = c35_request_write(
        1, FWLAB_PERSIST_SELF_DURABLE, 3, value1);
    if (!c35_run_command(runtime, &request, &result)) return 0;
    request = c35_request_read(1);
    return c35_run_command(runtime, &request, &result) &&
           expect(&result, 1, value1);
}

static int execute_instance(
    enum c35_lane lane,
    uint8_t workload,
    uint8_t actor,
    pthread_barrier_t *barrier,
    struct thread_result *result
)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    uint8_t uuid[16];
    uint64_t nonce = UINT64_C(0x3570000000000000) |
                     ((uint64_t)workload << 16) |
                     ((uint64_t)actor << 8) | (uint64_t)lane | 1u;
    int wait_result = 0;
    int ok = 0;

    memset(result, 0, sizeof(*result));
    if (barrier != NULL) {
        wait_result = pthread_barrier_wait(barrier);
        if (wait_result != 0 && wait_result != PTHREAD_BARRIER_SERIAL_THREAD)
            goto out;
    }
    if (storage == NULL || runtime == NULL) goto out;
    make_uuid(uuid, lane, workload, actor);
    if (!c35_storage_init(storage, lane, uuid) ||
        !c35_runtime_init(
            runtime, storage, lane, nonce,
            UINT64_C(0x9b6d3e7a4c2158f1) ^ nonce, 1, actor,
            0x3570u + workload)) goto out;
    if (!workload_run(runtime, workload, actor) ||
        !c35_runtime_projection(runtime, result->raw)) goto out;
    result->trace = runtime->trace;
    if (!c35_runtime_teardown(runtime) || !c35_storage_close(storage))
        goto out;
    ok = 1;
out:
    if (!ok && runtime != NULL && runtime->lifecycle != NULL &&
        !runtime->headless.teardown_complete) {
        (void)c35_runtime_teardown(runtime);
    }
    if (storage != NULL && storage->initialized && !storage->bundle.claimed)
        (void)c35_storage_close(storage);
    free(runtime);
    free(storage);
    result->ok = ok;
    return ok;
}

static void *thread_main(void *opaque)
{
    struct thread_argument *argument = opaque;

    (void)execute_instance(
        argument->lane, argument->workload, argument->actor,
        argument->barrier, argument->result);
    return NULL;
}

static int result_equal(
    const struct thread_result *left,
    const struct thread_result *right
)
{
    return left->ok && right->ok &&
           c35_trace_equal(&left->trace, &right->trace) &&
           memcmp(left->raw, right->raw, sizeof(left->raw)) == 0;
}

int main(void)
{
    static const enum c35_lane pairs[3][2] = {
        {C35_LANE_MEMORY, C35_LANE_MEMORY},
        {C35_LANE_BYTE, C35_LANE_BYTE},
        {C35_LANE_MEMORY, C35_LANE_BYTE},
    };
    struct thread_result reference[2][2][2];
    unsigned int lane;
    unsigned int workload;
    unsigned int actor;
    unsigned int pair;
    unsigned int iteration;
    unsigned int runs = 0;

    for (lane = 0; lane < 2; ++lane) {
        for (workload = 0; workload < 2; ++workload) {
            for (actor = 0; actor < 2; ++actor) {
                CHECK(execute_instance(
                    lane == 0 ? C35_LANE_MEMORY : C35_LANE_BYTE,
                    (uint8_t)workload, (uint8_t)actor, NULL,
                    &reference[lane][workload][actor]));
            }
        }
    }
    for (pair = 0; pair < 3; ++pair) {
        for (iteration = 0; iteration < C35_THREAD_REPETITIONS;
             ++iteration) {
            pthread_barrier_t barrier;
            pthread_t threads[2];
            struct thread_argument argument[2];
            struct thread_result result[2];
            uint8_t selected = (uint8_t)(iteration & 1u);

            CHECK(pthread_barrier_init(&barrier, NULL, 2) == 0);
            for (actor = 0; actor < 2; ++actor) {
                argument[actor].barrier = &barrier;
                argument[actor].lane = pairs[pair][actor];
                argument[actor].workload = selected;
                argument[actor].actor = (uint8_t)actor;
                argument[actor].result = &result[actor];
                CHECK(pthread_create(
                    &threads[actor], NULL, thread_main,
                    &argument[actor]) == 0);
            }
            for (actor = 0; actor < 2; ++actor) {
                unsigned int lane_index = pairs[pair][actor] ==
                    C35_LANE_MEMORY ? 0u : 1u;

                CHECK(pthread_join(threads[actor], NULL) == 0);
                CHECK(result_equal(
                    &result[actor],
                    &reference[lane_index][selected][actor]));
            }
            CHECK(pthread_barrier_destroy(&barrier) == 0);
            ++runs;
        }
    }
    CHECK(runs == 192);
    puts("C3.5 pthread isolation: PASS (192 barrier-started twin runs; "
         "same-instance calls remain caller-serialized)");
    return 0;
}
