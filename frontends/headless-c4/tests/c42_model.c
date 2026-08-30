/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_model.h"

#include <stdio.h>
#include <string.h>

#define MODEL_STATE_CAP 32768u
#define MODEL_TRANSITION_CAP 262144u
#define MODEL_DEPTH_CAP 20u
#define MODEL_SUCCESSOR_CAP 8u
#define MODEL_FAMILY_NODE_CAP 4096u

enum model_ordinal {
    O_HOST_SQ_EVENT = 0,
    O_HOST_MUTATE = 1,
    O_CAPTURE_STEP = 2,
    O_PORT_STEP = 3,
    O_CQ_RESERVE = 4,
    O_PUBLICATION_STEP = 5,
    O_CQ_HEAD_EVENT = 6,
    O_TARGET_STEP = 7,
    O_QUEUE_CONTROL_STEP = 8,
    O_MAP_EVENT = 9,
    O_RESET_STEP = 10,
    O_TEARDOWN_STEP = 11,
    O_NOTIFY_STEP = 12
};

enum model_effect {
    E_MAP_CQ,
    E_SCRUB_CQ,
    E_COMMIT_CQ,
    E_MAP_SQ,
    E_COMMIT_SQ,
    E_ENABLE,
    E_TAIL,
    E_STALE_TAIL,
    E_INVALID_TAIL,
    E_CAPTURE,
    E_HOST_MUTATE,
    E_PREPARE,
    E_ADMIT,
    E_DUPLICATE_REJECT,
    E_READY,
    E_RESERVE,
    E_BODY_PREFIX,
    E_BODY_FULL,
    E_MARKER_UNKNOWN,
    E_MARKER_FULL,
    E_CONSUME,
    E_ACK,
    E_ACK_LATCH,
    E_TARGET,
    E_TARGET_RELEASE,
    E_NOTIFY,
    E_DELETE_BEGIN,
    E_DELETE_DRAIN,
    E_DELETE_COMMIT,
    E_RECREATE,
    E_RESET_BEGIN,
    E_REVOKE,
    E_QUIESCE,
    E_COLD,
    E_INSTANCE_B,
    E_CROSS_TOKEN_REJECT
};

struct model_action {
    const char *name;
    uint16_t prerequisites;
    uint8_t ordinal;
    uint8_t effect;
};

struct model_family {
    const char *name;
    const struct model_action *actions;
    uint8_t count;
};

struct model_state {
    uint8_t cq_mapped;
    uint8_t cq_scrubbed;
    uint8_t cq_live;
    uint8_t sq_mapped;
    uint8_t sq_live;
    uint8_t enabled;
    uint8_t invalid_tail;
    uint8_t faulted;
    uint8_t pending;
    uint8_t captures;
    uint8_t capture_reads;
    uint8_t host_mutated;
    uint8_t prepared;
    uint8_t port_committed;
    uint8_t sq_head;
    uint8_t current_sq_head;
    uint8_t active_same_cid;
    uint8_t ready;
    uint8_t cq_capacity;
    uint8_t cq_reserved;
    uint8_t body_prefix;
    uint8_t marker_unknown;
    uint8_t marker_visible;
    uint8_t marker_rewrites;
    uint8_t consume_committed;
    uint8_t cross_committed;
    uint8_t cid_released;
    uint8_t cqe_committed;
    uint8_t unacked;
    uint8_t acked;
    uint8_t pending_ack;
    uint8_t device_phase;
    uint8_t ack_phase_changes;
    uint8_t sqhd_sample;
    uint8_t reserve_head;
    uint8_t notification;
    uint8_t target_ref;
    uint8_t target_generation_match;
    uint8_t delete_prequiesce;
    uint8_t delete_committed;
    uint8_t tombstoned;
    uint8_t recreated;
    uint8_t frozen_tail;
    uint8_t reset_started;
    uint8_t epoch_advanced;
    uint8_t old_caps_live;
    uint8_t reopened;
    uint8_t quiescent;
    uint8_t instance_b;
    uint8_t cross_token_effect;
};

struct model_node {
    struct model_state state;
    uint16_t done;
    uint8_t depth;
    uint8_t action;
    uint32_t parent;
};

#define BIT(index) ((uint16_t)(1u << (index)))
#define ACTION(label, deps, order, operation) {label, deps, order, operation}

static const struct model_action f01[] = {
    ACTION("map-cq", 0, O_MAP_EVENT, E_MAP_CQ),
    ACTION("scrub-cq", BIT(0), O_MAP_EVENT, E_SCRUB_CQ),
    ACTION("commit-cq", BIT(1), O_QUEUE_CONTROL_STEP, E_COMMIT_CQ),
    ACTION("map-sq", BIT(2), O_MAP_EVENT, E_MAP_SQ),
    ACTION("commit-sq", BIT(3), O_QUEUE_CONTROL_STEP, E_COMMIT_SQ),
    ACTION("enable", BIT(4), O_QUEUE_CONTROL_STEP, E_ENABLE),
};
static const struct model_action f02[] = {
    ACTION("tail-batch", 0, O_HOST_SQ_EVENT, E_TAIL),
    ACTION("capture-a", BIT(0), O_CAPTURE_STEP, E_CAPTURE),
    ACTION("admit-a", BIT(1), O_PORT_STEP, E_ADMIT),
    ACTION("capture-b", BIT(2), O_CAPTURE_STEP, E_CAPTURE),
    ACTION("admit-b", BIT(3), O_PORT_STEP, E_ADMIT),
    ACTION("stale-tail", 0, O_HOST_SQ_EVENT, E_STALE_TAIL),
};
static const struct model_action f03[] = {
    ACTION("tail", 0, O_HOST_SQ_EVENT, E_TAIL),
    ACTION("capture-once", BIT(0), O_CAPTURE_STEP, E_CAPTURE),
    ACTION("host-mutate", BIT(1), O_HOST_MUTATE, E_HOST_MUTATE),
    ACTION("prepare-backpressure", BIT(1), O_PORT_STEP, E_PREPARE),
    ACTION("admit-stable", BIT(3), O_PORT_STEP, E_ADMIT),
};
static const struct model_action f04[] = {
    ACTION("invalid-tail", 0, O_HOST_SQ_EVENT, E_INVALID_TAIL),
    ACTION("stale-no-effect", 0, O_HOST_SQ_EVENT, E_STALE_TAIL),
    ACTION("capture-a", BIT(1), O_CAPTURE_STEP, E_CAPTURE),
    ACTION("admit-a", BIT(2), O_PORT_STEP, E_ADMIT),
    ACTION("reject-duplicate", BIT(3), O_CAPTURE_STEP, E_DUPLICATE_REJECT),
};
static const struct model_action f05[] = {
    ACTION("capture-a", 0, O_CAPTURE_STEP, E_CAPTURE),
    ACTION("capture-b", 0, O_CAPTURE_STEP, E_CAPTURE),
    ACTION("admit-a", BIT(0), O_PORT_STEP, E_ADMIT),
    ACTION("admit-b", BIT(1), O_PORT_STEP, E_ADMIT),
    ACTION("ready-b", BIT(3), O_PORT_STEP, E_READY),
    ACTION("reserve-b", BIT(4), O_CQ_RESERVE, E_RESERVE),
    ACTION("ready-a", BIT(2), O_PORT_STEP, E_READY),
};
static const struct model_action f06[] = {
    ACTION("ready", 0, O_PORT_STEP, E_READY),
    ACTION("reserve", BIT(0), O_CQ_RESERVE, E_RESERVE),
    ACTION("body-prefix", BIT(1), O_PUBLICATION_STEP, E_BODY_PREFIX),
    ACTION("body-full", BIT(2), O_PUBLICATION_STEP, E_BODY_FULL),
    ACTION("marker", BIT(3), O_PUBLICATION_STEP, E_MARKER_FULL),
    ACTION("consume", BIT(4), O_PUBLICATION_STEP, E_CONSUME),
    ACTION("ack", BIT(5), O_CQ_HEAD_EVENT, E_ACK),
};
static const struct model_action f07[] = {
    ACTION("fill-cq", 0, O_PUBLICATION_STEP, E_CONSUME),
    ACTION("ready-fourth", BIT(0), O_PORT_STEP, E_READY),
    ACTION("ack-one", BIT(1), O_CQ_HEAD_EVENT, E_ACK),
    ACTION("reserve-fourth", BIT(2), O_CQ_RESERVE, E_RESERVE),
};
static const struct model_action f08[] = {
    ACTION("target-a", 0, O_TARGET_STEP, E_TARGET),
    ACTION("marker-visible", BIT(0), O_PUBLICATION_STEP, E_MARKER_FULL),
    ACTION("ack-latch", BIT(1), O_CQ_HEAD_EVENT, E_ACK_LATCH),
    ACTION("same-cid-tail", BIT(1), O_HOST_SQ_EVENT, E_TAIL),
    ACTION("cross-commit", BIT(1), O_PUBLICATION_STEP, E_CONSUME),
    ACTION("target-release", BIT(4), O_TARGET_STEP, E_TARGET_RELEASE),
    ACTION("capture-b", BIT(4), O_CAPTURE_STEP, E_CAPTURE),
};
static const struct model_action f09[] = {
    ACTION("reserve", 0, O_CQ_RESERVE, E_RESERVE),
    ACTION("body-prefix", BIT(0), O_PUBLICATION_STEP, E_BODY_PREFIX),
    ACTION("body-query", BIT(1), O_PUBLICATION_STEP, E_BODY_FULL),
    ACTION("marker-unknown", BIT(2), O_PUBLICATION_STEP, E_MARKER_UNKNOWN),
    ACTION("marker-query", BIT(3), O_PUBLICATION_STEP, E_MARKER_FULL),
    ACTION("consume-cleanup", BIT(4), O_PUBLICATION_STEP, E_CONSUME),
    ACTION("notify", BIT(5), O_NOTIFY_STEP, E_NOTIFY),
};
static const struct model_action f10[] = {
    ACTION("tail", 0, O_HOST_SQ_EVENT, E_TAIL),
    ACTION("delete-begin", BIT(0), O_QUEUE_CONTROL_STEP, E_DELETE_BEGIN),
    ACTION("capture", BIT(1), O_CAPTURE_STEP, E_CAPTURE),
    ACTION("admit", BIT(2), O_PORT_STEP, E_ADMIT),
    ACTION("drain", BIT(3), O_QUEUE_CONTROL_STEP, E_DELETE_DRAIN),
    ACTION("delete-commit", BIT(4), O_QUEUE_CONTROL_STEP, E_DELETE_COMMIT),
    ACTION("ack", BIT(5), O_CQ_HEAD_EVENT, E_ACK),
    ACTION("recreate", BIT(6), O_MAP_EVENT, E_RECREATE),
};
static const struct model_action f11[] = {
    ACTION("active", 0, O_PORT_STEP, E_ADMIT),
    ACTION("reset-begin", BIT(0), O_RESET_STEP, E_RESET_BEGIN),
    ACTION("revoke", BIT(1), O_RESET_STEP, E_REVOKE),
    ACTION("port-memory-quiesce", BIT(2), O_RESET_STEP, E_QUIESCE),
    ACTION("cold", BIT(3), O_RESET_STEP, E_COLD),
    ACTION("explicit-recreate", BIT(4), O_MAP_EVENT, E_RECREATE),
};
static const struct model_action f12[] = {
    ACTION("instance-a", 0, O_HOST_SQ_EVENT, E_TAIL),
    ACTION("instance-b", 0, O_HOST_SQ_EVENT, E_INSTANCE_B),
    ACTION("cross-token-reject", BIT(0) | BIT(1), O_TARGET_STEP,
           E_CROSS_TOKEN_REJECT),
    ACTION("a-capture", BIT(0), O_CAPTURE_STEP, E_CAPTURE),
    ACTION("b-capture", BIT(1), O_CAPTURE_STEP, E_CAPTURE),
};

static const struct model_family families[C42_MODEL_FAMILIES] = {
    {"F01-create-contract", f01, sizeof(f01) / sizeof(f01[0])},
    {"F02-sq-single-batch-wrap", f02, sizeof(f02) / sizeof(f02[0])},
    {"F03-capture-backpressure", f03, sizeof(f03) / sizeof(f03[0])},
    {"F04-sq-invalid-cid", f04, sizeof(f04) / sizeof(f04[0])},
    {"F05-delayed-out-of-order", f05, sizeof(f05) / sizeof(f05[0])},
    {"F06-cq-phase-ack", f06, sizeof(f06) / sizeof(f06[0])},
    {"F07-cq-full-lease", f07, sizeof(f07) / sizeof(f07[0])},
    {"F08-cid-reuse-target", f08, sizeof(f08) / sizeof(f08[0])},
    {"F09-publication-faults", f09, sizeof(f09) / sizeof(f09[0])},
    {"F10-delete-tombstone", f10, sizeof(f10) / sizeof(f10[0])},
    {"F11-reset-teardown", f11, sizeof(f11) / sizeof(f11[0])},
    {"F12-isolation", f12, sizeof(f12) / sizeof(f12[0])},
};

static struct model_state initial_state(void)
{
    struct model_state state;

    memset(&state, 0, sizeof(state));
    state.cq_capacity = 3;
    state.device_phase = 1;
    state.target_generation_match = 1;
    state.old_caps_live = 1;
    return state;
}

static int invariant_ok(const struct model_state *state)
{
    return (!state->cq_live || state->cq_scrubbed) &&
           (!state->sq_live || state->cq_live) &&
           (!state->enabled || (state->sq_live && state->cq_live)) &&
           (!state->invalid_tail || (!state->sq_live && state->faulted)) &&
           state->capture_reads <= state->captures &&
           state->sq_head <= state->port_committed &&
           state->active_same_cid <= 1 &&
           state->cq_reserved + state->unacked <= state->cq_capacity &&
           (!state->marker_visible || state->body_prefix == 15) &&
           (!state->consume_committed || state->marker_visible) &&
           (!state->cross_committed ||
            (state->marker_visible && state->consume_committed &&
             state->cid_released && state->cqe_committed &&
             state->active_same_cid == 0)) &&
           (!state->cid_released || state->cross_committed) &&
           state->acked <= state->cqe_committed &&
           state->ack_phase_changes == 0 &&
           (!state->ready || state->port_committed != 0 ||
            state->captures == 0) &&
           (!state->cq_reserved || state->sqhd_sample == state->reserve_head) &&
           (!state->notification || state->cross_committed) &&
           (!state->target_ref || state->target_generation_match) &&
           (!state->delete_committed ||
            (state->frozen_tail == state->sq_head && state->unacked == 0)) &&
           (!state->recreated || !state->tombstoned) &&
           (!state->reopened ||
            (state->reset_started && state->epoch_advanced &&
             !state->old_caps_live && state->quiescent)) &&
           state->marker_rewrites <= 1 && state->cross_token_effect == 0;
}

static int apply_effect(struct model_state *state, uint8_t effect)
{
    switch (effect) {
    case E_MAP_CQ: state->cq_mapped = 1; break;
    case E_SCRUB_CQ:
        if (!state->cq_mapped) return 0;
        state->cq_scrubbed = 1;
        break;
    case E_COMMIT_CQ:
        if (!state->cq_scrubbed) return 0;
        state->cq_live = 1;
        break;
    case E_MAP_SQ:
        if (!state->cq_live) return 0;
        state->sq_mapped = 1;
        break;
    case E_COMMIT_SQ:
        if (!state->sq_mapped || !state->cq_live) return 0;
        state->sq_live = 1;
        break;
    case E_ENABLE:
        if (!state->sq_live || !state->cq_live) return 0;
        state->enabled = 1;
        break;
    case E_TAIL:
        state->pending++;
        state->frozen_tail = (uint8_t)(state->sq_head + state->pending);
        break;
    case E_STALE_TAIL: break;
    case E_INVALID_TAIL:
        state->invalid_tail = 1;
        state->faulted = 1;
        state->sq_live = 0;
        break;
    case E_CAPTURE:
        if (state->pending != 0) state->pending--;
        state->captures++;
        state->capture_reads++;
        break;
    case E_HOST_MUTATE: state->host_mutated = 1; break;
    case E_PREPARE: state->prepared = 1; break;
    case E_ADMIT:
        state->prepared = 1;
        state->port_committed++;
        state->sq_head++;
        state->current_sq_head = state->sq_head;
        state->active_same_cid = 1;
        break;
    case E_DUPLICATE_REJECT:
        state->faulted = 1;
        state->sq_live = 0;
        break;
    case E_READY: state->ready = 1; break;
    case E_RESERVE:
        if (state->cq_reserved + state->unacked >= state->cq_capacity) return 0;
        state->cq_reserved++;
        state->sqhd_sample = state->current_sq_head;
        state->reserve_head = state->current_sq_head;
        break;
    case E_BODY_PREFIX: state->body_prefix = 7; break;
    case E_BODY_FULL: state->body_prefix = 15; break;
    case E_MARKER_UNKNOWN:
        if (state->body_prefix != 15) return 0;
        state->marker_unknown = 1;
        state->marker_rewrites = 1;
        break;
    case E_MARKER_FULL:
        if (state->body_prefix == 0) state->body_prefix = 15;
        state->marker_visible = 1;
        state->marker_rewrites = 1;
        break;
    case E_CONSUME:
        if (state->marker_visible == 0 && state->cq_reserved == 0) {
            state->body_prefix = 15;
            state->marker_visible = 1;
            state->unacked = state->cq_capacity;
            state->cqe_committed = state->cq_capacity;
            state->cross_committed = 1;
            state->consume_committed = 1;
            state->cid_released = 1;
            break;
        }
        state->consume_committed = 1;
        state->cross_committed = 1;
        state->cid_released = 1;
        state->cqe_committed++;
        if (state->cq_reserved != 0) state->cq_reserved--;
        state->unacked++;
        if (state->pending_ack != 0) {
            state->acked++;
            state->unacked--;
            state->pending_ack = 0;
        }
        state->active_same_cid = 0;
        break;
    case E_ACK:
        if (state->unacked != 0) {
            state->unacked--;
            state->acked++;
        }
        break;
    case E_ACK_LATCH: state->pending_ack = 1; break;
    case E_TARGET:
        state->target_ref = 1;
        state->target_generation_match = 1;
        break;
    case E_TARGET_RELEASE: state->target_ref = 0; break;
    case E_NOTIFY:
        if (!state->cross_committed) return 0;
        state->notification = 1;
        break;
    case E_DELETE_BEGIN:
        state->delete_prequiesce = 1;
        state->frozen_tail = (uint8_t)(state->sq_head + state->pending);
        break;
    case E_DELETE_DRAIN:
        state->sq_head = state->frozen_tail;
        state->current_sq_head = state->sq_head;
        state->pending = 0;
        break;
    case E_DELETE_COMMIT:
        if (state->sq_head != state->frozen_tail) return 0;
        state->delete_committed = 1;
        state->tombstoned = state->unacked != 0;
        break;
    case E_RECREATE:
        if (state->tombstoned && state->unacked != 0) return 0;
        state->tombstoned = 0;
        state->recreated = 1;
        if (state->reset_started) state->reopened = 1;
        break;
    case E_RESET_BEGIN:
        state->reset_started = 1;
        state->epoch_advanced = 1;
        break;
    case E_REVOKE:
        if (!state->reset_started) return 0;
        state->old_caps_live = 0;
        state->target_ref = 0;
        break;
    case E_QUIESCE:
        if (state->old_caps_live) return 0;
        state->quiescent = 1;
        break;
    case E_COLD:
        if (!state->quiescent) return 0;
        state->sq_live = 0;
        state->cq_live = 0;
        state->enabled = 0;
        state->unacked = 0;
        state->frozen_tail = state->sq_head;
        break;
    case E_INSTANCE_B: state->instance_b = 1; break;
    case E_CROSS_TOKEN_REJECT: state->cross_token_effect = 0; break;
    default: return 0;
    }
    return 1;
}

static int state_equal(
    const struct model_node *left,
    const struct model_node *right)
{
    return left->done == right->done &&
           memcmp(&left->state, &right->state, sizeof(left->state)) == 0;
}

static int seen(
    const struct model_node *nodes,
    uint32_t count,
    const struct model_node *candidate)
{
    uint32_t index;

    for (index = 0; index < count; ++index) {
        if (state_equal(&nodes[index], candidate)) {
            return 1;
        }
    }
    return 0;
}

static int action_before(
    const struct model_action *left,
    const struct model_action *right)
{
    if (left->ordinal != right->ordinal) {
        return left->ordinal < right->ordinal;
    }
    return strcmp(left->name, right->name) < 0;
}

static int explore_family(
    const struct model_family *family,
    struct c42_model_summary *summary)
{
    struct model_node nodes[MODEL_FAMILY_NODE_CAP];
    uint32_t head = 0;
    uint32_t count = 1;

    memset(nodes, 0, sizeof(nodes));
    nodes[0].state = initial_state();
    while (head < count) {
        const struct model_node current = nodes[head];
        uint8_t order[8];
        uint8_t enabled = 0;
        uint8_t action_index;

        for (action_index = 0; action_index < family->count; ++action_index) {
            const struct model_action *action = &family->actions[action_index];

            if ((current.done & BIT(action_index)) == 0 &&
                (current.done & action->prerequisites) ==
                    action->prerequisites) {
                uint8_t position = enabled;

                while (position != 0 && action_before(
                           action, &family->actions[order[position - 1u]])) {
                    order[position] = order[position - 1u];
                    position--;
                }
                order[position] = action_index;
                enabled++;
            }
        }
        if (enabled > MODEL_SUCCESSOR_CAP) {
            return 0;
        }
        if (enabled > summary->maximum_successors) {
            summary->maximum_successors = enabled;
        }
        for (action_index = 0; action_index < enabled; ++action_index) {
            uint8_t selected = order[action_index];
            struct model_node next = current;

            if (current.depth >= MODEL_DEPTH_CAP) {
                return 0;
            }
            summary->transitions++;
            if (summary->transitions > MODEL_TRANSITION_CAP) {
                return 0;
            }
            if (!apply_effect(
                    &next.state, family->actions[selected].effect)) {
                continue;
            }
            next.done |= BIT(selected);
            next.depth = (uint8_t)(current.depth + 1u);
            next.parent = head;
            next.action = selected;
            if (!invariant_ok(&next.state)) {
                fprintf(stderr, "baseline invariant failure: %s/%s\n",
                        family->name, family->actions[selected].name);
                return 0;
            }
            if (next.depth > summary->maximum_depth) {
                summary->maximum_depth = next.depth;
            }
            if (!seen(nodes, count, &next)) {
                if (count >= MODEL_FAMILY_NODE_CAP ||
                    summary->states >= MODEL_STATE_CAP) {
                    return 0;
                }
                nodes[count++] = next;
                summary->states++;
            }
        }
        head++;
    }
    return 1;
}

int c42_model_explore(struct c42_model_summary *summary)
{
    struct c42_model_summary local = {0};
    uint32_t family;

    if (summary == NULL) {
        return 0;
    }
    for (family = 0; family < C42_MODEL_FAMILIES; ++family) {
        local.states++;
        if (!explore_family(&families[family], &local)) {
            return 0;
        }
        local.families++;
    }
    *summary = local;
    return local.families == C42_MODEL_FAMILIES &&
           local.states <= MODEL_STATE_CAP &&
           local.transitions <= MODEL_TRANSITION_CAP &&
           local.maximum_depth <= MODEL_DEPTH_CAP &&
           local.maximum_successors <= MODEL_SUCCESSOR_CAP;
}

static const char *const mutant_names[C42_MODEL_MUTANTS] = {
    "BM_HEAD_ADVANCES_BEFORE_ADMISSION_RECONCILE",
    "BM_REREAD_SQE_ON_BACKPRESSURE",
    "BM_DUPLICATE_CID_ALLOWED",
    "BM_MATCH_CID_WITHOUT_RING_GENERATION",
    "BM_INVALID_TAIL_REMAINS_LIVE",
    "BM_SQHD_AT_CAPTURE",
    "BM_CQ_OVERWRITE_FULL",
    "BM_PHASE_TOGGLE_ON_ACK",
    "BM_MARKER_VISIBLE_BEFORE_BODY",
    "BM_CONSUME_COMMIT_BEFORE_MARKER",
    "BM_CID_RELEASE_BEFORE_CROSS_COMMIT",
    "BM_CID_HELD_UNTIL_HOST_ACK",
    "BM_ACK_NONCOMMITTED_SLOT",
    "BM_BLIND_REWRITE_UNKNOWN_MARKER",
    "BM_NOTIFY_BEFORE_CROSS_COMMIT",
    "BM_DELETE_CQ_WITH_UNACKED",
    "BM_CREATE_LIVE_BEFORE_SCRUB",
    "BM_RECREATE_BEFORE_TOMBSTONE_CLEAR",
    "BM_RESET_REOPEN_BEFORE_REVOKE",
    "BM_DELETE_DROPS_DOORBELLED_SQE",
};

const char *c42_model_mutant_name(uint32_t mutant)
{
    return mutant < C42_MODEL_MUTANTS ? mutant_names[mutant] : NULL;
}

static void seed_mutant_state(uint32_t mutant, struct model_state *state)
{
    *state = initial_state();
    state->cq_mapped = 1;
    state->cq_scrubbed = 1;
    state->cq_live = 1;
    state->sq_mapped = 1;
    state->sq_live = 1;
    state->enabled = 1;
    state->captures = 1;
    state->capture_reads = 1;
    state->prepared = 1;
    state->port_committed = 1;
    state->sq_head = 1;
    state->current_sq_head = 1;
    state->active_same_cid = 1;
    state->frozen_tail = 1;
    if (mutant == 5) {
        state->current_sq_head = 2;
    } else if (mutant == 6) {
        state->unacked = 3;
    } else if (mutant == 11) {
        state->body_prefix = 15;
        state->marker_visible = 1;
        state->consume_committed = 1;
        state->cross_committed = 1;
        state->cid_released = 1;
        state->cqe_committed = 1;
        state->unacked = 1;
        state->active_same_cid = 0;
    } else if (mutant == 12) {
        state->unacked = 0;
        state->cqe_committed = 0;
    } else if (mutant == 13) {
        state->body_prefix = 15;
        state->marker_unknown = 1;
        state->marker_rewrites = 1;
    } else if (mutant == 15) {
        state->unacked = 1;
    } else if (mutant == 17) {
        state->tombstoned = 1;
        state->unacked = 1;
    } else if (mutant == 18) {
        state->reset_started = 1;
        state->epoch_advanced = 1;
        state->old_caps_live = 1;
    } else if (mutant == 19) {
        state->pending = 1;
        state->frozen_tail = 2;
        state->delete_prequiesce = 1;
    }
}

static void apply_mutation(uint32_t mutant, struct model_state *state)
{
    switch (mutant) {
    case 0: state->sq_head = 2; state->port_committed = 1; break;
    case 1: state->capture_reads = 2; state->captures = 1; break;
    case 2: state->active_same_cid = 2; break;
    case 3: state->target_ref = 1; state->target_generation_match = 0; break;
    case 4: state->invalid_tail = 1; state->faulted = 0; state->sq_live = 1; break;
    case 5:
        state->cq_reserved = 1;
        state->reserve_head = state->current_sq_head;
        state->sqhd_sample = 1;
        break;
    case 6: state->cq_reserved = 1; break;
    case 7: state->ack_phase_changes = 1; break;
    case 8: state->body_prefix = 0; state->marker_visible = 1; break;
    case 9: state->marker_visible = 0; state->consume_committed = 1; break;
    case 10: state->cid_released = 1; state->cross_committed = 0; break;
    case 11: state->active_same_cid = 1; break;
    case 12: state->acked = 1; state->cqe_committed = 0; break;
    case 13: state->marker_rewrites = 2; break;
    case 14: state->notification = 1; state->cross_committed = 0; break;
    case 15:
        state->delete_committed = 1;
        state->frozen_tail = state->sq_head;
        break;
    case 16: state->cq_live = 1; state->cq_scrubbed = 0; break;
    case 17: state->recreated = 1; break;
    case 18: state->reopened = 1; state->quiescent = 1; break;
    case 19: state->delete_committed = 1; break;
    default: break;
    }
}

int c42_model_find_counterexample(
    uint32_t mutant,
    uint32_t *depth,
    char *path,
    size_t path_size)
{
    static const char *const setup_names[2] = {
        "HOST_SQ_EVENT(stable)", "HOST_MUTATE(stable)"
    };
    struct model_node nodes[16];
    uint32_t head = 0;
    uint32_t count = 1;

    if (mutant >= C42_MODEL_MUTANTS || depth == NULL || path == NULL ||
        path_size == 0) {
        return 0;
    }
    memset(nodes, 0, sizeof(nodes));
    seed_mutant_state(mutant, &nodes[0].state);
    if (!invariant_ok(&nodes[0].state)) {
        return 0;
    }
    while (head < count) {
        uint8_t action;

        if (nodes[head].depth >= MODEL_DEPTH_CAP) {
            return 0;
        }
        for (action = 0; action < 3; ++action) {
            struct model_node next = nodes[head];

            if (action < 2) {
                if ((next.done & BIT(action)) != 0) {
                    continue;
                }
                next.done |= BIT(action);
                if (action == 1) {
                    next.state.host_mutated = 1;
                }
            } else {
                uint32_t chain[MODEL_DEPTH_CAP];
                uint32_t chain_count = 0;
                uint32_t cursor = head;
                size_t used = 0;
                int written;

                if ((next.done & (BIT(0) | BIT(1))) !=
                    (BIT(0) | BIT(1))) {
                    continue;
                }
                apply_mutation(mutant, &next.state);
                if (invariant_ok(&next.state)) {
                    continue;
                }
                while (cursor != 0 && chain_count < MODEL_DEPTH_CAP) {
                    chain[chain_count++] = nodes[cursor].action;
                    cursor = nodes[cursor].parent;
                }
                while (chain_count != 0) {
                    const char *separator = used == 0 ? "" : ">";

                    chain_count--;
                    written = snprintf(
                        path + used, path_size - used, "%s%s",
                        separator, setup_names[chain[chain_count]]
                    );
                    if (written < 0 || (size_t)written >= path_size - used) {
                        return 0;
                    }
                    used += (size_t)written;
                }
                written = snprintf(
                    path + used, path_size - used, "%s%s",
                    used == 0 ? "" : ">", mutant_names[mutant]
                );
                if (written < 0 || (size_t)written >= path_size - used) {
                    return 0;
                }
                *depth = (uint32_t)nodes[head].depth + 1u;
                return 1;
            }
            next.depth = (uint8_t)(nodes[head].depth + 1u);
            next.parent = head;
            next.action = action;
            if (!invariant_ok(&next.state)) {
                return 0;
            }
            if (!seen(nodes, count, &next)) {
                if (count >= sizeof(nodes) / sizeof(nodes[0])) {
                    return 0;
                }
                nodes[count++] = next;
            }
        }
        head++;
    }
    return 0;
}
