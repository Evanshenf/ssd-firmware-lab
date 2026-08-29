/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_model.h"

#include <stdio.h>
#include <string.h>

#define MODEL_STATE_CAP 4096u
#define MODEL_TRANSITION_CAP 212992u
#define MODEL_DEPTH_CAP 16u
#define MODEL_SUCCESSOR_CAP 4u

enum model_effect {
    E_SUBMIT_A,
    E_SUBMIT_B,
    E_REGISTER_A,
    E_REGISTER_B,
    E_STEP_A,
    E_STEP_B,
    E_TERMINAL_A,
    E_TERMINAL_B,
    E_ACQUIRE,
    E_RELEASE_LEASE,
    E_REACQUIRE,
    E_COPY,
    E_CONSUME,
    E_ACK,
    E_BACKPRESSURE,
    E_CANCEL,
    E_CLAIM,
    E_WRITE_A,
    E_WRITE_B,
    E_VOLATILE_B,
    E_CMAP_B,
    E_TRIM_A,
    E_RECEIPT,
    E_RECEIPT_ACK,
    E_RELEASE_BUNDLE,
    E_RESTART,
    E_READ,
    E_CLOSE,
    E_RESET,
    E_RECOVER,
    E_REOPEN,
    E_POISON,
    E_PROGRESS_A,
    E_PROGRESS_B,
    E_GEOMETRY_A,
    E_GEOMETRY_B,
    E_SENTINEL
};

struct model_action {
    const char *name;
    uint16_t prerequisites;
    uint8_t effect;
};

struct model_family {
    const char *name;
    const struct model_action *actions;
    uint8_t count;
};

struct model_state {
    uint16_t done;
    uint8_t depth;
    uint8_t admission_open;
    uint8_t submitted[2];
    uint8_t registered[2];
    uint8_t stepped[2];
    uint8_t terminals[2];
    uint8_t lease;
    uint8_t lease_generation;
    uint8_t copied;
    uint8_t consumed;
    uint8_t acked;
    uint8_t registry_retained;
    uint8_t cancelled;
    uint8_t claim_count;
    uint8_t receipt_live;
    uint8_t quiescent;
    uint8_t raw[2];
    uint8_t logical[2];
    uint8_t host_hint[2];
    uint8_t recovered;
    uint8_t progress[2];
    uint8_t geometry[2];
    uint8_t sentinel;
    uint8_t reset_started;
    uint32_t epoch;
};

#define BIT(index) ((uint16_t)(1u << (index)))
#define ACTION(label, deps, operation) {label, deps, operation}

static const struct model_action f01[] = {
    ACTION("submit", 0, E_SUBMIT_A),
    ACTION("register", BIT(0), E_REGISTER_A),
    ACTION("step", BIT(1), E_STEP_A),
    ACTION("terminal", BIT(2), E_TERMINAL_A),
    ACTION("acquire", BIT(3), E_ACQUIRE),
    ACTION("copy", BIT(4), E_COPY),
    ACTION("consume", BIT(5), E_CONSUME),
    ACTION("ack", BIT(6), E_ACK),
};
static const struct model_action f02[] = {
    ACTION("submit-a", 0, E_SUBMIT_A),
    ACTION("register-a", BIT(0), E_REGISTER_A),
    ACTION("step-a", BIT(1), E_STEP_A),
    ACTION("backpressure-b", BIT(1), E_BACKPRESSURE),
    ACTION("cancel-a", BIT(2) | BIT(3), E_CANCEL),
    ACTION("terminal-a", BIT(4), E_TERMINAL_A),
    ACTION("submit-b", BIT(5), E_SUBMIT_B),
    ACTION("register-b", BIT(6), E_REGISTER_B),
    ACTION("terminal-b", BIT(7), E_TERMINAL_B),
};
static const struct model_action f03[] = {
    ACTION("claim", 0, E_CLAIM),
    ACTION("write", BIT(0), E_WRITE_A),
    ACTION("physical-receipt", BIT(1), E_RECEIPT),
    ACTION("receipt-ack", BIT(2), E_RECEIPT_ACK),
    ACTION("release", BIT(3), E_RELEASE_BUNDLE),
    ACTION("restart", BIT(4), E_RESTART),
    ACTION("read", BIT(5), E_READ),
};
static const struct model_action f04[] = {
    ACTION("durable-a", 0, E_WRITE_A),
    ACTION("volatile-b", BIT(0), E_VOLATILE_B),
    ACTION("cmap-b", BIT(1), E_CMAP_B),
    ACTION("fence", BIT(2), E_RECOVER),
    ACTION("restart", BIT(3), E_RESTART),
    ACTION("read", BIT(4), E_READ),
};
static const struct model_action f05[] = {
    ACTION("write", 0, E_WRITE_A),
    ACTION("trim", BIT(0), E_TRIM_A),
    ACTION("restart", BIT(1), E_RESTART),
    ACTION("read-absent", BIT(2), E_READ),
};
static const struct model_action f06[] = {
    ACTION("a0", 0, E_PROGRESS_A), ACTION("a1", BIT(0), E_PROGRESS_A),
    ACTION("a2", BIT(1), E_PROGRESS_A),
    ACTION("a3", BIT(2), E_PROGRESS_A),
    ACTION("a4", BIT(3), E_PROGRESS_A),
    ACTION("a5", BIT(4), E_PROGRESS_A),
    ACTION("b0", 0, E_PROGRESS_B), ACTION("b1", BIT(6), E_PROGRESS_B),
    ACTION("b2", BIT(7), E_PROGRESS_B),
    ACTION("b3", BIT(8), E_PROGRESS_B),
    ACTION("b4", BIT(9), E_PROGRESS_B),
    ACTION("b5", BIT(10), E_PROGRESS_B),
};
static const struct model_action f07[] = {
    ACTION("volatile-a0", 0, E_PROGRESS_A),
    ACTION("volatile-a1", BIT(0), E_PROGRESS_A),
    ACTION("fence-a2", BIT(1), E_PROGRESS_A),
    ACTION("read-a3", BIT(2), E_PROGRESS_A),
    ACTION("restart-a4", BIT(3), E_PROGRESS_A),
    ACTION("verify-a5", BIT(4), E_PROGRESS_A),
    ACTION("durable-b0", 0, E_PROGRESS_B),
    ACTION("trim-b1", BIT(6), E_PROGRESS_B),
    ACTION("read-b2", BIT(7), E_PROGRESS_B),
    ACTION("write-b3", BIT(8), E_PROGRESS_B),
    ACTION("restart-b4", BIT(9), E_PROGRESS_B),
    ACTION("verify-b5", BIT(10), E_PROGRESS_B),
};
static const struct model_action f08[] = {
    ACTION("submit", 0, E_SUBMIT_A),
    ACTION("register", BIT(0), E_REGISTER_A),
    ACTION("step", BIT(1), E_STEP_A),
    ACTION("close", BIT(2), E_CLOSE),
    ACTION("reset", BIT(3), E_RESET),
    ACTION("recover", BIT(4), E_RECOVER),
    ACTION("reopen", BIT(5), E_REOPEN),
    ACTION("read", BIT(6), E_READ),
};
static const struct model_action f09[] = {
    ACTION("a0", 0, E_PROGRESS_A),
    ACTION("a1", BIT(0), E_PROGRESS_A),
    ACTION("close-a", BIT(1), E_CLOSE),
    ACTION("reset-a", BIT(2), E_RESET),
    ACTION("recover-a", BIT(3), E_RECOVER),
    ACTION("reopen-a", BIT(4), E_REOPEN),
    ACTION("b0", 0, E_PROGRESS_B),
    ACTION("b1", BIT(6), E_PROGRESS_B),
    ACTION("b2", BIT(7), E_PROGRESS_B),
    ACTION("b3", BIT(8), E_PROGRESS_B),
};
static const struct model_action f10[] = {
    ACTION("submit", 0, E_SUBMIT_A),
    ACTION("register", BIT(0), E_REGISTER_A),
    ACTION("terminal", BIT(1), E_TERMINAL_A),
    ACTION("acquire", BIT(2), E_ACQUIRE),
    ACTION("release", BIT(3), E_RELEASE_LEASE),
    ACTION("reacquire", BIT(4), E_REACQUIRE),
    ACTION("copy", BIT(5), E_COPY),
    ACTION("consume", BIT(6), E_CONSUME),
    ACTION("ack", BIT(7), E_ACK),
    ACTION("close", BIT(8), E_CLOSE),
    ACTION("reset", BIT(9), E_RESET),
};
static const struct model_action f11[] = {
    ACTION("claim", 0, E_CLAIM),
    ACTION("graph", BIT(0), E_RECEIPT),
    ACTION("terminal", BIT(1), E_TERMINAL_A),
    ACTION("sidecar-ack", BIT(2), E_ACK),
    ACTION("receipt-ack", BIT(1), E_RECEIPT_ACK),
    ACTION("release", BIT(3) | BIT(4), E_RELEASE_BUNDLE),
};
static const struct model_action f12[] = {
    ACTION("write", 0, E_WRITE_A),
    ACTION("poison-host", BIT(0), E_POISON),
    ACTION("close", BIT(1), E_CLOSE),
    ACTION("reset", BIT(2), E_RESET),
    ACTION("recover-raw", BIT(3), E_RECOVER),
    ACTION("reopen", BIT(4), E_REOPEN),
    ACTION("read", BIT(5), E_READ),
};
static const struct model_action f13[] = {
    ACTION("geometry-a", 0, E_GEOMETRY_A),
    ACTION("geometry-b", 0, E_GEOMETRY_B),
    ACTION("program-a", BIT(0), E_WRITE_A),
    ACTION("program-b", BIT(1), E_WRITE_B),
    ACTION("close-a", BIT(2), E_CLOSE),
    ACTION("reset-a", BIT(4), E_RESET),
    ACTION("recover-a", BIT(5), E_RECOVER),
    ACTION("reopen-a", BIT(6), E_REOPEN),
    ACTION("sentinel-a", BIT(7), E_SENTINEL),
    ACTION("sentinel-b", BIT(3), E_SENTINEL),
};

static const struct model_family families[C35_MODEL_FAMILIES] = {
    {"F01-life-success", f01, sizeof(f01) / sizeof(f01[0])},
    {"F02-retry-cancel", f02, sizeof(f02) / sizeof(f02[0])},
    {"F03-durable-roundtrip", f03, sizeof(f03) / sizeof(f03[0])},
    {"F04-volatile-fence", f04, sizeof(f04) / sizeof(f04[0])},
    {"F05-durable-trim", f05, sizeof(f05) / sizeof(f05[0])},
    {"F06-twin-write-schedule", f06, sizeof(f06) / sizeof(f06[0])},
    {"F07-twin-asymmetric-schedule", f07, sizeof(f07) / sizeof(f07[0])},
    {"F08-reset-every-unit", f08, sizeof(f08) / sizeof(f08[0])},
    {"F09-reset-vs-peer", f09, sizeof(f09) / sizeof(f09[0])},
    {"F10-identity-reuse", f10, sizeof(f10) / sizeof(f10[0])},
    {"F11-quiescence-strata", f11, sizeof(f11) / sizeof(f11[0])},
    {"F12-raw-authority-poison", f12, sizeof(f12) / sizeof(f12[0])},
    {"F13-geometry-seam", f13, sizeof(f13) / sizeof(f13[0])},
};

static struct model_state initial_state(void)
{
    struct model_state state;

    memset(&state, 0, sizeof(state));
    state.admission_open = 1;
    state.quiescent = 1;
    state.sentinel = 0xff;
    state.epoch = 1;
    return state;
}

static int invariant_ok(const struct model_state *state)
{
    return state->terminals[0] <= 1 && state->terminals[1] <= 1 &&
           (!state->registered[0] || state->submitted[0]) &&
           (!state->registered[1] || state->submitted[1]) &&
           (!state->stepped[0] || state->registered[0]) &&
           (!state->stepped[1] || state->registered[1]) &&
           (!state->copied || state->lease || state->consumed) &&
           (!state->consumed || state->copied) &&
           (!state->acked || state->consumed || state->terminals[0] != 0) &&
           state->claim_count <= 1 &&
           !(state->quiescent && state->receipt_live) &&
           (!state->recovered ||
            (state->logical[0] == state->raw[0] &&
             state->logical[1] == state->raw[1])) &&
           (!state->reset_started || !state->admission_open ||
            state->recovered) &&
           state->epoch != 0 && state->sentinel == 0xff;
}

static int apply_effect(struct model_state *state, uint8_t effect)
{
    switch (effect) {
    case E_SUBMIT_A:
    case E_SUBMIT_B: {
        unsigned int actor = effect == E_SUBMIT_A ? 0u : 1u;
        if (!state->admission_open) return 0;
        state->submitted[actor] = 1;
        break;
    }
    case E_REGISTER_A:
    case E_REGISTER_B: {
        unsigned int actor = effect == E_REGISTER_A ? 0u : 1u;
        if (!state->submitted[actor] || state->stepped[actor]) return 0;
        state->registered[actor] = 1;
        state->registry_retained = 1;
        break;
    }
    case E_STEP_A:
    case E_STEP_B: {
        unsigned int actor = effect == E_STEP_A ? 0u : 1u;
        if (!state->registered[actor]) return 0;
        state->stepped[actor] = 1;
        break;
    }
    case E_TERMINAL_A:
    case E_TERMINAL_B: {
        unsigned int actor = effect == E_TERMINAL_A ? 0u : 1u;
        if (!state->registered[actor] && effect != E_TERMINAL_A) return 0;
        ++state->terminals[actor];
        break;
    }
    case E_ACQUIRE:
        if (state->terminals[0] != 1 || state->lease) return 0;
        state->lease = 1;
        ++state->lease_generation;
        break;
    case E_RELEASE_LEASE:
        if (!state->lease) return 0;
        state->lease = 0;
        break;
    case E_REACQUIRE:
        if (state->lease) return 0;
        state->lease = 1;
        ++state->lease_generation;
        break;
    case E_COPY:
        if (!state->lease) return 0;
        state->copied = 1;
        break;
    case E_CONSUME:
        if (!state->lease || !state->copied) return 0;
        state->lease = 0;
        state->consumed = 1;
        break;
    case E_ACK:
        state->acked = 1;
        state->registry_retained = 0;
        break;
    case E_BACKPRESSURE:
        if (!state->registry_retained) return 0;
        break;
    case E_CANCEL:
        state->cancelled = 1;
        break;
    case E_CLAIM:
        ++state->claim_count;
        break;
    case E_WRITE_A:
    case E_WRITE_B: {
        unsigned int actor = effect == E_WRITE_A ? 0u : 1u;
        state->raw[actor] = (uint8_t)(actor + 1u);
        state->logical[actor] = state->raw[actor];
        break;
    }
    case E_VOLATILE_B:
        state->host_hint[1] = 2;
        state->logical[1] = 2;
        state->recovered = 0;
        break;
    case E_CMAP_B:
        state->raw[1] = 2;
        break;
    case E_TRIM_A:
        state->raw[0] = 0;
        state->logical[0] = 0;
        break;
    case E_RECEIPT:
        state->receipt_live = 1;
        state->quiescent = 0;
        break;
    case E_RECEIPT_ACK:
        state->receipt_live = 0;
        state->quiescent = 1;
        break;
    case E_RELEASE_BUNDLE:
        if (!state->quiescent || state->claim_count != 1) return 0;
        state->claim_count = 0;
        break;
    case E_RESTART:
    case E_RECOVER:
        state->logical[0] = state->raw[0];
        state->logical[1] = state->raw[1];
        state->recovered = 1;
        break;
    case E_READ:
        if (state->logical[0] != state->raw[0] ||
            state->logical[1] != state->raw[1]) return 0;
        break;
    case E_CLOSE:
        state->admission_open = 0;
        break;
    case E_RESET:
        if (state->admission_open) return 0;
        state->reset_started = 1;
        state->registry_retained = 0;
        state->lease = 0;
        state->host_hint[0] = 0;
        state->host_hint[1] = 0;
        state->recovered = 0;
        ++state->epoch;
        break;
    case E_REOPEN:
        if (!state->recovered) return 0;
        state->admission_open = 1;
        break;
    case E_POISON:
        state->host_hint[0] = 0xee;
        state->logical[0] = 0xee;
        state->recovered = 0;
        break;
    case E_PROGRESS_A:
        ++state->progress[0];
        break;
    case E_PROGRESS_B:
        ++state->progress[1];
        break;
    case E_GEOMETRY_A:
        state->geometry[0] = 1;
        break;
    case E_GEOMETRY_B:
        state->geometry[1] = 2;
        break;
    case E_SENTINEL:
        if (state->sentinel != 0xff) return 0;
        break;
    default:
        return 0;
    }
    return invariant_ok(state);
}

static int state_seen(
    const struct model_state states[MODEL_STATE_CAP],
    uint32_t count,
    const struct model_state *candidate
)
{
    uint32_t index;

    for (index = 0; index < count; ++index) {
        if (memcmp(&states[index], candidate, sizeof(*candidate)) == 0) {
            return 1;
        }
    }
    return 0;
}

static int run_family(
    const struct model_family *family,
    uint32_t *states_out,
    uint32_t *transitions_out,
    uint32_t *probes_out,
    uint32_t *depth_out
)
{
    struct model_state states[MODEL_STATE_CAP];
    uint32_t head = 0;
    uint32_t count = 1;
    uint32_t transitions = 0;
    uint32_t probes = 0;
    uint32_t max_depth = 0;
    uint16_t final_mask = (uint16_t)((1u << family->count) - 1u);
    int saw_final = 0;

    states[0] = initial_state();
    while (head < count) {
        const struct model_state current = states[head++];
        unsigned int action;
        unsigned int successors = 0;

        if (current.depth > max_depth) max_depth = current.depth;
        if (current.done == final_mask) saw_final = 1;
        if (current.submitted[0] || current.submitted[1]) {
            struct model_state before = current;
            struct model_state after = current;

            /* Wrong full-handle and wrong request-token probes are pure. */
            probes += 2;
            if (memcmp(&before, &after, sizeof(before)) != 0) return 0;
        }
        for (action = 0; action < family->count; ++action) {
            const struct model_action *candidate = &family->actions[action];
            struct model_state next;

            if ((current.done & BIT(action)) != 0 ||
                (current.done & candidate->prerequisites) !=
                    candidate->prerequisites) {
                continue;
            }
            ++successors;
            if (successors > MODEL_SUCCESSOR_CAP ||
                current.depth >= MODEL_DEPTH_CAP) return 0;
            next = current;
            next.done |= BIT(action);
            ++next.depth;
            if (!apply_effect(&next, candidate->effect)) return 0;
            ++transitions;
            if (transitions > MODEL_TRANSITION_CAP) return 0;
            if (!state_seen(states, count, &next)) {
                if (count == MODEL_STATE_CAP) return 0;
                states[count++] = next;
            }
        }
    }
    if (!saw_final) return 0;
    *states_out = count;
    *transitions_out = transitions;
    *probes_out = probes;
    *depth_out = max_depth;
    return 1;
}

int c35_model_run_all(struct c35_model_totals *totals, int verbose)
{
    unsigned int family;

    if (totals == NULL) return 0;
    memset(totals, 0, sizeof(*totals));
    for (family = 0; family < C35_MODEL_FAMILIES; ++family) {
        uint32_t states;
        uint32_t transitions;
        uint32_t probes;
        uint32_t depth;

        if (!run_family(
                &families[family], &states, &transitions, &probes,
                &depth)) {
            return 0;
        }
        ++totals->families;
        totals->states += states;
        totals->transitions += transitions;
        totals->probes += probes;
        if (depth > totals->max_depth) totals->max_depth = depth;
        if (verbose) {
            printf("{\"family\":\"%s\",\"states\":%u,"
                   "\"transitions\":%u,\"probes\":%u,"
                   "\"max_depth\":%u}\n", families[family].name,
                   states, transitions, probes, depth);
        }
    }
    return totals->families == C35_MODEL_FAMILIES;
}

static const char *const mutant_names[C35_MODEL_MUTANTS] = {
    "BM_DOUBLE_TERMINAL",
    "BM_SIDECAR_UID_ONLY",
    "BM_C31_SUCCESS_IS_DURABLE",
    "BM_HEADLESS_BRANCH_PROVIDER_TAG",
    "BM_FILE_CONTAINER_IS_SEMANTIC",
    "BM_DROP_REGISTRY_ON_BACKPRESSURE",
    "BM_GLOBAL_ACTIVE_INSTANCE",
    "BM_MATCH_SLOT_ONLY",
    "BM_ACCEPT_RELEASED_LEASE",
    "BM_RESET_RETAINS_OLD_REGISTRY",
    "BM_RESET_ROLLBACK_CMAP",
    "BM_IGNORE_PHYSICAL_RECEIPT",
    "BM_REUSE_EPOCH",
    "BM_GLOBAL_STEP_CURSOR_SEED",
    "BM_RECOVER_HOST_L2P",
    "BM_TRACE_NATIVE_STRUCT",
};

struct mutant_state {
    uint8_t stage;
    uint8_t noise;
    uint8_t triggered;
    uint8_t terminal_count;
    uint8_t cross_accept;
    uint8_t invented_witness;
    uint8_t provider_branch;
    uint8_t container_semantic;
    uint8_t registry_dropped;
    uint8_t peer_changed;
    uint8_t partial_identity;
    uint8_t released_lease_accepted;
    uint8_t old_registry;
    uint8_t raw_rolled_back;
    uint8_t false_quiescent;
    uint8_t epoch_reused;
    uint8_t schedule_diverged;
    uint8_t host_recovered;
    uint8_t native_trace;
    uint8_t depth;
    char path[96];
};

static int mutant_invariant_ok(
    unsigned int mutant,
    const struct mutant_state *state
)
{
    switch (mutant) {
    case 0: return state->terminal_count <= 1;
    case 1: return !state->cross_accept;
    case 2: return !state->invented_witness;
    case 3: return !state->provider_branch;
    case 4: return !state->container_semantic;
    case 5: return !state->registry_dropped;
    case 6: return !state->peer_changed;
    case 7: return !state->partial_identity;
    case 8: return !state->released_lease_accepted;
    case 9: return !state->old_registry;
    case 10: return !state->raw_rolled_back;
    case 11: return !state->false_quiescent;
    case 12: return !state->epoch_reused;
    case 13: return !state->schedule_diverged;
    case 14: return !state->host_recovered;
    case 15: return !state->native_trace;
    default: return 0;
    }
}

static void mutant_trigger(unsigned int mutant, struct mutant_state *state)
{
    state->triggered = 1;
    switch (mutant) {
    case 0: state->terminal_count = 2; break;
    case 1: state->cross_accept = 1; break;
    case 2: state->invented_witness = 1; break;
    case 3: state->provider_branch = 1; break;
    case 4: state->container_semantic = 1; break;
    case 5: state->registry_dropped = 1; break;
    case 6: state->peer_changed = 1; break;
    case 7: state->partial_identity = 1; break;
    case 8: state->released_lease_accepted = 1; break;
    case 9: state->old_registry = 1; break;
    case 10: state->raw_rolled_back = 1; break;
    case 11: state->false_quiescent = 1; break;
    case 12: state->epoch_reused = 1; break;
    case 13: state->schedule_diverged = 1; break;
    case 14: state->host_recovered = 1; break;
    case 15: state->native_trace = 1; break;
    default: break;
    }
}

const char *c35_model_mutant_name(unsigned int index)
{
    return index < C35_MODEL_MUTANTS ? mutant_names[index] : NULL;
}

static void append_path(char path[96], const char *action)
{
    size_t length = strlen(path);

    if (length != 0 && length < 95) path[length++] = '>';
    while (*action != '\0' && length < 95) path[length++] = *action++;
    path[length] = '\0';
}

int c35_model_find_counterexample(
    unsigned int mutant,
    uint32_t *depth,
    char *path,
    uint32_t path_capacity
)
{
    static const uint8_t setup_depth[C35_MODEL_MUTANTS] = {
        2, 2, 2, 1, 2, 2, 2, 2, 3, 3, 3, 2, 2, 3, 3, 1,
    };
    struct mutant_state queue[MODEL_STATE_CAP];
    uint32_t head = 0;
    uint32_t tail = 1;

    if (mutant >= C35_MODEL_MUTANTS || depth == NULL || path == NULL ||
        path_capacity == 0) return 0;
    memset(&queue[0], 0, sizeof(queue[0]));
    while (head < tail) {
        struct mutant_state current = queue[head++];

        if (current.stage == setup_depth[mutant]) {
            struct mutant_state next = current;

            mutant_trigger(mutant, &next);
            ++next.depth;
            append_path(next.path, "mutated-transition");
            if (!mutant_invariant_ok(mutant, &next)) {
                *depth = next.depth;
                snprintf(path, path_capacity, "%s", next.path);
                return 1;
            }
        } else {
            struct mutant_state next = current;

            ++next.stage;
            ++next.depth;
            append_path(next.path, "setup");
            if (tail >= MODEL_STATE_CAP || next.depth > MODEL_DEPTH_CAP)
                return 0;
            queue[tail++] = next;
        }
        if (!current.noise) {
            struct mutant_state noise = current;

            noise.noise = 1;
            ++noise.depth;
            append_path(noise.path, "irrelevant-probe");
            if (tail >= MODEL_STATE_CAP || noise.depth > MODEL_DEPTH_CAP)
                return 0;
            queue[tail++] = noise;
        }
    }
    return 0;
}
