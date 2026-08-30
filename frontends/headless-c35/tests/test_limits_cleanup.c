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

static void uuid_make(uint8_t uuid[16], enum c35_lane lane)
{
    unsigned int index;

    for (index = 0; index < 16; ++index)
        uuid[index] = (uint8_t)(0xa5u + index * 13u + (unsigned int)lane);
}

static int nfc_cleanup_epoch17(const struct c35_runtime *runtime)
{
    uint32_t count = fwlab_nfc_model_trace_count(runtime->nfc_model);
    uint32_t resets = 0;
    uint32_t last_old_epoch = 0;
    uint32_t index;

    for (index = 0; index < count; ++index) {
        struct fwlab_nfc_trace_entry entry;

        if (fwlab_nfc_model_trace_read(
                runtime->nfc_model, index, &entry) != FWLAB_NFC_API_OK)
            return 0;
        if (entry.kind == FWLAB_NFC_TRACE_RESET) {
            ++resets;
            last_old_epoch = entry.detail;
        }
    }
    return resets == 16 && last_old_epoch == 16;
}

static int test_reset_boundary(enum c35_lane lane)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct c35_fault_lifecycle counter;
    struct c35_operation_token token;
    struct c35_operation_status status;
    struct c35_publication publication;
    struct c35_request request = c35_request_read(0);
    struct c35_semantic_result semantic;
    uint8_t uuid[16];
    unsigned int reset;
    int ok;

    CHECK(storage != NULL && runtime != NULL);
    uuid_make(uuid, lane);
    CHECK(c35_storage_init(storage, lane, uuid));
    CHECK(c35_runtime_init(
        runtime, storage, lane,
        UINT64_C(0x35a2000000000000) | (uint64_t)lane,
        UINT64_C(0x9182736455463728), 0, 0, 0x35a20000u + lane));
    CHECK(c35_fault_lifecycle_init(
        &counter, &runtime->headless.lifecycle,
        C35_LIFECYCLE_CUT_RESET_BEGIN, C35_FAULT_BEFORE_EFFECT,
        UINT32_MAX, FWLAB_C31_API_NO_CAPACITY));
    runtime->headless.lifecycle = c35_fault_lifecycle_port(&counter);
    for (reset = 1; reset <= 15; ++reset) {
        memset(&publication, 0, sizeof(publication));
        CHECK(c35_headless_reset_observed(
            &runtime->headless, 16384, &publication) == C35_OK);
        CHECK(runtime->headless.owner_epoch == reset + 1u);
        CHECK(publication.kind == C35_PUBLICATION_RESET &&
              publication.epoch == reset + 1u &&
              publication.commit_state == C35_COMMIT_COMMITTED);
    }
    CHECK(counter.calls[C35_LIFECYCLE_CUT_RESET_BEGIN] == 15);
    CHECK(fwlab_c31_phase(runtime->lifecycle) == FWLAB_C31_INSTANCE_READY);
    CHECK(c35_reset_start(&runtime->headless, &token) == C35_OK);
    CHECK(c35_operation_finalize(
        &runtime->headless, &token, &status) == C35_OK);
    CHECK(status.outcome == C35_COUNTER_EXHAUSTED &&
          status.commit_state == C35_COMMIT_NOT_STARTED &&
          status.cleanup_state == C35_CLEANUP_NONE &&
          status.service_phase == C35_SERVICE_READY &&
          !status.publication_valid);
    CHECK(counter.calls[C35_LIFECYCLE_CUT_RESET_BEGIN] == 15);
    CHECK(fwlab_c31_phase(runtime->lifecycle) == FWLAB_C31_INSTANCE_READY);
    CHECK(c35_operation_retire(&runtime->headless, &token) == C35_OK);
    CHECK(c35_run_command(runtime, &request, &semantic));
    ok = c35_runtime_teardown(runtime) && !runtime->claimed;
    if (lane != C35_LANE_SCRIPTED)
        ok = nfc_cleanup_epoch17(runtime) && ok;
    ok = c35_storage_close(storage) && ok;
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

enum low_limit_kind {
    LOW_COMMAND_UID = 0,
    LOW_SLOT_GENERATION = 1,
    LOW_OPERATION_GENERATION = 2,
    LOW_LEASE_GENERATION = 3,
    LOW_TICKET_GENERATION = 4
};

struct low_fixture {
    void *arena;
    struct c31_fake_dma_context dma;
    struct c31_fake_provider_context nfc;
    struct c35_scripted_binding scripted;
    struct fwlab_c31 *lifecycle;
    struct c35_lifecycle_port port;
    struct c35_headless headless;
};

static struct fwlab_c31_capacity low_capacity(enum low_limit_kind kind)
{
    struct fwlab_c31_capacity capacity;

    memset(&capacity, 0, sizeof(capacity));
    capacity.version = FWLAB_C31_CONTRACT_VERSION;
    capacity.size = sizeof(capacity);
    capacity.commands = 1;
    capacity.abort_tickets = 1;
    capacity.event_batch = 2;
    capacity.trace_entries = 64;
    capacity.scratch_bytes = 128;
    capacity.slot_generation_limit = kind == LOW_SLOT_GENERATION ? 1 : 64;
    capacity.operation_generation_limit =
        kind == LOW_OPERATION_GENERATION ? 1 : 64;
    capacity.lease_generation_limit = kind == LOW_LEASE_GENERATION ? 1 : 64;
    capacity.ticket_generation_limit = kind == LOW_TICKET_GENERATION ? 1 : 64;
    capacity.controller_epoch_limit = 16;
    capacity.command_uid_limit = kind == LOW_COMMAND_UID ? 1 : 64;
    return capacity;
}

static int low_open(struct low_fixture *fixture, enum low_limit_kind kind)
{
    struct fwlab_c31_capacity capacity = low_capacity(kind);
    struct fwlab_c31_provider_set providers;
    struct c35_binding binding;
    size_t arena_size = fwlab_c31_arena_size(&capacity);
    uint64_t nonce = UINT64_C(0x35a3000000000000) | (uint64_t)kind;

    memset(fixture, 0, sizeof(*fixture));
    fixture->arena = calloc(1, arena_size);
    if (fixture->arena == NULL) return 0;
    c31_fake_dma_init(&fixture->dma);
    c31_fake_provider_init(&fixture->nfc, FWLAB_C31_PROVIDER_NFC);
    memset(&providers, 0, sizeof(providers));
    providers.dma = c31_fake_dma_provider(&fixture->dma);
    providers.nfc = c31_fake_provider(&fixture->nfc);
    if (c35_scripted_binding_init(
            &fixture->scripted, &fixture->nfc, nonce, 1) != C35_OK)
        return 0;
    binding = c35_scripted_binding_provider(&fixture->scripted);
    if (fwlab_c31_init(
            fixture->arena, arena_size, &capacity, nonce, &providers,
            &fixture->lifecycle) != FWLAB_C31_API_OK) return 0;
    fixture->port = c35_lifecycle_port_native(fixture->lifecycle);
    return c35_headless_init(
               &fixture->headless, &fixture->port, &binding, nonce, 1, 16,
               64, 64, 0) == C35_OK;
}

static int low_run_success(struct low_fixture *fixture)
{
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_intent intent;
    struct c35_semantic_result semantic;

    return c35_headless_submit(&fixture->headless, &request, &command) ==
               C35_OK &&
           c35_headless_complete(
               &fixture->headless, &command, &semantic, &intent) == C35_OK;
}

static int low_teardown(struct low_fixture *fixture)
{
    int ok = fixture->headless.service_phase == C35_SERVICE_DEAD ||
             c35_headless_teardown(&fixture->headless, 16384) == C35_OK;

    ok = ok && fwlab_c31_phase(fixture->lifecycle) ==
                   FWLAB_C31_INSTANCE_DEAD;

    free(fixture->arena);
    memset(fixture, 0, sizeof(*fixture));
    return ok;
}

static int test_runtime_limit(enum low_limit_kind kind)
{
    struct low_fixture fixture;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    struct c35_operation_token token;
    struct c35_operation_status status;
    unsigned int iteration;

    CHECK(kind != LOW_TICKET_GENERATION);
    CHECK(low_open(&fixture, kind));
    CHECK(low_run_success(&fixture));
    if (kind == LOW_COMMAND_UID || kind == LOW_SLOT_GENERATION ||
        kind == LOW_OPERATION_GENERATION) {
        CHECK(c35_headless_submit(
            &fixture.headless, &request, &command) == C35_COUNTER_EXHAUSTED);
    } else {
        CHECK(c35_headless_submit(
            &fixture.headless, &request, &command) == C35_OK);
        CHECK(c35_completion_start(&fixture.headless, &command, &token) ==
              C35_OK);
        for (iteration = 0; iteration < 64; ++iteration) {
            enum c35_result result = c35_operation_progress(
                &fixture.headless, &token, 1, &status);

            CHECK(result == C35_OK || result == C35_IN_PROGRESS);
            if (fixture.headless.service_phase ==
                C35_SERVICE_FAULTED_CLEANUP) break;
        }
        CHECK(iteration < 64);
    }
    CHECK(fixture.headless.service_phase == C35_SERVICE_FAULTED_CLEANUP);
    CHECK(fwlab_c31_phase(fixture.lifecycle) == FWLAB_C31_INSTANCE_FAULTED);
    CHECK(c35_submit_start(&fixture.headless, &request, &token) ==
          C35_WRONG_STATE);
    CHECK(c35_reset_start(&fixture.headless, &token) == C35_OK);
    CHECK(c35_operation_finalize(&fixture.headless, &token, &status) ==
          C35_IN_PROGRESS);
    CHECK(c35_teardown_start(&fixture.headless, &token) == C35_OK);
    for (iteration = 0; iteration < 16384; ++iteration) {
        enum c35_result result = c35_operation_progress(
            &fixture.headless, &token, 1, &status);

        CHECK(result == C35_OK || result == C35_IN_PROGRESS);
        if (result == C35_OK) break;
    }
    CHECK(iteration < 16384 && status.outcome == C35_OK &&
          status.commit_state == C35_COMMIT_COMMITTED &&
          status.cleanup_state == C35_CLEANUP_COMPLETE);
    CHECK(c35_operation_retire(&fixture.headless, &token) == C35_OK);
    CHECK(low_teardown(&fixture));
    return 1;
}

static enum c35_result rollback_once(
    struct low_fixture *fixture,
    struct c35_fault_binding *fault
)
{
    struct c35_binding native = c35_scripted_binding_provider(
        &fixture->scripted);
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;
    enum c35_result result;

    if (!c35_fault_binding_init(
            fault, &native, C35_BINDING_CUT_REGISTRATION_COMMIT,
            C35_FAULT_BEFORE_EFFECT, 1, C35_NO_CAPACITY)) return C35_INVALID;
    fixture->headless.binding = c35_fault_binding_provider(fault);
    result = c35_headless_submit(&fixture->headless, &request, &command);
    fixture->headless.binding = native;
    return fault->injected ? result : C35_INVALID;
}

static int test_ticket_limit(void)
{
    struct low_fixture fixture;
    struct c35_fault_binding first;
    struct c35_fault_binding second;

    CHECK(low_open(&fixture, LOW_TICKET_GENERATION));
    CHECK(rollback_once(&fixture, &first) == C35_NO_CAPACITY);
    CHECK(rollback_once(&fixture, &second) == C35_COUNTER_EXHAUSTED);
    CHECK(fixture.headless.service_phase == C35_SERVICE_FAULTED_CLEANUP);
    CHECK(fwlab_c31_phase(fixture.lifecycle) == FWLAB_C31_INSTANCE_FAULTED);
    CHECK(low_teardown(&fixture));
    return 1;
}

static int volatile_credits_zero(const struct c35_c34_binding *binding)
{
    return binding->inner_used == 0 && binding->physical_used == 0 &&
           binding->physical_sequence_used == 0 && binding->cache_used == 0 &&
           binding->nfc_uid_used == 0 && binding->nfc_generation_used == 0 &&
           binding->nfc_submit_used == 0 && binding->nfc_trace_used == 0 &&
           binding->nfc_tick_used == 0;
}

static int test_credit_guards(void)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct c35_fault_lifecycle counter;
    struct c35_fault_lifecycle submit_fault;
    struct c35_fault_binding commit_fault;
    struct c35_lifecycle_port native_lifecycle;
    struct c35_binding native_binding;
    struct c35_request request;
    struct c35_semantic_result semantic;
    struct fwlab_c31_command_handle command;
    uint64_t inner_before;
    uint64_t physical_before;
    uint32_t sequence_before;
    uint8_t value[C35_ATOM_BYTES];
    uint8_t uuid[16];
    int ok;

    CHECK(storage != NULL && runtime != NULL);
    uuid_make(uuid, C35_LANE_MEMORY);
    memset(value, 0x6d, sizeof(value));
    CHECK(c35_storage_init(storage, C35_LANE_MEMORY, uuid));
    CHECK(c35_runtime_init(
        runtime, storage, C35_LANE_MEMORY, UINT64_C(0x35a7000000000001),
        UINT64_C(0x13579bdf2468ace0), 0, 0, 0x35a70001));
    native_lifecycle = runtime->headless.lifecycle;
    native_binding = runtime->headless.binding;
    CHECK(c35_fault_lifecycle_init(
        &counter, &native_lifecycle, C35_LIFECYCLE_CUT_SUBMIT,
        C35_FAULT_BEFORE_EFFECT, UINT32_MAX, FWLAB_C31_API_NO_CAPACITY));
    runtime->headless.lifecycle = c35_fault_lifecycle_port(&counter);
    inner_before = runtime->firmware->next_inner_uid;
    physical_before = runtime->firmware->next_physical_op_id;
    sequence_before = runtime->firmware->next_physical_sequence;
    request = c35_request_trim(
        0, FWLAB_PERSIST_SELF_DURABLE, 1);

    runtime->c34_binding.physical_used = C35_C34_PHYSICAL_CREDIT_LIMIT;
    CHECK(c35_headless_submit(&runtime->headless, &request, &command) ==
          C35_COUNTER_EXHAUSTED);
    CHECK(counter.calls[C35_LIFECYCLE_CUT_SUBMIT] == 0);
    CHECK(runtime->firmware->next_inner_uid == inner_before &&
          runtime->firmware->next_physical_op_id == physical_before &&
          runtime->firmware->next_physical_sequence == sequence_before);
    runtime->c34_binding.physical_used = 0;
    runtime->c34_binding.physical_sequence_used =
        C35_C34_PHYSICAL_CREDIT_LIMIT;
    CHECK(c35_headless_submit(&runtime->headless, &request, &command) ==
          C35_COUNTER_EXHAUSTED);
    CHECK(counter.calls[C35_LIFECYCLE_CUT_SUBMIT] == 0);
    runtime->c34_binding.physical_sequence_used = 0;
    storage->credits.record_used = C35_C34_RECORD_CREDIT_LIMIT;
    CHECK(c35_headless_submit(&runtime->headless, &request, &command) ==
          C35_MEDIA_CAPACITY);
    CHECK(counter.calls[C35_LIFECYCLE_CUT_SUBMIT] == 0);
    storage->credits.record_used = 0;
    storage->credits.known = 0;
    request = c35_request_write(
        0, FWLAB_PERSIST_SELF_DURABLE, 1, value);
    CHECK(c35_headless_submit(&runtime->headless, &request, &command) ==
          C35_MEDIA_CAPACITY);
    storage->credits.known = 1;
    CHECK(volatile_credits_zero(&runtime->c34_binding));

    CHECK(c35_fault_lifecycle_init(
        &submit_fault, &native_lifecycle, C35_LIFECYCLE_CUT_SUBMIT,
        C35_FAULT_BEFORE_EFFECT, 1, FWLAB_C31_API_NO_CAPACITY));
    runtime->headless.lifecycle = c35_fault_lifecycle_port(&submit_fault);
    CHECK(c35_headless_submit(&runtime->headless, &request, &command) ==
          C35_NO_CAPACITY);
    CHECK(submit_fault.injected &&
          volatile_credits_zero(&runtime->c34_binding) &&
          storage->credits.record_used == 0);
    runtime->headless.lifecycle = native_lifecycle;

    CHECK(c35_fault_binding_init(
        &commit_fault, &native_binding, C35_BINDING_CUT_REGISTRATION_COMMIT,
        C35_FAULT_BEFORE_EFFECT, 1, C35_NO_CAPACITY));
    runtime->headless.binding = c35_fault_binding_provider(&commit_fault);
    CHECK(c35_headless_submit(&runtime->headless, &request, &command) ==
          C35_NO_CAPACITY);
    CHECK(commit_fault.injected &&
          volatile_credits_zero(&runtime->c34_binding) &&
          storage->credits.record_used == 0);
    runtime->headless.binding = native_binding;

    CHECK(c35_run_command(runtime, &request, &semantic));
    CHECK(runtime->c34_binding.inner_used == 4 &&
          runtime->c34_binding.physical_used == 2 &&
          runtime->c34_binding.physical_sequence_used == 2 &&
          runtime->c34_binding.cache_used == 2 &&
          runtime->c34_binding.nfc_uid_used == 4 &&
          runtime->c34_binding.nfc_generation_used == 4 &&
          runtime->c34_binding.nfc_submit_used == 4 &&
          runtime->c34_binding.nfc_trace_used == 36 &&
          runtime->c34_binding.nfc_tick_used == 24 &&
          storage->credits.record_used == 2);
    CHECK(c35_runtime_teardown(runtime));
    CHECK(c35_storage_restart(storage));
    CHECK(c35_runtime_init(
        runtime, storage, C35_LANE_MEMORY, UINT64_C(0x35a7000000000001),
        UINT64_C(0x13579bdf2468ace0), 0, 0, 0x35a70002));
    CHECK(volatile_credits_zero(&runtime->c34_binding) &&
          storage->credits.record_used == 2);
    ok = c35_runtime_teardown(runtime) && c35_storage_close(storage);
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

static int test_c35_uid_limits(void)
{
    struct low_fixture fixture;
    struct c35_request request = c35_request_read(0);
    struct fwlab_c31_command_handle command;

    CHECK(low_open(&fixture, LOW_COMMAND_UID));
    fixture.headless.request_uid_limit = 1;
    fixture.headless.operation_uid_limit = 8;
    CHECK(low_run_success(&fixture));
    CHECK(c35_headless_submit(&fixture.headless, &request, &command) ==
          C35_COUNTER_EXHAUSTED);
    CHECK(fwlab_c31_phase(fixture.lifecycle) == FWLAB_C31_INSTANCE_READY);
    CHECK(low_teardown(&fixture));

    CHECK(low_open(&fixture, LOW_COMMAND_UID));
    fixture.headless.operation_uid_limit = 1;
    CHECK(c35_headless_submit(&fixture.headless, &request, &command) ==
          C35_OK);
    {
        struct c35_operation_token token;

        CHECK(c35_completion_start(&fixture.headless, &command, &token) ==
          C35_COUNTER_EXHAUSTED);
    }
    CHECK(fixture.headless.service_phase == C35_SERVICE_READY);
    CHECK(low_teardown(&fixture));
    return 1;
}

static int test_scripted_scenario_limit(void)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct c35_fault_lifecycle counter;
    struct c35_request request = c35_request_read(0);
    struct c35_semantic_result semantic;
    struct fwlab_c31_command_handle command;
    uint8_t uuid[16];
    unsigned int index;
    int ok;

    CHECK(storage != NULL && runtime != NULL);
    uuid_make(uuid, C35_LANE_SCRIPTED);
    CHECK(c35_storage_init(storage, C35_LANE_SCRIPTED, uuid));
    CHECK(c35_runtime_init(
        runtime, storage, C35_LANE_SCRIPTED,
        UINT64_C(0x35a8000000000001), UINT64_C(0x0102030405060708),
        0, 0, 0x35a80001));
    CHECK(c35_fault_lifecycle_init(
        &counter, &runtime->headless.lifecycle,
        C35_LIFECYCLE_CUT_SUBMIT, C35_FAULT_BEFORE_EFFECT,
        UINT32_MAX, FWLAB_C31_API_NO_CAPACITY));
    runtime->headless.lifecycle = c35_fault_lifecycle_port(&counter);
    for (index = 0; index < C31_FAKE_MAX_SCENARIOS; ++index)
        CHECK(c35_run_command(runtime, &request, &semantic));
    CHECK(counter.calls[C35_LIFECYCLE_CUT_SUBMIT] ==
          C31_FAKE_MAX_SCENARIOS);
    CHECK(c35_headless_submit(&runtime->headless, &request, &command) ==
          C35_COUNTER_EXHAUSTED);
    CHECK(counter.calls[C35_LIFECYCLE_CUT_SUBMIT] ==
          C31_FAKE_MAX_SCENARIOS);
    CHECK(runtime->headless.service_phase == C35_SERVICE_READY);
    runtime->dma_next = 513;
    {
        struct c35_operation_token token;
        uint8_t input[C35_ATOM_BYTES] = {0};

        CHECK(c35_dma_start(runtime, input, &token) ==
              C35_COUNTER_EXHAUSTED);
    }
    ok = c35_runtime_teardown(runtime) && c35_storage_close(storage);
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

static int maintenance_checkpoint(struct c35_runtime *runtime)
{
    struct c35_c34_binding *binding = &runtime->c34_binding;
    struct c35_persistent_credits *persistent = binding->persistent;

    if (binding->inner_used + 5u > C35_C34_INNER_CREDIT_LIMIT ||
        binding->physical_used + 3u > C35_C34_PHYSICAL_CREDIT_LIMIT ||
        binding->physical_sequence_used + 3u >
            C35_C34_PHYSICAL_CREDIT_LIMIT ||
        binding->cache_used + 2u > C35_C34_CACHE_CREDIT_LIMIT ||
        persistent->record_used + 2u > C35_C34_RECORD_CREDIT_LIMIT ||
        binding->nfc_uid_used + 5u > 32u ||
        binding->nfc_generation_used + 5u > 32u ||
        binding->nfc_submit_used + 5u > 32u ||
        binding->nfc_trace_used + 45u > 288u ||
        binding->nfc_tick_used + 30u > 192u)
        return 0;
    binding->inner_used += 5;
    binding->physical_used += 3;
    binding->physical_sequence_used += 3;
    binding->cache_used += 2;
    binding->nfc_uid_used += 5;
    binding->nfc_generation_used += 5;
    binding->nfc_submit_used += 5;
    binding->nfc_trace_used += 45;
    binding->nfc_tick_used += 30;
    persistent->record_used += 2;
    return c34_checkpoint_start(runtime->firmware) == C34_OK &&
           c35_headless_pump_quiescent(
               &runtime->headless, 8192) == C35_OK;
}

static int test_live_physical_boundary(enum c35_lane lane)
{
    static const uint8_t atoms[5] = {0, 1, 0, 0, 1};
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct c35_fault_lifecycle counter;
    struct c35_request request;
    struct c35_semantic_result semantic;
    struct c35_operation_token operation;
    struct c35_operation_status status;
    uint8_t value[C35_ATOM_BYTES];
    uint8_t uuid[16];
    uint64_t inner_before;
    uint64_t physical_before;
    uint32_t sequence_before;
    unsigned int index;
    unsigned int progress;
    int ok;

    CHECK(storage != NULL && runtime != NULL);
    uuid_make(uuid, lane);
    CHECK(c35_storage_init(storage, lane, uuid));
    CHECK(c35_runtime_init(
        runtime, storage, lane,
        UINT64_C(0x35ae000000000000) | (uint64_t)lane,
        UINT64_C(0xa1b2c3d4e5f60718), 0, 0, 0x35ae0000u + lane));
    for (index = 0; index < 5; ++index) {
        memset(value, (int)(0x50u + index), sizeof(value));
        request = c35_request_write(
            atoms[index], FWLAB_PERSIST_SELF_DURABLE,
            index + 1u, value);
        CHECK(c35_run_command(runtime, &request, &semantic));
        CHECK(semantic.status == C34_COMMAND_SUCCESS);
        if (index == 3 || index == 4)
            CHECK(maintenance_checkpoint(runtime));
    }
    CHECK(runtime->c34_binding.inner_used == 30 &&
          runtime->c34_binding.physical_used == 16 &&
          runtime->c34_binding.physical_sequence_used == 16 &&
          runtime->c34_binding.cache_used == 14 &&
          runtime->c34_binding.nfc_uid_used == 30 &&
          runtime->c34_binding.nfc_generation_used == 30 &&
          runtime->c34_binding.nfc_submit_used == 30 &&
          runtime->c34_binding.nfc_trace_used == 270 &&
          runtime->c34_binding.nfc_tick_used == 180 &&
          storage->credits.record_used == 14);
    CHECK(runtime->firmware->next_inner_uid == 31 &&
          runtime->firmware->next_physical_op_id == 17 &&
          runtime->firmware->next_physical_sequence == 17 &&
          runtime->firmware->next_record_id == 15);
    inner_before = runtime->firmware->next_inner_uid;
    physical_before = runtime->firmware->next_physical_op_id;
    sequence_before = runtime->firmware->next_physical_sequence;
    CHECK(c35_fault_lifecycle_init(
        &counter, &runtime->headless.lifecycle,
        C35_LIFECYCLE_CUT_SUBMIT, C35_FAULT_BEFORE_EFFECT,
        UINT32_MAX, FWLAB_C31_API_NO_CAPACITY));
    runtime->headless.lifecycle = c35_fault_lifecycle_port(&counter);
    request = c35_request_trim(
        0, FWLAB_PERSIST_SELF_DURABLE, 6);
    CHECK(c35_submit_start(&runtime->headless, &request, &operation) == C35_OK);
    for (progress = 0; progress < 16; ++progress) {
        enum c35_result result = c35_operation_progress(
            &runtime->headless, &operation, 1, &status);

        CHECK(result == C35_OK || result == C35_IN_PROGRESS);
        if (result == C35_OK) break;
    }
    CHECK(progress < 16 && status.outcome == C35_COUNTER_EXHAUSTED &&
          status.commit_state == C35_COMMIT_NOT_STARTED &&
          status.cleanup_state == C35_CLEANUP_COMPLETE &&
          status.cause_domain == C35_CAUSE_C35 &&
          status.cause_code == C35_COUNTER_EXHAUSTED &&
          status.retry_class == C35_RETRY_REPAIR_REQUIRED);
    CHECK(c35_operation_retire(&runtime->headless, &operation) == C35_OK);
    CHECK(counter.calls[C35_LIFECYCLE_CUT_SUBMIT] == 0 &&
          runtime->firmware->next_inner_uid == inner_before &&
          runtime->firmware->next_physical_op_id == physical_before &&
          runtime->firmware->next_physical_sequence == sequence_before &&
          storage->credits.record_used == 14);
    CHECK(c35_runtime_teardown(runtime));
    CHECK(c35_storage_restart(storage));
    CHECK(c35_runtime_init(
        runtime, storage, lane,
        UINT64_C(0x35ae000000000000) | (uint64_t)lane,
        UINT64_C(0xa1b2c3d4e5f60718), 0, 0, 0x35ae0100u + lane));
    request = c35_request_read(0);
    CHECK(c35_run_command(runtime, &request, &semantic));
    CHECK(semantic.present_mask == 1 && semantic.payload[0][0] == 0x53);
    request = c35_request_read(1);
    CHECK(c35_run_command(runtime, &request, &semantic));
    CHECK(semantic.present_mask == 2 && semantic.payload[1][0] == 0x54);
    ok = c35_runtime_teardown(runtime) && c35_storage_close(storage);
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

static int test_reset_registration_reuse(enum c35_lane lane)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct c35_request request = c35_request_read(0);
    struct c35_semantic_result semantic;
    struct fwlab_c31_command_handle command;
    uint8_t uuid[16];
    unsigned int iteration;
    int ok;

    CHECK(storage != NULL && runtime != NULL);
    uuid_make(uuid, lane);
    CHECK(c35_storage_init(storage, lane, uuid));
    CHECK(c35_runtime_init(
        runtime, storage, lane,
        UINT64_C(0x35af000000000000) | (uint64_t)lane,
        UINT64_C(0x1234432112344321), 0, 0, 0x35af0000u + lane));
    for (iteration = 0; iteration < 5; ++iteration) {
        CHECK(c35_headless_submit(
            &runtime->headless, &request, &command) == C35_OK);
        CHECK(c35_headless_reset(&runtime->headless, 8192) == C35_OK);
        if (lane == C35_LANE_SCRIPTED) {
            CHECK(!runtime->scripted_binding.entry[0].used &&
                  !runtime->scripted_binding.entry[1].used);
        } else {
            CHECK(!runtime->c34_binding.registration[0].used &&
                  !runtime->c34_binding.registration[1].used);
        }
    }
    CHECK(c35_run_command(runtime, &request, &semantic));
    ok = c35_runtime_teardown(runtime) && c35_storage_close(storage);
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

int main(void)
{
    enum c35_lane lane;

    for (lane = C35_LANE_SCRIPTED; lane <= C35_LANE_POSIX; ++lane)
        CHECK(test_reset_boundary(lane));
    CHECK(test_runtime_limit(LOW_COMMAND_UID));
    CHECK(test_runtime_limit(LOW_SLOT_GENERATION));
    CHECK(test_runtime_limit(LOW_OPERATION_GENERATION));
    CHECK(test_runtime_limit(LOW_LEASE_GENERATION));
    CHECK(test_ticket_limit());
    CHECK(test_credit_guards());
    CHECK(test_c35_uid_limits());
    CHECK(test_scripted_scenario_limit());
    CHECK(test_live_physical_boundary(C35_LANE_MEMORY));
    CHECK(test_live_physical_boundary(C35_LANE_BYTE));
    CHECK(test_live_physical_boundary(C35_LANE_POSIX));
    CHECK(test_reset_registration_reuse(C35_LANE_SCRIPTED));
    CHECK(test_reset_registration_reuse(C35_LANE_MEMORY));
    puts("C3.5a limits/cleanup: PASS (15+1 resets x S/M/B/P; "
         "command/slot/op/lease/ticket fault teardown; guarded credits/UIDs; "
         "live inner30/physical16/record14 boundary)");
    return 0;
}
