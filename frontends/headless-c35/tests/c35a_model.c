/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35a_model.h"

#include <stdio.h>
#include <string.h>

struct model_state {
    uint8_t stage;
    uint8_t x;
    uint8_t y;
    uint8_t z;
    uint8_t w;
    uint8_t terminal;
    uint8_t branch;
    uint8_t reserved;
};

struct successor {
    struct model_state state;
    const char *action;
};

struct queue_entry {
    struct model_state state;
    uint16_t parent;
    uint8_t depth;
    uint8_t reserved;
    const char *action;
};

static const char *const invariant_names[C35A_MODEL_FAMILIES] = {
    "A-TRACE-CHECKED",
    "A-EVIDENCE-NONAUTH",
    "A-REGISTER-COMPENSATE",
    "A-LEASE-RECONCILE",
    "A-CONSUME-RECONCILE",
    "A-ACK-IDEMPOTENT",
    "A-RESET-RESUMABLE",
    "A-TEARDOWN-RESUMABLE",
    "A-FAULTED-CLEANUP",
    "A-EXHAUSTION-EXPLICIT",
    "A-BINDING-TABLE",
    "A-BUNDLE-TABLE",
    "A-PROFILE-COMPAT",
    "A-FINALIZE-IDEMPOTENT",
};

static const char *const mutation_names[C35A_MODEL_FAMILIES] = {
    "BM_TRACE_SUBTRACT_BEFORE_CHECK",
    "BM_TRACE_FAILURE_BLOCKS_FINALIZE",
    "BM_REGISTER_MUTATION_LEAK",
    "BM_RELEASE_ERROR_FORGETS_LEASE",
    "BM_CONSUME_ERROR_REPLAYS_PUBLICATION",
    "BM_ACK_AFTER_MUTATION_UNKNOWN",
    "BM_RESET_ZERO_BUDGET_WEDGES",
    "BM_TEARDOWN_PARTIAL_WEDGES",
    "BM_ADMISSION_GATES_FAULTED_TEARDOWN",
    "BM_COUNTER_IS_CAPACITY",
    "BM_BINDING_NULL_CALLBACK",
    "BM_BUNDLE_NULL_QUIESCENT",
    "BM_CONTEXT_EQUALS_PROFILE",
    "BM_RELEASE_AFTER_MUTATION_RETRIES_RAW",
};

const char *c35a_invariant_name(unsigned int family)
{
    return family < C35A_MODEL_FAMILIES ? invariant_names[family] : NULL;
}

const char *c35a_mutation_name(unsigned int family)
{
    return family < C35A_MODEL_FAMILIES ? mutation_names[family] : NULL;
}

static void add_successor(
    struct successor out[C35A_MODEL_SUCCESSOR_CAP],
    unsigned int *count,
    const struct model_state *state,
    const char *action
)
{
    if (*count >= C35A_MODEL_SUCCESSOR_CAP) return;
    out[*count].state = *state;
    out[*count].action = action;
    ++*count;
}

static unsigned int successors(
    unsigned int family,
    const struct model_state *source,
    int mutant,
    struct successor out[C35A_MODEL_SUCCESSOR_CAP]
)
{
    struct model_state next = *source;
    unsigned int count = 0;

    if (source->terminal) return 0;
    switch (family) {
    case 0: /* checked trace rejection */
        next.stage = 1;
        next.y = 1; /* rejected */
        next.x = mutant ? 1 : 0; /* bytes/length mutated */
        next.terminal = 1;
        add_successor(out, &count, &next, "append-oversize");
        next = *source;
        next.stage = 2;
        next.x = 1; /* a valid append may mutate */
        next.terminal = 1;
        add_successor(out, &count, &next, "append-valid");
        break;
    case 1: /* evidence is not authoritative */
        if (source->stage == 0) {
            next.stage = 1;
            next.x = 1; /* authoritative commit */
            add_successor(out, &count, &next, "commit-operation");
        } else if (source->stage == 1) {
            next.stage = 2;
            next.y = 1; /* observer failed */
            if (mutant) next.x = 0;
            add_successor(out, &count, &next, "observer-fail");
            next = *source;
            next.stage = 3;
            next.z = 1;
            next.terminal = 1;
            add_successor(out, &count, &next, "observer-record");
        } else {
            next.stage = 3;
            next.z = 1; /* finalized */
            next.terminal = 1;
            add_successor(out, &count, &next, "finalize-authority");
        }
        break;
    case 2: /* registration compensation */
        if (source->stage == 0) {
            next.stage = 1; next.x = 1;
            add_successor(out, &count, &next, "binding-prepare");
        } else if (source->stage == 1) {
            next.stage = 2; next.y = 1;
            add_successor(out, &count, &next, "c31-submit");
        } else if (source->stage == 2) {
            next.stage = 3;
            add_successor(out, &count, &next, "commit-report-fail");
            add_successor(out, &count, source, "query-retry");
        } else {
            next.stage = 4; next.terminal = 1;
            next.y = 0;
            if (!mutant) next.x = 0;
            next.z = 1; /* abort ticket acknowledged */
            add_successor(out, &count, &next, "compensate-and-ack");
        }
        break;
    case 3: /* lease release reconciliation */
        if (source->stage == 0) {
            next.stage = 1; next.x = 1; next.y = 1;
            add_successor(out, &count, &next, "acquire-lease");
        } else if (source->stage == 1) {
            next.stage = 2; next.branch = 1; next.x = 0;
            add_successor(out, &count, &next, "release-after-error");
            next = *source;
            next.stage = 2; next.branch = 2; next.x = 1;
            add_successor(out, &count, &next, "release-before-error");
        } else {
            next.stage = 3; next.z = 1; next.terminal = 1;
            next.y = mutant && source->branch == 2 ? 0 : next.x;
            add_successor(out, &count, &next, "query-lease-state");
        }
        break;
    case 4: /* consume reconciliation and single publication */
        if (source->stage == 0) {
            next.stage = 1; next.x = 1; next.branch = 1;
            add_successor(out, &count, &next, "consume-after-error");
            next = *source;
            next.stage = 1; next.x = 0; next.branch = 2;
            add_successor(out, &count, &next, "consume-before-error");
        } else if (source->stage == 1) {
            if (source->x) {
                next.stage = 2;
                next.y = mutant ? 2 : 1;
                next.terminal = 1;
                add_successor(out, &count, &next, "query-consumed");
            } else {
                next.stage = 0;
                add_successor(out, &count, &next, "query-still-leased");
            }
        }
        break;
    case 5: /* ACK reconciliation */
        if (source->stage == 0) {
            next.stage = 1; next.x = 1; next.branch = 1;
            add_successor(out, &count, &next, "ack-after-error");
            next = *source;
            next.stage = 1; next.x = 0; next.branch = 2;
            add_successor(out, &count, &next, "ack-before-error");
        } else if (source->stage == 1) {
            next.stage = source->x ? 2 : 3;
            next.y = mutant ? 1 : next.x;
            next.terminal = source->x || mutant;
            add_successor(out, &count, &next, "query-ack-ledger");
        } else {
            next.stage = 2; next.x = 1; next.y = 1; next.terminal = 1;
            add_successor(out, &count, &next, "retry-ack");
        }
        break;
    case 6: /* reset budget zero */
        next.stage = 1; next.y = 1;
        next.x = mutant ? 1 : 0;
        next.terminal = 1;
        add_successor(out, &count, &next, "progress-budget-zero");
        next = *source;
        next.stage = 2; next.x = 1; next.terminal = 1;
        add_successor(out, &count, &next, "progress-one-unit");
        break;
    case 7: /* teardown resumes rather than restarts */
        if (source->stage == 0) {
            next.stage = 1; next.x = 1;
            add_successor(out, &count, &next, "teardown-begin");
        } else if (source->stage == 1) {
            next.stage = 2;
            add_successor(out, &count, &next, "partial-return");
        } else {
            next.stage = 3; next.y = 1; next.terminal = 1;
            if (mutant) ++next.x;
            add_successor(out, &count, &next, "resume-and-ack");
        }
        break;
    case 8: /* faulted cleanup is always admitted */
        next.stage = 1; next.x = 1; next.terminal = 1;
        if (mutant) next.z = 1; else next.y = 1;
        add_successor(out, &count, &next, "teardown-from-faulted");
        break;
    case 9: /* explicit counter exhaustion */
        next.stage = 1; next.x = 1; next.terminal = 1;
        next.y = mutant ? 2 : 1; /* counter vs generic capacity */
        next.z = mutant ? 1 : 0; /* forbidden C31 call */
        next.w = mutant ? 1 : 0; /* commit started */
        add_successor(out, &count, &next, "reset-at-limit");
        break;
    case 10: /* binding table */
    case 11: /* bundle table */
        next.stage = 1; next.x = 0; next.y = mutant ? 1 : 0;
        next.terminal = 1;
        add_successor(out, &count, &next,
                      family == 10 ? "validate-binding-table" :
                                     "validate-bundle-table");
        break;
    case 12: /* full wire profile, not context equality */
        next.stage = 1; next.x = 1; next.y = 0;
        next.z = mutant ? 1 : 0; next.terminal = 1;
        add_successor(out, &count, &next, "validate-profile-wire");
        break;
    case 13: /* release/finalize idempotence */
        if (source->stage == 0) {
            next.stage = 1; next.x = 1; next.y = 1;
            add_successor(out, &count, &next, "release-after-error");
        } else if (source->stage == 1) {
            next.stage = 2; next.z = 1;
            if (mutant) ++next.x;
            add_successor(out, &count, &next, "reconcile-release");
        } else if (source->stage == 2) {
            next.stage = 3; next.w = 1;
            add_successor(out, &count, &next, "finalize-first");
        } else {
            next.stage = 4; next.terminal = 1;
            add_successor(out, &count, &next, "finalize-repeat");
        }
        break;
    default:
        break;
    }
    return count;
}

static int invariant_holds(
    unsigned int family,
    const struct model_state *state
)
{
    switch (family) {
    case 0: return !(state->y && state->x);
    case 1: return !(state->y && !state->x);
    case 2: return !(state->terminal && (state->x || state->y || !state->z));
    case 3: return !(state->z && !state->y && state->x);
    case 4: return state->y <= 1 && !(state->y != 0 && !state->x);
    case 5: return !(state->y && !state->x);
    case 6: return !(state->y && state->x);
    case 7: return state->x <= 1 && state->y <= 1;
    case 8: return !(state->x && state->z);
    case 9:
        return !state->x || (state->y == 1 && state->z == 0 && state->w == 0);
    case 10:
    case 11:
        return !(state->y && !state->x);
    case 12: return !(state->z && !state->y);
    case 13: return state->x <= 1;
    default: return 0;
    }
}

static int state_equal(
    const struct model_state *left,
    const struct model_state *right
)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static void sort_successors(struct successor *items, unsigned int count)
{
    unsigned int outer;

    for (outer = 1; outer < count; ++outer) {
        struct successor value = items[outer];
        unsigned int inner = outer;

        while (inner > 0 &&
               strcmp(items[inner - 1u].action, value.action) > 0) {
            items[inner] = items[inner - 1u];
            --inner;
        }
        items[inner] = value;
    }
}

static void build_path(
    const struct queue_entry queue[C35A_MODEL_STATE_CAP],
    uint16_t index,
    char output[512]
)
{
    const char *actions[C35A_MODEL_DEPTH_CAP + 1u];
    unsigned int count = 0;
    size_t offset = 0;

    while (queue[index].parent != UINT16_MAX &&
           count < C35A_MODEL_DEPTH_CAP + 1u) {
        actions[count++] = queue[index].action;
        index = queue[index].parent;
    }
    output[0] = '\0';
    while (count > 0) {
        int written;

        --count;
        written = snprintf(&output[offset], 512u - offset, "%s%s",
                           offset == 0 ? "" : ">", actions[count]);
        if (written < 0 || (size_t)written >= 512u - offset) {
            output[511] = '\0';
            return;
        }
        offset += (size_t)written;
    }
}

int c35a_model_explore(
    unsigned int family,
    int mutation_enabled,
    struct c35a_model_result *result
)
{
    struct queue_entry queue[C35A_MODEL_STATE_CAP];
    uint16_t head = 0;
    uint16_t tail = 1;

    if (family >= C35A_MODEL_FAMILIES || result == NULL) return 0;
    memset(result, 0, sizeof(*result));
    memset(queue, 0, sizeof(queue));
    queue[0].parent = UINT16_MAX;
    while (head < tail) {
        struct successor next[C35A_MODEL_SUCCESSOR_CAP];
        unsigned int count;
        unsigned int index;

        if (queue[head].depth > result->max_depth)
            result->max_depth = queue[head].depth;
        if (queue[head].state.terminal) result->terminal_reached = 1;
        count = successors(
            family, &queue[head].state, mutation_enabled, next);
        if (count > C35A_MODEL_SUCCESSOR_CAP ||
            (!queue[head].state.terminal && count == 0)) {
            result->cap_reached = 1;
            return 0;
        }
        sort_successors(next, count);
        for (index = 0; index < count; ++index) {
            uint16_t existing;
            int seen = 0;

            ++result->transitions;
            if (!invariant_holds(family, &next[index].state)) {
                struct queue_entry violation;

                memset(&violation, 0, sizeof(violation));
                violation.state = next[index].state;
                violation.parent = head;
                violation.depth = (uint8_t)(queue[head].depth + 1u);
                violation.action = next[index].action;
                if (tail >= C35A_MODEL_STATE_CAP) {
                    result->cap_reached = 1;
                    return 0;
                }
                queue[tail] = violation;
                result->violation = 1;
                result->states = tail + 1u;
                result->max_depth = violation.depth;
                build_path(queue, tail, result->path);
                return 1;
            }
            if (queue[head].depth >= C35A_MODEL_DEPTH_CAP) {
                result->cap_reached = 1;
                return 0;
            }
            for (existing = 0; existing < tail; ++existing) {
                if (state_equal(&queue[existing].state, &next[index].state)) {
                    seen = 1;
                    break;
                }
            }
            if (seen) continue;
            if (tail >= C35A_MODEL_STATE_CAP) {
                result->cap_reached = 1;
                return 0;
            }
            queue[tail].state = next[index].state;
            queue[tail].parent = head;
            queue[tail].depth = (uint8_t)(queue[head].depth + 1u);
            queue[tail].action = next[index].action;
            ++tail;
        }
        ++head;
    }
    result->states = tail;
    return result->terminal_reached && !result->cap_reached;
}
