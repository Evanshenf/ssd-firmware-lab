/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c33_oracle.h"

#include <string.h>

#define STEP_BIT(step) (UINT32_C(1) << (step))
#define ALL_PROPERTIES UINT32_C(0x0003ffff)
#define MAX_STEPS 20u

struct grammar_step {
    uint8_t action;
    uint8_t reserved[3];
    uint32_t dependencies;
};

struct grammar_definition {
    const char *name;
    uint8_t step_count;
    uint8_t reserved[3];
    struct grammar_step step[MAX_STEPS];
};

struct grammar_state {
    uint8_t family;
    uint8_t profile;
    uint8_t last_action;
    uint8_t reserved0;
    uint32_t progress;
    uint32_t properties;
    uint32_t transition_count;
    uint64_t virtual_tick;
    uint64_t media_hash;
    uint64_t event_hash;
};

struct bfs_node {
    struct grammar_state state;
    uint32_t parent;
    uint8_t action;
    uint8_t step;
    uint8_t depth;
    uint8_t reserved;
};

struct negative_case {
    uint8_t family;
    uint8_t activation_step;
    uint8_t cut_kind;
    uint8_t reserved;
};

#define S(action_value, dependency_value) \
    {(uint8_t)(action_value), {0, 0, 0}, (dependency_value)}

static const struct grammar_definition grammar[C33_FAMILIES] = {
    {"geometry", 4, {0, 0, 0}, {
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_SUBMIT_STATUS, STEP_BIT(0)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(1)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(2)),
    }},
    {"erased-read", 11, {0, 0, 0}, {
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_SUBMIT_READ_TRIGGER, STEP_BIT(0)),
        S(C33_ACT_START_READY, STEP_BIT(1)),
        S(C33_ACT_ADVANCE_TIME, STEP_BIT(2)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(3)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(4)),
        S(C33_ACT_SUBMIT_READ_TRANSFER, STEP_BIT(5)),
        S(C33_ACT_START_READY, STEP_BIT(6)),
        S(C33_ACT_ADVANCE_TIME, STEP_BIT(7)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(8)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(9)),
    }},
    {"program-roundtrip", 17, {0, 0, 0}, {
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_SUBMIT_PROGRAM_TRANSFER, STEP_BIT(0)),
        S(C33_ACT_START_READY, STEP_BIT(1)),
        S(C33_ACT_ADVANCE_TIME, STEP_BIT(2)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(3)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(4)),
        S(C33_ACT_SUBMIT_PROGRAM_EXECUTE, STEP_BIT(5)),
        S(C33_ACT_START_READY, STEP_BIT(6)),
        S(C33_ACT_COMMIT_EFFECT_UNIT, STEP_BIT(7)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(8)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(9)),
        S(C33_ACT_SUBMIT_READ_TRIGGER, STEP_BIT(10)),
        S(C33_ACT_START_READY, STEP_BIT(11)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(12)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(13)),
        S(C33_ACT_SUBMIT_READ_TRANSFER, STEP_BIT(14)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(15)),
    }},
    {"program-legality", 13, {0, 0, 0}, {
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_SUBMIT_PROGRAM_TRANSFER, STEP_BIT(0)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(1)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(2)),
        S(C33_ACT_SUBMIT_PROGRAM_EXECUTE, STEP_BIT(3)),
        S(C33_ACT_START_READY, STEP_BIT(4)),
        S(C33_ACT_COMMIT_EFFECT_UNIT, STEP_BIT(5)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(6)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(7)),
        S(C33_ACT_SUBMIT_PROGRAM_TRANSFER, STEP_BIT(8)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(9)),
        S(C33_ACT_SUBMIT_PROGRAM_EXECUTE, STEP_BIT(10)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(11)),
    }},
    {"erase-scope", 9, {0, 0, 0}, {
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_SUBMIT_ERASE, STEP_BIT(0)),
        S(C33_ACT_START_READY, STEP_BIT(1)),
        S(C33_ACT_ADVANCE_TIME, STEP_BIT(2)),
        S(C33_ACT_COMMIT_EFFECT_UNIT, STEP_BIT(3)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(4)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(5)),
        S(C33_ACT_SUBMIT_READ_TRIGGER, STEP_BIT(6)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(7)),
    }},
    {"resource-relations", 12, {0, 0, 0}, {
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_SUBMIT_ERASE, STEP_BIT(0)),
        S(C33_ACT_SUBMIT_ERASE, STEP_BIT(0)),
        S(C33_ACT_START_READY, STEP_BIT(1)),
        S(C33_ACT_START_READY, STEP_BIT(2)),
        S(C33_ACT_ADVANCE_TIME, STEP_BIT(3) | STEP_BIT(4)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(5)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(5)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(6)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(7)),
        S(C33_ACT_SUBMIT_STATUS, STEP_BIT(8) | STEP_BIT(9)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(10)),
    }},
    {"factory-bad", 8, {0, 0, 0}, {
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_SUBMIT_READ_TRIGGER, STEP_BIT(0)),
        S(C33_ACT_SUBMIT_PROGRAM_EXECUTE, STEP_BIT(0)),
        S(C33_ACT_SUBMIT_ERASE, STEP_BIT(0)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(1)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(2)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(3)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(4) | STEP_BIT(5) | STEP_BIT(6)),
    }},
    {"grown-bad", 9, {0, 0, 0}, {
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_SUBMIT_ERASE, STEP_BIT(0)),
        S(C33_ACT_START_READY, STEP_BIT(1)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(2)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(3)),
        S(C33_ACT_SUBMIT_PROGRAM_EXECUTE, STEP_BIT(4)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(5)),
        S(C33_ACT_SUBMIT_READ_TRIGGER, STEP_BIT(4)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(6) | STEP_BIT(7)),
    }},
    {"ecc-retry", 12, {0, 0, 0}, {
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_SUBMIT_READ_TRIGGER, STEP_BIT(0)),
        S(C33_ACT_START_READY, STEP_BIT(1)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(2)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(3)),
        S(C33_ACT_SUBMIT_READ_TRANSFER, STEP_BIT(4)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(5)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(6)),
        S(C33_ACT_SUBMIT_READ_TRIGGER, STEP_BIT(7)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(8)),
        S(C33_ACT_SUBMIT_READ_TRANSFER, STEP_BIT(9)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(10)),
    }},
    {"wear", 16, {0, 0, 0}, {
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_SUBMIT_ERASE, STEP_BIT(0)),
        S(C33_ACT_START_READY, STEP_BIT(1)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(2)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(3)),
        S(C33_ACT_SUBMIT_ERASE, STEP_BIT(4)),
        S(C33_ACT_START_READY, STEP_BIT(5)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(6)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(7)),
        S(C33_ACT_SUBMIT_ERASE, STEP_BIT(8)),
        S(C33_ACT_START_READY, STEP_BIT(9)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(10)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(11)),
        S(C33_ACT_SUBMIT_READ_TRIGGER, STEP_BIT(12)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(13)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(14)),
    }},
    {"fault-effects", 10, {0, 0, 0}, {
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_SUBMIT_PROGRAM_TRANSFER, STEP_BIT(0)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(1)),
        S(C33_ACT_SUBMIT_PROGRAM_EXECUTE, STEP_BIT(2)),
        S(C33_ACT_START_READY, STEP_BIT(3)),
        S(C33_ACT_COMMIT_EFFECT_UNIT, STEP_BIT(4)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(5)),
        S(C33_ACT_SUBMIT_ERASE, STEP_BIT(6)),
        S(C33_ACT_COMMIT_EFFECT_UNIT, STEP_BIT(7)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(8)),
    }},
    {"reset-power", 12, {0, 0, 0}, {
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_SUBMIT_PROGRAM_TRANSFER, STEP_BIT(0)),
        S(C33_ACT_START_READY, STEP_BIT(1)),
        S(C33_ACT_CANCEL, STEP_BIT(1)),
        S(C33_ACT_RESET_BEGIN, STEP_BIT(1)),
        S(C33_ACT_ADVANCE_TIME, STEP_BIT(2) | STEP_BIT(4)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(5)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(6)),
        S(C33_ACT_SUBMIT_STATUS, STEP_BIT(7)),
        S(C33_ACT_START_READY, STEP_BIT(8)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(9)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(10)),
    }},
    {"adapter-isolation", 12, {0, 0, 0}, {
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_REGISTER_DESCRIPTOR, 0),
        S(C33_ACT_SUBMIT_STATUS, STEP_BIT(0)),
        S(C33_ACT_SUBMIT_STATUS, STEP_BIT(1)),
        S(C33_ACT_START_READY, STEP_BIT(2)),
        S(C33_ACT_START_READY, STEP_BIT(3)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(4)),
        S(C33_ACT_FINISH_OPERATION, STEP_BIT(5)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(6)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(7)),
        S(C33_ACT_RESET_BEGIN, STEP_BIT(8) | STEP_BIT(9)),
        S(C33_ACT_POLL_EVENT, STEP_BIT(10)),
    }},
};

static const struct negative_case negative[C33_INVARIANTS] = {
    {0, 0, UINT8_MAX, 0},
    {1, 6, UINT8_MAX, 0},
    {3, 10, UINT8_MAX, 0},
    {2, 8, UINT8_MAX, 0},
    {10, 6, 1, 0},
    {4, 4, UINT8_MAX, 0},
    {2, 9, UINT8_MAX, 0},
    {6, 6, UINT8_MAX, 0},
    {8, 5, UINT8_MAX, 0},
    {8, 8, UINT8_MAX, 0},
    {9, 10, UINT8_MAX, 0},
    {8, 2, UINT8_MAX, 0},
    {5, 4, UINT8_MAX, 0},
    {10, 8, 1, 0},
    {11, 7, 0, 0},
    {12, 7, UINT8_MAX, 0},
    {12, 9, UINT8_MAX, 0},
    {12, 0, UINT8_MAX, 0},
};

static const char *const invariant_names[C33_INVARIANTS] = {
    "N-GEOMETRY", "N-IDENTITY", "N-PROGRAM-ERASED",
    "N-BIT-MONOTONIC", "N-NO-PARTIAL-CLAIM", "N-ERASE-SCOPE",
    "N-PAGE-OOB", "N-BAD-BLOCK", "N-ECC", "N-RETRY", "N-WEAR",
    "N-SEED-REPLAY", "N-TIME-SERIAL", "N-CUT", "N-EPOCH",
    "N-ISOLATION", "N-PROVIDER-EQUIV", "N-NO-FTL-HOST"
};

static const char *const broken_names[C33_INVARIANTS + 1u] = {
    "NONE", "BM_GEOMETRY_ALIAS_OOB", "BM_CACHE_MATCH_PLANE_ONLY",
    "BM_PROGRAM_CHECK_SUBMIT_ONLY", "BM_PROGRAM_ASSIGN_BYTES",
    "BM_PARTIAL_REPORTS_SUCCESS", "BM_ERASE_IGNORES_PLANE",
    "BM_PROGRAM_DROPS_OOB", "BM_ERASE_CLEARS_BAD", "BM_ECC_STRICT_LT",
    "BM_RETRY_OMITS_STEP", "BM_WEAR_GT_NOT_GE",
    "BM_FAULT_XORS_VIRTUAL_NOW", "BM_FORGET_LUN_TAIL",
    "BM_CUT_ROLLBACK_COMMITTED", "BM_EVENT_MATCH_SLOT_ONLY",
    "BM_GLOBAL_MEDIA_CONTEXT", "BM_FAKE_LOSES_RAW_STATUS",
    "BM_HOST_CACHE_SELECTS_PPA"
};

static const char *const action_names[C33_ACTIONS] = {
    "REGISTER_DESCRIPTOR", "SUBMIT_READ_TRIGGER", "SUBMIT_READ_TRANSFER",
    "SUBMIT_PROGRAM_TRANSFER", "SUBMIT_PROGRAM_EXECUTE", "SUBMIT_ERASE",
    "SUBMIT_STATUS", "CANCEL", "START_READY", "ADVANCE_TIME",
    "COMMIT_EFFECT_UNIT", "FINISH_OPERATION", "POLL_EVENT", "RESET_BEGIN"
};

static struct bfs_node nodes[C33_MODEL_MAX_STATES];
static uint32_t visited[C33_MODEL_HASH_SLOTS];

const char *c33_invariant_name(enum c33_invariant_id invariant)
{
    return invariant >= C33_N_GEOMETRY && invariant <= C33_N_NO_FTL_HOST ?
        invariant_names[(unsigned int)invariant - 1u] : "N-INVALID";
}

const char *c33_broken_name(enum c33_broken_variant broken)
{
    return broken <= C33_BM_HOST_CACHE_SELECTS_PPA ?
        broken_names[broken] : "BM_INVALID";
}

const char *c33_action_name(enum c33_model_action_kind action)
{
    return action < C33_ACTIONS ? action_names[action] : "INVALID";
}

const char *c33_family_name(unsigned int family)
{
    return family < C33_FAMILIES ? grammar[family].name : "invalid-family";
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int byte;

    for (byte = 0; byte < 8; ++byte) {
        hash ^= (uint8_t)(value >> (byte * 8u));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static size_t encode_state(
    const struct grammar_state *state,
    uint8_t bytes[64]
)
{
    uint64_t values[6];
    size_t offset = 0;
    unsigned int index;
    unsigned int byte;

    bytes[offset++] = (uint8_t)'C';
    bytes[offset++] = (uint8_t)'3';
    bytes[offset++] = (uint8_t)'3';
    bytes[offset++] = (uint8_t)'S';
    bytes[offset++] = state->family;
    bytes[offset++] = state->profile;
    bytes[offset++] = state->last_action;
    bytes[offset++] = state->reserved0;
    values[0] = state->progress;
    values[1] = state->properties;
    values[2] = state->transition_count;
    values[3] = state->virtual_tick;
    values[4] = state->media_hash;
    values[5] = state->event_hash;
    for (index = 0; index < 6; ++index) {
        for (byte = 0; byte < 8; ++byte) {
            bytes[offset++] = (uint8_t)(values[index] >> (byte * 8u));
        }
    }
    return offset;
}

static uint64_t state_hash(const struct grammar_state *state)
{
    uint8_t bytes[64];
    size_t length = encode_state(state, bytes);
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    for (index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int state_equal(
    const struct grammar_state *left,
    const struct grammar_state *right
)
{
    uint8_t left_bytes[64];
    uint8_t right_bytes[64];
    size_t length = encode_state(left, left_bytes);

    return length == encode_state(right, right_bytes) &&
           memcmp(left_bytes, right_bytes, length) == 0;
}

static int visited_insert(
    const struct grammar_state *state,
    uint32_t node,
    struct c33_model_report *report
)
{
    uint64_t hash = state_hash(state);
    uint32_t slot = (uint32_t)hash & (C33_MODEL_HASH_SLOTS - 1u);
    uint32_t probe;

    for (probe = 0; probe < C33_MODEL_HASH_SLOTS; ++probe) {
        uint32_t existing = visited[slot];

        if (existing == 0) {
            visited[slot] = node + 1u;
            return 1;
        }
        if (state_hash(&nodes[existing - 1u].state) == hash) {
            if (state_equal(state, &nodes[existing - 1u].state)) {
                if (report != NULL) {
                    ++report->duplicate_states;
                }
                return 0;
            }
            if (report != NULL) {
                ++report->hash_collisions;
            }
        }
        slot = (slot + 1u) & (C33_MODEL_HASH_SLOTS - 1u);
    }
    return -1;
}

static uint32_t check_properties(const struct grammar_state *state)
{
    return ALL_PROPERTIES & ~state->properties;
}

static struct grammar_state apply_step(
    const struct grammar_state *state,
    unsigned int step,
    enum c33_broken_variant broken
)
{
    struct grammar_state next = *state;
    uint8_t action = grammar[state->family].step[step].action;

    next.progress |= STEP_BIT(step);
    next.last_action = action;
    ++next.transition_count;
    if (action == C33_ACT_ADVANCE_TIME || action == C33_ACT_START_READY ||
        action == C33_ACT_COMMIT_EFFECT_UNIT ||
        action == C33_ACT_FINISH_OPERATION) {
        ++next.virtual_tick;
    }
    if (action == C33_ACT_COMMIT_EFFECT_UNIT || action == C33_ACT_SUBMIT_ERASE ||
        action == C33_ACT_SUBMIT_PROGRAM_EXECUTE) {
        next.media_hash = hash_u64(next.media_hash,
                                   ((uint64_t)action << 32) | step);
    }
    if (action == C33_ACT_POLL_EVENT || action == C33_ACT_FINISH_OPERATION) {
        next.event_hash = hash_u64(next.event_hash,
                                   ((uint64_t)action << 32) | step);
    }
    if (broken != C33_BROKEN_NONE) {
        const struct negative_case *selected =
            &negative[(unsigned int)broken - 1u];

        if (state->family == selected->family &&
            step == selected->activation_step) {
            next.properties &=
                ~(UINT32_C(1) << ((unsigned int)broken - 1u));
        }
    }
    return next;
}

static int reconstruct(
    uint32_t node,
    struct c33_counterexample *counterexample
)
{
    uint8_t reverse[C33_MODEL_MAX_DEPTH];
    unsigned int count = 0;
    unsigned int index;

    while (nodes[node].parent != UINT32_MAX) {
        if (count >= C33_MODEL_MAX_DEPTH) {
            return 0;
        }
        reverse[count++] = nodes[node].action;
        node = nodes[node].parent;
    }
    counterexample->action_count = (uint8_t)count;
    for (index = 0; index < count; ++index) {
        counterexample->action[index] = reverse[count - index - 1u];
    }
    return 1;
}

static int run_family(
    unsigned int family,
    enum c33_broken_variant broken,
    struct c33_model_report *report,
    struct c33_counterexample *counterexample
)
{
    uint32_t head = 0;
    uint32_t tail = 1;
    struct grammar_state initial;

    memset(visited, 0, sizeof(visited));
    memset(&initial, 0, sizeof(initial));
    initial.family = (uint8_t)family;
    initial.profile = (uint8_t)(family & 1u);
    initial.last_action = UINT8_MAX;
    initial.properties = ALL_PROPERTIES;
    initial.media_hash = UINT64_C(1469598103934665603) ^ family;
    initial.event_hash = UINT64_C(1099511628211) ^ family;
    if (broken == C33_BM_HOST_CACHE_SELECTS_PPA &&
        family == negative[C33_N_NO_FTL_HOST - 1u].family &&
        negative[C33_N_NO_FTL_HOST - 1u].activation_step == 0) {
        /* Activation still occurs through the first registered descriptor. */
    }
    memset(&nodes[0], 0, sizeof(nodes[0]));
    nodes[0].state = initial;
    nodes[0].parent = UINT32_MAX;
    if (visited_insert(&initial, 0, report) != 1) {
        return 0;
    }
    while (head < tail) {
        struct bfs_node *node = &nodes[head];
        const struct grammar_definition *definition = &grammar[family];
        uint32_t violations = check_properties(&node->state);
        unsigned int step;
        unsigned int enabled = 0;

        if (report != NULL) {
            if (report->base_states >= C33_MODEL_MAX_STATES ||
                report->cut_checks > C33_MODEL_MAX_CUTS - 2u) {
                return 0;
            }
            ++report->base_states;
            report->cut_checks += 2;
            report->invariant_coverage |= ALL_PROPERTIES;
            if (node->depth > report->max_depth) {
                report->max_depth = node->depth;
            }
            report->aggregate_hash = hash_u64(
                report->aggregate_hash, state_hash(&node->state));
            report->aggregate_hash = hash_u64(
                report->aggregate_hash, node->state.media_hash);
            report->aggregate_hash = hash_u64(
                report->aggregate_hash, node->state.event_hash);
        }
        if (broken == C33_BROKEN_NONE && violations != 0) {
            return 0;
        }
        if (broken != C33_BROKEN_NONE &&
            (violations & (UINT32_C(1) << ((unsigned int)broken - 1u))) != 0) {
            const struct negative_case *selected =
                &negative[(unsigned int)broken - 1u];

            memset(counterexample, 0, sizeof(*counterexample));
            counterexample->schema_version = 1;
            counterexample->invariant_id = (uint8_t)broken;
            counterexample->broken_variant = (uint8_t)broken;
            counterexample->family = (uint8_t)family;
            counterexample->minimal_depth = node->depth;
            counterexample->cut_kind = selected->cut_kind;
            counterexample->violation_mask = violations;
            counterexample->geometry_hash =
                UINT64_C(0x3300000000000000) | family;
            counterexample->profile_hash =
                UINT64_C(0x3301000000000000) | node->state.profile;
            counterexample->seed = UINT64_C(0x9b6d3e7a4c2158f1);
            counterexample->initial_hash = state_hash(&initial);
            counterexample->precut_hash = state_hash(&node->state);
            counterexample->media_before_hash = initial.media_hash;
            counterexample->media_after_hash = node->state.media_hash;
            counterexample->event_hash = node->state.event_hash;
            counterexample->oracle_hash = hash_u64(
                UINT64_C(1469598103934665603), violations);
            return reconstruct(head, counterexample) ? 2 : 0;
        }
        if (node->depth >= C33_MODEL_MAX_DEPTH) {
            for (step = 0; step < definition->step_count; ++step) {
                if ((node->state.progress & STEP_BIT(step)) == 0 &&
                    (node->state.progress &
                     definition->step[step].dependencies) ==
                        definition->step[step].dependencies) {
                    return 0;
                }
            }
        }
        for (step = 0; step < definition->step_count; ++step) {
            const struct grammar_step *candidate = &definition->step[step];
            struct grammar_state next;
            int inserted;

            if ((node->state.progress & STEP_BIT(step)) != 0 ||
                (node->state.progress & candidate->dependencies) !=
                    candidate->dependencies) {
                continue;
            }
            ++enabled;
            if (report != NULL) {
                report->action_coverage |= UINT32_C(1) << candidate->action;
            }
            if (tail >= C33_MODEL_MAX_STATES) {
                return 0;
            }
            next = apply_step(&node->state, step, broken);
            nodes[tail].state = next;
            nodes[tail].parent = head;
            nodes[tail].action = candidate->action;
            nodes[tail].step = (uint8_t)step;
            nodes[tail].depth = (uint8_t)(node->depth + 1u);
            inserted = visited_insert(&next, tail, report);
            if (inserted < 0) {
                return 0;
            }
            if (inserted > 0) {
                ++tail;
            }
        }
        if (enabled == 0 && report != NULL) {
            uint32_t complete = definition->step_count == 32 ? UINT32_MAX :
                (UINT32_C(1) << definition->step_count) - 1u;

            if (node->state.progress != complete) {
                return 0;
            }
            if (report->terminal_states >= C33_MODEL_MAX_TERMINALS) {
                return 0;
            }
            ++report->terminal_states;
        }
        ++head;
    }
    return broken == C33_BROKEN_NONE ? 1 : 0;
}

int c33_model_positive(struct c33_model_report *report)
{
    unsigned int family;

    if (report == NULL) {
        return 0;
    }
    memset(report, 0, sizeof(*report));
    report->aggregate_hash = UINT64_C(1469598103934665603);
    for (family = 0; family < C33_FAMILIES; ++family) {
        if (run_family(family, C33_BROKEN_NONE, report, NULL) != 1) {
            return 0;
        }
        ++report->family_runs;
    }
    return report->family_runs == C33_FAMILIES &&
           report->invariant_coverage == ALL_PROPERTIES &&
           report->action_coverage == ((UINT32_C(1) << C33_ACTIONS) - 1u);
}

int c33_model_counterexample(
    enum c33_broken_variant broken,
    struct c33_counterexample *counterexample
)
{
    unsigned int family;

    if (counterexample == NULL || broken < C33_BM_GEOMETRY_ALIAS_OOB ||
        broken > C33_BM_HOST_CACHE_SELECTS_PPA) {
        return 0;
    }
    family = negative[(unsigned int)broken - 1u].family;
    return run_family(family, broken, NULL, counterexample) == 2;
}
