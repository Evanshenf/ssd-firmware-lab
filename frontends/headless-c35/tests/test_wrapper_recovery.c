/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_fault_binding.h"
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

static uint64_t next_nonce = UINT64_C(0x35b1000000000000);
static uint32_t next_scenario = 0x35b10000u;

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
               UINT64_C(0x35b1abcdef012345), 0, 0, next_scenario++);
}

static int token_equal(
    const struct c35_operation_token *left,
    const struct c35_operation_token *right
)
{
    return left->instance_nonce == right->instance_nonce &&
           left->uid == right->uid && left->generation == right->generation &&
           left->kind == right->kind;
}

static int no_headless_residue(const struct c35_headless *headless)
{
    unsigned int index;

    if (headless->compat_active || headless->compat_tombstone_valid ||
        headless->control_active ||
        headless->previous_control_used || headless->control.used ||
        headless->previous_control.used ||
        headless->active_slot != UINT8_MAX) return 0;
    for (index = 0; index < C35_OPERATION_SLOTS; ++index) {
        if (headless->operation[index].used) return 0;
    }
    return 1;
}

static int fixture_close(struct fixture *fixture)
{
    bool quiescent = false;
    int ok = 1;

    if (fixture->runtime != NULL && fixture->runtime->claimed)
        ok = c35_runtime_teardown(fixture->runtime);
    if (fixture->runtime != NULL) {
        ok = no_headless_residue(&fixture->runtime->headless) && ok;
        ok = !fixture->runtime->finalizer.used && ok;
        ok = fixture->runtime->headless.binding.ops->quiescent(
                 fixture->runtime->headless.binding.context, &quiescent) ==
                 C35_OK && quiescent && ok;
    }
    if (fixture->storage != NULL) {
        ok = !fixture->storage->bundle.claimed && ok;
        ok = c35_storage_close(fixture->storage) && ok;
        ok = fixture->storage->fd == -1 && ok;
    }
    free(fixture->runtime);
    free(fixture->storage);
    memset(fixture, 0, sizeof(*fixture));
    return ok;
}

static int compat_token(
    struct c35_headless *headless,
    uint8_t kind,
    struct c35_operation_token *token,
    struct c35_operation_status *status
)
{
    enum c35_result result = c35_headless_compat_query(
        headless, token, status);

    return (result == C35_OK || result == C35_IN_PROGRESS) &&
           token->kind == kind;
}

static int test_invalid_contract(void)
{
    struct c35_publication publication;
    struct c35_operation_status status;
    struct c35_operation_token token;

    CHECK(c35_headless_reset_status(NULL, 0, &publication, &status) ==
          C35_INVALID);
    CHECK(c35_headless_teardown_status(NULL, 0, &publication, &status) ==
          C35_INVALID);
    CHECK(c35_headless_compat_query(NULL, &token, &status) == C35_INVALID);
    CHECK(c35_headless_compat_transfer(NULL, &token) == C35_INVALID);
    return 1;
}

static int test_submit_budget_resume(void)
{
    struct fixture fixture;
    struct c35_request request = c35_request_read(0);
    struct c35_request different = c35_request_read(1);
    struct c35_submission submission;
    struct c35_operation_status status;
    struct c35_operation_token first;
    struct c35_operation_token again;
    struct c35_headless before_mismatch;
    unsigned int iteration;
    enum c35_result result;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_headless_submit_status(
        &fixture.runtime->headless, &request, 0, &submission, &status) ==
          C35_IN_PROGRESS);
    CHECK(compat_token(
        &fixture.runtime->headless, C35_OPERATION_SUBMIT, &first, &status));
    before_mismatch = fixture.runtime->headless;
    CHECK(c35_headless_submit_status(
        &fixture.runtime->headless, &different, 1, &submission, &status) ==
          C35_INVALID);
    CHECK(memcmp(&before_mismatch, &fixture.runtime->headless,
                 sizeof(before_mismatch)) == 0);
    CHECK(compat_token(
        &fixture.runtime->headless, C35_OPERATION_SUBMIT, &again, &status) &&
          token_equal(&first, &again));
    result = C35_IN_PROGRESS;
    for (iteration = 0; iteration < 512 && result == C35_IN_PROGRESS;
         ++iteration) {
        result = c35_headless_submit_status(
            &fixture.runtime->headless, &request, 1, &submission, &status);
    }
    CHECK(result == C35_OK && submission.command.command_uid != 0);
    CHECK(c35_headless_compat_query(
        &fixture.runtime->headless, &again, &status) == C35_NOT_FOUND);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int test_completion_budget_resume(void)
{
    struct fixture fixture;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_command_handle different;
    struct c35_semantic_result semantic;
    struct fwlab_c31_completion_intent intent;
    struct c35_publication publication;
    struct c35_operation_status status;
    struct c35_operation_token first;
    struct c35_operation_token again;
    struct c35_headless before_mismatch;
    unsigned int iteration;
    enum c35_result result;

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_OK);
    CHECK(c35_headless_complete_status(
        &fixture.runtime->headless, &command, 0, &semantic, &intent,
        &publication, &status) == C35_IN_PROGRESS);
    CHECK(compat_token(
        &fixture.runtime->headless, C35_OPERATION_COMPLETION, &first,
        &status));
    different = command;
    ++different.command_uid;
    before_mismatch = fixture.runtime->headless;
    CHECK(c35_headless_complete_status(
        &fixture.runtime->headless, &different, 1, &semantic, &intent,
        &publication, &status) == C35_INVALID);
    CHECK(memcmp(&before_mismatch, &fixture.runtime->headless,
                 sizeof(before_mismatch)) == 0);
    CHECK(compat_token(
        &fixture.runtime->headless, C35_OPERATION_COMPLETION, &again,
        &status) && token_equal(&first, &again));
    result = C35_IN_PROGRESS;
    for (iteration = 0; iteration < 16384 && result == C35_IN_PROGRESS;
         ++iteration) {
        result = c35_headless_complete_status(
            &fixture.runtime->headless, &command, 1, &semantic, &intent,
            &publication, &status);
    }
    CHECK(result == C35_OK && publication.version == C35_PUBLICATION_VERSION);
    CHECK(c35_headless_compat_query(
        &fixture.runtime->headless, &again, &status) == C35_NOT_FOUND);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int test_reset_budget_resume(void)
{
    struct fixture fixture;
    struct c35_publication publication;
    struct c35_operation_status status;
    struct c35_operation_token first;
    struct c35_operation_token again;
    unsigned int iteration;
    enum c35_result result;

    CHECK(fixture_open(&fixture, C35_LANE_MEMORY));
    CHECK(c35_headless_reset_status(
        &fixture.runtime->headless, 0, &publication, &status) ==
          C35_IN_PROGRESS);
    CHECK(compat_token(
        &fixture.runtime->headless, C35_OPERATION_RESET, &first, &status));
    result = c35_headless_reset_status(
        &fixture.runtime->headless, 1, &publication, &status);
    CHECK(compat_token(
        &fixture.runtime->headless, C35_OPERATION_RESET, &again, &status) &&
          token_equal(&first, &again));
    for (iteration = 1; iteration < 16384 && result == C35_IN_PROGRESS;
         ++iteration) {
        result = c35_headless_reset_status(
            &fixture.runtime->headless, 1, &publication, &status);
    }
    CHECK(result == C35_OK && publication.kind == C35_PUBLICATION_RESET);
    CHECK(c35_headless_compat_query(
        &fixture.runtime->headless, &again, &status) == C35_NOT_FOUND);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int start_compat_operation(
    struct fixture *fixture,
    uint8_t kind,
    struct fwlab_c31_command_handle *command
)
{
    struct c35_request request = c35_request_read(0);
    struct c35_submission submission;
    struct c35_semantic_result semantic;
    struct fwlab_c31_completion_intent intent;
    struct c35_publication publication;
    struct c35_operation_status status;

    if (kind == C35_OPERATION_SUBMIT)
        return c35_headless_submit_status(
                   &fixture->runtime->headless, &request, 0, &submission,
                   &status) == C35_IN_PROGRESS;
    if (kind == C35_OPERATION_COMPLETION) {
        if (c35_headless_submit(
                &fixture->runtime->headless, &request, command) != C35_OK)
            return 0;
        return c35_headless_complete_status(
                   &fixture->runtime->headless, command, 0, &semantic,
                   &intent, &publication, &status) == C35_IN_PROGRESS;
    }
    if (kind == C35_OPERATION_RESET)
        return c35_headless_reset_status(
                   &fixture->runtime->headless, 0, &publication, &status) ==
               C35_IN_PROGRESS;
    return c35_headless_teardown_status(
               &fixture->runtime->headless, 0, &publication, &status) ==
           C35_IN_PROGRESS;
}

static int test_finalizer_takeover_all(void)
{
    static const uint8_t kinds[] = {
        C35_OPERATION_SUBMIT, C35_OPERATION_COMPLETION,
        C35_OPERATION_RESET, C35_OPERATION_TEARDOWN
    };
    unsigned int index;

    for (index = 0; index < sizeof(kinds) / sizeof(kinds[0]); ++index) {
        struct fixture fixture;
        struct fwlab_c31_command_handle command;
        struct c35_operation_token pending;
        struct c35_operation_status status;

        CHECK(fixture_open(
            &fixture, kinds[index] == C35_OPERATION_TEARDOWN ?
                          C35_LANE_POSIX : C35_LANE_MEMORY));
        CHECK(start_compat_operation(&fixture, kinds[index], &command));
        CHECK(compat_token(
            &fixture.runtime->headless, kinds[index], &pending, &status));
        if (kinds[index] == C35_OPERATION_TEARDOWN) {
            struct c35_publication publication;
            struct c35_operation_token after_one;

            CHECK(c35_headless_teardown_status(
                &fixture.runtime->headless, 1, &publication, &status) ==
                  C35_IN_PROGRESS);
            CHECK(compat_token(
                &fixture.runtime->headless, C35_OPERATION_TEARDOWN,
                &after_one, &status) && token_equal(&pending, &after_one));
        }
        CHECK(c35_runtime_teardown(fixture.runtime));
        CHECK(!fixture.runtime->claimed &&
              !fixture.storage->bundle.claimed &&
              no_headless_residue(&fixture.runtime->headless));
        CHECK(c35_headless_compat_query(
            &fixture.runtime->headless, &pending, &status) == C35_NOT_FOUND);
        CHECK(fixture_close(&fixture));
    }
    return 1;
}

static int test_completed_teardown_adoption(void)
{
    struct fixture fixture;
    struct c35_publication publication;
    struct c35_operation_status status;
    struct c35_operation_token token;

    CHECK(fixture_open(&fixture, C35_LANE_POSIX));
    CHECK(c35_headless_teardown_status(
        &fixture.runtime->headless, 16384, &publication, &status) == C35_OK);
    CHECK(fixture.runtime->headless.service_phase == C35_SERVICE_DEAD &&
          fixture.runtime->claimed && fixture.storage->bundle.claimed);
    CHECK(c35_headless_compat_query(
        &fixture.runtime->headless, &token, &status) == C35_OK &&
          token.kind == C35_OPERATION_TEARDOWN &&
          status.publication_valid &&
          memcmp(&status.publication, &publication, sizeof(publication)) == 0);
    CHECK(c35_runtime_teardown(fixture.runtime));
    CHECK(!fixture.runtime->claimed && !fixture.storage->bundle.claimed &&
          no_headless_residue(&fixture.runtime->headless));
    CHECK(fixture_close(&fixture));
    return 1;
}

static int retire_fault_case(
    uint8_t kind,
    enum c35_fault_effect effect
)
{
    struct fixture fixture;
    struct c35_fault_binding setup_fault;
    struct c35_fault_binding fault;
    struct c35_binding native;
    struct c35_binding retire_inner;
    struct c35_request request = c35_request_read(0);
    struct c35_submission submission;
    struct fwlab_c31_command_handle command;
    struct c35_semantic_result semantic;
    struct fwlab_c31_completion_intent intent;
    struct c35_publication publication;
    struct c35_publication repeated_publication;
    struct c35_operation_status status;
    struct c35_operation_token token;
    enum c35_result result;
    enum c35_result retire_error = kind == C35_OPERATION_SUBMIT ?
        C35_NO_CAPACITY : C35_PROVIDER_FAILURE;
    enum c35_result business_outcome = kind == C35_OPERATION_SUBMIT ?
        C35_PROVIDER_FAILURE : C35_OK;

    CHECK(kind >= C35_OPERATION_SUBMIT && kind <= C35_OPERATION_RESET);
    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    if (kind == C35_OPERATION_COMPLETION)
        CHECK(c35_headless_submit(
            &fixture.runtime->headless, &request, &command) == C35_OK);
    native = fixture.runtime->headless.binding;
    retire_inner = native;
    if (kind == C35_OPERATION_SUBMIT) {
        CHECK(c35_fault_binding_init(
            &setup_fault, &native, C35_BINDING_CUT_REGISTRATION_COMMIT,
            C35_FAULT_BEFORE_EFFECT, 1, C35_PROVIDER_FAILURE));
        retire_inner = c35_fault_binding_provider(&setup_fault);
    }
    CHECK(c35_fault_binding_init(
        &fault, &retire_inner, C35_BINDING_CUT_TRANSACTION_RETIRE, effect, 1,
        retire_error));
    fixture.runtime->headless.binding = c35_fault_binding_provider(&fault);
    memset(&publication, 0, sizeof(publication));
    if (kind == C35_OPERATION_SUBMIT)
        result = c35_headless_submit_status(
            &fixture.runtime->headless, &request, 16384, &submission,
            &status);
    else if (kind == C35_OPERATION_COMPLETION)
        result = c35_headless_complete_status(
            &fixture.runtime->headless, &command, 16384, &semantic, &intent,
            &publication, &status);
    else
        result = c35_headless_reset_status(
            &fixture.runtime->headless, 16384, &publication, &status);
    if (!(result == retire_error && fault.injected &&
          status.call_state == C35_CALL_DONE &&
          status.outcome == (uint32_t)business_outcome &&
          status.cleanup_state == C35_CLEANUP_PENDING &&
          status.cause_domain == C35_CAUSE_BINDING &&
          status.retry_class == C35_RETRY_SAME_TOKEN)) {
        fprintf(stderr,
                "retire fault kind=%u effect=%u result=%u injected=%u "
                "call=%u outcome=%u cleanup=%u cause=%u retry=%u\n",
                kind, (unsigned int)effect, (unsigned int)result,
                fault.injected, status.call_state, status.outcome,
                status.cleanup_state, status.cause_domain,
                status.retry_class);
        CHECK(0);
    }
    CHECK(compat_token(
        &fixture.runtime->headless, kind, &token, &status));
    if ((kind == C35_OPERATION_COMPLETION || kind == C35_OPERATION_RESET) &&
        publication.version == C35_PUBLICATION_VERSION) {
        CHECK(status.publication_valid &&
              memcmp(&status.publication, &publication,
                     sizeof(publication)) == 0);
    }
    if (effect == C35_FAULT_AFTER_EFFECT) {
        CHECK(c35_runtime_teardown(fixture.runtime));
        CHECK(!fixture.runtime->claimed &&
              !fixture.storage->bundle.claimed &&
              no_headless_residue(&fixture.runtime->headless));
    } else if (kind == C35_OPERATION_SUBMIT) {
        result = c35_headless_submit_status(
            &fixture.runtime->headless, &request, 0, &submission, &status);
        CHECK(result == business_outcome &&
              status.cleanup_state == C35_CLEANUP_COMPLETE);
    } else if (kind == C35_OPERATION_COMPLETION) {
        repeated_publication = publication;
        result = c35_headless_complete_status(
            &fixture.runtime->headless, &command, 0, &semantic, &intent,
            &publication, &status);
        CHECK(result == C35_OK && status.cleanup_state == C35_CLEANUP_COMPLETE &&
              memcmp(&publication, &repeated_publication,
                     sizeof(publication)) == 0);
    } else {
        repeated_publication = publication;
        result = c35_headless_reset_status(
            &fixture.runtime->headless, 0, &publication, &status);
        CHECK(result == C35_OK && status.cleanup_state == C35_CLEANUP_COMPLETE &&
              memcmp(&publication, &repeated_publication,
                     sizeof(publication)) == 0);
    }
    CHECK(c35_headless_compat_query(
        &fixture.runtime->headless, &token, &status) == C35_NOT_FOUND);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int test_retire_faults(void)
{
    uint8_t kind;
    enum c35_fault_effect effect;

    for (kind = C35_OPERATION_SUBMIT; kind <= C35_OPERATION_RESET; ++kind) {
        for (effect = C35_FAULT_BEFORE_EFFECT;
             effect <= C35_FAULT_AFTER_EFFECT; ++effect) {
            CHECK(retire_fault_case(kind, effect));
        }
    }
    return 1;
}

static enum c35_result always_fail_retire(
    void *context,
    const struct c35_txid *txid
)
{
    (void)context;
    (void)txid;
    return C35_NO_CAPACITY;
}

static int test_permanent_retire_retention(void)
{
    struct fixture fixture;
    struct c35_binding native;
    struct c35_binding_ops failing_ops;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct c35_semantic_result semantic;
    struct fwlab_c31_completion_intent intent;
    struct c35_publication publication[3];
    struct c35_operation_status status;
    struct c35_operation_token token[2];

    CHECK(fixture_open(&fixture, C35_LANE_SCRIPTED));
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_OK);
    native = fixture.runtime->headless.binding;
    failing_ops = *native.ops;
    failing_ops.transaction_retire = always_fail_retire;
    fixture.runtime->headless.binding.ops = &failing_ops;
    CHECK(c35_headless_complete_status(
        &fixture.runtime->headless, &command, 16384, &semantic, &intent,
        &publication[0], &status) == C35_NO_CAPACITY);
    CHECK(compat_token(
        &fixture.runtime->headless, C35_OPERATION_COMPLETION, &token[0],
        &status) && status.cleanup_state == C35_CLEANUP_PENDING);
    CHECK(c35_headless_complete_status(
        &fixture.runtime->headless, &command, 0, &semantic, &intent,
        &publication[1], &status) == C35_NO_CAPACITY);
    CHECK(compat_token(
        &fixture.runtime->headless, C35_OPERATION_COMPLETION, &token[1],
        &status) && token_equal(&token[0], &token[1]) &&
          memcmp(&publication[0], &publication[1], sizeof(publication[0])) ==
              0);
    fixture.runtime->headless.binding = native;
    CHECK(c35_headless_complete_status(
        &fixture.runtime->headless, &command, 0, &semantic, &intent,
        &publication[2], &status) == C35_OK);
    CHECK(status.cleanup_state == C35_CLEANUP_COMPLETE &&
          memcmp(&publication[0], &publication[2], sizeof(publication[0])) ==
              0 &&
          c35_headless_compat_query(
              &fixture.runtime->headless, &token[0], &status) ==
              C35_NOT_FOUND);
    CHECK(fixture_close(&fixture));
    return 1;
}

static int test_permanent_retire_finalizer(void)
{
    struct fixture fixture;
    struct c35_binding native;
    struct c35_binding_ops failing_ops;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct c35_semantic_result semantic;
    struct fwlab_c31_completion_intent intent;
    struct c35_publication completion;
    struct c35_publication teardown;
    struct c35_operation_status status;
    struct c35_operation_token pending;
    struct c35_finalizer before_zero;

    CHECK(fixture_open(&fixture, C35_LANE_POSIX));
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_OK);
    native = fixture.runtime->headless.binding;
    failing_ops = *native.ops;
    failing_ops.transaction_retire = always_fail_retire;
    fixture.runtime->headless.binding.ops = &failing_ops;
    CHECK(c35_headless_complete_status(
        &fixture.runtime->headless, &command, 16384, &semantic, &intent,
        &completion, &status) == C35_NO_CAPACITY);
    CHECK(compat_token(
        &fixture.runtime->headless, C35_OPERATION_COMPLETION, &pending,
        &status));

    CHECK(!c35_runtime_teardown(fixture.runtime));
    CHECK(fixture.runtime->finalizer.used &&
          fixture.runtime->finalizer.phase == C35_FINALIZER_PENDING_RETIRE &&
          fixture.runtime->finalizer.pending_retire &&
          fixture.runtime->claimed && fixture.storage->bundle.claimed &&
          fixture.storage->fd >= 0 &&
          c35_headless_compat_query(
              &fixture.runtime->headless, &pending, &status) ==
              C35_NOT_FOUND);
    CHECK(c35_operation_query(
        &fixture.runtime->headless, &pending, &status) == C35_OK &&
          status.cleanup_state == C35_CLEANUP_PENDING &&
          status.publication_valid &&
          memcmp(&status.publication, &completion, sizeof(completion)) == 0);
    CHECK(c35_finalizer_query(
        &fixture.runtime->finalizer, &fixture.runtime->finalizer.token,
        &status) == C35_IN_PROGRESS && status.publication_valid);
    teardown = status.publication;

    before_zero = fixture.runtime->finalizer;
    CHECK(c35_finalizer_progress(
        &fixture.runtime->finalizer, &fixture.runtime->finalizer.token, 0,
        &status) == C35_IN_PROGRESS);
    CHECK(memcmp(&before_zero, &fixture.runtime->finalizer,
                 sizeof(before_zero)) == 0 &&
          memcmp(&status.publication, &teardown, sizeof(teardown)) == 0);
    CHECK(c35_finalizer_progress(
        &fixture.runtime->finalizer, &fixture.runtime->finalizer.token, 1,
        &status) == C35_IN_PROGRESS);
    CHECK(fixture.runtime->finalizer.phase == C35_FINALIZER_PENDING_RETIRE &&
          fixture.runtime->finalizer.pending_retire &&
          !fixture.runtime->finalizer.bundle_released &&
          fixture.storage->bundle.claimed &&
          memcmp(&status.publication, &teardown, sizeof(teardown)) == 0);
    CHECK(c35_operation_query(
        &fixture.runtime->headless, &pending, &status) == C35_OK &&
          memcmp(&status.publication, &completion, sizeof(completion)) == 0);

    fixture.runtime->headless.binding = native;
    CHECK(c35_runtime_teardown(fixture.runtime));
    CHECK(!fixture.runtime->claimed && !fixture.storage->bundle.claimed &&
          !fixture.runtime->finalizer.used &&
          no_headless_residue(&fixture.runtime->headless));
    CHECK(fixture_close(&fixture));
    return 1;
}

int main(void)
{
    CHECK(test_invalid_contract());
    CHECK(test_submit_budget_resume());
    CHECK(test_completion_budget_resume());
    CHECK(test_reset_budget_resume());
    CHECK(test_finalizer_takeover_all());
    CHECK(test_completed_teardown_adoption());
    CHECK(test_retire_faults());
    CHECK(test_permanent_retire_retention());
    CHECK(test_permanent_retire_finalizer());
    puts("C3.5c wrapper recovery: PASS (zero/short budget; same-token retry; "
         "4-kind takeover; retire before/after/permanent wrapper+finalizer)");
    return 0;
}
