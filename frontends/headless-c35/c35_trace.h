/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_TRACE_H
#define FWLAB_C35_TRACE_H

#include <stddef.h>
#include <stdint.h>

#include "c35_binding.h"

#define C35_TRACE_VERSION 1u
#define C35_TRACE_BYTES 65536u
#define C35_TRACE_EVENT_BYTES 96u

enum c35_trace_kind {
    C35_TRACE_DMA = 1,
    C35_TRACE_COMMAND = 2,
    C35_TRACE_RESET = 3,
    C35_TRACE_STALE = 4,
    C35_TRACE_RECOVERY = 5,
    C35_TRACE_TEARDOWN = 6
};

struct c35_trace_event {
    uint8_t kind;
    uint8_t actor;
    uint8_t request_kind;
    uint8_t api_result;
    uint8_t terminal;
    uint8_t completion_result;
    uint8_t effect_class;
    uint8_t witness_class;
    uint8_t witness_reason;
    uint8_t status;
    uint8_t atom_mask;
    uint8_t present_mask;
    uint32_t epoch;
    uint32_t ordinal;
    struct c35_semantic_result semantic;
};

struct c35_trace {
    uint8_t bytes[C35_TRACE_BYTES];
    uint32_t length;
    uint32_t events;
};

void c35_trace_init(struct c35_trace *trace, uint32_t scenario);
enum c35_result c35_trace_append(
    struct c35_trace *trace,
    const struct c35_trace_event *event
);
enum c35_result c35_trace_append_projection(
    struct c35_trace *trace,
    uint8_t kind,
    const uint8_t *bytes,
    uint32_t length
);
uint64_t c35_trace_hash(const struct c35_trace *trace);
int c35_trace_equal(const struct c35_trace *left, const struct c35_trace *right);

#endif
