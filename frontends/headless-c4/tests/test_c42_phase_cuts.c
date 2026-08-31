/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_support.h"

#include <stdio.h>
#include <string.h>

static int failures;
static uint32_t executions;
static uint32_t cut_count;

static void check(int condition, const char *label)
{
    if (!condition) {
        fprintf(stderr, "phase cuts FAIL: %s\n", label);
        failures++;
    }
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
            check(finish_cut(&fixture, mode != 0),
                  "command publication reset/teardown cut");
            cut_count++;
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
        direct.result = C42_MEMORY_IN_PROGRESS;
        direct.omit_status = 1;
        direct.apply_effect = 1;
        direct.logical_effect = C42_MEMORY_RETIRED;
        direct.applied_effect = C42_MEMORY_RETIRED;
        direct.committed = 1;
        direct.quiescent = 1;
        check(c42_fake_memory_direct_push(
                  &fixture->memory, &direct) == C42_OK &&
              c42_candidate_retire(
                  fixture->controller, candidate) == C42_IN_PROGRESS,
              "candidate retire response-loss cut");
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
            uint32_t unused = 0;

            executions++;
            check(c42_test_fixture_init_with_nonce(
                      &fixture, 4, 0,
                      UINT64_C(0x4402000000000000) +
                      (uint64_t)mode * 0x100u + phase + 1u),
                  "candidate cut fixture");
            prepare_candidate_phase(&fixture, phase, &candidate);
            observe_masks(
                &fixture, &unused, &unused, &unused,
                candidate_mask, &unused
            );
            check(finish_cut(&fixture, mode != 0),
                  "candidate reset/teardown cut");
            cut_count++;
        }
    }
}

static void test_reset_teardown_takeover_cuts(uint32_t *control_mask)
{
    uint32_t cut;

    for (cut = 0; cut < 12; ++cut) {
        struct c42_test_fixture fixture;
        struct c42_fake_command_script script = {0};
        struct c42_operation_token reset = {0};
        struct c42_operation_token teardown = {0};
        struct c42_control_status status = {0};
        struct c42_snapshot snapshot = {0};
        uint32_t step;
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
        observe_masks(
            &fixture, &unused, &unused, &unused, &unused, control_mask
        );
        check(c42_teardown_start(
                  fixture.controller, &teardown) == C42_OK,
              "teardown takeover start");
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
        cut_count++;
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
            check(finish_cut(&fixture, mode != 0),
                  "notification reset/teardown cut");
            cut_count++;
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
    cut_count++;
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
    test_candidate_cuts(&candidate_mask);
    test_reset_teardown_takeover_cuts(&control_mask);
    test_notification_cuts(&notification_mask);
    for (state = C42_OBSERVER_COMMAND_CAPTURED;
         state <= C42_OBSERVER_COMMAND_LEASED;
         ++state) {
        required_command |= UINT32_C(1) << state;
    }
    required_command |=
        (UINT32_C(1) << C42_OBSERVER_COMMAND_PUB_RESERVED) |
        (UINT32_C(1) << C42_OBSERVER_COMMAND_MARKER_RECONCILE);
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
            (UINT32_C(1) << C42_CANDIDATE_COMMITTED_AWAIT_RETIRE) |
            (UINT32_C(1) << C42_CANDIDATE_RETIRE_UNKNOWN) |
            (UINT32_C(1) << C42_CANDIDATE_RETIRE_READY);
        uint32_t required_control =
            (UINT32_C(1) << C42_CONTROL_STARTED) |
            (UINT32_C(1) << C42_CONTROL_WAITING) |
            (UINT32_C(1) << C42_CONTROL_COMMITTED);

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
        "C4.2 phase cuts: PASS executions=%u cuts=%u command=%08x "
        "reconcile=%08x notification=%08x candidate=%08x control=%08x\n",
        executions, cut_count, command_mask, reconcile_mask,
        notification_mask, candidate_mask, control_mask
    );
    return 0;
}
