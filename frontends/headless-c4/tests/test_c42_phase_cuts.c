/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_support.h"

#include <stdio.h>
#include <string.h>

static int failures;
static uint32_t executions;
static uint32_t requested_cut_count;
static uint32_t distinct_cut_count;

struct semantic_queue_key {
    uint16_t host_index;
    uint16_t device_index;
    uint16_t pending;
    uint16_t unacked;
    uint16_t reserved;
    uint8_t life;
    uint8_t slot_state[C42_MAX_QUEUE_DEPTH];
    uint8_t slot_phase[C42_MAX_QUEUE_DEPTH];
    uint16_t slot_command_id[C42_MAX_QUEUE_DEPTH];
};

struct semantic_candidate_key {
    uint32_t state;
    uint8_t in_use;
    uint8_t kind;
    uint8_t scrub_started;
    uint8_t retire_started;
    uint8_t provider_retired;
};

struct semantic_command_key {
    uint16_t command_id;
    uint16_t sqhd_snapshot;
    uint8_t state;
    uint8_t prepared_origin_matches;
    uint8_t ticket_identity_matches;
    uint8_t ready_ticket_matches;
    uint8_t lease_ticket_matches;
    uint8_t consume_known;
    uint8_t publication_in_use;
    uint8_t body_prefix;
    uint8_t body_started;
    uint8_t marker_started;
    uint8_t marker_visible;
    uint8_t reconcile_in_use;
    uint8_t reconcile_state;
    uint8_t reconcile_consume_known;
    uint8_t notification_in_use;
    uint8_t notification_state;
};

struct semantic_control_key {
    uint32_t state;
    uint8_t in_use;
    uint8_t kind;
    uint8_t port_started;
    uint8_t memory_started;
};

struct semantic_cut_key {
    uint32_t phase;
    uint32_t fault_cause;
    uint8_t teardown;
    uint8_t admission_paused;
    uint8_t ready_poll_pending;
    struct semantic_queue_key sq[C42_MAX_QUEUE_PAIRS];
    struct semantic_queue_key cq[C42_MAX_QUEUE_PAIRS];
    struct semantic_candidate_key candidates[C42_MAX_QUEUE_PAIRS * 2u];
    struct semantic_command_key commands[C42_MAX_COMMANDS];
    struct semantic_control_key controls[4];
};

static struct semantic_cut_key distinct_cut_keys[256];

static void check(int condition, const char *label)
{
    if (!condition) {
        fprintf(stderr, "phase cuts FAIL: %s\n", label);
        failures++;
    }
}

static void semantic_cut_normalize(
    const struct c42_observer_v2 *observer,
    int teardown,
    struct semantic_cut_key *key)
{
    uint32_t index;

    memset(key, 0, sizeof(*key));
    key->phase = observer->phase;
    key->fault_cause = observer->fault_cause;
    key->teardown = (uint8_t)(teardown != 0);
    key->admission_paused = observer->admission_paused;
    key->ready_poll_pending = observer->ready_poll_pending;
    for (index = 0; index < C42_MAX_QUEUE_PAIRS; ++index) {
        uint32_t slot;

        key->sq[index].life = observer->sq[index].life;
        key->sq[index].host_index = observer->sq[index].host_index;
        key->sq[index].device_index = observer->sq[index].device_index;
        key->sq[index].pending = observer->sq[index].pending;
        key->cq[index].life = observer->cq[index].life;
        key->cq[index].host_index = observer->cq[index].host_index;
        key->cq[index].device_index = observer->cq[index].device_index;
        key->cq[index].unacked = observer->cq[index].unacked_count;
        key->cq[index].reserved = observer->cq[index].reserved_count;
        for (slot = 0; slot < observer->cq[index].depth; ++slot) {
            key->cq[index].slot_state[slot] =
                observer->cq[index].slots[slot].state;
            key->cq[index].slot_phase[slot] =
                observer->cq[index].slots[slot].phase;
            key->cq[index].slot_command_id[slot] =
                observer->cq[index].slots[slot].command_id;
        }
    }
    for (index = 0; index < C42_MAX_QUEUE_PAIRS * 2u; ++index) {
        key->candidates[index].in_use = observer->candidates[index].in_use;
        key->candidates[index].state = observer->candidates[index].state;
        key->candidates[index].kind = observer->candidates[index].kind;
        key->candidates[index].scrub_started =
            observer->candidates[index].scrub_started;
        key->candidates[index].retire_started =
            observer->candidates[index].retire_started;
        key->candidates[index].provider_retired =
            observer->candidates[index].provider_retired;
    }
    for (index = 0; index < observer->command_capacity; ++index) {
        struct semantic_command_key *command = &key->commands[index];

        command->state = observer->commands[index].state;
        command->command_id = observer->commands[index].command_id;
        command->sqhd_snapshot = observer->commands[index].sqhd_snapshot;
        command->prepared_origin_matches =
            observer->commands[index].prepared_origin_matches;
        command->ticket_identity_matches =
            observer->commands[index].ticket_identity_matches;
        command->ready_ticket_matches =
            observer->commands[index].ready_ticket_matches;
        command->lease_ticket_matches =
            observer->commands[index].lease_ticket_matches;
        command->consume_known = observer->commands[index].consume_known;
        command->publication_in_use = observer->publications[index].in_use;
        command->body_prefix = (uint8_t)observer->publications[index].body_prefix;
        command->body_started = observer->publications[index].body_started;
        command->marker_started = observer->publications[index].marker_started;
        command->marker_visible = observer->publications[index].marker_visible;
        command->reconcile_in_use = observer->reconciles[index].in_use;
        command->reconcile_state = observer->reconciles[index].state;
        command->reconcile_consume_known =
            observer->reconciles[index].consume_known;
        command->notification_in_use = observer->notifications[index].in_use;
        command->notification_state = observer->notifications[index].state;
    }
    for (index = 0; index < 4; ++index) {
        key->controls[index].in_use = observer->controls[index].in_use;
        key->controls[index].kind = observer->controls[index].kind;
        key->controls[index].state = observer->controls[index].state;
        key->controls[index].port_started =
            observer->controls[index].port_started;
        key->controls[index].memory_started =
            observer->controls[index].memory_started;
    }
}

static int register_semantic_cut(
    const struct c42_test_fixture *fixture,
    int teardown,
    const char *label)
{
    struct c42_observer_v2 observer;
    struct semantic_cut_key key;
    uint32_t index;

    requested_cut_count++;
    if (c42_observer_read_v2(fixture->controller, &observer) != C42_OK) {
        check(0, label);
        return 0;
    }
    semantic_cut_normalize(&observer, teardown, &key);
    for (index = 0; index < distinct_cut_count; ++index) {
        if (memcmp(&distinct_cut_keys[index], &key, sizeof(key)) == 0) {
            return 0;
        }
    }
    if (distinct_cut_count >=
        sizeof(distinct_cut_keys) / sizeof(distinct_cut_keys[0])) {
        check(0, "semantic cut key capacity");
        return 0;
    }
    distinct_cut_keys[distinct_cut_count++] = key;
    return 1;
}

static struct c42_queue_memory_cap fresh_cap(
    const struct c42_test_fixture *fixture,
    uint16_t queue_id,
    uint64_t memory_uid)
{
    struct c42_queue_memory_cap cap = {0};

    cap.instance_nonce = fixture->config.instance_nonce;
    cap.owner_epoch = fixture->config.owner_epoch;
    cap.memory_uid = memory_uid;
    cap.controller_epoch = fixture->config.initial_controller_epoch;
    cap.ring_generation = 1;
    cap.mapping_generation = 1;
    cap.exact_bytes = (uint32_t)fixture->depth * C42_CQE_BYTES;
    cap.queue_id = queue_id;
    cap.role = C42_MEMORY_CQ_PUBLISH;
    return cap;
}

static struct c42_queue_descriptor cq_descriptor(
    const struct c42_test_fixture *fixture,
    const struct c42_queue_memory_cap *cap)
{
    struct c42_queue_descriptor descriptor = {0};

    descriptor.version = C42_COMPONENT_VERSION;
    descriptor.size = sizeof(descriptor);
    descriptor.queue_id = cap->queue_id;
    descriptor.associated_cq_id = cap->queue_id;
    descriptor.depth = fixture->depth;
    descriptor.kind = C42_QUEUE_CQ;
    descriptor.queue_class = FWLAB_NVME_QUEUE_IO;
    descriptor.memory = *cap;
    return descriptor;
}

static int finish_cut(struct c42_test_fixture *fixture, int teardown)
{
    struct c42_operation_token token = {0};
    struct c42_control_status status = {0};
    struct c42_snapshot snapshot = {0};
    uint32_t attempt;
    enum c42_result result = teardown != 0 ?
        c42_teardown_start(fixture->controller, &token) :
        c42_reset_start(fixture->controller, &token);

    if (result != C42_OK) {
        return 0;
    }
    for (attempt = 0; attempt < 512; ++attempt) {
        result = c42_control_progress(fixture->controller, &token, 1);
        if (result != C42_OK && result != C42_IN_PROGRESS) {
            return 0;
        }
        if (c42_control_query(
                fixture->controller, &token, &status) == C42_OK &&
            status.state == C42_CONTROL_COMMITTED) {
            break;
        }
    }
    if (attempt == 512 ||
        c42_snapshot_read(fixture->controller, &snapshot) != C42_OK) {
        return 0;
    }
    return teardown != 0 ?
           snapshot.phase == C42_CONTROLLER_DEAD :
           snapshot.phase == C42_CONTROLLER_COLD_NO_QUEUES;
}

static void observe_masks(
    const struct c42_test_fixture *fixture,
    uint32_t *command_mask,
    uint32_t *reconcile_mask,
    uint32_t *notification_mask,
    uint32_t *candidate_mask,
    uint32_t *control_mask)
{
    struct c42_observer_v2 observer;
    uint32_t index;

    if (c42_observer_read_v2(fixture->controller, &observer) != C42_OK) {
        failures++;
        return;
    }
    for (index = 0; index < observer.command_capacity; ++index) {
        if (observer.commands[index].state < 32) {
            *command_mask |= UINT32_C(1) << observer.commands[index].state;
        }
        if (observer.reconciles[index].in_use != 0 &&
            observer.reconciles[index].state < 32) {
            *reconcile_mask |= UINT32_C(1) <<
                               observer.reconciles[index].state;
        }
        if (observer.notifications[index].in_use != 0 &&
            observer.notifications[index].state < 32) {
            *notification_mask |= UINT32_C(1) <<
                                  observer.notifications[index].state;
        }
    }
    for (index = 0; index < C42_MAX_QUEUE_PAIRS * 2u; ++index) {
        if (observer.candidates[index].in_use != 0 &&
            observer.candidates[index].state < 32) {
            *candidate_mask |= UINT32_C(1) << observer.candidates[index].state;
        }
    }
    for (index = 0; index < 4; ++index) {
        if (observer.controls[index].in_use != 0 &&
            observer.controls[index].state < 32) {
            *control_mask |= UINT32_C(1) << observer.controls[index].state;
        }
    }
}

static void push_publication_script(struct c42_fake_memory *memory)
{
    struct c42_fake_memory_outcome outcome = {0};

    outcome.operation = C42_FAKE_MEMORY_BODY;
    outcome.effect = C42_MEMORY_EXACT_PREFIX;
    outcome.prefix = 7;
    check(c42_fake_memory_script_push(memory, &outcome) == C42_OK,
          "body prefix script");
    outcome.effect = C42_MEMORY_FULL;
    outcome.prefix = 15;
    outcome.committed = 1;
    check(c42_fake_memory_script_push(memory, &outcome) == C42_OK,
          "body full script");
    memset(&outcome, 0, sizeof(outcome));
    outcome.operation = C42_FAKE_MEMORY_MARKER;
    outcome.effect = C42_MEMORY_UNKNOWN;
    outcome.committed = 1;
    check(c42_fake_memory_script_push(memory, &outcome) == C42_OK,
          "marker unknown script");
    outcome.effect = C42_MEMORY_FULL;
    check(c42_fake_memory_script_push(memory, &outcome) == C42_OK,
          "marker full script");
}

static int run_until_command_state(
    struct c42_test_fixture *fixture,
    uint8_t expected,
    uint32_t limit)
{
    uint32_t step;

    for (step = 0; step < limit; ++step) {
        struct c42_observer_v2 observer;
        struct c42_step_result result = {0};
        enum c42_result step_result;
        uint32_t index;

        if (c42_observer_read_v2(
                fixture->controller, &observer) != C42_OK) {
            return 0;
        }
        for (index = 0; index < observer.command_capacity; ++index) {
            if (observer.commands[index].state == expected) {
                return 1;
            }
        }
        step_result = c42_step(fixture->controller, 1, &result);
        if ((step_result != C42_OK && step_result != C42_FAULTED) ||
            result.units_executed == 0) {
            return 0;
        }
    }
    return 0;
}

static void test_abnormal_command_cuts(uint32_t *command_mask)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_injection injection = {0};
    uint32_t unused = 0;

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4400800000000001)),
          "consume-prepare cut fixture");
    injection.operation = C42_FAKE_COMMAND_CONSUME_PREPARE;
    injection.result = FWLAB_HIF_PORT_IN_PROGRESS;
    injection.omit_outputs = 1;
    check(c42_fake_command_injection_push(
              &fixture.command, &injection) == C42_OK &&
          c42_test_submit(&fixture, 0, 0, 1, 407) &&
          run_until_command_state(
              &fixture, C42_OBSERVER_COMMAND_CONSUME_PREPARE, 128),
          "command CONSUME_PREPARE reached");
    observe_masks(
        &fixture, command_mask, &unused, &unused, &unused, &unused
    );
    (void)register_semantic_cut(&fixture, 0, "consume-prepare command cut");

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4400800000000002)),
          "admit-poison cut fixture");
    memset(&injection, 0, sizeof(injection));
    injection.operation = C42_FAKE_COMMAND_ADMIT;
    injection.result = FWLAB_HIF_PORT_OK;
    injection.value = FWLAB_HIF_ADMISSION_POISONED + 1u;
    injection.write_mask = C42_FAKE_WRITE_VALUE | C42_FAKE_WRITE_OBJECT;
    injection.object_variant = C42_FAKE_OBJECT_EXACT;
    check(c42_fake_command_injection_push(
              &fixture.command, &injection) == C42_OK &&
          c42_test_submit(&fixture, 0, 0, 1, 408) &&
          run_until_command_state(
              &fixture, C42_OBSERVER_COMMAND_ADMIT_POISON_HOLD, 128),
          "command ADMIT_POISON_HOLD reached");
    observe_masks(
        &fixture, command_mask, &unused, &unused, &unused, &unused
    );
    (void)register_semantic_cut(&fixture, 0, "admit-poison command cut");

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4400800000000003)),
          "consume-poison cut fixture");
    memset(&injection, 0, sizeof(injection));
    injection.operation = C42_FAKE_COMMAND_CONSUME_PREPARE;
    injection.result = FWLAB_HIF_PORT_OK;
    injection.value = FWLAB_HIF_CONSUME_POISONED + 1u;
    injection.write_mask = C42_FAKE_WRITE_VALUE | C42_FAKE_WRITE_OBJECT;
    injection.flags = C42_FAKE_APPLY_EFFECT;
    injection.requested_effect =
        C42_FAKE_COMMAND_EFFECT_CONSUME_PREPARED;
    injection.object_variant = C42_FAKE_OBJECT_EXACT;
    check(c42_fake_command_injection_push(
              &fixture.command, &injection) == C42_OK &&
          c42_test_submit(&fixture, 0, 0, 1, 409) &&
          run_until_command_state(
              &fixture, C42_OBSERVER_COMMAND_CONSUME_POISON_HOLD, 128),
          "command CONSUME_POISON_HOLD reached");
    observe_masks(
        &fixture, command_mask, &unused, &unused, &unused, &unused
    );
    (void)register_semantic_cut(&fixture, 0, "consume-poison command cut");
}

static void test_command_publication_cuts(
    uint32_t *command_mask,
    uint32_t *reconcile_mask,
    uint32_t *notification_mask)
{
    uint32_t cut;
    uint32_t mode;

    for (mode = 0; mode < 2; ++mode) {
        for (cut = 0; cut < 48; ++cut) {
            struct c42_test_fixture fixture;
            struct c42_fake_command_script script = {0};
            uint32_t step;
            uint32_t unused = 0;

            executions++;
            check(c42_test_fixture_init_with_nonce(
                      &fixture, 4, 0,
                      UINT64_C(0x4400000000000000) +
                      (uint64_t)mode * 0x100u + cut + 1u),
                  "command cut fixture");
            script.prepare_delay = 2;
            script.admit_delay = 2;
            script.poll_delay = 2;
            script.consume_commit_delay = 2;
            script.cleanup_pending = 1;
            script.cleanup_delay = 2;
            c42_fake_command_set_script(&fixture.command, &script);
            push_publication_script(&fixture.memory);
            check(c42_test_submit(&fixture, 0, 0, 1, 401),
                  "command cut submit");
            for (step = 0; step < cut; ++step) {
                struct c42_step_result result = {0};

                if (c42_step(fixture.controller, 1, &result) != C42_OK ||
                    result.units_executed == 0) {
                    break;
                }
            }
            observe_masks(
                &fixture, command_mask, reconcile_mask, notification_mask,
                &unused, &unused
            );
            (void)register_semantic_cut(
                &fixture, mode != 0, "command semantic cut"
            );
            check(finish_cut(&fixture, mode != 0),
                  "command publication reset/teardown cut");
        }
    }
}

static void prepare_candidate_phase(
    struct c42_test_fixture *fixture,
    uint32_t phase,
    struct c42_operation_token *candidate)
{
    struct c42_queue_memory_cap cap = fresh_cap(
        fixture, 1, UINT64_C(0x4401000000000000) + phase
    );
    struct c42_queue_descriptor descriptor = cq_descriptor(fixture, &cap);
    struct c42_fake_memory_outcome unknown = {0};
    struct c42_fake_memory_outcome full = {0};

    unknown.operation = C42_FAKE_MEMORY_SCRUB;
    unknown.effect = C42_MEMORY_UNKNOWN;
    unknown.committed = 1;
    full.operation = C42_FAKE_MEMORY_SCRUB;
    full.effect = C42_MEMORY_FULL;
    full.prefix = (uint8_t)fixture->depth;
    full.committed = 1;
    check(c42_fake_memory_map(
              &fixture->memory, &cap, fixture->depth) == C42_OK &&
          c42_fake_memory_script_push(
              &fixture->memory, &unknown) == C42_OK &&
          c42_fake_memory_script_push(
              &fixture->memory, &full) == C42_OK &&
          c42_candidate_prepare(
              fixture->controller, &descriptor, candidate) == C42_OK,
          "candidate cut prepare");
    if (phase >= 1) {
        check(c42_candidate_progress(
                  fixture->controller, candidate, 1) == C42_OK,
              "candidate cut unknown");
    }
    if (phase >= 2) {
        check(c42_candidate_progress(
                  fixture->controller, candidate, 1) == C42_OK,
              "candidate cut ready");
    }
    if (phase >= 3) {
        check(c42_candidate_commit(
                  fixture->controller, candidate) == C42_OK,
              "candidate cut commit");
    }
    if (phase >= 4) {
        struct c42_fake_memory_direct_injection direct = {0};

        direct.operation = C42_FAKE_MEMORY_SCRUB_RETIRE;
        direct.result = C42_MEMORY_OK;
        direct.write_status = 1;
        direct.apply_effect = 1;
        direct.logical_effect = C42_MEMORY_UNKNOWN;
        direct.applied_effect = C42_MEMORY_RETIRED;
        direct.committed = 1;
        direct.quiescent = 1;
        check(c42_fake_memory_direct_push(
                  &fixture->memory, &direct) == C42_OK &&
              c42_candidate_retire(
                  fixture->controller, candidate) == C42_IN_PROGRESS,
              "candidate retire reported-unknown cut");
    }
    if (phase >= 5) {
        check(c42_candidate_retire(
                  fixture->controller, candidate) == C42_IN_PROGRESS,
              "candidate retire-ready cut");
    }
}

static void test_candidate_cuts(uint32_t *candidate_mask)
{
    uint32_t phase;
    uint32_t mode;

    for (mode = 0; mode < 2; ++mode) {
        for (phase = 0; phase < 6; ++phase) {
            struct c42_test_fixture fixture;
            struct c42_operation_token candidate = {0};
            struct c42_candidate_status candidate_status = {0};
            static const uint32_t expected_state[] = {
                C42_CANDIDATE_PREPARED,
                C42_CANDIDATE_SCRUB_UNKNOWN,
                C42_CANDIDATE_READY,
                C42_CANDIDATE_COMMITTED_AWAIT_RETIRE,
                C42_CANDIDATE_RETIRE_UNKNOWN,
                C42_CANDIDATE_RETIRE_READY,
            };
            uint32_t unused = 0;

            executions++;
            check(c42_test_fixture_init_with_nonce(
                      &fixture, 4, 0,
                      UINT64_C(0x4402000000000000) +
                      (uint64_t)mode * 0x100u + phase + 1u),
                  "candidate cut fixture");
            prepare_candidate_phase(&fixture, phase, &candidate);
            check(c42_candidate_query(
                      fixture.controller, &candidate,
                      &candidate_status) == C42_OK &&
                  candidate_status.state == expected_state[phase],
                  "candidate intended pre-LP state reached");
            observe_masks(
                &fixture, &unused, &unused, &unused,
                candidate_mask, &unused
            );
            (void)register_semantic_cut(
                &fixture, mode != 0, "candidate semantic cut"
            );
            check(finish_cut(&fixture, mode != 0),
                  "candidate reset/teardown cut");
        }
    }
}

static void test_retire_unknown_post_lp(void)
{
    uint32_t mode;

    for (mode = 0; mode < 2; ++mode) {
        struct c42_test_fixture fixture;
        struct c42_operation_token candidate = {0};
        struct c42_operation_token control = {0};
        struct c42_candidate_status status = {0};
        uint32_t provider_events;

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0x4402800000000000) + mode + 1u),
              "retire-unknown post-LP fixture");
        prepare_candidate_phase(&fixture, 4, &candidate);
        check(c42_candidate_query(
                  fixture.controller, &candidate, &status) == C42_OK &&
              status.state == C42_CANDIDATE_RETIRE_UNKNOWN,
              "retire-unknown exact pre-LP state");
        provider_events = fixture.event_log.count;
        check((mode == 0 ?
               c42_reset_start(fixture.controller, &control) :
               c42_teardown_start(fixture.controller, &control)) == C42_OK,
              "retire-unknown LP succeeds");
        memset(&status, 0, sizeof(status));
        check(c42_candidate_query(
                  fixture.controller, &candidate, &status) == C42_OK &&
              status.state == C42_CANDIDATE_SUPERSEDED,
              "retire-unknown query is immediately superseded");
        check(c42_candidate_progress(
                  fixture.controller, &candidate, 1) == C42_SUPERSEDED &&
              c42_candidate_commit(
                  fixture.controller, &candidate) == C42_SUPERSEDED &&
              c42_candidate_abort(
                  fixture.controller, &candidate) == C42_SUPERSEDED &&
              c42_candidate_retire(
                  fixture.controller, &candidate) == C42_SUPERSEDED,
              "all retire-unknown mutators reject after LP");
        check(fixture.event_log.count == provider_events,
              "post-LP candidate access makes no provider call");
    }
}

static void record_candidate_query_state(
    struct c42_test_fixture *fixture,
    const struct c42_operation_token *candidate,
    uint32_t expected,
    uint32_t *candidate_mask,
    const char *label)
{
    struct c42_candidate_status status = {0};

    check(c42_candidate_query(
              fixture->controller, candidate, &status) == C42_OK &&
          status.state == expected,
          label);
    if (status.state < 32) {
        *candidate_mask |= UINT32_C(1) << status.state;
    }
}

static void test_abnormal_candidate_states(uint32_t *candidate_mask)
{
    struct c42_test_fixture fixture;
    struct c42_operation_token candidate = {0};
    struct c42_operation_token reset = {0};
    struct c42_fake_memory_direct_injection direct = {0};

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4402850000000001)),
          "candidate aborted fixture");
    prepare_candidate_phase(&fixture, 0, &candidate);
    check(c42_candidate_abort(
              fixture.controller, &candidate) == C42_OK,
          "candidate aborted transition");
    record_candidate_query_state(
        &fixture, &candidate, C42_CANDIDATE_ABORTED,
        candidate_mask, "candidate ABORTED reached"
    );
    (void)register_semantic_cut(&fixture, 0, "candidate aborted cut");

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4402850000000002)),
          "candidate aborting fixture");
    prepare_candidate_phase(&fixture, 1, &candidate);
    check(c42_candidate_abort(
              fixture.controller, &candidate) == C42_OK,
          "candidate aborting transition");
    record_candidate_query_state(
        &fixture, &candidate, C42_CANDIDATE_ABORTING,
        candidate_mask, "candidate ABORTING reached"
    );
    (void)register_semantic_cut(&fixture, 0, "candidate aborting cut");

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4402850000000003)),
          "candidate poisoned fixture");
    prepare_candidate_phase(&fixture, 0, &candidate);
    direct.operation = C42_FAKE_MEMORY_SCRUB;
    direct.result = C42_MEMORY_POISONED;
    direct.omit_status = 1;
    check(c42_fake_memory_direct_push(
              &fixture.memory, &direct) == C42_OK &&
          c42_candidate_progress(
              fixture.controller, &candidate, 1) == C42_POISONED,
          "candidate poison provider result");
    record_candidate_query_state(
        &fixture, &candidate, C42_CANDIDATE_POISONED,
        candidate_mask, "candidate POISONED reached"
    );
    (void)register_semantic_cut(&fixture, 0, "candidate poisoned cut");

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4402850000000004)),
          "candidate superseded fixture");
    prepare_candidate_phase(&fixture, 0, &candidate);
    check(c42_reset_start(fixture.controller, &reset) == C42_OK,
          "candidate supersede LP");
    record_candidate_query_state(
        &fixture, &candidate, C42_CANDIDATE_SUPERSEDED,
        candidate_mask, "candidate SUPERSEDED reached"
    );
    (void)register_semantic_cut(&fixture, 0, "candidate superseded cut");

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4402850000000005)),
          "candidate retired fixture");
    prepare_candidate_phase(&fixture, 5, &candidate);
    check(c42_candidate_retire(
              fixture.controller, &candidate) == C42_OK,
          "candidate retire tombstone transition");
    record_candidate_query_state(
        &fixture, &candidate, C42_CANDIDATE_RETIRED,
        candidate_mask, "candidate RETIRED reached"
    );
    (void)register_semantic_cut(&fixture, 0, "candidate retired cut");
}

static void test_business_control_post_lp(uint32_t *control_mask)
{
    uint32_t mode;

    for (mode = 0; mode < 2; ++mode) {
        struct c42_test_fixture fixture;
        struct c42_operation_token business = {0};
        struct c42_operation_token epoch = {0};
        struct c42_control_status status = {0};
        uint32_t provider_events;

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0x4402900000000000) + mode + 1u) &&
              c42_delete_start(
                  fixture.controller, C42_QUEUE_SQ, 0,
                  &business) == C42_OK,
              "business-control post-LP fixture");
        check(c42_control_query(
                  fixture.controller, &business, &status) == C42_OK &&
              status.state == C42_CONTROL_STARTED,
              "business control exact pre-LP state");
        *control_mask |= UINT32_C(1) << status.state;
        provider_events = fixture.event_log.count;
        check((mode == 0 ?
               c42_reset_start(fixture.controller, &epoch) :
               c42_teardown_start(fixture.controller, &epoch)) == C42_OK,
              "business-control LP succeeds");
        memset(&status, 0, sizeof(status));
        check(c42_control_query(
                  fixture.controller, &business, &status) == C42_OK &&
              status.state == C42_CONTROL_SUPERSEDED &&
              c42_control_progress(
                  fixture.controller, &business, 1) == C42_SUPERSEDED,
              "business control query/progress superseded after LP");
        *control_mask |= UINT32_C(1) << status.state;
        check(fixture.event_log.count == provider_events &&
              c42_control_retire(
                  fixture.controller, &business) == C42_OK,
              "business-control post-LP cleanup is provider-free");
    }
}

static void test_poisoned_control(uint32_t *control_mask)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_operation_token reset = {0};
    struct c42_control_status status = {0};

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4402950000000001)),
          "poisoned control fixture");
    script.inject_operation = C42_FAKE_COMMAND_RESET_BEGIN;
    script.inject_result = FWLAB_HIF_PORT_POISONED;
    script.inject_count = 1;
    script.inject_omit_outputs = 1;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_reset_start(fixture.controller, &reset) == C42_OK &&
          c42_control_progress(
              fixture.controller, &reset, 1) == C42_POISONED &&
          c42_control_query(
              fixture.controller, &reset, &status) == C42_OK &&
          status.state == C42_CONTROL_POISONED,
          "control provider poison state reached");
    *control_mask |= UINT32_C(1) << status.state;
    (void)register_semantic_cut(&fixture, 0, "poisoned control cut");
}

static void test_committed_control(uint32_t *control_mask)
{
    struct c42_test_fixture fixture;
    struct c42_operation_token reset = {0};
    struct c42_control_status status = {0};
    uint32_t step;

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4402960000000001)) &&
          c42_reset_start(fixture.controller, &reset) == C42_OK,
          "committed control fixture");
    for (step = 0; step < 32; ++step) {
        check(c42_control_progress(
                  fixture.controller, &reset, 1) == C42_OK,
              "committed control progress");
        if (c42_control_query(
                fixture.controller, &reset, &status) == C42_OK &&
            status.state == C42_CONTROL_COMMITTED) {
            break;
        }
    }
    check(step < 32 && status.state == C42_CONTROL_COMMITTED,
          "control COMMITTED reached independently of takeover");
    *control_mask |= UINT32_C(1) << status.state;
    (void)register_semantic_cut(&fixture, 0, "committed control cut");
}

static void test_notification_post_lp(void)
{
    uint32_t mode;

    for (mode = 0; mode < 2; ++mode) {
        struct c42_test_fixture fixture;
        struct c42_notification notification = {0};
        struct c42_notification queried = {0};
        struct c42_operation_token control = {0};
        uint32_t provider_events;

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0x4402a00000000000) + mode + 1u) &&
              c42_test_submit(&fixture, 0, 0, 1, 405) &&
              c42_test_run(&fixture, 128, 4) &&
              c42_notification_acquire(
                  fixture.controller, &notification) == C42_OK,
              "notification post-LP fixture");
        provider_events = fixture.event_log.count;
        check((mode == 0 ?
               c42_reset_start(fixture.controller, &control) :
               c42_teardown_start(fixture.controller, &control)) == C42_OK,
              "notification LP succeeds");
        check(c42_notification_query(
                  fixture.controller, &notification.token,
                  &queried) == C42_OK &&
              queried.state == C42_NOTIFICATION_SUPPRESSED &&
              c42_notification_consume(
                  fixture.controller, &notification.token) ==
                  C42_SUPERSEDED,
              "notification immediately suppressed after LP");
        check(fixture.event_log.count == provider_events &&
              c42_notification_retire(
                  fixture.controller, &notification.token) == C42_OK,
              "notification post-LP cleanup is provider-free");
    }
}

static void test_target_post_lp(void)
{
    uint32_t mode;

    for (mode = 0; mode < 2; ++mode) {
        struct c42_test_fixture fixture;
        struct c42_fake_command_script script = {0};
        struct c42_target_ref target = {0};
        struct c42_operation_token control = {0};
        uint32_t provider_events;

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0x4402b00000000000) + mode + 1u),
              "target post-LP fixture");
        script.poll_delay = 100;
        c42_fake_command_set_script(&fixture.command, &script);
        check(c42_test_submit(&fixture, 0, 0, 1, 406) &&
              c42_test_run(&fixture, 32, 1) &&
              c42_target_prepare(
                  fixture.controller, 0,
                  fixture.sq_cap[0].ring_generation, 406,
                  &target) == C42_OK,
              "target exact pre-LP state");
        provider_events = fixture.event_log.count;
        check((mode == 0 ?
               c42_reset_start(fixture.controller, &control) :
               c42_teardown_start(fixture.controller, &control)) == C42_OK,
              "target LP succeeds");
        check(c42_target_release(
                  fixture.controller, &target.token) == C42_STALE &&
              fixture.event_log.count == provider_events,
              "old target is revoked without provider call after LP");
    }
}

static void test_reset_teardown_takeover_cuts(uint32_t *control_mask)
{
    uint32_t cut;

    for (cut = 0; cut < 4; ++cut) {
        struct c42_test_fixture fixture;
        struct c42_fake_command_script script = {0};
        struct c42_operation_token reset = {0};
        struct c42_operation_token teardown = {0};
        struct c42_control_status status = {0};
        struct c42_snapshot snapshot = {0};
        struct c42_observer_v2 observer;
        const struct c42_observer_control_v2 *reset_observer = NULL;
        uint32_t step;
        uint32_t index;
        uint32_t unused = 0;

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0x4403000000000000) + cut + 1u),
              "takeover cut fixture");
        script.cleanup_pending = 1;
        script.cleanup_delay = 3;
        script.inject_operation = C42_FAKE_COMMAND_RESET_QUIESCENT;
        script.inject_result = FWLAB_HIF_PORT_IN_PROGRESS;
        script.inject_count = 3;
        script.inject_omit_outputs = 1;
        c42_fake_command_set_script(&fixture.command, &script);
        check(c42_test_submit(&fixture, 0, 0, 1, 402) &&
              c42_test_run(&fixture, 64, 4) &&
              c42_reset_start(fixture.controller, &reset) == C42_OK,
              "takeover reset start");
        for (step = 0; step < cut; ++step) {
            enum c42_result result = c42_control_progress(
                fixture.controller, &reset, 1
            );

            if (result != C42_OK && result != C42_IN_PROGRESS) {
                break;
            }
        }
        check(step == cut &&
              c42_control_query(
                  fixture.controller, &reset, &status) == C42_OK &&
              status.state ==
                  (cut == 3 ? C42_CONTROL_WAITING : C42_CONTROL_STARTED) &&
              c42_observer_read_v2(
                  fixture.controller, &observer) == C42_OK,
              "takeover intended reset control phase reached");
        for (index = 0; index < 4; ++index) {
            if (observer.controls[index].in_use != 0 &&
                observer.controls[index].kind == C42_CONTROL_RESET) {
                reset_observer = &observer.controls[index];
                break;
            }
        }
        check(reset_observer != NULL &&
              reset_observer->port_started == (uint8_t)(cut >= 1) &&
              reset_observer->memory_started == (uint8_t)(cut >= 2),
              "takeover reset provider phase is exact");
        observe_masks(
            &fixture, &unused, &unused, &unused, &unused, control_mask
        );
        (void)register_semantic_cut(
            &fixture, 1, "takeover semantic cut"
        );
        check(c42_teardown_start(
                  fixture.controller, &teardown) == C42_OK,
              "teardown takeover start");
        memset(&status, 0, sizeof(status));
        check(teardown.kind != reset.kind &&
              c42_control_query(
                  fixture.controller, &reset, &status) == C42_OK &&
              status.state == C42_CONTROL_SUPERSEDED &&
              c42_control_progress(
                  fixture.controller, &reset, 1) == C42_SUPERSEDED,
              "teardown immediately owns and supersedes reset token");
        for (step = 0; step < 512; ++step) {
            enum c42_result result = c42_control_progress(
                fixture.controller, &teardown, 1
            );

            if (result != C42_OK && result != C42_IN_PROGRESS) break;
            if (c42_control_query(
                    fixture.controller, &teardown, &status) == C42_OK &&
                status.state == C42_CONTROL_COMMITTED) break;
        }
        check(step < 512 &&
              c42_snapshot_read(fixture.controller, &snapshot) == C42_OK &&
              snapshot.phase == C42_CONTROLLER_DEAD,
              "teardown takeover terminal");
    }
}

static void test_notification_cuts(uint32_t *notification_mask)
{
    uint32_t phase;
    uint32_t mode;

    for (mode = 0; mode < 2; ++mode) {
        for (phase = 0; phase < 3; ++phase) {
            struct c42_test_fixture fixture;
            struct c42_notification notification = {0};
            uint32_t unused = 0;

            executions++;
            check(c42_test_fixture_init_with_nonce(
                      &fixture, 4, 0,
                      UINT64_C(0x4404000000000000) +
                      (uint64_t)mode * 0x100u + phase + 1u) &&
                  c42_test_submit(&fixture, 0, 0, 1, 403) &&
                  c42_test_run(&fixture, 128, 4),
                  "notification cut fixture");
            if (phase >= 1) {
                check(c42_notification_acquire(
                          fixture.controller, &notification) == C42_OK,
                      "notification acquire cut");
            }
            if (phase >= 2) {
                check(c42_notification_consume(
                          fixture.controller, &notification.token) == C42_OK,
                      "notification consume cut");
            }
            observe_masks(
                &fixture, &unused, &unused, notification_mask,
                &unused, &unused
            );
            (void)register_semantic_cut(
                &fixture, mode != 0, "notification semantic cut"
            );
            check(finish_cut(&fixture, mode != 0),
                  "notification reset/teardown cut");
        }
    }
}

static void test_notification_suppressed_cut(uint32_t *notification_mask)
{
    struct c42_test_fixture fixture;
    struct c42_notification notification = {0};
    struct c42_operation_token reset = {0};
    struct c42_operation_token teardown = {0};
    struct c42_control_status status = {0};
    uint32_t unused = 0;
    uint32_t step;

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4405000000000001)) &&
          c42_test_submit(&fixture, 0, 0, 1, 404) &&
          c42_test_run(&fixture, 128, 4) &&
          c42_notification_acquire(
              fixture.controller, &notification) == C42_OK &&
          c42_reset_start(fixture.controller, &reset) == C42_OK,
          "notification suppressed cut setup");
    observe_masks(
        &fixture, &unused, &unused, notification_mask, &unused, &unused
    );
    (void)register_semantic_cut(
        &fixture, 1, "suppressed-notification semantic cut"
    );
    check(c42_teardown_start(
              fixture.controller, &teardown) == C42_OK,
          "notification suppressed teardown takeover");
    for (step = 0; step < 512; ++step) {
        enum c42_result result = c42_control_progress(
            fixture.controller, &teardown, 1
        );

        if (result != C42_OK && result != C42_IN_PROGRESS) break;
        if (c42_control_query(
                fixture.controller, &teardown, &status) == C42_OK &&
            status.state == C42_CONTROL_COMMITTED) break;
    }
    check(step < 512, "notification suppressed cut terminal");
}

int main(void)
{
    uint32_t command_mask = 0;
    uint32_t reconcile_mask = 0;
    uint32_t notification_mask = 0;
    uint32_t candidate_mask = 0;
    uint32_t control_mask = 0;
    uint32_t required_command = 0;
    uint32_t state;

    test_command_publication_cuts(
        &command_mask, &reconcile_mask, &notification_mask
    );
    test_abnormal_command_cuts(&command_mask);
    test_candidate_cuts(&candidate_mask);
    test_abnormal_candidate_states(&candidate_mask);
    test_retire_unknown_post_lp();
    test_business_control_post_lp(&control_mask);
    test_poisoned_control(&control_mask);
    test_committed_control(&control_mask);
    test_notification_post_lp();
    test_target_post_lp();
    test_reset_teardown_takeover_cuts(&control_mask);
    test_notification_cuts(&notification_mask);
    for (state = C42_OBSERVER_COMMAND_CAPTURED;
         state <= C42_OBSERVER_COMMAND_LEASED;
         ++state) {
        required_command |= UINT32_C(1) << state;
    }
    required_command |=
        (UINT32_C(1) << C42_OBSERVER_COMMAND_CONSUME_PREPARE) |
        (UINT32_C(1) << C42_OBSERVER_COMMAND_PUB_RESERVED) |
        (UINT32_C(1) << C42_OBSERVER_COMMAND_MARKER_RECONCILE) |
        (UINT32_C(1) << C42_OBSERVER_COMMAND_ADMIT_POISON_HOLD) |
        (UINT32_C(1) << C42_OBSERVER_COMMAND_CONSUME_POISON_HOLD);
    test_notification_suppressed_cut(&notification_mask);
    check((command_mask & required_command) == required_command,
          "all normal command/publication phases observed before cuts");
    check((reconcile_mask & UINT32_C(0x1f)) == UINT32_C(0x1f),
          "all reconcile phases observed before cuts");
    check((notification_mask & UINT32_C(0x1f)) == UINT32_C(0x1f),
          "all notification phases observed before cuts");
    {
        uint32_t required_candidate =
            (UINT32_C(1) << C42_CANDIDATE_PREPARED) |
            (UINT32_C(1) << C42_CANDIDATE_SCRUB_UNKNOWN) |
            (UINT32_C(1) << C42_CANDIDATE_READY) |
            (UINT32_C(1) << C42_CANDIDATE_ABORTING) |
            (UINT32_C(1) << C42_CANDIDATE_ABORTED) |
            (UINT32_C(1) << C42_CANDIDATE_POISONED) |
            (UINT32_C(1) << C42_CANDIDATE_SUPERSEDED) |
            (UINT32_C(1) << C42_CANDIDATE_COMMITTED_AWAIT_RETIRE) |
            (UINT32_C(1) << C42_CANDIDATE_RETIRE_UNKNOWN) |
            (UINT32_C(1) << C42_CANDIDATE_RETIRE_READY) |
            (UINT32_C(1) << C42_CANDIDATE_RETIRED);
        uint32_t required_control =
            (UINT32_C(1) << C42_CONTROL_STARTED) |
            (UINT32_C(1) << C42_CONTROL_WAITING) |
            (UINT32_C(1) << C42_CONTROL_COMMITTED) |
            (UINT32_C(1) << C42_CONTROL_POISONED) |
            (UINT32_C(1) << C42_CONTROL_SUPERSEDED);

        check((candidate_mask & required_candidate) == required_candidate,
              "candidate lifecycle phases observed before cuts");
        check((control_mask & required_control) == required_control,
              "control/takeover phases observed before cuts");
    }
    if (failures != 0) {
        fprintf(stderr,
            "phase masks command=%08x reconcile=%08x notify=%08x "
            "candidate=%08x control=%08x\n",
            command_mask, reconcile_mask, notification_mask,
            candidate_mask, control_mask);
        return 1;
    }
    printf(
        "C4.2 phase cuts: PASS executions=%u requested=%u distinct=%u "
        "command=%08x "
        "reconcile=%08x notification=%08x candidate=%08x control=%08x\n",
        executions, requested_cut_count, distinct_cut_count,
        command_mask, reconcile_mask,
        notification_mask, candidate_mask, control_mask
    );
    return 0;
}
