/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "nfc_internal.h"

#include <stdalign.h>
#include <string.h>

int c33_checked_add_size(size_t left, size_t right, size_t *result)
{
    if (result == NULL || left > SIZE_MAX - right) {
        return 0;
    }
    *result = left + right;
    return 1;
}

int c33_checked_mul_size(size_t left, size_t right, size_t *result)
{
    if (result == NULL || (left != 0 && right > SIZE_MAX / left)) {
        return 0;
    }
    *result = left * right;
    return 1;
}

static size_t align_up(size_t value, size_t alignment)
{
    size_t mask = alignment - 1u;

    if ((alignment & mask) != 0 || value > SIZE_MAX - mask) {
        return 0;
    }
    return (value + mask) & ~mask;
}

static int all_zero_u32(const uint32_t *values, size_t count)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        if (values[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int duration_valid(uint32_t value)
{
    return value != 0 && value <= FWLAB_NFC_MODEL_MAX_DURATION_TICKS;
}

static int geometry_validate(const struct fwlab_nfc_geometry *geometry)
{
    size_t value;
    size_t pages;
    size_t media_bytes;

    if (geometry->version != FWLAB_NFC_CONTRACT_VERSION ||
        geometry->size != sizeof(*geometry) || geometry->channels == 0 ||
        geometry->channels > FWLAB_NFC_MODEL_MAX_CHANNELS ||
        geometry->luns_per_channel == 0 ||
        geometry->luns_per_channel > FWLAB_NFC_MODEL_MAX_LUNS_PER_CHANNEL ||
        geometry->planes_per_lun == 0 ||
        geometry->planes_per_lun > FWLAB_NFC_MODEL_MAX_PLANES_PER_LUN ||
        geometry->blocks_per_plane == 0 ||
        geometry->blocks_per_plane > FWLAB_NFC_MODEL_MAX_BLOCKS_PER_PLANE ||
        geometry->pages_per_block == 0 ||
        geometry->pages_per_block > FWLAB_NFC_MODEL_MAX_PAGES_PER_BLOCK ||
        geometry->plane_parallelism_per_lun == 0 ||
        geometry->plane_parallelism_per_lun > geometry->planes_per_lun ||
        geometry->main_bytes_per_page == 0 ||
        geometry->main_bytes_per_page > FWLAB_NFC_MODEL_MAX_MAIN_BYTES ||
        geometry->oob_bytes_per_page == 0 ||
        geometry->oob_bytes_per_page > FWLAB_NFC_MODEL_MAX_OOB_BYTES ||
        geometry->max_programs_per_erase != 1 ||
        geometry->program_order > FWLAB_NFC_PROGRAM_ASCENDING ||
        geometry->reserved0 != 0 ||
        !all_zero_u32(geometry->reserved1, 2) ||
        !c33_checked_mul_size(geometry->channels,
                              geometry->luns_per_channel, &value) ||
        !c33_checked_mul_size(value, geometry->planes_per_lun, &value) ||
        !c33_checked_mul_size(value, geometry->blocks_per_plane, &value) ||
        !c33_checked_mul_size(value, geometry->pages_per_block, &pages) ||
        pages > FWLAB_NFC_MODEL_MAX_TOTAL_PAGES ||
        !c33_checked_add_size(geometry->main_bytes_per_page,
                              geometry->oob_bytes_per_page, &value) ||
        !c33_checked_mul_size(pages, value, &media_bytes) ||
        media_bytes > FWLAB_NFC_MODEL_MAX_MEDIA_BYTES) {
        return 0;
    }
    return 1;
}

static int ecc_validate(
    const struct fwlab_nfc_ecc_profile *ecc,
    const struct fwlab_nfc_geometry *geometry
)
{
    return ecc->version == FWLAB_NFC_CONTRACT_VERSION &&
           ecc->size == sizeof(*ecc) &&
           ecc->main_covered_bytes == geometry->main_bytes_per_page &&
           ecc->oob_covered_bytes == geometry->oob_bytes_per_page &&
           ecc->main_step_bytes != 0 && ecc->oob_step_bytes != 0 &&
           ecc->main_covered_bytes % ecc->main_step_bytes == 0 &&
           ecc->oob_covered_bytes % ecc->oob_step_bytes == 0 &&
           ecc->main_strength_bits <= UINT8_MAX &&
           ecc->oob_strength_bits <= UINT8_MAX &&
           ecc->max_retry_step <= FWLAB_NFC_MODEL_MAX_RETRY_STEP &&
           ecc->reserved0[0] == 0 && ecc->reserved0[1] == 0 &&
           ecc->reserved0[2] == 0 && all_zero_u32(ecc->reserved1, 2);
}

static int timing_validate(const struct fwlab_nfc_timing_profile *timing)
{
    return timing->version == FWLAB_NFC_CONTRACT_VERSION &&
           timing->size == sizeof(*timing) &&
           duration_valid(timing->command_ticks) &&
           duration_valid(timing->transfer_ticks_per_unit) &&
           duration_valid(timing->read_array_ticks) &&
           duration_valid(timing->program_setup_ticks) &&
           duration_valid(timing->program_ticks_per_unit) &&
           duration_valid(timing->program_status_ticks) &&
           duration_valid(timing->erase_setup_ticks) &&
           duration_valid(timing->erase_ticks_per_page) &&
           duration_valid(timing->erase_status_ticks) &&
           duration_valid(timing->status_ticks) &&
           all_zero_u32(timing->reserved, 2);
}

static int fault_validate(const struct fwlab_nfc_fault_profile *fault)
{
    return fault->version == FWLAB_NFC_FAULT_PROFILE_VERSION &&
           fault->size == sizeof(*fault) && fault->profile_version != 0 &&
           all_zero_u32(fault->reserved, 2);
}

static int capacity_validate(
    const struct fwlab_nfc_capacity *capacity,
    const struct fwlab_nfc_geometry *geometry
)
{
    return capacity->version == FWLAB_NFC_CONTRACT_VERSION &&
           capacity->size == sizeof(*capacity) && capacity->operations != 0 &&
           capacity->operations <= FWLAB_NFC_MODEL_MAX_OPERATIONS &&
           capacity->request_registry >= capacity->operations &&
           capacity->request_registry <= FWLAB_NFC_MODEL_MAX_OPERATIONS &&
           capacity->terminal_events >= capacity->operations &&
           capacity->terminal_events <= FWLAB_NFC_MODEL_MAX_OPERATIONS &&
           capacity->result_slots >= capacity->operations &&
           capacity->result_slots <= FWLAB_NFC_MODEL_MAX_OPERATIONS &&
           capacity->trace_entries >=
               (uint32_t)capacity->operations * 16u + 1u &&
           capacity->reserved0 == 0 &&
           capacity->scratch_main_bytes >= geometry->main_bytes_per_page &&
           capacity->scratch_oob_bytes >= geometry->oob_bytes_per_page &&
           capacity->operation_generation_limit != 0 &&
           capacity->cache_generation_limit != 0 &&
           capacity->controller_epoch_limit != 0 &&
           capacity->submit_sequence_limit != 0 &&
           capacity->operation_uid_limit != 0 &&
           capacity->virtual_tick_limit != 0 &&
           all_zero_u32(capacity->reserved1, 2);
}

enum fwlab_nfc_api_result fwlab_nfc_model_config_validate(
    const struct fwlab_nfc_model_config *config
)
{
    if (config == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    if (config->version != FWLAB_NFC_CONTRACT_VERSION) {
        return FWLAB_NFC_API_UNSUPPORTED_VERSION;
    }
    if (config->size != sizeof(*config) || config->reserved0 != 0 ||
        config->successful_erase_limit == 0 || config->reserved1 != 0 ||
        !all_zero_u32(config->reserved2, 2) ||
        !geometry_validate(&config->geometry) ||
        !ecc_validate(&config->ecc, &config->geometry) ||
        !timing_validate(&config->timing) ||
        !fault_validate(&config->fault) ||
        !capacity_validate(&config->capacity, &config->geometry)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    return FWLAB_NFC_API_OK;
}

size_t fwlab_nfc_model_arena_alignment(void)
{
    return alignof(max_align_t);
}

static int arena_add(
    size_t *offset,
    size_t count,
    size_t element_size,
    size_t alignment
)
{
    size_t bytes;
    size_t aligned = align_up(*offset, alignment);

    if (aligned == 0 || !c33_checked_mul_size(count, element_size, &bytes) ||
        !c33_checked_add_size(aligned, bytes, offset)) {
        return 0;
    }
    return 1;
}

static int model_counts(
    const struct fwlab_nfc_model_config *config,
    size_t *planes,
    size_t *array_lanes
)
{
    size_t luns;

    return c33_checked_mul_size(config->geometry.channels,
                                config->geometry.luns_per_channel, &luns) &&
           c33_checked_mul_size(luns, config->geometry.planes_per_lun,
                                planes) &&
           c33_checked_mul_size(
               luns, config->geometry.plane_parallelism_per_lun,
               array_lanes);
}

size_t fwlab_nfc_model_arena_size(
    const struct fwlab_nfc_model_config *config
)
{
    size_t offset;
    size_t planes;
    size_t array_lanes;
    size_t operations;

    if (fwlab_nfc_model_config_validate(config) != FWLAB_NFC_API_OK ||
        !model_counts(config, &planes, &array_lanes)) {
        return 0;
    }
    operations = config->capacity.operations;
    offset = align_up(sizeof(struct fwlab_nfc_model), alignof(max_align_t));
    if (offset == 0 ||
        !arena_add(&offset, operations, sizeof(struct c33_operation),
                   alignof(struct c33_operation)) ||
        !arena_add(&offset, planes, sizeof(struct c33_plane_cache),
                   alignof(struct c33_plane_cache)) ||
        !arena_add(&offset, config->capacity.trace_entries,
                   sizeof(struct fwlab_nfc_trace_entry),
                   alignof(struct fwlab_nfc_trace_entry)) ||
        !arena_add(&offset, config->geometry.channels, sizeof(uint64_t),
                   alignof(uint64_t)) ||
        !arena_add(&offset, array_lanes, sizeof(uint64_t),
                   alignof(uint64_t)) ||
        !arena_add(&offset, planes, sizeof(uint64_t), alignof(uint64_t)) ||
        !arena_add(&offset, operations, config->capacity.scratch_main_bytes,
                   1) ||
        !arena_add(&offset, operations, config->capacity.scratch_oob_bytes,
                   1) ||
        !arena_add(&offset, planes, config->geometry.main_bytes_per_page, 1) ||
        !arena_add(&offset, planes, config->geometry.oob_bytes_per_page, 1)) {
        return 0;
    }
    return align_up(offset, alignof(max_align_t));
}

static int buffer_provider_valid(
    const struct fwlab_nfc_buffer_provider *provider
)
{
    return provider != NULL && provider->ops != NULL &&
           provider->ops->version == FWLAB_NFC_CONTRACT_VERSION &&
           provider->ops->size == sizeof(*provider->ops) &&
           provider->ops->reserved == 0 && provider->ops->read != NULL &&
           provider->ops->write != NULL && provider->context != NULL;
}

static int media_provider_valid(const struct fwlab_nand_media *media)
{
    return media != NULL && media->ops != NULL &&
           media->ops->version == FWLAB_NFC_CONTRACT_VERSION &&
           media->ops->size == sizeof(*media->ops) &&
           media->ops->reserved == 0 && media->ops->read_page != NULL &&
           media->ops->program != NULL && media->ops->erase != NULL &&
           media->ops->mark_runtime_bad != NULL && media->ops->hash != NULL &&
           media->context != NULL;
}

static void *take_region(
    uint8_t *arena,
    size_t arena_size,
    size_t *offset,
    size_t count,
    size_t element_size,
    size_t alignment
)
{
    size_t aligned = align_up(*offset, alignment);
    size_t bytes;

    if (aligned == 0 ||
        !c33_checked_mul_size(count, element_size, &bytes) ||
        aligned > arena_size || bytes > arena_size - aligned) {
        return NULL;
    }
    *offset = aligned + bytes;
    return &arena[aligned];
}

enum fwlab_nfc_api_result fwlab_nfc_model_init(
    void *arena,
    size_t arena_size,
    const struct fwlab_nfc_model_config *config,
    uint64_t instance_nonce,
    const struct fwlab_nfc_buffer_provider *buffers,
    const struct fwlab_nand_media *media,
    struct fwlab_nfc_model **model_out
)
{
    size_t required = fwlab_nfc_model_arena_size(config);
    size_t planes;
    size_t array_lanes;
    size_t offset;
    size_t index;
    struct fwlab_nfc_model *model;
    uint8_t *bytes = arena;

    if (arena == NULL || config == NULL || model_out == NULL ||
        instance_nonce == 0 || required == 0 || arena_size < required ||
        ((uintptr_t)arena % alignof(max_align_t)) != 0 ||
        !buffer_provider_valid(buffers) || !media_provider_valid(media) ||
        !model_counts(config, &planes, &array_lanes)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(arena, 0, required);
    model = arena;
    model->magic = C33_MODEL_MAGIC;
    model->config = *config;
    model->instance_nonce = instance_nonce;
    model->current_epoch = 1;
    model->next_submit_sequence = 1;
    model->next_array_sequence = 1;
    model->next_operation_uid = 1;
    model->next_trace_sequence = 1;
    model->phase = FWLAB_NFC_MODEL_READY;
    model->buffers = *buffers;
    model->media = *media;
    offset = align_up(sizeof(*model), alignof(max_align_t));
    model->operation = take_region(
        bytes, required, &offset, config->capacity.operations,
        sizeof(*model->operation), alignof(struct c33_operation));
    model->cache = take_region(
        bytes, required, &offset, planes, sizeof(*model->cache),
        alignof(struct c33_plane_cache));
    model->trace = take_region(
        bytes, required, &offset, config->capacity.trace_entries,
        sizeof(*model->trace), alignof(struct fwlab_nfc_trace_entry));
    model->channel_tail = take_region(
        bytes, required, &offset, config->geometry.channels, sizeof(uint64_t),
        alignof(uint64_t));
    model->array_tail = take_region(
        bytes, required, &offset, array_lanes, sizeof(uint64_t),
        alignof(uint64_t));
    model->cache_tail = take_region(
        bytes, required, &offset, planes, sizeof(uint64_t), alignof(uint64_t));
    model->operation_main = take_region(
        bytes, required, &offset, config->capacity.operations,
        config->capacity.scratch_main_bytes, 1);
    model->operation_oob = take_region(
        bytes, required, &offset, config->capacity.operations,
        config->capacity.scratch_oob_bytes, 1);
    model->cache_main = take_region(
        bytes, required, &offset, planes,
        config->geometry.main_bytes_per_page, 1);
    model->cache_oob = take_region(
        bytes, required, &offset, planes,
        config->geometry.oob_bytes_per_page, 1);
    if (model->operation == NULL || model->cache == NULL ||
        model->trace == NULL ||
        model->channel_tail == NULL || model->array_tail == NULL ||
        model->cache_tail == NULL || model->operation_main == NULL ||
        model->operation_oob == NULL || model->cache_main == NULL ||
        model->cache_oob == NULL) {
        memset(arena, 0, required);
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    for (index = 0; index < config->capacity.operations; ++index) {
        model->operation[index].main =
            &model->operation_main[index *
                                   config->capacity.scratch_main_bytes];
        model->operation[index].oob =
            &model->operation_oob[index *
                                  config->capacity.scratch_oob_bytes];
    }
    for (index = 0; index < planes; ++index) {
        model->cache[index].main =
            &model->cache_main[index * config->geometry.main_bytes_per_page];
        model->cache[index].oob =
            &model->cache_oob[index * config->geometry.oob_bytes_per_page];
    }
    c33_trace(model, FWLAB_NFC_TRACE_INIT, NULL, 0, 0, 0);
    *model_out = model;
    return FWLAB_NFC_API_OK;
}

int c33_model_valid(const struct fwlab_nfc_model *model)
{
    return model != NULL && model->magic == C33_MODEL_MAGIC &&
           model->phase <= FWLAB_NFC_MODEL_FAULTED;
}

int c33_operation_equal(
    const struct fwlab_nfc_operation_token *left,
    const struct fwlab_nfc_operation_token *right
)
{
    return left->instance_nonce == right->instance_nonce &&
           left->operation_uid == right->operation_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->generation == right->generation;
}

int c33_ppa_valid(
    const struct fwlab_nfc_geometry *geometry,
    const struct fwlab_nfc_ppa *ppa
)
{
    return ppa != NULL && ppa->reserved == 0 &&
           ppa->channel < geometry->channels &&
           ppa->lun < geometry->luns_per_channel &&
           ppa->plane < geometry->planes_per_lun &&
           ppa->block < geometry->blocks_per_plane &&
           ppa->page < geometry->pages_per_block;
}

uint32_t c33_plane_index(
    const struct fwlab_nfc_geometry *geometry,
    const struct fwlab_nfc_ppa *ppa
)
{
    uint32_t index = ppa->channel;

    index = index * geometry->luns_per_channel + ppa->lun;
    return index * geometry->planes_per_lun + ppa->plane;
}

uint32_t c33_lun_index(
    const struct fwlab_nfc_geometry *geometry,
    const struct fwlab_nfc_ppa *ppa
)
{
    return ppa->channel * geometry->luns_per_channel + ppa->lun;
}

static int buffer_zero(const struct fwlab_nfc_buffer_ref *buffer)
{
    return buffer->controller_region == 0 && buffer->offset == 0 &&
           buffer->length == 0 && buffer->reserved == 0;
}

static int cache_zero(const struct fwlab_nfc_cache_token *cache)
{
    return cache->instance_nonce == 0 && cache->controller_epoch == 0 &&
           cache->generation == 0 && cache->channel == 0 &&
           cache->lun == 0 && cache->plane == 0 && cache->reserved == 0;
}

static int selected_buffer_valid(
    const struct fwlab_nfc_buffer_ref *buffer,
    uint32_t expected,
    int selected
)
{
    if (!selected) {
        return buffer_zero(buffer);
    }
    return buffer->controller_region != 0 && buffer->length == expected &&
           buffer->reserved == 0 &&
           (uint64_t)buffer->offset + buffer->length <= UINT32_MAX;
}

int c33_request_shape_valid(
    const struct fwlab_nfc_model *model,
    const struct fwlab_nfc_request *request,
    uint8_t *reason
)
{
    const struct fwlab_nfc_geometry *geometry = &model->config.geometry;
    int main_selected;
    int oob_selected;

    *reason = FWLAB_NFC_REASON_RANGE;
    if (request == NULL || request->version != FWLAB_NFC_CONTRACT_VERSION ||
        request->size != sizeof(*request) || request->reserved0 != 0 ||
        request->reserved1[0] != 0 || request->reserved1[1] != 0 ||
        request->reserved1[2] != 0 || request->reserved2[0] != 0 ||
        request->reserved2[1] != 0 || request->kind > FWLAB_NFC_STATUS ||
        (request->region_mask & ~FWLAB_NFC_REGION_MASK) != 0 ||
        request->operation.instance_nonce != model->instance_nonce ||
        request->operation.controller_epoch != model->current_epoch ||
        request->operation.operation_uid == 0 ||
        request->operation.operation_uid >
            model->config.capacity.operation_uid_limit ||
        request->operation.generation == 0 ||
        request->operation.generation >
            model->config.capacity.operation_generation_limit ||
        request->retry_step > model->config.ecc.max_retry_step ||
        !c33_ppa_valid(geometry, &request->ppa)) {
        return 0;
    }
    main_selected = (request->region_mask & FWLAB_NFC_REGION_MAIN) != 0;
    oob_selected = (request->region_mask & FWLAB_NFC_REGION_OOB) != 0;
    if (request->kind == FWLAB_NFC_READ_TRIGGER) {
        return request->region_mask != 0 && buffer_zero(&request->main) &&
               buffer_zero(&request->oob) && cache_zero(&request->cache);
    }
    if (request->kind == FWLAB_NFC_READ_TRANSFER) {
        return request->region_mask != 0 &&
               selected_buffer_valid(&request->main,
                   geometry->main_bytes_per_page, main_selected) &&
               selected_buffer_valid(&request->oob,
                   geometry->oob_bytes_per_page, oob_selected) &&
               request->cache.instance_nonce == model->instance_nonce &&
               request->cache.controller_epoch == model->current_epoch &&
               request->cache.generation != 0 &&
               request->cache.generation <=
                   model->config.capacity.cache_generation_limit &&
               request->cache.reserved == 0;
    }
    if (request->kind == FWLAB_NFC_PROGRAM_TRANSFER) {
        return main_selected &&
               selected_buffer_valid(&request->main,
                   geometry->main_bytes_per_page, 1) &&
               selected_buffer_valid(&request->oob,
                   geometry->oob_bytes_per_page, oob_selected) &&
               cache_zero(&request->cache) && request->retry_step == 0;
    }
    if (request->kind == FWLAB_NFC_PROGRAM_EXECUTE) {
        return main_selected && buffer_zero(&request->main) &&
               buffer_zero(&request->oob) &&
               request->cache.instance_nonce == model->instance_nonce &&
               request->cache.controller_epoch == model->current_epoch &&
               request->cache.generation != 0 &&
               request->cache.generation <=
                   model->config.capacity.cache_generation_limit &&
               request->cache.reserved == 0 && request->retry_step == 0;
    }
    return request->region_mask == 0 && buffer_zero(&request->main) &&
           buffer_zero(&request->oob) && cache_zero(&request->cache) &&
           request->retry_step == 0 &&
           (request->kind != FWLAB_NFC_ERASE || request->ppa.page == 0);
}

void c33_trace(
    struct fwlab_nfc_model *model,
    uint32_t kind,
    const struct c33_operation *operation,
    uint32_t from_state,
    uint32_t to_state,
    uint32_t detail
)
{
    struct fwlab_nfc_trace_entry *entry;

    if (!c33_model_valid(model) ||
        model->trace_count >= model->config.capacity.trace_entries ||
        model->next_trace_sequence == 0) {
        if (model != NULL) {
            model->phase = FWLAB_NFC_MODEL_FAULTED;
        }
        return;
    }
    entry = &model->trace[model->trace_count++];
    memset(entry, 0, sizeof(*entry));
    entry->version = FWLAB_NFC_CONTRACT_VERSION;
    entry->size = (uint16_t)sizeof(*entry);
    entry->kind = kind;
    entry->sequence = model->next_trace_sequence++;
    entry->virtual_tick = model->virtual_now;
    if (operation != NULL) {
        entry->operation = operation->request.operation;
        entry->ppa = operation->request.ppa;
    }
    entry->from_state = from_state;
    entry->to_state = to_state;
    entry->detail = detail;
}

static struct c33_operation *find_operation(
    struct fwlab_nfc_model *model,
    const struct fwlab_nfc_operation_token *token
)
{
    uint32_t index;

    for (index = 0; index < model->config.capacity.operations; ++index) {
        if (model->operation[index].state != C33_OP_FREE &&
            c33_operation_equal(&model->operation[index].request.operation,
                                token)) {
            return &model->operation[index];
        }
    }
    return NULL;
}

static struct c33_operation *free_operation(struct fwlab_nfc_model *model)
{
    uint32_t index;

    for (index = 0; index < model->config.capacity.operations; ++index) {
        if (model->operation[index].state == C33_OP_FREE) {
            return &model->operation[index];
        }
    }
    return NULL;
}

static struct fwlab_nfc_submit_result reject_submit(uint32_t reason)
{
    struct fwlab_nfc_submit_result result;

    result.disposition = FWLAB_NFC_REJECTED;
    result.reason = reason;
    return result;
}

static struct fwlab_nfc_submit_result model_try_submit(
    void *opaque,
    const struct fwlab_nfc_request *request
)
{
    struct fwlab_nfc_model *model = opaque;
    struct c33_operation *operation;
    struct fwlab_nfc_submit_result result;
    uint8_t reason;

    if (!c33_model_valid(model) || request == NULL) {
        return reject_submit(FWLAB_NFC_REASON_INTERNAL);
    }
    if (model->phase != FWLAB_NFC_MODEL_READY) {
        result.disposition = FWLAB_NFC_BACKPRESSURE;
        result.reason = FWLAB_NFC_REASON_RESET;
        return result;
    }
    if (!c33_request_shape_valid(model, request, &reason)) {
        return reject_submit(reason);
    }
    if (find_operation(model, &request->operation) != NULL) {
        return reject_submit(FWLAB_NFC_REASON_STALE);
    }
    if (model->next_operation_uid == 0 ||
        request->operation.operation_uid < model->next_operation_uid) {
        return reject_submit(FWLAB_NFC_REASON_STALE);
    }
    operation = free_operation(model);
    if (operation == NULL) {
        result.disposition = FWLAB_NFC_BACKPRESSURE;
        result.reason = FWLAB_NFC_REASON_NONE;
        return result;
    }
    if (model->next_submit_sequence == 0 ||
        model->next_submit_sequence >
            model->config.capacity.submit_sequence_limit) {
        return reject_submit(FWLAB_NFC_REASON_INTERNAL);
    }
    memset(operation, 0, sizeof(*operation));
    operation->main = &model->operation_main[
        (size_t)(operation - model->operation) *
        model->config.capacity.scratch_main_bytes];
    operation->oob = &model->operation_oob[
        (size_t)(operation - model->operation) *
        model->config.capacity.scratch_oob_bytes];
    operation->request = *request;
    operation->submit_sequence = model->next_submit_sequence++;
    operation->state = C33_OP_QUEUED;
    if (request->kind == FWLAB_NFC_PROGRAM_TRANSFER) {
        if (model->buffers.ops->read(model->buffers.context, &request->main,
                                     operation->main,
                                     request->main.length) !=
                FWLAB_NFC_API_OK) {
            memset(operation, 0, sizeof(*operation));
            return reject_submit(FWLAB_NFC_REASON_RANGE);
        }
        if ((request->region_mask & FWLAB_NFC_REGION_OOB) != 0) {
            if (model->buffers.ops->read(
                    model->buffers.context, &request->oob, operation->oob,
                    request->oob.length) != FWLAB_NFC_API_OK) {
                memset(operation, 0, sizeof(*operation));
                return reject_submit(FWLAB_NFC_REASON_RANGE);
            }
        } else {
            memset(operation->oob, 0xff,
                   model->config.geometry.oob_bytes_per_page);
        }
    }
    memset(&operation->completion, 0, sizeof(operation->completion));
    operation->completion.version = FWLAB_NFC_CONTRACT_VERSION;
    operation->completion.size = (uint16_t)sizeof(operation->completion);
    operation->completion.operation = request->operation;
    operation->completion.ppa = request->ppa;
    operation->completion.cache = request->cache;
    operation->completion.cookie = request->cookie;
    operation->completion.accepted_tick = model->virtual_now;
    operation->completion.submit_sequence = operation->submit_sequence;
    operation->completion.requested_region_mask = request->region_mask;
    operation->completion.retry_step = request->retry_step;
    operation->completion.operation_kind = request->kind;
    c33_trace(model, FWLAB_NFC_TRACE_ACCEPT, operation, C33_OP_FREE,
              C33_OP_QUEUED, 0);
    model->next_operation_uid =
        request->operation.operation_uid == UINT64_MAX ? 0 :
        request->operation.operation_uid + 1u;
    result.disposition = FWLAB_NFC_ACCEPTED;
    result.reason = FWLAB_NFC_REASON_NONE;
    return result;
}

static enum fwlab_nfc_api_result model_cancel(
    void *opaque,
    const struct fwlab_nfc_operation_token *operation_token
)
{
    struct fwlab_nfc_model *model = opaque;
    struct c33_operation *operation;

    if (!c33_model_valid(model) || operation_token == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    operation = find_operation(model, operation_token);
    if (operation == NULL) {
        return FWLAB_NFC_API_NOT_FOUND;
    }
    operation->cancel_requested = 1;
    c33_trace(model, FWLAB_NFC_TRACE_CANCEL, operation, operation->state,
              operation->state, 0);
    return FWLAB_NFC_API_OK;
}

static int operation_less(
    const struct c33_operation *left,
    uint32_t left_slot,
    const struct c33_operation *right,
    uint32_t right_slot
)
{
    if (left->state == C33_OP_QUEUED || right->state == C33_OP_QUEUED) {
        if (left->state != right->state) {
            return left->state == C33_OP_QUEUED;
        }
        if (left->request.priority != right->request.priority) {
            return left->request.priority < right->request.priority;
        }
        if (left->submit_sequence != right->submit_sequence) {
            return left->submit_sequence < right->submit_sequence;
        }
    }
    if (left->next_tick != right->next_tick) {
        return left->next_tick < right->next_tick;
    }
    if (left->submit_sequence != right->submit_sequence) {
        return left->submit_sequence < right->submit_sequence;
    }
    if (left->request.operation.operation_uid !=
        right->request.operation.operation_uid) {
        return left->request.operation.operation_uid <
               right->request.operation.operation_uid;
    }
    return left_slot < right_slot;
}

static struct c33_operation *next_operation(
    struct fwlab_nfc_model *model
)
{
    struct c33_operation *selected = NULL;
    uint32_t selected_slot = 0;
    uint32_t index;

    for (index = 0; index < model->config.capacity.operations; ++index) {
        struct c33_operation *operation = &model->operation[index];

        if (operation->state != C33_OP_QUEUED &&
            operation->state != C33_OP_SCHEDULED &&
            operation->state != C33_OP_RUNNING) {
            continue;
        }
        if (selected == NULL ||
            operation_less(operation, index, selected, selected_slot)) {
            selected = operation;
            selected_slot = index;
        }
    }
    return selected;
}

static uint64_t unit_deadline(const struct c33_operation *operation)
{
    uint64_t duration = operation->finish_tick - operation->start_tick;
    uint64_t numerator = duration * (operation->unit_index + 1u);
    uint64_t deadline = operation->start_tick +
        (numerator + operation->unit_count - 1u) / operation->unit_count;

    return deadline > operation->finish_tick ? operation->finish_tick :
                                                deadline;
}

static enum fwlab_nfc_api_result model_step(
    void *opaque,
    uint32_t budget,
    struct fwlab_nfc_step_result *result
)
{
    struct fwlab_nfc_model *model = opaque;
    uint32_t used = 0;

    if (!c33_model_valid(model) || result == NULL || budget == 0 ||
        model->phase == FWLAB_NFC_MODEL_POWERED_OFF ||
        model->phase == FWLAB_NFC_MODEL_FAULTED) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(result, 0, sizeof(*result));
    while (used < budget) {
        struct c33_operation *operation = next_operation(model);

        if (operation == NULL) {
            break;
        }
        if (operation->next_tick > model->virtual_now) {
            model->virtual_now = operation->next_tick;
            ++used;
            ++result->transitions;
            continue;
        }
        if (operation->state == C33_OP_QUEUED) {
            if (!c33_schedule_operation(model, operation)) {
                model->phase = FWLAB_NFC_MODEL_FAULTED;
                return FWLAB_NFC_API_COUNTER_EXHAUSTED;
            }
            operation->state = C33_OP_SCHEDULED;
            c33_trace(model, FWLAB_NFC_TRACE_START, operation,
                      C33_OP_QUEUED, C33_OP_SCHEDULED, 0);
            ++used;
            ++result->transitions;
            continue;
        }
        if (operation->state == C33_OP_QUEUED ||
            operation->state == C33_OP_SCHEDULED) {
            if (!c33_begin_operation(model, operation)) {
                model->phase = FWLAB_NFC_MODEL_FAULTED;
                return FWLAB_NFC_API_INVARIANT_FAILURE;
            }
            ++used;
            ++result->transitions;
            continue;
        }
        if (operation->unit_index < operation->unit_count) {
            ++operation->unit_index;
            c33_trace(model, FWLAB_NFC_TRACE_EFFECT, operation,
                      C33_OP_RUNNING, C33_OP_RUNNING,
                      operation->unit_index);
            if (operation->unit_index == operation->unit_count) {
                model->virtual_now = operation->finish_tick;
                if (!c33_finish_operation(model, operation)) {
                    model->phase = FWLAB_NFC_MODEL_FAULTED;
                    return FWLAB_NFC_API_INVARIANT_FAILURE;
                }
            } else {
                operation->next_tick = unit_deadline(operation);
            }
            ++used;
            ++result->transitions;
        }
    }
    result->units_used = used;
    result->events_pending = model->event_count;
    result->phase = model->phase;
    result->virtual_now = model->virtual_now;
    return FWLAB_NFC_API_OK;
}

static int event_less(
    const struct c33_operation *left,
    uint32_t left_slot,
    const struct c33_operation *right,
    uint32_t right_slot
)
{
    if (left->completion.status_tick != right->completion.status_tick) {
        return left->completion.status_tick < right->completion.status_tick;
    }
    if (left->submit_sequence != right->submit_sequence) {
        return left->submit_sequence < right->submit_sequence;
    }
    if (left->request.operation.generation !=
        right->request.operation.generation) {
        return left->request.operation.generation <
               right->request.operation.generation;
    }
    return left_slot < right_slot;
}

static struct c33_operation *next_event(
    struct fwlab_nfc_model *model,
    uint32_t *slot_out
)
{
    struct c33_operation *selected = NULL;
    uint32_t selected_slot = 0;
    uint32_t index;

    for (index = 0; index < model->config.capacity.operations; ++index) {
        struct c33_operation *operation = &model->operation[index];

        if (operation->state != C33_OP_EVENT_PENDING) {
            continue;
        }
        if (selected == NULL ||
            event_less(operation, index, selected, selected_slot)) {
            selected = operation;
            selected_slot = index;
        }
    }
    if (selected != NULL) {
        *slot_out = selected_slot;
    }
    return selected;
}

static enum fwlab_nfc_api_result model_poll(
    void *opaque,
    uint32_t budget,
    struct fwlab_nfc_completion *events,
    uint32_t event_capacity,
    uint32_t *event_count
)
{
    struct fwlab_nfc_model *model = opaque;
    uint32_t emitted = 0;

    if (!c33_model_valid(model) || events == NULL || event_count == NULL ||
        budget == 0 || event_capacity == 0) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    while (emitted < budget && emitted < event_capacity) {
        uint32_t slot;
        struct c33_operation *operation = next_event(model, &slot);
        uint8_t *main;
        uint8_t *oob;

        if (operation == NULL) {
            break;
        }
        events[emitted++] = operation->completion;
        c33_trace(model, FWLAB_NFC_TRACE_EVENT, operation,
                  C33_OP_EVENT_PENDING, C33_OP_FREE,
                  operation->completion.reason);
        main = operation->main;
        oob = operation->oob;
        memset(operation, 0, sizeof(*operation));
        operation->main = main;
        operation->oob = oob;
        (void)slot;
        --model->event_count;
    }
    *event_count = emitted;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result model_reset_begin(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch
)
{
    struct fwlab_nfc_model *model = opaque;
    size_t planes;
    size_t array_lanes;
    uint32_t index;

    if (!c33_model_valid(model) || instance_nonce != model->instance_nonce ||
        old_epoch != model->current_epoch ||
        model->phase != FWLAB_NFC_MODEL_READY ||
        old_epoch >= model->config.capacity.controller_epoch_limit ||
        !model_counts(&model->config, &planes, &array_lanes)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    model->phase = FWLAB_NFC_MODEL_RESET_DRAIN;
    ++model->current_epoch;
    for (index = 0; index < planes; ++index) {
        if (model->cache[index].generation >=
            model->config.capacity.cache_generation_limit) {
            model->phase = FWLAB_NFC_MODEL_FAULTED;
            return FWLAB_NFC_API_COUNTER_EXHAUSTED;
        }
        ++model->cache[index].generation;
        model->cache[index].kind = FWLAB_NFC_CACHE_NONE;
        model->cache[index].valid_region_mask = 0;
        model->cache[index].controller_epoch = model->current_epoch;
    }
    for (index = 0; index < model->config.capacity.operations; ++index) {
        struct c33_operation *operation = &model->operation[index];

        if (operation->state == C33_OP_QUEUED ||
            operation->state == C33_OP_SCHEDULED) {
            operation->cancel_requested = 1;
            operation->reset_owned = 1;
        } else if (operation->state == C33_OP_RUNNING) {
            operation->reset_owned = 1;
        }
    }
    c33_trace(model, FWLAB_NFC_TRACE_RESET, NULL, FWLAB_NFC_MODEL_READY,
              FWLAB_NFC_MODEL_RESET_DRAIN, old_epoch);
    (void)array_lanes;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result model_quiescent(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch,
    bool *quiescent
)
{
    struct fwlab_nfc_model *model = opaque;
    uint32_t index;

    if (!c33_model_valid(model) || quiescent == NULL ||
        instance_nonce != model->instance_nonce ||
        old_epoch + 1u != model->current_epoch ||
        model->phase != FWLAB_NFC_MODEL_RESET_DRAIN) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *quiescent = true;
    for (index = 0; index < model->config.capacity.operations; ++index) {
        if (model->operation[index].state != C33_OP_FREE &&
            model->operation[index].request.operation.controller_epoch ==
                old_epoch) {
            *quiescent = false;
            break;
        }
    }
    if (*quiescent) {
        model->phase = FWLAB_NFC_MODEL_READY;
    }
    return FWLAB_NFC_API_OK;
}

static const struct fwlab_nfc_provider_ops model_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_nfc_provider_ops),
    .reserved = 0,
    .try_submit = model_try_submit,
    .cancel = model_cancel,
    .step = model_step,
    .poll = model_poll,
    .reset_begin = model_reset_begin,
    .quiescent = model_quiescent,
};

struct fwlab_nfc_provider fwlab_nfc_model_provider(
    struct fwlab_nfc_model *model
)
{
    struct fwlab_nfc_provider provider;

    provider.ops = &model_ops;
    provider.context = c33_model_valid(model) ? model : NULL;
    return provider;
}

enum fwlab_nfc_api_result fwlab_nfc_model_inject_cut(
    struct fwlab_nfc_model *model,
    enum fwlab_nfc_cut_kind cut
)
{
    uint32_t index;
    size_t planes;
    size_t array_lanes;

    if (!c33_model_valid(model) || cut > FWLAB_NFC_CUT_SSD_POWER_LOSS) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    if (cut == FWLAB_NFC_CUT_CONTROLLER_RESET) {
        return model_reset_begin(model, model->instance_nonce,
                                 model->current_epoch);
    }
    for (index = 0; index < model->config.capacity.operations; ++index) {
        struct c33_operation *operation = &model->operation[index];

        if (operation->state == C33_OP_RUNNING &&
            !c33_commit_power_prefix(model, operation)) {
            model->phase = FWLAB_NFC_MODEL_FAULTED;
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
        if (operation->state != C33_OP_FREE) {
            uint8_t *main = operation->main;
            uint8_t *oob = operation->oob;

            memset(operation, 0, sizeof(*operation));
            operation->main = main;
            operation->oob = oob;
        }
    }
    if (!model_counts(&model->config, &planes, &array_lanes)) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    for (index = 0; index < planes; ++index) {
        model->cache[index].kind = FWLAB_NFC_CACHE_NONE;
        model->cache[index].valid_region_mask = 0;
    }
    memset(model->channel_tail, 0,
           model->config.geometry.channels * sizeof(uint64_t));
    memset(model->array_tail, 0, array_lanes * sizeof(uint64_t));
    memset(model->cache_tail, 0, planes * sizeof(uint64_t));
    model->event_count = 0;
    model->phase = FWLAB_NFC_MODEL_POWERED_OFF;
    c33_trace(model, FWLAB_NFC_TRACE_POWER, NULL, FWLAB_NFC_MODEL_READY,
              FWLAB_NFC_MODEL_POWERED_OFF, 0);
    return FWLAB_NFC_API_OK;
}

uint32_t fwlab_nfc_model_trace_count(const struct fwlab_nfc_model *model)
{
    return c33_model_valid(model) ? model->trace_count : 0;
}

enum fwlab_nfc_api_result fwlab_nfc_model_trace_read(
    const struct fwlab_nfc_model *model,
    uint32_t ordinal,
    struct fwlab_nfc_trace_entry *entry
)
{
    if (!c33_model_valid(model) || entry == NULL ||
        ordinal >= model->trace_count) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *entry = model->trace[ordinal];
    return FWLAB_NFC_API_OK;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int byte;

    for (byte = 0; byte < 8; ++byte) {
        hash ^= (uint8_t)(value >> (byte * 8u));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

uint64_t fwlab_nfc_model_state_hash(const struct fwlab_nfc_model *model)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t index;
    size_t planes;
    size_t array_lanes;

    if (!c33_model_valid(model) ||
        !model_counts(&model->config, &planes, &array_lanes)) {
        return 0;
    }
    hash = hash_u64(hash, model->instance_nonce);
    hash = hash_u64(hash, model->config.geometry.channels);
    hash = hash_u64(hash, model->config.geometry.luns_per_channel);
    hash = hash_u64(hash, model->config.geometry.planes_per_lun);
    hash = hash_u64(hash, model->config.geometry.blocks_per_plane);
    hash = hash_u64(hash, model->config.geometry.pages_per_block);
    hash = hash_u64(hash, model->config.geometry.main_bytes_per_page);
    hash = hash_u64(hash, model->config.geometry.oob_bytes_per_page);
    hash = hash_u64(hash,
                    model->config.geometry.plane_parallelism_per_lun);
    hash = hash_u64(hash, model->config.geometry.program_order);
    hash = hash_u64(hash, model->config.ecc.main_strength_bits);
    hash = hash_u64(hash, model->config.ecc.oob_strength_bits);
    hash = hash_u64(hash, model->config.ecc.max_retry_step);
    hash = hash_u64(hash, model->config.fault.profile_version);
    hash = hash_u64(hash, model->config.fault.seed);
    hash = hash_u64(hash, model->config.successful_erase_limit);
    hash = hash_u64(hash, model->virtual_now);
    hash = hash_u64(hash, model->current_epoch);
    hash = hash_u64(hash, model->next_operation_uid);
    hash = hash_u64(hash, model->next_submit_sequence);
    hash = hash_u64(hash, model->next_array_sequence);
    hash = hash_u64(hash, model->next_trace_sequence);
    hash = hash_u64(hash, model->trace_count);
    hash = hash_u64(hash, model->event_count);
    hash = hash_u64(hash, model->phase);
    for (index = 0; index < model->config.capacity.operations; ++index) {
        const struct c33_operation *operation = &model->operation[index];

        hash = hash_u64(hash, operation->state);
        hash = hash_u64(hash, operation->submit_sequence);
        hash = hash_u64(hash, operation->unit_index);
        hash = hash_u64(hash, operation->unit_count);
        hash = hash_u64(hash, operation->cancel_requested);
        hash = hash_u64(hash, operation->reset_owned);
        hash = hash_u64(hash, operation->legality_reason);
        hash = hash_u64(hash, operation->fault_word);
        hash = hash_u64(hash, operation->request.operation.operation_uid);
        hash = hash_u64(hash, operation->request.operation.controller_epoch);
        hash = hash_u64(hash, operation->request.kind);
        hash = hash_u64(hash, operation->request.ppa.channel);
        hash = hash_u64(hash, operation->request.ppa.lun);
        hash = hash_u64(hash, operation->request.ppa.plane);
        hash = hash_u64(hash, operation->request.ppa.block);
        hash = hash_u64(hash, operation->request.ppa.page);
        hash = hash_u64(hash, operation->start_tick);
        hash = hash_u64(hash, operation->next_tick);
        hash = hash_u64(hash, operation->finish_tick);
        hash = hash_u64(hash, operation->completion.terminal);
        hash = hash_u64(hash, operation->completion.reason);
        hash = hash_u64(hash, operation->completion.physical_outcome);
        if (operation->state != C33_OP_FREE) {
            uint32_t byte;

            for (byte = 0; byte < model->config.geometry.main_bytes_per_page;
                 ++byte) {
                hash = hash_u64(hash, operation->main[byte]);
            }
            for (byte = 0; byte < model->config.geometry.oob_bytes_per_page;
                 ++byte) {
                hash = hash_u64(hash, operation->oob[byte]);
            }
        }
    }
    for (index = 0; index < planes; ++index) {
        hash = hash_u64(hash, model->cache[index].kind);
        hash = hash_u64(hash, model->cache[index].generation);
        hash = hash_u64(hash, model->cache[index].controller_epoch);
        hash = hash_u64(hash, model->cache[index].valid_region_mask);
        hash = hash_u64(hash, model->cache[index].retry_step);
        hash = hash_u64(hash, model->cache[index].ecc_status);
        hash = hash_u64(hash, model->cache[index].fault_word);
        hash = hash_u64(hash, model->cache_tail[index]);
        if (model->cache[index].kind != FWLAB_NFC_CACHE_NONE) {
            uint32_t byte;

            for (byte = 0; byte < model->config.geometry.main_bytes_per_page;
                 ++byte) {
                hash = hash_u64(hash, model->cache[index].main[byte]);
            }
            for (byte = 0; byte < model->config.geometry.oob_bytes_per_page;
                 ++byte) {
                hash = hash_u64(hash, model->cache[index].oob[byte]);
            }
        }
    }
    for (index = 0; index < model->config.geometry.channels; ++index) {
        hash = hash_u64(hash, model->channel_tail[index]);
    }
    for (index = 0; index < array_lanes; ++index) {
        hash = hash_u64(hash, model->array_tail[index]);
    }
    return hash;
}

uint64_t fwlab_nfc_model_media_hash(const struct fwlab_nfc_model *model)
{
    if (!c33_model_valid(model)) {
        return 0;
    }
    return model->media.ops->hash(model->media.context);
}
