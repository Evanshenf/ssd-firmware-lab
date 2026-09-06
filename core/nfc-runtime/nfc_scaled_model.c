/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fwlab/private/nfc_scaled_model.h"
#include "../../nfc/nfc_internal.h"

#include <stdalign.h>
#include <string.h>

/* This file owns an additional construction profile. Execution, scheduling,
 * faults, reset and provider operations remain in the frozen NFC runtime. */
static int zero_words(const uint32_t *words, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i)
        if (words[i] != 0)
            return 0;
    return 1;
}

static int geometry_valid(const struct fwlab_nfc_geometry *g)
{
    uint64_t count, bytes;
    if (g->version != FWLAB_NFC_CONTRACT_VERSION || g->size != sizeof(*g) ||
        !g->channels || g->channels > 4 || !g->luns_per_channel ||
        g->luns_per_channel > 4 || !g->planes_per_lun ||
        g->planes_per_lun > 4 || !g->blocks_per_plane ||
        !g->pages_per_block || g->pages_per_block > 64 ||
        !g->plane_parallelism_per_lun ||
        g->plane_parallelism_per_lun > g->planes_per_lun ||
        g->main_bytes_per_page != 4096 || g->oob_bytes_per_page != 128 ||
        g->max_programs_per_erase != 1 ||
        g->program_order != FWLAB_NFC_PROGRAM_ASCENDING ||
        g->reserved0 || !zero_words(g->reserved1, 2))
        return 0;
    count = (uint64_t)g->channels * g->luns_per_channel;
    if (count > UINT32_MAX / g->planes_per_lun)
        return 0;
    count *= g->planes_per_lun;
    if (count > UINT32_MAX / g->blocks_per_plane)
        return 0;
    count *= g->blocks_per_plane;
    if (count > UINT32_MAX / g->pages_per_block)
        return 0;
    count *= g->pages_per_block;
    bytes = (uint64_t)g->main_bytes_per_page + g->oob_bytes_per_page;
    return count <= (uint64_t)INT64_MAX / bytes;
}

static int ecc_valid(const struct fwlab_nfc_ecc_profile *e,
                     const struct fwlab_nfc_geometry *g)
{
    return e->version == FWLAB_NFC_CONTRACT_VERSION && e->size == sizeof(*e) &&
        e->main_covered_bytes == g->main_bytes_per_page &&
        e->oob_covered_bytes == g->oob_bytes_per_page &&
        e->main_step_bytes && e->oob_step_bytes &&
        e->main_covered_bytes % e->main_step_bytes == 0 &&
        e->oob_covered_bytes % e->oob_step_bytes == 0 &&
        e->main_strength_bits <= UINT8_MAX && e->oob_strength_bits <= UINT8_MAX &&
        e->max_retry_step <= FWLAB_NFC_MODEL_MAX_RETRY_STEP &&
        !e->reserved0[0] && !e->reserved0[1] && !e->reserved0[2] &&
        zero_words(e->reserved1, 2);
}

static int duration_valid(uint32_t duration)
{
    return duration && duration <= FWLAB_NFC_MODEL_MAX_DURATION_TICKS;
}

static int timing_valid(const struct fwlab_nfc_model_config *config)
{
    const struct fwlab_nfc_timing_profile *t = &config->timing;
    uint64_t limit = config->capacity.virtual_tick_limit;
    if (t->version != FWLAB_NFC_CONTRACT_VERSION || t->size != sizeof(*t) ||
        !duration_valid(t->command_ticks) ||
        !duration_valid(t->transfer_ticks_per_unit) ||
        !duration_valid(t->read_array_ticks) ||
        !duration_valid(t->program_setup_ticks) ||
        !duration_valid(t->program_ticks_per_unit) ||
        !duration_valid(t->program_status_ticks) ||
        !duration_valid(t->erase_setup_ticks) ||
        !duration_valid(t->erase_ticks_per_page) ||
        !duration_valid(t->erase_status_ticks) ||
        !duration_valid(t->status_ticks) || !zero_words(t->reserved, 2))
        return 0;
    /* The scheduler subtracts a complete duration from this limit. All
     * products below are promoted before multiplication, including erase. */
    return (uint64_t)t->command_ticks + t->read_array_ticks <= limit &&
        (uint64_t)t->command_ticks + 2u * (uint64_t)t->transfer_ticks_per_unit <= limit &&
        (uint64_t)t->program_setup_ticks + 2u * (uint64_t)t->program_ticks_per_unit +
            t->program_status_ticks <= limit &&
        (uint64_t)t->erase_setup_ticks +
            (uint64_t)config->geometry.pages_per_block * t->erase_ticks_per_page +
            t->erase_status_ticks <= limit && t->status_ticks <= limit;
}

static int capacity_valid(const struct fwlab_nfc_capacity *c,
                          const struct fwlab_nfc_geometry *g)
{
    return c->version == FWLAB_NFC_CONTRACT_VERSION && c->size == sizeof(*c) &&
        c->operations && c->operations <= FWLAB_NFC_MODEL_MAX_OPERATIONS &&
        c->request_registry >= c->operations &&
        c->request_registry <= FWLAB_NFC_MODEL_MAX_OPERATIONS &&
        c->terminal_events >= c->operations &&
        c->terminal_events <= FWLAB_NFC_MODEL_MAX_OPERATIONS &&
        c->result_slots >= c->operations &&
        c->result_slots <= FWLAB_NFC_MODEL_MAX_OPERATIONS &&
        c->trace_entries >= (uint32_t)c->operations * 16u + 1u &&
        !c->reserved0 && c->scratch_main_bytes >= g->main_bytes_per_page &&
        c->scratch_oob_bytes >= g->oob_bytes_per_page &&
        c->operation_generation_limit && c->cache_generation_limit &&
        c->controller_epoch_limit && c->submit_sequence_limit &&
        c->operation_uid_limit && c->virtual_tick_limit &&
        zero_words(c->reserved1, 2);
}

enum fwlab_nfc_api_result fwlab_nfc_scaled_config_validate(
    const struct fwlab_nfc_model_config *config)
{
    if (config == NULL)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    if (config->version != FWLAB_NFC_CONTRACT_VERSION)
        return FWLAB_NFC_API_UNSUPPORTED_VERSION;
    if (config->size != sizeof(*config) || config->reserved0 ||
        !config->successful_erase_limit || config->reserved1 ||
        !zero_words(config->reserved2, 2) || !geometry_valid(&config->geometry) ||
        !ecc_valid(&config->ecc, &config->geometry) || !timing_valid(config) ||
        config->fault.version != FWLAB_NFC_FAULT_PROFILE_VERSION ||
        config->fault.size != sizeof(config->fault) ||
        !config->fault.profile_version || !zero_words(config->fault.reserved, 2) ||
        !capacity_valid(&config->capacity, &config->geometry))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    return FWLAB_NFC_API_OK;
}

struct scaled_layout {
    size_t bytes;
    size_t planes;
    size_t operation, cache, trace, channel_tail, array_tail, cache_tail;
    size_t operation_main, operation_oob, cache_main, cache_oob;
};

static int reserve_region(struct scaled_layout *layout, size_t *region,
                          size_t count, size_t stride, size_t alignment)
{
    size_t aligned, bytes;
    if (!alignment || (alignment & (alignment - 1u)) ||
        !c33_checked_add_size(layout->bytes, alignment - 1u, &aligned) ||
        !c33_checked_mul_size(count, stride, &bytes))
        return 0;
    aligned &= ~(alignment - 1u);
    if (!c33_checked_add_size(aligned, bytes, &layout->bytes))
        return 0;
    *region = aligned;
    return 1;
}

static int make_layout(const struct fwlab_nfc_model_config *config,
                       struct scaled_layout *layout)
{
    size_t luns, lanes, end;
    size_t operations;
    if (fwlab_nfc_scaled_config_validate(config) != FWLAB_NFC_API_OK)
        return 0;
    memset(layout, 0, sizeof(*layout));
    layout->bytes = sizeof(struct fwlab_nfc_model);
    operations = config->capacity.operations;
    if (!c33_checked_mul_size(config->geometry.channels,
                              config->geometry.luns_per_channel, &luns) ||
        !c33_checked_mul_size(luns, config->geometry.planes_per_lun,
                              &layout->planes) ||
        !c33_checked_mul_size(luns, config->geometry.plane_parallelism_per_lun,
                              &lanes) ||
        !reserve_region(layout, &end, 0, 1, alignof(max_align_t)) ||
        !reserve_region(layout, &layout->operation, operations,
                         sizeof(struct c33_operation), alignof(struct c33_operation)) ||
        !reserve_region(layout, &layout->cache, layout->planes,
                         sizeof(struct c33_plane_cache), alignof(struct c33_plane_cache)) ||
        !reserve_region(layout, &layout->trace, config->capacity.trace_entries,
                         sizeof(struct fwlab_nfc_trace_entry), alignof(struct fwlab_nfc_trace_entry)) ||
        !reserve_region(layout, &layout->channel_tail, config->geometry.channels,
                         sizeof(uint64_t), alignof(uint64_t)) ||
        !reserve_region(layout, &layout->array_tail, lanes,
                         sizeof(uint64_t), alignof(uint64_t)) ||
        !reserve_region(layout, &layout->cache_tail, layout->planes,
                         sizeof(uint64_t), alignof(uint64_t)) ||
        !reserve_region(layout, &layout->operation_main, operations,
                         config->capacity.scratch_main_bytes, 1) ||
        !reserve_region(layout, &layout->operation_oob, operations,
                         config->capacity.scratch_oob_bytes, 1) ||
        !reserve_region(layout, &layout->cache_main, layout->planes,
                         config->geometry.main_bytes_per_page, 1) ||
        !reserve_region(layout, &layout->cache_oob, layout->planes,
                         config->geometry.oob_bytes_per_page, 1) ||
        !reserve_region(layout, &end, 0, 1, alignof(max_align_t)))
        return 0;
    return 1;
}

size_t fwlab_nfc_scaled_arena_size(const struct fwlab_nfc_model_config *config)
{
    struct scaled_layout layout;
    return make_layout(config, &layout) ? layout.bytes : 0;
}

enum fwlab_nfc_api_result fwlab_nfc_scaled_init(
    void *arena, size_t arena_size, const struct fwlab_nfc_model_config *config,
    uint64_t instance_nonce, const struct fwlab_nfc_buffer_provider *buffers,
    const struct fwlab_nand_media *media, struct fwlab_nfc_model **out)
{
    struct scaled_layout layout;
    struct fwlab_nfc_model *model = arena;
    uint8_t *bytes = arena;
    size_t i;
    if (out == NULL)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    *out = NULL;
    if (arena == NULL || (uintptr_t)arena % alignof(max_align_t) ||
        !instance_nonce || !make_layout(config, &layout) || arena_size < layout.bytes ||
        !buffers || !buffers->context || !buffers->ops ||
        buffers->ops->version != FWLAB_NFC_CONTRACT_VERSION ||
        buffers->ops->size != sizeof(*buffers->ops) || buffers->ops->reserved ||
        !buffers->ops->read || !buffers->ops->write ||
        !media || !media->context || !media->ops ||
        media->ops->version != FWLAB_NFC_CONTRACT_VERSION ||
        media->ops->size != sizeof(*media->ops) || media->ops->reserved ||
        !media->ops->read_page || !media->ops->program || !media->ops->erase ||
        !media->ops->mark_runtime_bad || !media->ops->hash)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    memset(arena, 0, layout.bytes);
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
    model->operation = (struct c33_operation *)(bytes + layout.operation);
    model->cache = (struct c33_plane_cache *)(bytes + layout.cache);
    model->trace = (struct fwlab_nfc_trace_entry *)(bytes + layout.trace);
    model->channel_tail = (uint64_t *)(bytes + layout.channel_tail);
    model->array_tail = (uint64_t *)(bytes + layout.array_tail);
    model->cache_tail = (uint64_t *)(bytes + layout.cache_tail);
    model->operation_main = bytes + layout.operation_main;
    model->operation_oob = bytes + layout.operation_oob;
    model->cache_main = bytes + layout.cache_main;
    model->cache_oob = bytes + layout.cache_oob;
    for (i = 0; i < config->capacity.operations; ++i) {
        model->operation[i].main = model->operation_main + i * config->capacity.scratch_main_bytes;
        model->operation[i].oob = model->operation_oob + i * config->capacity.scratch_oob_bytes;
    }
    for (i = 0; i < layout.planes; ++i) {
        model->cache[i].main = model->cache_main + i * config->geometry.main_bytes_per_page;
        model->cache[i].oob = model->cache_oob + i * config->geometry.oob_bytes_per_page;
    }
    model->magic = C33_MODEL_MAGIC;
    c33_trace(model, FWLAB_NFC_TRACE_INIT, NULL, 0, 0, 0);
    *out = model;
    return FWLAB_NFC_API_OK;
}
