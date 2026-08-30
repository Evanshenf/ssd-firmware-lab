/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _POSIX_C_SOURCE 200809L

#include "c35_test_support.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef C35_TEST_LANE_MASK
#define C35_TEST_LANE_MASK 0x0fu
#endif

#define C35_HAS_SCRIPTED (C35_TEST_LANE_MASK & 0x01u)
#define C35_HAS_MEMORY (C35_TEST_LANE_MASK & 0x02u)
#define C35_HAS_BYTE (C35_TEST_LANE_MASK & 0x04u)
#define C35_HAS_POSIX (C35_TEST_LANE_MASK & 0x08u)
#define C35_HAS_C34 (C35_HAS_MEMORY || C35_HAS_BYTE || C35_HAS_POSIX)

#if C35_HAS_C34
static int storage_providers(
    struct c35_storage *storage,
    struct fwlab_nand_media *media,
    struct c34_physical_txn_provider *physical
);

static int storage_bundle_init(struct c35_storage *storage)
{
    struct fwlab_nand_media media;
    struct c34_physical_txn_provider physical;
    struct c35_media_endpoint media_endpoint;
    struct c35_physical_endpoint physical_endpoint;
    uint64_t coherence_cookie = UINT64_C(0xb35d1e0000000000) |
                                (uint64_t)storage->lane;

    if (!storage_providers(storage, &media, &physical)) return 0;
    c35_media_endpoint_make(
        &media_endpoint, &storage->profile, coherence_cookie, &media);
    c35_physical_endpoint_make(
        &physical_endpoint, &storage->profile, coherence_cookie, &physical);
    return c35_bundle_init(
               &storage->bundle, &media_endpoint, &physical_endpoint) ==
           C35_OK;
}
#endif

struct fwlab_nfc_model_config c35_test_nfc_config(uint64_t seed)
{
    struct fwlab_nfc_model_config config;

    memset(&config, 0, sizeof(config));
    config.version = FWLAB_NFC_CONTRACT_VERSION;
    config.size = sizeof(config);
    config.geometry.version = FWLAB_NFC_CONTRACT_VERSION;
    config.geometry.size = sizeof(config.geometry);
    config.geometry.channels = 1;
    config.geometry.luns_per_channel = 1;
    config.geometry.planes_per_lun = 1;
    config.geometry.blocks_per_plane = C34_BLOCKS;
    config.geometry.pages_per_block = C34_PAGES_PER_BLOCK;
    config.geometry.plane_parallelism_per_lun = 1;
    config.geometry.main_bytes_per_page = C34_MAIN_BYTES;
    config.geometry.oob_bytes_per_page = C34_OOB_BYTES;
    config.geometry.max_programs_per_erase = 1;
    config.geometry.program_order = FWLAB_NFC_PROGRAM_ASCENDING;
    config.ecc.version = FWLAB_NFC_CONTRACT_VERSION;
    config.ecc.size = sizeof(config.ecc);
    config.ecc.main_covered_bytes = C34_MAIN_BYTES;
    config.ecc.oob_covered_bytes = C34_OOB_BYTES;
    config.ecc.main_step_bytes = 16;
    config.ecc.oob_step_bytes = 16;
    config.ecc.main_strength_bits = 4;
    config.ecc.oob_strength_bits = 4;
    config.ecc.max_retry_step = 1;
    config.timing.version = FWLAB_NFC_CONTRACT_VERSION;
    config.timing.size = sizeof(config.timing);
    config.timing.command_ticks = 1;
    config.timing.transfer_ticks_per_unit = 1;
    config.timing.read_array_ticks = 1;
    config.timing.program_setup_ticks = 1;
    config.timing.program_ticks_per_unit = 1;
    config.timing.program_status_ticks = 1;
    config.timing.erase_setup_ticks = 1;
    config.timing.erase_ticks_per_page = 1;
    config.timing.erase_status_ticks = 1;
    config.timing.status_ticks = 1;
    config.fault.version = FWLAB_NFC_FAULT_PROFILE_VERSION;
    config.fault.size = sizeof(config.fault);
    config.fault.profile_version = 1;
    config.fault.seed = seed;
    config.capacity.version = FWLAB_NFC_CONTRACT_VERSION;
    config.capacity.size = sizeof(config.capacity);
    config.capacity.operations = 2;
    config.capacity.request_registry = 2;
    config.capacity.terminal_events = 2;
    config.capacity.result_slots = 2;
    config.capacity.trace_entries = 512;
    config.capacity.scratch_main_bytes = C34_MAIN_BYTES;
    config.capacity.scratch_oob_bytes = C34_OOB_BYTES;
    config.capacity.operation_generation_limit = 32;
    config.capacity.cache_generation_limit = 32;
    config.capacity.controller_epoch_limit = 17;
    config.capacity.submit_sequence_limit = 32;
    config.capacity.operation_uid_limit = 32;
    config.capacity.virtual_tick_limit = UINT64_C(1000000);
    config.successful_erase_limit = 4;
    return config;
}

static struct fwlab_c31_capacity c31_capacity(void)
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

int c35_storage_init(
    struct c35_storage *storage,
    enum c35_lane lane,
    const uint8_t uuid[16]
)
{
    if (storage == NULL || uuid == NULL) {
        return 0;
    }
    memset(storage, 0, sizeof(*storage));
    storage->lane = (uint8_t)lane;
    storage->fd = -1;
    storage->image_serial = (uint32_t)uuid[12] |
                            ((uint32_t)uuid[13] << 8) |
                            ((uint32_t)uuid[14] << 16) |
                            ((uint32_t)uuid[15] << 24);
    if (storage->image_serial == 0) storage->image_serial = 1;
    c35_profile_fixed(&storage->profile);
    c35_persistent_credits_init(&storage->credits);
    c35_profile_uuid(
        &storage->profile, storage->image_serial, storage->uuid);
#if C35_HAS_SCRIPTED
    if (lane == C35_LANE_SCRIPTED) {
        storage->initialized = 1;
        return 1;
    }
#endif
#if C35_HAS_MEMORY
    if (lane == C35_LANE_MEMORY) {
        c34_memory_media_init(&storage->memory);
        storage->initialized = storage_bundle_init(storage);
        return storage->initialized;
    }
#endif
#if C35_HAS_BYTE
    if (lane == C35_LANE_BYTE) {
        struct c34_file_substrate substrate;

        c34f_memory_substrate_init(&storage->byte);
        substrate = c34f_memory_substrate_provider(&storage->byte);
        if (c34_file_format(
                storage->file_arena[0].bytes,
                sizeof(storage->file_arena[0].bytes), &substrate,
                storage->uuid, &storage->file) != C34_FILE_OK) {
            return 0;
        }
        storage->initialized = storage_bundle_init(storage);
        return storage->initialized;
    }
#endif
#if C35_HAS_POSIX
    if (lane == C35_LANE_POSIX) {
        char path[] = "/tmp/c35-posix-XXXXXX";

        storage->fd = mkstemp(path);
        if (storage->fd < 0 || unlink(path) != 0 ||
            c34_file_posix_format(
                storage->file_arena[0].bytes,
                sizeof(storage->file_arena[0].bytes), storage->fd,
                storage->uuid, &storage->file) != C34_FILE_OK) {
            if (storage->fd >= 0) {
                (void)close(storage->fd);
                storage->fd = -1;
            }
            return 0;
        }
        storage->initialized = storage_bundle_init(storage);
        if (!storage->initialized) {
            (void)close(storage->fd);
            storage->fd = -1;
        }
        return storage->initialized;
    }
#endif
    return 0;
}

int c35_storage_restart(struct c35_storage *storage)
{
#if C35_HAS_BYTE || C35_HAS_POSIX
    uint8_t next = (uint8_t)(storage->file_slot ^ 1u);
#endif

    if (storage == NULL || !storage->initialized || storage->bundle.claimed) {
        return 0;
    }
#if C35_HAS_SCRIPTED
    if (storage->lane == C35_LANE_SCRIPTED) return 1;
#endif
#if C35_HAS_MEMORY
    if (storage->lane == C35_LANE_MEMORY) return 1;
#endif
#if C35_HAS_BYTE
    if (storage->lane == C35_LANE_BYTE) {
        struct c34_file_substrate substrate =
            c34f_memory_substrate_provider(&storage->byte);

        if (c34_file_restart(
                storage->file_arena[next].bytes,
                sizeof(storage->file_arena[next].bytes), &substrate,
                storage->uuid, &storage->file) != C34_FILE_OK) {
            return 0;
        }
        storage->file_slot = next;
        return storage_bundle_init(storage);
    }
#endif
#if C35_HAS_POSIX
    if (storage->lane == C35_LANE_POSIX) {
        if (c34_file_posix_restart(
                   storage->file_arena[next].bytes,
                   sizeof(storage->file_arena[next].bytes), storage->fd,
                   storage->uuid, &storage->file) != C34_FILE_OK) {
            return 0;
        }
        storage->file_slot = next;
        return storage_bundle_init(storage);
    }
#endif
    return 0;
}

int c35_storage_close(struct c35_storage *storage)
{
    int result = 1;

    if (storage == NULL) {
        return 0;
    }
    if (storage->bundle.claimed) {
        return 0;
    }
#if C35_HAS_POSIX
    if (storage->fd >= 0) {
        result = close(storage->fd) == 0;
        storage->fd = -1;
    }
#endif
    storage->initialized = 0;
    return result;
}

int c35_storage_container(
    const struct c35_storage *storage,
    uint8_t bytes[C34_FILE_IMAGE_BYTES]
)
{
#if C35_HAS_POSIX
    size_t done = 0;
#endif

#if C35_HAS_BYTE
    if (storage->lane == C35_LANE_BYTE) {
        memcpy(bytes, storage->byte.working, C34_FILE_IMAGE_BYTES);
        return 1;
    }
#endif
#if C35_HAS_POSIX
    if (storage->lane != C35_LANE_POSIX || storage->fd < 0) {
        return 0;
    }
    while (done < C34_FILE_IMAGE_BYTES) {
        ssize_t count = pread(storage->fd, &bytes[done],
                              C34_FILE_IMAGE_BYTES - done, (off_t)done);

        if (count <= 0) {
            return 0;
        }
        done += (size_t)count;
    }
    return 1;
#else
    (void)storage;
    (void)bytes;
    return 0;
#endif
}

#if C35_HAS_C34
static int storage_providers(
    struct c35_storage *storage,
    struct fwlab_nand_media *media,
    struct c34_physical_txn_provider *physical
)
{
#if C35_HAS_MEMORY
    if (storage->lane == C35_LANE_MEMORY) {
        *media = c34_memory_media_provider(&storage->memory);
        *physical = c34_memory_txn_provider(&storage->memory);
        return 1;
    }
#endif
#if C35_HAS_BYTE || C35_HAS_POSIX
    if (storage->lane == C35_LANE_BYTE ||
        storage->lane == C35_LANE_POSIX) {
        *media = c34_file_nand_media(storage->file);
        *physical = c34_file_txn_provider(storage->file);
        return 1;
    }
#endif
    return 0;
}
#endif

int c35_runtime_init(
    struct c35_runtime *runtime,
    struct c35_storage *storage,
    enum c35_lane lane,
    uint64_t nonce,
    uint64_t seed,
    uint8_t cache_enabled,
    uint8_t actor,
    uint32_t scenario
)
{
    struct fwlab_nfc_model_config nfc_config = c35_test_nfc_config(seed);

    return c35_runtime_init_profile(
        runtime, storage, lane, nonce, seed, cache_enabled, actor,
        scenario, &nfc_config);
}

static int fixed_profile_valid(
    const struct fwlab_nfc_model_config *config,
    const struct fwlab_c31_capacity *capacity
)
{
    return config != NULL &&
           config->geometry.channels == 1 &&
           config->geometry.luns_per_channel == 1 &&
           config->geometry.planes_per_lun == 1 &&
           config->geometry.blocks_per_plane == C34_BLOCKS &&
           config->geometry.pages_per_block == C34_PAGES_PER_BLOCK &&
           config->geometry.plane_parallelism_per_lun == 1 &&
           config->geometry.main_bytes_per_page == C34_MAIN_BYTES &&
           config->geometry.oob_bytes_per_page == C34_OOB_BYTES &&
           config->geometry.max_programs_per_erase == 1 &&
           config->geometry.program_order == FWLAB_NFC_PROGRAM_ASCENDING &&
           config->ecc.main_covered_bytes == C34_MAIN_BYTES &&
           config->ecc.oob_covered_bytes == C34_OOB_BYTES &&
           config->capacity.operation_generation_limit == 32 &&
           config->capacity.cache_generation_limit == 32 &&
           config->capacity.controller_epoch_limit == 17 &&
           config->capacity.submit_sequence_limit == 32 &&
           config->capacity.operation_uid_limit == 32 &&
           c35_fixed_capacity_dominance_valid(
               capacity, config, 32, 16, 16, 1);
}

int c35_runtime_init_profile(
    struct c35_runtime *runtime,
    struct c35_storage *storage,
    enum c35_lane lane,
    uint64_t nonce,
    uint64_t seed,
    uint8_t cache_enabled,
    uint8_t actor,
    uint32_t scenario,
    const struct fwlab_nfc_model_config *nfc_config
)
{
    struct fwlab_c31_capacity capacity = c31_capacity();
    struct fwlab_c31_provider_set providers;
    struct c35_binding binding;

    if (runtime == NULL || storage == NULL || !storage->initialized ||
        lane > C35_LANE_POSIX ||
        (C35_TEST_LANE_MASK & (1u << (unsigned int)lane)) == 0 ||
        storage->lane != (uint8_t)lane || nonce == 0 ||
        (lane != C35_LANE_SCRIPTED &&
         !fixed_profile_valid(nfc_config, &capacity))) {
        return 0;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->storage = storage;
    runtime->lane = (uint8_t)lane;
    runtime->nonce = nonce;
    runtime->seed = seed;
    runtime->cache_enabled = cache_enabled;
    runtime->actor = actor;
    runtime->dma_next = 1;
    c35_trace_init(&runtime->trace, scenario);
    c31_fake_dma_init(&runtime->dma);
    memset(&providers, 0, sizeof(providers));
    providers.dma = c31_fake_dma_provider(&runtime->dma);
#if C35_HAS_SCRIPTED
    if (lane == C35_LANE_SCRIPTED) {
        c31_fake_provider_init(
            &runtime->scripted_nfc, FWLAB_C31_PROVIDER_NFC);
        providers.nfc = c31_fake_provider(&runtime->scripted_nfc);
        if (c35_scripted_binding_init(
                &runtime->scripted_binding, &runtime->scripted_nfc,
                nonce, 1) !=
            C35_OK) {
            return 0;
        }
        binding = c35_scripted_binding_provider(
            &runtime->scripted_binding);
    }
#if C35_HAS_C34
    else
#endif
#endif
#if C35_HAS_C34
    {
        struct fwlab_nand_media media;
        struct c34_physical_txn_provider physical;
        struct fwlab_nfc_buffer_provider buffers;
        struct fwlab_nfc_provider nfc;
        struct c34_config c34_config;

        runtime->bundle = &storage->bundle;
        if (!storage_providers(storage, &media, &physical) ||
            c35_bundle_claim(runtime->bundle, nonce) != C35_OK) {
            goto fail;
        }
        runtime->claimed = 1;
        c34_fake_buffer_init(&runtime->buffer, 7);
        buffers = c34_fake_buffer_provider(&runtime->buffer);
        if (fwlab_nfc_model_init(
                runtime->nfc_arena.bytes, sizeof(runtime->nfc_arena.bytes),
                nfc_config, nonce, &buffers, &media,
                &runtime->nfc_model) != FWLAB_NFC_API_OK) {
            goto fail;
        }
        nfc = fwlab_nfc_model_provider(runtime->nfc_model);
        memset(&c34_config, 0, sizeof(c34_config));
        c34_config.version = C34_CONTRACT_VERSION;
        c34_config.size = sizeof(c34_config);
        c34_config.instance_nonce = nonce;
        c34_config.controller_epoch = 1;
        c34_config.controller_region = 7;
        c34_config.controller_buffer_length = C34_FAKE_BUFFER_BYTES;
        c34_config.persistence.version = FWLAB_PERSIST_VERSION;
        c34_config.persistence.size = sizeof(c34_config.persistence);
        c34_config.persistence.cache_enabled = cache_enabled;
        c34_config.persistence.plp_kind = FWLAB_PERSIST_PLP_NONE;
        c34_config.inner_uid_limit = 32;
        c34_config.physical_op_limit = 16;
        c34_config.physical_sequence_limit = 16;
        if (c34_init(
                runtime->c34_arena.bytes, sizeof(runtime->c34_arena.bytes),
                &c34_config, &buffers, &nfc, &media, &physical,
                &runtime->firmware) != C34_OK ||
            c35_c34_binding_init(
                &runtime->c34_binding, runtime->firmware,
                &storage->credits, nonce, 1) !=
                C35_OK) {
            goto fail;
        }
        providers.nfc = c34_c31_provider(runtime->firmware);
        binding = c35_c34_binding_provider(&runtime->c34_binding);
    }
#endif
    if (fwlab_c31_init(
            runtime->c31_arena.bytes, sizeof(runtime->c31_arena.bytes),
            &capacity, nonce, &providers, &runtime->lifecycle) !=
            FWLAB_C31_API_OK) {
        goto fail;
    }
    runtime->lifecycle_port = c35_lifecycle_port_native(runtime->lifecycle);
    if (c35_headless_init(
            &runtime->headless, &runtime->lifecycle_port, &binding,
            nonce, 1, capacity.controller_epoch_limit,
            UINT64_C(512), UINT64_C(512), actor) != C35_OK) {
        goto fail;
    }
    return 1;

fail:
    if (runtime->claimed) {
        bool released = false;
        enum c35_result release = c35_bundle_release(runtime->bundle, nonce);

        if (release == C35_OK ||
            (c35_bundle_release_query(
                 runtime->bundle, nonce, &released) == C35_OK && released)) {
            runtime->claimed = 0;
            runtime->bundle_released = 1;
        }
    }
    return 0;
}

static enum c35_result observe_publication(
    struct c35_runtime *runtime,
    const struct c35_publication *publication
)
{
    enum c35_result result;

    if (publication == NULL ||
        publication->version != C35_PUBLICATION_VERSION) {
        runtime->last_observation = C35_OBSERVATION_LOST_INVALID_SINK;
        return C35_INVALID;
    }
    result = c35_trace_append(&runtime->trace, publication);
    runtime->last_observation = result == C35_OK ?
        C35_OBSERVATION_RECORDED :
        result == C35_NO_CAPACITY ? C35_OBSERVATION_LOST_NO_CAPACITY :
                                    C35_OBSERVATION_LOST_INVALID_SINK;
    return result;
}

int c35_runtime_teardown(struct c35_runtime *runtime)
{
    struct c35_operation_status status;
    struct c35_operation_token token;
    enum c35_result result;
    unsigned int iteration;

    if (runtime == NULL || runtime->dma_operation.used) return 0;
    if (runtime->headless.instance_nonce == 0) {
        bool released = false;

        if (!runtime->claimed) return 1;
        result = c35_bundle_release(runtime->bundle, runtime->nonce);
        if (result != C35_OK &&
            (c35_bundle_release_query(
                 runtime->bundle, runtime->nonce, &released) != C35_OK ||
             !released)) return 0;
        runtime->claimed = 0;
        runtime->bundle_released = 1;
        return 1;
    }
    if (runtime->teardown_committed && runtime->bundle_released &&
        !runtime->finalizer.used) return 1;
    if (!runtime->finalizer.used) {
        result = c35_finalizer_start(
            &runtime->finalizer, &runtime->headless, runtime->bundle,
            runtime->nonce, &token);
        if (result != C35_OK) return 0;
    } else {
        token = runtime->finalizer.token;
    }
    memset(&status, 0, sizeof(status));
    for (iteration = 0; iteration < 8192; ++iteration) {
        result = c35_finalizer_progress(
            &runtime->finalizer, &token, 1, &status);
        if (result == C35_OK || result != C35_IN_PROGRESS) break;
    }
    if (result != C35_OK || status.call_state != C35_CALL_DONE) return 0;
    if (status.publication_valid) {
        runtime->teardown_publication = status.publication;
        runtime->teardown_committed =
            status.commit_state == C35_COMMIT_COMMITTED;
    }
    runtime->bundle_released = runtime->finalizer.bundle_released;
    if (status.outcome != C35_OK ||
        status.cleanup_state != C35_CLEANUP_COMPLETE) return 0;
    runtime->claimed = 0;
    if (!runtime->teardown_observed) {
        (void)observe_publication(
            runtime, &runtime->teardown_publication);
        runtime->teardown_observed = 1;
    }
    if (c35_finalizer_retire(&runtime->finalizer, &token) != C35_OK) return 0;
    return 1;
}

int c35_runtime_projection(
    struct c35_runtime *runtime,
    uint8_t bytes[C35_RAW_PROJECTION_BYTES]
)
{
    return runtime->claimed &&
           c35_bundle_projection(runtime->bundle, bytes) == C35_OK;
}

static int dma_token_equal(
    const struct c35_operation_token *left,
    const struct c35_operation_token *right
)
{
    return left != NULL && right != NULL &&
           left->instance_nonce == right->instance_nonce &&
           left->uid == right->uid && left->generation == right->generation &&
           left->kind == C35_OPERATION_DMA && right->kind == C35_OPERATION_DMA &&
           left->reserved[0] == 0 && left->reserved[1] == 0 &&
           left->reserved[2] == 0 && right->reserved[0] == 0 &&
           right->reserved[1] == 0 && right->reserved[2] == 0;
}

static int dma_command_valid(
    const struct c35_runtime *runtime,
    const struct fwlab_c31_command_handle *command
)
{
    return command->instance_nonce == runtime->nonce &&
           command->command_uid != 0 && command->controller_epoch != 0 &&
           command->slot_generation != 0;
}

static int dma_lease_valid(
    const struct c35_runtime *runtime,
    const struct c35_dma_transaction *operation
)
{
    const struct fwlab_c31_completion_lease *lease = &operation->lease;
    const struct fwlab_c31_command_handle *command = &operation->command;

    return dma_command_valid(runtime, &lease->command) &&
           lease->command.command_uid == command->command_uid &&
           lease->command.controller_epoch == command->controller_epoch &&
           lease->command.slot == command->slot &&
           lease->command.slot_generation == command->slot_generation &&
           lease->lease_generation != 0 && lease->reserved == 0;
}

static enum c35_result dma_map_c31(enum fwlab_c31_api_result result)
{
    switch (result) {
    case FWLAB_C31_API_OK: return C35_OK;
    case FWLAB_C31_API_NO_CAPACITY: return C35_NO_CAPACITY;
    case FWLAB_C31_API_INVALID_CONTRACT: return C35_INVALID;
    case FWLAB_C31_API_WRONG_STATE: return C35_WRONG_STATE;
    case FWLAB_C31_API_STALE_TOKEN: return C35_STALE;
    case FWLAB_C31_API_UNSUPPORTED_VERSION: return C35_UNSUPPORTED_VERSION;
    case FWLAB_C31_API_COUNTER_EXHAUSTED: return C35_COUNTER_EXHAUSTED;
    case FWLAB_C31_API_NOT_FOUND: return C35_NOT_FOUND;
    case FWLAB_C31_API_INVARIANT_FAILURE: return C35_INVARIANT;
    default: return C35_INVARIANT;
    }
}

static void dma_finish(
    struct c35_dma_transaction *operation,
    enum c35_result outcome,
    uint32_t commit_state,
    uint32_t cleanup_state
)
{
    operation->outcome = outcome;
    operation->commit_state = commit_state;
    operation->cleanup_state = cleanup_state;
    operation->phase = C35_DMA_DONE;
    operation->finished = 1;
}

static void dma_publication(
    const struct c35_runtime *runtime,
    struct c35_dma_transaction *operation
)
{
    struct c35_publication *publication = &operation->publication;

    memset(publication, 0, sizeof(*publication));
    publication->version = C35_PUBLICATION_VERSION;
    publication->size = sizeof(*publication);
    publication->kind = C35_PUBLICATION_DMA;
    publication->actor = runtime->actor;
    publication->completion_result = (uint8_t)operation->intent.result;
    publication->effect_class = operation->intent.fault.effect_class;
    publication->epoch = runtime->headless.owner_epoch;
    publication->commit_state = C35_COMMIT_COMMITTED;
    publication->publication_uid = UINT64_C(0x35ad000000000000) ^
                                   operation->token.uid;
    memcpy(publication->semantic.payload[0], operation->output,
           C35_ATOM_BYTES);
}

static void dma_status_fill(
    const struct c35_runtime *runtime,
    const struct c35_dma_transaction *operation,
    struct c35_operation_status *status
)
{
    memset(status, 0, sizeof(*status));
    status->version = C35_OPERATION_VERSION;
    status->size = sizeof(*status);
    status->token = operation->token;
    status->call_state = operation->finished ?
        C35_CALL_DONE : C35_CALL_IN_PROGRESS;
    status->operation_kind = C35_OPERATION_DMA;
    status->commit_state = (uint8_t)operation->commit_state;
    status->cleanup_state = (uint8_t)operation->cleanup_state;
    status->outcome = operation->outcome;
    status->service_phase = runtime->headless.service_phase;
    status->internal_phase = operation->phase;
    status->cause_domain = operation->cause_domain;
    status->cause_code = operation->cause_code;
    status->retry_class = operation->finished ? C35_RETRY_NONE :
                          C35_RETRY_SAME_TOKEN;
    status->publication_valid =
        operation->publication.version == C35_PUBLICATION_VERSION;
    if (status->publication_valid) status->publication = operation->publication;
}

enum c35_result c35_dma_start(
    struct c35_runtime *runtime,
    const uint8_t input[C35_ATOM_BYTES],
    struct c35_operation_token *token
)
{
    struct c35_dma_transaction *operation;
    uint64_t identity;

    if (runtime == NULL || input == NULL || token == NULL ||
        runtime->headless.service_phase != C35_SERVICE_READY)
        return C35_INVALID;
    if (runtime->dma_operation.used) return C35_WRONG_STATE;
    if (runtime->dma_next == 0 || runtime->dma_next > 512 ||
        runtime->dma_next > UINT32_MAX) return C35_COUNTER_EXHAUSTED;
    identity = runtime->dma_next++;
    operation = &runtime->dma_operation;
    memset(operation, 0, sizeof(*operation));
    operation->used = 1;
    operation->phase = C35_DMA_REGISTER_BUFFER;
    operation->outcome = C35_IN_PROGRESS;
    operation->commit_state = C35_COMMIT_NOT_STARTED;
    operation->capability_index = -1;
    operation->token.instance_nonce = runtime->nonce;
    operation->token.uid = identity;
    operation->token.generation = (uint32_t)identity;
    operation->token.kind = C35_OPERATION_DMA;
    memcpy(operation->input, input, C35_ATOM_BYTES);
    operation->descriptor.version = FWLAB_C31_CONTRACT_VERSION;
    operation->descriptor.size = sizeof(operation->descriptor);
    operation->descriptor.origin.word[0] = runtime->nonce ^ identity;
    operation->descriptor.origin.word[1] = UINT64_C(0xd3500000) | identity;
    operation->descriptor.trace_cookie = identity;
    operation->descriptor.provider_request.word[0] =
        UINT64_C(0xd350000000000000) | identity;
    operation->descriptor.provider_request.word[1] = ~identity;
    operation->descriptor.capability.word[0] =
        UINT64_C(0xca35000000000000) | identity;
    operation->descriptor.capability.word[1] =
        UINT64_C(0xcb35000000000000) | identity;
    operation->descriptor.controller_region = 0;
    operation->descriptor.length = C35_ATOM_BYTES;
    operation->descriptor.provider_kind = FWLAB_C31_PROVIDER_DMA;
    operation->descriptor.dma_direction = FWLAB_C31_DMA_TO_CONTROLLER;
    operation->scenario.request = operation->descriptor.provider_request;
    operation->scenario.terminal = FWLAB_C31_PROVIDER_SUCCESS;
    operation->scenario.effect_class = FWLAB_C31_EFFECT_FULL;
    *token = operation->token;
    return C35_OK;
}

static void dma_progress_one(
    struct c35_runtime *runtime,
    struct c35_dma_transaction *operation
)
{
    enum fwlab_c31_api_result lower;
    enum fwlab_c31_lifecycle_state state;

    switch (operation->phase) {
    case C35_DMA_REGISTER_BUFFER:
        operation->capability_index = c31_fake_dma_register(
            &runtime->dma, &operation->descriptor.capability,
            &operation->descriptor.origin, runtime->nonce,
            runtime->headless.owner_epoch, FWLAB_C31_DMA_TO_CONTROLLER,
            operation->input, C35_ATOM_BYTES);
        if (operation->capability_index >= 0)
            operation->phase = C35_DMA_ADD_SCENARIO;
        else
            dma_finish(operation, C35_NO_CAPACITY,
                       C35_COMMIT_NOT_STARTED, C35_CLEANUP_COMPLETE);
        break;
    case C35_DMA_ADD_SCENARIO:
        if (c31_fake_dma_add(&runtime->dma, &operation->scenario))
            operation->phase = C35_DMA_SUBMIT;
        else
            dma_finish(operation, C35_COUNTER_EXHAUSTED,
                       C35_COMMIT_NOT_STARTED, C35_CLEANUP_COMPLETE);
        break;
    case C35_DMA_SUBMIT:
        lower = runtime->lifecycle_port.ops->submit(
            runtime->lifecycle_port.context, &operation->descriptor,
            &operation->command);
        if (lower == FWLAB_C31_API_OK) {
            operation->command_valid = 1;
            operation->phase = C35_DMA_WAIT_READY;
        } else if (dma_command_valid(runtime, &operation->command)) {
            operation->command_valid = 1;
            operation->cause_domain = C35_CAUSE_C31;
            operation->cause_code = (uint32_t)lower;
            operation->phase = C35_DMA_SUBMIT_QUERY;
        } else {
            if (runtime->lifecycle_port.ops->phase(
                    runtime->lifecycle_port.context) ==
                FWLAB_C31_INSTANCE_FAULTED)
                runtime->headless.service_phase = C35_SERVICE_FAULTED_CLEANUP;
            dma_finish(operation, dma_map_c31(lower),
                       C35_COMMIT_NOT_STARTED, C35_CLEANUP_COMPLETE);
        }
        break;
    case C35_DMA_SUBMIT_QUERY:
        lower = runtime->lifecycle_port.ops->command_state(
            runtime->lifecycle_port.context, &operation->command, &state);
        if (lower == FWLAB_C31_API_OK)
            operation->phase = C35_DMA_WAIT_READY;
        else if (lower == FWLAB_C31_API_STALE_TOKEN ||
                 lower == FWLAB_C31_API_NOT_FOUND)
            dma_finish(operation, dma_map_c31(
                           (enum fwlab_c31_api_result)operation->cause_code),
                       C35_COMMIT_NOT_STARTED, C35_CLEANUP_COMPLETE);
        break;
    case C35_DMA_WAIT_READY:
        lower = runtime->lifecycle_port.ops->command_state(
            runtime->lifecycle_port.context, &operation->command, &state);
        if (lower == FWLAB_C31_API_OK &&
            state == FWLAB_C31_CMD_COMPLETION_READY) {
            operation->phase = C35_DMA_ACQUIRE;
        } else if (lower == FWLAB_C31_API_OK) {
            struct fwlab_c31_step_result step;

            lower = runtime->lifecycle_port.ops->step(
                runtime->lifecycle_port.context, 1, &step);
            if (lower != FWLAB_C31_API_OK) {
                operation->cause_domain = C35_CAUSE_C31;
                operation->cause_code = (uint32_t)lower;
                if (runtime->lifecycle_port.ops->phase(
                        runtime->lifecycle_port.context) ==
                    FWLAB_C31_INSTANCE_FAULTED) {
                    runtime->headless.service_phase =
                        C35_SERVICE_FAULTED_CLEANUP;
                    dma_finish(operation, dma_map_c31(lower),
                               C35_COMMIT_UNKNOWN, C35_CLEANUP_PENDING);
                }
            }
        } else if (lower != FWLAB_C31_API_NO_CAPACITY) {
            dma_finish(operation, dma_map_c31(lower),
                       C35_COMMIT_NOT_STARTED, C35_CLEANUP_COMPLETE);
        }
        break;
    case C35_DMA_ACQUIRE:
        lower = runtime->lifecycle_port.ops->completion_acquire(
            runtime->lifecycle_port.context, &operation->command,
            &operation->lease, &operation->intent);
        if (lower == FWLAB_C31_API_OK) {
            operation->lease_valid = 1;
            operation->phase = C35_DMA_CAPTURE;
        } else {
            operation->cause_domain = C35_CAUSE_C31;
            operation->cause_code = (uint32_t)lower;
            operation->phase = C35_DMA_ACQUIRE_QUERY;
        }
        break;
    case C35_DMA_ACQUIRE_QUERY:
        lower = runtime->lifecycle_port.ops->command_state(
            runtime->lifecycle_port.context, &operation->command, &state);
        if (lower == FWLAB_C31_API_OK &&
            state == FWLAB_C31_CMD_COMPLETION_LEASED &&
            dma_lease_valid(runtime, operation)) {
            operation->lease_valid = 1;
            operation->phase = C35_DMA_CAPTURE;
        } else if (lower == FWLAB_C31_API_OK &&
                   state == FWLAB_C31_CMD_COMPLETION_READY) {
            memset(&operation->lease, 0, sizeof(operation->lease));
            operation->phase = C35_DMA_ACQUIRE;
        }
        break;
    case C35_DMA_CAPTURE:
        if (operation->intent.result != FWLAB_C31_COMPLETION_SUCCESS) {
            dma_finish(operation, C35_PROVIDER_FAILURE,
                       C35_COMMIT_NOT_STARTED, C35_CLEANUP_PENDING);
        } else {
            memcpy(operation->output,
                   c31_fake_dma_controller(&runtime->dma, 0),
                   C35_ATOM_BYTES);
            operation->phase = C35_DMA_CONSUME;
        }
        break;
    case C35_DMA_CONSUME:
        lower = runtime->lifecycle_port.ops->completion_consume(
            runtime->lifecycle_port.context, &operation->lease);
        if (lower == FWLAB_C31_API_OK) {
            operation->lease_valid = 0;
            dma_publication(runtime, operation);
            dma_finish(operation, C35_OK, C35_COMMIT_COMMITTED,
                       C35_CLEANUP_COMPLETE);
        } else {
            operation->cause_domain = C35_CAUSE_C31;
            operation->cause_code = (uint32_t)lower;
            operation->phase = C35_DMA_CONSUME_QUERY;
        }
        break;
    case C35_DMA_CONSUME_QUERY:
        lower = runtime->lifecycle_port.ops->command_state(
            runtime->lifecycle_port.context, &operation->command, &state);
        if ((lower == FWLAB_C31_API_STALE_TOKEN ||
             lower == FWLAB_C31_API_NOT_FOUND) &&
            runtime->lifecycle_port.ops->phase(
                runtime->lifecycle_port.context) == FWLAB_C31_INSTANCE_READY) {
            operation->lease_valid = 0;
            dma_publication(runtime, operation);
            dma_finish(operation, C35_OK, C35_COMMIT_COMMITTED,
                       C35_CLEANUP_COMPLETE);
        } else if (lower == FWLAB_C31_API_OK &&
                   state == FWLAB_C31_CMD_COMPLETION_LEASED) {
            operation->phase = C35_DMA_CONSUME;
        }
        break;
    default:
        dma_finish(operation, C35_INVARIANT, C35_COMMIT_UNKNOWN,
                   C35_CLEANUP_POISONED);
        runtime->headless.service_phase = C35_SERVICE_POISONED;
        break;
    }
}

enum c35_result c35_dma_progress(
    struct c35_runtime *runtime,
    const struct c35_operation_token *token,
    uint32_t budget,
    struct c35_operation_status *status
)
{
    struct c35_dma_transaction *operation;
    uint32_t used = 0;

    if (runtime == NULL || token == NULL || status == NULL)
        return C35_INVALID;
    operation = &runtime->dma_operation;
    if (!operation->used || !dma_token_equal(&operation->token, token))
        return C35_STALE;
    while (!operation->finished && used < budget) {
        dma_progress_one(runtime, operation);
        ++used;
    }
    dma_status_fill(runtime, operation, status);
    status->units_used = used;
    return operation->finished ? C35_OK : C35_IN_PROGRESS;
}

enum c35_result c35_dma_query(
    const struct c35_runtime *runtime,
    const struct c35_operation_token *token,
    struct c35_operation_status *status
)
{
    const struct c35_dma_transaction *operation;

    if (runtime == NULL || token == NULL || status == NULL)
        return C35_INVALID;
    operation = &runtime->dma_operation;
    if (!operation->used || !dma_token_equal(&operation->token, token))
        return C35_STALE;
    dma_status_fill(runtime, operation, status);
    status->units_used = 0;
    return operation->finished ? C35_OK : C35_IN_PROGRESS;
}

enum c35_result c35_dma_finalize(
    const struct c35_runtime *runtime,
    const struct c35_operation_token *token,
    struct c35_operation_status *status,
    uint8_t output[C35_ATOM_BYTES],
    struct c35_publication *publication
)
{
    enum c35_result result;

    if (output == NULL || publication == NULL) return C35_INVALID;
    result = c35_dma_query(runtime, token, status);
    if (result != C35_OK) return result;
    if (status->outcome == C35_OK && status->publication_valid) {
        memcpy(output, runtime->dma_operation.output, C35_ATOM_BYTES);
        *publication = runtime->dma_operation.publication;
    }
    return C35_OK;
}

enum c35_result c35_dma_retire(
    struct c35_runtime *runtime,
    const struct c35_operation_token *token
)
{
    if (runtime == NULL || token == NULL || !runtime->dma_operation.used ||
        !runtime->dma_operation.finished ||
        !dma_token_equal(&runtime->dma_operation.token, token))
        return C35_STALE;
    memset(&runtime->dma_operation, 0, sizeof(runtime->dma_operation));
    return C35_OK;
}

enum c35_result c35_dma_capture_status(
    struct c35_runtime *runtime,
    const uint8_t input[C35_ATOM_BYTES],
    uint8_t output[C35_ATOM_BYTES],
    struct c35_operation_status *status,
    struct c35_publication *publication
)
{
    struct c35_operation_token token;
    unsigned int iteration;
    enum c35_result result = c35_dma_start(runtime, input, &token);

    if (result != C35_OK) return result;
    for (iteration = 0; iteration < 1024; ++iteration) {
        result = c35_dma_progress(runtime, &token, 1, status);
        if (result == C35_OK || result != C35_IN_PROGRESS) break;
    }
    if (result == C35_OK)
        result = c35_dma_finalize(
            runtime, &token, status, output, publication);
    if (status->call_state == C35_CALL_DONE) {
        enum c35_result outcome = (enum c35_result)status->outcome;

        (void)c35_dma_retire(runtime, &token);
        return result == C35_OK ? outcome : result;
    }
    return result;
}

int c35_dma_capture(
    struct c35_runtime *runtime,
    const uint8_t input[C35_ATOM_BYTES],
    uint8_t output[C35_ATOM_BYTES]
)
{
    struct c35_operation_status status;
    struct c35_publication publication;
    enum c35_result result = c35_dma_capture_status(
        runtime, input, output, &status, &publication);

    if (result != C35_OK) return 0;
    (void)observe_publication(runtime, &publication);
    return 1;
}

enum c35_result c35_run_command_status(
    struct c35_runtime *runtime,
    const struct c35_request *request,
    struct c35_semantic_result *result,
    struct c35_operation_status *status
)
{
    struct fwlab_c31_command_handle command;
    struct c35_operation_token token;
    enum c35_result operation;
    unsigned int iteration;

    if (runtime == NULL || request == NULL || result == NULL || status == NULL)
        return C35_INVALID;
    operation = c35_headless_submit(&runtime->headless, request, &command);
    if (operation != C35_OK) return operation;
    operation = c35_completion_start(&runtime->headless, &command, &token);
    if (operation != C35_OK) return operation;
    memset(status, 0, sizeof(*status));
    for (iteration = 0; iteration < 8192; ++iteration) {
        operation = c35_operation_progress(
            &runtime->headless, &token, 1, status);
        if (operation == C35_OK || operation != C35_IN_PROGRESS) break;
    }
    if (status->publication_valid) {
        *result = status->publication.semantic;
        (void)observe_publication(runtime, &status->publication);
    }
    if (status->call_state == C35_CALL_DONE)
        (void)c35_operation_retire(&runtime->headless, &token);
    return operation == C35_OK ? (enum c35_result)status->outcome : operation;
}

int c35_run_command(
    struct c35_runtime *runtime,
    const struct c35_request *request,
    struct c35_semantic_result *result
)
{
    struct c35_operation_status status;
    enum c35_result operation = c35_run_command_status(
        runtime, request, result, &status);

    return operation == C35_OK && status.publication_valid &&
           status.publication.completion_result ==
               FWLAB_C31_COMPLETION_SUCCESS;
}

static struct c35_request request_base(uint8_t kind)
{
    struct c35_request request;

    memset(&request, 0, sizeof(request));
    request.version = C35_REQUEST_VERSION;
    request.size = sizeof(request);
    request.kind = kind;
    request.scope = 9;
    return request;
}

struct c35_request c35_request_read(uint8_t atom)
{
    struct c35_request request = request_base(C35_READ);

    request.durability_kind = FWLAB_PERSIST_DEFAULT;
    request.atom = atom;
    return request;
}

struct c35_request c35_request_write(
    uint8_t atom,
    uint8_t durability,
    uint32_t sequence,
    const uint8_t payload[C35_ATOM_BYTES]
)
{
    struct c35_request request = request_base(C35_WRITE);

    request.durability_kind = durability;
    request.atom_mask = (uint8_t)(1u << atom);
    request.atom = atom;
    request.sequence = sequence;
    memcpy(request.payload[atom], payload, C35_ATOM_BYTES);
    return request;
}

struct c35_request c35_request_write_mask(
    uint8_t mask,
    uint8_t durability,
    uint32_t sequence,
    const uint8_t payload[C35_ATOMS][C35_ATOM_BYTES]
)
{
    struct c35_request request = request_base(C35_WRITE);

    request.durability_kind = durability;
    request.atom_mask = mask;
    request.atom = 0;
    request.sequence = sequence;
    memcpy(request.payload, payload, sizeof(request.payload));
    return request;
}

struct c35_request c35_request_trim(
    uint8_t atom,
    uint8_t durability,
    uint32_t sequence
)
{
    return c35_request_trim_mask(
        (uint8_t)(1u << atom), durability, sequence);
}

struct c35_request c35_request_trim_mask(
    uint8_t mask,
    uint8_t durability,
    uint32_t sequence
)
{
    struct c35_request request = request_base(C35_TRIM);

    request.durability_kind = durability;
    request.atom_mask = mask;
    request.atom = 0;
    request.sequence = sequence;
    return request;
}

struct c35_request c35_request_fence(uint32_t frontier)
{
    struct c35_request request = request_base(C35_FENCE);

    request.durability_kind = FWLAB_PERSIST_FENCE;
    request.frontier = frontier;
    return request;
}
