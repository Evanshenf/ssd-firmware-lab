/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_oracle.h"

#include <string.h>

struct family {
    uint32_t enabled;
};

static const struct family families[C34O_FAMILIES] = {
    {C34O_HOST_CACHE},
    {C34O_CAPTURE | C34O_DATA_C | C34O_MAP_C},
    {C34O_CAPTURE | C34O_DATA_C | C34O_MAP_C |
     C34O_VOLATILE_SUCCESS | C34O_DURABLE_SUCCESS},
    {C34O_CAPTURE | C34O_DATA_C | C34O_MAP_C | C34O_NFC_EVENT},
    {C34O_CAPTURE | C34O_DATA_C | C34O_MAP_C | C34O_TORN_DATA},
    {C34O_CAPTURE | C34O_TRIM_C | C34O_VOLATILE_SUCCESS |
     C34O_DURABLE_SUCCESS},
    {C34O_CAPTURE | C34O_TRIM_C | C34O_DATA_C | C34O_MAP_C},
    {C34O_CAPTURE | C34O_DATA_C | C34O_MAP_C | C34O_FENCE_SUCCESS},
    {C34O_CAPTURE | C34O_DATA_C | C34O_MAP_C |
     C34O_CHECKPOINT_IMAGE | C34O_CHECKPOINT_ANCHOR},
    {C34O_CAPTURE | C34O_DATA_C | C34O_MAP_C | C34O_GC_COPY |
     C34O_RELOCATION_C | C34O_RELEASE_LEASE | C34O_ERASE_SOURCE},
    {C34O_CAPTURE | C34O_DATA_C | C34O_MAP_C | C34O_TORN_MAP |
     C34O_SYNC},
    {C34O_CAPTURE | C34O_DATA_C | C34O_MAP_C | C34O_DURABLE_SUCCESS |
     C34O_NFC_EVENT | C34O_SYNC},
    {C34O_CAPTURE | C34O_DATA_C | C34O_MAP_C | C34O_RESET |
     C34O_OLD_EVENT | C34O_HOST_CACHE | C34O_SECOND_INSTANCE},
};

static uint32_t dependencies(uint32_t action)
{
    switch (action) {
    case C34O_DATA_C:
    case C34O_TRIM_C:
    case C34O_VOLATILE_SUCCESS:
    case C34O_TORN_DATA:
        return C34O_CAPTURE;
    case C34O_MAP_C:
        return C34O_DATA_C;
    case C34O_DURABLE_SUCCESS:
        return C34O_MAP_C | C34O_CAPTURE;
    case C34O_GC_COPY:
        return C34O_MAP_C;
    case C34O_RELOCATION_C:
        return C34O_GC_COPY | C34O_MAP_C;
    case C34O_RELEASE_LEASE:
        return C34O_RELOCATION_C;
    case C34O_ERASE_SOURCE:
        return C34O_RELEASE_LEASE;
    case C34O_CHECKPOINT_IMAGE:
        return C34O_MAP_C;
    case C34O_CHECKPOINT_ANCHOR:
        return C34O_CHECKPOINT_IMAGE;
    case C34O_FENCE_SUCCESS:
        return C34O_MAP_C;
    case C34O_OLD_EVENT:
        return C34O_RESET;
    case C34O_NFC_EVENT:
        return C34O_CAPTURE;
    case C34O_TORN_MAP:
        return C34O_DATA_C;
    default:
        return 0;
    }
}

static uint32_t bit_count(uint32_t value)
{
    uint32_t count = 0;

    while (value != 0) {
        count += value & 1u;
        value >>= 1;
    }
    return count;
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
    unsigned int index;

    for (index = 0; index < 4; ++index) {
        hash ^= (uint8_t)(value >> (index * 8u));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int state_valid(uint32_t state)
{
    uint32_t bit;

    for (bit = 1; bit != 0 && bit <= C34O_SECOND_INSTANCE; bit <<= 1) {
        if ((state & bit) != 0 &&
            (state & dependencies(bit)) != dependencies(bit)) {
            return 0;
        }
    }
    return 1;
}

static uint32_t positive_violation(uint32_t state)
{
    uint32_t violation = 0;

    if ((state & C34O_MAP_C) != 0 && (state & C34O_DATA_C) == 0) {
        violation |= UINT32_C(1) << 2;
    }
    if ((state & C34O_DURABLE_SUCCESS) != 0 &&
        (state & C34O_MAP_C) == 0) {
        violation |= UINT32_C(1) << 3;
    }
    if ((state & C34O_ERASE_SOURCE) != 0 &&
        (state & (C34O_RELOCATION_C | C34O_RELEASE_LEASE)) !=
            (C34O_RELOCATION_C | C34O_RELEASE_LEASE)) {
        violation |= UINT32_C(1) << 6;
    }
    if ((state & C34O_CHECKPOINT_ANCHOR) != 0 &&
        (state & C34O_CHECKPOINT_IMAGE) == 0) {
        violation |= UINT32_C(1) << 7;
    }
    if ((state & C34O_FENCE_SUCCESS) != 0 &&
        (state & (C34O_MAP_C | C34O_TRIM_C)) == 0) {
        violation |= UINT32_C(1) << 9;
    }
    return violation;
}

static int broken_trigger(enum c34o_broken broken, uint32_t state)
{
    switch (broken) {
    case C34O_BM_UNIQUE:
        return (state & (C34O_MAP_C | C34O_TRIM_C)) != 0;
    case C34O_BM_TORN:
        return (state & (C34O_TORN_DATA | C34O_TORN_MAP)) != 0;
    case C34O_BM_DEPEND:
        return (state & C34O_DATA_C) != 0;
    case C34O_BM_DURABLE:
        return (state & C34O_DURABLE_SUCCESS) != 0;
    case C34O_BM_VOLATILE:
        return (state & C34O_VOLATILE_SUCCESS) != 0;
    case C34O_BM_TRIM:
        return (state & C34O_TRIM_C) != 0;
    case C34O_BM_GC:
        return (state & C34O_GC_COPY) != 0;
    case C34O_BM_CHECKPOINT:
        return (state & C34O_CHECKPOINT_IMAGE) != 0;
    case C34O_BM_EPOCH:
        return (state & (C34O_RESET | C34O_OLD_EVENT)) ==
               (C34O_RESET | C34O_OLD_EVENT);
    case C34O_BM_FENCE:
        return (state & C34O_CAPTURE) != 0;
    case C34O_BM_PLP:
        return (state & C34O_VOLATILE_SUCCESS) != 0;
    case C34O_BM_CONSERVE:
        return (state & C34O_DATA_C) != 0;
    case C34O_BM_HOST:
        return (state & C34O_HOST_CACHE) != 0;
    case C34O_BM_WITNESS:
        return (state & C34O_NFC_EVENT) != 0;
    case C34O_BM_PHYS_FACT:
        return (state & C34O_SYNC) != 0;
    case C34O_BM_OWNERSHIP:
        return (state & (C34O_VOLATILE_SUCCESS | C34O_CAPTURE)) ==
               (C34O_VOLATILE_SUCCESS | C34O_CAPTURE);
    case C34O_BM_REPLACE:
        return (state & C34O_MAP_C) != 0;
    case C34O_BM_INSTANCE:
        return (state & C34O_SECOND_INSTANCE) != 0;
    default:
        return 0;
    }
}

static int enumerate_family(
    unsigned int family,
    enum c34o_broken broken,
    struct c34o_metrics *metrics,
    uint32_t *minimum,
    uint64_t *counterexample_hash
)
{
    uint32_t queue[4096];
    uint8_t visited[1u << 21];
    size_t head = 0;
    size_t tail = 0;
    uint32_t enabled = families[family].enabled;

    memset(visited, 0, sizeof(visited));
    queue[tail++] = 0;
    visited[0] = 1;
    while (head < tail) {
        uint32_t state = queue[head++];
        uint32_t action;
        uint32_t depth = bit_count(state);

        if (!state_valid(state) || positive_violation(state) != 0) {
            return 0;
        }
        if (broken != C34O_BROKEN_NONE && broken_trigger(broken, state)) {
            *minimum = depth;
            *counterexample_hash = hash_u32(
                hash_u32(UINT64_C(1469598103934665603), family), state);
            return 1;
        }
        if (metrics != NULL) {
            unsigned int event;

            ++metrics->states;
            metrics->hash = hash_u32(
                hash_u32(metrics->hash, family), state);
            for (event = 0; event < 3; ++event) {
                uint32_t recovered_new =
                    (state & C34O_MAP_C) != 0 &&
                    (state & C34O_TORN_MAP) == 0;
                uint32_t recovered_trim = (state & C34O_TRIM_C) != 0;

                ++metrics->cuts;
                metrics->hash = hash_u32(
                    metrics->hash,
                    state ^ (event << 24) ^ (recovered_new << 28) ^
                        (recovered_trim << 29));
            }
        }
        for (action = 1; action != 0 && action <= C34O_SECOND_INSTANCE;
             action <<= 1) {
            uint32_t next;

            if ((enabled & action) == 0 || (state & action) != 0 ||
                (state & dependencies(action)) != dependencies(action)) {
                continue;
            }
            next = state | action;
            if (!visited[next]) {
                if (tail >= sizeof(queue) / sizeof(queue[0])) {
                    return 0;
                }
                visited[next] = 1;
                queue[tail++] = next;
            }
        }
    }
    if (metrics != NULL) {
        ++metrics->terminals;
    }
    return broken == C34O_BROKEN_NONE;
}

int c34o_run_positive(struct c34o_metrics *metrics)
{
    unsigned int family;

    if (metrics == NULL) {
        return 0;
    }
    memset(metrics, 0, sizeof(*metrics));
    metrics->families = C34O_FAMILIES;
    metrics->invariant_mask = (UINT32_C(1) << C34O_INVARIANTS) - 1u;
    metrics->hash = UINT64_C(1469598103934665603);
    for (family = 0; family < C34O_FAMILIES; ++family) {
        uint32_t minimum = 0;
        uint64_t counterexample = 0;

        if (!enumerate_family(family, C34O_BROKEN_NONE, metrics, &minimum,
                              &counterexample)) {
            return 0;
        }
    }
    return 1;
}

int c34o_run_broken(
    enum c34o_broken broken,
    uint32_t *minimal_depth,
    uint64_t *counterexample_hash
)
{
    unsigned int family;

    if (broken <= C34O_BROKEN_NONE || broken > C34O_BM_INSTANCE ||
        minimal_depth == NULL || counterexample_hash == NULL) {
        return 0;
    }
    for (family = 0; family < C34O_FAMILIES; ++family) {
        if (enumerate_family(family, broken, NULL, minimal_depth,
                             counterexample_hash)) {
            return 1;
        }
    }
    return 0;
}
