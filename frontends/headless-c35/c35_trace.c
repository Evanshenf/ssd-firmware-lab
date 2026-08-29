/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_trace.h"

#include <string.h>

static void put_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void c35_trace_init(struct c35_trace *trace, uint32_t scenario)
{
    memset(trace, 0, sizeof(*trace));
    trace->bytes[0] = 'C';
    trace->bytes[1] = '3';
    trace->bytes[2] = '5';
    trace->bytes[3] = 'T';
    put_u16(&trace->bytes[4], C35_TRACE_VERSION);
    put_u16(&trace->bytes[6], 16);
    put_u32(&trace->bytes[8], scenario);
    trace->length = 16;
}

enum c35_result c35_trace_append(
    struct c35_trace *trace,
    const struct c35_trace_event *event
)
{
    uint8_t *bytes;
    unsigned int atom;

    if (trace == NULL || event == NULL ||
        trace->length > C35_TRACE_BYTES - C35_TRACE_EVENT_BYTES) {
        return C35_NO_CAPACITY;
    }
    bytes = &trace->bytes[trace->length];
    memset(bytes, 0, C35_TRACE_EVENT_BYTES);
    bytes[0] = event->kind;
    bytes[1] = event->actor;
    bytes[2] = event->request_kind;
    bytes[3] = event->api_result;
    bytes[4] = event->terminal;
    bytes[5] = event->completion_result;
    bytes[6] = event->effect_class;
    bytes[7] = event->witness_class;
    bytes[8] = event->witness_reason;
    bytes[9] = event->status;
    bytes[10] = event->atom_mask;
    bytes[11] = event->present_mask;
    put_u32(&bytes[12], event->epoch);
    put_u32(&bytes[16], event->ordinal);
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        bytes[20 + atom] = event->semantic.logical_kind[atom];
        bytes[22 + atom] = event->semantic.logical_version[atom];
        bytes[24 + atom] = event->semantic.logical_copy[atom];
        put_u32(&bytes[28 + atom * 4], event->semantic.value_crc[atom]);
        memcpy(&bytes[40 + atom * C35_ATOM_BYTES],
               event->semantic.payload[atom], C35_ATOM_BYTES);
    }
    trace->length += C35_TRACE_EVENT_BYTES;
    ++trace->events;
    put_u32(&trace->bytes[12], trace->events);
    return C35_OK;
}

enum c35_result c35_trace_append_projection(
    struct c35_trace *trace,
    uint8_t kind,
    const uint8_t *bytes,
    uint32_t length
)
{
    if (trace == NULL || bytes == NULL || length > UINT16_MAX ||
        trace->length > C35_TRACE_BYTES - length - 4u) {
        return C35_NO_CAPACITY;
    }
    trace->bytes[trace->length] = kind;
    trace->bytes[trace->length + 1u] = 0;
    put_u16(&trace->bytes[trace->length + 2u], (uint16_t)length);
    memcpy(&trace->bytes[trace->length + 4u], bytes, length);
    trace->length += length + 4u;
    return C35_OK;
}

uint64_t c35_trace_hash(const struct c35_trace *trace)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t index;

    if (trace == NULL || trace->length > C35_TRACE_BYTES) {
        return 0;
    }
    for (index = 0; index < trace->length; ++index) {
        hash ^= trace->bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

int c35_trace_equal(const struct c35_trace *left, const struct c35_trace *right)
{
    return left != NULL && right != NULL && left->length == right->length &&
           memcmp(left->bytes, right->bytes, left->length) == 0;
}
