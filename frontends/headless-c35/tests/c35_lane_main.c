/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_test_support.h"

#include <stdio.h>
#include <string.h>

#ifndef C35_LANE_KIND
#error "C35_LANE_KIND must select one lane"
#endif

static const uint8_t lane_uuid[16] = {
    0x35, 0x01, 0x20, 0x26, 0x08, 0x29, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
};

static int write_exact(const void *bytes, size_t length)
{
    return fwrite(bytes, 1, length, stdout) == length &&
           fflush(stdout) == 0;
}

static int emit_life(void)
{
    struct c35_storage storage;
    struct c35_runtime runtime;
    struct c35_request read = c35_request_read(0);
    struct c35_semantic_result result;
    uint8_t input[C35_ATOM_BYTES];
    uint8_t output[C35_ATOM_BYTES];

    memset(input, 0x5a, sizeof(input));
    if (!c35_storage_init(&storage, C35_LANE_KIND, lane_uuid) ||
        !c35_runtime_init(
            &runtime, &storage, C35_LANE_KIND, UINT64_C(0x3501),
            UINT64_C(0x9b6d3e7a4c2158f1), 0, 0, 1) ||
        !c35_dma_capture(&runtime, input, output) ||
        memcmp(input, output, sizeof(input)) != 0 ||
        !c35_run_command(&runtime, &read, &result) ||
        result.present_mask != 0 || result.status != 0 ||
        !c35_runtime_teardown(&runtime) ||
        !write_exact(runtime.trace.bytes, runtime.trace.length) ||
        !c35_storage_close(&storage)) {
        return 0;
    }
    return 1;
}

#if C35_LANE_KIND != C35_LANE_SCRIPTED
static int run_semantic(
    struct c35_storage *storage,
    struct c35_runtime *runtime,
    uint8_t raw[C35_RAW_PROJECTION_BYTES],
    uint8_t container[C34_FILE_IMAGE_BYTES],
    int need_container
)
{
    struct c35_semantic_result result;
    struct c35_request request;
    uint8_t input0[C35_ATOM_BYTES];
    uint8_t input1[C35_ATOM_BYTES];
    uint8_t captured[C35_ATOM_BYTES];

    memset(input0, 0x11, sizeof(input0));
    memset(input1, 0x22, sizeof(input1));
    if (!c35_storage_init(storage, C35_LANE_KIND, lane_uuid) ||
        !c35_runtime_init(
            runtime, storage, C35_LANE_KIND, UINT64_C(0x3502),
            UINT64_C(0x9b6d3e7a4c2158f1), 1, 0, 2) ||
        !c35_dma_capture(runtime, input0, captured)) {
        return 0;
    }
    request = c35_request_write(
        0, FWLAB_PERSIST_SELF_DURABLE, 1, captured);
    if (!c35_run_command(runtime, &request, &result)) return 0;
    request = c35_request_read(0);
    if (!c35_run_command(runtime, &request, &result) ||
        result.present_mask != 1u ||
        memcmp(result.payload[0], input0, sizeof(input0)) != 0) return 0;
    request = c35_request_write(1, FWLAB_PERSIST_DEFAULT, 2, input1);
    if (!c35_run_command(runtime, &request, &result) ||
        c35_headless_pump_quiescent(&runtime->headless, 8192) != C35_OK)
        return 0;
    request = c35_request_fence(2);
    if (!c35_run_command(runtime, &request, &result)) return 0;
    request = c35_request_trim(
        0, FWLAB_PERSIST_SELF_DURABLE, 3);
    if (!c35_run_command(runtime, &request, &result)) return 0;
    request = c35_request_read(0);
    if (!c35_run_command(runtime, &request, &result) ||
        result.present_mask != 0 ||
        !c35_runtime_projection(runtime, raw) ||
        (need_container &&
         !c35_storage_container(storage, container)) ||
        !c35_runtime_teardown(runtime)) {
        return 0;
    }
    return 1;
}

static int emit_semantic(int container_only)
{
    struct c35_storage storage;
    struct c35_runtime runtime;
    uint8_t raw[C35_RAW_PROJECTION_BYTES];
    uint8_t container[C34_FILE_IMAGE_BYTES];
    int need_container = C35_LANE_KIND == C35_LANE_BYTE ||
                         C35_LANE_KIND == C35_LANE_POSIX;
    int ok;

    if (container_only && !need_container) return 0;
    if (!run_semantic(
            &storage, &runtime, raw, container, need_container)) return 0;
    ok = container_only ?
        write_exact(container, sizeof(container)) :
        (write_exact(runtime.trace.bytes, runtime.trace.length) &&
         write_exact(raw, sizeof(raw)));
    return c35_storage_close(&storage) && ok;
}
#endif

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (strcmp(argv[1], "life") == 0) return emit_life() ? 0 : 1;
#if C35_LANE_KIND != C35_LANE_SCRIPTED
    if (strcmp(argv[1], "semantic") == 0)
        return emit_semantic(0) ? 0 : 1;
    if (strcmp(argv[1], "container") == 0)
        return emit_semantic(1) ? 0 : 1;
#endif
    return 2;
}
