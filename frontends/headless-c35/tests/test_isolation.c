/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_test_support.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define C35_MACRO_ACTIONS 6u
#define C35_SCHEDULE_BITS 12u
#define C35_SCHEDULES 924u
#define C35_PREFIXES 3431u

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

struct c35_prefix {
    struct c35_trace trace;
    uint8_t raw[C35_RAW_PROJECTION_BYTES];
};

struct c35_instance {
    struct c35_storage *storage;
    struct c35_runtime *runtime;
};

static unsigned int popcount12(uint16_t value)
{
    unsigned int count = 0;

    while (value != 0) {
        count += value & 1u;
        value >>= 1;
    }
    return count;
}

static void make_uuid(
    uint8_t uuid[16],
    uint8_t family,
    uint8_t actor,
    enum c35_lane lane
)
{
    unsigned int index;

    for (index = 0; index < 16; ++index) {
        uuid[index] = (uint8_t)(0x35u + index * 11u);
    }
    uuid[0] ^= family;
    uuid[1] ^= actor;
    uuid[2] ^= (uint8_t)lane;
}

static int instance_start(
    struct c35_instance *instance,
    enum c35_lane lane,
    uint8_t family,
    uint8_t actor
)
{
    uint8_t uuid[16];
    uint64_t nonce = UINT64_C(0x3506000000000000) |
                     ((uint64_t)family << 16) |
                     ((uint64_t)actor << 8) | (uint64_t)lane | 1u;

    memset(instance, 0, sizeof(*instance));
    instance->storage = calloc(1, sizeof(*instance->storage));
    instance->runtime = calloc(1, sizeof(*instance->runtime));
    if (instance->storage == NULL || instance->runtime == NULL) {
        return 0;
    }
    make_uuid(uuid, family, actor, lane);
    return c35_storage_init(instance->storage, lane, uuid) &&
           c35_runtime_init(
               instance->runtime, instance->storage, lane, nonce,
               UINT64_C(0x9b6d3e7a4c2158f1) ^
                   ((uint64_t)family << 20) ^ ((uint64_t)actor << 8),
               1, actor, 0x3500u + family);
}

static int instance_finish(struct c35_instance *instance)
{
    int ok = 1;

    if (instance->runtime != NULL && instance->runtime->lifecycle != NULL) {
        ok = c35_runtime_teardown(instance->runtime);
    }
    if (instance->storage != NULL && instance->storage->initialized) {
        ok = c35_storage_close(instance->storage) && ok;
    }
    free(instance->runtime);
    free(instance->storage);
    memset(instance, 0, sizeof(*instance));
    return ok;
}

static void payload_make(
    uint8_t payload[C35_ATOM_BYTES],
    uint8_t family,
    uint8_t actor,
    uint8_t atom
)
{
    unsigned int index;

    for (index = 0; index < C35_ATOM_BYTES; ++index) {
        payload[index] = (uint8_t)(0x20u * family + 0x30u * actor +
                                   0x10u * atom + index);
    }
}

static int expect_payload(
    const struct c35_semantic_result *result,
    uint8_t atom,
    const uint8_t expected[C35_ATOM_BYTES]
)
{
    return (result->present_mask & (uint8_t)(1u << atom)) != 0 &&
           memcmp(result->payload[atom], expected, C35_ATOM_BYTES) == 0;
}

static int perform_action(
    struct c35_runtime *runtime,
    uint8_t family,
    uint8_t actor,
    uint8_t action
)
{
    struct c35_request request;
    struct c35_semantic_result result;
    uint8_t value0[C35_ATOM_BYTES];
    uint8_t value1[C35_ATOM_BYTES];

    payload_make(value0, family, actor, 0);
    payload_make(value1, family, actor, 1);
    if (family == 6) {
        switch (action) {
        case 0:
            request = c35_request_write(
                0, FWLAB_PERSIST_SELF_DURABLE, 1, value0);
            return c35_run_command(runtime, &request, &result);
        case 1:
            request = c35_request_read(0);
            return c35_run_command(runtime, &request, &result) &&
                   expect_payload(&result, 0, value0);
        case 2:
            request = c35_request_write(
                1, FWLAB_PERSIST_DEFAULT, 2, value1);
            return c35_run_command(runtime, &request, &result);
        case 3:
            return c35_headless_pump_quiescent(
                       &runtime->headless, 8192) == C35_OK;
        case 4:
            request = c35_request_fence(2);
            return c35_run_command(runtime, &request, &result);
        case 5:
            request = c35_request_read(1);
            return c35_run_command(runtime, &request, &result) &&
                   expect_payload(&result, 1, value1);
        default:
            return 0;
        }
    }
    if (family != 7) {
        return 0;
    }
    if (actor == 0) {
        switch (action) {
        case 0:
            request = c35_request_write(
                0, FWLAB_PERSIST_SELF_DURABLE, 1, value0);
            return c35_run_command(runtime, &request, &result);
        case 1:
            request = c35_request_write(
                1, FWLAB_PERSIST_DEFAULT, 2, value1);
            return c35_run_command(runtime, &request, &result);
        case 2:
            request = c35_request_read(1);
            return c35_run_command(runtime, &request, &result) &&
                   expect_payload(&result, 1, value1);
        case 3:
            return c35_headless_pump_quiescent(
                       &runtime->headless, 8192) == C35_OK;
        case 4:
            request = c35_request_fence(2);
            return c35_run_command(runtime, &request, &result);
        case 5:
            request = c35_request_read(1);
            return c35_run_command(runtime, &request, &result) &&
                   expect_payload(&result, 1, value1);
        default:
            return 0;
        }
    }
    switch (action) {
    case 0:
        request = c35_request_write(
            0, FWLAB_PERSIST_SELF_DURABLE, 1, value0);
        return c35_run_command(runtime, &request, &result);
    case 1:
        request = c35_request_read(0);
        return c35_run_command(runtime, &request, &result) &&
               expect_payload(&result, 0, value0);
    case 2:
        request = c35_request_trim(
            0, FWLAB_PERSIST_SELF_DURABLE, 2);
        return c35_run_command(runtime, &request, &result);
    case 3:
        request = c35_request_read(0);
        return c35_run_command(runtime, &request, &result) &&
               result.present_mask == 0;
    case 4:
        request = c35_request_write(
            1, FWLAB_PERSIST_SELF_DURABLE, 3, value1);
        return c35_run_command(runtime, &request, &result);
    case 5:
        request = c35_request_read(1);
        return c35_run_command(runtime, &request, &result) &&
               expect_payload(&result, 1, value1);
    default:
        return 0;
    }
}

static int prefix_capture(
    struct c35_runtime *runtime,
    struct c35_prefix *prefix
)
{
    prefix->trace = runtime->trace;
    return c35_runtime_projection(runtime, prefix->raw);
}

static int prefix_equal_runtime(
    const struct c35_prefix *expected,
    struct c35_runtime *runtime
)
{
    uint8_t raw[C35_RAW_PROJECTION_BYTES];

    return c35_trace_equal(&expected->trace, &runtime->trace) &&
           c35_runtime_projection(runtime, raw) &&
           memcmp(expected->raw, raw, sizeof(raw)) == 0;
}

static int build_solo(
    enum c35_lane lane,
    uint8_t family,
    uint8_t actor,
    struct c35_prefix prefix[C35_MACRO_ACTIONS + 1u]
)
{
    struct c35_instance instance;
    unsigned int action;
    int ok;

    CHECK(instance_start(&instance, lane, family, actor));
    CHECK(prefix_capture(instance.runtime, &prefix[0]));
    for (action = 0; action < C35_MACRO_ACTIONS; ++action) {
        CHECK(perform_action(instance.runtime, family, actor,
                             (uint8_t)action));
        CHECK(prefix_capture(instance.runtime, &prefix[action + 1u]));
    }
    ok = instance_finish(&instance);
    CHECK(ok);
    return 1;
}

static int run_live_schedule(
    enum c35_lane lane_a,
    enum c35_lane lane_b,
    uint8_t family,
    uint16_t schedule,
    const struct c35_prefix reference_a[C35_MACRO_ACTIONS + 1u],
    const struct c35_prefix reference_b[C35_MACRO_ACTIONS + 1u]
)
{
    struct c35_instance instance[2];
    uint8_t progress[2] = {0, 0};
    unsigned int position;
    int ok;

    CHECK(instance_start(&instance[0], lane_a, family, 0));
    CHECK(instance_start(&instance[1], lane_b, family, 1));
    for (position = 0; position < C35_SCHEDULE_BITS; ++position) {
        unsigned int actor = (schedule >> position) & 1u;
        unsigned int peer = actor ^ 1u;

        CHECK(progress[actor] < C35_MACRO_ACTIONS);
        CHECK(perform_action(
            instance[actor].runtime, family, (uint8_t)actor,
            progress[actor]));
        ++progress[actor];
        CHECK(prefix_equal_runtime(
            actor == 0 ? &reference_a[progress[actor]] :
                         &reference_b[progress[actor]],
            instance[actor].runtime));
        CHECK(prefix_equal_runtime(
            peer == 0 ? &reference_a[progress[peer]] :
                        &reference_b[progress[peer]],
            instance[peer].runtime));
    }
    CHECK(progress[0] == C35_MACRO_ACTIONS &&
          progress[1] == C35_MACRO_ACTIONS);
    ok = instance_finish(&instance[0]);
    ok = instance_finish(&instance[1]) && ok;
    CHECK(ok);
    return 1;
}

static int replay_all_schedules(
    const uint16_t schedules[C35_SCHEDULES],
    const struct c35_prefix reference_a[C35_MACRO_ACTIONS + 1u],
    const struct c35_prefix reference_b[C35_MACRO_ACTIONS + 1u],
    uint32_t *prefixes
)
{
    uint8_t seen[8192];
    unsigned int schedule_index;
    uint32_t count = 0;

    memset(seen, 0, sizeof(seen));
    seen[1] = 1;
    count = 1;
    for (schedule_index = 0; schedule_index < C35_SCHEDULES;
         ++schedule_index) {
        uint16_t schedule = schedules[schedule_index];
        uint16_t bits = 0;
        uint8_t progress[2] = {0, 0};
        const struct c35_prefix *current[2] = {
            &reference_a[0], &reference_b[0]
        };
        unsigned int position;

        for (position = 0; position < C35_SCHEDULE_BITS; ++position) {
            unsigned int actor = (schedule >> position) & 1u;
            unsigned int peer = actor ^ 1u;
            const struct c35_prefix *peer_before = current[peer];
            uint16_t key;

            bits |= (uint16_t)(actor << position);
            ++progress[actor];
            current[actor] = actor == 0 ?
                &reference_a[progress[actor]] :
                &reference_b[progress[actor]];
            CHECK(current[peer] == peer_before);
            CHECK(c35_trace_equal(
                &current[actor]->trace,
                actor == 0 ? &reference_a[progress[actor]].trace :
                             &reference_b[progress[actor]].trace));
            CHECK(memcmp(
                current[actor]->raw,
                actor == 0 ? reference_a[progress[actor]].raw :
                             reference_b[progress[actor]].raw,
                C35_RAW_PROJECTION_BYTES) == 0);
            key = (uint16_t)((1u << (position + 1u)) | bits);
            if (!seen[key]) {
                seen[key] = 1;
                ++count;
            }
        }
        CHECK(progress[0] == C35_MACRO_ACTIONS &&
              progress[1] == C35_MACRO_ACTIONS);
    }
    CHECK(count == C35_PREFIXES);
    *prefixes += count;
    return 1;
}

static int test_schedule_matrix(
    uint32_t *complete_schedules,
    uint32_t *distinct_prefixes,
    uint32_t *live_schedules
)
{
    static const enum c35_lane pairs[3][2] = {
        {C35_LANE_MEMORY, C35_LANE_MEMORY},
        {C35_LANE_BYTE, C35_LANE_BYTE},
        {C35_LANE_MEMORY, C35_LANE_BYTE},
    };
    static const unsigned int live_index[6] = {0, 1, 231, 462, 692, 923};
    uint16_t schedules[C35_SCHEDULES];
    unsigned int mask;
    unsigned int count = 0;
    unsigned int family;
    unsigned int pair;

    for (mask = 0; mask < (1u << C35_SCHEDULE_BITS); ++mask) {
        if (popcount12((uint16_t)mask) == C35_MACRO_ACTIONS) {
            CHECK(count < C35_SCHEDULES);
            schedules[count++] = (uint16_t)mask;
        }
    }
    CHECK(count == C35_SCHEDULES);
    for (family = 6; family <= 7; ++family) {
        for (pair = 0; pair < 3; ++pair) {
            struct c35_prefix *reference_a =
                calloc(C35_MACRO_ACTIONS + 1u, sizeof(*reference_a));
            struct c35_prefix *reference_b =
                calloc(C35_MACRO_ACTIONS + 1u, sizeof(*reference_b));
            unsigned int live;

            CHECK(reference_a != NULL && reference_b != NULL);
            CHECK(build_solo(
                pairs[pair][0], (uint8_t)family, 0, reference_a));
            CHECK(build_solo(
                pairs[pair][1], (uint8_t)family, 1, reference_b));
            CHECK(replay_all_schedules(
                schedules, reference_a, reference_b, distinct_prefixes));
            *complete_schedules += C35_SCHEDULES;
            for (live = 0; live < 6; ++live) {
                CHECK(run_live_schedule(
                    pairs[pair][0], pairs[pair][1], (uint8_t)family,
                    schedules[live_index[live]], reference_a,
                    reference_b));
                ++*live_schedules;
            }
            free(reference_b);
            free(reference_a);
        }
    }
    return 1;
}

static int wait_ready(
    struct c35_runtime *runtime,
    const struct fwlab_c31_command_handle *command
)
{
    unsigned int index;

    for (index = 0; index < 8192; ++index) {
        enum fwlab_c31_lifecycle_state state;
        struct fwlab_c31_step_result step;

        if (fwlab_c31_command_state(
                runtime->lifecycle, command, &state) != FWLAB_C31_API_OK) {
            return 0;
        }
        if (state == FWLAB_C31_CMD_COMPLETION_READY) {
            return 1;
        }
        if (fwlab_c31_step(runtime->lifecycle, 1, &step) !=
            FWLAB_C31_API_OK) {
            return 0;
        }
    }
    return 0;
}

static int test_cross_identity_and_reset(
    enum c35_lane lane_a,
    enum c35_lane lane_b
)
{
    struct c35_instance instance[2];
    struct c35_request request;
    struct c35_submission submission;
    struct c35_semantic_result semantic;
    struct fwlab_c31_completion_intent intent;
    enum fwlab_c31_lifecycle_state state;
    struct c35_trace trace_b;
    uint8_t raw_b[C35_RAW_PROJECTION_BYTES];
    uint8_t raw_after[C35_RAW_PROJECTION_BYTES];
    uint8_t value_a[C35_ATOM_BYTES];
    uint8_t value_b[C35_ATOM_BYTES];
    int ok;

    CHECK(instance_start(&instance[0], lane_a, 9, 0));
    CHECK(instance_start(&instance[1], lane_b, 9, 1));
    payload_make(value_a, 9, 0, 0);
    payload_make(value_b, 9, 1, 1);
    request = c35_request_write(
        0, FWLAB_PERSIST_SELF_DURABLE, 1, value_a);
    CHECK(c35_run_command(instance[0].runtime, &request, &semantic));
    request = c35_request_write(
        1, FWLAB_PERSIST_SELF_DURABLE, 1, value_b);
    CHECK(c35_run_command(instance[1].runtime, &request, &semantic));
    trace_b = instance[1].runtime->trace;
    CHECK(c35_runtime_projection(instance[1].runtime, raw_b));

    request = c35_request_read(0);
    CHECK(c35_headless_submit_observed(
        &instance[0].runtime->headless, &request, &submission) == C35_OK);
    CHECK(instance[1].runtime->headless.binding.ops->register_after_submit(
        instance[1].runtime->headless.binding.context, &submission.request,
        &submission.command,
        instance[1].runtime->headless.owner_epoch, &request) == C35_INVALID);
    CHECK(fwlab_c31_command_state(
        instance[1].runtime->lifecycle, &submission.command, &state) ==
        FWLAB_C31_API_STALE_TOKEN);
    CHECK(wait_ready(instance[0].runtime, &submission.command));
    CHECK(instance[1].runtime->headless.binding.ops->result_copy_before_consume(
        instance[1].runtime->headless.binding.context, &submission.command,
        &intent, &semantic) == C35_STALE);
    CHECK(c35_headless_complete(
        &instance[0].runtime->headless, &submission.command,
        &semantic, &intent) == C35_OK);
    CHECK(c35_headless_reset(
        &instance[0].runtime->headless, 8192) == C35_OK);
    CHECK(c35_trace_equal(&trace_b, &instance[1].runtime->trace));
    CHECK(c35_runtime_projection(instance[1].runtime, raw_after));
    CHECK(memcmp(raw_b, raw_after, sizeof(raw_b)) == 0);
    request = c35_request_read(1);
    CHECK(c35_run_command(instance[1].runtime, &request, &semantic));
    CHECK(expect_payload(&semantic, 1, value_b));
    ok = instance_finish(&instance[0]);
    ok = instance_finish(&instance[1]) && ok;
    CHECK(ok);
    return 1;
}

static int test_bundle_exclusion(void)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *first = calloc(1, sizeof(*first));
    struct c35_runtime *second = calloc(1, sizeof(*second));
    uint8_t uuid[16];
    int ok;

    CHECK(storage != NULL && first != NULL && second != NULL);
    make_uuid(uuid, 11, 0, C35_LANE_MEMORY);
    CHECK(c35_storage_init(storage, C35_LANE_MEMORY, uuid));
    CHECK(c35_runtime_init(
        first, storage, C35_LANE_MEMORY, UINT64_C(0x35110001),
        UINT64_C(0x1111222233334444), 0, 0, 0x3511));
    CHECK(!c35_runtime_init(
        second, storage, C35_LANE_MEMORY, UINT64_C(0x35110002),
        UINT64_C(0x5555666677778888), 0, 1, 0x3511));
    CHECK(!c35_storage_close(storage));
    ok = c35_runtime_teardown(first) && c35_storage_close(storage);
    free(second);
    free(first);
    free(storage);
    CHECK(ok);
    return 1;
}

static int test_posix_pair(void)
{
    struct c35_instance instance[2];
    struct c35_request request;
    struct c35_semantic_result result;
    uint8_t value[2][C35_ATOM_BYTES];
    unsigned int actor;
    int ok;

    CHECK(instance_start(&instance[0], C35_LANE_POSIX, 10, 0));
    CHECK(instance_start(&instance[1], C35_LANE_POSIX, 10, 1));
    CHECK(instance[0].storage->fd >= 0 && instance[1].storage->fd >= 0 &&
          instance[0].storage->fd != instance[1].storage->fd);
    for (actor = 0; actor < 2; ++actor) {
        payload_make(value[actor], 10, (uint8_t)actor, (uint8_t)actor);
        request = c35_request_write(
            (uint8_t)actor, FWLAB_PERSIST_SELF_DURABLE, 1,
            value[actor]);
        CHECK(c35_run_command(instance[actor].runtime, &request, &result));
    }
    for (actor = 0; actor < 2; ++actor) {
        request = c35_request_read((uint8_t)actor);
        CHECK(c35_run_command(instance[actor].runtime, &request, &result));
        CHECK(expect_payload(&result, (uint8_t)actor, value[actor]));
    }
    ok = instance_finish(&instance[0]);
    ok = instance_finish(&instance[1]) && ok;
    CHECK(ok);
    return 1;
}

int main(void)
{
    uint32_t schedules = 0;
    uint32_t prefixes = 0;
    uint32_t live = 0;

    CHECK(test_schedule_matrix(&schedules, &prefixes, &live));
    CHECK(schedules == 5544 && prefixes == 20586 && live == 36);
    CHECK(test_cross_identity_and_reset(
        C35_LANE_MEMORY, C35_LANE_MEMORY));
    CHECK(test_cross_identity_and_reset(C35_LANE_BYTE, C35_LANE_BYTE));
    CHECK(test_cross_identity_and_reset(C35_LANE_MEMORY, C35_LANE_BYTE));
    CHECK(test_bundle_exclusion());
    CHECK(test_posix_pair());
    printf("C3.5 isolation schedules: PASS (%" PRIu32
           " complete / %" PRIu32 " distinct prefixes / %" PRIu32
           " live twin schedules)\n", schedules, prefixes, live);
    return 0;
}
