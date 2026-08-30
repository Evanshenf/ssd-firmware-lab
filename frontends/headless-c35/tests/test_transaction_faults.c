/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_fault_binding.h"
#include "c35_fault_lifecycle.h"
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

struct fixture {
    struct c35_storage *storage;
    struct c35_runtime *runtime;
};

static uint64_t next_nonce = UINT64_C(0x35a1000000000000);
static uint32_t next_scenario = 0x35a10000u;

static void uuid_make(uint8_t uuid[16], uint64_t nonce)
{
    unsigned int index;

    for (index = 0; index < 16; ++index)
        uuid[index] = (uint8_t)(nonce >> ((index % 8u) * 8u));
}

static int fixture_open(struct fixture *fixture, enum c35_lane lane)
{
    uint8_t uuid[16];
    uint64_t nonce = next_nonce++;

    memset(fixture, 0, sizeof(*fixture));
    fixture->storage = calloc(1, sizeof(*fixture->storage));
    fixture->runtime = calloc(1, sizeof(*fixture->runtime));
    if (fixture->storage == NULL || fixture->runtime == NULL) return 0;
    uuid_make(uuid, nonce);
    return c35_storage_init(fixture->storage, lane, uuid) &&
           c35_runtime_init(
               fixture->runtime, fixture->storage, lane, nonce,
               UINT64_C(0x1020304050607080), 0, 0, next_scenario++);
}

static int fixture_close(struct fixture *fixture)
{
    int ok = 1;

    if (fixture->runtime != NULL &&
        fixture->runtime->headless.service_phase != C35_SERVICE_DEAD)
        ok = c35_runtime_teardown(fixture->runtime);
    if (fixture->storage != NULL) ok = c35_storage_close(fixture->storage) && ok;
    free(fixture->runtime);
    free(fixture->storage);
    memset(fixture, 0, sizeof(*fixture));
    return ok;
}

static int drive(
    struct c35_headless *headless,
    const struct c35_operation_token *token,
    struct c35_operation_status *status
)
{
    unsigned int iteration;

    for (iteration = 0; iteration < 16384; ++iteration) {
        enum c35_result result = c35_operation_progress(
            headless, token, 1, status);

        if (result == C35_OK) return 1;
        if (result != C35_IN_PROGRESS) return 0;
    }
    return 0;
}

static int test_zero_budget_and_finalize(void)
{
    struct fixture fixture;
    struct c35_operation_token token;
    struct c35_operation_token stale;
    struct c35_operation_status zero;
    struct c35_operation_status query;
    struct c35_operation_status final[3];
    struct c35_headless before;
    unsigned int index;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_reset_start(&fixture.runtime->headless, &token) == C35_OK);
    before = fixture.runtime->headless;
    CHECK(c35_operation_progress(
        &fixture.runtime->headless, &token, 0, &zero) == C35_IN_PROGRESS);
    CHECK(memcmp(&before, &fixture.runtime->headless, sizeof(before)) == 0);
    CHECK(zero.units_used == 0 && zero.retry_class == C35_RETRY_SAME_TOKEN);
    CHECK(c35_operation_query(
        &fixture.runtime->headless, &token, &query) == C35_IN_PROGRESS);
    CHECK(memcmp(&before, &fixture.runtime->headless, sizeof(before)) == 0);
    CHECK(drive(&fixture.runtime->headless, &token, &final[0]));
    CHECK(final[0].outcome == C35_OK &&
          final[0].commit_state == C35_COMMIT_COMMITTED &&
          final[0].cleanup_state == C35_CLEANUP_COMPLETE &&
          final[0].publication_valid);
    for (index = 0; index < 3; ++index) {
        CHECK(c35_operation_finalize(
            &fixture.runtime->headless, &token, &final[index]) == C35_OK);
        CHECK(memcmp(&final[0], &final[index], sizeof(final[0])) == 0);
    }
    stale = token;
    ++stale.generation;
    CHECK(c35_operation_query(
        &fixture.runtime->headless, &stale, &query) == C35_STALE);
    CHECK(c35_operation_retire(&fixture.runtime->headless, &token) == C35_OK);
    CHECK(c35_operation_query(
        &fixture.runtime->headless, &token, &query) == C35_STALE);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int test_teardown_finalize_idempotent(void)
{
    struct fixture fixture;
    struct c35_operation_token token;
    struct c35_operation_status status[3];
    unsigned int index;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_teardown_start(&fixture.runtime->headless, &token) == C35_OK);
    CHECK(drive(&fixture.runtime->headless, &token, &status[0]));
    CHECK(status[0].outcome == C35_OK && status[0].publication_valid);
    for (index = 0; index < 3; ++index) {
        CHECK(c35_operation_finalize(
            &fixture.runtime->headless, &token, &status[index]) == C35_OK);
        CHECK(memcmp(&status[0], &status[index], sizeof(status[0])) == 0);
    }
    CHECK(c35_operation_retire(&fixture.runtime->headless, &token) == C35_OK);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int test_runtime_finalizer_idempotent(void)
{
    struct fixture fixture;
    struct c35_operation_token token;
    struct c35_operation_status status[3];
    unsigned int iteration;
    unsigned int index;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_finalizer_start(
        &fixture.runtime->finalizer, &fixture.runtime->headless, NULL,
        fixture.runtime->nonce, &token) == C35_OK);
    for (iteration = 0; iteration < 8192; ++iteration) {
        enum c35_result result = c35_finalizer_progress(
            &fixture.runtime->finalizer, &token, 1, &status[0]);

        CHECK(result == C35_OK || result == C35_IN_PROGRESS);
        if (result == C35_OK) break;
    }
    CHECK(iteration < 8192 && status[0].outcome == C35_OK &&
          status[0].commit_state == C35_COMMIT_COMMITTED &&
          status[0].cleanup_state == C35_CLEANUP_COMPLETE &&
          status[0].publication_valid);
    for (index = 0; index < 3; ++index) {
        CHECK(c35_finalizer_finalize(
            &fixture.runtime->finalizer, &token, &status[index]) == C35_OK);
        CHECK(memcmp(&status[0], &status[index], sizeof(status[0])) == 0);
    }
    CHECK(c35_finalizer_retire(
        &fixture.runtime->finalizer, &token) == C35_OK);
    CHECK(c35_finalizer_query(
        &fixture.runtime->finalizer, &token, &status[0]) == C35_STALE);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int completion_lifecycle_cut(
    enum c35_lifecycle_cut cut,
    enum c35_fault_effect effect
)
{
    struct fixture fixture;
    struct c35_fault_lifecycle fault;
    struct c35_lifecycle_port port;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_intent intent;
    struct c35_semantic_result semantic;
    struct c35_publication publication;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_OK);
    CHECK(c35_fault_lifecycle_init(
        &fault, &fixture.runtime->headless.lifecycle, cut, effect, 1,
        FWLAB_C31_API_NO_CAPACITY));
    port = c35_fault_lifecycle_port(&fault);
    fixture.runtime->headless.lifecycle = port;
    CHECK(c35_headless_complete_observed(
        &fixture.runtime->headless, &command, &semantic, &intent,
        &publication) == C35_OK);
    CHECK(fault.injected && publication.version == C35_PUBLICATION_VERSION &&
          publication.commit_state == C35_COMMIT_COMMITTED);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int submit_lifecycle_cut(enum c35_fault_effect effect)
{
    struct fixture fixture;
    struct c35_fault_lifecycle fault;
    struct c35_lifecycle_port native;
    struct c35_request request = c35_request_read(0);
    struct c35_semantic_result semantic;
    struct c35_operation_status status;
    enum c35_result result;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    native = fixture.runtime->headless.lifecycle;
    CHECK(c35_fault_lifecycle_init(
        &fault, &native, C35_LIFECYCLE_CUT_SUBMIT, effect, 1,
        FWLAB_C31_API_NO_CAPACITY));
    fixture.runtime->headless.lifecycle = c35_fault_lifecycle_port(&fault);
    result = c35_run_command_status(
        fixture.runtime, &request, &semantic, &status);
    CHECK(fault.injected);
    if (effect == C35_FAULT_BEFORE_EFFECT) {
        CHECK(result == C35_NO_CAPACITY);
        fixture.runtime->headless.lifecycle = native;
        CHECK(c35_run_command(fixture.runtime, &request, &semantic));
    } else {
        CHECK(result == C35_OK && status.commit_state == C35_COMMIT_COMMITTED);
    }
    CHECK(fixture_close(&fixture));
    return 1;
}

static int rollback_lifecycle_cut(
    enum c35_lifecycle_cut cut,
    enum c35_fault_effect effect
)
{
    struct fixture fixture;
    struct c35_fault_binding binding_fault;
    struct c35_fault_lifecycle lifecycle_fault;
    struct c35_binding native_binding;
    struct c35_lifecycle_port native_lifecycle;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct c35_semantic_result semantic;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    native_binding = fixture.runtime->headless.binding;
    native_lifecycle = fixture.runtime->headless.lifecycle;
    CHECK(c35_fault_binding_init(
        &binding_fault, &native_binding, C35_BINDING_CUT_REGISTRATION_COMMIT,
        C35_FAULT_BEFORE_EFFECT, 1, C35_NO_CAPACITY));
    CHECK(c35_fault_lifecycle_init(
        &lifecycle_fault, &native_lifecycle, cut, effect, 1,
        FWLAB_C31_API_NO_CAPACITY));
    fixture.runtime->headless.binding =
        c35_fault_binding_provider(&binding_fault);
    fixture.runtime->headless.lifecycle =
        c35_fault_lifecycle_port(&lifecycle_fault);
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_NO_CAPACITY);
    CHECK(binding_fault.injected && lifecycle_fault.injected);
    fixture.runtime->headless.binding = native_binding;
    fixture.runtime->headless.lifecycle = native_lifecycle;
    CHECK(c35_run_command(fixture.runtime, &request, &semantic));
    CHECK(c35_run_command(fixture.runtime, &request, &semantic));
    CHECK(fixture_close(&fixture));
    return 1;
}

static int release_lifecycle_cut(enum c35_fault_effect effect)
{
    struct fixture fixture;
    struct c35_fault_binding binding_fault;
    struct c35_fault_lifecycle lifecycle_fault;
    struct c35_binding native_binding;
    struct c35_lifecycle_port native_lifecycle;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_intent intent;
    struct c35_semantic_result semantic;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_OK);
    native_binding = fixture.runtime->headless.binding;
    native_lifecycle = fixture.runtime->headless.lifecycle;
    CHECK(c35_fault_binding_init(
        &binding_fault, &native_binding, C35_BINDING_CUT_RESULT_PREPARE,
        C35_FAULT_BEFORE_EFFECT, 1, C35_PROVIDER_FAILURE));
    CHECK(c35_fault_lifecycle_init(
        &lifecycle_fault, &native_lifecycle,
        C35_LIFECYCLE_CUT_COMPLETION_RELEASE, effect, 1,
        FWLAB_C31_API_NO_CAPACITY));
    fixture.runtime->headless.binding =
        c35_fault_binding_provider(&binding_fault);
    fixture.runtime->headless.lifecycle =
        c35_fault_lifecycle_port(&lifecycle_fault);
    CHECK(c35_headless_complete(
        &fixture.runtime->headless, &command, &semantic, &intent) ==
          C35_PROVIDER_FAILURE);
    CHECK(binding_fault.injected && lifecycle_fault.injected);
    fixture.runtime->headless.binding = native_binding;
    fixture.runtime->headless.lifecycle = native_lifecycle;
    CHECK(c35_headless_complete(
        &fixture.runtime->headless, &command, &semantic, &intent) == C35_OK);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int reset_lifecycle_cut(
    enum c35_lifecycle_cut cut,
    enum c35_fault_effect effect
)
{
    struct fixture fixture;
    struct c35_fault_lifecycle fault;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct c35_operation_token completion_token;
    struct c35_operation_status completion_status;
    struct c35_publication publication;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    if (cut == C35_LIFECYCLE_CUT_STEP) {
        CHECK(c35_headless_submit(
            &fixture.runtime->headless, &request, &command) == C35_OK);
        CHECK(c35_completion_start(
            &fixture.runtime->headless, &command, &completion_token) == C35_OK);
        CHECK(c35_operation_progress(
            &fixture.runtime->headless, &completion_token, 2,
            &completion_status) == C35_IN_PROGRESS);
    }
    CHECK(c35_fault_lifecycle_init(
        &fault, &fixture.runtime->headless.lifecycle, cut, effect, 1,
        FWLAB_C31_API_NO_CAPACITY));
    fixture.runtime->headless.lifecycle = c35_fault_lifecycle_port(&fault);
    CHECK(c35_headless_reset_observed(
        &fixture.runtime->headless, 16384, &publication) == C35_OK);
    CHECK(fault.injected && publication.kind == C35_PUBLICATION_RESET &&
          publication.commit_state == C35_COMMIT_COMMITTED);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int teardown_lifecycle_cut(
    enum c35_lifecycle_cut cut,
    enum c35_fault_effect effect
)
{
    struct fixture fixture;
    struct c35_fault_lifecycle fault;
    struct c35_publication publication;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct c35_operation_token completion;
    struct c35_operation_status status;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    if (cut == C35_LIFECYCLE_CUT_STEP) {
        CHECK(c35_headless_submit(
            &fixture.runtime->headless, &request, &command) == C35_OK);
        CHECK(c35_completion_start(
            &fixture.runtime->headless, &command, &completion) == C35_OK);
        CHECK(c35_operation_progress(
            &fixture.runtime->headless, &completion, 2, &status) ==
              C35_IN_PROGRESS);
    }
    CHECK(c35_fault_lifecycle_init(
        &fault, &fixture.runtime->headless.lifecycle, cut, effect, 1,
        FWLAB_C31_API_NO_CAPACITY));
    fixture.runtime->headless.lifecycle = c35_fault_lifecycle_port(&fault);
    CHECK(c35_headless_teardown_observed(
        &fixture.runtime->headless, 16384, &publication) == C35_OK);
    CHECK(fault.injected && publication.kind == C35_PUBLICATION_TEARDOWN &&
          publication.commit_state == C35_COMMIT_COMMITTED);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int registration_binding_cut(
    enum c35_binding_cut cut,
    enum c35_fault_effect effect,
    int expect_success
)
{
    struct fixture fixture;
    struct c35_fault_binding fault;
    struct c35_binding native;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct c35_semantic_result semantic;
    enum c35_result result;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    native = fixture.runtime->headless.binding;
    CHECK(c35_fault_binding_init(
        &fault, &native, cut, effect, 1, C35_PROVIDER_FAILURE));
    fixture.runtime->headless.binding = c35_fault_binding_provider(&fault);
    result = c35_headless_submit(
        &fixture.runtime->headless, &request, &command);
    CHECK(fault.injected);
    CHECK(expect_success ? result == C35_OK : result == C35_PROVIDER_FAILURE);
    fixture.runtime->headless.binding = native;
    if (expect_success) {
        struct fwlab_c31_completion_intent intent;

        CHECK(c35_headless_complete(
            &fixture.runtime->headless, &command, &semantic, &intent) == C35_OK);
    } else {
        CHECK(c35_run_command(fixture.runtime, &request, &semantic));
    }
    CHECK(fixture_close(&fixture));
    return 1;
}

static int result_binding_cut(
    enum c35_binding_cut cut,
    enum c35_fault_effect effect,
    int expect_success
)
{
    struct fixture fixture;
    struct c35_fault_binding fault;
    struct c35_binding native;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_intent intent;
    struct c35_semantic_result semantic;
    struct c35_publication publication;
    enum c35_result result;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_OK);
    native = fixture.runtime->headless.binding;
    CHECK(c35_fault_binding_init(
        &fault, &native, cut, effect, 1, C35_PROVIDER_FAILURE));
    fixture.runtime->headless.binding = c35_fault_binding_provider(&fault);
    result = c35_headless_complete_observed(
        &fixture.runtime->headless, &command, &semantic, &intent,
        &publication);
    CHECK(fault.injected);
    CHECK(expect_success ? result == C35_OK : result == C35_PROVIDER_FAILURE);
    fixture.runtime->headless.binding = native;
    if (!expect_success)
        CHECK(c35_headless_complete(
            &fixture.runtime->headless, &command, &semantic, &intent) == C35_OK);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int reset_binding_cut(
    enum c35_binding_cut cut,
    enum c35_fault_effect effect
)
{
    struct fixture fixture;
    struct c35_fault_binding fault;
    struct c35_binding native;
    struct c35_publication publication;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    native = fixture.runtime->headless.binding;
    CHECK(c35_fault_binding_init(
        &fault, &native, cut, effect, 1, C35_PROVIDER_FAILURE));
    fixture.runtime->headless.binding = c35_fault_binding_provider(&fault);
    CHECK(c35_headless_reset_observed(
        &fixture.runtime->headless, 16384, &publication) == C35_OK);
    CHECK(fault.injected && publication.commit_state == C35_COMMIT_COMMITTED);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int teardown_binding_cut(
    enum c35_binding_cut cut,
    enum c35_fault_effect effect
)
{
    struct fixture fixture;
    struct c35_fault_binding fault;
    struct c35_publication publication;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_fault_binding_init(
        &fault, &fixture.runtime->headless.binding, cut, effect, 1,
        C35_PROVIDER_FAILURE));
    fixture.runtime->headless.binding = c35_fault_binding_provider(&fault);
    CHECK(c35_headless_teardown_observed(
        &fixture.runtime->headless, 16384, &publication) == C35_OK);
    CHECK(fault.injected && publication.commit_state == C35_COMMIT_COMMITTED);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int registration_abort_binding_cut(enum c35_fault_effect effect)
{
    struct fixture fixture;
    struct c35_fault_binding commit_fault;
    struct c35_fault_binding abort_fault;
    struct c35_binding native;
    struct c35_binding inner;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct c35_semantic_result semantic;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    native = fixture.runtime->headless.binding;
    CHECK(c35_fault_binding_init(
        &commit_fault, &native, C35_BINDING_CUT_REGISTRATION_COMMIT,
        C35_FAULT_BEFORE_EFFECT, 1, C35_NO_CAPACITY));
    inner = c35_fault_binding_provider(&commit_fault);
    CHECK(c35_fault_binding_init(
        &abort_fault, &inner, C35_BINDING_CUT_REGISTRATION_ABORT,
        effect, 1, C35_PROVIDER_FAILURE));
    fixture.runtime->headless.binding =
        c35_fault_binding_provider(&abort_fault);
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_NO_CAPACITY);
    CHECK(commit_fault.injected && abort_fault.injected);
    fixture.runtime->headless.binding = native;
    CHECK(c35_run_command(fixture.runtime, &request, &semantic));
    CHECK(fixture_close(&fixture));
    return 1;
}

static int result_query_binding_cut(enum c35_fault_effect effect)
{
    struct fixture fixture;
    struct c35_fault_binding prepare_fault;
    struct c35_fault_binding query_fault;
    struct c35_binding native;
    struct c35_binding inner;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_intent intent;
    struct c35_semantic_result semantic;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_OK);
    native = fixture.runtime->headless.binding;
    CHECK(c35_fault_binding_init(
        &prepare_fault, &native, C35_BINDING_CUT_RESULT_PREPARE,
        C35_FAULT_BEFORE_EFFECT, 1, C35_PROVIDER_FAILURE));
    inner = c35_fault_binding_provider(&prepare_fault);
    CHECK(c35_fault_binding_init(
        &query_fault, &inner, C35_BINDING_CUT_RESULT_QUERY,
        effect, 1, C35_PROVIDER_FAILURE));
    fixture.runtime->headless.binding =
        c35_fault_binding_provider(&query_fault);
    CHECK(c35_headless_complete(
        &fixture.runtime->headless, &command, &semantic, &intent) ==
          C35_PROVIDER_FAILURE);
    CHECK(prepare_fault.injected && query_fault.injected);
    fixture.runtime->headless.binding = native;
    CHECK(c35_headless_complete(
        &fixture.runtime->headless, &command, &semantic, &intent) == C35_OK);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int result_abort_binding_cut(enum c35_fault_effect effect)
{
    struct fixture fixture;
    struct c35_fault_binding corrupt;
    struct c35_fault_binding abort_fault;
    struct c35_binding native;
    struct c35_binding inner;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_intent intent;
    struct c35_semantic_result semantic;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_OK);
    native = fixture.runtime->headless.binding;
    CHECK(c35_fault_binding_corrupt_result_init(&corrupt, &native));
    inner = c35_fault_binding_provider(&corrupt);
    CHECK(c35_fault_binding_init(
        &abort_fault, &inner, C35_BINDING_CUT_RESULT_ABORT,
        effect, 1, C35_PROVIDER_FAILURE));
    fixture.runtime->headless.binding =
        c35_fault_binding_provider(&abort_fault);
    CHECK(c35_headless_complete(
        &fixture.runtime->headless, &command, &semantic, &intent) ==
          C35_INVARIANT);
    CHECK(corrupt.injected && abort_fault.injected);
    fixture.runtime->headless.binding = native;
    CHECK(c35_headless_complete(
        &fixture.runtime->headless, &command, &semantic, &intent) == C35_OK);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int reset_query_binding_cut(enum c35_fault_effect effect)
{
    struct fixture fixture;
    struct c35_fault_binding recover_fault;
    struct c35_fault_binding query_fault;
    struct c35_binding inner;
    struct c35_publication publication;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_fault_binding_init(
        &recover_fault, &fixture.runtime->headless.binding,
        C35_BINDING_CUT_RESET_RECOVER, C35_FAULT_BEFORE_EFFECT,
        1, C35_PROVIDER_FAILURE));
    inner = c35_fault_binding_provider(&recover_fault);
    CHECK(c35_fault_binding_init(
        &query_fault, &inner, C35_BINDING_CUT_RESET_QUERY,
        effect, 1, C35_PROVIDER_FAILURE));
    fixture.runtime->headless.binding =
        c35_fault_binding_provider(&query_fault);
    CHECK(c35_headless_reset_observed(
        &fixture.runtime->headless, 16384, &publication) == C35_OK);
    CHECK(recover_fault.injected && query_fault.injected &&
          publication.commit_state == C35_COMMIT_COMMITTED);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int transaction_retire_binding_cut(enum c35_fault_effect effect)
{
    struct fixture fixture;
    struct c35_fault_binding fault;
    struct c35_binding native;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct c35_operation_token token;
    struct c35_operation_status before;
    struct c35_operation_status after;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_OK);
    native = fixture.runtime->headless.binding;
    CHECK(c35_fault_binding_init(
        &fault, &native, C35_BINDING_CUT_TRANSACTION_RETIRE,
        effect, 1, C35_PROVIDER_FAILURE));
    fixture.runtime->headless.binding = c35_fault_binding_provider(&fault);
    CHECK(c35_completion_start(
        &fixture.runtime->headless, &command, &token) == C35_OK);
    CHECK(drive(&fixture.runtime->headless, &token, &before));
    CHECK(before.outcome == C35_OK && before.publication_valid);
    CHECK(c35_operation_retire(&fixture.runtime->headless, &token) ==
          C35_PROVIDER_FAILURE);
    CHECK(fault.injected);
    CHECK(c35_operation_finalize(
        &fixture.runtime->headless, &token, &after) == C35_OK);
    CHECK(memcmp(&before.publication, &after.publication,
                 sizeof(before.publication)) == 0);
    CHECK(c35_operation_retire(&fixture.runtime->headless, &token) == C35_OK);
    fixture.runtime->headless.binding = native;
    CHECK(fixture_close(&fixture));
    return 1;
}

static int abort_query_lifecycle_cut(enum c35_fault_effect effect)
{
    struct fixture fixture;
    struct c35_fault_binding commit_fault;
    struct c35_fault_lifecycle ack_fault;
    struct c35_fault_lifecycle query_fault;
    struct c35_binding native_binding;
    struct c35_lifecycle_port native_lifecycle;
    struct c35_lifecycle_port inner;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    native_binding = fixture.runtime->headless.binding;
    native_lifecycle = fixture.runtime->headless.lifecycle;
    CHECK(c35_fault_binding_init(
        &commit_fault, &native_binding,
        C35_BINDING_CUT_REGISTRATION_COMMIT,
        C35_FAULT_BEFORE_EFFECT, 1, C35_NO_CAPACITY));
    CHECK(c35_fault_lifecycle_init(
        &ack_fault, &native_lifecycle, C35_LIFECYCLE_CUT_ABORT_ACK,
        C35_FAULT_BEFORE_EFFECT, 1, FWLAB_C31_API_NO_CAPACITY));
    inner = c35_fault_lifecycle_port(&ack_fault);
    CHECK(c35_fault_lifecycle_init(
        &query_fault, &inner, C35_LIFECYCLE_CUT_ABORT_QUERY,
        effect, 1, FWLAB_C31_API_NO_CAPACITY));
    fixture.runtime->headless.binding =
        c35_fault_binding_provider(&commit_fault);
    fixture.runtime->headless.lifecycle =
        c35_fault_lifecycle_port(&query_fault);
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_NO_CAPACITY);
    CHECK(commit_fault.injected && ack_fault.injected && query_fault.injected);
    fixture.runtime->headless.binding = native_binding;
    fixture.runtime->headless.lifecycle = native_lifecycle;
    CHECK(fixture_close(&fixture));
    return 1;
}

static int completion_publication_immutable(enum c35_fault_effect effect)
{
    struct fixture fixture;
    struct c35_fault_binding fault;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct c35_operation_token token;
    struct c35_operation_status committed;
    struct c35_operation_status finished;
    unsigned int iteration;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_OK);
    CHECK(c35_fault_binding_init(
        &fault, &fixture.runtime->headless.binding,
        C35_BINDING_CUT_RESULT_ACK, effect, 1, C35_PROVIDER_FAILURE));
    fixture.runtime->headless.binding = c35_fault_binding_provider(&fault);
    CHECK(c35_completion_start(
        &fixture.runtime->headless, &command, &token) == C35_OK);
    for (iteration = 0; iteration < 1024; ++iteration) {
        CHECK(c35_operation_query(
            &fixture.runtime->headless, &token, &committed) ==
              C35_IN_PROGRESS);
        if (committed.internal_phase == C35_COMPLETE_ACK) break;
        CHECK(c35_operation_progress(
            &fixture.runtime->headless, &token, 1, &committed) ==
              C35_IN_PROGRESS);
    }
    CHECK(iteration < 1024 && committed.publication_valid &&
          committed.commit_state == C35_COMMIT_COMMITTED);
    CHECK(drive(&fixture.runtime->headless, &token, &finished));
    CHECK(fault.injected && finished.outcome == C35_OK &&
          finished.cleanup_state == C35_CLEANUP_COMPLETE &&
          memcmp(&committed.publication, &finished.publication,
                 sizeof(committed.publication)) == 0);
    CHECK(c35_operation_retire(&fixture.runtime->headless, &token) == C35_OK);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int reset_publication_immutable(enum c35_fault_effect effect)
{
    struct fixture fixture;
    struct c35_fault_binding fault;
    struct c35_operation_token token;
    struct c35_operation_status committed;
    struct c35_operation_status finished;
    unsigned int iteration;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_fault_binding_init(
        &fault, &fixture.runtime->headless.binding,
        C35_BINDING_CUT_RESET_RECOVER, effect, 1, C35_PROVIDER_FAILURE));
    fixture.runtime->headless.binding = c35_fault_binding_provider(&fault);
    CHECK(c35_reset_start(&fixture.runtime->headless, &token) == C35_OK);
    for (iteration = 0; iteration < 1024; ++iteration) {
        CHECK(c35_operation_query(
            &fixture.runtime->headless, &token, &committed) ==
              C35_IN_PROGRESS);
        if (committed.internal_phase == C35_RESET_RECOVER) break;
        CHECK(c35_operation_progress(
            &fixture.runtime->headless, &token, 1, &committed) ==
              C35_IN_PROGRESS);
    }
    CHECK(iteration < 1024 && committed.publication_valid &&
          committed.commit_state == C35_COMMIT_COMMITTED);
    CHECK(drive(&fixture.runtime->headless, &token, &finished));
    CHECK(fault.injected && finished.outcome == C35_OK &&
          finished.cleanup_state == C35_CLEANUP_COMPLETE &&
          memcmp(&committed.publication, &finished.publication,
                 sizeof(committed.publication)) == 0);
    CHECK(c35_operation_retire(&fixture.runtime->headless, &token) == C35_OK);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int dma_lifecycle_cut(
    enum c35_lifecycle_cut cut,
    enum c35_fault_effect effect
)
{
    struct fixture fixture;
    struct c35_fault_lifecycle fault;
    struct c35_operation_token token;
    struct c35_operation_status status;
    struct c35_publication publication;
    uint8_t input[C35_ATOM_BYTES];
    uint8_t output[C35_ATOM_BYTES];
    unsigned int index;
    unsigned int iteration;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    for (index = 0; index < C35_ATOM_BYTES; ++index)
        input[index] = (uint8_t)(0x91u + index * 7u);
    memset(output, 0, sizeof(output));
    memset(&publication, 0, sizeof(publication));
    CHECK(c35_fault_lifecycle_init(
        &fault, &fixture.runtime->lifecycle_port, cut, effect, 1,
        FWLAB_C31_API_NO_CAPACITY));
    fixture.runtime->lifecycle_port = c35_fault_lifecycle_port(&fault);
    CHECK(c35_dma_start(fixture.runtime, input, &token) == C35_OK);
    for (iteration = 0; iteration < 1024; ++iteration) {
        enum c35_result result = c35_dma_progress(
            fixture.runtime, &token, 1, &status);

        CHECK(result == C35_OK || result == C35_IN_PROGRESS);
        if (result == C35_OK) break;
    }
    CHECK(iteration < 1024 && fault.injected);
    CHECK(c35_dma_finalize(
        fixture.runtime, &token, &status, output, &publication) == C35_OK);
    if (cut == C35_LIFECYCLE_CUT_SUBMIT &&
        effect == C35_FAULT_BEFORE_EFFECT) {
        CHECK(status.outcome == C35_NO_CAPACITY &&
              status.commit_state == C35_COMMIT_NOT_STARTED &&
              !status.publication_valid);
    } else {
        CHECK(status.outcome == C35_OK &&
              status.commit_state == C35_COMMIT_COMMITTED &&
              status.cleanup_state == C35_CLEANUP_COMPLETE &&
              status.publication_valid &&
              memcmp(input, output, sizeof(input)) == 0 &&
              publication.kind == C35_PUBLICATION_DMA);
    }
    CHECK(c35_dma_retire(fixture.runtime, &token) == C35_OK);
    CHECK(c35_dma_query(fixture.runtime, &token, &status) == C35_STALE);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int dma_zero_budget(void)
{
    struct fixture fixture;
    struct c35_operation_token token;
    struct c35_operation_status status[3];
    struct c35_dma_transaction before;
    struct c35_publication publication;
    uint8_t input[C35_ATOM_BYTES];
    uint8_t output[C35_ATOM_BYTES];
    unsigned int iteration;
    unsigned int index;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    memset(input, 0x5a, sizeof(input));
    CHECK(c35_dma_start(fixture.runtime, input, &token) == C35_OK);
    before = fixture.runtime->dma_operation;
    CHECK(c35_dma_progress(
        fixture.runtime, &token, 0, &status[0]) == C35_IN_PROGRESS);
    CHECK(memcmp(&before, &fixture.runtime->dma_operation, sizeof(before)) == 0);
    CHECK(status[0].units_used == 0 &&
          status[0].retry_class == C35_RETRY_SAME_TOKEN);
    for (iteration = 0; iteration < 1024; ++iteration) {
        enum c35_result result = c35_dma_progress(
            fixture.runtime, &token, 1, &status[0]);

        CHECK(result == C35_OK || result == C35_IN_PROGRESS);
        if (result == C35_OK) break;
    }
    CHECK(iteration < 1024);
    for (index = 0; index < 3; ++index) {
        CHECK(c35_dma_finalize(
            fixture.runtime, &token, &status[index], output,
            &publication) == C35_OK);
        CHECK(memcmp(&status[0], &status[index], sizeof(status[0])) == 0);
    }
    CHECK(memcmp(input, output, sizeof(input)) == 0);
    CHECK(c35_dma_retire(fixture.runtime, &token) == C35_OK);
    CHECK(fixture_close(&fixture));
    return 1;
}

int main(void)
{
    enum c35_fault_effect effect;

    CHECK(test_zero_budget_and_finalize());
    CHECK(test_teardown_finalize_idempotent());
    CHECK(test_runtime_finalizer_idempotent());
    CHECK(dma_zero_budget());
    for (effect = C35_FAULT_BEFORE_EFFECT;
         effect <= C35_FAULT_AFTER_EFFECT; ++effect) {
        CHECK(submit_lifecycle_cut(effect));
        CHECK(completion_lifecycle_cut(C35_LIFECYCLE_CUT_STEP, effect));
        CHECK(completion_lifecycle_cut(
            C35_LIFECYCLE_CUT_COMMAND_STATE, effect));
        CHECK(completion_lifecycle_cut(
            C35_LIFECYCLE_CUT_COMPLETION_ACQUIRE, effect));
        CHECK(completion_lifecycle_cut(
            C35_LIFECYCLE_CUT_COMPLETION_CONSUME, effect));
        CHECK(rollback_lifecycle_cut(
            C35_LIFECYCLE_CUT_ABORT_REQUEST, effect));
        CHECK(rollback_lifecycle_cut(
            C35_LIFECYCLE_CUT_COMPLETION_ACQUIRE, effect));
        CHECK(rollback_lifecycle_cut(
            C35_LIFECYCLE_CUT_COMPLETION_CONSUME, effect));
        CHECK(rollback_lifecycle_cut(
            C35_LIFECYCLE_CUT_ABORT_ACK, effect));
        CHECK(release_lifecycle_cut(effect));
        CHECK(reset_lifecycle_cut(C35_LIFECYCLE_CUT_RESET_BEGIN, effect));
        CHECK(reset_lifecycle_cut(C35_LIFECYCLE_CUT_STEP, effect));
        CHECK(reset_lifecycle_cut(C35_LIFECYCLE_CUT_RESET_ACK, effect));
        CHECK(teardown_lifecycle_cut(
            C35_LIFECYCLE_CUT_TEARDOWN_BEGIN, effect));
        CHECK(teardown_lifecycle_cut(C35_LIFECYCLE_CUT_STEP, effect));
        CHECK(teardown_lifecycle_cut(
            C35_LIFECYCLE_CUT_TEARDOWN_ACK, effect));

        CHECK(registration_binding_cut(
            C35_BINDING_CUT_REGISTRATION_PREPARE, effect, 0));
        CHECK(registration_binding_cut(
            C35_BINDING_CUT_REGISTRATION_COMMIT, effect,
            effect == C35_FAULT_AFTER_EFFECT));
        CHECK(registration_binding_cut(
            C35_BINDING_CUT_REGISTRATION_QUERY, effect, 1));
        CHECK(result_binding_cut(
            C35_BINDING_CUT_RESULT_PREPARE, effect,
            effect == C35_FAULT_AFTER_EFFECT));
        CHECK(result_binding_cut(C35_BINDING_CUT_RESULT_ACK, effect, 1));
        CHECK(reset_binding_cut(C35_BINDING_CUT_RESET_RECOVER, effect));
        CHECK(reset_binding_cut(C35_BINDING_CUT_QUIESCENT, effect));
        CHECK(teardown_binding_cut(
            C35_BINDING_CUT_TEARDOWN_FINALIZE, effect));
        CHECK(teardown_binding_cut(C35_BINDING_CUT_QUIESCENT, effect));
        CHECK(registration_abort_binding_cut(effect));
        CHECK(result_query_binding_cut(effect));
        CHECK(result_abort_binding_cut(effect));
        CHECK(reset_query_binding_cut(effect));
        CHECK(transaction_retire_binding_cut(effect));
        CHECK(abort_query_lifecycle_cut(effect));
        CHECK(completion_publication_immutable(effect));
        CHECK(reset_publication_immutable(effect));
        CHECK(dma_lifecycle_cut(C35_LIFECYCLE_CUT_SUBMIT, effect));
        CHECK(dma_lifecycle_cut(C35_LIFECYCLE_CUT_STEP, effect));
        CHECK(dma_lifecycle_cut(
            C35_LIFECYCLE_CUT_COMMAND_STATE, effect));
        CHECK(dma_lifecycle_cut(
            C35_LIFECYCLE_CUT_COMPLETION_ACQUIRE, effect));
        CHECK(dma_lifecycle_cut(
            C35_LIFECYCLE_CUT_COMPLETION_CONSUME, effect));
    }
    puts("C3.5a transaction faults: PASS (80 resumable/zero-budget cuts)");
    return 0;
}
