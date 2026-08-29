/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "c31_fake_dma.h"
#include "c31_fake_nfc.h"
#include "c31_fake_provider.h"
#include "fwlab/portable/c31.h"
#include "fwlab/portable/c31_codec.h"

#define TEST_ARENA_BYTES 262144u
#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

union test_arena {
    max_align_t alignment;
    uint8_t bytes[TEST_ARENA_BYTES];
};

struct fixture {
    union test_arena arena;
    struct c31_fake_provider_context dma;
    struct c31_fake_dma_context data_dma;
    struct c31_fake_nfc_context dedicated_nfc;
    struct c31_fake_provider_context nfc;
    struct fwlab_c31_capacity capacity;
    struct fwlab_c31_provider_set providers;
    struct fwlab_c31 *instance;
    uint64_t nonce;
};

static struct fixture fixture_a;
static struct fixture fixture_b;

static void capacity_default(struct fwlab_c31_capacity *capacity)
{
    memset(capacity, 0, sizeof(*capacity));
    capacity->version = FWLAB_C31_CONTRACT_VERSION;
    capacity->size = (uint16_t)sizeof(*capacity);
    capacity->commands = 4;
    capacity->abort_tickets = 4;
    capacity->event_batch = 4;
    capacity->trace_entries = 128;
    capacity->scratch_bytes = 256;
    capacity->slot_generation_limit = 100;
    capacity->operation_generation_limit = 100;
    capacity->lease_generation_limit = 100;
    capacity->ticket_generation_limit = 100;
    capacity->controller_epoch_limit = 100;
    capacity->command_uid_limit = 1000;
}

static int fixture_init_data_dma(struct fixture *fixture, uint64_t nonce)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->nonce = nonce;
    capacity_default(&fixture->capacity);
    c31_fake_dma_init(&fixture->data_dma);
    c31_fake_provider_init(&fixture->nfc, FWLAB_C31_PROVIDER_NFC);
    fixture->providers.dma = c31_fake_dma_provider(&fixture->data_dma);
    fixture->providers.nfc = c31_fake_provider(&fixture->nfc);
    return fwlab_c31_init(fixture->arena.bytes, sizeof(fixture->arena.bytes),
                          &fixture->capacity, nonce, &fixture->providers,
                          &fixture->instance) == FWLAB_C31_API_OK;
}

static int fixture_init_dedicated_nfc(struct fixture *fixture, uint64_t nonce)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->nonce = nonce;
    capacity_default(&fixture->capacity);
    c31_fake_provider_init(&fixture->dma, FWLAB_C31_PROVIDER_DMA);
    c31_fake_nfc_init(&fixture->dedicated_nfc);
    fixture->providers.dma = c31_fake_provider(&fixture->dma);
    fixture->providers.nfc = c31_fake_nfc_provider(&fixture->dedicated_nfc);
    return fwlab_c31_init(fixture->arena.bytes, sizeof(fixture->arena.bytes),
                          &fixture->capacity, nonce, &fixture->providers,
                          &fixture->instance) == FWLAB_C31_API_OK;
}

static int fixture_init(
    struct fixture *fixture,
    uint64_t nonce,
    const struct fwlab_c31_capacity *override
)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->nonce = nonce;
    if (override == NULL) {
        capacity_default(&fixture->capacity);
    } else {
        fixture->capacity = *override;
    }
    c31_fake_provider_init(&fixture->dma, FWLAB_C31_PROVIDER_DMA);
    c31_fake_provider_init(&fixture->nfc, FWLAB_C31_PROVIDER_NFC);
    fixture->providers.dma = c31_fake_provider(&fixture->dma);
    fixture->providers.nfc = c31_fake_provider(&fixture->nfc);
    return fwlab_c31_init(fixture->arena.bytes, sizeof(fixture->arena.bytes),
                          &fixture->capacity, nonce, &fixture->providers,
                          &fixture->instance) == FWLAB_C31_API_OK;
}

static struct fwlab_c31_command_descriptor descriptor_make(
    uint8_t provider_kind,
    uint64_t identifier
)
{
    struct fwlab_c31_command_descriptor descriptor;

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.version = FWLAB_C31_CONTRACT_VERSION;
    descriptor.size = (uint16_t)sizeof(descriptor);
    descriptor.origin.word[0] = UINT64_C(0xabc00000) + identifier;
    descriptor.origin.word[1] = UINT64_C(0xdef00000) + identifier;
    descriptor.trace_cookie = UINT64_C(0x10000000) + identifier;
    descriptor.provider_kind = provider_kind;
    if (provider_kind != FWLAB_C31_PROVIDER_NONE) {
        descriptor.provider_request.word[0] = identifier;
        descriptor.provider_request.word[1] = ~identifier;
    }
    if (provider_kind == FWLAB_C31_PROVIDER_DMA) {
        descriptor.capability.word[0] = UINT64_C(0x20000000) + identifier;
        descriptor.capability.word[1] = UINT64_C(0x30000000) + identifier;
        descriptor.dma_direction = FWLAB_C31_DMA_TO_CONTROLLER;
        descriptor.length = 32;
    }
    return descriptor;
}

static struct c31_fake_scenario scenario_make(
    uint64_t identifier,
    uint8_t terminal
)
{
    struct c31_fake_scenario scenario;

    memset(&scenario, 0, sizeof(scenario));
    scenario.request.word[0] = identifier;
    scenario.request.word[1] = ~identifier;
    scenario.submit_disposition = FWLAB_C31_PROVIDER_ACCEPTED;
    scenario.terminal = terminal;
    scenario.cancel_wins = true;
    if (terminal == FWLAB_C31_PROVIDER_SUCCESS) {
        scenario.terminal_fault.effect_class = FWLAB_C31_EFFECT_FULL;
    } else {
        scenario.terminal_fault.domain = FWLAB_C31_FAULT_PROVIDER;
        scenario.terminal_fault.retry_class = FWLAB_C31_RETRY_NEVER;
        scenario.terminal_fault.effect_class = FWLAB_C31_EFFECT_NONE;
        scenario.terminal_fault.reason = FWLAB_C31_REASON_PROVIDER_FAILED;
    }
    return scenario;
}

static int step_once(struct fixture *fixture)
{
    struct fwlab_c31_step_result result;

    return fwlab_c31_step(fixture->instance, 1, &result) ==
           FWLAB_C31_API_OK;
}

static int wait_state(
    struct fixture *fixture,
    const struct fwlab_c31_command_handle *command,
    enum fwlab_c31_lifecycle_state wanted,
    unsigned int limit
)
{
    unsigned int iteration;

    for (iteration = 0; iteration < limit; ++iteration) {
        enum fwlab_c31_lifecycle_state state;

        if (fwlab_c31_command_state(fixture->instance, command, &state) ==
                FWLAB_C31_API_OK && state == wanted) {
            return 1;
        }
        if (!step_once(fixture)) {
            return 0;
        }
    }
    return 0;
}

static int consume_completion(
    struct fixture *fixture,
    const struct fwlab_c31_command_handle *command,
    uint32_t expected_result,
    uint8_t expected_effect
)
{
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;

    CHECK(fwlab_c31_completion_acquire(fixture->instance, command, &lease,
                                       &intent) == FWLAB_C31_API_OK);
    CHECK(intent.result == expected_result);
    CHECK(intent.fault.effect_class == expected_effect);
    CHECK(fwlab_c31_completion_consume(fixture->instance, &lease) ==
          FWLAB_C31_API_OK);
    return 0;
}

static int test_codec_literal(void)
{
    struct fwlab_c31_command_descriptor descriptor =
        descriptor_make(FWLAB_C31_PROVIDER_DMA, UINT64_C(0x3132333435363738));
    struct fwlab_c31_command_descriptor decoded;
    uint8_t wire[FWLAB_C31_DESCRIPTOR_WIRE_SIZE];
    static const uint8_t expected[FWLAB_C31_DESCRIPTOR_WIRE_SIZE] = {
        0x43, 0x33, 0x31, 0x44, 0x01, 0x00, 0x60, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11,
        0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
        0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31,
        0x48, 0x47, 0x46, 0x45, 0x44, 0x43, 0x42, 0x41,
        0x58, 0x57, 0x56, 0x55, 0x54, 0x53, 0x52, 0x51,
        0x68, 0x67, 0x66, 0x65, 0x64, 0x63, 0x62, 0x61,
        0x74, 0x73, 0x72, 0x71, 0x84, 0x83, 0x82, 0x81,
        0x94, 0x93, 0x92, 0x91, 0x04, 0x03, 0x02, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    descriptor.origin.word[0] = UINT64_C(0x0102030405060708);
    descriptor.origin.word[1] = UINT64_C(0x1112131415161718);
    descriptor.trace_cookie = UINT64_C(0x2122232425262728);
    descriptor.provider_request.word[0] = UINT64_C(0x3132333435363738);
    descriptor.provider_request.word[1] = UINT64_C(0x4142434445464748);
    descriptor.capability.word[0] = UINT64_C(0x5152535455565758);
    descriptor.capability.word[1] = UINT64_C(0x6162636465666768);
    descriptor.capability_offset = UINT32_C(0x71727374);
    descriptor.controller_region = UINT32_C(0x81828384);
    descriptor.controller_offset = UINT32_C(0x91929394);
    descriptor.length = UINT32_C(0x01020304);

    CHECK(fwlab_c31_descriptor_encode(&descriptor, wire, sizeof(wire)) ==
          FWLAB_C31_API_OK);
    CHECK(memcmp(wire, expected, sizeof(wire)) == 0);
    CHECK(fwlab_c31_descriptor_decode(wire, sizeof(wire), &decoded) ==
          FWLAB_C31_API_OK);
    CHECK(decoded.origin.word[0] == descriptor.origin.word[0]);
    CHECK(decoded.provider_request.word[1] ==
          descriptor.provider_request.word[1]);
    CHECK(decoded.capability.word[0] == descriptor.capability.word[0]);
    CHECK(decoded.controller_offset == descriptor.controller_offset);
    CHECK(decoded.length == descriptor.length);
    wire[95] = 1;
    CHECK(fwlab_c31_descriptor_decode(wire, sizeof(wire), &decoded) ==
          FWLAB_C31_API_INVALID_CONTRACT);
    return 0;
}

static int test_submit_and_policy_lease(void)
{
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease1;
    struct fwlab_c31_completion_lease lease2;
    struct fwlab_c31_completion_intent intent;

    CHECK(fixture_init(&fixture_a, 1, NULL));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_NONE, 1);
    descriptor.reserved0 = 1;
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_INVALID_CONTRACT);
    descriptor.reserved0 = 0;
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command,
                     FWLAB_C31_CMD_COMPLETION_READY, 4));
    CHECK(fwlab_c31_completion_acquire(fixture_a.instance, &command, &lease1,
                                       &intent) == FWLAB_C31_API_OK);
    CHECK(intent.result == FWLAB_C31_COMPLETION_SUCCESS);
    CHECK(fwlab_c31_completion_release(fixture_a.instance, &lease1) ==
          FWLAB_C31_API_OK);
    CHECK(fwlab_c31_completion_acquire(fixture_a.instance, &command, &lease2,
                                       &intent) == FWLAB_C31_API_OK);
    CHECK(lease2.lease_generation != lease1.lease_generation);
    CHECK(fwlab_c31_completion_consume(fixture_a.instance, &lease1) ==
          FWLAB_C31_API_STALE_TOKEN);
    CHECK(fwlab_c31_completion_consume(fixture_a.instance, &lease2) ==
          FWLAB_C31_API_OK);
    CHECK(fwlab_c31_completion_consume(fixture_a.instance, &lease2) ==
          FWLAB_C31_API_STALE_TOKEN);
    return 0;
}

static int test_immutable_ownership_and_missing_provider(void)
{
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent1;
    struct fwlab_c31_completion_intent intent2;
    struct fwlab_c31_provider_set no_providers;
    struct fwlab_c31_capacity capacity;
    enum fwlab_c31_lifecycle_state state;
    uint64_t expected_origin;
    uint64_t expected_trace;

    CHECK(fixture_init(&fixture_a, 33, NULL));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_NONE, 700);
    expected_origin = descriptor.origin.word[0];
    expected_trace = descriptor.trace_cookie;
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    memset(&descriptor, 0xa5, sizeof(descriptor));
    CHECK(fwlab_c31_command_state(fixture_a.instance, &command, &state) ==
          FWLAB_C31_API_OK);
    CHECK(state == FWLAB_C31_CMD_ACCEPTED);
    CHECK(wait_state(&fixture_a, &command,
                     FWLAB_C31_CMD_COMPLETION_READY, 4));
    CHECK(fwlab_c31_completion_acquire(fixture_a.instance, &command, &lease,
                                       &intent1) == FWLAB_C31_API_OK);
    CHECK(intent1.origin.word[0] == expected_origin);
    CHECK(intent1.trace_cookie == expected_trace);
    CHECK(fwlab_c31_completion_release(fixture_a.instance, &lease) ==
          FWLAB_C31_API_OK);
    intent1.result = FWLAB_C31_COMPLETION_INTERNAL_FAILURE;
    intent1.origin.word[0] = 0;
    CHECK(fwlab_c31_completion_acquire(fixture_a.instance, &command, &lease,
                                       &intent2) == FWLAB_C31_API_OK);
    CHECK(intent2.result == FWLAB_C31_COMPLETION_SUCCESS);
    CHECK(intent2.origin.word[0] == expected_origin);
    CHECK(fwlab_c31_completion_consume(fixture_a.instance, &lease) ==
          FWLAB_C31_API_OK);

    memset(&fixture_a, 0, sizeof(fixture_a));
    capacity_default(&capacity);
    memset(&no_providers, 0, sizeof(no_providers));
    CHECK(fwlab_c31_init(fixture_a.arena.bytes,
                         sizeof(fixture_a.arena.bytes), &capacity, 34,
                         &no_providers, &fixture_a.instance) ==
          FWLAB_C31_API_OK);
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_DMA, 701);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command,
                     FWLAB_C31_CMD_COMPLETION_READY, 4));
    CHECK(fwlab_c31_completion_acquire(fixture_a.instance, &command, &lease,
                                       &intent2) == FWLAB_C31_API_OK);
    CHECK(intent2.result == FWLAB_C31_COMPLETION_UNSUPPORTED_COMMAND);
    CHECK(fwlab_c31_completion_consume(fixture_a.instance, &lease) ==
          FWLAB_C31_API_OK);
    return 0;
}

static int test_backpressure_success_and_reject(void)
{
    struct c31_fake_scenario scenario;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;

    CHECK(fixture_init(&fixture_a, 2, NULL));
    scenario = scenario_make(10, FWLAB_C31_PROVIDER_SUCCESS);
    scenario.backpressure_count = 2;
    scenario.delay_polls = 1;
    CHECK(c31_fake_provider_add(&fixture_a.dma, &scenario));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_DMA, 10);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command, FWLAB_C31_CMD_HELD, 8));
    CHECK(wait_state(&fixture_a, &command,
                     FWLAB_C31_CMD_COMPLETION_READY, 32));
    CHECK(consume_completion(&fixture_a, &command,
                             FWLAB_C31_COMPLETION_SUCCESS,
                             FWLAB_C31_EFFECT_FULL) == 0);

    scenario = scenario_make(11, FWLAB_C31_PROVIDER_FAILED);
    scenario.submit_disposition = FWLAB_C31_PROVIDER_REJECTED;
    scenario.submit_fault.domain = FWLAB_C31_FAULT_RESOURCE;
    scenario.submit_fault.retry_class = FWLAB_C31_RETRY_NEVER;
    scenario.submit_fault.effect_class = FWLAB_C31_EFFECT_NONE;
    scenario.submit_fault.reason = FWLAB_C31_REASON_PROVIDER_REJECTED;
    CHECK(c31_fake_provider_add(&fixture_a.dma, &scenario));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_DMA, 11);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command,
                     FWLAB_C31_CMD_COMPLETION_READY, 12));
    CHECK(consume_completion(&fixture_a, &command,
                             FWLAB_C31_COMPLETION_TRANSFER_FAILURE,
                             FWLAB_C31_EFFECT_NONE) == 0);
    return 0;
}

static int test_abort_before_and_after_accept(void)
{
    struct c31_fake_scenario scenario;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_abort_ticket ticket;
    enum fwlab_c31_abort_outcome outcome;

    CHECK(fixture_init(&fixture_a, 3, NULL));
    scenario = scenario_make(20, FWLAB_C31_PROVIDER_SUCCESS);
    scenario.delay_polls = 2;
    CHECK(c31_fake_provider_add(&fixture_a.dma, &scenario));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_DMA, 20);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(fwlab_c31_abort_request(fixture_a.instance, &command, &ticket,
                                  &outcome) == FWLAB_C31_API_OK);
    CHECK(outcome == FWLAB_C31_ABORT_TERMINAL);
    CHECK(wait_state(&fixture_a, &command,
                     FWLAB_C31_CMD_COMPLETION_READY, 2));
    CHECK(consume_completion(&fixture_a, &command,
                             FWLAB_C31_COMPLETION_ABORTED,
                             FWLAB_C31_EFFECT_NONE) == 0);
    CHECK(fwlab_c31_abort_ack(fixture_a.instance, &ticket) ==
          FWLAB_C31_API_OK);

    scenario = scenario_make(21, FWLAB_C31_PROVIDER_SUCCESS);
    scenario.delay_polls = 3;
    scenario.cancel_wins = true;
    CHECK(c31_fake_provider_add(&fixture_a.dma, &scenario));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_DMA, 21);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command, FWLAB_C31_CMD_RUNNING, 12));
    CHECK(fwlab_c31_abort_request(fixture_a.instance, &command, &ticket,
                                  &outcome) == FWLAB_C31_API_OK);
    CHECK(outcome == FWLAB_C31_ABORT_PENDING);
    CHECK(wait_state(&fixture_a, &command,
                     FWLAB_C31_CMD_COMPLETION_READY, 32));
    CHECK(fwlab_c31_abort_query(fixture_a.instance, &ticket, &outcome) ==
          FWLAB_C31_API_OK);
    CHECK(outcome == FWLAB_C31_ABORT_TERMINAL);
    CHECK(consume_completion(&fixture_a, &command,
                             FWLAB_C31_COMPLETION_ABORTED,
                             FWLAB_C31_EFFECT_FULL) == 0);
    CHECK(fwlab_c31_abort_ack(fixture_a.instance, &ticket) ==
          FWLAB_C31_API_OK);
    return 0;
}

static int test_abort_race_normal_wins(void)
{
    struct c31_fake_scenario scenario;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_abort_ticket ticket;
    enum fwlab_c31_abort_outcome outcome;

    CHECK(fixture_init(&fixture_a, 31, NULL));
    scenario = scenario_make(610, FWLAB_C31_PROVIDER_SUCCESS);
    scenario.delay_polls = 2;
    scenario.cancel_wins = false;
    CHECK(c31_fake_provider_add(&fixture_a.dma, &scenario));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_DMA, 610);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command, FWLAB_C31_CMD_RUNNING, 12));
    CHECK(fwlab_c31_abort_request(fixture_a.instance, &command, &ticket,
                                  &outcome) == FWLAB_C31_API_OK);
    CHECK(outcome == FWLAB_C31_ABORT_PENDING);
    CHECK(wait_state(&fixture_a, &command,
                     FWLAB_C31_CMD_COMPLETION_READY, 24));
    CHECK(fwlab_c31_abort_query(fixture_a.instance, &ticket, &outcome) ==
          FWLAB_C31_API_OK);
    CHECK(outcome == FWLAB_C31_ABORT_TOO_LATE);
    CHECK(consume_completion(&fixture_a, &command,
                             FWLAB_C31_COMPLETION_SUCCESS,
                             FWLAB_C31_EFFECT_FULL) == 0);
    CHECK(fwlab_c31_abort_ack(fixture_a.instance, &ticket) ==
          FWLAB_C31_API_OK);
    return 0;
}

static int test_reset_and_stale_lease(void)
{
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;
    enum fwlab_c31_lifecycle_state state;

    CHECK(fixture_init(&fixture_a, 4, NULL));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_NONE, 30);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command,
                     FWLAB_C31_CMD_COMPLETION_READY, 4));
    CHECK(fwlab_c31_completion_acquire(fixture_a.instance, &command, &lease,
                                       &intent) == FWLAB_C31_API_OK);
    CHECK(fwlab_c31_reset_begin(fixture_a.instance) == FWLAB_C31_API_OK);
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_RESET_ACK);
    CHECK(fwlab_c31_completion_consume(fixture_a.instance, &lease) ==
          FWLAB_C31_API_WRONG_STATE);
    CHECK(fwlab_c31_reset_ack(fixture_a.instance) == FWLAB_C31_API_OK);
    CHECK(fwlab_c31_command_state(fixture_a.instance, &command, &state) ==
          FWLAB_C31_API_STALE_TOKEN);
    return 0;
}

static int test_reset_running_and_abort_ack(void)
{
    struct c31_fake_scenario scenario;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_abort_ticket ticket;
    enum fwlab_c31_abort_outcome outcome;
    unsigned int iteration;

    CHECK(fixture_init(&fixture_a, 5, NULL));
    scenario = scenario_make(40, FWLAB_C31_PROVIDER_SUCCESS);
    scenario.delay_polls = 2;
    scenario.cancel_wins = true;
    CHECK(c31_fake_provider_add(&fixture_a.dma, &scenario));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_DMA, 40);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command, FWLAB_C31_CMD_RUNNING, 12));
    CHECK(fwlab_c31_abort_request(fixture_a.instance, &command, &ticket,
                                  &outcome) == FWLAB_C31_API_OK);
    CHECK(fwlab_c31_reset_begin(fixture_a.instance) == FWLAB_C31_API_OK);
    CHECK(fwlab_c31_abort_query(fixture_a.instance, &ticket, &outcome) ==
          FWLAB_C31_API_OK);
    CHECK(outcome == FWLAB_C31_ABORT_RESET_SUPERSEDED);
    for (iteration = 0; iteration < 32 &&
         fwlab_c31_phase(fixture_a.instance) ==
             FWLAB_C31_INSTANCE_RESET_DRAIN; ++iteration) {
        CHECK(step_once(&fixture_a));
    }
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_RESET_DRAIN);
    CHECK(fwlab_c31_abort_ack(fixture_a.instance, &ticket) ==
          FWLAB_C31_API_OK);
    CHECK(step_once(&fixture_a));
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_RESET_ACK);
    CHECK(fwlab_c31_reset_ack(fixture_a.instance) == FWLAB_C31_API_OK);
    return 0;
}

static int test_duplicate_event_faults(void)
{
    struct c31_fake_scenario scenario;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_step_result step;
    unsigned int iteration;

    CHECK(fixture_init(&fixture_a, 6, NULL));
    scenario = scenario_make(50, FWLAB_C31_PROVIDER_SUCCESS);
    scenario.duplicate_terminal = true;
    CHECK(c31_fake_provider_add(&fixture_a.dma, &scenario));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_DMA, 50);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command,
                     FWLAB_C31_CMD_COMPLETION_READY, 16));
    for (iteration = 0; iteration < 4; ++iteration) {
        if (fwlab_c31_step(fixture_a.instance, 1, &step) ==
            FWLAB_C31_API_INVARIANT_FAILURE) {
            break;
        }
    }
    CHECK(iteration < 4);
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_FAULTED);
    return 0;
}

static int run_dma_case(
    struct fixture *fixture,
    uint64_t identifier,
    uint8_t direction,
    uint32_t region,
    const uint8_t *external,
    uint32_t external_length,
    const struct c31_fake_dma_scenario *scenario,
    struct fwlab_c31_completion_intent *intent,
    struct fwlab_c31_command_handle *command_out,
    int *capability_index
)
{
    struct fwlab_c31_command_descriptor descriptor =
        descriptor_make(FWLAB_C31_PROVIDER_DMA, identifier);
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    int index;

    descriptor.dma_direction = direction;
    descriptor.controller_region = region;
    descriptor.length = 8;
    index = c31_fake_dma_register(
        &fixture->data_dma, &descriptor.capability, &descriptor.origin,
        fixture->nonce,
        1, direction, external, external_length);
    CHECK(index >= 0);
    CHECK(c31_fake_dma_add(&fixture->data_dma, scenario));
    CHECK(fwlab_c31_submit(fixture->instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(fixture, &command, FWLAB_C31_CMD_COMPLETION_READY, 24));
    CHECK(fwlab_c31_completion_acquire(fixture->instance, &command, &lease,
                                       intent) == FWLAB_C31_API_OK);
    CHECK(fwlab_c31_completion_consume(fixture->instance, &lease) ==
          FWLAB_C31_API_OK);
    if (command_out != NULL) {
        *command_out = command;
    }
    if (capability_index != NULL) {
        *capability_index = index;
    }
    return 0;
}

static int test_dma_effects_and_capabilities(void)
{
    struct c31_fake_dma_scenario scenario;
    struct fwlab_c31_completion_intent intent;
    uint8_t external[C31_FAKE_DMA_BYTES];
    uint8_t before[C31_FAKE_DMA_BYTES];
    uint8_t *controller;
    uint8_t *capability;
    unsigned int index;
    int cap_index;

    CHECK(fixture_init_data_dma(&fixture_a, 11));
    for (index = 0; index < sizeof(external); ++index) {
        external[index] = (uint8_t)(index + 1);
    }
    memset(&scenario, 0, sizeof(scenario));
    scenario.request.word[0] = 100;
    scenario.request.word[1] = ~UINT64_C(100);
    scenario.terminal = FWLAB_C31_PROVIDER_SUCCESS;
    scenario.effect_class = FWLAB_C31_EFFECT_FULL;
    CHECK(run_dma_case(&fixture_a, 100, FWLAB_C31_DMA_TO_CONTROLLER,
                       0, external, sizeof(external), &scenario, &intent,
                       NULL, &cap_index) == 0);
    CHECK(intent.result == FWLAB_C31_COMPLETION_SUCCESS);
    CHECK(intent.fault.effect_class == FWLAB_C31_EFFECT_FULL);
    controller = c31_fake_dma_controller(&fixture_a.data_dma, 0);
    CHECK(memcmp(controller, external, 8) == 0);

    controller = c31_fake_dma_controller(&fixture_a.data_dma, 1);
    memset(controller, 0xa5, C31_FAKE_DMA_BYTES);
    memcpy(before, controller, sizeof(before));
    memset(&scenario, 0, sizeof(scenario));
    scenario.request.word[0] = 101;
    scenario.request.word[1] = ~UINT64_C(101);
    scenario.terminal = FWLAB_C31_PROVIDER_FAILED;
    scenario.effect_class = FWLAB_C31_EFFECT_EXACT_PREFIX;
    scenario.actual_prefix = 4;
    CHECK(run_dma_case(&fixture_a, 101, FWLAB_C31_DMA_TO_CONTROLLER,
                       1, external, sizeof(external), &scenario, &intent,
                       NULL, NULL) == 0);
    CHECK(intent.result == FWLAB_C31_COMPLETION_TRANSFER_FAILURE);
    CHECK(intent.fault.effect_class == FWLAB_C31_EFFECT_NONE);
    CHECK(memcmp(controller, before, sizeof(before)) == 0);

    controller = c31_fake_dma_controller(&fixture_a.data_dma, 2);
    for (index = 0; index < 8; ++index) {
        controller[index] = (uint8_t)(0x80u + index);
    }
    memset(external, 0, sizeof(external));
    memset(&scenario, 0, sizeof(scenario));
    scenario.request.word[0] = 102;
    scenario.request.word[1] = ~UINT64_C(102);
    scenario.terminal = FWLAB_C31_PROVIDER_FAILED;
    scenario.effect_class = FWLAB_C31_EFFECT_EXACT_PREFIX;
    scenario.actual_prefix = 3;
    CHECK(run_dma_case(&fixture_a, 102, FWLAB_C31_DMA_FROM_CONTROLLER,
                       2, external, sizeof(external), &scenario, &intent,
                       NULL, &cap_index) == 0);
    capability = c31_fake_dma_external(&fixture_a.data_dma,
                                       (uint32_t)cap_index);
    CHECK(intent.fault.effect_class == FWLAB_C31_EFFECT_EXACT_PREFIX);
    CHECK(intent.fault.prefix_length == 3);
    CHECK(memcmp(capability, controller, 3) == 0);
    CHECK(capability[3] == 0);

    controller = c31_fake_dma_controller(&fixture_a.data_dma, 3);
    for (index = 0; index < 8; ++index) {
        controller[index] = (uint8_t)(0x40u + index);
    }
    memset(external, 0, sizeof(external));
    memset(&scenario, 0, sizeof(scenario));
    scenario.request.word[0] = 103;
    scenario.request.word[1] = ~UINT64_C(103);
    scenario.terminal = FWLAB_C31_PROVIDER_FAILED;
    scenario.effect_class = FWLAB_C31_EFFECT_UNKNOWN_PREFIX;
    scenario.actual_prefix = 2;
    scenario.reported_prefix = 5;
    CHECK(run_dma_case(&fixture_a, 103, FWLAB_C31_DMA_FROM_CONTROLLER,
                       3, external, sizeof(external), &scenario, &intent,
                       NULL, &cap_index) == 0);
    capability = c31_fake_dma_external(&fixture_a.data_dma,
                                       (uint32_t)cap_index);
    CHECK(intent.fault.effect_class == FWLAB_C31_EFFECT_UNKNOWN_PREFIX);
    CHECK(intent.fault.prefix_length == 5);
    CHECK(memcmp(capability, controller, 2) == 0);
    CHECK(capability[2] == 0);
    return 0;
}

static int reset_one_state(
    enum fwlab_c31_lifecycle_state target,
    uint64_t identifier
)
{
    struct c31_fake_scenario scenario;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_abort_ticket ticket;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;
    enum fwlab_c31_abort_outcome outcome;
    enum fwlab_c31_lifecycle_state state;
    unsigned int iteration;
    int ticket_valid = 0;
    int lease_valid = 0;

    CHECK(fixture_init(&fixture_a, UINT64_C(1000) + identifier, NULL));
    if (target == FWLAB_C31_CMD_ACCEPTED ||
        target == FWLAB_C31_CMD_COMPLETION_READY ||
        target == FWLAB_C31_CMD_COMPLETION_LEASED) {
        descriptor = descriptor_make(FWLAB_C31_PROVIDER_NONE, identifier);
    } else {
        scenario = scenario_make(identifier, FWLAB_C31_PROVIDER_SUCCESS);
        scenario.delay_polls = 4;
        if (target == FWLAB_C31_CMD_HELD) {
            scenario.backpressure_count = 4;
        }
        CHECK(c31_fake_provider_add(&fixture_a.dma, &scenario));
        descriptor = descriptor_make(FWLAB_C31_PROVIDER_DMA, identifier);
    }
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    if (target != FWLAB_C31_CMD_ACCEPTED) {
        CHECK(wait_state(&fixture_a, &command,
                         target == FWLAB_C31_CMD_CANCEL_PENDING ?
                             FWLAB_C31_CMD_RUNNING :
                         target == FWLAB_C31_CMD_COMPLETION_LEASED ?
                             FWLAB_C31_CMD_COMPLETION_READY : target,
                         32));
    }
    if (target == FWLAB_C31_CMD_CANCEL_PENDING) {
        CHECK(fwlab_c31_abort_request(fixture_a.instance, &command, &ticket,
                                      &outcome) == FWLAB_C31_API_OK);
        CHECK(outcome == FWLAB_C31_ABORT_PENDING);
        ticket_valid = 1;
    } else if (target == FWLAB_C31_CMD_COMPLETION_LEASED) {
        CHECK(fwlab_c31_completion_acquire(fixture_a.instance, &command,
                                           &lease, &intent) ==
              FWLAB_C31_API_OK);
        lease_valid = 1;
    }
    CHECK(fwlab_c31_command_state(fixture_a.instance, &command, &state) ==
          FWLAB_C31_API_OK);
    CHECK(state == target);
    CHECK(fwlab_c31_reset_begin(fixture_a.instance) == FWLAB_C31_API_OK);
    if (ticket_valid) {
        CHECK(fwlab_c31_abort_query(fixture_a.instance, &ticket, &outcome) ==
              FWLAB_C31_API_OK);
        CHECK(outcome == FWLAB_C31_ABORT_RESET_SUPERSEDED);
        CHECK(fwlab_c31_abort_ack(fixture_a.instance, &ticket) ==
              FWLAB_C31_API_OK);
    }
    for (iteration = 0; iteration < 64 &&
         fwlab_c31_phase(fixture_a.instance) ==
             FWLAB_C31_INSTANCE_RESET_DRAIN; ++iteration) {
        CHECK(step_once(&fixture_a));
    }
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_RESET_ACK);
    if (lease_valid) {
        CHECK(fwlab_c31_completion_consume(fixture_a.instance, &lease) ==
              FWLAB_C31_API_WRONG_STATE);
    }
    CHECK(fwlab_c31_reset_ack(fixture_a.instance) == FWLAB_C31_API_OK);
    CHECK(fwlab_c31_command_state(fixture_a.instance, &command, &state) ==
          FWLAB_C31_API_STALE_TOKEN);
    return 0;
}

static int test_reset_every_live_state(void)
{
    static const enum fwlab_c31_lifecycle_state states[] = {
        FWLAB_C31_CMD_ACCEPTED,
        FWLAB_C31_CMD_DISPATCHED,
        FWLAB_C31_CMD_HELD,
        FWLAB_C31_CMD_RUNNING,
        FWLAB_C31_CMD_CANCEL_PENDING,
        FWLAB_C31_CMD_COMPLETION_READY,
        FWLAB_C31_CMD_COMPLETION_LEASED,
    };
    unsigned int index;

    for (index = 0; index < sizeof(states) / sizeof(states[0]); ++index) {
        int line = reset_one_state(states[index], UINT64_C(200) + index);

        if (line != 0) {
            fprintf(stderr, "reset-state=%u failed at nested line %d\n",
                    (unsigned int)states[index], line);
            return line;
        }
    }
    return 0;
}

static int test_wrong_instance_event_cannot_quiesce(void)
{
    struct c31_fake_scenario scenario;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;
    enum fwlab_c31_lifecycle_state state;
    unsigned int iteration;
    uint32_t active_index;

    CHECK(fixture_init(&fixture_a, 12, NULL));
    scenario = scenario_make(300, FWLAB_C31_PROVIDER_SUCCESS);
    scenario.delay_polls = 1;
    CHECK(c31_fake_provider_add(&fixture_a.dma, &scenario));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_DMA, 300);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command, FWLAB_C31_CMD_RUNNING, 12));
    for (active_index = 0; active_index < C31_FAKE_MAX_ACTIVE;
         ++active_index) {
        if (fixture_a.dma.active[active_index].used) {
            break;
        }
    }
    CHECK(active_index < C31_FAKE_MAX_ACTIVE);
    fixture_a.dma.active[active_index]
        .request.operation.command.instance_nonce ^= UINT64_C(0x10000);
    for (iteration = 0; iteration < 4; ++iteration) {
        CHECK(step_once(&fixture_a));
    }
    CHECK(fwlab_c31_phase(fixture_a.instance) == FWLAB_C31_INSTANCE_READY);
    CHECK(fwlab_c31_command_state(fixture_a.instance, &command, &state) ==
          FWLAB_C31_API_OK);
    CHECK(state == FWLAB_C31_CMD_RUNNING);
    CHECK(fwlab_c31_reset_begin(fixture_a.instance) == FWLAB_C31_API_OK);
    for (iteration = 0; iteration < 16; ++iteration) {
        CHECK(step_once(&fixture_a));
    }
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_RESET_DRAIN);
    return 0;
}

static int test_nfc_provider_replacement(void)
{
    struct c31_fake_nfc_scenario scenario;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;

    CHECK(fixture_init_dedicated_nfc(&fixture_a, 13));
    memset(&scenario, 0, sizeof(scenario));
    scenario.request.word[0] = 400;
    scenario.request.word[1] = ~UINT64_C(400);
    scenario.backpressure_count = 1;
    scenario.delay_polls = 1;
    scenario.terminal = FWLAB_C31_PROVIDER_SUCCESS;
    scenario.cancel_wins = true;
    scenario.fault.effect_class = FWLAB_C31_EFFECT_NONE;
    CHECK(c31_fake_nfc_add(&fixture_a.dedicated_nfc, &scenario));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_NFC, 400);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command,
                     FWLAB_C31_CMD_COMPLETION_READY, 24));
    CHECK(consume_completion(&fixture_a, &command,
                             FWLAB_C31_COMPLETION_SUCCESS,
                             FWLAB_C31_EFFECT_NONE) == 0);
    return 0;
}

static int test_reset_allows_effect_but_blocks_completion(void)
{
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;
    struct c31_fake_dma_scenario scenario;
    uint8_t external[C31_FAKE_DMA_BYTES];
    uint8_t *controller;
    unsigned int index;
    unsigned int iteration;

    CHECK(fixture_init_data_dma(&fixture_a, 32));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_DMA, 620);
    descriptor.length = 8;
    for (index = 0; index < sizeof(external); ++index) {
        external[index] = (uint8_t)(0xc0u + (index & 0x1fu));
    }
    CHECK(c31_fake_dma_register(
              &fixture_a.data_dma, &descriptor.capability,
              &descriptor.origin, fixture_a.nonce, 1,
              FWLAB_C31_DMA_TO_CONTROLLER, external, sizeof(external)) >= 0);
    memset(&scenario, 0, sizeof(scenario));
    scenario.request = descriptor.provider_request;
    scenario.delay_polls = 2;
    scenario.terminal = FWLAB_C31_PROVIDER_SUCCESS;
    scenario.effect_class = FWLAB_C31_EFFECT_FULL;
    scenario.cancel_wins = false;
    CHECK(c31_fake_dma_add(&fixture_a.data_dma, &scenario));
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command, FWLAB_C31_CMD_RUNNING, 12));
    CHECK(fwlab_c31_reset_begin(fixture_a.instance) == FWLAB_C31_API_OK);
    for (iteration = 0; iteration < 32 &&
         fwlab_c31_phase(fixture_a.instance) ==
             FWLAB_C31_INSTANCE_RESET_DRAIN; ++iteration) {
        CHECK(step_once(&fixture_a));
    }
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_RESET_ACK);
    controller = c31_fake_dma_controller(&fixture_a.data_dma, 0);
    CHECK(memcmp(controller, external, 8) == 0);
    CHECK(fwlab_c31_completion_acquire(fixture_a.instance, &command, &lease,
                                       &intent) ==
          FWLAB_C31_API_WRONG_STATE);
    CHECK(fwlab_c31_reset_ack(fixture_a.instance) == FWLAB_C31_API_OK);
    CHECK(fwlab_c31_completion_acquire(fixture_a.instance, &command, &lease,
                                       &intent) ==
          FWLAB_C31_API_STALE_TOKEN);
    return 0;
}

static int test_reduced_counters_and_capacity(void)
{
    struct fwlab_c31_capacity capacity;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command1;
    struct fwlab_c31_command_handle command2;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;
    struct fwlab_c31_abort_ticket ticket;
    enum fwlab_c31_abort_outcome outcome;
    struct c31_fake_scenario scenario;

    capacity_default(&capacity);
    capacity.commands = 1;
    capacity.abort_tickets = 1;
    CHECK(fixture_init(&fixture_a, 20, &capacity));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_NONE, 500);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command1) ==
          FWLAB_C31_API_OK);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command2) ==
          FWLAB_C31_API_NO_CAPACITY);
    CHECK(fwlab_c31_phase(fixture_a.instance) == FWLAB_C31_INSTANCE_READY);

    capacity_default(&capacity);
    capacity.commands = 1;
    capacity.abort_tickets = 1;
    capacity.command_uid_limit = 1;
    CHECK(fixture_init(&fixture_a, 21, &capacity));
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command1) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command1,
                     FWLAB_C31_CMD_COMPLETION_READY, 4));
    CHECK(consume_completion(&fixture_a, &command1,
                             FWLAB_C31_COMPLETION_SUCCESS,
                             FWLAB_C31_EFFECT_NONE) == 0);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command2) ==
          FWLAB_C31_API_COUNTER_EXHAUSTED);
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_FAULTED);

    capacity_default(&capacity);
    capacity.commands = 1;
    capacity.abort_tickets = 1;
    capacity.operation_generation_limit = 1;
    CHECK(fixture_init(&fixture_a, 22, &capacity));
    scenario = scenario_make(501, FWLAB_C31_PROVIDER_SUCCESS);
    CHECK(c31_fake_provider_add(&fixture_a.dma, &scenario));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_DMA, 501);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command1) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command1,
                     FWLAB_C31_CMD_COMPLETION_READY, 12));
    CHECK(consume_completion(&fixture_a, &command1,
                             FWLAB_C31_COMPLETION_SUCCESS,
                             FWLAB_C31_EFFECT_FULL) == 0);
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_DMA, 502);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command2) ==
          FWLAB_C31_API_COUNTER_EXHAUSTED);
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_FAULTED);

    capacity_default(&capacity);
    capacity.lease_generation_limit = 1;
    CHECK(fixture_init(&fixture_a, 23, &capacity));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_NONE, 503);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command1) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command1,
                     FWLAB_C31_CMD_COMPLETION_READY, 4));
    CHECK(fwlab_c31_completion_acquire(fixture_a.instance, &command1, &lease,
                                       &intent) == FWLAB_C31_API_OK);
    CHECK(fwlab_c31_completion_release(fixture_a.instance, &lease) ==
          FWLAB_C31_API_OK);
    CHECK(fwlab_c31_completion_acquire(fixture_a.instance, &command1, &lease,
                                       &intent) ==
          FWLAB_C31_API_COUNTER_EXHAUSTED);
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_FAULTED);

    capacity_default(&capacity);
    capacity.commands = 1;
    capacity.abort_tickets = 1;
    capacity.ticket_generation_limit = 1;
    CHECK(fixture_init(&fixture_a, 24, &capacity));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_NONE, 504);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command1) ==
          FWLAB_C31_API_OK);
    CHECK(fwlab_c31_abort_request(fixture_a.instance, &command1, &ticket,
                                  &outcome) == FWLAB_C31_API_OK);
    CHECK(fwlab_c31_abort_ack(fixture_a.instance, &ticket) ==
          FWLAB_C31_API_OK);
    CHECK(consume_completion(&fixture_a, &command1,
                             FWLAB_C31_COMPLETION_ABORTED,
                             FWLAB_C31_EFFECT_NONE) == 0);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command2) ==
          FWLAB_C31_API_OK);
    CHECK(fwlab_c31_abort_request(fixture_a.instance, &command2, &ticket,
                                  &outcome) ==
          FWLAB_C31_API_COUNTER_EXHAUSTED);
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_FAULTED);

    capacity_default(&capacity);
    capacity.controller_epoch_limit = 1;
    CHECK(fixture_init(&fixture_a, 25, &capacity));
    CHECK(fwlab_c31_reset_begin(fixture_a.instance) ==
          FWLAB_C31_API_COUNTER_EXHAUSTED);
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_FAULTED);
    return 0;
}

static int test_two_instances_and_wrap(void)
{
    struct fwlab_c31_capacity capacity;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command_a;
    struct fwlab_c31_command_handle command_b;
    enum fwlab_c31_lifecycle_state state;

    CHECK(fixture_init(&fixture_a, 7, NULL));
    CHECK(fixture_init(&fixture_b, 8, NULL));
    descriptor = descriptor_make(FWLAB_C31_PROVIDER_NONE, 60);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command_a) ==
          FWLAB_C31_API_OK);
    CHECK(fwlab_c31_submit(fixture_b.instance, &descriptor, &command_b) ==
          FWLAB_C31_API_OK);
    CHECK(command_a.slot == command_b.slot);
    CHECK(command_a.instance_nonce != command_b.instance_nonce);
    CHECK(fwlab_c31_command_state(fixture_a.instance, &command_b, &state) ==
          FWLAB_C31_API_STALE_TOKEN);
    CHECK(wait_state(&fixture_a, &command_a,
                     FWLAB_C31_CMD_COMPLETION_READY, 4));
    CHECK(wait_state(&fixture_b, &command_b,
                     FWLAB_C31_CMD_COMPLETION_READY, 4));
    CHECK(consume_completion(&fixture_a, &command_a,
                             FWLAB_C31_COMPLETION_SUCCESS,
                             FWLAB_C31_EFFECT_NONE) == 0);
    CHECK(consume_completion(&fixture_b, &command_b,
                             FWLAB_C31_COMPLETION_SUCCESS,
                             FWLAB_C31_EFFECT_NONE) == 0);

    capacity_default(&capacity);
    capacity.commands = 1;
    capacity.abort_tickets = 1;
    capacity.slot_generation_limit = 1;
    CHECK(fixture_init(&fixture_a, 9, &capacity));
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command_a) ==
          FWLAB_C31_API_OK);
    CHECK(wait_state(&fixture_a, &command_a,
                     FWLAB_C31_CMD_COMPLETION_READY, 4));
    CHECK(consume_completion(&fixture_a, &command_a,
                             FWLAB_C31_COMPLETION_SUCCESS,
                             FWLAB_C31_EFFECT_NONE) == 0);
    CHECK(fwlab_c31_submit(fixture_a.instance, &descriptor, &command_a) ==
          FWLAB_C31_API_COUNTER_EXHAUSTED);
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_FAULTED);
    return 0;
}

static int test_teardown_and_trace(void)
{
    struct fwlab_c31_trace_entry entry;

    CHECK(fixture_init(&fixture_a, 10, NULL));
    CHECK(fwlab_c31_trace_count(fixture_a.instance) == 1);
    CHECK(fwlab_c31_trace_read(fixture_a.instance, 0, &entry) ==
          FWLAB_C31_API_OK);
    CHECK(entry.kind == FWLAB_C31_TRACE_INIT);
    CHECK(fwlab_c31_teardown_begin(fixture_a.instance) == FWLAB_C31_API_OK);
    CHECK(fwlab_c31_phase(fixture_a.instance) ==
          FWLAB_C31_INSTANCE_TEARDOWN_ACK);
    CHECK(fwlab_c31_teardown_ack(fixture_a.instance) == FWLAB_C31_API_OK);
    CHECK(fwlab_c31_phase(fixture_a.instance) == FWLAB_C31_INSTANCE_DEAD);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        {"codec_literal", test_codec_literal},
        {"submit_and_policy_lease", test_submit_and_policy_lease},
        {"immutable_ownership_and_missing_provider",
         test_immutable_ownership_and_missing_provider},
        {"backpressure_success_and_reject",
         test_backpressure_success_and_reject},
        {"abort_before_and_after_accept",
         test_abort_before_and_after_accept},
        {"abort_race_normal_wins", test_abort_race_normal_wins},
        {"reset_and_stale_lease", test_reset_and_stale_lease},
        {"reset_running_and_abort_ack", test_reset_running_and_abort_ack},
        {"duplicate_event_faults", test_duplicate_event_faults},
        {"dma_effects_and_capabilities", test_dma_effects_and_capabilities},
        {"reset_every_live_state", test_reset_every_live_state},
        {"wrong_instance_event_cannot_quiesce",
         test_wrong_instance_event_cannot_quiesce},
        {"nfc_provider_replacement", test_nfc_provider_replacement},
        {"reset_allows_effect_but_blocks_completion",
         test_reset_allows_effect_but_blocks_completion},
        {"reduced_counters_and_capacity",
         test_reduced_counters_and_capacity},
        {"two_instances_and_wrap", test_two_instances_and_wrap},
        {"teardown_and_trace", test_teardown_and_trace},
    };
    unsigned int index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        int line = tests[index].run();

        if (line != 0) {
            fprintf(stderr, "C3.1 test %s failed at line %d\n",
                    tests[index].name, line);
            return 1;
        }
    }
    printf("C3.1 unit tests: PASS (%u cases)\n",
           (unsigned int)(sizeof(tests) / sizeof(tests[0])));
    return 0;
}
