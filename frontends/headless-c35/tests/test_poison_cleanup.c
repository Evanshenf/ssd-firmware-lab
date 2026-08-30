/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static int child_fixture(
    struct c35_storage **storage_out,
    struct c35_runtime **runtime_out,
    enum c35_lane lane,
    uint64_t nonce
)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    uint8_t uuid[16] = {
        0x35, 0xc3, 0x5a, 0xa5, 0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88, 0x04, 0x03, 0x02, 0x01
    };

    if (storage == NULL || runtime == NULL ||
        !c35_storage_init(storage, lane, uuid) ||
        !c35_runtime_init(
            runtime, storage, lane, nonce,
            UINT64_C(0x1029384756abcdef), 0, 0, 0x35a60001))
        return 0;
    *storage_out = storage;
    *runtime_out = runtime;
    return 1;
}

static void child_phase2(void)
{
    struct c35_storage *storage;
    struct c35_runtime *runtime;
    struct c35_operation_token token;
    struct c35_operation_status status;
    unsigned int iteration;

    if (!child_fixture(
            &storage, &runtime, C35_LANE_POSIX,
            UINT64_C(0x35a6000000000001))) _exit(11);
    runtime->firmware->phase = 2;
    if (c35_teardown_start(&runtime->headless, &token) != C35_OK) _exit(12);
    for (iteration = 0; iteration < 128; ++iteration) {
        enum c35_result result = c35_operation_progress(
            &runtime->headless, &token, 1, &status);

        if (result == C35_OK) break;
        if (result != C35_IN_PROGRESS) _exit(13);
    }
    if (iteration == 128 || status.outcome != C35_POISONED ||
        status.cleanup_state != C35_CLEANUP_POISONED ||
        status.cause_domain != C35_CAUSE_C34 ||
        status.cause_code != C34_WRONG_STATE ||
        runtime->headless.service_phase != C35_SERVICE_POISONED ||
        !runtime->claimed || !storage->bundle.claimed || storage->fd < 0 ||
        fwlab_c31_phase(runtime->lifecycle) != FWLAB_C31_INSTANCE_FAULTED)
        _exit(14);
    /* Deliberately retain the claim/fd/arena until process reap. */
    _exit(0);
}

static enum c35_result always_nonquiescent(
    void *context,
    bool *quiescent
)
{
    (void)context;
    if (quiescent == NULL) return C35_INVALID;
    *quiescent = false;
    return C35_OK;
}

static void child_nonquiescent(void)
{
    struct c35_storage *storage;
    struct c35_runtime *runtime;
    struct c35_publication publication;
    struct c35_binding_ops binding_ops;

    if (!child_fixture(
            &storage, &runtime, C35_LANE_MEMORY,
            UINT64_C(0x35a6000000000002))) _exit(21);
    binding_ops = *runtime->headless.binding.ops;
    binding_ops.quiescent = always_nonquiescent;
    runtime->headless.binding.ops = &binding_ops;
    if (c35_headless_teardown_observed(
            &runtime->headless, 64, &publication) != C35_IN_PROGRESS)
        _exit(22);
    if (runtime->headless.service_phase != C35_SERVICE_TEARING_DOWN)
        _exit(23);
    if (!runtime->claimed || !storage->bundle.claimed) _exit(24);
    /* false is retryable: no timeout-based force release or fake poison. */
    _exit(0);
}

static int run_child(void (*entry)(void))
{
    pid_t child = fork();
    int status;

    CHECK(child >= 0);
    if (child == 0) entry();
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 1;
}

int main(void)
{
    CHECK(run_child(child_phase2));
    CHECK(run_child(child_nonquiescent));
    puts("C3.5a poison cleanup: PASS (phase2 explicit POISONED; "
         "quiescent=false remains claimed/IN_PROGRESS in reaped children)");
    return 0;
}
