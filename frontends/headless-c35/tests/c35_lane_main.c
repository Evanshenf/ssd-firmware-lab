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
static void put_u32(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

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

static void encode_semantic(
    const struct c35_semantic_result *result,
    uint8_t bytes[64]
)
{
    unsigned int atom;

    memset(bytes, 0, 64);
    bytes[0] = result->status;
    bytes[1] = result->request_kind;
    bytes[2] = result->atom_mask;
    bytes[3] = result->present_mask;
    bytes[4] = result->witness_class;
    bytes[5] = result->witness_reason;
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        bytes[8 + atom] = result->logical_kind[atom];
        bytes[10 + atom] = result->logical_version[atom];
        bytes[12 + atom] = result->logical_copy[atom];
        put_u32(&bytes[16 + atom * 4u], result->value_crc[atom]);
        memcpy(&bytes[24 + atom * C35_ATOM_BYTES], result->payload[atom],
               C35_ATOM_BYTES);
    }
}

static int two_atom_restart(
    struct c35_runtime *runtime,
    struct c35_storage *storage,
    uint32_t scenario
)
{
    return c35_runtime_teardown(runtime) && c35_storage_restart(storage) &&
           c35_runtime_init(
               runtime, storage, C35_LANE_KIND,
               UINT64_C(0x35aa0011223344),
               UINT64_C(0x7f6e5d4c3b2a1908), 0, 0, scenario);
}

static int emit_two_atom(void)
{
    struct c35_storage storage;
    struct c35_runtime runtime;
    struct c35_semantic_result result;
    struct c35_request request;
    uint8_t payload[C35_ATOMS][C35_ATOM_BYTES];
    const uint8_t (*payload_view)[C35_ATOM_BYTES];
    uint8_t canonical[10][64];
    uint8_t raw[3][C35_RAW_PROJECTION_BYTES];
    unsigned int atom;
    unsigned int output = 0;
    int ok;

    for (atom = 0; atom < C35_ATOM_BYTES; ++atom) {
        payload[0][atom] = (uint8_t)(0x20u + atom * 3u);
        payload[1][atom] = (uint8_t)(0xe0u - atom * 5u);
    }
    payload_view = (const uint8_t (*)[C35_ATOM_BYTES])(const void *)payload;
    if (!c35_storage_init(&storage, C35_LANE_KIND, lane_uuid) ||
        !c35_runtime_init(
            &runtime, &storage, C35_LANE_KIND,
            UINT64_C(0x35aa0011223344),
            UINT64_C(0x7f6e5d4c3b2a1908), 0, 0, 0x35aa0001))
        return 0;
    request = c35_request_write_mask(
        UINT8_C(0x03), FWLAB_PERSIST_SELF_DURABLE, 1, payload_view);
    if (!c35_run_command(&runtime, &request, &result)) return 0;
    encode_semantic(&result, canonical[output++]);
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        request = c35_request_read((uint8_t)atom);
        if (!c35_run_command(&runtime, &request, &result)) return 0;
        encode_semantic(&result, canonical[output++]);
    }
    if (!c35_runtime_projection(&runtime, raw[0]) ||
        !two_atom_restart(&runtime, &storage, 0x35aa0002)) return 0;
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        request = c35_request_read((uint8_t)atom);
        if (!c35_run_command(&runtime, &request, &result)) return 0;
        encode_semantic(&result, canonical[output++]);
    }
    if (!c35_runtime_projection(&runtime, raw[1])) return 0;
    request = c35_request_trim_mask(
        UINT8_C(0x03), FWLAB_PERSIST_SELF_DURABLE, 2);
    if (!c35_run_command(&runtime, &request, &result)) return 0;
    encode_semantic(&result, canonical[output++]);
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        request = c35_request_read((uint8_t)atom);
        if (!c35_run_command(&runtime, &request, &result)) return 0;
        encode_semantic(&result, canonical[output++]);
    }
    if (!two_atom_restart(&runtime, &storage, 0x35aa0003)) return 0;
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        request = c35_request_read((uint8_t)atom);
        if (!c35_run_command(&runtime, &request, &result)) return 0;
        encode_semantic(&result, canonical[output++]);
    }
    if (output != 10 || !c35_runtime_projection(&runtime, raw[2]) ||
        !c35_runtime_teardown(&runtime)) return 0;
    ok = write_exact(canonical, sizeof(canonical)) &&
         write_exact(raw, sizeof(raw)) && c35_storage_close(&storage);
    return ok;
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
    if (strcmp(argv[1], "two-atom") == 0)
        return emit_two_atom() ? 0 : 1;
#endif
    return 2;
}
