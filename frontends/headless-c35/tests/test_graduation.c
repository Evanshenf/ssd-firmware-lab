/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_test_support.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static const uint8_t uuid[16] = {
    0x35, 0x01, 0x20, 0x26, 0x08, 0x29, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
};

static int run_life(enum c35_lane lane, struct c35_trace *trace)
{
    struct c35_storage storage;
    struct c35_runtime runtime;
    struct c35_request read = c35_request_read(0);
    struct c35_semantic_result result;
    uint8_t input[C35_ATOM_BYTES];
    uint8_t output[C35_ATOM_BYTES];

    memset(input, 0x5a, sizeof(input));
    CHECK(c35_storage_init(&storage, lane, uuid));
    CHECK(c35_runtime_init(
        &runtime, &storage, lane, UINT64_C(0x3501),
        UINT64_C(0x9b6d3e7a4c2158f1), 0, 0, 1));
    CHECK(c35_dma_capture(&runtime, input, output));
    CHECK(memcmp(input, output, sizeof(input)) == 0);
    CHECK(c35_run_command(&runtime, &read, &result));
    CHECK(result.present_mask == 0 && result.status == 0);
    CHECK(c35_runtime_teardown(&runtime));
    *trace = runtime.trace;
    CHECK(c35_storage_close(&storage));
    return 1;
}

static int run_contract(enum c35_lane lane)
{
    struct c35_storage storage;
    struct c35_runtime runtime;
    struct c35_request malformed[6];
    struct fwlab_c31_command_handle command;
    uint32_t trace_count;
    unsigned int index;

    CHECK(c35_storage_init(&storage, lane, uuid));
    CHECK(c35_runtime_init(
        &runtime, &storage, lane, UINT64_C(0x3503),
        UINT64_C(0x8877665544332211), 0, 0, 4));
    malformed[0] = c35_request_read(0);
    malformed[0].atom = C35_ATOMS;
    malformed[1] = c35_request_read(0);
    malformed[1].reserved[0] = 1;
    malformed[2] = c35_request_write(
        0, FWLAB_PERSIST_SELF_DURABLE, 1,
        (const uint8_t[C35_ATOM_BYTES]){0});
    malformed[2].atom_mask = 0;
    malformed[3] = c35_request_trim(
        0, FWLAB_PERSIST_SELF_DURABLE, 1);
    malformed[3].payload[0][0] = 1;
    malformed[4] = c35_request_fence(0);
    malformed[5] = c35_request_read(0);
    malformed[5].kind = C35_FENCE + 1u;
    trace_count = fwlab_c31_trace_count(runtime.lifecycle);
    for (index = 0; index < 6; ++index) {
        CHECK(c35_headless_submit(
            &runtime.headless, &malformed[index], &command) == C35_INVALID);
        CHECK(fwlab_c31_trace_count(runtime.lifecycle) == trace_count);
    }
    CHECK(c35_runtime_teardown(&runtime));
    CHECK(c35_storage_close(&storage));
    return 1;
}

static int run_semantic(
    enum c35_lane lane,
    struct c35_trace *trace,
    uint8_t raw[C35_RAW_PROJECTION_BYTES],
    uint8_t *container
)
{
    struct c35_storage storage;
    struct c35_runtime runtime;
    struct c35_semantic_result result;
    struct c35_request request;
    uint8_t input0[C35_ATOM_BYTES];
    uint8_t input1[C35_ATOM_BYTES];
    uint8_t captured[C35_ATOM_BYTES];

    memset(input0, 0x11, sizeof(input0));
    memset(input1, 0x22, sizeof(input1));
    CHECK(c35_storage_init(&storage, lane, uuid));
    CHECK(c35_runtime_init(
        &runtime, &storage, lane, UINT64_C(0x3502),
        UINT64_C(0x9b6d3e7a4c2158f1), 1, 0, 2));
    CHECK(c35_dma_capture(&runtime, input0, captured));
    request = c35_request_write(
        0, FWLAB_PERSIST_SELF_DURABLE, 1, captured);
    CHECK(c35_run_command(&runtime, &request, &result));
    CHECK(result.witness_class == FWLAB_PERSIST_DURABLE_ELIGIBLE &&
          result.logical_kind[0] == C34_LOGICAL_VALUE);
    request = c35_request_read(0);
    CHECK(c35_run_command(&runtime, &request, &result));
    CHECK(result.present_mask == 1 &&
          memcmp(result.payload[0], input0, sizeof(input0)) == 0);
    request = c35_request_write(
        1, FWLAB_PERSIST_DEFAULT, 2, input1);
    CHECK(c35_run_command(&runtime, &request, &result));
    CHECK(result.witness_class == FWLAB_PERSIST_VOLATILE_ELIGIBLE);
    CHECK(c35_headless_pump_quiescent(&runtime.headless, 8192) == C35_OK);
    request = c35_request_fence(2);
    CHECK(c35_run_command(&runtime, &request, &result));
    CHECK(result.witness_class == FWLAB_PERSIST_DURABLE_ELIGIBLE);
    request = c35_request_trim(
        0, FWLAB_PERSIST_SELF_DURABLE, 3);
    CHECK(c35_run_command(&runtime, &request, &result));
    CHECK(result.logical_kind[0] == C34_LOGICAL_TOMBSTONE);
    request = c35_request_read(0);
    CHECK(c35_run_command(&runtime, &request, &result));
    CHECK(result.present_mask == 0);
    CHECK(c35_runtime_projection(&runtime, raw));
    if (container != NULL) {
        CHECK(c35_storage_container(&storage, container));
    }
    CHECK(c35_runtime_teardown(&runtime));
    *trace = runtime.trace;
    CHECK(c35_storage_close(&storage));
    return 1;
}

static int run_restart(enum c35_lane lane)
{
    struct c35_storage storage;
    struct c35_runtime before;
    struct c35_runtime after;
    struct c35_request request;
    struct c35_semantic_result result;
    uint8_t payload[C35_ATOM_BYTES];
    uint8_t raw_before[C35_RAW_PROJECTION_BYTES];
    uint8_t raw_after[C35_RAW_PROJECTION_BYTES];

    memset(payload, 0x33, sizeof(payload));
    CHECK(c35_storage_init(&storage, lane, uuid));
    CHECK(c35_runtime_init(
        &before, &storage, lane, UINT64_C(0x3510),
        UINT64_C(0x1020304050607080), 0, 0, 3));
    request = c35_request_write(
        1, FWLAB_PERSIST_SELF_DURABLE, 1, payload);
    CHECK(c35_run_command(&before, &request, &result));
    CHECK(c35_runtime_projection(&before, raw_before));
    CHECK(c35_runtime_teardown(&before));
    CHECK(c35_storage_restart(&storage));
    CHECK(c35_runtime_init(
        &after, &storage, lane, UINT64_C(0x3511),
        UINT64_C(0x1122334455667788), 0, 0, 3));
    request = c35_request_read(1);
    CHECK(c35_run_command(&after, &request, &result));
    CHECK(result.present_mask == 2 &&
          memcmp(result.payload[1], payload, sizeof(payload)) == 0);
    CHECK(c35_runtime_projection(&after, raw_after));
    CHECK(memcmp(raw_before, raw_after, sizeof(raw_before)) == 0);
    CHECK(c35_runtime_teardown(&after));
    CHECK(c35_storage_close(&storage));
    return 1;
}

int main(void)
{
    struct c35_trace life[4];
    struct c35_trace semantic[3];
    uint8_t raw[3][C35_RAW_PROJECTION_BYTES];
    uint8_t byte_container[C34_FILE_IMAGE_BYTES];
    uint8_t posix_container[C34_FILE_IMAGE_BYTES];
    unsigned int lane;

    for (lane = C35_LANE_SCRIPTED; lane <= C35_LANE_POSIX; ++lane) {
        CHECK(run_life((enum c35_lane)lane, &life[lane]));
        CHECK(c35_trace_equal(&life[0], &life[lane]));
        CHECK(run_contract((enum c35_lane)lane));
    }
    CHECK(run_semantic(
        C35_LANE_MEMORY, &semantic[0], raw[0], NULL));
    CHECK(run_semantic(
        C35_LANE_BYTE, &semantic[1], raw[1], byte_container));
    CHECK(run_semantic(
        C35_LANE_POSIX, &semantic[2], raw[2], posix_container));
    CHECK(c35_trace_equal(&semantic[0], &semantic[1]) &&
          c35_trace_equal(&semantic[0], &semantic[2]));
    CHECK(memcmp(raw[0], raw[1], sizeof(raw[0])) == 0 &&
          memcmp(raw[0], raw[2], sizeof(raw[0])) == 0);
    CHECK(memcmp(byte_container, posix_container,
                 sizeof(byte_container)) == 0);
    CHECK(run_restart(C35_LANE_MEMORY));
    CHECK(run_restart(C35_LANE_BYTE));
    CHECK(run_restart(C35_LANE_POSIX));
    puts("C3.5 tiered S/M/B/P graduation: PASS (14 cases)");
    printf("  life=%016" PRIx64 " semantic=%016" PRIx64 "\n",
           c35_trace_hash(&life[0]), c35_trace_hash(&semantic[0]));
    return 0;
}
