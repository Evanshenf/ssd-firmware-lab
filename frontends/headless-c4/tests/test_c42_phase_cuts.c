/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_support.h"

#include <stdio.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define C42_TEST_NOINLINE __attribute__((noinline))
#else
#define C42_TEST_NOINLINE
#endif

static int failures;
static uint32_t executions;
static uint32_t requested_cut_count;
static uint32_t distinct_cut_count;

#define SEMANTIC_BUSINESS_CONTROLS 2u

struct semantic_cut_key {
    struct c42_observer_v2 observer;
    uint8_t teardown;
    uint8_t reserved[7];
};

static struct semantic_cut_key distinct_cut_keys[256];

static void check(int condition, const char *label)
{
    if (!condition) {
        fprintf(stderr, "phase cuts FAIL: %s\n", label);
        failures++;
    }
}

struct semantic_id_map {
    uint64_t value[128];
    uint32_t count;
};

static uint64_t semantic_id(
    struct semantic_id_map *map,
    uint64_t value)
{
    uint32_t index;

    if (value == 0) {
        return 0;
    }
    for (index = 0; index < map->count; ++index) {
        if (map->value[index] == value) {
            return index + 1u;
        }
    }
    if (map->count >= sizeof(map->value) / sizeof(map->value[0])) {
        check(0, "semantic alpha-renaming capacity");
        return UINT64_MAX;
    }
    map->value[map->count++] = value;
    return map->count;
}

static void semantic_operation_token(
    struct c42_operation_token *token,
    struct semantic_id_map *instance_ids,
    struct semantic_id_map *uid_ids)
{
    token->instance_nonce = semantic_id(
        instance_ids, token->instance_nonce
    );
    token->uid = semantic_id(uid_ids, token->uid);
}

static void semantic_command_handle(
    struct fwlab_nvme_command_handle *handle,
    struct semantic_id_map *instance_ids,
    struct semantic_id_map *command_ids)
{
    handle->instance_nonce = semantic_id(
        instance_ids, handle->instance_nonce
    );
    handle->command_uid = semantic_id(command_ids, handle->command_uid);
}

static void semantic_cut_normalize(
    const struct c42_observer_v2 *observer,
    int teardown,
    struct semantic_cut_key *key)
{
    struct semantic_id_map instance_ids = {{0}, 0};
    struct semantic_id_map publication_ids = {{0}, 0};
    struct semantic_id_map notification_ids = {{0}, 0};
    struct semantic_id_map candidate_ids = {{0}, 0};
    struct semantic_id_map target_ids = {{0}, 0};
    struct semantic_id_map business_control_ids = {{0}, 0};
    struct semantic_id_map reset_ids = {{0}, 0};
    struct semantic_id_map teardown_ids = {{0}, 0};
    struct semantic_id_map command_ids = {{0}, 0};
    struct semantic_id_map client_ids = {{0}, 0};
    struct semantic_id_map consume_ids = {{0}, 0};
    uint32_t index;

    /*
     * Typed alpha-renaming quotient over the complete zero-filled public
     * observer.  Every state, cursor, queue, generation, slot, wire and
     * equality fact remains literal.  Only opaque 64-bit identity names are
     * canonicalized; equality within each identity domain is preserved.
     */
    memset(key, 0, sizeof(*key));
    key->observer = *observer;
    key->teardown = (uint8_t)(teardown != 0);
    key->observer.instance_nonce = semantic_id(
        &instance_ids, key->observer.instance_nonce
    );
    for (index = 0; index < C42_MAX_QUEUE_PAIRS; ++index) {
        uint32_t slot;

        for (slot = 0; slot < C42_MAX_QUEUE_DEPTH; ++slot) {
            key->observer.cq[index].slots[slot].publication_uid = semantic_id(
                &publication_ids,
                key->observer.cq[index].slots[slot].publication_uid
            );
            key->observer.cq[index].slots[slot].notification_uid = semantic_id(
                &notification_ids,
                key->observer.cq[index].slots[slot].notification_uid
            );
        }
    }
    for (index = 0; index < C42_MAX_QUEUE_PAIRS * 2u; ++index) {
        semantic_operation_token(
            &key->observer.candidates[index].token,
            &instance_ids, &candidate_ids
        );
    }
    for (index = 0; index < observer->command_capacity; ++index) {
        semantic_command_handle(
            &key->observer.commands[index].handle,
            &instance_ids, &command_ids
        );
        key->observer.commands[index].client_uid = semantic_id(
            &client_ids, key->observer.commands[index].client_uid
        );
        key->observer.commands[index].publication_uid = semantic_id(
            &publication_ids,
            key->observer.commands[index].publication_uid
        );
        key->observer.commands[index].notification_uid = semantic_id(
            &notification_ids,
            key->observer.commands[index].notification_uid
        );
        key->observer.publications[index].publication_uid = semantic_id(
            &publication_ids,
            key->observer.publications[index].publication_uid
        );
        key->observer.publications[index].body_token_uid = semantic_id(
            &publication_ids,
            key->observer.publications[index].body_token_uid
        );
        key->observer.publications[index].marker_token_uid = semantic_id(
            &publication_ids,
            key->observer.publications[index].marker_token_uid
        );
        key->observer.reconciles[index].publication_uid = semantic_id(
            &publication_ids,
            key->observer.reconciles[index].publication_uid
        );
        key->observer.reconciles[index].consume_uid = semantic_id(
            &consume_ids, key->observer.reconciles[index].consume_uid
        );
        semantic_operation_token(
            &key->observer.notifications[index].token,
            &instance_ids, &notification_ids
        );
        key->observer.notifications[index].publication_uid = semantic_id(
            &publication_ids,
            key->observer.notifications[index].publication_uid
        );
    }
    for (index = 0; index < SEMANTIC_BUSINESS_CONTROLS; ++index) {
        semantic_operation_token(
            &key->observer.controls[index].token,
            &instance_ids, &business_control_ids
        );
    }
    semantic_operation_token(
        &key->observer.controls[SEMANTIC_BUSINESS_CONTROLS].token,
        &instance_ids, &reset_ids
    );
    semantic_operation_token(
        &key->observer.controls[SEMANTIC_BUSINESS_CONTROLS + 1u].token,
        &instance_ids, &teardown_ids
    );
    for (index = 0; index < observer->target_capacity; ++index) {
        semantic_operation_token(
            &key->observer.targets[index].token,
            &instance_ids, &target_ids
        );
        semantic_command_handle(
            &key->observer.targets[index].handle,
            &instance_ids, &command_ids
        );
    }
}

static C42_TEST_NOINLINE void test_semantic_quotient_laws(void)
{
    struct c42_observer_v2 left = {0};
    struct c42_observer_v2 right;
    struct semantic_cut_key left_key;
    struct semantic_cut_key right_key;

    executions++;
    left.version = C42_OBSERVER_VERSION;
    left.size = sizeof(left);
    left.instance_nonce = 100;
    left.command_capacity = 1;
    left.target_capacity = 1;
    left.candidates[0].in_use = 1;
    left.candidates[0].token.instance_nonce = 100;
    left.candidates[0].token.uid = 10;
    left.candidates[0].token.generation = 1;
    left.candidates[0].token.kind = 1;
    left.commands[0].handle.instance_nonce = 100;
    left.commands[0].handle.command_uid = 20;
    left.commands[0].handle.controller_epoch = 1;
    left.commands[0].handle.generation = 1;
    left.commands[0].client_uid = 30;
    left.notifications[0].token.instance_nonce = 100;
    left.notifications[0].token.uid = 40;
    left.notifications[0].token.generation = 1;
    left.notifications[0].token.kind = 4;
    left.controls[0].token.instance_nonce = 100;
    left.controls[0].token.uid = 50;
    left.controls[0].token.generation = 1;
    left.controls[0].token.kind = 5;
    left.controls[SEMANTIC_BUSINESS_CONTROLS].token.instance_nonce = 100;
    left.controls[SEMANTIC_BUSINESS_CONTROLS].token.uid = 60;
    left.controls[SEMANTIC_BUSINESS_CONTROLS].token.generation = 1;
    left.controls[SEMANTIC_BUSINESS_CONTROLS].token.kind = 6;
    left.targets[0].token.instance_nonce = 100;
    left.targets[0].token.uid = 70;
    left.targets[0].token.generation = 1;
    left.targets[0].token.kind = 7;
    left.targets[0].handle = left.commands[0].handle;

    right = left;
    right.instance_nonce = 200;
    right.candidates[0].token.instance_nonce = 200;
    right.candidates[0].token.uid = 110;
    right.commands[0].handle.instance_nonce = 200;
    right.commands[0].handle.command_uid = 120;
    right.commands[0].client_uid = 130;
    right.notifications[0].token.instance_nonce = 200;
    right.notifications[0].token.uid = 140;
    right.controls[0].token.instance_nonce = 200;
    right.controls[0].token.uid = 150;
    right.controls[SEMANTIC_BUSINESS_CONTROLS].token.instance_nonce = 200;
    right.controls[SEMANTIC_BUSINESS_CONTROLS].token.uid = 160;
    right.targets[0].token.instance_nonce = 200;
    right.targets[0].token.uid = 170;
    right.targets[0].handle.instance_nonce = 200;
    right.targets[0].handle.command_uid = 120;
    semantic_cut_normalize(&left, 0, &left_key);
    semantic_cut_normalize(&right, 0, &right_key);
    check(memcmp(&left_key, &right_key, sizeof(left_key)) == 0,
          "typed identity bijection preserves quotient equality");

    right = left;
    right.candidates[0].token.instance_nonce++;
    semantic_cut_normalize(&right, 0, &right_key);
    check(memcmp(&left_key, &right_key, sizeof(left_key)) != 0,
          "instance equality mismatch changes quotient key");

    right = left;
    right.candidates[0].token.uid = 0;
    semantic_cut_normalize(&right, 0, &right_key);
    check(memcmp(&left_key, &right_key, sizeof(left_key)) != 0,
          "zero and nonzero identities remain distinct");

    left.targets[0].token.uid = left.candidates[0].token.uid;
    right = left;
    right.targets[0].token.uid++;
    semantic_cut_normalize(&left, 0, &left_key);
    semantic_cut_normalize(&right, 0, &right_key);
    check(memcmp(&left_key, &right_key, sizeof(left_key)) == 0,
          "cross-domain numeric collisions do not affect quotient");

    right = left;
    right.targets[0].handle.command_uid++;
    semantic_cut_normalize(&left, 0, &left_key);
    semantic_cut_normalize(&right, 0, &right_key);
    check(memcmp(&left_key, &right_key, sizeof(left_key)) != 0,
          "command and target handle equality mismatch changes quotient key");

    memset(&left, 0, sizeof(left));
    left.version = C42_OBSERVER_VERSION;
    left.size = sizeof(left);
    left.instance_nonce = 100;
    left.command_capacity = 1;
    left.commands[0].publication_uid = 80;
    left.publications[0].publication_uid = 80;
    left.publications[0].body_token_uid = 80;
    left.publications[0].marker_token_uid = 80;
    right = left;
    right.publications[0].body_token_uid = 81;
    right.publications[0].marker_token_uid = 81;
    semantic_cut_normalize(&left, 0, &left_key);
    semantic_cut_normalize(&right, 0, &right_key);
    check(memcmp(&left_key, &right_key, sizeof(left_key)) != 0,
          "publication and memory-token UID equality changes quotient key");
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

static struct c42_queue_descriptor sq_descriptor(
    const struct c42_test_fixture *fixture,
    const struct c42_queue_memory_cap *cap)
{
    struct c42_queue_descriptor descriptor = cq_descriptor(fixture, cap);

    descriptor.kind = C42_QUEUE_SQ;
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
        if (observer.commands[index].handle.command_uid != 0) {
            check(observer.commands[index].handle.instance_nonce ==
                      observer.instance_nonce,
                  "observer command shares controller instance identity");
        }
        if (observer.commands[index].state < 32) {
            *command_mask |= UINT32_C(1) << observer.commands[index].state;
        }
        if (observer.reconciles[index].in_use != 0 &&
            observer.reconciles[index].state < 32) {
            *reconcile_mask |= UINT32_C(1) <<
                               observer.reconciles[index].state;
        }
        if (observer.publications[index].in_use != 0) {
            check(observer.publications[index].publication_uid != 0 &&
                      ((observer.publications[index].body_token_uid == 0 &&
                        observer.publications[index].marker_token_uid == 0) ||
                       (observer.publications[index].body_token_uid ==
                            observer.publications[index].publication_uid &&
                        observer.publications[index].marker_token_uid ==
                            observer.publications[index].publication_uid)),
                  "observer publication and memory tokens share UID identity");
        }
        if (observer.notifications[index].in_use != 0 &&
            observer.notifications[index].state < 32) {
            check(observer.notifications[index].token.instance_nonce ==
                      observer.instance_nonce,
                  "observer notification shares controller instance identity");
            *notification_mask |= UINT32_C(1) <<
                                  observer.notifications[index].state;
        }
    }
    for (index = 0; index < C42_MAX_QUEUE_PAIRS * 2u; ++index) {
        if (observer.candidates[index].in_use != 0 &&
            observer.candidates[index].state < 32) {
            check(observer.candidates[index].token.instance_nonce ==
                      observer.instance_nonce,
                  "observer candidate shares controller instance identity");
            *candidate_mask |= UINT32_C(1) << observer.candidates[index].state;
        }
    }
    for (index = 0; index < 4; ++index) {
        if (observer.controls[index].in_use != 0 &&
            observer.controls[index].state < 32) {
            check(observer.controls[index].token.instance_nonce ==
                      observer.instance_nonce,
                  "observer control shares controller instance identity");
            *control_mask |= UINT32_C(1) << observer.controls[index].state;
        }
    }
    for (index = 0; index < observer.target_capacity; ++index) {
        if (observer.targets[index].in_use != 0) {
            uint32_t command_index;
            int command_match = 0;

            for (command_index = 0;
                 command_index < observer.command_capacity;
                 ++command_index) {
                const struct fwlab_nvme_command_handle *left =
                    &observer.targets[index].handle;
                const struct fwlab_nvme_command_handle *right =
                    &observer.commands[command_index].handle;

                if (left->instance_nonce == right->instance_nonce &&
                    left->command_uid == right->command_uid &&
                    left->controller_epoch == right->controller_epoch &&
                    left->generation == right->generation) {
                    command_match = 1;
                    break;
                }
            }
            check(observer.targets[index].token.instance_nonce ==
                      observer.instance_nonce &&
                  observer.targets[index].handle.instance_nonce ==
                      observer.instance_nonce,
                  "observer target shares controller instance identity");
            check(observer.targets[index].identity_matches_active == 0 ||
                      command_match,
                  "active observer target shares one complete command handle");
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

static C42_TEST_NOINLINE void test_abnormal_command_cuts(uint32_t *command_mask)
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

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4400800000000004)),
          "abort-reconcile cut fixture");
    memset(&injection, 0, sizeof(injection));
    injection.operation = C42_FAKE_COMMAND_ADMIT;
    injection.result = FWLAB_HIF_PORT_OK;
    injection.value = FWLAB_HIF_ADMISSION_ABORTED;
    injection.write_mask = C42_FAKE_WRITE_VALUE;
    check(c42_fake_command_injection_push(
              &fixture.command, &injection) == C42_OK &&
          c42_test_submit(&fixture, 0, 0, 1, 412) &&
          run_until_command_state(
              &fixture, C42_OBSERVER_COMMAND_ABORT_RECONCILE, 128),
          "command ABORT_RECONCILE reached");
    observe_masks(
        &fixture, command_mask, &unused, &unused, &unused, &unused
    );
    (void)register_semantic_cut(&fixture, 0, "abort-reconcile command cut");
}

static C42_TEST_NOINLINE void test_command_publication_cuts(
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

static void prepare_candidate_state(
    struct c42_test_fixture *fixture,
    uint32_t state,
    struct c42_operation_token *candidate)
{
    struct c42_fake_memory_direct_injection direct = {0};

    switch (state) {
    case C42_CANDIDATE_PREPARED:
        prepare_candidate_phase(fixture, 0, candidate);
        break;
    case C42_CANDIDATE_SCRUB_UNKNOWN:
        prepare_candidate_phase(fixture, 1, candidate);
        break;
    case C42_CANDIDATE_READY:
        prepare_candidate_phase(fixture, 2, candidate);
        break;
    case C42_CANDIDATE_COMMITTED_AWAIT_RETIRE:
        prepare_candidate_phase(fixture, 3, candidate);
        break;
    case C42_CANDIDATE_RETIRE_UNKNOWN:
        prepare_candidate_phase(fixture, 4, candidate);
        break;
    case C42_CANDIDATE_RETIRE_READY:
        prepare_candidate_phase(fixture, 5, candidate);
        break;
    case C42_CANDIDATE_ABORTED:
        prepare_candidate_phase(fixture, 0, candidate);
        check(c42_candidate_abort(
                  fixture->controller, candidate) == C42_OK,
              "candidate post-LP ABORTED setup");
        break;
    case C42_CANDIDATE_ABORTING:
        prepare_candidate_phase(fixture, 1, candidate);
        check(c42_candidate_abort(
                  fixture->controller, candidate) == C42_OK,
              "candidate post-LP ABORTING setup");
        break;
    case C42_CANDIDATE_POISONED:
        prepare_candidate_phase(fixture, 0, candidate);
        direct.operation = C42_FAKE_MEMORY_SCRUB;
        direct.result = C42_MEMORY_POISONED;
        direct.omit_status = 1;
        check(c42_fake_memory_direct_push(
                  &fixture->memory, &direct) == C42_OK &&
              c42_candidate_progress(
                  fixture->controller, candidate, 1) == C42_POISONED,
              "candidate post-LP POISONED setup");
        break;
    default:
        check(0, "unknown candidate post-LP setup state");
        break;
    }
}

static C42_TEST_NOINLINE void test_all_candidate_post_lp(void)
{
    static const uint32_t states[] = {
        C42_CANDIDATE_PREPARED,
        C42_CANDIDATE_SCRUB_UNKNOWN,
        C42_CANDIDATE_READY,
        C42_CANDIDATE_ABORTING,
        C42_CANDIDATE_ABORTED,
        C42_CANDIDATE_POISONED,
        C42_CANDIDATE_COMMITTED_AWAIT_RETIRE,
        C42_CANDIDATE_RETIRE_UNKNOWN,
        C42_CANDIDATE_RETIRE_READY,
    };
    uint32_t mode;
    uint32_t state_index;

    for (mode = 0; mode < 2; ++mode) {
        for (state_index = 0;
             state_index < sizeof(states) / sizeof(states[0]);
             ++state_index) {
            struct c42_test_fixture fixture;
            struct c42_operation_token candidate = {0};
            struct c42_operation_token control = {0};
            struct c42_candidate_status status = {0};
            uint32_t provider_events;

            executions++;
            check(c42_test_fixture_init_with_nonce(
                      &fixture, 4, 0,
                      UINT64_C(0x4402700000000000) +
                      (uint64_t)mode * 0x100u + state_index + 1u),
                  "all-state candidate post-LP fixture");
            prepare_candidate_state(
                &fixture, states[state_index], &candidate
            );
            check(c42_candidate_query(
                      fixture.controller, &candidate, &status) == C42_OK &&
                  status.state == states[state_index],
                  "candidate exact all-state pre-LP query");
            provider_events = fixture.event_log.count;
            check((mode == 0 ?
                   c42_reset_start(fixture.controller, &control) :
                   c42_teardown_start(fixture.controller, &control)) == C42_OK,
                  "candidate all-state LP succeeds");
            memset(&status, 0, sizeof(status));
            check(c42_candidate_query(
                      fixture.controller, &candidate, &status) == C42_OK &&
                  status.state == C42_CANDIDATE_SUPERSEDED,
                  "candidate all-state query immediately superseded");
            check(c42_candidate_progress(
                      fixture.controller, &candidate, 1) == C42_SUPERSEDED &&
                  c42_candidate_commit(
                      fixture.controller, &candidate) == C42_SUPERSEDED &&
                  c42_candidate_abort(
                      fixture.controller, &candidate) == C42_SUPERSEDED &&
                  c42_candidate_retire(
                      fixture.controller, &candidate) == C42_SUPERSEDED,
                  "candidate all-state mutators reject after LP");
            check(fixture.event_log.count == provider_events,
                  "candidate all-state post-LP provider silence");
        }
    }
}

static C42_TEST_NOINLINE void test_sq_ready_candidate_post_lp(void)
{
    uint32_t mode;

    for (mode = 0; mode < 2; ++mode) {
        struct c42_test_fixture fixture;
        struct c42_operation_token cq_candidate = {0};
        struct c42_operation_token sq_candidate = {0};
        struct c42_operation_token control = {0};
        struct c42_candidate_status status = {0};
        struct c42_queue_memory_cap sq_cap;
        struct c42_queue_descriptor descriptor;
        uint32_t provider_events;

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0x4402850000000000) + mode + 1u),
              "SQ READY candidate post-LP fixture");
        prepare_candidate_phase(&fixture, 3, &cq_candidate);
        sq_cap = fresh_cap(
            &fixture, 1, UINT64_C(0x4402851000000000) + mode
        );
        sq_cap.role = C42_MEMORY_SQ_READ;
        sq_cap.exact_bytes = (uint32_t)fixture.depth * C42_SQE_BYTES;
        descriptor = sq_descriptor(&fixture, &sq_cap);
        check(c42_fake_memory_map(
                  &fixture.memory, &sq_cap, fixture.depth) == C42_OK &&
              c42_candidate_prepare(
                  fixture.controller, &descriptor,
                  &sq_candidate) == C42_OK &&
              c42_candidate_query(
                  fixture.controller, &sq_candidate,
                  &status) == C42_OK &&
              status.state == C42_CANDIDATE_READY,
              "SQ READY candidate exact pre-LP state");
        provider_events = fixture.event_log.count;
        check((mode == 0 ?
               c42_reset_start(fixture.controller, &control) :
               c42_teardown_start(fixture.controller, &control)) == C42_OK,
              "SQ READY candidate LP succeeds");
        memset(&status, 0, sizeof(status));
        check(c42_candidate_query(
                  fixture.controller, &sq_candidate,
                  &status) == C42_OK &&
              status.state == C42_CANDIDATE_SUPERSEDED &&
              c42_candidate_progress(
                  fixture.controller, &sq_candidate,
                  1) == C42_SUPERSEDED &&
              fixture.event_log.count == provider_events,
              "SQ READY candidate superseded provider-free after LP");
    }
}

static C42_TEST_NOINLINE void test_candidate_cuts(uint32_t *candidate_mask)
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

static C42_TEST_NOINLINE void test_retire_unknown_post_lp(void)
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

static C42_TEST_NOINLINE void test_abnormal_candidate_states(uint32_t *candidate_mask)
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

static C42_TEST_NOINLINE void test_business_control_post_lp(uint32_t *control_mask)
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

static C42_TEST_NOINLINE void test_all_business_control_post_lp(void)
{
    static const uint32_t states[] = {
        C42_CONTROL_STARTED,
        C42_CONTROL_WAITING,
        C42_CONTROL_COMMITTED,
    };
    uint32_t mode;
    uint32_t state_index;

    for (mode = 0; mode < 2; ++mode) {
        for (state_index = 0;
             state_index < sizeof(states) / sizeof(states[0]);
             ++state_index) {
            struct c42_test_fixture fixture;
            struct c42_operation_token business = {0};
            struct c42_operation_token epoch = {0};
            struct c42_control_status status = {0};
            uint32_t progress;
            uint32_t provider_events;

            executions++;
            check(c42_test_fixture_init_with_nonce(
                      &fixture, 4, 0,
                      UINT64_C(0x4402910000000000) +
                      (uint64_t)mode * 0x100u + state_index + 1u) &&
                  c42_delete_start(
                      fixture.controller, C42_QUEUE_SQ, 0,
                      &business) == C42_OK,
                  "business all-state post-LP fixture");
            for (progress = C42_CONTROL_STARTED;
                 progress < states[state_index];
                 ++progress) {
                check(c42_control_progress(
                          fixture.controller, &business, 1) == C42_OK,
                      "business all-state setup progress");
            }
            check(c42_control_query(
                      fixture.controller, &business, &status) == C42_OK &&
                  status.state == states[state_index],
                  "business exact all-state pre-LP query");
            provider_events = fixture.event_log.count;
            check((mode == 0 ?
                   c42_reset_start(fixture.controller, &epoch) :
                   c42_teardown_start(fixture.controller, &epoch)) == C42_OK,
                  "business all-state LP succeeds");
            memset(&status, 0, sizeof(status));
            check(c42_control_query(
                      fixture.controller, &business, &status) == C42_OK &&
                  status.state ==
                      (states[state_index] == C42_CONTROL_COMMITTED ?
                       C42_CONTROL_COMMITTED : C42_CONTROL_SUPERSEDED) &&
                  c42_control_progress(
                      fixture.controller, &business, 1) == C42_SUPERSEDED,
                  "business all-state query/progress after LP");
            check(fixture.event_log.count == provider_events &&
                  c42_control_retire(
                      fixture.controller, &business) == C42_OK,
                  "business all-state post-LP provider-free retire");
        }
    }
}

static C42_TEST_NOINLINE void test_poisoned_reset_takeover(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_operation_token reset = {0};
    struct c42_operation_token teardown = {0};
    struct c42_control_status status = {0};
    uint32_t provider_events;

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4402951000000001)),
          "poisoned reset takeover fixture");
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
          "poisoned reset exact pre-takeover state");
    provider_events = fixture.event_log.count;
    check(c42_teardown_start(
              fixture.controller, &teardown) == C42_OK &&
          c42_control_query(
              fixture.controller, &reset, &status) == C42_OK &&
          status.state == C42_CONTROL_SUPERSEDED &&
          c42_control_progress(
              fixture.controller, &reset, 1) == C42_SUPERSEDED &&
          fixture.event_log.count == provider_events,
          "poisoned reset public token superseded provider-free");
}

static C42_TEST_NOINLINE void test_poisoned_control(uint32_t *control_mask)
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

static C42_TEST_NOINLINE void test_committed_control(uint32_t *control_mask)
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

static C42_TEST_NOINLINE void test_notification_post_lp(void)
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

static int observer_notification_token(
    const struct c42_test_fixture *fixture,
    uint8_t expected_state,
    struct c42_operation_token *token)
{
    struct c42_observer_v2 observer;
    uint32_t index;

    if (c42_observer_read_v2(
            fixture->controller, &observer) != C42_OK) {
        return 0;
    }
    for (index = 0; index < observer.command_capacity; ++index) {
        if (observer.notifications[index].in_use != 0 &&
            observer.notifications[index].state == expected_state) {
            *token = observer.notifications[index].token;
            return 1;
        }
    }
    return 0;
}

static void prepare_notification_state(
    struct c42_test_fixture *fixture,
    uint8_t observer_state,
    struct c42_operation_token *token)
{
    struct c42_fake_command_script script = {0};
    struct c42_notification notification = {0};

    if (observer_state == C42_OBSERVER_NOTIFY_RESERVED) {
        script.poll_delay = 100;
        c42_fake_command_set_script(&fixture->command, &script);
        check(c42_test_submit(fixture, 0, 0, 1, 410) &&
              run_until_command_state(
                  fixture, C42_OBSERVER_COMMAND_HIF_COMMITTED, 128) &&
              observer_notification_token(
                  fixture, observer_state, token),
              "notification RESERVED setup");
        return;
    }
    check(c42_test_submit(fixture, 0, 0, 1, 411) &&
          c42_test_run(fixture, 128, 4),
          "notification READY/ACQUIRED setup");
    if (observer_state == C42_OBSERVER_NOTIFY_ACQUIRED) {
        check(c42_notification_acquire(
                  fixture->controller, &notification) == C42_OK,
              "notification ACQUIRED setup");
        *token = notification.token;
    } else {
        check(observer_state == C42_OBSERVER_NOTIFY_READY &&
              observer_notification_token(
                  fixture, observer_state, token),
              "notification READY setup");
    }
}

static C42_TEST_NOINLINE void test_all_notification_post_lp(void)
{
    static const uint8_t states[] = {
        C42_OBSERVER_NOTIFY_RESERVED,
        C42_OBSERVER_NOTIFY_READY,
        C42_OBSERVER_NOTIFY_ACQUIRED,
    };
    uint32_t mode;
    uint32_t state_index;

    for (mode = 0; mode < 2; ++mode) {
        for (state_index = 0;
             state_index < sizeof(states) / sizeof(states[0]);
             ++state_index) {
            struct c42_test_fixture fixture;
            struct c42_operation_token notification_token = {0};
            struct c42_operation_token control = {0};
            struct c42_notification queried = {0};
            struct c42_notification unavailable;
            struct c42_notification unavailable_sentinel;
            uint32_t provider_events;

            executions++;
            check(c42_test_fixture_init_with_nonce(
                      &fixture, 4, 0,
                      UINT64_C(0x4402a10000000000) +
                      (uint64_t)mode * 0x100u + state_index + 1u),
                  "all-state notification post-LP fixture");
            prepare_notification_state(
                &fixture, states[state_index], &notification_token
            );
            provider_events = fixture.event_log.count;
            check((mode == 0 ?
                   c42_reset_start(fixture.controller, &control) :
                   c42_teardown_start(fixture.controller, &control)) == C42_OK,
                  "notification all-state LP succeeds");
            check(c42_notification_query(
                      fixture.controller, &notification_token,
                      &queried) == C42_OK &&
                  queried.state == C42_NOTIFICATION_SUPPRESSED &&
                  c42_notification_consume(
                      fixture.controller, &notification_token) ==
                      C42_SUPERSEDED,
                  "notification all-state immediately suppressed");
            memset(&unavailable, 0xa5, sizeof(unavailable));
            unavailable_sentinel = unavailable;
            check(c42_notification_acquire(
                      fixture.controller, &unavailable) == C42_SUPERSEDED &&
                  memcmp(&unavailable, &unavailable_sentinel,
                         sizeof(unavailable)) == 0,
                  "notification acquire output unchanged after LP");
            check(fixture.event_log.count == provider_events &&
                  c42_notification_retire(
                      fixture.controller, &notification_token) == C42_OK,
                  "notification all-state post-LP provider-free retire");
        }
    }
}

static C42_TEST_NOINLINE void test_target_post_lp(void)
{
    uint32_t mode;

    for (mode = 0; mode < 2; ++mode) {
        struct c42_test_fixture fixture;
        struct c42_fake_command_script script = {0};
        struct c42_target_ref target = {0};
        struct c42_target_ref new_target;
        struct c42_target_ref target_sentinel;
        struct c42_operation_token control = {0};
        uint8_t raw[C42_SQE_BYTES];
        uint8_t sentinel[C42_SQE_BYTES];
        uint32_t provider_events;
        uint32_t unused = 0;

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
        memset(raw, 0, sizeof(raw));
        check(c42_raw_snapshot_copy(
                  fixture.controller, &target.handle,
                  &target.origin, raw) == C42_OK &&
              raw[2] == (uint8_t)406,
              "target raw snapshot exact before LP");
        observe_masks(
            &fixture, &unused, &unused, &unused, &unused, &unused
        );
        provider_events = fixture.event_log.count;
        check((mode == 0 ?
               c42_reset_start(fixture.controller, &control) :
               c42_teardown_start(fixture.controller, &control)) == C42_OK,
              "target LP succeeds");
        memset(raw, 0xa5, sizeof(raw));
        memset(sentinel, 0xa5, sizeof(sentinel));
        memset(&new_target, 0xa5, sizeof(new_target));
        target_sentinel = new_target;
        check(c42_raw_snapshot_copy(
                  fixture.controller, &target.handle,
                  &target.origin, raw) == C42_SUPERSEDED &&
              c42_target_prepare(
                  fixture.controller, 0,
                  fixture.sq_cap[0].ring_generation, 406,
                  &new_target) == C42_INVALID &&
              memcmp(&new_target, &target_sentinel,
                     sizeof(new_target)) == 0 &&
              memcmp(raw, sentinel, sizeof(raw)) == 0 &&
              c42_target_release(
                  fixture.controller, &target.token) == C42_STALE &&
              fixture.event_log.count == provider_events,
              "old target/raw-copy revoked without provider call after LP");
    }
}

static C42_TEST_NOINLINE void test_new_entrypoints_post_lp(void)
{
    uint32_t mode;

    for (mode = 0; mode < 2; ++mode) {
        struct c42_test_fixture fixture;
        struct c42_queue_memory_cap cap;
        struct c42_queue_descriptor descriptor;
        struct c42_fake_memory_direct_injection validate = {0};
        struct c42_operation_token control = {0};
        struct c42_operation_token candidate;
        struct c42_operation_token delete_token;
        struct c42_operation_token candidate_sentinel;
        struct c42_operation_token delete_sentinel;
        struct c42_sq_tail_event tail = {0};
        struct c42_snapshot before = {0};
        struct c42_snapshot after = {0};
        uint32_t provider_events;

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0x4402c00000000000) + mode + 1u),
              "new-entrypoint post-LP fixture");
        cap = fresh_cap(
            &fixture, 1, UINT64_C(0x4402c10000000000) + mode
        );
        descriptor = cq_descriptor(&fixture, &cap);
        check(c42_fake_memory_map(
                  &fixture.memory, &cap, fixture.depth) == C42_OK &&
              c42_snapshot_read(fixture.controller, &before) == C42_OK,
              "new-entrypoint pre-LP setup");
        provider_events = fixture.event_log.count;
        check((mode == 0 ?
               c42_reset_start(fixture.controller, &control) :
               c42_teardown_start(fixture.controller, &control)) == C42_OK &&
              c42_snapshot_read(fixture.controller, &after) == C42_OK,
              "new-entrypoint LP succeeds");
        descriptor.memory.controller_epoch = after.controller_epoch;
        validate.operation = C42_FAKE_MEMORY_VALIDATE;
        validate.result = C42_MEMORY_OK;
        check(c42_fake_memory_direct_push(
                  &fixture.memory, &validate) == C42_OK,
              "new candidate post-LP provider probe");
        memset(&candidate, 0xa5, sizeof(candidate));
        candidate_sentinel = candidate;
        memset(&delete_token, 0xa5, sizeof(delete_token));
        delete_sentinel = delete_token;
        tail.instance_nonce = fixture.config.instance_nonce;
        tail.controller_epoch = after.controller_epoch;
        tail.ring_generation = fixture.sq_cap[0].ring_generation;
        tail.queue_id = 0;
        tail.new_tail = 1;
        check(c42_candidate_prepare(
                  fixture.controller, &descriptor,
                  &candidate) == C42_INVALID &&
              memcmp(&candidate, &candidate_sentinel,
                     sizeof(candidate)) == 0 &&
              c42_delete_start(
                  fixture.controller, C42_QUEUE_SQ, 0,
                  &delete_token) == C42_INVALID &&
              memcmp(&delete_token, &delete_sentinel,
                     sizeof(delete_token)) == 0 &&
              c42_sq_tail_event_apply(
                  fixture.controller, &tail) == C42_WRONG_STATE &&
              c42_enable(fixture.controller) == C42_WRONG_STATE &&
              c42_snapshot_read(fixture.controller, &after) == C42_OK &&
              after.sq[0].host_index == before.sq[0].host_index &&
              after.sq[0].pending_or_unacked ==
                  before.sq[0].pending_or_unacked &&
              fixture.event_log.count == provider_events,
              "new candidate/delete/tail/enable closed provider-free after LP");

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0x4402c20000000000) + mode + 1u) &&
              c42_test_submit(&fixture, 0, 0, 1, 420) &&
              c42_test_run(&fixture, 128, 4) &&
              c42_snapshot_read(fixture.controller, &before) == C42_OK &&
              before.cq[0].pending_or_unacked != 0,
              "new CQ-head post-LP fixture");
        provider_events = fixture.event_log.count;
        memset(&control, 0, sizeof(control));
        check((mode == 0 ?
               c42_reset_start(fixture.controller, &control) :
               c42_teardown_start(fixture.controller, &control)) == C42_OK &&
              c42_snapshot_read(fixture.controller, &after) == C42_OK,
              "new CQ-head LP succeeds");
        {
            struct c42_cq_head_event head = {0};

            head.instance_nonce = fixture.config.instance_nonce;
            head.controller_epoch = after.controller_epoch;
            head.ring_generation = fixture.cq_cap[0].ring_generation;
            head.queue_id = 0;
            head.new_head = 1;
            check(c42_cq_head_event_apply(
                      fixture.controller, &head) == C42_WRONG_STATE &&
                  c42_snapshot_read(fixture.controller, &after) == C42_OK &&
                  after.cq[0].host_index == before.cq[0].host_index &&
                  after.cq[0].pending_or_unacked ==
                      before.cq[0].pending_or_unacked &&
                  fixture.event_log.count == provider_events,
                  "new CQ-head ACK closed provider-free after LP");
        }
    }
}

static C42_TEST_NOINLINE void test_step_drives_epoch_controls(void)
{
    uint32_t mode;

    for (mode = 0; mode < 2; ++mode) {
        static struct c42_test_fixture fixture;
        struct c42_operation_token control = {0};
        struct c42_control_status status = {0};
        struct c42_step_result step = {0};
        const struct c42_fake_event *event;
        uint32_t provider_events;
        uint32_t operation = mode == 0 ?
            C42_FAKE_COMMAND_RESET_BEGIN :
            C42_FAKE_COMMAND_TEARDOWN_BEGIN;

        executions++;
        check(c42_test_fixture_init_with_nonce(
                  &fixture, 4, 0,
                  UINT64_C(0x4402d00000000000) + mode + 1u),
              "epoch-control step fixture");
        provider_events = fixture.event_log.count;
        check((mode == 0 ?
               c42_reset_start(fixture.controller, &control) :
               c42_teardown_start(fixture.controller, &control)) == C42_OK,
              "epoch-control step LP succeeds");
        check(c42_step(fixture.controller, 1, &step) == C42_OK &&
              step.requested_budget == 1 && step.units_executed == 1 &&
              step.transitions == 1 &&
              fixture.event_log.count == provider_events + 1u,
              "c42_step drives one epoch-control provider unit");
        event = fixture.event_log.count == 0 ? NULL :
            &fixture.event_log.events[fixture.event_log.count - 1u];
        check(event != NULL &&
              event->provider == C42_FAKE_EVENT_COMMAND &&
              event->operation == operation &&
              event->call_kind == C42_FAKE_CALL_START &&
              event->direct_result == FWLAB_HIF_PORT_OK &&
              c42_control_query(
                  fixture.controller, &control, &status) == C42_OK &&
              status.state == C42_CONTROL_STARTED,
              "c42_step epoch-control event and state exact");
    }
}

static C42_TEST_NOINLINE void test_reset_teardown_takeover_cuts(uint32_t *control_mask)
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

static uint32_t provider_operation_count(
    const struct c42_fake_event_log *log,
    uint8_t provider,
    uint32_t operation)
{
    uint32_t count = 0;
    uint32_t index;

    for (index = 0; index < log->count; ++index) {
        if (log->events[index].provider == provider &&
            log->events[index].operation == operation) {
            count++;
        }
    }
    return count;
}

static void finish_takeover(
    struct c42_test_fixture *fixture,
    const struct c42_operation_token *teardown)
{
    struct c42_control_status status = {0};
    uint32_t step;

    for (step = 0; step < 512; ++step) {
        enum c42_result result = c42_control_progress(
            fixture->controller, teardown, 1
        );

        if (result != C42_OK && result != C42_IN_PROGRESS) {
            check(0, "response-unknown takeover progress");
            return;
        }
        if (c42_control_query(
                fixture->controller, teardown, &status) == C42_OK &&
            status.state == C42_CONTROL_COMMITTED) {
            return;
        }
    }
    check(0, "response-unknown takeover terminal");
}

static C42_TEST_NOINLINE void test_reset_begin_response_unknown_takeover(void)
{
    struct c42_test_fixture fixture;
    struct c42_fake_command_script script = {0};
    struct c42_fake_memory_direct_injection direct = {0};
    struct c42_operation_token reset = {0};
    struct c42_operation_token teardown = {0};

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4403100000000001)),
          "command reset-begin unknown takeover fixture");
    script.inject_operation = C42_FAKE_COMMAND_RESET_BEGIN;
    script.inject_result = FWLAB_HIF_PORT_IN_PROGRESS;
    script.inject_count = 1;
    script.inject_omit_outputs = 1;
    c42_fake_command_set_script(&fixture.command, &script);
    check(c42_reset_start(fixture.controller, &reset) == C42_OK &&
          c42_teardown_start(
              fixture.controller, &teardown) == C42_OK,
          "command reset-begin unknown takeover start");
    check(c42_control_progress(
              fixture.controller, &teardown, 1) == C42_OK &&
          provider_operation_count(
              &fixture.event_log, C42_FAKE_EVENT_COMMAND,
              C42_FAKE_COMMAND_RESET_BEGIN) == 1 &&
          fixture.command.reset_active == 0,
          "command reset-begin unknown remains unacknowledged");
    check(c42_control_progress(
              fixture.controller, &teardown, 1) == C42_OK &&
          provider_operation_count(
              &fixture.event_log, C42_FAKE_EVENT_COMMAND,
              C42_FAKE_COMMAND_RESET_BEGIN) == 2 &&
          fixture.command.reset_active == 1,
          "command reset-begin retries same key before takeover");
    finish_takeover(&fixture, &teardown);

    executions++;
    check(c42_test_fixture_init_with_nonce(
              &fixture, 4, 0, UINT64_C(0x4403100000000002)),
          "memory reset-begin unknown takeover fixture");
    direct.operation = C42_FAKE_MEMORY_RESET_BEGIN;
    direct.result = C42_MEMORY_IN_PROGRESS;
    direct.omit_status = 1;
    check(c42_fake_memory_direct_push(
              &fixture.memory, &direct) == C42_OK &&
          c42_reset_start(fixture.controller, &reset) == C42_OK &&
          c42_teardown_start(
              fixture.controller, &teardown) == C42_OK,
          "memory reset-begin unknown takeover start");
    check(c42_control_progress(
              fixture.controller, &teardown, 1) == C42_OK,
          "memory takeover bridges command begin first");
    check(c42_control_progress(
              fixture.controller, &teardown, 1) == C42_OK &&
          provider_operation_count(
              &fixture.event_log, C42_FAKE_EVENT_MEMORY,
              C42_FAKE_MEMORY_RESET_BEGIN) == 1 &&
          fixture.memory.reset_active == 0,
          "memory reset-begin unknown remains unacknowledged");
    check(c42_control_progress(
              fixture.controller, &teardown, 1) == C42_OK &&
          provider_operation_count(
              &fixture.event_log, C42_FAKE_EVENT_MEMORY,
              C42_FAKE_MEMORY_RESET_BEGIN) == 2 &&
          fixture.memory.reset_active == 1,
          "memory reset-begin retries same key before takeover");
    finish_takeover(&fixture, &teardown);
}

static C42_TEST_NOINLINE void test_notification_cuts(uint32_t *notification_mask)
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

static C42_TEST_NOINLINE void test_notification_suppressed_cut(uint32_t *notification_mask)
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

    test_semantic_quotient_laws();
    test_command_publication_cuts(
        &command_mask, &reconcile_mask, &notification_mask
    );
    test_abnormal_command_cuts(&command_mask);
    test_candidate_cuts(&candidate_mask);
    test_abnormal_candidate_states(&candidate_mask);
    test_all_candidate_post_lp();
    test_sq_ready_candidate_post_lp();
    test_retire_unknown_post_lp();
    test_business_control_post_lp(&control_mask);
    test_all_business_control_post_lp();
    test_poisoned_control(&control_mask);
    test_poisoned_reset_takeover();
    test_committed_control(&control_mask);
    test_notification_post_lp();
    test_all_notification_post_lp();
    test_target_post_lp();
    test_new_entrypoints_post_lp();
    test_step_drives_epoch_controls();
    test_reset_teardown_takeover_cuts(&control_mask);
    test_reset_begin_response_unknown_takeover();
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
        (UINT32_C(1) << C42_OBSERVER_COMMAND_ABORT_RECONCILE) |
        (UINT32_C(1) << C42_OBSERVER_COMMAND_ADMIT_POISON_HOLD) |
        (UINT32_C(1) << C42_OBSERVER_COMMAND_CONSUME_POISON_HOLD);
    test_notification_suppressed_cut(&notification_mask);
    check(requested_cut_count == 130,
          "phase requested cut count is exact");
    check(distinct_cut_count == 74,
          "phase full-observer quotient count is exact");
    check((command_mask & required_command) == required_command,
          "all normal command/publication phases observed before cuts");
    check((command_mask &
           (UINT32_C(1) << C42_OBSERVER_COMMAND_RELEASE_RECONCILE)) == 0,
          "compat-only release-reconcile state remains unreachable");
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
        check((candidate_mask &
               (UINT32_C(1) << C42_CANDIDATE_COMMITTED)) == 0,
              "compat-only candidate COMMITTED remains unreachable");
        check((control_mask & required_control) == required_control,
              "control/takeover phases observed before cuts");
        check((control_mask &
               ((UINT32_C(1) << C42_CONTROL_CLEANUP_PENDING) |
                (UINT32_C(1) << C42_CONTROL_RETIRED))) == 0,
              "compat-only control cleanup states remain unreachable");
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
