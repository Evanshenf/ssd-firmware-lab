/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static void make_uuid(uint8_t uuid[16], enum c35_lane lane)
{
    unsigned int index;

    for (index = 0; index < 16; ++index) {
        uuid[index] = (uint8_t)(0x35u + index * 17u + (unsigned int)lane);
    }
}

static int test_backpressure_cancel(enum c35_lane lane)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct c35_submission first;
    struct c35_submission second;
    struct c35_request request;
    struct c35_semantic_result semantic;
    struct fwlab_c31_completion_intent intent;
    struct fwlab_c31_abort_ticket ticket;
    enum fwlab_c31_abort_outcome outcome;
    enum fwlab_c31_lifecycle_state first_state;
    enum fwlab_c31_lifecycle_state second_state;
    uint8_t uuid[16];
    uint8_t value[C35_ATOM_BYTES];
    unsigned int iteration;
    int saw_backpressure = 0;
    int ok;

    CHECK(storage != NULL && runtime != NULL);
    make_uuid(uuid, lane);
    memset(value, 0x7c, sizeof(value));
    CHECK(c35_storage_init(storage, lane, uuid));
    CHECK(c35_runtime_init(
        runtime, storage, lane,
        UINT64_C(0x3520000000000000) | (uint64_t)lane,
        UINT64_C(0x1020304050607080), 0, 0, 0x3502));
    request = c35_request_write(
        0, FWLAB_PERSIST_SELF_DURABLE, 1, value);
    CHECK(c35_headless_submit_observed(
        &runtime->headless, &request, &first) == C35_OK);
    request = c35_request_read(1);
    CHECK(c35_headless_submit_observed(
        &runtime->headless, &request, &second) == C35_OK);
    for (iteration = 0; iteration < 32; ++iteration) {
        struct fwlab_c31_step_result step;

        CHECK(fwlab_c31_command_state(
            runtime->lifecycle, &first.command, &first_state) ==
            FWLAB_C31_API_OK);
        CHECK(fwlab_c31_command_state(
            runtime->lifecycle, &second.command, &second_state) ==
            FWLAB_C31_API_OK);
        if (first_state == FWLAB_C31_CMD_RUNNING &&
            second_state == FWLAB_C31_CMD_HELD) {
            saw_backpressure = 1;
            break;
        }
        CHECK(fwlab_c31_step(runtime->lifecycle, 1, &step) ==
              FWLAB_C31_API_OK);
    }
    CHECK(saw_backpressure);
    CHECK(fwlab_c31_abort_request(
        runtime->lifecycle, &first.command, &ticket, &outcome) ==
        FWLAB_C31_API_OK);
    CHECK(outcome == FWLAB_C31_ABORT_PENDING);
    CHECK(c35_headless_complete(
        &runtime->headless, &first.command, &semantic, &intent) == C35_OK);
    CHECK(intent.result == FWLAB_C31_COMPLETION_ABORTED);
    CHECK(fwlab_c31_abort_query(
        runtime->lifecycle, &ticket, &outcome) == FWLAB_C31_API_OK);
    CHECK(outcome == FWLAB_C31_ABORT_TERMINAL ||
          outcome == FWLAB_C31_ABORT_TOO_LATE);
    CHECK(fwlab_c31_abort_ack(runtime->lifecycle, &ticket) ==
          FWLAB_C31_API_OK);
    CHECK(c35_headless_complete(
        &runtime->headless, &second.command, &semantic, &intent) == C35_OK);
    CHECK(intent.result == FWLAB_C31_COMPLETION_SUCCESS);
    CHECK(runtime->trace.events == 2);
    ok = c35_runtime_teardown(runtime) && c35_storage_close(storage);
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

struct rejecting_binding {
    struct fwlab_c31 *lifecycle;
    uint8_t observed_accepted;
};

static enum c35_result reject_register(
    void *context,
    const struct fwlab_c31_request_token *token,
    const struct fwlab_c31_command_handle *command,
    uint32_t owner_epoch,
    const struct c35_request *request
)
{
    struct rejecting_binding *rejecting = context;
    enum fwlab_c31_lifecycle_state state;

    (void)token;
    (void)owner_epoch;
    (void)request;
    if (fwlab_c31_command_state(
            rejecting->lifecycle, command, &state) == FWLAB_C31_API_OK &&
        state == FWLAB_C31_CMD_ACCEPTED) {
        rejecting->observed_accepted = 1;
    }
    return C35_NO_CAPACITY;
}

static enum c35_result unused_result(
    void *context,
    const struct fwlab_c31_command_handle *command,
    const struct fwlab_c31_completion_intent *intent,
    struct c35_semantic_result *result
)
{
    (void)context; (void)command; (void)intent; (void)result;
    return C35_INVARIANT;
}

static enum c35_result unused_ack(
    void *context,
    const struct fwlab_c31_command_handle *command
)
{
    (void)context; (void)command;
    return C35_INVARIANT;
}

static enum c35_result unused_reset(void *context)
{
    (void)context;
    return C35_INVARIANT;
}

static enum c35_result unused_snapshot(
    void *context,
    struct c35_semantic_result *result
)
{
    (void)context; (void)result;
    return C35_INVARIANT;
}

static enum c35_result unused_quiescent(void *context, bool *quiescent)
{
    (void)context; (void)quiescent;
    return C35_INVARIANT;
}

static const struct c35_binding_ops rejecting_ops = {
    .version = C35_BINDING_VERSION,
    .size = sizeof(struct c35_binding_ops),
    .reserved = 0,
    .register_after_submit = reject_register,
    .result_copy_before_consume = unused_result,
    .result_ack_after_consume = unused_ack,
    .post_reset_recover = unused_reset,
    .semantic_snapshot = unused_snapshot,
    .quiescent = unused_quiescent,
};

static int test_registration_rollback(void)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct rejecting_binding rejecting;
    struct c35_binding original;
    struct c35_request request = c35_request_read(0);
    struct c35_semantic_result semantic;
    struct fwlab_c31_command_handle command;
    uint8_t uuid[16];
    int ok;

    CHECK(storage != NULL && runtime != NULL);
    make_uuid(uuid, C35_LANE_SCRIPTED);
    CHECK(c35_storage_init(storage, C35_LANE_SCRIPTED, uuid));
    CHECK(c35_runtime_init(
        runtime, storage, C35_LANE_SCRIPTED, UINT64_C(0x35210001),
        UINT64_C(0x1122334455667788), 0, 0, 0x3521));
    original = runtime->headless.binding;
    memset(&rejecting, 0, sizeof(rejecting));
    rejecting.lifecycle = runtime->lifecycle;
    runtime->headless.binding.ops = &rejecting_ops;
    runtime->headless.binding.context = &rejecting;
    CHECK(c35_headless_submit(
        &runtime->headless, &request, &command) == C35_NO_CAPACITY);
    CHECK(rejecting.observed_accepted);
    runtime->headless.binding = original;
    CHECK(c35_run_command(runtime, &request, &semantic));
    CHECK(c35_run_command(runtime, &request, &semantic));
    ok = c35_runtime_teardown(runtime) && c35_storage_close(storage);
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

int main(void)
{
    CHECK(test_backpressure_cancel(C35_LANE_MEMORY));
    CHECK(test_backpressure_cancel(C35_LANE_BYTE));
    CHECK(test_registration_rollback());
    puts("C3.5 ownership: PASS (M/B backpressure-cancel + "
         "submit/register rollback)");
    return 0;
}
