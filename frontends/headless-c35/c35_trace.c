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

static void put_u64(uint8_t *bytes, uint64_t value)
{
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

static uint16_t get_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t get_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int records_valid(const struct c35_trace *trace)
{
    uint32_t offset = C35_TRACE_HEADER_BYTES;
    uint32_t events = 0;

    while (offset < trace->length) {
        uint8_t kind = trace->bytes[offset];
        uint32_t remaining = trace->length - offset;

        if (kind >= C35_PUBLICATION_DMA &&
            kind <= C35_PUBLICATION_CLEANUP) {
            if (remaining < C35_TRACE_EVENT_BYTES) {
                return 0;
            }
            offset += C35_TRACE_EVENT_BYTES;
            ++events;
        } else if (kind >= 0x80u) {
            uint32_t payload;

            if (remaining < 4u) {
                return 0;
            }
            payload = get_u16(&trace->bytes[offset + 2u]);
            if (payload > remaining - 4u) {
                return 0;
            }
            offset += payload + 4u;
        } else {
            return 0;
        }
    }
    return offset == trace->length && events == trace->events;
}

void c35_trace_init(struct c35_trace *trace, uint32_t scenario)
{
    if (trace == NULL) {
        return;
    }
    memset(trace, 0, sizeof(*trace));
    trace->bytes[0] = 'C';
    trace->bytes[1] = '3';
    trace->bytes[2] = '5';
    trace->bytes[3] = 'T';
    put_u16(&trace->bytes[4], C35_TRACE_VERSION);
    put_u16(&trace->bytes[6], C35_TRACE_HEADER_BYTES);
    put_u32(&trace->bytes[8], scenario);
    put_u32(&trace->bytes[12], 0);
    trace->length = C35_TRACE_HEADER_BYTES;
    trace->next_generation = 1;
}

int c35_trace_valid(const struct c35_trace *trace)
{
    return trace != NULL && trace->bytes[0] == 'C' &&
           trace->bytes[1] == '3' && trace->bytes[2] == '5' &&
           trace->bytes[3] == 'T' &&
           get_u16(&trace->bytes[4]) == C35_TRACE_VERSION &&
           get_u16(&trace->bytes[6]) == C35_TRACE_HEADER_BYTES &&
           trace->length >= C35_TRACE_HEADER_BYTES &&
           trace->length <= C35_TRACE_BYTES &&
           get_u32(&trace->bytes[12]) == trace->events &&
           trace->next_generation != 0 &&
           ((trace->active_generation == 0 && trace->active_uid == 0) ||
            (trace->active_generation != 0 && trace->active_uid != 0)) &&
           ((trace->last_recorded_generation == 0 &&
             trace->last_recorded_uid == 0) ||
            (trace->last_recorded_generation != 0 &&
             trace->last_recorded_uid != 0)) &&
           trace->reserved == 0 &&
           records_valid(trace);
}

enum c35_result c35_trace_reserve(
    struct c35_trace *trace,
    uint64_t publication_uid,
    uint32_t encoded_max,
    struct c35_trace_reservation *reservation
)
{
    uint32_t remaining;

    if (reservation == NULL || publication_uid == 0 ||
        encoded_max != C35_TRACE_EVENT_BYTES ||
        !c35_trace_valid(trace)) {
        return C35_INVALID;
    }
    if (trace->active_generation != 0) {
        if (trace->active_uid == publication_uid) {
            memset(reservation, 0, sizeof(*reservation));
            reservation->publication_uid = publication_uid;
            reservation->offset = trace->length;
            reservation->generation = trace->active_generation;
            reservation->length = C35_TRACE_EVENT_BYTES;
            reservation->state = C35_OBSERVATION_RESERVED;
            return C35_OK;
        }
        return C35_WRONG_STATE;
    }
    if (trace->last_recorded_uid == publication_uid) {
        memset(reservation, 0, sizeof(*reservation));
        reservation->publication_uid = publication_uid;
        reservation->offset = trace->length - C35_TRACE_EVENT_BYTES;
        reservation->generation = trace->last_recorded_generation;
        reservation->length = C35_TRACE_EVENT_BYTES;
        reservation->state = C35_OBSERVATION_RECORDED;
        return C35_OK;
    }
    remaining = C35_TRACE_BYTES - trace->length;
    if (remaining < C35_TRACE_EVENT_BYTES) {
        return C35_NO_CAPACITY;
    }
    if (trace->next_generation == UINT32_MAX) {
        return C35_LIMIT;
    }
    memset(reservation, 0, sizeof(*reservation));
    reservation->publication_uid = publication_uid;
    reservation->offset = trace->length;
    reservation->generation = trace->next_generation++;
    reservation->length = C35_TRACE_EVENT_BYTES;
    reservation->state = C35_OBSERVATION_RESERVED;
    trace->active_generation = reservation->generation;
    trace->active_uid = publication_uid;
    return C35_OK;
}

static int publication_valid(const struct c35_publication *publication)
{
    return publication != NULL &&
           publication->version == C35_PUBLICATION_VERSION &&
           publication->size == sizeof(*publication) &&
           publication->kind >= C35_PUBLICATION_DMA &&
           publication->kind <= C35_PUBLICATION_CLEANUP &&
           publication->publication_uid != 0 &&
           publication->reserved[0] == 0 && publication->reserved[1] == 0;
}

static void encode_publication(
    uint8_t bytes[C35_TRACE_EVENT_BYTES],
    const struct c35_publication *publication,
    uint32_t ordinal
)
{
    unsigned int atom;

    memset(bytes, 0, C35_TRACE_EVENT_BYTES);
    bytes[0] = publication->kind;
    bytes[1] = publication->actor;
    bytes[2] = publication->request_kind;
    bytes[3] = publication->api_result;
    bytes[4] = publication->terminal;
    bytes[5] = publication->completion_result;
    bytes[6] = publication->effect_class;
    bytes[7] = publication->witness_class;
    bytes[8] = publication->witness_reason;
    bytes[9] = publication->status;
    bytes[10] = publication->atom_mask;
    bytes[11] = publication->present_mask;
    put_u32(&bytes[12], publication->epoch);
    put_u32(&bytes[16], ordinal);
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        bytes[20 + atom] = publication->semantic.logical_kind[atom];
        bytes[22 + atom] = publication->semantic.logical_version[atom];
        bytes[24 + atom] = publication->semantic.logical_copy[atom];
        put_u32(&bytes[28 + atom * 4],
                publication->semantic.value_crc[atom]);
        memcpy(&bytes[40 + atom * C35_ATOM_BYTES],
               publication->semantic.payload[atom], C35_ATOM_BYTES);
    }
    put_u64(&bytes[72], publication->publication_uid);
    put_u32(&bytes[80], publication->commit_state);
}

enum c35_result c35_trace_commit(
    struct c35_trace *trace,
    struct c35_trace_reservation *reservation,
    const struct c35_publication *publication
)
{
    if (c35_trace_valid(trace) && reservation != NULL &&
        publication_valid(publication) &&
        reservation->state == C35_OBSERVATION_RECORDED &&
        reservation->publication_uid == publication->publication_uid &&
        reservation->publication_uid == trace->last_recorded_uid &&
        reservation->generation == trace->last_recorded_generation &&
        reservation->offset + C35_TRACE_EVENT_BYTES == trace->length &&
        trace->events != 0) {
        uint8_t encoded[C35_TRACE_EVENT_BYTES];

        encode_publication(encoded, publication, trace->events - 1u);
        return memcmp(&trace->bytes[reservation->offset], encoded,
                      C35_TRACE_EVENT_BYTES) == 0 ? C35_OK : C35_CORRUPT;
    }
    if (!c35_trace_valid(trace) || reservation == NULL ||
        !publication_valid(publication) ||
        reservation->state != C35_OBSERVATION_RESERVED ||
        reservation->length != C35_TRACE_EVENT_BYTES ||
        reservation->offset != trace->length ||
        reservation->generation != trace->active_generation ||
        reservation->publication_uid != trace->active_uid ||
        reservation->publication_uid != publication->publication_uid ||
        C35_TRACE_BYTES - trace->length < C35_TRACE_EVENT_BYTES) {
        return C35_INVALID;
    }
    encode_publication(&trace->bytes[trace->length], publication,
                       trace->events);
    trace->length += C35_TRACE_EVENT_BYTES;
    ++trace->events;
    put_u32(&trace->bytes[12], trace->events);
    trace->last_recorded_uid = publication->publication_uid;
    trace->last_recorded_generation = reservation->generation;
    trace->active_generation = 0;
    trace->active_uid = 0;
    reservation->state = C35_OBSERVATION_RECORDED;
    return C35_OK;
}

enum c35_result c35_trace_query(
    const struct c35_trace *trace,
    const struct c35_trace_reservation *reservation,
    enum c35_observation_state *state
)
{
    if (!c35_trace_valid(trace) || reservation == NULL || state == NULL) {
        return C35_INVALID;
    }
    if (reservation->state == C35_OBSERVATION_RECORDED &&
        reservation->publication_uid == trace->last_recorded_uid &&
        reservation->generation == trace->last_recorded_generation) {
        *state = C35_OBSERVATION_RECORDED;
        return C35_OK;
    }
    if (reservation->state == C35_OBSERVATION_RESERVED &&
        reservation->generation == trace->active_generation &&
        reservation->publication_uid == trace->active_uid) {
        *state = C35_OBSERVATION_RESERVED;
        return C35_OK;
    }
    if (reservation->state == C35_OBSERVATION_NONE) {
        *state = C35_OBSERVATION_NONE;
        return C35_OK;
    }
    return C35_STALE;
}

enum c35_result c35_trace_cancel(
    struct c35_trace *trace,
    struct c35_trace_reservation *reservation
)
{
    if (!c35_trace_valid(trace) || reservation == NULL ||
        reservation->state != C35_OBSERVATION_RESERVED ||
        reservation->generation != trace->active_generation ||
        reservation->publication_uid != trace->active_uid) {
        return C35_STALE;
    }
    trace->active_generation = 0;
    trace->active_uid = 0;
    memset(reservation, 0, sizeof(*reservation));
    return C35_OK;
}

enum c35_result c35_trace_append(
    struct c35_trace *trace,
    const struct c35_publication *publication
)
{
    struct c35_trace_reservation reservation;
    enum c35_result result;

    if (!publication_valid(publication)) {
        return C35_INVALID;
    }
    result = c35_trace_reserve(
        trace, publication->publication_uid, C35_TRACE_EVENT_BYTES,
        &reservation);
    if (result != C35_OK) {
        return result;
    }
    result = c35_trace_commit(trace, &reservation, publication);
    if (result != C35_OK) {
        (void)c35_trace_cancel(trace, &reservation);
    }
    return result;
}

enum c35_result c35_trace_append_projection(
    struct c35_trace *trace,
    uint8_t kind,
    const uint8_t *bytes,
    uint32_t length
)
{
    uint32_t remaining;

    if (bytes == NULL || kind < 0x80u || !c35_trace_valid(trace) ||
        length > UINT16_MAX ||
        trace->active_generation != 0) {
        return C35_INVALID;
    }
    remaining = C35_TRACE_BYTES - trace->length;
    if (remaining < 4u || length > remaining - 4u) {
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

    if (!c35_trace_valid(trace)) {
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
    return c35_trace_valid(left) && c35_trace_valid(right) &&
           left->length == right->length && left->events == right->events &&
           memcmp(left->bytes, right->bytes, left->length) == 0;
}
