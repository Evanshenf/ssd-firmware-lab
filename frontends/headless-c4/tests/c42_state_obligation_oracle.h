/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C42_STATE_OBLIGATION_ORACLE_H
#define FWLAB_C42_STATE_OBLIGATION_ORACLE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum c42_state_target_kind {
    C42_STATE_TARGET_FIELD = 1,
    C42_STATE_TARGET_IDENTITY_EDGE = 2,
    C42_STATE_TARGET_IDENTITY_DOMAIN = 3,
    C42_STATE_TARGET_TRANSITION = 4
};

enum c42_state_rule_kind {
    C42_STATE_RULE_EXACT = 1,
    C42_STATE_RULE_REQUIRED_ZERO = 2,
    C42_STATE_RULE_DERIVED_EQUAL = 3,
    C42_STATE_RULE_EQUAL = 4,
    C42_STATE_RULE_STABLE = 5,
    C42_STATE_RULE_INDEPENDENT = 6,
    C42_STATE_RULE_BOUNDED_TERMINAL = 7,
    C42_STATE_RULE_BOUNDED_SERVICE = 8
};

enum c42_state_operator_kind {
    C42_STATE_OPERATOR_FIELD_CORRUPT = 1,
    C42_STATE_OPERATOR_REQUIRED_ZERO_VIOLATION = 2,
    C42_STATE_OPERATOR_REQUIRED_OMISSION = 3,
    C42_STATE_OPERATOR_STALE_KEY = 4,
    C42_STATE_OPERATOR_INVALID_ENUM = 5,
    C42_STATE_OPERATOR_IDENTITY_SPLIT = 6,
    C42_STATE_OPERATOR_DOMAIN_COLLAPSE = 7,
    C42_STATE_OPERATOR_TRANSITION_SKIP = 8,
    C42_STATE_OPERATOR_TRANSITION_STALL = 9,
    C42_STATE_OPERATOR_TRANSITION_DUPLICATE = 10,
    C42_STATE_OPERATOR_EARLY_TERMINAL = 11,
    C42_STATE_OPERATOR_LATE_TERMINAL = 12
};

struct c42_state_obligation_stimulus {
    const char *obligation_id;
    const char *node_id;
    uint32_t target_kind;
    uint32_t rule_kind;
    uint32_t operator_kind;
    uint32_t element_count;
    uint32_t element_index;
    uint32_t rank_before;
    uint32_t rank_after;
    uint32_t maximum_work_units;
};

#include "c42_state_obligations.inc"

#define C42_STATE_MAX_ELEMENTS 64u

struct c42_state_semantic_probe {
    uint64_t expected[C42_STATE_MAX_ELEMENTS];
    uint64_t observed[C42_STATE_MAX_ELEMENTS];
    uint32_t transition_count;
    uint32_t rank_before;
    uint32_t rank_after;
    uint32_t terminal;
    uint32_t domain_left;
    uint32_t domain_right;
};

static int c42_state_probe_init(
    const struct c42_state_obligation_stimulus *stimulus,
    struct c42_state_semantic_probe *probe)
{
    uint32_t index;

    if (stimulus == NULL || probe == NULL || stimulus->element_count == 0 ||
        stimulus->element_count > C42_STATE_MAX_ELEMENTS ||
        (stimulus->element_index != UINT32_MAX &&
         stimulus->element_index >= stimulus->element_count)) {
        return 0;
    }
    memset(probe, 0, sizeof(*probe));
    for (index = 0; index < stimulus->element_count; ++index) {
        uint64_t value = UINT64_C(0x4300000000000100) + index;

        if (stimulus->rule_kind == C42_STATE_RULE_REQUIRED_ZERO) value = 0;
        probe->expected[index] = value;
        probe->observed[index] = value;
    }
    if (stimulus->target_kind == C42_STATE_TARGET_IDENTITY_EDGE) {
        probe->expected[0] = 17;
        probe->expected[1] = 17;
        probe->observed[0] = 17;
        probe->observed[1] = 17;
    } else if (stimulus->target_kind == C42_STATE_TARGET_IDENTITY_DOMAIN) {
        probe->expected[0] = 17;
        probe->expected[1] = 17;
        probe->observed[0] = 17;
        probe->observed[1] = 17;
        probe->domain_left = 1;
        probe->domain_right = 2;
    } else if (stimulus->target_kind == C42_STATE_TARGET_TRANSITION) {
        probe->transition_count = 1;
        probe->rank_before = stimulus->rank_before;
        probe->rank_after = stimulus->rank_after;
        probe->terminal = (uint32_t)(
            stimulus->rule_kind == C42_STATE_RULE_BOUNDED_TERMINAL &&
            stimulus->rank_after == 0
        );
    }
    return 1;
}

static int c42_state_probe_valid(
    const struct c42_state_obligation_stimulus *stimulus,
    const struct c42_state_semantic_probe *probe)
{
    uint32_t index;

    if (stimulus->target_kind == C42_STATE_TARGET_IDENTITY_EDGE) {
        return probe->observed[0] == probe->observed[1];
    }
    if (stimulus->target_kind == C42_STATE_TARGET_IDENTITY_DOMAIN) {
        return probe->domain_left != probe->domain_right;
    }
    if (stimulus->target_kind == C42_STATE_TARGET_TRANSITION) {
        if (probe->transition_count != 1 ||
            probe->rank_after >= probe->rank_before) return 0;
        if (stimulus->rule_kind == C42_STATE_RULE_BOUNDED_TERMINAL) {
            return probe->terminal == (uint32_t)(probe->rank_after == 0);
        }
        return probe->terminal == 0 && stimulus->maximum_work_units >= 1;
    }
    for (index = 0; index < stimulus->element_count; ++index) {
        if (stimulus->rule_kind == C42_STATE_RULE_REQUIRED_ZERO) {
            if (probe->observed[index] != 0) return 0;
        } else if (probe->observed[index] != probe->expected[index]) {
            return 0;
        }
    }
    return 1;
}

static int c42_state_probe_mutate(
    const struct c42_state_obligation_stimulus *stimulus,
    struct c42_state_semantic_probe *probe)
{
    uint32_t element = stimulus->element_index == UINT32_MAX ?
        0 : stimulus->element_index;

    switch (stimulus->operator_kind) {
    case C42_STATE_OPERATOR_FIELD_CORRUPT:
        probe->observed[element] ^= UINT64_C(0x100);
        break;
    case C42_STATE_OPERATOR_REQUIRED_ZERO_VIOLATION:
        probe->observed[element] = 1;
        break;
    case C42_STATE_OPERATOR_REQUIRED_OMISSION:
        probe->observed[element] = 0;
        break;
    case C42_STATE_OPERATOR_STALE_KEY:
        probe->observed[element]--;
        break;
    case C42_STATE_OPERATOR_INVALID_ENUM:
        probe->observed[element] = UINT64_MAX;
        break;
    case C42_STATE_OPERATOR_IDENTITY_SPLIT:
        probe->observed[1]++;
        break;
    case C42_STATE_OPERATOR_DOMAIN_COLLAPSE:
        probe->domain_right = probe->domain_left;
        break;
    case C42_STATE_OPERATOR_TRANSITION_SKIP:
        probe->transition_count = 0;
        break;
    case C42_STATE_OPERATOR_TRANSITION_STALL:
        probe->rank_after = probe->rank_before;
        break;
    case C42_STATE_OPERATOR_TRANSITION_DUPLICATE:
        probe->transition_count = 2;
        break;
    case C42_STATE_OPERATOR_EARLY_TERMINAL:
        probe->terminal = 1;
        break;
    case C42_STATE_OPERATOR_LATE_TERMINAL:
        probe->terminal = 0;
        break;
    default:
        return 0;
    }
    return 1;
}

static int c42_state_obligations_run(uint32_t *kills)
{
    uint32_t index;
    uint32_t count = 0;

    if (kills == NULL) return 0;
    for (index = 0; index < C42_STATE_OBLIGATION_COUNT; ++index) {
        const struct c42_state_obligation_stimulus *stimulus =
            &c42_state_obligations[index];
        struct c42_state_semantic_probe probe;

        if (!c42_state_probe_init(stimulus, &probe) ||
            !c42_state_probe_valid(stimulus, &probe)) {
            fprintf(stderr, "state obligation baseline FAIL: %s node=%s\n",
                    stimulus->obligation_id, stimulus->node_id);
            return 0;
        }
        if (!c42_state_probe_mutate(stimulus, &probe) ||
            c42_state_probe_valid(stimulus, &probe)) {
            fprintf(stderr, "state obligation survived: %s node=%s\n",
                    stimulus->obligation_id, stimulus->node_id);
            return 0;
        }
        count++;
    }
    *kills = count;
    return count == C42_STATE_OBLIGATION_COUNT;
}

#endif
