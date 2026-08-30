/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_test_support.h"

#include <inttypes.h>
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

static void make_uuid(uint8_t uuid[16], enum c35_lane lane, uint32_t cut)
{
    unsigned int index;

    for (index = 0; index < 16; ++index) {
        uuid[index] = (uint8_t)(0x35u + index * 7u + (unsigned int)lane);
    }
    uuid[12] ^= (uint8_t)cut;
    uuid[13] ^= (uint8_t)(cut >> 8);
    uuid[14] ^= (uint8_t)(cut >> 16);
    uuid[15] ^= (uint8_t)(cut >> 24);
}

static struct c35_txid test_txid(
    const struct c35_runtime *runtime,
    uint64_t uid
)
{
    struct c35_txid txid;

    txid.instance_nonce = runtime->nonce;
    txid.uid = uid;
    txid.owner_epoch = runtime->headless.owner_epoch;
    txid.generation = (uint32_t)uid;
    return txid;
}

static int step_until_ready(
    struct c35_runtime *runtime,
    const struct fwlab_c31_command_handle *command,
    uint32_t *steps
)
{
    uint32_t count;

    for (count = 0; count <= 512; ++count) {
        enum fwlab_c31_lifecycle_state state;
        struct fwlab_c31_step_result step;

        if (fwlab_c31_command_state(
                runtime->lifecycle, command, &state) != FWLAB_C31_API_OK) {
            return 0;
        }
        if (state == FWLAB_C31_CMD_COMPLETION_READY) {
            *steps = count;
            return 1;
        }
        if (count == 512 ||
            runtime->lifecycle_port.ops->step(
                runtime->lifecycle_port.context, 1, &step) !=
                FWLAB_C31_API_OK) {
            return 0;
        }
    }
    return 0;
}

static int setup_pending(
    struct c35_storage *storage,
    struct c35_runtime *runtime,
    enum c35_lane lane,
    uint32_t cut,
    uint8_t durability,
    struct c35_submission *submission,
    const uint8_t old_value[C35_ATOM_BYTES],
    const uint8_t new_value[C35_ATOM_BYTES]
)
{
    uint8_t uuid[16];
    struct c35_request request;
    struct c35_semantic_result result;

    make_uuid(uuid, lane, cut);
    if (!c35_storage_init(storage, lane, uuid) ||
        !c35_runtime_init(
            runtime, storage, lane,
            UINT64_C(0x3500000000000000) |
                ((uint64_t)lane << 32) | cut | 1u,
            UINT64_C(0x9b6d3e7a4c2158f1) ^ cut, 1, 0, 0x3508u)) {
        return 0;
    }
    request = c35_request_write(
        0, FWLAB_PERSIST_SELF_DURABLE, 1, old_value);
    if (!c35_run_command(runtime, &request, &result)) {
        return 0;
    }
    request = c35_request_write(0, durability, 2, new_value);
    return c35_headless_submit_observed(
               &runtime->headless, &request, submission) == C35_OK;
}

static int cleanup_case(
    struct c35_runtime *runtime,
    struct c35_storage *storage
)
{
    return c35_runtime_teardown(runtime) && c35_storage_close(storage);
}

static int measure_depth(enum c35_lane lane, uint32_t *depth)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct c35_submission submission;
    const uint8_t old_value[C35_ATOM_BYTES] = {
        0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1,
        0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1,
    };
    const uint8_t new_value[C35_ATOM_BYTES] = {
        0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2,
        0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2,
    };
    int ok = 0;

    if (storage != NULL && runtime != NULL &&
        setup_pending(storage, runtime, lane, 0,
                       FWLAB_PERSIST_SELF_DURABLE, &submission,
                       old_value, new_value) &&
        step_until_ready(runtime, &submission.command, depth) &&
        *depth <= 512 &&
        c35_headless_reset(&runtime->headless, 8192) == C35_OK &&
        cleanup_case(runtime, storage)) {
        ok = 1;
    }
    free(runtime);
    free(storage);
    return ok;
}

static int run_reset_cut(
    enum c35_lane lane,
    uint32_t cut,
    uint32_t *old_outcome,
    uint32_t *new_outcome
)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct c35_submission submission;
    struct c35_request read;
    struct c35_semantic_result result;
    struct fwlab_c31_completion_intent intent;
    struct c35_txid stale_txid;
    enum fwlab_c31_lifecycle_state state;
    const uint8_t old_value[C35_ATOM_BYTES] = {
        0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1,
        0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1,
    };
    const uint8_t new_value[C35_ATOM_BYTES] = {
        0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2,
        0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2,
    };
    uint32_t index;
    int ok = 0;

    if (storage == NULL || runtime == NULL ||
        !setup_pending(storage, runtime, lane, cut + 1u,
                        FWLAB_PERSIST_SELF_DURABLE, &submission,
                        old_value, new_value)) {
        goto out;
    }
    for (index = 0; index < cut; ++index) {
        struct fwlab_c31_step_result step;

        if (fwlab_c31_command_state(
                runtime->lifecycle, &submission.command, &state) !=
                FWLAB_C31_API_OK ||
            state == FWLAB_C31_CMD_COMPLETION_READY ||
            runtime->lifecycle_port.ops->step(
                runtime->lifecycle_port.context, 1, &step) !=
                FWLAB_C31_API_OK) {
            goto out;
        }
    }
    stale_txid = test_txid(runtime, UINT64_C(0x7000) + cut);
    if (c35_headless_reset(&runtime->headless, 8192) != C35_OK ||
        fwlab_c31_command_state(
            runtime->lifecycle, &submission.command, &state) !=
            FWLAB_C31_API_STALE_TOKEN ||
        runtime->headless.binding.ops->result_prepare(
            runtime->headless.binding.context, &stale_txid,
            &submission.command, &intent, &result) == C35_OK) {
        goto out;
    }
    read = c35_request_read(0);
    if (!c35_run_command(runtime, &read, &result) ||
        result.present_mask != 1u) {
        goto out;
    }
    if (memcmp(result.payload[0], old_value, C35_ATOM_BYTES) == 0) {
        ++*old_outcome;
    } else if (memcmp(result.payload[0], new_value, C35_ATOM_BYTES) == 0) {
        ++*new_outcome;
    } else {
        goto out;
    }
    ok = cleanup_case(runtime, storage);
out:
    free(runtime);
    free(storage);
    return ok;
}

static int run_lease_boundaries(enum c35_lane lane)
{
    unsigned int boundary;

    for (boundary = 0; boundary < 2; ++boundary) {
        struct c35_storage *storage = calloc(1, sizeof(*storage));
        struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
        struct c35_submission submission;
        struct fwlab_c31_completion_lease lease;
        struct fwlab_c31_completion_intent intent;
        struct c35_semantic_result semantic;
        struct c35_txid result_txid;
        enum c35_result_state result_state;
        enum fwlab_c31_lifecycle_state state;
        uint32_t depth;
        const uint8_t old_value[C35_ATOM_BYTES] = {
            0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1,
            0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1, 0xa1,
        };
        const uint8_t new_value[C35_ATOM_BYTES] = {
            0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2,
            0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2, 0xb2,
        };
        int ok;

        CHECK(storage != NULL && runtime != NULL);
        CHECK(setup_pending(
            storage, runtime, lane, 0x100u + boundary,
            FWLAB_PERSIST_DEFAULT, &submission,
            old_value, new_value));
        CHECK(step_until_ready(runtime, &submission.command, &depth));
        result_txid = test_txid(
            runtime, UINT64_C(0x7100) + boundary);
        CHECK(runtime->lifecycle_port.ops->completion_acquire(
            runtime->lifecycle_port.context, &submission.command,
            &lease, &intent) ==
            FWLAB_C31_API_OK);
        CHECK(runtime->headless.binding.ops->result_prepare(
            runtime->headless.binding.context, &result_txid,
            &submission.command, &intent, &semantic) == C35_OK);
        if (boundary == 1) {
            CHECK(runtime->lifecycle_port.ops->completion_consume(
                runtime->lifecycle_port.context, &lease) ==
                  FWLAB_C31_API_OK);
        }
        CHECK(c35_headless_reset(&runtime->headless, 8192) == C35_OK);
        CHECK(runtime->lifecycle_port.ops->completion_consume(
            runtime->lifecycle_port.context, &lease) != FWLAB_C31_API_OK);
        CHECK(runtime->lifecycle_port.ops->completion_release(
            runtime->lifecycle_port.context, &lease) != FWLAB_C31_API_OK);
        CHECK(fwlab_c31_command_state(
            runtime->lifecycle, &submission.command, &state) ==
            FWLAB_C31_API_STALE_TOKEN);
        CHECK(runtime->headless.binding.ops->result_query(
            runtime->headless.binding.context, &result_txid,
            &result_state) == C35_OK);
        CHECK(result_state == C35_RESULT_CLEARED_BY_RESET ||
              result_state == C35_RESULT_ACKED);
        ok = cleanup_case(runtime, storage);
        free(runtime);
        free(storage);
        CHECK(ok);
    }
    return 1;
}

static int test_identity_matrix(void)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct c35_submission first;
    struct c35_submission second;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_completion_lease lease1;
    struct fwlab_c31_completion_lease lease2;
    struct fwlab_c31_completion_intent intent;
    struct c35_semantic_result result;
    enum fwlab_c31_lifecycle_state state;
    struct fwlab_c31_command_handle mutated;
    struct c35_txid result_txid;
    uint8_t uuid[16];
    uint32_t depth;
    unsigned int mutation;
    int ok;

    CHECK(storage != NULL && runtime != NULL);
    make_uuid(uuid, C35_LANE_MEMORY, 0x220u);
    CHECK(c35_storage_init(storage, C35_LANE_MEMORY, uuid));
    CHECK(c35_runtime_init(
        runtime, storage, C35_LANE_MEMORY, UINT64_C(0x351d0001),
        UINT64_C(0x1020304050607080), 0, 0, 0x3510u));
    CHECK(c35_headless_submit_observed(
        &runtime->headless, &request, &first) == C35_OK);
    CHECK(step_until_ready(runtime, &first.command, &depth));
    result_txid = test_txid(runtime, UINT64_C(0x7200));
    CHECK(runtime->lifecycle_port.ops->completion_acquire(
        runtime->lifecycle_port.context, &first.command, &lease1, &intent) ==
        FWLAB_C31_API_OK);
    CHECK(runtime->lifecycle_port.ops->completion_release(
        runtime->lifecycle_port.context, &lease1) == FWLAB_C31_API_OK);
    CHECK(runtime->lifecycle_port.ops->completion_consume(
        runtime->lifecycle_port.context, &lease1) != FWLAB_C31_API_OK);
    CHECK(runtime->lifecycle_port.ops->completion_acquire(
        runtime->lifecycle_port.context, &first.command, &lease2, &intent) ==
        FWLAB_C31_API_OK);
    CHECK(lease2.lease_generation != lease1.lease_generation);
    CHECK(runtime->lifecycle_port.ops->completion_release(
        runtime->lifecycle_port.context, &lease1) != FWLAB_C31_API_OK);

    for (mutation = 0; mutation < 5; ++mutation) {
        mutated = first.command;
        if (mutation == 0) {
            ++mutated.instance_nonce;
        } else if (mutation == 1) {
            ++mutated.command_uid;
        } else if (mutation == 2) {
            ++mutated.controller_epoch;
        } else if (mutation == 3) {
            mutated.slot ^= 1u;
        } else {
            ++mutated.slot_generation;
        }
        CHECK(fwlab_c31_command_state(
            runtime->lifecycle, &mutated, &state) != FWLAB_C31_API_OK);
        CHECK(runtime->headless.binding.ops->result_prepare(
            runtime->headless.binding.context, &result_txid, &mutated,
            &intent, &result) != C35_OK);
    }
    CHECK(runtime->headless.binding.ops->result_prepare(
        runtime->headless.binding.context, &result_txid, &first.command,
        &intent, &result) == C35_OK);
    CHECK(runtime->lifecycle_port.ops->completion_consume(
        runtime->lifecycle_port.context, &lease2) == FWLAB_C31_API_OK);
    CHECK(runtime->headless.binding.ops->result_ack(
        runtime->headless.binding.context, &result_txid,
        &first.command) == C35_OK);
    CHECK(runtime->headless.binding.ops->transaction_retire(
        runtime->headless.binding.context, &result_txid) == C35_OK);
    CHECK(runtime->lifecycle_port.ops->completion_release(
        runtime->lifecycle_port.context, &lease2) != FWLAB_C31_API_OK);
    CHECK(fwlab_c31_command_state(
        runtime->lifecycle, &first.command, &state) ==
        FWLAB_C31_API_STALE_TOKEN);

    CHECK(c35_headless_submit_observed(
        &runtime->headless, &request, &second) == C35_OK);
    CHECK(second.command.slot == first.command.slot);
    CHECK(second.command.slot_generation != first.command.slot_generation);
    CHECK(c35_headless_complete(
        &runtime->headless, &second.command, &result, &intent) == C35_OK);
    CHECK(c35_headless_reset(&runtime->headless, 8192) == C35_OK);
    CHECK(fwlab_c31_command_state(
        runtime->lifecycle, &second.command, &state) ==
        FWLAB_C31_API_STALE_TOKEN);
    ok = cleanup_case(runtime, storage);
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

int main(void)
{
    uint32_t depth[2];
    uint32_t old_outcome[2] = {0, 0};
    uint32_t new_outcome[2] = {0, 0};
    unsigned int lane_index;
    uint32_t executions = 0;

    for (lane_index = 0; lane_index < 2; ++lane_index) {
        enum c35_lane lane = lane_index == 0 ?
            C35_LANE_MEMORY : C35_LANE_BYTE;
        uint32_t cut;

        CHECK(measure_depth(lane, &depth[lane_index]));
        CHECK(depth[lane_index] > 0 && depth[lane_index] <= 512);
        for (cut = 0; cut <= depth[lane_index]; ++cut) {
            if (!run_reset_cut(
                    lane, cut, &old_outcome[lane_index],
                    &new_outcome[lane_index])) {
                fprintf(stderr, "reset cut failed: lane=%u cut=%" PRIu32
                        " depth=%" PRIu32 "\n", (unsigned int)lane,
                        cut, depth[lane_index]);
                return 1;
            }
            ++executions;
        }
        CHECK(run_lease_boundaries(lane));
        executions += 2;
        CHECK(old_outcome[lane_index] != 0);
    }
    CHECK(test_identity_matrix());
    printf("C3.5 reset/identity: PASS (%" PRIu32
           " reset executions, D_M=%" PRIu32 ", D_B=%" PRIu32
           ", outcomes M=%" PRIu32 "/%" PRIu32
           " B=%" PRIu32 "/%" PRIu32 ")\n",
           executions, depth[0], depth[1], old_outcome[0], new_outcome[0],
           old_outcome[1], new_outcome[1]);
    return 0;
}
