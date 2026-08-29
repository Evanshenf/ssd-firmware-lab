/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_test_support.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static int run_and_consume(
    struct c34_test_environment *environment,
    const struct c34_request *request,
    struct c34_command_result *result
)
{
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;

    CHECK(c34_test_submit(environment, request, &command, &lease, &intent,
                          result));
    CHECK(intent.result == FWLAB_C31_COMPLETION_SUCCESS);
    CHECK(c34_test_consume(environment, &command, &lease));
    return 1;
}

static int test_durable_write_read_trim(void)
{
    struct c34_test_environment environment;
    struct c34_request write = c34_test_write(
        1, FWLAB_PERSIST_DEFAULT, 1, 0x31, 0);
    struct c34_request read = c34_test_read(0);
    struct c34_request trim = c34_test_trim(
        1, FWLAB_PERSIST_SELF_DURABLE, 2);
    struct c34_command_result result;
    struct c34_logical_entry entry;

    CHECK(c34_test_init(&environment, 0, UINT64_C(0x3401),
                        UINT64_C(0x9b6d3e7a4c2158f1)));
    CHECK(run_and_consume(&environment, &write, &result));
    CHECK(result.witness.witness_class ==
          FWLAB_PERSIST_DURABLE_ELIGIBLE);
    CHECK(c34_logical_state(environment.c34, 0, &entry) == C34_OK &&
          entry.kind == C34_LOGICAL_VALUE && entry.version == 1);
    CHECK(run_and_consume(&environment, &read, &result));
    CHECK(result.present_mask == 1 &&
          result.payload[0][0] == 0x31 && result.payload[0][15] == 0x31);
    CHECK(run_and_consume(&environment, &trim, &result));
    CHECK(c34_logical_state(environment.c34, 0, &entry) == C34_OK &&
          entry.kind == C34_LOGICAL_TOMBSTONE && entry.version == 2);
    read = c34_test_read(0);
    CHECK(run_and_consume(&environment, &read, &result));
    CHECK(result.present_mask == 0);
    return 1;
}

static int test_volatile_then_drain(void)
{
    struct c34_test_environment environment;
    struct c34_request write = c34_test_write(
        1, FWLAB_PERSIST_DEFAULT, 1, 0x42, 0);
    struct c34_command_result result;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;
    struct c34_logical_entry entry;

    CHECK(c34_test_init(&environment, 1, UINT64_C(0x3402),
                        UINT64_C(0x9b6d3e7a4c2158f1)));
    CHECK(c34_test_submit(&environment, &write, &command, &lease, &intent,
                          &result));
    CHECK(result.witness.witness_class ==
          FWLAB_PERSIST_VOLATILE_ELIGIBLE);
    CHECK(c34_logical_state(environment.c34, 0, &entry) == C34_OK &&
          entry.kind == C34_LOGICAL_NONE);
    CHECK(c34_test_consume(&environment, &command, &lease));
    CHECK(c34_test_pump_quiescent(&environment, 4096));
    CHECK(c34_logical_state(environment.c34, 0, &entry) == C34_OK &&
          entry.kind == C34_LOGICAL_VALUE && entry.version == 1);
    CHECK(c34_recover(environment.c34) == C34_OK);
    CHECK(c34_logical_state(environment.c34, 0, &entry) == C34_OK &&
          entry.kind == C34_LOGICAL_VALUE);
    return 1;
}

static int test_checkpoint(void)
{
    struct c34_test_environment environment;
    struct c34_request write = c34_test_write(
        3, FWLAB_PERSIST_SELF_DURABLE, 1, 0x51, 0x52);
    struct c34_command_result result;
    struct c34_logical_entry before[C34_ATOMS];
    struct c34_logical_entry after[C34_ATOMS];

    CHECK(c34_test_init(&environment, 0, UINT64_C(0x3403),
                        UINT64_C(0x9b6d3e7a4c2158f1)));
    CHECK(run_and_consume(&environment, &write, &result));
    CHECK(c34_logical_state(environment.c34, 0, &before[0]) == C34_OK &&
          c34_logical_state(environment.c34, 1, &before[1]) == C34_OK);
    CHECK(c34_checkpoint_start(environment.c34) == C34_OK);
    CHECK(c34_test_pump_quiescent(&environment, 4096));
    CHECK(c34_recover(environment.c34) == C34_OK);
    CHECK(c34_logical_state(environment.c34, 0, &after[0]) == C34_OK &&
          c34_logical_state(environment.c34, 1, &after[1]) == C34_OK);
    CHECK(after[0].kind == C34_LOGICAL_VALUE &&
          after[1].kind == C34_LOGICAL_VALUE &&
          after[0].value_crc32c == before[0].value_crc32c &&
          after[1].value_crc32c == before[1].value_crc32c);
    return 1;
}

static int write_one(
    struct c34_test_environment *environment,
    uint8_t atom,
    uint32_t sequence,
    uint8_t fill
)
{
    struct c34_request write = c34_test_write(
        (uint8_t)(1u << atom), FWLAB_PERSIST_SELF_DURABLE, sequence,
        atom == 0 ? fill : 0, atom == 1 ? fill : 0);
    struct c34_command_result result;

    return run_and_consume(environment, &write, &result);
}

static int test_bounded_relocation(void)
{
    struct c34_test_environment environment;
    struct c34_logical_entry before;
    struct c34_logical_entry after;

    CHECK(c34_test_init(&environment, 0, UINT64_C(0x3404),
                        UINT64_C(0x9b6d3e7a4c2158f1)));
    CHECK(write_one(&environment, 0, 1, 0x61));
    CHECK(write_one(&environment, 1, 2, 0x62));
    CHECK(write_one(&environment, 0, 3, 0x63));
    CHECK(write_one(&environment, 0, 4, 0x64));
    CHECK(c34_checkpoint_start(environment.c34) == C34_OK);
    CHECK(c34_test_pump_quiescent(&environment, 4096));
    CHECK(write_one(&environment, 1, 5, 0x65));
    CHECK(c34_logical_state(environment.c34, 0, &before) == C34_OK &&
          before.kind == C34_LOGICAL_VALUE && before.version == 3 &&
          before.copy_sequence == 0 && before.data_ppa.block == 0);
    CHECK(c34_relocation_start(environment.c34) == C34_OK);
    CHECK(c34_test_pump_quiescent(&environment, 4096));
    CHECK(c34_logical_state(environment.c34, 0, &after) == C34_OK &&
          after.kind == C34_LOGICAL_VALUE && after.version == 3 &&
          after.copy_sequence == 1 && after.data_ppa.block == 2 &&
          after.value_crc32c == before.value_crc32c);
    CHECK(environment.media.block[0].erase_generation == 1);
    CHECK(c34_recover(environment.c34) == C34_OK);
    CHECK(c34_logical_state(environment.c34, 0, &after) == C34_OK &&
          after.copy_sequence == 1 && after.data_ppa.block == 2);
    return 1;
}

static int test_reset_discards_overlay(void)
{
    struct c34_test_environment environment;
    struct c34_request write = c34_test_write(
        1, FWLAB_PERSIST_DEFAULT, 1, 0x71, 0);
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;
    struct c34_command_result result;
    struct c34_logical_entry entry;
    unsigned int iteration;

    CHECK(c34_test_init(&environment, 1, UINT64_C(0x3405),
                        UINT64_C(0x9b6d3e7a4c2158f1)));
    CHECK(c34_test_submit(&environment, &write, &command, &lease, &intent,
                          &result));
    CHECK(result.witness.witness_class ==
          FWLAB_PERSIST_VOLATILE_ELIGIBLE);
    CHECK(c34_test_consume(&environment, &command, &lease));
    CHECK(fwlab_c31_reset_begin(environment.c31) == FWLAB_C31_API_OK);
    for (iteration = 0; iteration < 4096; ++iteration) {
        struct fwlab_c31_step_result step;

        if (fwlab_c31_phase(environment.c31) ==
            FWLAB_C31_INSTANCE_RESET_ACK) {
            break;
        }
        CHECK(fwlab_c31_step(environment.c31, 1, &step) ==
              FWLAB_C31_API_OK);
    }
    CHECK(iteration < 4096 &&
          fwlab_c31_reset_ack(environment.c31) == FWLAB_C31_API_OK);
    CHECK(c34_recover(environment.c34) == C34_OK);
    CHECK(c34_logical_state(environment.c34, 0, &entry) == C34_OK &&
          entry.kind == C34_LOGICAL_NONE);
    return 1;
}

static int test_fixed_frontier_fence(void)
{
    struct c34_test_environment environment;
    struct c34_request write = c34_test_write(
        1, FWLAB_PERSIST_DEFAULT, 1, 0x81, 0);
    struct c34_request fence = c34_test_fence(1);
    struct fwlab_c31_command_handle write_command;
    struct fwlab_c31_completion_lease write_lease;
    struct fwlab_c31_completion_intent write_intent;
    struct c34_command_result write_result;
    struct c34_command_result fence_result;

    CHECK(c34_test_init(&environment, 1, UINT64_C(0x3406),
                        UINT64_C(0x9b6d3e7a4c2158f1)));
    CHECK(c34_test_submit(
              &environment, &write, &write_command, &write_lease,
              &write_intent, &write_result));
    CHECK(write_result.witness.witness_class ==
          FWLAB_PERSIST_VOLATILE_ELIGIBLE);
    CHECK(fwlab_c31_completion_consume(environment.c31, &write_lease) ==
          FWLAB_C31_API_OK);
    CHECK(c34_test_pump_quiescent(&environment, 4096));
    CHECK(run_and_consume(&environment, &fence, &fence_result));
    CHECK(fence_result.witness.witness_class ==
          FWLAB_PERSIST_DURABLE_ELIGIBLE);
    CHECK(c34_result_ack(environment.c34, &write_command) == C34_OK);
    return 1;
}

static int test_plp_rejected(void)
{
    struct c34_config config;

    memset(&config, 0, sizeof(config));
    config.version = C34_CONTRACT_VERSION;
    config.size = sizeof(config);
    config.instance_nonce = 1;
    config.controller_epoch = 1;
    config.controller_region = 1;
    config.controller_buffer_length = C34_MAIN_BYTES + C34_OOB_BYTES;
    config.persistence.version = FWLAB_PERSIST_VERSION;
    config.persistence.size = sizeof(config.persistence);
    config.persistence.cache_enabled = 1;
    config.persistence.plp_kind = FWLAB_PERSIST_PLP_VALIDATED;
    config.persistence.plp_capacity_credits = 1;
    config.persistence.survival_event_mask =
        FWLAB_PERSIST_EVENT_CONTROLLER_RESET |
        FWLAB_PERSIST_EVENT_POWER_LOSS;
    config.inner_uid_limit = 32;
    config.physical_op_limit = 16;
    config.physical_sequence_limit = 16;
    CHECK(fwlab_persist_profile_validate(&config.persistence) ==
          FWLAB_PERSIST_OK);
    CHECK(c34_arena_size(&config) == 0);
    return 1;
}

int main(void)
{
    CHECK(test_durable_write_read_trim());
    CHECK(test_volatile_then_drain());
    CHECK(test_checkpoint());
    CHECK(test_bounded_relocation());
    CHECK(test_reset_discards_overlay());
    CHECK(test_fixed_frontier_fence());
    CHECK(test_plp_rejected());
    puts("C3.4 coordinator flows: PASS (7 cases)");
    return 0;
}
