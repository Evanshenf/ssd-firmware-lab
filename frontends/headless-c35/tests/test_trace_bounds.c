/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "../c35_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

struct guarded_trace {
    uint8_t before[32];
    struct c35_trace trace;
    uint8_t after[32];
};

static struct c35_publication publication_make(uint64_t uid)
{
    struct c35_publication publication;

    memset(&publication, 0, sizeof(publication));
    publication.version = C35_PUBLICATION_VERSION;
    publication.size = sizeof(publication);
    publication.kind = C35_PUBLICATION_COMMAND;
    publication.publication_uid = uid;
    publication.commit_state = 2;
    return publication;
}

static int test_projection_boundaries(void)
{
    struct guarded_trace *guarded = calloc(1, sizeof(*guarded));
    struct c35_trace *before = malloc(sizeof(*before));
    uint8_t *payload = malloc(UINT16_MAX);
    unsigned int index;

    CHECK(guarded != NULL && before != NULL && payload != NULL);
    memset(guarded->before, 0xa5, sizeof(guarded->before));
    memset(guarded->after, 0x5a, sizeof(guarded->after));
    memset(payload, 0x3c, UINT16_MAX);
    c35_trace_init(&guarded->trace, 1);
    CHECK(c35_trace_append_projection(
        &guarded->trace, 0x80, payload, 65516) == C35_OK);
    CHECK(guarded->trace.length == C35_TRACE_BYTES);
    CHECK(c35_trace_valid(&guarded->trace));
    for (index = 0; index < sizeof(guarded->before); ++index) {
        CHECK(guarded->before[index] == 0xa5 && guarded->after[index] == 0x5a);
    }

    c35_trace_init(&guarded->trace, 1);
    *before = guarded->trace;
    CHECK(c35_trace_append_projection(
        &guarded->trace, 0x80, payload, 65517) == C35_NO_CAPACITY);
    CHECK(memcmp(before, &guarded->trace, sizeof(*before)) == 0);
    for (index = 65533; index <= 65535; ++index) {
        *before = guarded->trace;
        CHECK(c35_trace_append_projection(
            &guarded->trace, 0x80, payload, index) == C35_NO_CAPACITY);
        CHECK(memcmp(before, &guarded->trace, sizeof(*before)) == 0);
    }
    for (index = 0; index < sizeof(guarded->before); ++index) {
        CHECK(guarded->before[index] == 0xa5 && guarded->after[index] == 0x5a);
    }
    free(payload);
    free(before);
    free(guarded);
    return 1;
}

static int test_corrupt_lengths(void)
{
    static const uint32_t corrupt[] = {
        C35_TRACE_BYTES, C35_TRACE_BYTES + 1u, UINT32_MAX
    };
    struct c35_trace left;
    struct c35_trace right;
    struct c35_publication publication = publication_make(1);
    uint8_t payload = 0;
    unsigned int index;

    c35_trace_init(&left, 2);
    right = left;
    for (index = 0; index < sizeof(corrupt) / sizeof(corrupt[0]); ++index) {
        left = right;
        left.length = corrupt[index];
        CHECK(!c35_trace_valid(&left));
        CHECK(c35_trace_hash(&left) == 0);
        CHECK(!c35_trace_equal(&left, &right));
        CHECK(c35_trace_append(&left, &publication) == C35_INVALID);
        CHECK(c35_trace_append_projection(
            &left, 0x80, &payload, 1) == C35_INVALID);
    }
    return 1;
}

static int test_reservation_and_capacity(void)
{
    struct c35_trace trace;
    struct c35_trace_reservation reservation;
    enum c35_observation_state state;
    unsigned int index;

    c35_trace_init(&trace, 3);
    {
        struct c35_trace before = trace;

        CHECK(c35_trace_reserve(
            &trace, 1, C35_TRACE_EVENT_BYTES - 1u, &reservation) ==
              C35_INVALID);
        CHECK(memcmp(&trace, &before, sizeof(trace)) == 0);
    }
    for (index = 1; index <= 682; ++index) {
        struct c35_publication publication = publication_make(index);

        CHECK(c35_trace_reserve(
            &trace, index, C35_TRACE_EVENT_BYTES, &reservation) == C35_OK);
        if (index == 1) {
            struct c35_trace before = trace;
            struct c35_trace_reservation repeated;

            CHECK(c35_trace_reserve(
                &trace, index, C35_TRACE_EVENT_BYTES, &repeated) == C35_OK);
            CHECK(memcmp(&trace, &before, sizeof(trace)) == 0);
            CHECK(memcmp(&repeated, &reservation, sizeof(repeated)) == 0);
        }
        CHECK(c35_trace_query(&trace, &reservation, &state) == C35_OK);
        CHECK(state == C35_OBSERVATION_RESERVED);
        CHECK(c35_trace_commit(
            &trace, &reservation, &publication) == C35_OK);
        CHECK(c35_trace_query(&trace, &reservation, &state) == C35_OK);
        CHECK(state == C35_OBSERVATION_RECORDED);
        if (index == 1) {
            struct c35_trace before = trace;
            struct c35_trace_reservation repeated;
            struct c35_publication different = publication;

            CHECK(c35_trace_reserve(
                &trace, index, C35_TRACE_EVENT_BYTES, &repeated) == C35_OK);
            CHECK(repeated.state == C35_OBSERVATION_RECORDED);
            CHECK(c35_trace_commit(&trace, &repeated, &publication) == C35_OK);
            CHECK(memcmp(&trace, &before, sizeof(trace)) == 0);
            different.actor ^= 1u;
            CHECK(c35_trace_commit(&trace, &repeated, &different) ==
                  C35_CORRUPT);
            CHECK(memcmp(&trace, &before, sizeof(trace)) == 0);
        }
    }
    CHECK(trace.events == 682);
    CHECK(trace.length == C35_TRACE_HEADER_BYTES +
                              682u * C35_TRACE_EVENT_BYTES);
    CHECK(c35_trace_valid(&trace));
    CHECK(c35_trace_reserve(
        &trace, 683, C35_TRACE_EVENT_BYTES, &reservation) ==
          C35_NO_CAPACITY);
    return 1;
}

static int test_rejected_property(void)
{
    struct c35_trace trace;
    struct c35_trace before;
    uint8_t *payload = malloc(UINT16_MAX);
    uint32_t iteration;

    CHECK(payload != NULL);
    memset(payload, 0x96, UINT16_MAX);
    c35_trace_init(&trace, 4);
    for (iteration = 0; iteration < 5000; ++iteration) {
        uint32_t length = (iteration * 1103515245u + 12345u) & 0xffffu;
        enum c35_result result;

        before = trace;
        result = c35_trace_append_projection(
            &trace, 0x81, payload, length);
        if (result != C35_OK) {
            CHECK(memcmp(&before, &trace, sizeof(trace)) == 0);
        } else {
            CHECK(trace.length <= C35_TRACE_BYTES);
        }
        if (trace.length == C35_TRACE_BYTES) {
            break;
        }
    }
    free(payload);
    return 1;
}

int main(void)
{
    CHECK(test_projection_boundaries());
    CHECK(test_corrupt_lengths());
    CHECK(test_reservation_and_capacity());
    CHECK(test_rejected_property());
    puts("C3.5a trace/observer bounds: PASS");
    return 0;
}
