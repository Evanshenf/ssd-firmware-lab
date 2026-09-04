/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fakes/spine_fake_adjacent.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STORE_BYTES 262144u
#define MAX_TEST_COMMANDS 40u

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                     \
            fprintf(stderr, "S0-B check failed: %s:%d: %s\n", __FILE__,      \
                    __LINE__, #condition);                                      \
            return 0;                                                          \
        }                                                                      \
    } while (0)

union aligned_store {
    max_align_t alignment;
    unsigned char bytes[STORE_BYTES];
};

struct test_environment {
    union aligned_store lifecycle;
    union aligned_store c43;
    union aligned_store linux_profile;
    union aligned_store tiny;
    struct fwlab_spine_fake_v0 fake;
    struct fwlab_host_action_driver_table_v0 drivers;
    struct fwlab_host_profile_adapter_v0 c43_adapter;
    struct fwlab_host_profile_adapter_v0 linux_adapter;
    struct fwlab_host_profile_adapter_v0 tiny_adapter;
    uint64_t next_command_uid;
};

struct close_receipt {
    struct fwlab_spine_epoch_status_v0 epoch;
    uint32_t fini_calls;
};

static struct test_environment environment;
static const char *lifecycle_object_digest;
static uint32_t rejecting_sink_calls;

static struct fwlab_host_lifecycle_config_v0 lifecycle_config(
    uint16_t capacity,
    uint64_t command_maximum)
{
    struct fwlab_host_lifecycle_config_v0 config;

    memset(&config, 0, sizeof(config));
    config.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    config.size = sizeof(config);
    config.lifecycle_instance_nonce = UINT64_C(0x5100);
    config.execution_epoch = 7;
    config.generation = 3;
    config.command_capacity = capacity;
    config.actions_per_command = FWLAB_HOST_ACTION_V0_MAX_ACTIONS;
    config.command_uid.next = 1;
    config.command_uid.maximum = command_maximum;
    config.action_uid.next = 100;
    config.action_uid.maximum = 10000;
    config.abort_uid.next = 20000;
    config.abort_uid.maximum = 21000;
    config.completion_lease_uid.next = 30000;
    config.completion_lease_uid.maximum = 31000;
    return config;
}

static int environment_init(uint16_t capacity, uint64_t command_maximum)
{
    struct fwlab_host_lifecycle_config_v0 config =
        lifecycle_config(capacity, command_maximum);

    memset(&environment, 0, sizeof(environment));
    CHECK(fwlab_spine_lifecycle_v0_arena_size() <=
          sizeof(environment.lifecycle.bytes));
    CHECK(fwlab_c43_p1_adapter_v0_arena_size() <=
          sizeof(environment.c43.bytes));
    CHECK(fwlab_linux_profile_v1_adapter_arena_size() <=
          sizeof(environment.linux_profile.bytes));
    CHECK(fwlab_tiny_profile_v0_arena_size() <=
          sizeof(environment.tiny.bytes));
    CHECK(((uintptr_t)environment.lifecycle.bytes %
           fwlab_spine_lifecycle_v0_arena_alignment()) == 0);
    CHECK(((uintptr_t)environment.c43.bytes %
           fwlab_c43_p1_adapter_v0_arena_alignment()) == 0);
    CHECK(((uintptr_t)environment.linux_profile.bytes %
           fwlab_linux_profile_v1_adapter_arena_alignment()) == 0);
    CHECK(((uintptr_t)environment.tiny.bytes %
           fwlab_tiny_profile_v0_arena_alignment()) == 0);
    CHECK(fwlab_c43_p1_adapter_v0_init(
              environment.c43.bytes, fwlab_c43_p1_adapter_v0_arena_size(),
              UINT64_C(0xc430), 11, &environment.c43_adapter) ==
          FWLAB_SPINE_V0_OK);
    CHECK(fwlab_linux_profile_v1_adapter_init(
              environment.linux_profile.bytes,
              fwlab_linux_profile_v1_adapter_arena_size(),
              UINT64_C(0x7100), 12,
              &environment.linux_adapter) == FWLAB_SPINE_V0_OK);
    CHECK(fwlab_tiny_profile_v0_init(
              environment.tiny.bytes, fwlab_tiny_profile_v0_arena_size(),
              UINT64_C(0x7700), 13, &environment.tiny_adapter) ==
          FWLAB_SPINE_V0_OK);
    fwlab_spine_fake_v0_init(&environment.fake, &environment.drivers);
    CHECK(fwlab_spine_lifecycle_v0_init(
              environment.lifecycle.bytes,
              fwlab_spine_lifecycle_v0_arena_size(), &config,
              &environment.drivers) == FWLAB_SPINE_V0_OK);
    environment.next_command_uid = 1;
    return 1;
}

static struct fwlab_nvme_command command_base(
    uint8_t queue_class,
    uint8_t opcode,
    uint32_t namespace_id,
    uint8_t data_present)
{
    struct fwlab_nvme_command command;
    const uint64_t uid = environment.next_command_uid++;

    memset(&command, 0, sizeof(command));
    command.version = FWLAB_NVME_COMMAND_VERSION;
    command.size = sizeof(command);
    command.handle.instance_nonce = UINT64_C(0x1000);
    command.handle.command_uid = uid;
    command.handle.controller_epoch = 5;
    command.handle.generation = 2;
    command.origin.word[0] = UINT64_C(0xa000) + uid;
    command.origin.word[1] = UINT64_C(0xb000) + uid;
    command.trace_cookie = UINT64_C(0xc000) + uid;
    command.safety_generation = 9;
    command.namespace_id = namespace_id;
    command.opcode = opcode;
    command.queue_class = queue_class;
    command.fuse = FWLAB_NVME_FUSE_NONE;
    command.data_pointer_format = FWLAB_NVME_DATA_POINTER_PRP;
    command.data_address_present = data_present;
    return command;
}

static int program_shape(
    const struct fwlab_host_action_program_v0 *program,
    uint16_t count,
    uint16_t first,
    uint16_t second)
{
    if (program->action_count != count) {
        return 0;
    }
    if (count > 0 && program->action[0].kind != first) {
        return 0;
    }
    return count < 2 || program->action[1].kind == second;
}

static const struct fwlab_spine_fake_action_v0 *fake_action_for(
    const struct fwlab_nvme_command *command,
    uint16_t kind)
{
    uint32_t index;

    for (index = 0; index < FWLAB_SPINE_FAKE_V0_MAX_ACTIONS; ++index) {
        const struct fwlab_spine_fake_action_v0 *action =
            &environment.fake.action[index];

        if (action->occupied &&
            action->token.command.command_uid == command->handle.command_uid &&
            action->token.kind == kind) {
            return action;
        }
    }
    return NULL;
}

static void expected_put_u16(uint8_t *output, size_t offset, uint16_t value)
{
    output[offset] = (uint8_t)value;
    output[offset + 1] = (uint8_t)(value >> 8);
}

static void expected_put_u32(uint8_t *output, size_t offset, uint32_t value)
{
    output[offset] = (uint8_t)value;
    output[offset + 1] = (uint8_t)(value >> 8);
    output[offset + 2] = (uint8_t)(value >> 16);
    output[offset + 3] = (uint8_t)(value >> 24);
}

static void expected_put_u64(uint8_t *output, size_t offset, uint64_t value)
{
    uint32_t index;

    for (index = 0; index < 8; ++index) {
        output[offset + index] = (uint8_t)(value >> (index * 8));
    }
}

static void expected_space_padded(
    uint8_t *output,
    size_t offset,
    size_t width,
    const char *value,
    size_t value_size)
{
    memset(output + offset, ' ', width);
    memcpy(output + offset, value, value_size);
}

static void expected_c43_controller(uint8_t *output)
{
    static const char serial[] = "FWLABC43P1-000000001";
    static const char model[] = "SSD Firmware Lab C43-P1";
    static const char firmware[] = "C43P1";

    memset(output, 0, 4096);
    expected_space_padded(output, 4, 20, serial, sizeof(serial) - 1);
    expected_space_padded(output, 24, 40, model, sizeof(model) - 1);
    expected_space_padded(output, 64, 8, firmware, sizeof(firmware) - 1);
    expected_put_u32(output, 516, 1);
    output[525] = 1;
}

static void expected_linux_controller(uint8_t *output)
{
    static const char serial[] = "FWLABLINUXV1-0000001";
    static const char model[] = "SSD Firmware Lab Linux-profile-v1";
    static const char firmware[] = "LNXV1";

    memset(output, 0, 4096);
    expected_put_u16(output, 0, UINT16_C(0xfffa));
    expected_put_u16(output, 2, UINT16_C(0xfffa));
    expected_space_padded(output, 4, 20, serial, sizeof(serial) - 1);
    expected_space_padded(output, 24, 40, model, sizeof(model) - 1);
    expected_space_padded(output, 64, 8, firmware, sizeof(firmware) - 1);
    output[77] = 1;
    expected_put_u16(output, 78, 1);
    expected_put_u32(output, 80, UINT32_C(0x00010000));
    output[512] = UINT8_C(0x66);
    output[513] = UINT8_C(0x44);
    expected_put_u32(output, 516, 1);
}

static void expected_namespace(uint8_t *output, uint64_t lba_count)
{
    memset(output, 0, 4096);
    expected_put_u64(output, 0, lba_count);
    expected_put_u64(output, 8, lba_count);
    expected_put_u64(output, 16, lba_count);
    output[130] = 9;
}

static int admit(
    const struct fwlab_host_profile_adapter_v0 *adapter,
    const struct fwlab_spine_profile_binding_v0 *binding,
    const struct fwlab_nvme_command *command,
    uint32_t role,
    struct fwlab_host_action_program_v0 *program,
    struct fwlab_spine_command_ticket_v0 *ticket)
{
    struct fwlab_spine_command_ticket_v0 repeated;
    struct fwlab_spine_profile_binding_v0 effective = *binding;

    if (role == FWLAB_SPINE_ROLE_V0_ABORT) {
        CHECK(fwlab_spine_fake_v0_attach_relation_source(
                  &environment.fake, &effective) == FWLAB_SPINE_V0_OK);
    }

    CHECK(adapter->ops->plan(adapter->context, command, program) ==
          FWLAB_SPINE_V0_OK);
    CHECK(fwlab_spine_lifecycle_v0_admit_start(
              environment.lifecycle.bytes, &effective, program, role,
              ticket) ==
          FWLAB_SPINE_V0_OK);
    memset(&repeated, 0, sizeof(repeated));
    CHECK(fwlab_spine_lifecycle_v0_admit_start(
              environment.lifecycle.bytes, &effective, program, role,
              &repeated) == FWLAB_SPINE_V0_OK);
    CHECK(fwlab_spine_command_ticket_v0_equal(ticket, &repeated));
    memset(&repeated, 0, sizeof(repeated));
    CHECK(fwlab_spine_lifecycle_v0_admit_query(
              environment.lifecycle.bytes, &effective, program, role,
              &repeated) == FWLAB_SPINE_V0_OK);
    CHECK(fwlab_spine_command_ticket_v0_equal(ticket, &repeated));
    CHECK(fwlab_spine_fake_v0_expect_program(
              &environment.fake, &effective, program) ==
          FWLAB_SPINE_V0_OK);
    return 1;
}

static int run_to_intent(
    const struct fwlab_spine_command_ticket_v0 *ticket,
    struct fwlab_nvme_completion_intent *intent)
{
    uint32_t attempt;

    for (attempt = 0; attempt < 20000; ++attempt) {
        uint32_t units = 0;
        uint32_t transitions = 0;
        enum fwlab_spine_result_v0 result =
            fwlab_spine_lifecycle_v0_intent_read(
                environment.lifecycle.bytes, ticket, intent);

        if (result == FWLAB_SPINE_V0_OK) {
            return 1;
        }
        CHECK(result == FWLAB_SPINE_V0_IN_PROGRESS);
        result = fwlab_spine_lifecycle_v0_step(
            environment.lifecycle.bytes, 16, &units, &transitions);
        CHECK(result == FWLAB_SPINE_V0_OK ||
              result == FWLAB_SPINE_V0_IN_PROGRESS);
        CHECK(units != 0 || result == FWLAB_SPINE_V0_IN_PROGRESS);
    }
    return 0;
}

static int run_to_quarantine(
    const struct fwlab_spine_command_ticket_v0 *ticket)
{
    uint32_t attempt;

    for (attempt = 0; attempt < 20000; ++attempt) {
        struct fwlab_nvme_completion_intent intent;
        enum fwlab_spine_result_v0 observed =
            fwlab_spine_lifecycle_v0_intent_read(
                environment.lifecycle.bytes, ticket, &intent);
        uint32_t units = 0;
        uint32_t transitions = 0;

        if (observed == FWLAB_SPINE_V0_QUARANTINED) {
            return 1;
        }
        CHECK(observed == FWLAB_SPINE_V0_IN_PROGRESS);
        observed = fwlab_spine_lifecycle_v0_step(
            environment.lifecycle.bytes, 8, &units, &transitions);
        CHECK(observed == FWLAB_SPINE_V0_OK ||
              observed == FWLAB_SPINE_V0_IN_PROGRESS ||
              observed == FWLAB_SPINE_V0_POISONED);
    }
    return 0;
}

static int close_and_fini_receipt(
    uint32_t expected_intents,
    struct close_receipt *receipt)
{
    struct fwlab_spine_epoch_status_v0 status;
    uint32_t attempt;
    int quiescent = 0;

    CHECK(fwlab_spine_lifecycle_v0_epoch_close_start(
              environment.lifecycle.bytes, 7) == FWLAB_SPINE_V0_OK);
    CHECK(fwlab_spine_lifecycle_v0_epoch_close_start(
              environment.lifecycle.bytes, 7) == FWLAB_SPINE_V0_OK);
    for (attempt = 0; attempt < 20000; ++attempt) {
        uint32_t units = 0;
        uint32_t transitions = 0;
        enum fwlab_spine_result_v0 result;

        memset(&status, 0, sizeof(status));
        CHECK(fwlab_spine_lifecycle_v0_epoch_query(
                  environment.lifecycle.bytes, &status) ==
              FWLAB_SPINE_V0_OK);
        if (status.effectful_quiescent) {
            quiescent = 1;
            break;
        }
        result = fwlab_spine_lifecycle_v0_step(
            environment.lifecycle.bytes, 32, &units, &transitions);
        CHECK(result == FWLAB_SPINE_V0_OK ||
              result == FWLAB_SPINE_V0_IN_PROGRESS);
    }
    CHECK(quiescent);
    CHECK(status.active_commands == 0);
    CHECK(status.retained_intents == expected_intents);
    for (attempt = 0; attempt < FWLAB_HOST_ACTION_V0_KIND_COUNT; ++attempt) {
        CHECK(environment.fake.lane[attempt].close_calls != 0);
        CHECK(environment.fake.lane[attempt].close_acked == 1);
        CHECK(environment.fake.lane[attempt].quiescent_calls != 0);
    }
    for (attempt = 0; attempt < 20000; ++attempt) {
        enum fwlab_spine_result_v0 result =
            fwlab_spine_lifecycle_v0_fini(environment.lifecycle.bytes);

        if (result == FWLAB_SPINE_V0_OK) {
            if (receipt != NULL) {
                receipt->epoch = status;
                receipt->fini_calls = attempt + 1;
            }
            return 1;
        }
        CHECK(result == FWLAB_SPINE_V0_IN_PROGRESS);
    }
    return 0;
}

static int close_and_fini(uint32_t expected_intents)
{
    return close_and_fini_receipt(expected_intents, NULL);
}

static uint64_t digest_bytes(uint64_t digest, const void *value, size_t size)
{
    const unsigned char *bytes = value;
    size_t index;

    for (index = 0; index < size; ++index) {
        digest ^= bytes[index];
        digest *= UINT64_C(1099511628211);
    }
    return digest;
}

static uint64_t observed_token_digest(void)
{
    uint64_t digest = UINT64_C(1469598103934665603);
    uint32_t index;

    for (index = 0; index < environment.fake.observed_count; ++index) {
        digest = digest_bytes(digest, &environment.fake.observed[index],
                              sizeof(environment.fake.observed[index]));
    }
    return digest;
}

static uint64_t intent_digest(
    const struct fwlab_nvme_completion_intent *intent,
    uint32_t count)
{
    uint64_t digest = UINT64_C(1469598103934665603);
    uint32_t index;

    for (index = 0; index < count; ++index) {
        digest = digest_bytes(digest, &intent[index], sizeof(intent[index]));
    }
    return digest;
}

static void print_profile_receipt(
    const char *profile,
    const struct fwlab_spine_profile_binding_v0 *binding,
    const struct fwlab_spine_command_ticket_v0 *ticket,
    uint32_t actions,
    uint64_t tokens,
    uint64_t intents,
    const struct close_receipt *close)
{
    printf("S0B_PROFILE_ROW|profile=%s|adapter=%llx:%u|ticket=%llu:%llu|actions=%u|tokens=%016llx|intents=%016llx|epoch=%u|retained=%u|close=%u|fini_calls=%u|owner=%llx|object=%s\n",
           profile,
           (unsigned long long)binding->adapter_instance_nonce,
           binding->generation,
           (unsigned long long)ticket->ticket_uid,
           (unsigned long long)ticket->command.command_uid,
           actions,
           (unsigned long long)tokens,
           (unsigned long long)intents,
           close->epoch.execution_epoch,
           close->epoch.retained_intents,
           close->epoch.effectful_quiescent,
           close->fini_calls,
           (unsigned long long)fwlab_spine_lifecycle_v0_symbol_owner,
           lifecycle_object_digest);
}

static int test_c43_profile(void)
{
    enum { C43_COUNT = 15 };
    struct fwlab_nvme_command command[C43_COUNT];
    struct fwlab_host_action_program_v0 program[C43_COUNT];
    struct fwlab_spine_command_ticket_v0 ticket[C43_COUNT];
    struct fwlab_nvme_completion_intent intent[C43_COUNT];
    struct fwlab_spine_profile_binding_v0 normal;
    struct fwlab_spine_profile_binding_v0 abort_binding;
    struct close_receipt close;
    struct fwlab_spine_fake_behavior_v0 behavior;
    uint8_t payload[4096];
    uint64_t tokens;
    uint64_t intents;
    uint32_t index;

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_NORMAL, &normal) ==
          FWLAB_SPINE_V0_OK);
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_ABORT,
                                  &abort_binding) == FWLAB_SPINE_V0_OK);

    command[0] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x06, 0, 1);
    command[0].command_dword10_15[0] = 1;
    command[1] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x06, 1, 1);
    command[2] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x06, 0, 1);
    command[2].command_dword10_15[0] = 2;
    command[3] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x06, 1, 1);
    command[3].command_dword10_15[0] = 3;
    command[4] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x09, 0, 0);
    command[4].command_dword10_15[0] = 7;
    command[4].command_dword10_15[1] = UINT32_C(0x00030003);
    command[5] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x05, 0, 1);
    command[5].command_dword10_15[0] = UINT32_C(0x00030001);
    command[5].command_dword10_15[1] = UINT32_C(0x00000003);
    command[6] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x01, 0, 1);
    command[6].command_dword10_15[0] = UINT32_C(0x00030001);
    command[6].command_dword10_15[1] = UINT32_C(0x00010001);
    command[7] = command_base(FWLAB_NVME_QUEUE_IO, 0x02, 1, 1);
    command[7].command_dword10_15[0] = 2;
    command[7].command_dword10_15[2] = 1;
    command[8] = command_base(FWLAB_NVME_QUEUE_IO, 0x01, 1, 1);
    command[8].command_dword10_15[0] = 4;
    command[8].command_dword10_15[2] = UINT32_C(0x40000001);
    command[9] = command_base(FWLAB_NVME_QUEUE_IO, 0x00, 1, 0);
    command[10] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x00, 0, 0);
    command[10].command_dword10_15[0] = 1;
    command[11] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x04, 0, 0);
    command[11].command_dword10_15[0] = 1;
    command[12] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
    command[12].command_dword10_15[0] = UINT32_C(0x00550001);
    command[13] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x0c, 0, 0);
    command[14] = command_base(FWLAB_NVME_QUEUE_IO, 0x01, 1, 1);
    command[14].command_dword10_15[2] = UINT32_C(0x40000000);

    for (index = 0; index < C43_COUNT; ++index) {
        const uint32_t role = index == 12 ? FWLAB_SPINE_ROLE_V0_ABORT
                                          : FWLAB_SPINE_ROLE_V0_NORMAL;
        const struct fwlab_spine_profile_binding_v0 *binding =
            index == 12 ? &abort_binding : &normal;

        CHECK(admit(&environment.c43_adapter, binding, &command[index], role,
                    &program[index], &ticket[index]));
    }
    CHECK(program_shape(&program[0], 2, FWLAB_HOST_ACTION_V0_PAYLOAD_FILL,
                        FWLAB_HOST_ACTION_V0_DMA_OUT));
    CHECK(program_shape(&program[4], 1, FWLAB_HOST_ACTION_V0_QUEUE_EFFECT, 0));
    CHECK(program_shape(&program[7], 2, FWLAB_HOST_ACTION_V0_BLOCK_READ,
                        FWLAB_HOST_ACTION_V0_DMA_OUT));
    CHECK(program_shape(&program[8], 2, FWLAB_HOST_ACTION_V0_DMA_IN,
                        FWLAB_HOST_ACTION_V0_BLOCK_WRITE));
    CHECK(program_shape(&program[9], 1, FWLAB_HOST_ACTION_V0_BLOCK_FLUSH, 0));
    CHECK(program_shape(&program[12], 1,
                        FWLAB_HOST_ACTION_V0_TARGET_RESOLVE, 0));
    CHECK(program[13].action_count == 0);
    CHECK(program_shape(&program[14], 2, FWLAB_HOST_ACTION_V0_DMA_IN,
                        FWLAB_HOST_ACTION_V0_BLOCK_WRITE));

    memset(&behavior, 0, sizeof(behavior));
    behavior.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
    behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL;
    behavior.units_completed = 1;
    behavior.terminal_delay = 10000;
    behavior.cancel_delay = 1;
    behavior.retire_delay = 1;
    fwlab_spine_fake_v0_set_next(&environment.fake,
                                 FWLAB_HOST_ACTION_V0_DMA_IN, &behavior);

    memset(&behavior, 0, sizeof(behavior));
    behavior.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
    behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_NONE;
    behavior.abort_candidate_present = 1;
    behavior.abort_uid = ticket[12].relation_uid;
    behavior.abort_report = FWLAB_SPINE_ABORT_REPORT_V0_FOUND;
    behavior.target_present = 1;
    behavior.target = ticket[8];
    fwlab_spine_fake_v0_set_next(&environment.fake,
                                 FWLAB_HOST_ACTION_V0_TARGET_RESOLVE,
                                 &behavior);

    for (index = 0; index < C43_COUNT; ++index) {
        CHECK(run_to_intent(&ticket[index], &intent[index]));
        CHECK(intent[index].handle.command_uid ==
              command[index].handle.command_uid);
    }
    CHECK(intent[8].status_code == 0x07);
    CHECK(intent[12].status_code == 0 && intent[12].result_dword0 == 0);
    CHECK(intent[13].status_code == 0x01 && intent[13].do_not_retry == 1);
    CHECK(intent[14].status_code == 0 && intent[14].actual_length == 512);
    CHECK(intent[0].actual_length == 4096);
    CHECK(intent[7].actual_length == 1024);
    CHECK(fwlab_spine_fake_v0_observed_count(&environment.fake) == 20);
    {
        const struct fwlab_spine_fake_action_v0 *payload_action =
            fake_action_for(&command[0], FWLAB_HOST_ACTION_V0_PAYLOAD_FILL);
        const struct fwlab_spine_fake_action_v0 *namespace_payload =
            fake_action_for(&command[1], FWLAB_HOST_ACTION_V0_PAYLOAD_FILL);
        const struct fwlab_spine_fake_action_v0 *active_payload =
            fake_action_for(&command[2], FWLAB_HOST_ACTION_V0_PAYLOAD_FILL);
        const struct fwlab_spine_fake_action_v0 *descriptor_payload =
            fake_action_for(&command[3], FWLAB_HOST_ACTION_V0_PAYLOAD_FILL);
        const struct fwlab_spine_fake_action_v0 *number_of_queues =
            fake_action_for(&command[4], FWLAB_HOST_ACTION_V0_QUEUE_EFFECT);
        const struct fwlab_spine_fake_action_v0 *create_cq =
            fake_action_for(&command[5], FWLAB_HOST_ACTION_V0_QUEUE_EFFECT);
        const struct fwlab_spine_fake_action_v0 *read =
            fake_action_for(&command[7], FWLAB_HOST_ACTION_V0_BLOCK_READ);
        const struct fwlab_spine_fake_action_v0 *write =
            fake_action_for(&command[14], FWLAB_HOST_ACTION_V0_BLOCK_WRITE);
        const struct fwlab_spine_fake_action_v0 *abort =
            fake_action_for(&command[12],
                            FWLAB_HOST_ACTION_V0_TARGET_RESOLVE);

        CHECK(payload_action != NULL && payload_action->payload_bytes == 4096);
        expected_c43_controller(payload);
        CHECK(memcmp(payload_action->payload, payload, sizeof(payload)) == 0);
        CHECK(namespace_payload != NULL &&
              namespace_payload->payload_bytes == 4096);
        expected_namespace(payload, 8);
        CHECK(memcmp(namespace_payload->payload, payload, sizeof(payload)) ==
              0);
        CHECK(active_payload != NULL && active_payload->payload_bytes == 4096);
        memset(payload, 0, sizeof(payload));
        expected_put_u32(payload, 0, 1);
        CHECK(memcmp(active_payload->payload, payload, sizeof(payload)) == 0);
        CHECK(descriptor_payload != NULL &&
              descriptor_payload->payload_bytes == 4096);
        memset(payload, 0, sizeof(payload));
        CHECK(memcmp(descriptor_payload->payload, payload, sizeof(payload)) ==
              0);
        CHECK(number_of_queues != NULL &&
              number_of_queues->argument_value.semantic ==
                  FWLAB_SPINE_SEMANTIC_V0_SET_NUMBER_OF_QUEUES &&
              number_of_queues->argument_value.requested_cq_count == 4 &&
              number_of_queues->argument_value.requested_sq_count == 4);
        CHECK(create_cq != NULL &&
              create_cq->argument_value.queue_id == 1 &&
              create_cq->argument_value.queue_entries == 4 &&
              create_cq->argument_value.interrupt_vector == 0);
        CHECK(read != NULL && read->argument_value.namespace_id == 1 &&
              read->argument_value.lba == 2 &&
              read->argument_value.lba_count == 2 &&
              read->argument_value.exact_bytes == 1024);
        CHECK(write != NULL && write->argument_value.lba == 0 &&
              write->argument_value.lba_count == 1 &&
              write->argument_value.exact_bytes == 512 &&
              write->argument_value.durability ==
                  FWLAB_SPINE_DURABILITY_V0_SELF);
        CHECK(abort != NULL && abort->argument_value.target_sqid == 1 &&
              abort->argument_value.target_cid == 0x55);
        CHECK(payload_action->result_latched &&
              number_of_queues->result_latched && create_cq->result_latched &&
              read->result_latched && write->result_latched &&
              abort->result_latched);
    }
    CHECK(fwlab_spine_lifecycle_v0_intent_read(
              environment.lifecycle.bytes, &ticket[0], &intent[0]) ==
          FWLAB_SPINE_V0_OK);
    fwlab_c43_p1_retire_delay_v0(&environment.c43_adapter, 1);
    tokens = observed_token_digest();
    intents = intent_digest(intent, C43_COUNT);
    CHECK(close_and_fini_receipt(C43_COUNT, &close));
    print_profile_receipt("C43-P1", &normal, &ticket[0], 20, tokens,
                          intents, &close);
    return 1;
}

static int test_linux_profile(void)
{
    enum { LINUX_COUNT = 15 };
    struct fwlab_nvme_command command[LINUX_COUNT];
    struct fwlab_host_action_program_v0 program[LINUX_COUNT];
    struct fwlab_spine_command_ticket_v0 ticket[LINUX_COUNT];
    struct fwlab_nvme_completion_intent intent[LINUX_COUNT];
    struct fwlab_spine_profile_binding_v0 binding;
    struct close_receipt close;
    struct fwlab_spine_fake_behavior_v0 behavior;
    uint8_t payload[4096];
    uint64_t tokens;
    uint64_t intents;
    uint32_t index;

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_linux_profile_v1_binding_v0(
              &environment.linux_adapter, FWLAB_SPINE_ROLE_V0_NORMAL,
              &binding) == FWLAB_SPINE_V0_OK);

    command[0] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x06, 0, 1);
    command[0].command_dword10_15[0] = 1;
    command[1] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x02, UINT32_MAX, 1);
    command[1].command_dword10_15[0] = UINT32_C(0x007f0002);
    command[2] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x09, 0, 0);
    command[2].command_dword10_15[0] = 7;
    command[2].command_dword10_15[1] = UINT32_C(0x00070007);
    command[3] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x05, 0, 1);
    command[3].command_dword10_15[0] = UINT32_C(0x001f0001);
    command[3].command_dword10_15[1] = UINT32_C(0x00000003);
    command[4] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x01, 0, 1);
    command[4].command_dword10_15[0] = UINT32_C(0x001f0001);
    command[4].command_dword10_15[1] = UINT32_C(0x00010001);
    command[5] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x06, 0, 1);
    command[5].command_dword10_15[0] = 1;
    command[6] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x06, 1, 1);
    command[7] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x06, 1, 1);
    command[8] = command_base(FWLAB_NVME_QUEUE_IO, 0x01, 1, 1);
    command[8].command_dword10_15[0] = 16;
    command[8].command_dword10_15[2] = 15;
    command[9] = command_base(FWLAB_NVME_QUEUE_IO, 0x00, 1, 0);
    command[10] = command_base(FWLAB_NVME_QUEUE_IO, 0x02, 1, 1);
    command[10].command_dword10_15[0] = 16;
    command[10].command_dword10_15[2] = 15;
    command[11] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x00, 0, 0);
    command[11].command_dword10_15[0] = 1;
    command[12] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x04, 0, 0);
    command[12].command_dword10_15[0] = 1;
    command[13] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x0c, 0, 0);
    command[14] = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x06, 0, 1);
    command[14].command_dword10_15[0] = 1;
    command[14].transport_fault = FWLAB_NVME_TRANSPORT_UNSAFE_GRAPH;

    for (index = 0; index < LINUX_COUNT; ++index) {
        CHECK(admit(&environment.linux_adapter, &binding, &command[index],
                    FWLAB_SPINE_ROLE_V0_NORMAL, &program[index],
                    &ticket[index]));
    }
    CHECK(program_shape(&program[0], 2, FWLAB_HOST_ACTION_V0_PAYLOAD_FILL,
                        FWLAB_HOST_ACTION_V0_DMA_OUT));
    CHECK(program_shape(&program[1], 2, FWLAB_HOST_ACTION_V0_PAYLOAD_FILL,
                        FWLAB_HOST_ACTION_V0_DMA_OUT));
    CHECK(program_shape(&program[2], 1, FWLAB_HOST_ACTION_V0_QUEUE_EFFECT, 0));
    CHECK(program_shape(&program[8], 2, FWLAB_HOST_ACTION_V0_DMA_IN,
                        FWLAB_HOST_ACTION_V0_BLOCK_WRITE));
    CHECK(program_shape(&program[10], 2, FWLAB_HOST_ACTION_V0_BLOCK_READ,
                        FWLAB_HOST_ACTION_V0_DMA_OUT));
    CHECK(program[13].action_count == 0 && program[14].action_count == 0);

    memset(&behavior, 0, sizeof(behavior));
    behavior.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
    behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL;
    behavior.units_completed = 1;
    behavior.lose_submit_response = 1;
    behavior.terminal_delay = 1;
    behavior.retire_delay = 1;
    fwlab_spine_fake_v0_set_next(&environment.fake,
                                 FWLAB_HOST_ACTION_V0_BLOCK_READ, &behavior);
    fwlab_spine_fake_v0_set_close_delay(
        &environment.fake, FWLAB_HOST_ACTION_V0_DMA_OUT, 1);

    for (index = 0; index < LINUX_COUNT; ++index) {
        CHECK(run_to_intent(&ticket[index], &intent[index]));
    }
    CHECK(intent[0].actual_length == 4096);
    CHECK(intent[1].actual_length == 512);
    CHECK(intent[2].result_dword0 == 0);
    CHECK(intent[8].actual_length == 8192);
    CHECK(intent[10].actual_length == 8192);
    CHECK(intent[13].status_code == 0x01 && intent[13].do_not_retry == 1);
    CHECK(intent[14].status_code == 0x04 && intent[14].do_not_retry == 1);
    CHECK(fwlab_spine_fake_v0_observed_count(&environment.fake) == 20);
    {
        const struct fwlab_spine_fake_action_v0 *controller =
            fake_action_for(&command[0], FWLAB_HOST_ACTION_V0_PAYLOAD_FILL);
        const struct fwlab_spine_fake_action_v0 *smart =
            fake_action_for(&command[1], FWLAB_HOST_ACTION_V0_PAYLOAD_FILL);
        const struct fwlab_spine_fake_action_v0 *namespace_payload =
            fake_action_for(&command[6], FWLAB_HOST_ACTION_V0_PAYLOAD_FILL);
        const struct fwlab_spine_fake_action_v0 *number_of_queues =
            fake_action_for(&command[2], FWLAB_HOST_ACTION_V0_QUEUE_EFFECT);
        const struct fwlab_spine_fake_action_v0 *create_cq =
            fake_action_for(&command[3], FWLAB_HOST_ACTION_V0_QUEUE_EFFECT);
        const struct fwlab_spine_fake_action_v0 *write =
            fake_action_for(&command[8], FWLAB_HOST_ACTION_V0_BLOCK_WRITE);
        const struct fwlab_spine_fake_action_v0 *read =
            fake_action_for(&command[10], FWLAB_HOST_ACTION_V0_BLOCK_READ);

        CHECK(controller != NULL && controller->payload_bytes == 4096);
        expected_linux_controller(payload);
        CHECK(memcmp(controller->payload, payload, sizeof(payload)) == 0);
        CHECK(smart != NULL && smart->payload_bytes == 512);
        memset(payload, 0, 512);
        expected_put_u16(payload, 1, 300);
        payload[3] = 100;
        payload[4] = 10;
        CHECK(memcmp(smart->payload, payload, 512) == 0);
        CHECK(namespace_payload != NULL &&
              namespace_payload->payload_bytes == 4096);
        expected_namespace(payload, 2048);
        CHECK(memcmp(namespace_payload->payload, payload, sizeof(payload)) ==
              0);
        CHECK(number_of_queues != NULL &&
              number_of_queues->argument_value.requested_cq_count == 8 &&
              number_of_queues->argument_value.requested_sq_count == 8);
        CHECK(create_cq != NULL &&
              create_cq->argument_value.queue_entries == 32 &&
              create_cq->argument_value.queue_id == 1);
        CHECK(write != NULL && write->argument_value.lba == 16 &&
              write->argument_value.lba_count == 16 &&
              write->argument_value.exact_bytes == 8192 &&
              write->argument_value.durability ==
                  FWLAB_SPINE_DURABILITY_V0_SELF);
        CHECK(read != NULL && read->argument_value.lba == 16 &&
              read->argument_value.lba_count == 16 &&
              read->argument_value.exact_bytes == 8192);
        CHECK(controller->result_latched && smart->result_latched &&
              number_of_queues->result_latched && create_cq->result_latched &&
              write->result_latched && read->result_latched);
    }
    fwlab_linux_profile_v1_retire_delay_v0(&environment.linux_adapter, 1);
    tokens = observed_token_digest();
    intents = intent_digest(intent, LINUX_COUNT);
    CHECK(close_and_fini_receipt(LINUX_COUNT, &close));
    print_profile_receipt("Linux-profile-v1", &binding, &ticket[0], 20,
                          tokens, intents, &close);
    return 1;
}

static int test_tiny_profile(void)
{
    struct fwlab_nvme_command command;
    struct fwlab_host_action_program_v0 program;
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_nvme_completion_intent first;
    struct fwlab_nvme_completion_intent second;
    struct fwlab_spine_profile_binding_v0 binding;
    struct close_receipt close;
    uint64_t tokens;
    uint64_t intents;

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_tiny_profile_v0_binding(&environment.tiny_adapter, &binding) ==
          FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_ADMIN, UINT8_C(0xc0), 0, 1);
    CHECK(admit(&environment.tiny_adapter, &binding, &command,
                FWLAB_SPINE_ROLE_V0_NORMAL, &program, &ticket));
    CHECK(program_shape(&program, 2, FWLAB_HOST_ACTION_V0_PAYLOAD_FILL,
                        FWLAB_HOST_ACTION_V0_DMA_OUT));
    CHECK(run_to_intent(&ticket, &first));
    CHECK(first.status_code == 0 && first.actual_length == 16 &&
          first.effect_class == FWLAB_NVME_EFFECT_FULL);
    CHECK(fwlab_spine_lifecycle_v0_intent_read(
              environment.lifecycle.bytes, &ticket, &second) ==
          FWLAB_SPINE_V0_OK);
    CHECK(memcmp(&first, &second, sizeof(first)) == 0);
    CHECK(fwlab_spine_fake_v0_observed_count(&environment.fake) == 2);
    {
        const struct fwlab_spine_fake_action_v0 *payload =
            fake_action_for(&command, FWLAB_HOST_ACTION_V0_PAYLOAD_FILL);
        uint32_t index;

        CHECK(payload != NULL && payload->payload_bytes == 16);
        for (index = 0; index < 16; ++index) {
            CHECK(payload->payload[index] == (uint8_t)(0xa0u + index));
        }
    }
    tokens = observed_token_digest();
    intents = intent_digest(&first, 1);
    CHECK(close_and_fini_receipt(1, &close));
    print_profile_receipt("tiny-HARNESS-PROVISIONAL", &binding, &ticket, 2,
                          tokens, intents, &close);
    return 1;
}

static int test_cross_profile_rollback(void)
{
    struct fwlab_spine_profile_binding_v0 linux_binding;
    struct fwlab_spine_profile_binding_v0 c43_binding;
    struct fwlab_spine_command_ticket_v0 ticket[32];
    struct fwlab_host_action_program_v0 program[32];
    struct fwlab_nvme_completion_intent intent;
    uint32_t index;

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_linux_profile_v1_binding_v0(
              &environment.linux_adapter, FWLAB_SPINE_ROLE_V0_NORMAL,
              &linux_binding) == FWLAB_SPINE_V0_OK);
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_NORMAL, &c43_binding) ==
          FWLAB_SPINE_V0_OK);
    for (index = 0; index < 32; ++index) {
        struct fwlab_nvme_command command =
            command_base(FWLAB_NVME_QUEUE_ADMIN, 0x0c, 0, 0);

        CHECK(admit(&environment.linux_adapter, &linux_binding, &command,
                    FWLAB_SPINE_ROLE_V0_NORMAL, &program[index],
                    &ticket[index]));
    }
    for (index = 0; index < 40; ++index) {
        struct fwlab_nvme_command command =
            command_base(FWLAB_NVME_QUEUE_ADMIN, 0x0c, 0, 0);
        struct fwlab_host_action_program_v0 rejected;
        struct fwlab_spine_command_ticket_v0 rejected_ticket;

        CHECK(environment.c43_adapter.ops->plan(environment.c43_adapter.context,
                                                 &command, &rejected) ==
              FWLAB_SPINE_V0_OK);
        memset(&rejected_ticket, 0, sizeof(rejected_ticket));
        CHECK(fwlab_spine_lifecycle_v0_admit_start(
                  environment.lifecycle.bytes, &c43_binding, &rejected,
                  FWLAB_SPINE_ROLE_V0_NORMAL, &rejected_ticket) ==
              FWLAB_SPINE_V0_NO_CAPACITY);
        CHECK(environment.c43_adapter.ops->retire(
                  environment.c43_adapter.context, &rejected) ==
              FWLAB_SPINE_V0_OK);
    }
    for (index = 0; index < 32; ++index) {
        CHECK(run_to_intent(&ticket[index], &intent));
        CHECK(intent.status_code == 0x01);
    }
    CHECK(close_and_fini(32));
    return 1;
}

static int test_action_lifecycle_matrix(void)
{
    struct fwlab_spine_profile_binding_v0 binding;
    struct fwlab_nvme_command command;
    struct fwlab_host_action_program_v0 program;
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_nvme_completion_intent intent;
    struct fwlab_spine_fake_behavior_v0 behavior;
    uint32_t index;

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_NORMAL, &binding) ==
          FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_IO, 0x02, 1, 1);
    CHECK(admit(&environment.c43_adapter, &binding, &command,
                FWLAB_SPINE_ROLE_V0_NORMAL, &program, &ticket));
    memset(&behavior, 0, sizeof(behavior));
    behavior.backpressure_count = 2;
    behavior.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
    behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL;
    behavior.units_completed = 1;
    fwlab_spine_fake_v0_set_next(&environment.fake,
                                 FWLAB_HOST_ACTION_V0_BLOCK_READ, &behavior);
    CHECK(run_to_intent(&ticket, &intent));
    CHECK(intent.status_code == 0 && intent.actual_length == 512);
    CHECK(fwlab_spine_fake_v0_observed_count(&environment.fake) == 2);
    CHECK(close_and_fini(1));

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_NORMAL, &binding) ==
          FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_IO, 0x01, 1, 1);
    CHECK(admit(&environment.c43_adapter, &binding, &command,
                FWLAB_SPINE_ROLE_V0_NORMAL, &program, &ticket));
    memset(&behavior, 0, sizeof(behavior));
    behavior.terminal_kind = FWLAB_HOST_ACTION_V0_FAILED;
    behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_NONE;
    behavior.fault_domain = 4;
    behavior.fault_code = 3;
    fwlab_spine_fake_v0_set_next(&environment.fake,
                                 FWLAB_HOST_ACTION_V0_DMA_IN, &behavior);
    CHECK(run_to_intent(&ticket, &intent));
    CHECK(intent.status_code == 0x04 && intent.do_not_retry == 0 &&
          intent.effect_class == FWLAB_NVME_EFFECT_NONE);
    CHECK(fwlab_spine_fake_v0_observed_count(&environment.fake) == 1);
    CHECK(fwlab_spine_fake_v0_observed(&environment.fake, 0)->kind ==
          FWLAB_HOST_ACTION_V0_DMA_IN);
    CHECK(close_and_fini(1));

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_NORMAL, &binding) ==
          FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_IO, 0x02, 1, 1);
    CHECK(admit(&environment.c43_adapter, &binding, &command,
                FWLAB_SPINE_ROLE_V0_NORMAL, &program, &ticket));
    ++environment.fake.expected[0].argument[0].argument_uid;
    for (index = 0; index < 4; ++index) {
        uint32_t units = 0;
        uint32_t transitions = 0;
        enum fwlab_spine_result_v0 result =
            fwlab_spine_lifecycle_v0_step(
                environment.lifecycle.bytes, 1, &units, &transitions);

        if (result == FWLAB_SPINE_V0_POISONED) {
            break;
        }
        CHECK(result == FWLAB_SPINE_V0_OK);
    }
    CHECK(index < 4);
    CHECK(fwlab_spine_lifecycle_v0_intent_read(
              environment.lifecycle.bytes, &ticket, &intent) !=
          FWLAB_SPINE_V0_OK);

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_tiny_profile_v0_binding(&environment.tiny_adapter, &binding) ==
          FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_ADMIN, UINT8_C(0xc0), 0, 1);
    CHECK(admit(&environment.tiny_adapter, &binding, &command,
                FWLAB_SPINE_ROLE_V0_NORMAL, &program, &ticket));
    for (index = 0; index < 3; ++index) {
        uint32_t units = 0;
        uint32_t transitions = 0;

        CHECK(fwlab_spine_lifecycle_v0_step(
                  environment.lifecycle.bytes, 1, &units, &transitions) ==
              FWLAB_SPINE_V0_OK);
    }
    CHECK(environment.fake.observed_count == 1);
    ++environment.fake.action[0].token.action_uid;
    {
        uint32_t units = 0;
        uint32_t transitions = 0;

        CHECK(fwlab_spine_lifecycle_v0_step(
                  environment.lifecycle.bytes, 1, &units, &transitions) ==
              FWLAB_SPINE_V0_POISONED);
    }
    return 1;
}

static int test_abort_candidate_failure_and_close(void)
{
    struct fwlab_spine_profile_binding_v0 normal;
    struct fwlab_spine_profile_binding_v0 abort_binding;
    struct fwlab_nvme_command target_command;
    struct fwlab_nvme_command abort_command;
    struct fwlab_host_action_program_v0 target_program;
    struct fwlab_host_action_program_v0 abort_program;
    struct fwlab_spine_command_ticket_v0 target_ticket;
    struct fwlab_spine_command_ticket_v0 abort_ticket;
    struct fwlab_nvme_completion_intent target_intent;
    struct fwlab_nvme_completion_intent abort_intent;
    struct fwlab_spine_fake_behavior_v0 behavior;

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_NORMAL, &normal) ==
          FWLAB_SPINE_V0_OK);
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_ABORT,
                                  &abort_binding) == FWLAB_SPINE_V0_OK);
    target_command = command_base(FWLAB_NVME_QUEUE_IO, 0x02, 1, 1);
    abort_command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
    abort_command.command_dword10_15[0] = UINT32_C(0x00220001);
    CHECK(admit(&environment.c43_adapter, &normal, &target_command,
                FWLAB_SPINE_ROLE_V0_NORMAL, &target_program, &target_ticket));
    CHECK(admit(&environment.c43_adapter, &abort_binding, &abort_command,
                FWLAB_SPINE_ROLE_V0_ABORT, &abort_program, &abort_ticket));
    memset(&behavior, 0, sizeof(behavior));
    behavior.terminal_kind = FWLAB_HOST_ACTION_V0_FAILED;
    behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_NONE;
    behavior.fault_domain = 7;
    behavior.fault_code = 9;
    behavior.abort_candidate_present = 1;
    behavior.abort_uid = abort_ticket.relation_uid;
    behavior.abort_report = FWLAB_SPINE_ABORT_REPORT_V0_FOUND;
    behavior.target_present = 1;
    behavior.target = target_ticket;
    fwlab_spine_fake_v0_set_next(&environment.fake,
                                 FWLAB_HOST_ACTION_V0_TARGET_RESOLVE,
                                 &behavior);
    CHECK(run_to_intent(&target_ticket, &target_intent));
    CHECK(run_to_intent(&abort_ticket, &abort_intent));
    CHECK(target_intent.status_code == 0);
    CHECK(abort_intent.status_code == 0x06 &&
          abort_intent.result_dword0 == 0);
    CHECK(close_and_fini(2));

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_ABORT,
                                  &abort_binding) == FWLAB_SPINE_V0_OK);
    abort_command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
    abort_command.command_dword10_15[0] = UINT32_C(0x00330001);
    CHECK(admit(&environment.c43_adapter, &abort_binding, &abort_command,
                FWLAB_SPINE_ROLE_V0_ABORT, &abort_program, &abort_ticket));
    memset(&behavior, 0, sizeof(behavior));
    behavior.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
    behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_NONE;
    behavior.terminal_delay = 100;
    fwlab_spine_fake_v0_set_next(&environment.fake,
                                 FWLAB_HOST_ACTION_V0_TARGET_RESOLVE,
                                 &behavior);
    {
        uint32_t units = 0;
        uint32_t transitions = 0;

        CHECK(fwlab_spine_lifecycle_v0_step(
                  environment.lifecycle.bytes, 2, &units, &transitions) ==
              FWLAB_SPINE_V0_OK);
    }
    CHECK(fwlab_spine_lifecycle_v0_epoch_close_start(
              environment.lifecycle.bytes, 7) == FWLAB_SPINE_V0_OK);
    CHECK(run_to_intent(&abort_ticket, &abort_intent));
    CHECK(abort_intent.status_code == 0x07 &&
          abort_intent.result_dword0 == 0 &&
          abort_intent.effect_class == FWLAB_NVME_EFFECT_NONE);
    CHECK(close_and_fini(1));
    return 1;
}

static void set_abort_behavior(
    const struct fwlab_spine_command_ticket_v0 *abort_ticket,
    uint32_t report,
    const struct fwlab_spine_command_ticket_v0 *target)
{
    struct fwlab_spine_fake_behavior_v0 behavior;

    memset(&behavior, 0, sizeof(behavior));
    behavior.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
    behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_NONE;
    behavior.abort_candidate_present = 1;
    behavior.abort_uid = abort_ticket->relation_uid;
    behavior.abort_report = report;
    if (target != NULL) {
        behavior.target_present = 1;
        behavior.target = *target;
    }
    fwlab_spine_fake_v0_set_next(&environment.fake,
                                 FWLAB_HOST_ACTION_V0_TARGET_RESOLVE,
                                 &behavior);
}

static enum fwlab_spine_result_v0 rejecting_relation_sink(
    void *context,
    const struct fwlab_host_action_program_v0 *program,
    uint64_t abort_uid,
    uint32_t decision)
{
    if (context == NULL || program == NULL || abort_uid == 0 || decision == 0) {
        return FWLAB_SPINE_V0_INVALID;
    }
    ++rejecting_sink_calls;
    return FWLAB_SPINE_V0_POISONED;
}

static int test_abort_relation_matrix(void)
{
    struct fwlab_spine_profile_binding_v0 normal;
    struct fwlab_spine_profile_binding_v0 abort_binding;
    struct fwlab_nvme_command command;
    struct fwlab_host_action_program_v0 program;
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_nvme_completion_intent intent;
    struct fwlab_spine_fake_behavior_v0 behavior;

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_ABORT,
                                  &abort_binding) == FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
    command.command_dword10_15[0] = UINT32_C(0x00440001);
    CHECK(admit(&environment.c43_adapter, &abort_binding, &command,
                FWLAB_SPINE_ROLE_V0_ABORT, &program, &ticket));
    set_abort_behavior(&ticket, FWLAB_SPINE_ABORT_REPORT_V0_NOT_FOUND, NULL);
    {
        struct fwlab_spine_abort_candidate_v0 duplicate;
        uint32_t units = 0;
        uint32_t transitions = 0;

        CHECK(fwlab_spine_lifecycle_v0_step(
                  environment.lifecycle.bytes, 2, &units, &transitions) ==
              FWLAB_SPINE_V0_OK);
        memset(&duplicate, 0, sizeof(duplicate));
        duplicate.version = FWLAB_SPINE_LIFECYCLE_V0_VERSION;
        duplicate.size = sizeof(duplicate);
        duplicate.abort_uid = ticket.relation_uid;
        duplicate.resolver =
            *fwlab_spine_fake_v0_observed(&environment.fake, 0);
        duplicate.report = FWLAB_SPINE_ABORT_REPORT_V0_NOT_FOUND;
        CHECK(fwlab_spine_fake_v0_abort_candidate_append(
                  &environment.fake, &duplicate) ==
              FWLAB_SPINE_V0_OK);
        CHECK(fwlab_spine_fake_v0_abort_candidate_append(
                  &environment.fake, &duplicate) ==
              FWLAB_SPINE_V0_OK);
    }
    CHECK(run_to_intent(&ticket, &intent));
    CHECK(intent.status_code == 0 && intent.result_dword0 == 1);
    CHECK(close_and_fini(1));

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_ABORT,
                                  &abort_binding) == FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
    command.command_dword10_15[0] = UINT32_C(0x00450001);
    CHECK(admit(&environment.c43_adapter, &abort_binding, &command,
                FWLAB_SPINE_ROLE_V0_ABORT, &program, &ticket));
    set_abort_behavior(&ticket, FWLAB_SPINE_ABORT_REPORT_V0_STALE, NULL);
    CHECK(run_to_intent(&ticket, &intent));
    CHECK(intent.status_code == 0 && intent.result_dword0 == 1);
    CHECK(close_and_fini(1));

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_ABORT,
                                  &abort_binding) == FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
    command.command_dword10_15[0] = UINT32_C(0x00460001);
    CHECK(admit(&environment.c43_adapter, &abort_binding, &command,
                FWLAB_SPINE_ROLE_V0_ABORT, &program, &ticket));
    set_abort_behavior(&ticket, FWLAB_SPINE_ABORT_REPORT_V0_FOUND, &ticket);
    CHECK(run_to_intent(&ticket, &intent));
    CHECK(intent.status_code == 0 && intent.result_dword0 == 1);
    CHECK(close_and_fini(1));

    {
        struct fwlab_nvme_command target_command;
        struct fwlab_nvme_command abort_command;
        struct fwlab_host_action_program_v0 target_program;
        struct fwlab_host_action_program_v0 abort_program;
        struct fwlab_spine_command_ticket_v0 target_ticket;
        struct fwlab_spine_command_ticket_v0 abort_ticket;
        struct fwlab_nvme_completion_intent target_intent;
        struct fwlab_nvme_completion_intent abort_intent;

        CHECK(environment_init(32, 1000));
        CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                      FWLAB_SPINE_ROLE_V0_NORMAL, &normal) ==
              FWLAB_SPINE_V0_OK);
        CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                      FWLAB_SPINE_ROLE_V0_ABORT,
                                      &abort_binding) == FWLAB_SPINE_V0_OK);
        target_command = command_base(FWLAB_NVME_QUEUE_IO, 0x02, 1, 1);
        abort_command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
        abort_command.command_dword10_15[0] = UINT32_C(0x00470001);
        CHECK(admit(&environment.c43_adapter, &normal, &target_command,
                    FWLAB_SPINE_ROLE_V0_NORMAL, &target_program,
                    &target_ticket));
        CHECK(admit(&environment.c43_adapter, &abort_binding, &abort_command,
                    FWLAB_SPINE_ROLE_V0_ABORT, &abort_program,
                    &abort_ticket));
        memset(&behavior, 0, sizeof(behavior));
        behavior.backpressure_count = 100;
        behavior.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
        behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL;
        behavior.units_completed = 1;
        fwlab_spine_fake_v0_set_next(&environment.fake,
                                     FWLAB_HOST_ACTION_V0_BLOCK_READ,
                                     &behavior);
        set_abort_behavior(&abort_ticket, FWLAB_SPINE_ABORT_REPORT_V0_FOUND,
                           &target_ticket);
        CHECK(run_to_intent(&target_ticket, &target_intent));
        CHECK(run_to_intent(&abort_ticket, &abort_intent));
        CHECK(target_intent.status_code == 0x07);
        CHECK(abort_intent.status_code == 0 &&
              abort_intent.result_dword0 == 0);
        CHECK(fwlab_spine_fake_v0_observed_count(&environment.fake) == 1);
        CHECK(close_and_fini(2));
    }

    {
        struct fwlab_nvme_command target_command;
        struct fwlab_nvme_command abort_command;
        struct fwlab_host_action_program_v0 target_program;
        struct fwlab_host_action_program_v0 abort_program;
        struct fwlab_spine_command_ticket_v0 target_ticket;
        struct fwlab_spine_command_ticket_v0 abort_ticket;
        struct fwlab_nvme_completion_intent target_intent;
        struct fwlab_nvme_completion_intent abort_intent;

        CHECK(environment_init(32, 1000));
        CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                      FWLAB_SPINE_ROLE_V0_NORMAL, &normal) ==
              FWLAB_SPINE_V0_OK);
        CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                      FWLAB_SPINE_ROLE_V0_ABORT,
                                      &abort_binding) == FWLAB_SPINE_V0_OK);
        target_command = command_base(FWLAB_NVME_QUEUE_IO, 0x02, 1, 1);
        CHECK(admit(&environment.c43_adapter, &normal, &target_command,
                    FWLAB_SPINE_ROLE_V0_NORMAL, &target_program,
                    &target_ticket));
        CHECK(run_to_intent(&target_ticket, &target_intent));
        CHECK(target_intent.status_code == 0);
        abort_command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
        abort_command.command_dword10_15[0] = UINT32_C(0x00480001);
        CHECK(admit(&environment.c43_adapter, &abort_binding, &abort_command,
                    FWLAB_SPINE_ROLE_V0_ABORT, &abort_program,
                    &abort_ticket));
        set_abort_behavior(&abort_ticket, FWLAB_SPINE_ABORT_REPORT_V0_FOUND,
                           &target_ticket);
        CHECK(run_to_intent(&abort_ticket, &abort_intent));
        CHECK(abort_intent.status_code == 0 &&
              abort_intent.result_dword0 == 1);
        CHECK(close_and_fini(2));
    }

    {
        struct fwlab_nvme_command target_command;
        struct fwlab_nvme_command first_command;
        struct fwlab_nvme_command second_command;
        struct fwlab_host_action_program_v0 target_program;
        struct fwlab_host_action_program_v0 first_program;
        struct fwlab_host_action_program_v0 second_program;
        struct fwlab_spine_command_ticket_v0 target_ticket;
        struct fwlab_spine_command_ticket_v0 first_ticket;
        struct fwlab_spine_command_ticket_v0 second_ticket;
        struct fwlab_nvme_completion_intent target_intent;
        struct fwlab_nvme_completion_intent first_intent;
        struct fwlab_nvme_completion_intent second_intent;
        uint32_t attempt;
        int cancel_seen = 0;

        CHECK(environment_init(32, 1000));
        CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                      FWLAB_SPINE_ROLE_V0_NORMAL, &normal) ==
              FWLAB_SPINE_V0_OK);
        CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                      FWLAB_SPINE_ROLE_V0_ABORT,
                                      &abort_binding) == FWLAB_SPINE_V0_OK);
        target_command = command_base(FWLAB_NVME_QUEUE_IO, 0x01, 1, 1);
        first_command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
        first_command.command_dword10_15[0] = UINT32_C(0x00490001);
        CHECK(admit(&environment.c43_adapter, &normal, &target_command,
                    FWLAB_SPINE_ROLE_V0_NORMAL, &target_program,
                    &target_ticket));
        CHECK(admit(&environment.c43_adapter, &abort_binding, &first_command,
                    FWLAB_SPINE_ROLE_V0_ABORT, &first_program,
                    &first_ticket));
        memset(&behavior, 0, sizeof(behavior));
        behavior.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
        behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL;
        behavior.units_completed = 1;
        behavior.terminal_delay = 10000;
        behavior.cancel_delay = 1000;
        fwlab_spine_fake_v0_set_next(&environment.fake,
                                     FWLAB_HOST_ACTION_V0_DMA_IN, &behavior);
        set_abort_behavior(&first_ticket, FWLAB_SPINE_ABORT_REPORT_V0_FOUND,
                           &target_ticket);
        for (attempt = 0; attempt < 20000; ++attempt) {
            uint32_t units = 0;
            uint32_t transitions = 0;
            uint32_t action_index;

            CHECK(fwlab_spine_lifecycle_v0_step(
                      environment.lifecycle.bytes, 8, &units,
                      &transitions) == FWLAB_SPINE_V0_OK);
            for (action_index = 0;
                 action_index < FWLAB_SPINE_FAKE_V0_MAX_ACTIONS;
                 ++action_index) {
                if (environment.fake.action[action_index].occupied &&
                    environment.fake.action[action_index].token.kind ==
                        FWLAB_HOST_ACTION_V0_DMA_IN &&
                    environment.fake.action[action_index].cancel_calls != 0) {
                    cancel_seen = 1;
                    break;
                }
            }
            if (cancel_seen) {
                break;
            }
        }
        CHECK(cancel_seen);
        second_command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
        second_command.command_dword10_15[0] = UINT32_C(0x004a0001);
        CHECK(admit(&environment.c43_adapter, &abort_binding, &second_command,
                    FWLAB_SPINE_ROLE_V0_ABORT, &second_program,
                    &second_ticket));
        set_abort_behavior(&second_ticket, FWLAB_SPINE_ABORT_REPORT_V0_FOUND,
                           &target_ticket);
        CHECK(run_to_intent(&second_ticket, &second_intent));
        CHECK(second_intent.status_code == 0 &&
              second_intent.result_dword0 == 1);
        CHECK(run_to_intent(&target_ticket, &target_intent));
        CHECK(run_to_intent(&first_ticket, &first_intent));
        CHECK(target_intent.status_code == 0x07);
        CHECK(first_intent.status_code == 0 &&
              first_intent.result_dword0 == 0);
        CHECK(close_and_fini(3));
    }

    {
        struct fwlab_nvme_command target_command;
        struct fwlab_nvme_command abort_command;
        struct fwlab_host_action_program_v0 target_program;
        struct fwlab_host_action_program_v0 abort_program;
        struct fwlab_spine_command_ticket_v0 target_ticket;
        struct fwlab_spine_command_ticket_v0 abort_ticket;
        struct fwlab_nvme_completion_intent target_intent;
        struct fwlab_nvme_completion_intent abort_intent;

        CHECK(environment_init(32, 1000));
        CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                      FWLAB_SPINE_ROLE_V0_NORMAL, &normal) ==
              FWLAB_SPINE_V0_OK);
        CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                      FWLAB_SPINE_ROLE_V0_ABORT,
                                      &abort_binding) == FWLAB_SPINE_V0_OK);
        target_command = command_base(FWLAB_NVME_QUEUE_IO, 0x00, 1, 0);
        abort_command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
        abort_command.command_dword10_15[0] = UINT32_C(0x004b0001);
        CHECK(admit(&environment.c43_adapter, &normal, &target_command,
                    FWLAB_SPINE_ROLE_V0_NORMAL, &target_program,
                    &target_ticket));
        CHECK(admit(&environment.c43_adapter, &abort_binding, &abort_command,
                    FWLAB_SPINE_ROLE_V0_ABORT, &abort_program,
                    &abort_ticket));
        memset(&behavior, 0, sizeof(behavior));
        behavior.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
        behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL;
        behavior.units_completed = 1;
        behavior.retire_start_delay = 100;
        fwlab_spine_fake_v0_set_next(&environment.fake,
                                     FWLAB_HOST_ACTION_V0_BLOCK_FLUSH,
                                     &behavior);
        set_abort_behavior(&abort_ticket, FWLAB_SPINE_ABORT_REPORT_V0_FOUND,
                           &target_ticket);
        CHECK(run_to_intent(&abort_ticket, &abort_intent));
        CHECK(run_to_intent(&target_ticket, &target_intent));
        CHECK(abort_intent.status_code == 0 &&
              abort_intent.result_dword0 == 1);
        CHECK(target_intent.status_code == 0);
        CHECK(close_and_fini(2));
    }
    return 1;
}

static int test_abort_quarantine_matrix(void)
{
    static const uint32_t effects[] = {
        FWLAB_HOST_ACTION_V0_EFFECT_FULL,
        FWLAB_HOST_ACTION_V0_EFFECT_EXACT_PREFIX,
    };
    struct fwlab_spine_profile_binding_v0 abort_binding;
    uint32_t row;

    for (row = 0; row < 2; ++row) {
        struct fwlab_nvme_command command;
        struct fwlab_host_action_program_v0 program;
        struct fwlab_spine_command_ticket_v0 ticket;
        struct fwlab_spine_fake_behavior_v0 behavior;

        CHECK(environment_init(32, 1000));
        CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                      FWLAB_SPINE_ROLE_V0_ABORT,
                                      &abort_binding) == FWLAB_SPINE_V0_OK);
        command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
        command.command_dword10_15[0] =
            (UINT32_C(0x60) + row) << 16 | UINT32_C(1);
        CHECK(admit(&environment.c43_adapter, &abort_binding, &command,
                    FWLAB_SPINE_ROLE_V0_ABORT, &program, &ticket));
        memset(&behavior, 0, sizeof(behavior));
        behavior.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
        behavior.effect = effects[row];
        behavior.units_completed = 1;
        behavior.abort_candidate_present = 1;
        behavior.abort_uid = ticket.relation_uid;
        behavior.abort_report = FWLAB_SPINE_ABORT_REPORT_V0_NOT_FOUND;
        fwlab_spine_fake_v0_set_next(&environment.fake,
                                     FWLAB_HOST_ACTION_V0_TARGET_RESOLVE,
                                     &behavior);
        CHECK(run_to_quarantine(&ticket));
        CHECK(close_and_fini(0));
    }

    {
        struct fwlab_nvme_command command;
        struct fwlab_host_action_program_v0 program;
        struct fwlab_spine_command_ticket_v0 ticket;
        struct fwlab_spine_profile_binding_v0 effective;

        CHECK(environment_init(32, 1000));
        CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                      FWLAB_SPINE_ROLE_V0_ABORT,
                                      &effective) == FWLAB_SPINE_V0_OK);
        CHECK(fwlab_spine_fake_v0_attach_relation_source(
                  &environment.fake, &effective) == FWLAB_SPINE_V0_OK);
        effective.relation_sink = rejecting_relation_sink;
        effective.relation_context = &environment.fake;
        command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
        command.command_dword10_15[0] = UINT32_C(0x00620001);
        CHECK(environment.c43_adapter.ops->plan(
                  environment.c43_adapter.context, &command, &program) ==
              FWLAB_SPINE_V0_OK);
        CHECK(fwlab_spine_lifecycle_v0_admit_start(
                  environment.lifecycle.bytes, &effective, &program,
                  FWLAB_SPINE_ROLE_V0_ABORT, &ticket) == FWLAB_SPINE_V0_OK);
        CHECK(fwlab_spine_fake_v0_expect_program(
                  &environment.fake, &effective, &program) ==
              FWLAB_SPINE_V0_OK);
        set_abort_behavior(&ticket, FWLAB_SPINE_ABORT_REPORT_V0_NOT_FOUND,
                           NULL);
        rejecting_sink_calls = 0;
        CHECK(run_to_quarantine(&ticket));
        CHECK(rejecting_sink_calls == 1);
        CHECK(close_and_fini(0));
    }

    {
        struct fwlab_nvme_command command;
        struct fwlab_host_action_program_v0 program;
        struct fwlab_spine_command_ticket_v0 ticket;
        struct fwlab_spine_fake_behavior_v0 behavior;
        uint32_t step;

        CHECK(environment_init(32, 1000));
        CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                      FWLAB_SPINE_ROLE_V0_ABORT,
                                      &abort_binding) == FWLAB_SPINE_V0_OK);
        command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x08, 0, 0);
        command.command_dword10_15[0] = UINT32_C(0x00630001);
        CHECK(admit(&environment.c43_adapter, &abort_binding, &command,
                    FWLAB_SPINE_ROLE_V0_ABORT, &program, &ticket));
        memset(&behavior, 0, sizeof(behavior));
        behavior.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
        behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_NONE;
        fwlab_spine_fake_v0_set_next(&environment.fake,
                                     FWLAB_HOST_ACTION_V0_TARGET_RESOLVE,
                                     &behavior);
        for (step = 0; step < 5; ++step) {
            uint32_t units = 0;
            uint32_t transitions = 0;

            CHECK(fwlab_spine_lifecycle_v0_step(
                      environment.lifecycle.bytes, 1, &units,
                      &transitions) == FWLAB_SPINE_V0_OK);
        }
        CHECK(fwlab_spine_lifecycle_v0_epoch_close_start(
                  environment.lifecycle.bytes, 7) == FWLAB_SPINE_V0_OK);
        CHECK(run_to_quarantine(&ticket));
        CHECK(close_and_fini(0));
    }
    return 1;
}

static int test_atomic_copy_collision_and_counter(void)
{
    struct fwlab_spine_profile_binding_v0 binding;
    struct fwlab_nvme_command command;
    struct fwlab_host_action_program_v0 program;
    struct fwlab_host_action_program_v0 altered;
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_spine_command_ticket_v0 output;
    struct fwlab_nvme_completion_intent intent;

    CHECK(environment_init(4, 4));
    CHECK(fwlab_linux_profile_v1_binding_v0(
              &environment.linux_adapter, FWLAB_SPINE_ROLE_V0_NORMAL,
              &binding) == FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x0c, 0, 0);
    CHECK(admit(&environment.linux_adapter, &binding, &command,
                FWLAB_SPINE_ROLE_V0_NORMAL, &program, &ticket));
    memset(&program, 0xa5, sizeof(program));
    CHECK(run_to_intent(&ticket, &intent));
    CHECK(intent.status_code == 0x01);
    CHECK(close_and_fini(1));

    CHECK(environment_init(4, 1));
    CHECK(fwlab_linux_profile_v1_binding_v0(
              &environment.linux_adapter, FWLAB_SPINE_ROLE_V0_NORMAL,
              &binding) == FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x0c, 0, 0);
    CHECK(admit(&environment.linux_adapter, &binding, &command,
                FWLAB_SPINE_ROLE_V0_NORMAL, &program, &ticket));
    command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x0c, 0, 0);
    CHECK(environment.linux_adapter.ops->plan(environment.linux_adapter.context,
                                               &command, &altered) ==
          FWLAB_SPINE_V0_OK);
    CHECK(fwlab_spine_lifecycle_v0_admit_start(
              environment.lifecycle.bytes, &binding, &altered,
              FWLAB_SPINE_ROLE_V0_NORMAL, &output) ==
          FWLAB_SPINE_V0_COUNTER_EXHAUSTED);
    CHECK(environment.linux_adapter.ops->retire(
              environment.linux_adapter.context, &altered) ==
          FWLAB_SPINE_V0_OK);
    CHECK(run_to_intent(&ticket, &intent));
    CHECK(close_and_fini(1));

    CHECK(environment_init(4, 4));
    CHECK(fwlab_linux_profile_v1_binding_v0(
              &environment.linux_adapter, FWLAB_SPINE_ROLE_V0_NORMAL,
              &binding) == FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x0c, 0, 0);
    CHECK(environment.linux_adapter.ops->plan(environment.linux_adapter.context,
                                               &command, &program) ==
          FWLAB_SPINE_V0_OK);
    CHECK(fwlab_spine_lifecycle_v0_admit_start(
              environment.lifecycle.bytes, &binding, &program,
              FWLAB_SPINE_ROLE_V0_NORMAL, &ticket) == FWLAB_SPINE_V0_OK);
    altered = program;
    ++altered.program_uid;
    CHECK(fwlab_host_action_program_v0_valid(&altered));
    CHECK(fwlab_spine_lifecycle_v0_admit_query(
              environment.lifecycle.bytes, &binding, &altered,
              FWLAB_SPINE_ROLE_V0_NORMAL, &output) ==
          FWLAB_SPINE_V0_POISONED);
    command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x0c, 0, 0);
    CHECK(environment.linux_adapter.ops->plan(environment.linux_adapter.context,
                                               &command, &altered) ==
          FWLAB_SPINE_V0_OK);
    CHECK(fwlab_spine_lifecycle_v0_admit_start(
              environment.lifecycle.bytes, &binding, &altered,
              FWLAB_SPINE_ROLE_V0_NORMAL, &output) ==
          FWLAB_SPINE_V0_POISONED);
    CHECK(environment.linux_adapter.ops->retire(
              environment.linux_adapter.context, &altered) ==
          FWLAB_SPINE_V0_OK);
    CHECK(fwlab_spine_lifecycle_v0_epoch_close_start(
              environment.lifecycle.bytes, 7) == FWLAB_SPINE_V0_OK);
    CHECK(close_and_fini(0));
    return 1;
}

static int test_epoch_close_cuts(void)
{
    struct fwlab_spine_profile_binding_v0 binding;
    struct fwlab_nvme_command command;
    struct fwlab_nvme_command rejected_command;
    struct fwlab_host_action_program_v0 program;
    struct fwlab_host_action_program_v0 rejected_program;
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_spine_command_ticket_v0 rejected_ticket;
    struct fwlab_nvme_completion_intent intent;
    struct fwlab_spine_fake_behavior_v0 behavior;
    uint32_t units;
    uint32_t transitions;

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_NORMAL, &binding) ==
          FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_IO, 0x02, 1, 1);
    CHECK(admit(&environment.c43_adapter, &binding, &command,
                FWLAB_SPINE_ROLE_V0_NORMAL, &program, &ticket));
    CHECK(fwlab_spine_lifecycle_v0_epoch_close_start(
              environment.lifecycle.bytes, 7) == FWLAB_SPINE_V0_OK);
    rejected_command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x0c, 0, 0);
    CHECK(environment.c43_adapter.ops->plan(environment.c43_adapter.context,
                                             &rejected_command,
                                             &rejected_program) ==
          FWLAB_SPINE_V0_OK);
    CHECK(fwlab_spine_lifecycle_v0_admit_start(
              environment.lifecycle.bytes, &binding, &rejected_program,
              FWLAB_SPINE_ROLE_V0_NORMAL, &rejected_ticket) ==
          FWLAB_SPINE_V0_WRONG_STATE);
    CHECK(environment.c43_adapter.ops->retire(environment.c43_adapter.context,
                                               &rejected_program) ==
          FWLAB_SPINE_V0_OK);
    CHECK(run_to_intent(&ticket, &intent));
    CHECK(intent.status_code == 0x07 &&
          fwlab_spine_fake_v0_observed_count(&environment.fake) == 0);
    CHECK(close_and_fini(1));

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_NORMAL, &binding) ==
          FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_IO, 0x02, 1, 1);
    CHECK(admit(&environment.c43_adapter, &binding, &command,
                FWLAB_SPINE_ROLE_V0_NORMAL, &program, &ticket));
    units = 0;
    transitions = 0;
    CHECK(fwlab_spine_lifecycle_v0_step(
              environment.lifecycle.bytes, 3, &units, &transitions) ==
          FWLAB_SPINE_V0_OK);
    CHECK(environment.fake.observed_count == 1 &&
          environment.fake.action[0].retire_started == 0);
    CHECK(fwlab_spine_lifecycle_v0_epoch_close_start(
              environment.lifecycle.bytes, 7) == FWLAB_SPINE_V0_OK);
    CHECK(run_to_intent(&ticket, &intent));
    CHECK(intent.status_code == 0x07 &&
          intent.effect_class == FWLAB_NVME_EFFECT_UNKNOWN_PREFIX);
    CHECK(fwlab_spine_fake_v0_observed_count(&environment.fake) == 1);
    CHECK(close_and_fini(1));

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_c43_p1_binding_v0(&environment.c43_adapter,
                                  FWLAB_SPINE_ROLE_V0_NORMAL, &binding) ==
          FWLAB_SPINE_V0_OK);
    command = command_base(FWLAB_NVME_QUEUE_IO, 0x02, 1, 1);
    CHECK(admit(&environment.c43_adapter, &binding, &command,
                FWLAB_SPINE_ROLE_V0_NORMAL, &program, &ticket));
    memset(&behavior, 0, sizeof(behavior));
    behavior.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
    behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL;
    behavior.units_completed = 1;
    behavior.wait_for_epoch_close = 1;
    fwlab_spine_fake_v0_set_next(&environment.fake,
                                 FWLAB_HOST_ACTION_V0_BLOCK_READ, &behavior);
    units = 0;
    transitions = 0;
    CHECK(fwlab_spine_lifecycle_v0_step(
              environment.lifecycle.bytes, 2, &units, &transitions) ==
          FWLAB_SPINE_V0_OK);
    CHECK(environment.fake.observed_count == 1);
    CHECK(fwlab_spine_lifecycle_v0_epoch_close_start(
              environment.lifecycle.bytes, 7) == FWLAB_SPINE_V0_OK);
    CHECK(run_to_intent(&ticket, &intent));
    CHECK(intent.status_code == 0x07);
    CHECK(environment.fake.lane[FWLAB_HOST_ACTION_V0_BLOCK_READ - 1]
              .close_acked == 1);
    CHECK(close_and_fini(1));
    return 1;
}

static int test_normalized_queue_outcomes(void)
{
    static const uint32_t outcomes[] = {
        FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE,
        FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE_SIZE,
        FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE_DELETE,
    };
    static const uint16_t expected_status[] = {0x01, 0x02, 0x0c};
    struct fwlab_spine_profile_binding_v0 binding;
    uint32_t row;

    for (row = 0; row < 3; ++row) {
        struct fwlab_nvme_command command;
        struct fwlab_host_action_program_v0 program;
        struct fwlab_spine_command_ticket_v0 ticket;
        struct fwlab_nvme_completion_intent intent;
        struct fwlab_spine_fake_behavior_v0 behavior;

        CHECK(environment_init(32, 1000));
        CHECK(fwlab_linux_profile_v1_binding_v0(
                  &environment.linux_adapter, FWLAB_SPINE_ROLE_V0_NORMAL,
                  &binding) == FWLAB_SPINE_V0_OK);
        if (row == 0) {
            command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x01, 0, 1);
            command.command_dword10_15[0] = UINT32_C(0x001f0001);
            command.command_dword10_15[1] = UINT32_C(0x00010001);
        } else if (row == 1) {
            command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x05, 0, 1);
            command.command_dword10_15[0] = UINT32_C(0x001f0001);
            command.command_dword10_15[1] = UINT32_C(0x00000003);
        } else {
            command = command_base(FWLAB_NVME_QUEUE_ADMIN, 0x04, 0, 0);
            command.command_dword10_15[0] = 1;
        }
        CHECK(admit(&environment.linux_adapter, &binding, &command,
                    FWLAB_SPINE_ROLE_V0_NORMAL, &program, &ticket));
        memset(&behavior, 0, sizeof(behavior));
        behavior.terminal_kind = FWLAB_HOST_ACTION_V0_FAILED;
        behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_NONE;
        behavior.fault_domain = 8;
        behavior.fault_code = row + 1;
        behavior.normalized_outcome = outcomes[row];
        fwlab_spine_fake_v0_set_next(&environment.fake,
                                     FWLAB_HOST_ACTION_V0_QUEUE_EFFECT,
                                     &behavior);
        CHECK(run_to_intent(&ticket, &intent));
        CHECK(intent.status_code_type == 1 &&
              intent.status_code == expected_status[row] &&
              intent.do_not_retry == 1);
        CHECK(environment.fake.action[0].result_latched == 1);
        CHECK(close_and_fini(1));
    }

    CHECK(environment_init(32, 1000));
    CHECK(fwlab_linux_profile_v1_binding_v0(
              &environment.linux_adapter, FWLAB_SPINE_ROLE_V0_NORMAL,
              &binding) == FWLAB_SPINE_V0_OK);
    {
        struct fwlab_nvme_command command =
            command_base(FWLAB_NVME_QUEUE_ADMIN, 0x05, 0, 1);
        struct fwlab_host_action_program_v0 program;
        struct fwlab_spine_command_ticket_v0 ticket;
        struct fwlab_spine_fake_behavior_v0 behavior;
        uint32_t step;

        command.command_dword10_15[0] = UINT32_C(0x001f0001);
        command.command_dword10_15[1] = UINT32_C(0x00000003);
        CHECK(admit(&environment.linux_adapter, &binding, &command,
                    FWLAB_SPINE_ROLE_V0_NORMAL, &program, &ticket));
        memset(&behavior, 0, sizeof(behavior));
        behavior.terminal_kind = FWLAB_HOST_ACTION_V0_FAILED;
        behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_NONE;
        behavior.fault_domain = 8;
        behavior.fault_code = 1;
        behavior.normalized_outcome =
            FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE;
        fwlab_spine_fake_v0_set_next(&environment.fake,
                                     FWLAB_HOST_ACTION_V0_QUEUE_EFFECT,
                                     &behavior);
        for (step = 0; step < 4; ++step) {
            uint32_t units = 0;
            uint32_t transitions = 0;

            CHECK(fwlab_spine_lifecycle_v0_step(
                      environment.lifecycle.bytes, 1, &units,
                      &transitions) == FWLAB_SPINE_V0_OK);
        }
        environment.fake.action[0].behavior.normalized_outcome =
            FWLAB_SPINE_PROVIDER_V0_INVALID_QUEUE_SIZE;
        {
            uint32_t units = 0;
            uint32_t transitions = 0;

            CHECK(fwlab_spine_lifecycle_v0_step(
                      environment.lifecycle.bytes, 1, &units,
                      &transitions) == FWLAB_SPINE_V0_POISONED);
        }
    }
    return 1;
}

static int sha256_text_valid(const char *text)
{
    size_t index;

    if (text == NULL || strlen(text) != 64) {
        return 0;
    }
    for (index = 0; index < 64; ++index) {
        if (!((text[index] >= '0' && text[index] <= '9') ||
              (text[index] >= 'a' && text[index] <= 'f'))) {
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 2 || !sha256_text_valid(argv[1])) {
        fputs("S0-B requires the exact lifecycle object SHA-256\n", stderr);
        return 1;
    }
    lifecycle_object_digest = argv[1];
    if (!test_c43_profile() || !test_linux_profile() ||
        !test_tiny_profile() || !test_cross_profile_rollback() ||
        !test_action_lifecycle_matrix() ||
        !test_abort_candidate_failure_and_close() ||
        !test_abort_relation_matrix() || !test_abort_quarantine_matrix() ||
        !test_atomic_copy_collision_and_counter() ||
        !test_epoch_close_cuts() || !test_normalized_queue_outcomes()) {
        return 1;
    }
    puts("S0B_LIFECYCLE_CASES|admit-retry=1|rollback=1|dependency=1|backpressure=1|response-loss=1|typed-sidecars=1|normalized-results=1|substitution=1|first-failure=1|abort-cases=14|close-cuts=3|close-fair=1|close-resume=1|intent-repeat=1|cpls-advance=0|PASS");
    puts("S0-B profile matrix: PASS (profiles=3 artifacts=3)");
    return 0;
}
