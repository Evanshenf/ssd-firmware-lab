/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>

#include "fwlab/portable/nfc_codec.h"

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

static struct fwlab_nfc_request request_make(void)
{
    struct fwlab_nfc_request request;

    memset(&request, 0, sizeof(request));
    request.version = FWLAB_NFC_CONTRACT_VERSION;
    request.size = (uint16_t)sizeof(request);
    request.operation.instance_nonce = UINT64_C(0x0102030405060708);
    request.operation.operation_uid = UINT64_C(0x1112131415161718);
    request.operation.controller_epoch = UINT32_C(0x21222324);
    request.operation.generation = UINT32_C(0x31323334);
    request.ppa = (struct fwlab_nfc_ppa){1, 2, 3, 4, 5, 0};
    request.main = (struct fwlab_nfc_buffer_ref){7, 8, 9, 0};
    request.kind = FWLAB_NFC_READ_TRANSFER;
    request.region_mask = FWLAB_NFC_REGION_MAIN;
    request.cache.instance_nonce = request.operation.instance_nonce;
    request.cache.controller_epoch = request.operation.controller_epoch;
    request.cache.generation = 6;
    request.cache.channel = 1;
    request.cache.lun = 2;
    request.cache.plane = 3;
    request.cookie = UINT64_C(0x4142434445464748);
    request.fault_tag = UINT64_C(0x5152535455565758);
    request.scheduling_group = UINT32_C(0x61626364);
    request.priority = UINT16_C(0x7172);
    request.retry_step = 1;
    return request;
}

static int test_request_codec(void)
{
    static const uint8_t golden_prefix[] = {
        0x4e, 0x46, 0x52, 0x31, 0x01, 0x00, 0x90, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x08, 0x07, 0x06, 0x05,
        0x04, 0x03, 0x02, 0x01, 0x18, 0x17, 0x16, 0x15,
        0x14, 0x13, 0x12, 0x11, 0x24, 0x23, 0x22, 0x21,
        0x34, 0x33, 0x32, 0x31,
    };
    struct fwlab_nfc_request input = request_make();
    struct fwlab_nfc_request output;
    uint8_t wire[FWLAB_NFC_REQUEST_WIRE_SIZE];

    CHECK(fwlab_nfc_request_encode(&input, wire, sizeof(wire)) ==
          FWLAB_NFC_API_OK);
    CHECK(memcmp(wire, golden_prefix, sizeof(golden_prefix)) == 0);
    CHECK(fwlab_nfc_request_decode(wire, sizeof(wire), &output) ==
          FWLAB_NFC_API_OK);
    CHECK(output.operation.instance_nonce == input.operation.instance_nonce);
    CHECK(output.operation.operation_uid == input.operation.operation_uid);
    CHECK(output.ppa.plane == 3 && output.main.length == 9);
    CHECK(output.cookie == input.cookie && output.retry_step == 1);
    wire[sizeof(wire) - 1] = 1;
    CHECK(fwlab_nfc_request_decode(wire, sizeof(wire), &output) ==
          FWLAB_NFC_API_INVALID_CONTRACT);
    return 0;
}

static int test_completion_and_trace_codec(void)
{
    struct fwlab_nfc_request request = request_make();
    struct fwlab_nfc_completion input;
    struct fwlab_nfc_completion output;
    struct fwlab_nfc_trace_entry trace;
    struct fwlab_nfc_trace_entry decoded_trace;
    uint8_t completion_wire[FWLAB_NFC_COMPLETION_WIRE_SIZE];
    uint8_t trace_wire[FWLAB_NFC_TRACE_WIRE_SIZE];

    memset(&input, 0, sizeof(input));
    input.version = FWLAB_NFC_CONTRACT_VERSION;
    input.size = (uint16_t)sizeof(input);
    input.operation = request.operation;
    input.ppa = request.ppa;
    input.cache = request.cache;
    input.cookie = request.cookie;
    input.terminal = FWLAB_NFC_TERMINAL_SUCCESS;
    input.physical_outcome = FWLAB_NFC_PHYS_NO_EFFECT;
    input.integrity = FWLAB_NFC_INTEGRITY_NOT_APPLICABLE;
    input.ecc_status = FWLAB_NFC_ECC_CORRECTED;
    input.requested_region_mask = FWLAB_NFC_REGION_MAIN;
    input.valid_region_mask = FWLAB_NFC_REGION_MAIN;
    input.block_health = FWLAB_NFC_BLOCK_GOOD;
    input.operation_kind = FWLAB_NFC_READ_TRANSFER;
    CHECK(fwlab_nfc_completion_encode(&input, completion_wire,
                                       sizeof(completion_wire)) ==
          FWLAB_NFC_API_OK);
    CHECK(fwlab_nfc_completion_decode(completion_wire,
                                       sizeof(completion_wire), &output) ==
          FWLAB_NFC_API_OK);
    CHECK(output.cookie == input.cookie &&
          output.ecc_status == FWLAB_NFC_ECC_CORRECTED);

    memset(&trace, 0, sizeof(trace));
    trace.version = FWLAB_NFC_CONTRACT_VERSION;
    trace.size = (uint16_t)sizeof(trace);
    trace.kind = FWLAB_NFC_TRACE_ACCEPT;
    trace.sequence = 3;
    trace.virtual_tick = 4;
    trace.operation = request.operation;
    trace.ppa = request.ppa;
    trace.from_state = 1;
    trace.to_state = 2;
    trace.detail = 5;
    CHECK(fwlab_nfc_trace_encode(&trace, trace_wire, sizeof(trace_wire)) ==
          FWLAB_NFC_API_OK);
    CHECK(fwlab_nfc_trace_decode(trace_wire, sizeof(trace_wire),
                                 &decoded_trace) == FWLAB_NFC_API_OK);
    CHECK(decoded_trace.sequence == 3 && decoded_trace.detail == 5);
    return 0;
}

int main(void)
{
    int line = test_request_codec();

    if (line == 0) {
        line = test_completion_and_trace_codec();
    }
    if (line != 0) {
        fprintf(stderr, "C3.3 codec unit failed at line %d\n", line);
        return 1;
    }
    printf("C3.3 explicit codec unit: PASS (2 cases)\n");
    return 0;
}
