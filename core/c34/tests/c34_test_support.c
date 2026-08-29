/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_test_support.h"

#include <string.h>

static struct fwlab_c31_provider_submit_result dma_submit(
    void *context,
    const struct fwlab_c31_provider_request *request
)
{
    struct fwlab_c31_provider_submit_result result;

    memset(&result, 0, sizeof(result));
    result.disposition = FWLAB_C31_PROVIDER_REJECTED;
    result.fault.domain = FWLAB_C31_FAULT_RESOURCE;
    result.fault.retry_class = FWLAB_C31_RETRY_NEVER;
    result.fault.effect_class = FWLAB_C31_EFFECT_NONE;
    result.fault.reason = FWLAB_C31_REASON_UNSUPPORTED_WORK;
    (void)context;
    (void)request;
    return result;
}

static enum fwlab_c31_api_result dma_cancel(
    void *context,
    const struct fwlab_c31_operation_token *operation
)
{
    (void)context;
    (void)operation;
    return FWLAB_C31_API_NOT_FOUND;
}

static enum fwlab_c31_api_result dma_poll(
    void *context,
    uint32_t budget,
    struct fwlab_c31_provider_event *events,
    uint32_t event_capacity,
    uint32_t *event_count
)
{
    (void)context;
    (void)budget;
    (void)events;
    (void)event_capacity;
    *event_count = 0;
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result dma_reset(
    void *context,
    uint64_t nonce,
    uint32_t epoch
)
{
    (void)context;
    (void)nonce;
    (void)epoch;
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result dma_quiescent(
    void *context,
    uint64_t nonce,
    uint32_t epoch,
    bool *quiescent
)
{
    (void)context;
    (void)nonce;
    (void)epoch;
    *quiescent = true;
    return FWLAB_C31_API_OK;
}

static const struct fwlab_c31_provider_ops dma_ops = {
    .version = FWLAB_C31_PROVIDER_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_c31_provider_ops),
    .reserved = 0,
    .try_submit = dma_submit,
    .cancel = dma_cancel,
    .poll = dma_poll,
    .reset_begin = dma_reset,
    .quiescent = dma_quiescent,
};

struct fwlab_nfc_model_config c34_test_nfc_config(uint64_t seed)
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
    config.capacity.controller_epoch_limit = 8;
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
    capacity.slot_generation_limit = 256;
    capacity.operation_generation_limit = 256;
    capacity.lease_generation_limit = 256;
    capacity.ticket_generation_limit = 256;
    capacity.controller_epoch_limit = 8;
    capacity.command_uid_limit = 256;
    return capacity;
}

static int init_bound(
    struct c34_test_environment *environment,
    uint8_t cache_enabled,
    uint64_t nonce,
    uint64_t seed,
    const struct fwlab_nand_media *media,
    const struct c34_physical_txn_provider *physical
)
{
    struct fwlab_nfc_model_config nfc_config =
        c34_test_nfc_config(seed);
    struct fwlab_nfc_buffer_provider buffers;
    struct fwlab_nfc_provider nfc;
    struct c34_config c34_config;
    struct fwlab_c31_capacity capacity = c31_capacity();
    struct fwlab_c31_provider_set providers;

    environment->nonce = nonce;
    environment->next_token = 1;
    c34_fake_buffer_init(&environment->buffer, 7);
    buffers = c34_fake_buffer_provider(&environment->buffer);
    if (fwlab_nfc_model_arena_size(&nfc_config) >
            sizeof(environment->nfc_arena.bytes) ||
        fwlab_nfc_model_init(
            environment->nfc_arena.bytes,
            sizeof(environment->nfc_arena.bytes), &nfc_config, nonce,
            &buffers, media, &environment->nfc_model) != FWLAB_NFC_API_OK) {
        return 0;
    }
    nfc = fwlab_nfc_model_provider(environment->nfc_model);
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
    if (c34_arena_size(&c34_config) > sizeof(environment->c34_arena.bytes) ||
        c34_init(
            environment->c34_arena.bytes,
            sizeof(environment->c34_arena.bytes), &c34_config, &buffers,
            &nfc, media, physical, &environment->c34) != C34_OK) {
        return 0;
    }
    memset(&providers, 0, sizeof(providers));
    providers.dma.ops = &dma_ops;
    providers.dma.context = environment;
    providers.nfc = c34_c31_provider(environment->c34);
    return fwlab_c31_arena_size(&capacity) <=
               sizeof(environment->c31_arena.bytes) &&
           fwlab_c31_init(
               environment->c31_arena.bytes,
               sizeof(environment->c31_arena.bytes), &capacity, nonce,
               &providers, &environment->c31) == FWLAB_C31_API_OK;
}

int c34_test_init(
    struct c34_test_environment *environment,
    uint8_t cache_enabled,
    uint64_t nonce,
    uint64_t seed
)
{
    struct fwlab_nand_media media;
    struct c34_physical_txn_provider physical;

    memset(environment, 0, sizeof(*environment));
    c34_memory_media_init(&environment->media);
    media = c34_memory_media_provider(&environment->media);
    physical = c34_memory_txn_provider(&environment->media);
    return init_bound(environment, cache_enabled, nonce, seed, &media,
                      &physical);
}

int c34_test_init_bound(
    struct c34_test_environment *environment,
    uint8_t cache_enabled,
    uint64_t nonce,
    uint64_t seed,
    const struct fwlab_nand_media *media,
    const struct c34_physical_txn_provider *physical
)
{
    if (environment == NULL || media == NULL || physical == NULL) {
        return 0;
    }
    memset(environment, 0, sizeof(*environment));
    return init_bound(environment, cache_enabled, nonce, seed, media,
                      physical);
}

static struct fwlab_c31_request_token next_token(
    struct c34_test_environment *environment
)
{
    struct fwlab_c31_request_token token;

    token.word[0] = UINT64_C(0xc340000000000000) |
                    environment->next_token;
    token.word[1] = environment->nonce ^ environment->next_token;
    ++environment->next_token;
    return token;
}

int c34_test_submit(
    struct c34_test_environment *environment,
    const struct c34_request *request,
    struct fwlab_c31_command_handle *command,
    struct fwlab_c31_completion_lease *lease,
    struct fwlab_c31_completion_intent *intent,
    struct c34_command_result *result
)
{
    struct fwlab_c31_request_token token = next_token(environment);
    struct fwlab_c31_command_descriptor descriptor;
    unsigned int iteration;

    if (c34_request_register(environment->c34, &token, request) != C34_OK) {
        return 0;
    }
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.version = FWLAB_C31_CONTRACT_VERSION;
    descriptor.size = sizeof(descriptor);
    descriptor.origin.word[0] = UINT64_C(0x1111000000000000) |
                                environment->next_token;
    descriptor.origin.word[1] = environment->nonce;
    descriptor.trace_cookie = environment->next_token;
    descriptor.provider_request = token;
    descriptor.provider_kind = FWLAB_C31_PROVIDER_NFC;
    descriptor.dma_direction = FWLAB_C31_DMA_NONE;
    if (fwlab_c31_submit(environment->c31, &descriptor, command) !=
        FWLAB_C31_API_OK) {
        return 0;
    }
    for (iteration = 0; iteration < 4096; ++iteration) {
        struct fwlab_c31_step_result step;
        enum fwlab_c31_lifecycle_state state;

        if (fwlab_c31_step(environment->c31, 1, &step) !=
                FWLAB_C31_API_OK ||
            fwlab_c31_command_state(environment->c31, command, &state) !=
                FWLAB_C31_API_OK) {
            return 0;
        }
        if (state == FWLAB_C31_CMD_COMPLETION_READY) {
            if (fwlab_c31_completion_acquire(
                    environment->c31, command, lease, intent) !=
                    FWLAB_C31_API_OK ||
                c34_result_read(environment->c34, command, result) !=
                    C34_OK) {
                return 0;
            }
            return 1;
        }
        (void)step;
    }
    return 0;
}

int c34_test_consume(
    struct c34_test_environment *environment,
    const struct fwlab_c31_command_handle *command,
    const struct fwlab_c31_completion_lease *lease
)
{
    return fwlab_c31_completion_consume(environment->c31, lease) ==
               FWLAB_C31_API_OK &&
           c34_result_ack(environment->c34, command) == C34_OK;
}

int c34_test_pump_quiescent(
    struct c34_test_environment *environment,
    unsigned int limit
)
{
    unsigned int iteration;

    for (iteration = 0; iteration < limit; ++iteration) {
        struct fwlab_c31_step_result step;
        bool quiescent;

        if (c34_maintenance_quiescent(environment->c34, &quiescent) !=
            C34_OK) {
            return 0;
        }
        if (quiescent) {
            return 1;
        }
        if (fwlab_c31_step(environment->c31, 1, &step) !=
            FWLAB_C31_API_OK) {
            return 0;
        }
        (void)step;
    }
    return 0;
}

static struct c34_request request_base(uint8_t kind)
{
    struct c34_request request;

    memset(&request, 0, sizeof(request));
    request.version = C34_CONTRACT_VERSION;
    request.size = sizeof(request);
    request.kind = kind;
    request.owner_epoch = 1;
    request.scope = 9;
    return request;
}

struct c34_request c34_test_write(
    uint8_t atom_mask,
    uint8_t durability,
    uint32_t sequence,
    uint8_t fill0,
    uint8_t fill1
)
{
    struct c34_request request = request_base(C34_REQUEST_WRITE);

    request.durability_kind = durability;
    request.atom_mask = atom_mask;
    request.sequence = sequence;
    if ((atom_mask & 1u) != 0) {
        memset(request.payload[0], fill0, C34_ATOM_BYTES);
    }
    if ((atom_mask & 2u) != 0) {
        memset(request.payload[1], fill1, C34_ATOM_BYTES);
    }
    return request;
}

struct c34_request c34_test_read(uint8_t atom)
{
    struct c34_request request = request_base(C34_REQUEST_READ);

    request.durability_kind = FWLAB_PERSIST_DEFAULT;
    request.atom = atom;
    return request;
}

struct c34_request c34_test_trim(
    uint8_t atom_mask,
    uint8_t durability,
    uint32_t sequence
)
{
    struct c34_request request = request_base(C34_REQUEST_TRIM);

    request.durability_kind = durability;
    request.atom_mask = atom_mask;
    request.sequence = sequence;
    return request;
}

struct c34_request c34_test_fence(uint32_t frontier)
{
    struct c34_request request = request_base(C34_REQUEST_FENCE);

    request.durability_kind = FWLAB_PERSIST_FENCE;
    request.frontier = frontier;
    return request;
}
