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
    struct c35_publication publication;
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
        CHECK(runtime->lifecycle_port.ops->step(
                  runtime->lifecycle_port.context, 1, &step) ==
              FWLAB_C31_API_OK);
    }
    CHECK(saw_backpressure);
    CHECK(runtime->lifecycle_port.ops->abort_request(
        runtime->lifecycle_port.context, &first.command, &ticket, &outcome) ==
        FWLAB_C31_API_OK);
    CHECK(outcome == FWLAB_C31_ABORT_PENDING);
    CHECK(c35_headless_complete_observed(
        &runtime->headless, &first.command, &semantic, &intent,
        &publication) == C35_OK);
    CHECK(c35_trace_append(&runtime->trace, &publication) == C35_OK);
    CHECK(intent.result == FWLAB_C31_COMPLETION_ABORTED);
    CHECK(fwlab_c31_abort_query(
        runtime->lifecycle, &ticket, &outcome) == FWLAB_C31_API_OK);
    CHECK(outcome == FWLAB_C31_ABORT_TERMINAL ||
          outcome == FWLAB_C31_ABORT_TOO_LATE);
    CHECK(runtime->lifecycle_port.ops->abort_ack(
        runtime->lifecycle_port.context, &ticket) ==
          FWLAB_C31_API_OK);
    CHECK(c35_headless_complete_observed(
        &runtime->headless, &second.command, &semantic, &intent,
        &publication) == C35_OK);
    CHECK(c35_trace_append(&runtime->trace, &publication) == C35_OK);
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
    struct c35_binding inner;
    uint8_t observed_accepted;
    uint8_t prepared;
    uint8_t aborted;
    uint8_t retired;
};

static enum c35_result reject_prepare(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_request_token *token,
    uint32_t owner_epoch,
    const struct c35_request *request
)
{
    struct rejecting_binding *rejecting = context;

    rejecting->prepared = 1;
    return rejecting->inner.ops->registration_prepare(
        rejecting->inner.context, txid, token, owner_epoch, request);
}

static enum c35_result reject_commit(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command
)
{
    struct rejecting_binding *rejecting = context;
    enum fwlab_c31_lifecycle_state state;

    (void)txid;
    if (fwlab_c31_command_state(
            rejecting->lifecycle, command, &state) == FWLAB_C31_API_OK &&
        state == FWLAB_C31_CMD_ACCEPTED) {
        rejecting->observed_accepted = 1;
    }
    return C35_NO_CAPACITY;
}

static enum c35_result reject_registration_query(
    void *context,
    const struct c35_txid *txid,
    enum c35_registration_state *state
)
{
    struct rejecting_binding *rejecting = context;

    return rejecting->inner.ops->registration_query(
        rejecting->inner.context, txid, state);
}

static enum c35_result reject_registration_abort(
    void *context,
    const struct c35_txid *txid
)
{
    struct rejecting_binding *rejecting = context;
    enum c35_result result = rejecting->inner.ops->registration_abort(
        rejecting->inner.context, txid);

    if (result == C35_OK) rejecting->aborted = 1;
    return result;
}

static enum c35_result reject_result_prepare(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command,
    const struct fwlab_c31_completion_intent *intent,
    struct c35_semantic_result *result
)
{
    struct rejecting_binding *rejecting = context;

    return rejecting->inner.ops->result_prepare(
        rejecting->inner.context, txid, command, intent, result);
}

static enum c35_result reject_result_query(
    void *context,
    const struct c35_txid *txid,
    enum c35_result_state *state
)
{
    struct rejecting_binding *rejecting = context;

    return rejecting->inner.ops->result_query(
        rejecting->inner.context, txid, state);
}

static enum c35_result reject_result_abort(
    void *context,
    const struct c35_txid *txid
)
{
    struct rejecting_binding *rejecting = context;

    return rejecting->inner.ops->result_abort(
        rejecting->inner.context, txid);
}

static enum c35_result reject_result_ack(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command
)
{
    struct rejecting_binding *rejecting = context;

    return rejecting->inner.ops->result_ack(
        rejecting->inner.context, txid, command);
}

static enum c35_result reject_reset_recover(
    void *context,
    const struct c35_txid *txid,
    uint32_t old_epoch,
    uint32_t new_epoch
)
{
    struct rejecting_binding *rejecting = context;

    return rejecting->inner.ops->reset_recover(
        rejecting->inner.context, txid, old_epoch, new_epoch);
}

static enum c35_result reject_reset_query(
    void *context,
    const struct c35_txid *txid,
    enum c35_reset_state *state
)
{
    struct rejecting_binding *rejecting = context;

    return rejecting->inner.ops->reset_query(
        rejecting->inner.context, txid, state);
}

static enum c35_result reject_transaction_retire(
    void *context,
    const struct c35_txid *txid
)
{
    struct rejecting_binding *rejecting = context;
    enum c35_result result = rejecting->inner.ops->transaction_retire(
        rejecting->inner.context, txid);

    if (result == C35_OK) rejecting->retired = 1;
    return result;
}

static enum c35_result reject_teardown_finalize(void *context)
{
    struct rejecting_binding *rejecting = context;

    return rejecting->inner.ops->teardown_finalize(rejecting->inner.context);
}

static enum c35_result reject_snapshot(
    void *context,
    struct c35_semantic_result *result
)
{
    struct rejecting_binding *rejecting = context;

    return rejecting->inner.ops->semantic_snapshot(
        rejecting->inner.context, result);
}

static enum c35_result reject_quiescent(void *context, bool *quiescent)
{
    struct rejecting_binding *rejecting = context;

    return rejecting->inner.ops->quiescent(
        rejecting->inner.context, quiescent);
}

static enum c35_result reject_cause_query(
    void *context,
    struct c35_cause_detail *cause
)
{
    struct rejecting_binding *rejecting = context;

    return rejecting->inner.ops->cause_query(rejecting->inner.context, cause);
}

static const struct c35_binding_ops rejecting_ops = {
    .version = C35_BINDING_OPS_VERSION,
    .size = sizeof(struct c35_binding_ops),
    .reserved = 0,
    .registration_prepare = reject_prepare,
    .registration_commit = reject_commit,
    .registration_query = reject_registration_query,
    .registration_abort = reject_registration_abort,
    .result_prepare = reject_result_prepare,
    .result_query = reject_result_query,
    .result_abort = reject_result_abort,
    .result_ack = reject_result_ack,
    .reset_recover = reject_reset_recover,
    .reset_query = reject_reset_query,
    .transaction_retire = reject_transaction_retire,
    .teardown_finalize = reject_teardown_finalize,
    .semantic_snapshot = reject_snapshot,
    .quiescent = reject_quiescent,
    .cause_query = reject_cause_query,
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
    rejecting.inner = original;
    runtime->headless.binding.ops = &rejecting_ops;
    runtime->headless.binding.context = &rejecting;
    CHECK(c35_headless_submit(
        &runtime->headless, &request, &command) == C35_NO_CAPACITY);
    CHECK(rejecting.prepared);
    CHECK(rejecting.observed_accepted);
    CHECK(rejecting.aborted);
    CHECK(rejecting.retired);
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
         "prepare/submit/commit rollback)");
    return 0;
}
