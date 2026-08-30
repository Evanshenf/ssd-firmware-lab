/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_TRACE_H
#define FWLAB_C35_TRACE_H

#include <stddef.h>
#include <stdint.h>

#include "c35_publication.h"

#define C35_TRACE_VERSION 2u
#define C35_TRACE_BYTES 65536u
#define C35_TRACE_HEADER_BYTES 16u
#define C35_TRACE_EVENT_BYTES 96u

enum c35_observation_state {
    C35_OBSERVATION_NONE = 0,
    C35_OBSERVATION_RESERVED = 1,
    C35_OBSERVATION_RECORDED = 2,
    C35_OBSERVATION_LOST_NO_CAPACITY = 3,
    C35_OBSERVATION_LOST_INVALID_SINK = 4,
    C35_OBSERVATION_LOST_IO = 5
};

struct c35_trace_reservation {
    uint64_t publication_uid;
    uint32_t offset;
    uint32_t generation;
    uint16_t length;
    uint8_t state;
    uint8_t reserved;
};

struct c35_trace {
    uint8_t bytes[C35_TRACE_BYTES];
    uint32_t length;
    uint32_t events;
    uint32_t next_generation;
    uint32_t active_generation;
    uint64_t active_uid;
    uint64_t last_recorded_uid;
    uint32_t last_recorded_generation;
    uint32_t reserved;
};

void c35_trace_init(struct c35_trace *trace, uint32_t scenario);
int c35_trace_valid(const struct c35_trace *trace);
enum c35_result c35_trace_reserve(
    struct c35_trace *trace,
    uint64_t publication_uid,
    uint32_t encoded_max,
    struct c35_trace_reservation *reservation
);
enum c35_result c35_trace_commit(
    struct c35_trace *trace,
    struct c35_trace_reservation *reservation,
    const struct c35_publication *publication
);
enum c35_result c35_trace_query(
    const struct c35_trace *trace,
    const struct c35_trace_reservation *reservation,
    enum c35_observation_state *state
);
enum c35_result c35_trace_cancel(
    struct c35_trace *trace,
    struct c35_trace_reservation *reservation
);
enum c35_result c35_trace_append(
    struct c35_trace *trace,
    const struct c35_publication *publication
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
