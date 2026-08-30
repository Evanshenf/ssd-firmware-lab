/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_test_support.h"

#include <stddef.h>
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

static struct fwlab_c31_capacity fixed_c31_capacity(void)
{
    struct fwlab_c31_capacity capacity;

    memset(&capacity, 0, sizeof(capacity));
    capacity.version = FWLAB_C31_CONTRACT_VERSION;
    capacity.size = sizeof(capacity);
    capacity.commands = 2;
    capacity.abort_tickets = 2;
    capacity.event_batch = 2;
    capacity.trace_entries = 256;
    capacity.scratch_bytes = 256;
    capacity.slot_generation_limit = 512;
    capacity.operation_generation_limit = 512;
    capacity.lease_generation_limit = 512;
    capacity.ticket_generation_limit = 512;
    capacity.controller_epoch_limit = 16;
    capacity.command_uid_limit = 512;
    return capacity;
}

static int test_capacity_dominance(void)
{
    struct fwlab_c31_capacity c31 = fixed_c31_capacity();
    struct fwlab_nfc_model_config nfc = c35_test_nfc_config(
        UINT64_C(0x123456789abcdef0));
    struct fwlab_nfc_model_config changed;

    CHECK(c35_fixed_capacity_dominance_valid(&c31, &nfc, 32, 16, 16, 1));
    changed = nfc;
    changed.capacity.controller_epoch_limit = 16;
    CHECK(!c35_fixed_capacity_dominance_valid(
        &c31, &changed, 32, 16, 16, 1));
    changed = nfc;
    changed.capacity.cache_generation_limit = 31;
    CHECK(!c35_fixed_capacity_dominance_valid(
        &c31, &changed, 32, 16, 16, 1));
    changed = nfc;
    changed.capacity.trace_entries = 304;
    CHECK(!c35_fixed_capacity_dominance_valid(
        &c31, &changed, 32, 16, 16, 1));
    changed = nfc;
    changed.capacity.operation_uid_limit = 31;
    CHECK(!c35_fixed_capacity_dominance_valid(
        &c31, &changed, 32, 16, 16, 1));
    changed = nfc;
    changed.capacity.operation_generation_limit = 31;
    CHECK(!c35_fixed_capacity_dominance_valid(
        &c31, &changed, 32, 16, 16, 1));
    changed = nfc;
    changed.capacity.submit_sequence_limit = 31;
    CHECK(!c35_fixed_capacity_dominance_valid(
        &c31, &changed, 32, 16, 16, 1));
    CHECK(!c35_fixed_capacity_dominance_valid(&c31, &nfc, 32, 16, 15, 1));
    CHECK(!c35_fixed_capacity_dominance_valid(&c31, &nfc, 32, 16, 16, 0));
    c31.commands = 1;
    CHECK(!c35_fixed_capacity_dominance_valid(&c31, &nfc, 32, 16, 16, 1));
    return 1;
}

static int rejected_without_mutation(
    const struct c35_media_endpoint *media_endpoint,
    const struct c35_physical_endpoint *physical_endpoint,
    const struct fwlab_nand_media *authority
)
{
    struct c35_bundle bundle;
    struct c35_bundle before;
    uint64_t hash = authority->ops->hash(authority->context);

    memset(&bundle, 0xa5, sizeof(bundle));
    before = bundle;
    CHECK(c35_bundle_init(&bundle, media_endpoint, physical_endpoint) !=
          C35_OK);
    CHECK(memcmp(&bundle, &before, sizeof(bundle)) == 0);
    CHECK(authority->ops->hash(authority->context) == hash);
    return 1;
}

static int test_bundle_tables_and_identity(void)
{
    struct c34_memory_media context;
    struct c35_profile_descriptor profile;
    struct c35_profile_descriptor corrupt_profile;
    struct fwlab_nand_media media;
    struct c34_physical_txn_provider physical;
    struct c35_media_endpoint media_endpoint;
    struct c35_physical_endpoint physical_endpoint;
    struct c35_media_endpoint bad_media;
    struct c35_physical_endpoint bad_physical;
    struct fwlab_nand_media_ops media_ops;
    struct c34_physical_txn_ops physical_ops;
    struct c35_bundle bundle;
    uint8_t uuid[16];
    bool released;

    c34_memory_media_init(&context);
    c35_profile_fixed(&profile);
    CHECK(c35_profile_valid(&profile));
    CHECK(profile.geometry_id == UINT32_C(0x736c9756) &&
          profile.media_profile_id == UINT32_C(0x758162ca));
    CHECK(profile.media_wire[56] == 0 && profile.media_wire[57] == 0);
    c35_profile_uuid(&profile, UINT32_C(0x11223344), uuid);
    CHECK(memcmp(uuid, "F35A", 4) == 0);
    CHECK(uuid[12] == 0x44 && uuid[13] == 0x33 &&
          uuid[14] == 0x22 && uuid[15] == 0x11);

    media = c34_memory_media_provider(&context);
    physical = c34_memory_txn_provider(&context);
    c35_media_endpoint_make(
        &media_endpoint, &profile, UINT64_C(0x35b00c1e), &media);
    c35_physical_endpoint_make(
        &physical_endpoint, &profile, UINT64_C(0x35b00c1e), &physical);
    CHECK(c35_bundle_init(&bundle, &media_endpoint, &physical_endpoint) ==
          C35_OK);

#define REJECT_MEDIA_OP(field)                                               \
    do {                                                                     \
        bad_media = media_endpoint;                                          \
        media_ops = *media.ops;                                              \
        media_ops.field = NULL;                                              \
        bad_media.provider.ops = &media_ops;                                 \
        CHECK(rejected_without_mutation(                                    \
            &bad_media, &physical_endpoint, &media));                        \
    } while (0)
    REJECT_MEDIA_OP(read_page);
    REJECT_MEDIA_OP(program);
    REJECT_MEDIA_OP(erase);
    REJECT_MEDIA_OP(mark_runtime_bad);
    REJECT_MEDIA_OP(hash);
#undef REJECT_MEDIA_OP

#define REJECT_PHYSICAL_OP(field)                                            \
    do {                                                                     \
        bad_physical = physical_endpoint;                                    \
        physical_ops = *physical.ops;                                        \
        physical_ops.field = NULL;                                           \
        bad_physical.provider.ops = &physical_ops;                           \
        CHECK(rejected_without_mutation(                                    \
            &media_endpoint, &bad_physical, &media));                        \
    } while (0)
    REJECT_PHYSICAL_OP(bind);
    REJECT_PHYSICAL_OP(abandon);
    REJECT_PHYSICAL_OP(receipt);
    REJECT_PHYSICAL_OP(quiescent);
#undef REJECT_PHYSICAL_OP

#define REJECT_MEDIA_DESCRIPTOR(statement)                                  \
    do {                                                                     \
        bad_media = media_endpoint;                                          \
        statement;                                                           \
        CHECK(rejected_without_mutation(                                    \
            &bad_media, &physical_endpoint, &media));                        \
    } while (0)
    REJECT_MEDIA_DESCRIPTOR(++bad_media.descriptor.version);
    REJECT_MEDIA_DESCRIPTOR(--bad_media.descriptor.size);
    REJECT_MEDIA_DESCRIPTOR(bad_media.descriptor.reserved = 1);
    REJECT_MEDIA_DESCRIPTOR(bad_media.descriptor.role =
                            C35_ENDPOINT_PHYSICAL_TXN);
    REJECT_MEDIA_DESCRIPTOR(bad_media.descriptor.feature_bits = 1);
    REJECT_MEDIA_DESCRIPTOR(++bad_media.descriptor.media_profile_id);
    REJECT_MEDIA_DESCRIPTOR(++bad_media.descriptor.geometry_id);
    REJECT_MEDIA_DESCRIPTOR(++bad_media.descriptor.coherence_cookie);
    REJECT_MEDIA_DESCRIPTOR(bad_media.descriptor.profile.media_wire[63] = 1);
#undef REJECT_MEDIA_DESCRIPTOR

    bad_physical = physical_endpoint;
    ++bad_physical.descriptor.coherence_cookie;
    CHECK(rejected_without_mutation(
        &media_endpoint, &bad_physical, &media));
    bad_physical = physical_endpoint;
    bad_physical.provider.context = &bad_physical;
    CHECK(rejected_without_mutation(
        &media_endpoint, &bad_physical, &media));
    corrupt_profile = profile;
    corrupt_profile.geometry_wire[31] = 1;
    CHECK(!c35_profile_valid(&corrupt_profile));

    context.binding_used = 1;
    CHECK(rejected_without_mutation(
        &media_endpoint, &physical_endpoint, &media));
    context.binding_used = 0;

    CHECK(c35_bundle_claim(&bundle, UINT64_C(0x35112233)) == C35_OK);
    CHECK(c35_bundle_claim(&bundle, UINT64_C(0x35445566)) == C35_WRONG_STATE);
    CHECK(c35_bundle_release(&bundle, UINT64_C(0x35445566)) == C35_STALE);
    context.binding_used = 1;
    CHECK(c35_bundle_release(&bundle, UINT64_C(0x35112233)) ==
          C35_IN_PROGRESS);
    CHECK(bundle.claimed);
    context.binding_used = 0;
    CHECK(c35_bundle_release(&bundle, UINT64_C(0x35112233)) == C35_OK);
    CHECK(c35_bundle_release_query(
        &bundle, UINT64_C(0x35112233), &released) == C35_OK && released);
    CHECK(c35_bundle_release(&bundle, UINT64_C(0x35112233)) == C35_OK);
    CHECK(c35_bundle_release_query(
        &bundle, UINT64_C(0x35445566), &released) == C35_STALE);
    return 1;
}

static int test_operation_tables(void)
{
    struct c31_fake_provider_context fake;
    struct c35_scripted_binding scripted;
    struct c35_binding binding;
    struct c35_binding candidate;
    struct c35_binding_ops binding_ops;
    union {
        max_align_t alignment;
        uint8_t bytes[8];
    } dummy;
    struct c35_lifecycle_port lifecycle;
    struct c35_lifecycle_port lifecycle_candidate;
    struct c35_lifecycle_ops lifecycle_ops;

    c31_fake_provider_init(&fake, FWLAB_C31_PROVIDER_NFC);
    CHECK(c35_scripted_binding_init(
        &scripted, &fake, UINT64_C(0x35123456), 1) == C35_OK);
    binding = c35_scripted_binding_provider(&scripted);
    CHECK(c35_binding_valid(&binding));

#define REJECT_BINDING_OP(field)                                             \
    do {                                                                     \
        binding_ops = *binding.ops;                                          \
        binding_ops.field = NULL;                                            \
        candidate = binding;                                                 \
        candidate.ops = &binding_ops;                                        \
        CHECK(!c35_binding_valid(&candidate));                               \
    } while (0)
    REJECT_BINDING_OP(registration_prepare);
    REJECT_BINDING_OP(registration_commit);
    REJECT_BINDING_OP(registration_query);
    REJECT_BINDING_OP(registration_abort);
    REJECT_BINDING_OP(result_prepare);
    REJECT_BINDING_OP(result_query);
    REJECT_BINDING_OP(result_abort);
    REJECT_BINDING_OP(result_ack);
    REJECT_BINDING_OP(reset_recover);
    REJECT_BINDING_OP(reset_query);
    REJECT_BINDING_OP(transaction_retire);
    REJECT_BINDING_OP(teardown_finalize);
    REJECT_BINDING_OP(semantic_snapshot);
    REJECT_BINDING_OP(quiescent);
    REJECT_BINDING_OP(cause_query);
#undef REJECT_BINDING_OP
    binding_ops = *binding.ops;
    ++binding_ops.version;
    candidate = binding;
    candidate.ops = &binding_ops;
    CHECK(!c35_binding_valid(&candidate));
    binding_ops = *binding.ops;
    --binding_ops.size;
    candidate.ops = &binding_ops;
    CHECK(!c35_binding_valid(&candidate));
    binding_ops = *binding.ops;
    binding_ops.reserved = 1;
    candidate.ops = &binding_ops;
    CHECK(!c35_binding_valid(&candidate));

    lifecycle = c35_lifecycle_port_native((struct fwlab_c31 *)dummy.bytes);
    CHECK(c35_lifecycle_port_valid(&lifecycle));
#define REJECT_LIFECYCLE_OP(field)                                           \
    do {                                                                     \
        lifecycle_ops = *lifecycle.ops;                                      \
        lifecycle_ops.field = NULL;                                          \
        lifecycle_candidate = lifecycle;                                    \
        lifecycle_candidate.ops = &lifecycle_ops;                            \
        CHECK(!c35_lifecycle_port_valid(&lifecycle_candidate));              \
    } while (0)
    REJECT_LIFECYCLE_OP(phase);
    REJECT_LIFECYCLE_OP(submit);
    REJECT_LIFECYCLE_OP(step);
    REJECT_LIFECYCLE_OP(command_state);
    REJECT_LIFECYCLE_OP(completion_acquire);
    REJECT_LIFECYCLE_OP(completion_release);
    REJECT_LIFECYCLE_OP(completion_consume);
    REJECT_LIFECYCLE_OP(abort_request);
    REJECT_LIFECYCLE_OP(abort_query);
    REJECT_LIFECYCLE_OP(abort_ack);
    REJECT_LIFECYCLE_OP(reset_begin);
    REJECT_LIFECYCLE_OP(reset_ack);
    REJECT_LIFECYCLE_OP(teardown_begin);
    REJECT_LIFECYCLE_OP(teardown_ack);
#undef REJECT_LIFECYCLE_OP
    lifecycle_ops = *lifecycle.ops;
    ++lifecycle_ops.version;
    lifecycle_candidate = lifecycle;
    lifecycle_candidate.ops = &lifecycle_ops;
    CHECK(!c35_lifecycle_port_valid(&lifecycle_candidate));
    lifecycle_ops = *lifecycle.ops;
    --lifecycle_ops.size;
    lifecycle_candidate.ops = &lifecycle_ops;
    CHECK(!c35_lifecycle_port_valid(&lifecycle_candidate));
    lifecycle_ops = *lifecycle.ops;
    lifecycle_ops.reserved = 1;
    lifecycle_candidate.ops = &lifecycle_ops;
    CHECK(!c35_lifecycle_port_valid(&lifecycle_candidate));
    return 1;
}

static int fill_observer(struct c35_trace *trace)
{
    struct c35_publication publication;
    unsigned int index;

    for (index = 0; index < 682; ++index) {
        memset(&publication, 0, sizeof(publication));
        publication.version = C35_PUBLICATION_VERSION;
        publication.size = sizeof(publication);
        publication.kind = C35_PUBLICATION_COMMAND;
        publication.commit_state = C35_COMMIT_COMMITTED;
        publication.publication_uid = index + 1u;
        if (c35_trace_append(trace, &publication) != C35_OK) return 0;
    }
    return trace->length == 65488 && trace->events == 682;
}

static int observer_case(int corrupt)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct c35_request request = c35_request_read(0);
    struct c35_semantic_result semantic;
    struct c35_operation_status status;
    uint8_t uuid[16] = {
        0x35, 0x5a, 0xa5, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x66, 0x77, 0x88, 0x99, 0x78, 0x56, 0x34, 0x12
    };
    int ok;

    CHECK(storage != NULL && runtime != NULL);
    CHECK(c35_storage_init(storage, C35_LANE_POSIX, uuid));
    CHECK(c35_runtime_init(
        runtime, storage, C35_LANE_POSIX,
        UINT64_C(0x35a5000000000000) | (uint64_t)corrupt,
        UINT64_C(0xabcdef0123456789), 0, 0, 0x35a50000u + corrupt));
    if (corrupt) runtime->trace.length = C35_TRACE_BYTES + 1u;
    else CHECK(fill_observer(&runtime->trace));
    CHECK(c35_run_command_status(runtime, &request, &semantic, &status) ==
          C35_OK);
    CHECK(status.commit_state == C35_COMMIT_COMMITTED &&
          status.cleanup_state == C35_CLEANUP_COMPLETE &&
          status.publication_valid);
    CHECK(runtime->last_observation ==
          (corrupt ? C35_OBSERVATION_LOST_INVALID_SINK :
                     C35_OBSERVATION_LOST_NO_CAPACITY));
    CHECK(c35_runtime_teardown(runtime));
    CHECK(!runtime->claimed && !storage->bundle.claimed);
    ok = c35_storage_close(storage);
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

static int test_runtime_release_resume(void)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    uint8_t uuid[16] = {
        0x35, 0xb0, 0x0c, 0x1e, 0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88, 0x21, 0x43, 0x65, 0x87
    };
    uint32_t trace_length;
    uint32_t trace_events;
    struct c35_operation_token token;
    struct c35_operation_status status;
    unsigned int iteration;
    int ok;

    CHECK(storage != NULL && runtime != NULL);
    CHECK(c35_storage_init(storage, C35_LANE_MEMORY, uuid));
    CHECK(c35_runtime_init(
        runtime, storage, C35_LANE_MEMORY,
        UINT64_C(0x35ab000000000001), UINT64_C(0x1122334455667788),
        0, 0, 0x35ab0001));
    CHECK(c35_finalizer_start(
        &runtime->finalizer, &runtime->headless, runtime->bundle,
        runtime->nonce, &token) == C35_OK);
    {
        struct c35_finalizer before = runtime->finalizer;

        CHECK(c35_finalizer_progress(
            &runtime->finalizer, &token, 0, &status) == C35_IN_PROGRESS);
        CHECK(status.units_used == 0 &&
              memcmp(&before, &runtime->finalizer, sizeof(before)) == 0);
    }
    for (iteration = 0; iteration < 8192; ++iteration) {
        CHECK(c35_finalizer_query(
            &runtime->finalizer, &token, &status) == C35_IN_PROGRESS);
        if (runtime->finalizer.phase == C35_FINALIZER_BUNDLE_RELEASE) break;
        CHECK(c35_finalizer_progress(
            &runtime->finalizer, &token, 1, &status) == C35_IN_PROGRESS);
    }
    CHECK(iteration < 8192 && status.commit_state == C35_COMMIT_COMMITTED &&
          status.cleanup_state == C35_CLEANUP_PENDING &&
          status.publication_valid);
    storage->memory.binding_used = 1;
    CHECK(!c35_runtime_teardown(runtime));
    CHECK(!c35_runtime_teardown(runtime));
    CHECK(runtime->claimed && storage->bundle.claimed &&
          runtime->headless.service_phase == C35_SERVICE_DEAD);
    storage->memory.binding_used = 0;
    CHECK(c35_runtime_teardown(runtime));
    CHECK(!runtime->claimed && !storage->bundle.claimed);
    trace_length = runtime->trace.length;
    trace_events = runtime->trace.events;
    CHECK(c35_runtime_teardown(runtime));
    CHECK(runtime->trace.length == trace_length &&
          runtime->trace.events == trace_events);
    ok = c35_storage_close(storage);
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

struct authority_record {
    struct c35_publication dma;
    struct c35_publication command;
    struct c35_publication reset;
    struct c35_publication teardown;
    struct c35_semantic_result semantic;
    uint8_t dma_output[C35_ATOM_BYTES];
    uint32_t final_epoch;
};

struct release_fault {
    struct c34_physical_txn_provider inner;
    struct c35_bundle *bundle;
    uint64_t claimant;
    uint8_t after_effect;
    uint8_t injected;
};

static enum c34_physical_txn_result release_fault_quiescent(
    void *context,
    bool *quiescent
)
{
    struct release_fault *fault = context;
    enum c34_physical_txn_result result;

    if (fault->injected)
        return fault->inner.ops->quiescent(fault->inner.context, quiescent);
    fault->injected = 1;
    if (!fault->after_effect) return C34_PHYSICAL_TXN_IO;
    result = fault->inner.ops->quiescent(fault->inner.context, quiescent);
    if (result != C34_PHYSICAL_TXN_OK || !*quiescent) return result;
    fault->bundle->claimed = 0;
    fault->bundle->claimant = 0;
    fault->bundle->last_released_claimant = fault->claimant;
    return C34_PHYSICAL_TXN_IO;
}

static int authority_case(int observer_mode, struct authority_record *record)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct c35_request request = c35_request_read(0);
    struct c35_operation_status status;
    uint8_t input[C35_ATOM_BYTES];
    uint8_t uuid[16] = {
        0x35, 0xec, 0x1d, 0x5a, 0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88, 0x31, 0x42, 0x53, 0x64
    };
    enum c35_result observation;
    unsigned int index;
    int ok;

    CHECK(storage != NULL && runtime != NULL && record != NULL);
    memset(record, 0, sizeof(*record));
    for (index = 0; index < C35_ATOM_BYTES; ++index)
        input[index] = (uint8_t)(0x41u + index * 9u);
    CHECK(c35_storage_init(storage, C35_LANE_POSIX, uuid));
    CHECK(c35_runtime_init(
        runtime, storage, C35_LANE_POSIX,
        UINT64_C(0x35ac000000000001), UINT64_C(0x8877665544332211),
        0, 0, 0x35ac0001));
    if (observer_mode == 1) CHECK(fill_observer(&runtime->trace));
    else if (observer_mode == 2) runtime->trace.length = C35_TRACE_BYTES + 1u;

    CHECK(c35_dma_capture_status(
        runtime, input, record->dma_output, &status, &record->dma) == C35_OK);
    CHECK(status.commit_state == C35_COMMIT_COMMITTED &&
          status.cleanup_state == C35_CLEANUP_COMPLETE);
    observation = c35_trace_append(&runtime->trace, &record->dma);
    CHECK(observation == (observer_mode == 0 ? C35_OK :
                          observer_mode == 1 ? C35_NO_CAPACITY : C35_INVALID));
    CHECK(c35_run_command_status(
        runtime, &request, &record->semantic, &status) == C35_OK);
    CHECK(status.publication_valid &&
          status.commit_state == C35_COMMIT_COMMITTED);
    record->command = status.publication;
    CHECK(c35_headless_reset_observed(
        &runtime->headless, 8192, &record->reset) == C35_OK);
    observation = c35_trace_append(&runtime->trace, &record->reset);
    CHECK(observation == (observer_mode == 0 ? C35_OK :
                          observer_mode == 1 ? C35_NO_CAPACITY : C35_INVALID));
    record->final_epoch = runtime->headless.owner_epoch;
    CHECK(c35_runtime_teardown(runtime));
    record->teardown = runtime->teardown_publication;
    CHECK(!runtime->claimed && !storage->bundle.claimed);
    ok = c35_storage_close(storage);
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

static int profile_uuid_mismatch(enum c35_lane lane)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    uint8_t *before = malloc(C34_FILE_IMAGE_BYTES);
    uint8_t *after = malloc(C34_FILE_IMAGE_BYTES);
    uint8_t uuid[16] = {
        0x35, 0xf1, 0x1e, 0x00, 0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88, 0x14, 0x13, 0x12, 0x11
    };
    int ok;

    CHECK(storage != NULL && before != NULL && after != NULL);
    CHECK(c35_storage_init(storage, lane, uuid));
    CHECK(c35_storage_container(storage, before));
    storage->uuid[4] ^= UINT8_C(0x80);
    CHECK(!c35_storage_restart(storage));
    CHECK(!storage->bundle.claimed);
    CHECK(c35_storage_container(storage, after));
    CHECK(memcmp(before, after, C34_FILE_IMAGE_BYTES) == 0);
    ok = c35_storage_close(storage);
    free(after);
    free(before);
    free(storage);
    CHECK(ok);
    return 1;
}

static int bundle_release_fault_case(int after_effect)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct release_fault fault;
    struct c34_physical_txn_ops ops;
    uint8_t uuid[16] = {
        0x35, 0xfa, 0x17, 0x00, 0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88, 0x74, 0x63, 0x52, 0x41
    };
    int ok;

    CHECK(storage != NULL && runtime != NULL);
    CHECK(c35_storage_init(storage, C35_LANE_MEMORY, uuid));
    CHECK(c35_runtime_init(
        runtime, storage, C35_LANE_MEMORY,
        UINT64_C(0x35ad000000000000) | (uint64_t)after_effect,
        UINT64_C(0x5566778811223344), 0, 0,
        0x35ad0000u + (uint32_t)after_effect));
    memset(&fault, 0, sizeof(fault));
    fault.inner = storage->bundle.physical;
    fault.bundle = &storage->bundle;
    fault.claimant = runtime->nonce;
    fault.after_effect = (uint8_t)after_effect;
    ops = *fault.inner.ops;
    ops.quiescent = release_fault_quiescent;
    storage->bundle.physical.ops = &ops;
    storage->bundle.physical.context = &fault;
    CHECK(c35_runtime_teardown(runtime));
    CHECK(fault.injected && !runtime->claimed && !storage->bundle.claimed &&
          runtime->bundle_released);
    ok = c35_storage_close(storage);
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

int main(void)
{
    struct authority_record baseline;
    struct authority_record full;
    struct authority_record corrupt;

    CHECK(test_capacity_dominance());
    CHECK(test_bundle_tables_and_identity());
    CHECK(test_operation_tables());
    CHECK(observer_case(0));
    CHECK(observer_case(1));
    CHECK(test_runtime_release_resume());
    CHECK(authority_case(0, &baseline));
    CHECK(authority_case(1, &full));
    CHECK(authority_case(2, &corrupt));
    CHECK(memcmp(&baseline, &full, sizeof(baseline)) == 0);
    CHECK(memcmp(&baseline, &corrupt, sizeof(baseline)) == 0);
    CHECK(profile_uuid_mismatch(C35_LANE_BYTE));
    CHECK(profile_uuid_mismatch(C35_LANE_POSIX));
    CHECK(bundle_release_fault_case(0));
    CHECK(bundle_release_fault_case(1));
    puts("C3.5a profile/bundle: PASS (capacity dominance; exact tables; "
         "profile identity; observer non-authority)");
    return 0;
}
