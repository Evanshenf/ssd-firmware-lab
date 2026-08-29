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

    return storage_providers(storage, &media, &physical) &&
           c35_bundle_init(
               &storage->bundle, UINT64_C(0xb35d1e0000000000) |
                   (uint64_t)storage->lane, 1, 1, &media, &physical) ==
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
    config.capacity.operation_generation_limit = 256;
    config.capacity.cache_generation_limit = 256;
    config.capacity.controller_epoch_limit = 16;
    config.capacity.submit_sequence_limit = 512;
    config.capacity.operation_uid_limit = 256;
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
    memcpy(storage->uuid, uuid, sizeof(storage->uuid));
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
    const struct fwlab_nfc_model_config *config
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
           config->ecc.oob_covered_bytes == C34_OOB_BYTES;
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
        (lane != C35_LANE_SCRIPTED && !fixed_profile_valid(nfc_config))) {
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
        c34_config.inner_uid_limit = 256;
        c34_config.physical_op_limit = 256;
        c34_config.physical_sequence_limit = 256;
        if (c34_init(
                runtime->c34_arena.bytes, sizeof(runtime->c34_arena.bytes),
                &c34_config, &buffers, &nfc, &media, &physical,
                &runtime->firmware) != C34_OK ||
            c35_c34_binding_init(
                &runtime->c34_binding, runtime->firmware, nonce, 1) !=
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
            FWLAB_C31_API_OK ||
        c35_headless_init(
            &runtime->headless, runtime->lifecycle, &binding,
            &runtime->trace, nonce, 1, actor) != C35_OK) {
        goto fail;
    }
    return 1;

fail:
    if (runtime->claimed) {
        (void)c35_bundle_release(runtime->bundle, nonce);
        runtime->claimed = 0;
    }
    return 0;
}

int c35_runtime_teardown(struct c35_runtime *runtime)
{
    if (c35_headless_teardown(&runtime->headless, 8192) != C35_OK) {
        return 0;
    }
    if (runtime->claimed &&
        c35_bundle_release(runtime->bundle, runtime->nonce) != C35_OK) {
        return 0;
    }
    runtime->claimed = 0;
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

int c35_dma_capture(
    struct c35_runtime *runtime,
    const uint8_t input[C35_ATOM_BYTES],
    uint8_t output[C35_ATOM_BYTES]
)
{
    uint64_t identity = runtime->dma_next++;
    struct fwlab_c31_command_descriptor descriptor;
    struct c31_fake_dma_scenario scenario;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;
    struct c35_trace_event event;
    unsigned int iteration;

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.version = FWLAB_C31_CONTRACT_VERSION;
    descriptor.size = sizeof(descriptor);
    descriptor.origin.word[0] = runtime->nonce ^ identity;
    descriptor.origin.word[1] = UINT64_C(0xd3500000) | identity;
    descriptor.trace_cookie = identity;
    descriptor.provider_request.word[0] = UINT64_C(0xd350000000000000) |
                                          identity;
    descriptor.provider_request.word[1] = ~identity;
    descriptor.capability.word[0] = UINT64_C(0xca35000000000000) |
                                    identity;
    descriptor.capability.word[1] = UINT64_C(0xcb35000000000000) |
                                    identity;
    descriptor.controller_region = 0;
    descriptor.length = C35_ATOM_BYTES;
    descriptor.provider_kind = FWLAB_C31_PROVIDER_DMA;
    descriptor.dma_direction = FWLAB_C31_DMA_TO_CONTROLLER;
    if (c31_fake_dma_register(
            &runtime->dma, &descriptor.capability, &descriptor.origin,
            runtime->nonce, runtime->headless.owner_epoch,
            FWLAB_C31_DMA_TO_CONTROLLER, input, C35_ATOM_BYTES) < 0) {
        return 0;
    }
    memset(&scenario, 0, sizeof(scenario));
    scenario.request = descriptor.provider_request;
    scenario.terminal = FWLAB_C31_PROVIDER_SUCCESS;
    scenario.effect_class = FWLAB_C31_EFFECT_FULL;
    if (!c31_fake_dma_add(&runtime->dma, &scenario) ||
        fwlab_c31_submit(runtime->lifecycle, &descriptor, &command) !=
            FWLAB_C31_API_OK) {
        return 0;
    }
    for (iteration = 0; iteration < 256; ++iteration) {
        enum fwlab_c31_lifecycle_state state;
        struct fwlab_c31_step_result step;

        if (fwlab_c31_command_state(
                runtime->lifecycle, &command, &state) != FWLAB_C31_API_OK) {
            return 0;
        }
        if (state == FWLAB_C31_CMD_COMPLETION_READY) {
            break;
        }
        if (fwlab_c31_step(runtime->lifecycle, 1, &step) !=
            FWLAB_C31_API_OK) {
            return 0;
        }
    }
    if (iteration == 256 ||
        fwlab_c31_completion_acquire(
            runtime->lifecycle, &command, &lease, &intent) !=
            FWLAB_C31_API_OK ||
        intent.result != FWLAB_C31_COMPLETION_SUCCESS) {
        return 0;
    }
    memcpy(output, c31_fake_dma_controller(&runtime->dma, 0),
           C35_ATOM_BYTES);
    if (fwlab_c31_completion_consume(runtime->lifecycle, &lease) !=
        FWLAB_C31_API_OK) {
        return 0;
    }
    memset(&event, 0, sizeof(event));
    event.kind = C35_TRACE_DMA;
    event.actor = runtime->actor;
    event.completion_result = (uint8_t)intent.result;
    event.effect_class = intent.fault.effect_class;
    event.epoch = runtime->headless.owner_epoch;
    event.ordinal = runtime->trace.events;
    memcpy(event.semantic.payload[0], output, C35_ATOM_BYTES);
    return c35_trace_append(&runtime->trace, &event) == C35_OK;
}

int c35_run_command(
    struct c35_runtime *runtime,
    const struct c35_request *request,
    struct c35_semantic_result *result
)
{
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_intent intent;

    return c35_headless_submit(
               &runtime->headless, request, &command) == C35_OK &&
           c35_headless_complete(
               &runtime->headless, &command, result, &intent) == C35_OK &&
           intent.result == FWLAB_C31_COMPLETION_SUCCESS;
}

static struct c35_request request_base(uint8_t kind)
{
    struct c35_request request;

    memset(&request, 0, sizeof(request));
    request.version = C35_BINDING_VERSION;
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

struct c35_request c35_request_trim(
    uint8_t atom,
    uint8_t durability,
    uint32_t sequence
)
{
    struct c35_request request = request_base(C35_TRIM);

    request.durability_kind = durability;
    request.atom_mask = (uint8_t)(1u << atom);
    request.atom = atom;
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
