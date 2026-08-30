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

struct fixture {
    struct c35_storage *storage;
    struct c35_runtime *runtime;
};

static uint64_t nonce_next = UINT64_C(0x35a9000000000001);

static int fixture_open(struct fixture *fixture)
{
    uint8_t uuid[16] = {
        0x35, 0x90, 0x19, 0x26, 0x10, 0x20, 0x30, 0x40,
        0x50, 0x60, 0x70, 0x80, 0x44, 0x33, 0x22, 0x11
    };
    uint64_t nonce = nonce_next++;

    memset(fixture, 0, sizeof(*fixture));
    fixture->storage = calloc(1, sizeof(*fixture->storage));
    fixture->runtime = calloc(1, sizeof(*fixture->runtime));
    return fixture->storage != NULL && fixture->runtime != NULL &&
           c35_storage_init(
               fixture->storage, C35_LANE_SCRIPTED, uuid) &&
           c35_runtime_init(
               fixture->runtime, fixture->storage, C35_LANE_SCRIPTED,
               nonce, UINT64_C(0x1928374655647382), 0, 0,
               (uint32_t)nonce);
}

static int fixture_reap(struct fixture *fixture)
{
    int ok = fixture->runtime->headless.service_phase == C35_SERVICE_DEAD &&
             c35_storage_close(fixture->storage);

    free(fixture->runtime);
    free(fixture->storage);
    return ok;
}

static int drive_teardown(
    struct c35_runtime *runtime,
    struct c35_operation_token *token
)
{
    struct c35_operation_status status;
    unsigned int iteration;

    CHECK(c35_teardown_start(&runtime->headless, token) == C35_OK);
    CHECK(c35_teardown_start(&runtime->headless, token) == C35_WRONG_STATE);
    for (iteration = 0; iteration < 16384; ++iteration) {
        enum c35_result result = c35_operation_progress(
            &runtime->headless, token, 1, &status);

        CHECK(result == C35_OK || result == C35_IN_PROGRESS);
        if (result == C35_OK) break;
    }
    CHECK(iteration < 16384 && status.outcome == C35_OK &&
          status.commit_state == C35_COMMIT_COMMITTED &&
          status.cleanup_state == C35_CLEANUP_COMPLETE &&
          status.publication_valid &&
          status.publication.kind == C35_PUBLICATION_TEARDOWN);
    CHECK(c35_operation_retire(&runtime->headless, token) == C35_OK);
    return 1;
}

static int advance_completion_to_state(
    struct c35_runtime *runtime,
    const struct fwlab_c31_command_handle *command,
    enum fwlab_c31_lifecycle_state target,
    struct c35_operation_token *completion
)
{
    struct c35_operation_status status;
    unsigned int iteration;

    CHECK(c35_completion_start(&runtime->headless, command, completion) ==
          C35_OK);
    for (iteration = 0; iteration < 256; ++iteration) {
        enum fwlab_c31_lifecycle_state state;

        CHECK(fwlab_c31_command_state(runtime->lifecycle, command, &state) ==
              FWLAB_C31_API_OK);
        if (state == target) return 1;
        CHECK(c35_operation_progress(
            &runtime->headless, completion, 1, &status) == C35_IN_PROGRESS);
    }
    return 0;
}

static int test_command_state_takeover(
    enum fwlab_c31_lifecycle_state target
)
{
    struct fixture fixture;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct c35_operation_token completion;
    struct c35_operation_token teardown;
    struct c35_operation_status status;
    int has_completion = target != FWLAB_C31_CMD_ACCEPTED;

    CHECK(fixture_open(&fixture));
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_OK);
    if (target == FWLAB_C31_CMD_HELD)
        fixture.runtime->scripted_nfc.scenarios[0].backpressure_count = 1;
    if (has_completion)
        CHECK(advance_completion_to_state(
            fixture.runtime, &command, target, &completion));
    else {
        enum fwlab_c31_lifecycle_state state;

        CHECK(fwlab_c31_command_state(
            fixture.runtime->lifecycle, &command, &state) ==
              FWLAB_C31_API_OK && state == target);
    }
    CHECK(drive_teardown(fixture.runtime, &teardown));
    if (has_completion) {
        CHECK(c35_operation_finalize(
            &fixture.runtime->headless, &completion, &status) == C35_OK);
        CHECK(status.commit_state == C35_COMMIT_SUPERSEDED &&
              status.cleanup_state == C35_CLEANUP_COMPLETE &&
              !status.publication_valid);
        CHECK(c35_operation_retire(
            &fixture.runtime->headless, &completion) == C35_OK);
    }
    CHECK(fwlab_c31_phase(fixture.runtime->lifecycle) ==
          FWLAB_C31_INSTANCE_DEAD);
    CHECK(fixture_reap(&fixture));
    return 1;
}

static int advance_to_internal_phase(
    struct c35_runtime *runtime,
    const struct c35_operation_token *token,
    uint32_t target
)
{
    struct c35_operation_status status;
    unsigned int iteration;

    for (iteration = 0; iteration < 1024; ++iteration) {
        CHECK(c35_operation_query(&runtime->headless, token, &status) ==
              C35_IN_PROGRESS);
        if (status.internal_phase == target) return 1;
        CHECK(c35_operation_progress(
            &runtime->headless, token, 1, &status) == C35_IN_PROGRESS);
    }
    return 0;
}

static int test_completion_commit_takeover(uint32_t target)
{
    struct fixture fixture;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct c35_operation_token completion;
    struct c35_operation_token teardown;
    struct c35_operation_status status;
    int committed = target == C35_COMPLETE_ACK;

    CHECK(fixture_open(&fixture));
    CHECK(c35_headless_submit(
        &fixture.runtime->headless, &request, &command) == C35_OK);
    CHECK(c35_completion_start(
        &fixture.runtime->headless, &command, &completion) == C35_OK);
    CHECK(advance_to_internal_phase(fixture.runtime, &completion, target));
    CHECK(drive_teardown(fixture.runtime, &teardown));
    CHECK(c35_operation_finalize(
        &fixture.runtime->headless, &completion, &status) == C35_OK);
    CHECK(status.commit_state ==
          (committed ? C35_COMMIT_COMMITTED : C35_COMMIT_SUPERSEDED));
    CHECK(status.publication_valid == (uint8_t)committed);
    if (committed)
        CHECK(status.publication.kind == C35_PUBLICATION_COMMAND);
    CHECK(c35_operation_retire(
        &fixture.runtime->headless, &completion) == C35_OK);
    CHECK(fixture_reap(&fixture));
    return 1;
}

static int prepare_running_command(
    struct c35_runtime *runtime,
    struct c35_operation_token *completion
)
{
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;

    CHECK(c35_headless_submit(&runtime->headless, &request, &command) == C35_OK);
    return advance_completion_to_state(
        runtime, &command, FWLAB_C31_CMD_RUNNING, completion);
}

static int test_reset_takeover(uint32_t target, int active_command)
{
    struct fixture fixture;
    struct c35_operation_token completion;
    struct c35_operation_token reset;
    struct c35_operation_token teardown;
    struct c35_operation_status status;
    unsigned int iteration;
    int committed = target == C35_RESET_RECOVER ||
                    target == C35_RESET_QUIESCENT;

    CHECK(fixture_open(&fixture));
    if (active_command) CHECK(prepare_running_command(fixture.runtime, &completion));
    CHECK(c35_reset_start(&fixture.runtime->headless, &reset) == C35_OK);
    for (iteration = 0; iteration < 1024; ++iteration) {
        CHECK(c35_operation_query(
            &fixture.runtime->headless, &reset, &status) == C35_IN_PROGRESS);
        if (status.internal_phase == target) break;
        CHECK(c35_operation_progress(
            &fixture.runtime->headless, &reset, 1, &status) ==
              C35_IN_PROGRESS);
    }
    CHECK(iteration < 1024);
    CHECK(drive_teardown(fixture.runtime, &teardown));
    CHECK(c35_operation_finalize(
        &fixture.runtime->headless, &reset, &status) == C35_OK);
    CHECK(status.commit_state ==
          (committed ? C35_COMMIT_COMMITTED : C35_COMMIT_SUPERSEDED));
    CHECK(status.publication_valid == (uint8_t)committed);
    CHECK(c35_operation_retire(&fixture.runtime->headless, &reset) == C35_OK);
    if (active_command) {
        CHECK(c35_operation_finalize(
            &fixture.runtime->headless, &completion, &status) == C35_OK);
        CHECK(status.commit_state == C35_COMMIT_SUPERSEDED);
        CHECK(c35_operation_retire(
            &fixture.runtime->headless, &completion) == C35_OK);
    }
    CHECK(fixture_reap(&fixture));
    return 1;
}

int main(void)
{
    CHECK(test_command_state_takeover(FWLAB_C31_CMD_ACCEPTED));
    CHECK(test_command_state_takeover(FWLAB_C31_CMD_DISPATCHED));
    CHECK(test_command_state_takeover(FWLAB_C31_CMD_HELD));
    CHECK(test_command_state_takeover(FWLAB_C31_CMD_RUNNING));
    CHECK(test_command_state_takeover(FWLAB_C31_CMD_COMPLETION_READY));
    CHECK(test_command_state_takeover(FWLAB_C31_CMD_COMPLETION_LEASED));
    CHECK(test_completion_commit_takeover(C35_COMPLETE_CONSUME));
    CHECK(test_completion_commit_takeover(C35_COMPLETE_ACK));
    CHECK(test_reset_takeover(C35_RESET_BEGIN, 0));
    CHECK(test_reset_takeover(C35_RESET_DRAIN, 1));
    CHECK(test_reset_takeover(C35_RESET_ACK, 0));
    CHECK(test_reset_takeover(C35_RESET_RECOVER, 0));
    CHECK(test_reset_takeover(C35_RESET_QUIESCENT, 0));
    puts("C3.5a phase takeover: PASS (6 C31 command states; "
         "pre/post-consume; 5 reset phases -> teardown)");
    return 0;
}
