/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "nfc_internal.h"

#include <string.h>

static unsigned int region_count(uint8_t mask)
{
    return (unsigned int)((mask & FWLAB_NFC_REGION_MAIN) != 0) +
           (unsigned int)((mask & FWLAB_NFC_REGION_OOB) != 0);
}

static uint64_t next_unit_tick(const struct c33_operation *operation)
{
    uint64_t duration = operation->finish_tick - operation->start_tick;
    uint64_t numerator = duration * (operation->unit_index + 1u);

    return operation->start_tick +
           (numerator + operation->unit_count - 1u) /
               operation->unit_count;
}

static int ppa_equal(
    const struct fwlab_nfc_ppa *left,
    const struct fwlab_nfc_ppa *right
)
{
    return left->channel == right->channel && left->lun == right->lun &&
           left->plane == right->plane && left->block == right->block &&
           left->page == right->page;
}

static int cache_token_valid(
    const struct fwlab_nfc_model *model,
    const struct fwlab_nfc_request *request,
    uint8_t expected_kind,
    const struct c33_plane_cache **cache_out
)
{
    uint32_t index = c33_plane_index(&model->config.geometry, &request->ppa);
    const struct c33_plane_cache *cache = &model->cache[index];

    if (request->cache.instance_nonce != model->instance_nonce ||
        request->cache.controller_epoch != model->current_epoch ||
        request->cache.channel != request->ppa.channel ||
        request->cache.lun != request->ppa.lun ||
        request->cache.plane != request->ppa.plane ||
        request->cache.generation != cache->generation ||
        cache->kind != expected_kind ||
        cache->controller_epoch != model->current_epoch ||
        (request->region_mask & ~cache->valid_region_mask) != 0 ||
        (expected_kind == FWLAB_NFC_CACHE_READ &&
         request->retry_step != cache->retry_step) ||
        (expected_kind == FWLAB_NFC_CACHE_PROGRAM &&
         request->region_mask != cache->valid_region_mask) ||
        !ppa_equal(&cache->ppa, &request->ppa)) {
        return 0;
    }
    *cache_out = cache;
    return 1;
}

static void completion_failure(
    struct c33_operation *operation,
    uint8_t reason
)
{
    operation->completion.terminal = FWLAB_NFC_TERMINAL_FAILED;
    operation->completion.physical_outcome = FWLAB_NFC_PHYS_NO_EFFECT;
    operation->completion.integrity = FWLAB_NFC_INTEGRITY_NOT_APPLICABLE;
    operation->completion.reason = reason;
    operation->completion.ecc_status = FWLAB_NFC_ECC_NOT_APPLICABLE;
}

static void completion_success(struct c33_operation *operation)
{
    operation->completion.terminal = FWLAB_NFC_TERMINAL_SUCCESS;
    operation->completion.physical_outcome = FWLAB_NFC_PHYS_NO_EFFECT;
    operation->completion.integrity = FWLAB_NFC_INTEGRITY_NOT_APPLICABLE;
    operation->completion.reason = FWLAB_NFC_REASON_NONE;
}

static void completion_cancelled(struct c33_operation *operation)
{
    operation->completion.terminal = FWLAB_NFC_TERMINAL_CANCELLED;
    operation->completion.physical_outcome = FWLAB_NFC_PHYS_NO_EFFECT;
    operation->completion.integrity = FWLAB_NFC_INTEGRITY_NOT_APPLICABLE;
    operation->completion.reason = FWLAB_NFC_REASON_CANCELLED;
}

static void read_media_state(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation,
    struct fwlab_nand_page_info *page,
    struct fwlab_nand_block_info *block
)
{
    enum fwlab_nfc_api_result result = model->media.ops->read_page(
        model->media.context, &operation->request.ppa, operation->main,
        model->config.geometry.main_bytes_per_page, operation->oob,
        model->config.geometry.oob_bytes_per_page, page, block);

    if (result != FWLAB_NFC_API_OK) {
        operation->legality_reason = FWLAB_NFC_REASON_INTERNAL;
    }
}

static uint64_t payload_digest(
    const struct fwlab_nfc_model *model,
    const struct c33_operation *operation
)
{
    uint64_t hash = c33_hash_bytes(operation->main,
                                   model->config.geometry.main_bytes_per_page);

    if ((operation->request.region_mask & FWLAB_NFC_REGION_OOB) != 0) {
        uint64_t oob_hash = c33_hash_bytes(
            operation->oob, model->config.geometry.oob_bytes_per_page);
        uint8_t bytes[8];
        unsigned int index;

        for (index = 0; index < sizeof(bytes); ++index) {
            bytes[index] = (uint8_t)(oob_hash >> (index * 8u));
        }
        hash ^= c33_hash_bytes(bytes, sizeof(bytes));
    }
    return hash;
}

static void begin_read_trigger(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
)
{
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
    uint16_t main_errors;
    uint16_t oob_errors;

    read_media_state(model, operation, &page, &block);
    operation->completion.base_erase_generation = block.erase_generation;
    operation->completion.final_erase_generation = block.erase_generation;
    operation->completion.block_health = block.health;
    if (operation->legality_reason != 0) {
        return;
    }
    if (block.health != FWLAB_NFC_BLOCK_GOOD) {
        operation->legality_reason = FWLAB_NFC_REASON_BAD_BLOCK;
        return;
    }
    if (block.erase_state == FWLAB_NAND_ERASE_TORN ||
        page.state == FWLAB_NAND_PAGE_TORN) {
        operation->completion.corrected_main_bits =
            (uint16_t)(model->config.ecc.main_strength_bits + 1u);
        operation->completion.corrected_oob_bits =
            (uint16_t)(model->config.ecc.oob_strength_bits + 1u);
        operation->completion.ecc_status = FWLAB_NFC_ECC_UNCORRECTABLE;
        operation->fault_word = c33_fault_word(
            model, operation, block.erase_generation, page.program_count,
            operation->submit_sequence);
        return;
    }
    operation->fault_word = c33_fault_word(
        model, operation, block.erase_generation, page.program_count,
        operation->submit_sequence);
    c33_fault_read_counts(model, operation, operation->fault_word,
                          &main_errors, &oob_errors);
    operation->completion.corrected_main_bits = main_errors;
    operation->completion.corrected_oob_bits = oob_errors;
    if (((operation->request.region_mask & FWLAB_NFC_REGION_MAIN) != 0 &&
         main_errors > model->config.ecc.main_strength_bits) ||
        ((operation->request.region_mask & FWLAB_NFC_REGION_OOB) != 0 &&
         oob_errors > model->config.ecc.oob_strength_bits)) {
        operation->completion.ecc_status = FWLAB_NFC_ECC_UNCORRECTABLE;
    } else if (main_errors != 0 || oob_errors != 0) {
        operation->completion.ecc_status = FWLAB_NFC_ECC_CORRECTED;
    } else {
        operation->completion.ecc_status = FWLAB_NFC_ECC_CLEAN;
    }
}

static void begin_program_execute(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
)
{
    const struct c33_plane_cache *cache;
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;

    if (!cache_token_valid(model, &operation->request,
                           FWLAB_NFC_CACHE_PROGRAM, &cache)) {
        operation->legality_reason = FWLAB_NFC_REASON_STALE;
        return;
    }
    read_media_state(model, operation, &page, &block);
    operation->completion.base_erase_generation = block.erase_generation;
    operation->completion.final_erase_generation = block.erase_generation;
    operation->completion.block_health = block.health;
    if (operation->legality_reason != 0) {
        return;
    }
    if (block.health != FWLAB_NFC_BLOCK_GOOD) {
        operation->legality_reason = FWLAB_NFC_REASON_BAD_BLOCK;
        return;
    }
    if (block.erase_state != FWLAB_NAND_ERASE_CLEAN ||
        page.state != FWLAB_NAND_PAGE_ERASED || page.program_count != 0) {
        operation->legality_reason = FWLAB_NFC_REASON_NOT_ERASED;
        return;
    }
    if (model->config.geometry.program_order ==
            FWLAB_NFC_PROGRAM_ASCENDING &&
        operation->request.ppa.page != block.next_program_page) {
        operation->legality_reason = FWLAB_NFC_REASON_PROGRAM_ORDER;
        return;
    }
    memcpy(operation->main, cache->main,
           model->config.geometry.main_bytes_per_page);
    memcpy(operation->oob, cache->oob,
           model->config.geometry.oob_bytes_per_page);
    operation->completion.payload_digest = payload_digest(model, operation);
    operation->fault_word = c33_fault_word(
        model, operation, block.erase_generation, page.program_count, 0);
    operation->fault_class = c33_fault_classify(
        model, operation, operation->fault_word);
}

static void begin_erase(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
)
{
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;

    read_media_state(model, operation, &page, &block);
    operation->completion.base_erase_generation = block.erase_generation;
    operation->completion.final_erase_generation = block.erase_generation;
    operation->completion.block_health = block.health;
    if (operation->legality_reason != 0) {
        return;
    }
    if (block.health != FWLAB_NFC_BLOCK_GOOD) {
        operation->legality_reason = FWLAB_NFC_REASON_BAD_BLOCK;
        return;
    }
    if (block.successful_erase_count >=
        model->config.successful_erase_limit) {
        operation->fault_class = C33_FAULT_GROWN_BAD;
        return;
    }
    operation->fault_word = c33_fault_word(
        model, operation, block.erase_generation, page.program_count, 0);
    operation->fault_class = c33_fault_classify(
        model, operation, operation->fault_word);
}

int c33_begin_operation(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
)
{
    uint8_t from = operation->state;

    operation->completion.begin_tick = model->virtual_now;
    operation->completion.status_tick = operation->finish_tick;
    operation->completion.outcome_tick = operation->finish_tick;
    if (operation->cancel_requested && operation->state == C33_OP_SCHEDULED) {
        operation->legality_reason = FWLAB_NFC_REASON_CANCELLED;
    } else if (operation->request.kind == FWLAB_NFC_READ_TRIGGER) {
        begin_read_trigger(model, operation);
    } else if (operation->request.kind == FWLAB_NFC_READ_TRANSFER) {
        const struct c33_plane_cache *cache;

        if (!cache_token_valid(model, &operation->request,
                               FWLAB_NFC_CACHE_READ, &cache)) {
            operation->legality_reason = FWLAB_NFC_REASON_STALE;
        } else {
            operation->fault_word = cache->fault_word;
            operation->completion.ecc_status = cache->ecc_status;
            operation->completion.corrected_main_bits =
                cache->corrected_main_bits;
            operation->completion.corrected_oob_bits =
                cache->corrected_oob_bits;
        }
    } else if (operation->request.kind == FWLAB_NFC_PROGRAM_EXECUTE) {
        begin_program_execute(model, operation);
    } else if (operation->request.kind == FWLAB_NFC_ERASE) {
        begin_erase(model, operation);
    }
    if (operation->request.kind == FWLAB_NFC_READ_TRIGGER ||
        operation->request.kind == FWLAB_NFC_PROGRAM_EXECUTE ||
        operation->request.kind == FWLAB_NFC_ERASE) {
        if (model->next_array_sequence == 0 ||
            model->next_array_sequence >
                model->config.capacity.submit_sequence_limit) {
            return 0;
        }
        operation->array_sequence = model->next_array_sequence++;
        operation->completion.array_sequence = operation->array_sequence;
    }
    operation->completion.frozen_fault_word = operation->fault_word;
    operation->unit_count = (uint8_t)region_count(
        operation->request.region_mask);
    if (operation->request.kind == FWLAB_NFC_ERASE) {
        operation->unit_count = (uint8_t)
            model->config.geometry.pages_per_block;
    }
    if (operation->unit_count == 0 || operation->legality_reason != 0) {
        operation->unit_count = 1;
    }
    operation->unit_index = 0;
    operation->state = C33_OP_RUNNING;
    operation->next_tick = next_unit_tick(operation);
    c33_trace(model, FWLAB_NFC_TRACE_START, operation, from,
              C33_OP_RUNNING, operation->legality_reason);
    return 1;
}

static int allocate_cache_generation(
    struct fwlab_nfc_model *model,
    struct c33_plane_cache *cache
)
{
    if (cache->generation >= model->config.capacity.cache_generation_limit) {
        return 0;
    }
    ++cache->generation;
    cache->controller_epoch = model->current_epoch;
    return 1;
}

static void completion_cache_token(
    const struct fwlab_nfc_model *model,
    const struct c33_plane_cache *cache,
    struct fwlab_nfc_completion *completion
)
{
    completion->cache.instance_nonce = model->instance_nonce;
    completion->cache.controller_epoch = model->current_epoch;
    completion->cache.generation = cache->generation;
    completion->cache.channel = cache->ppa.channel;
    completion->cache.lun = cache->ppa.lun;
    completion->cache.plane = cache->ppa.plane;
}

static int finish_program_transfer(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
)
{
    uint32_t index = c33_plane_index(&model->config.geometry,
                                     &operation->request.ppa);
    struct c33_plane_cache *cache = &model->cache[index];

    if (!allocate_cache_generation(model, cache)) {
        return 0;
    }
    cache->kind = FWLAB_NFC_CACHE_PROGRAM;
    cache->valid_region_mask = operation->request.region_mask;
    cache->ppa = operation->request.ppa;
    memcpy(cache->main, operation->main,
           model->config.geometry.main_bytes_per_page);
    memcpy(cache->oob, operation->oob,
           model->config.geometry.oob_bytes_per_page);
    completion_cache_token(model, cache, &operation->completion);
    operation->completion.payload_digest = payload_digest(model, operation);
    completion_success(operation);
    operation->completion.valid_region_mask = operation->request.region_mask;
    return 1;
}

static int finish_read_trigger(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
)
{
    uint32_t index = c33_plane_index(&model->config.geometry,
                                     &operation->request.ppa);
    struct c33_plane_cache *cache = &model->cache[index];

    if (!allocate_cache_generation(model, cache)) {
        return 0;
    }
    cache->kind = FWLAB_NFC_CACHE_READ;
    cache->valid_region_mask = operation->request.region_mask;
    cache->ppa = operation->request.ppa;
    cache->fault_word = operation->fault_word;
    cache->ecc_status = operation->completion.ecc_status;
    cache->retry_step = operation->request.retry_step;
    cache->corrected_main_bits = operation->completion.corrected_main_bits;
    cache->corrected_oob_bits = operation->completion.corrected_oob_bits;
    cache->erase_generation =
        operation->completion.base_erase_generation;
    memcpy(cache->main, operation->main,
           model->config.geometry.main_bytes_per_page);
    memcpy(cache->oob, operation->oob,
           model->config.geometry.oob_bytes_per_page);
    completion_cache_token(model, cache, &operation->completion);
    completion_success(operation);
    operation->completion.valid_region_mask = operation->request.region_mask;
    return 1;
}

static int finish_read_transfer(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
)
{
    const struct c33_plane_cache *cache_const;
    struct c33_plane_cache *cache;
    uint32_t index;

    if (!cache_token_valid(model, &operation->request,
                           FWLAB_NFC_CACHE_READ, &cache_const)) {
        completion_failure(operation, FWLAB_NFC_REASON_STALE);
        return 1;
    }
    if (cache_const->ecc_status == FWLAB_NFC_ECC_UNCORRECTABLE) {
        completion_failure(operation, FWLAB_NFC_REASON_ECC_UNCORRECTABLE);
        operation->completion.ecc_status = FWLAB_NFC_ECC_UNCORRECTABLE;
        return 1;
    }
    if ((operation->request.region_mask & FWLAB_NFC_REGION_MAIN) != 0 &&
        model->buffers.ops->write(
            model->buffers.context, &operation->request.main,
            cache_const->main, operation->request.main.length) !=
                FWLAB_NFC_API_OK) {
        return 0;
    }
    if ((operation->request.region_mask & FWLAB_NFC_REGION_OOB) != 0 &&
        model->buffers.ops->write(
            model->buffers.context, &operation->request.oob,
            cache_const->oob, operation->request.oob.length) !=
                FWLAB_NFC_API_OK) {
        return 0;
    }
    operation->completion.ecc_status = cache_const->ecc_status;
    operation->completion.corrected_main_bits =
        cache_const->corrected_main_bits;
    operation->completion.corrected_oob_bits =
        cache_const->corrected_oob_bits;
    operation->completion.valid_region_mask = operation->request.region_mask;
    completion_success(operation);
    index = c33_plane_index(&model->config.geometry, &operation->request.ppa);
    cache = &model->cache[index];
    cache->kind = FWLAB_NFC_CACHE_NONE;
    cache->valid_region_mask = 0;
    return 1;
}

static void apply_media_result(
    struct c33_operation *operation,
    const struct fwlab_nand_media_result *media_result
)
{
    operation->completion.physical_outcome =
        media_result->physical_outcome;
    operation->completion.integrity = media_result->integrity;
    operation->completion.applied_region_mask =
        media_result->applied_region_mask;
    operation->completion.base_erase_generation =
        media_result->base_erase_generation;
    operation->completion.final_erase_generation =
        media_result->final_erase_generation;
    operation->completion.block_health = media_result->block_health;
}

static int finish_program_execute(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
)
{
    struct fwlab_nand_media_result media_result;
    uint32_t main_applied = model->config.geometry.main_bytes_per_page;
    uint32_t oob_applied = model->config.geometry.oob_bytes_per_page;
    uint8_t integrity = FWLAB_NFC_INTEGRITY_COMPLETE;
    enum fwlab_nfc_api_result result;
    uint32_t cache_index = c33_plane_index(
        &model->config.geometry, &operation->request.ppa);

    if (operation->fault_class == C33_FAULT_GROWN_BAD) {
        if (model->media.ops->mark_runtime_bad(
                model->media.context, &operation->request.ppa) !=
            FWLAB_NFC_API_OK) {
            return 0;
        }
        completion_failure(operation, FWLAB_NFC_REASON_PROGRAM_FAILURE);
        operation->completion.block_health = FWLAB_NFC_BLOCK_RUNTIME_BAD;
        model->cache[cache_index].kind = FWLAB_NFC_CACHE_NONE;
        return 1;
    }
    if (operation->fault_class == C33_FAULT_NO_EFFECT) {
        completion_failure(operation, FWLAB_NFC_REASON_PROGRAM_FAILURE);
        model->cache[cache_index].kind = FWLAB_NFC_CACHE_NONE;
        return 1;
    }
    if (operation->fault_class == C33_FAULT_TORN) {
        integrity = FWLAB_NFC_INTEGRITY_TORN;
        oob_applied = 0;
        main_applied = main_applied > 1 ? main_applied / 2u : 1u;
    }
    result = model->media.ops->program(
        model->media.context, &operation->request.ppa, operation->main,
        model->config.geometry.main_bytes_per_page, operation->oob,
        model->config.geometry.oob_bytes_per_page, main_applied, oob_applied,
        integrity, &media_result);
    if (result != FWLAB_NFC_API_OK) {
        return 0;
    }
    apply_media_result(operation, &media_result);
    model->cache[cache_index].kind = FWLAB_NFC_CACHE_NONE;
    if (media_result.physical_outcome == FWLAB_NFC_PHYS_APPLIED &&
        media_result.integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
        operation->fault_class == C33_FAULT_CLEAN) {
        operation->completion.terminal = FWLAB_NFC_TERMINAL_SUCCESS;
        operation->completion.reason = FWLAB_NFC_REASON_NONE;
        operation->completion.valid_region_mask =
            operation->request.region_mask;
    } else {
        operation->completion.terminal = FWLAB_NFC_TERMINAL_FAILED;
        operation->completion.reason = media_result.reason != 0 ?
            media_result.reason : FWLAB_NFC_REASON_PROGRAM_FAILURE;
    }
    return 1;
}

static int finish_erase(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
)
{
    struct fwlab_nand_media_result media_result;
    uint32_t pages = model->config.geometry.pages_per_block;
    uint8_t integrity = FWLAB_NFC_INTEGRITY_COMPLETE;
    enum fwlab_nfc_api_result result;

    if (operation->fault_class == C33_FAULT_GROWN_BAD) {
        if (model->media.ops->mark_runtime_bad(
                model->media.context, &operation->request.ppa) !=
            FWLAB_NFC_API_OK) {
            return 0;
        }
        completion_failure(operation, FWLAB_NFC_REASON_WEAR_OUT);
        operation->completion.block_health = FWLAB_NFC_BLOCK_RUNTIME_BAD;
        return 1;
    }
    if (operation->fault_class == C33_FAULT_NO_EFFECT) {
        completion_failure(operation, FWLAB_NFC_REASON_ERASE_FAILURE);
        return 1;
    }
    if (operation->fault_class == C33_FAULT_TORN) {
        integrity = FWLAB_NFC_INTEGRITY_TORN;
        pages = pages > 1 ? pages / 2u : 1u;
    }
    result = model->media.ops->erase(
        model->media.context, &operation->request.ppa, pages, integrity,
        &media_result);
    if (result != FWLAB_NFC_API_OK) {
        return 0;
    }
    apply_media_result(operation, &media_result);
    if (media_result.physical_outcome == FWLAB_NFC_PHYS_APPLIED &&
        media_result.integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
        operation->fault_class == C33_FAULT_CLEAN) {
        operation->completion.terminal = FWLAB_NFC_TERMINAL_SUCCESS;
        operation->completion.reason = FWLAB_NFC_REASON_NONE;
        operation->completion.valid_region_mask = FWLAB_NFC_REGION_MASK;
    } else {
        operation->completion.terminal = FWLAB_NFC_TERMINAL_FAILED;
        operation->completion.reason = media_result.reason != 0 ?
            media_result.reason : FWLAB_NFC_REASON_ERASE_FAILURE;
    }
    return 1;
}

int c33_finish_operation(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
)
{
    uint8_t from = operation->state;
    int ok = 1;

    if (operation->reset_owned &&
        operation->request.kind != FWLAB_NFC_PROGRAM_EXECUTE &&
        operation->request.kind != FWLAB_NFC_ERASE) {
        completion_failure(operation, FWLAB_NFC_REASON_RESET);
    } else if (operation->legality_reason == FWLAB_NFC_REASON_CANCELLED) {
        completion_cancelled(operation);
    } else if (operation->legality_reason != 0) {
        completion_failure(operation, operation->legality_reason);
    } else {
        switch ((enum fwlab_nfc_operation_kind)operation->request.kind) {
        case FWLAB_NFC_READ_TRIGGER:
            ok = finish_read_trigger(model, operation);
            break;
        case FWLAB_NFC_READ_TRANSFER:
            ok = finish_read_transfer(model, operation);
            break;
        case FWLAB_NFC_PROGRAM_TRANSFER:
            ok = finish_program_transfer(model, operation);
            break;
        case FWLAB_NFC_PROGRAM_EXECUTE:
            ok = finish_program_execute(model, operation);
            break;
        case FWLAB_NFC_ERASE:
            ok = finish_erase(model, operation);
            break;
        case FWLAB_NFC_STATUS:
            completion_success(operation);
            break;
        default:
            ok = 0;
            break;
        }
    }
    if (!ok || model->event_count >= model->config.capacity.terminal_events) {
        return 0;
    }
    operation->completion.outcome_tick = model->virtual_now;
    operation->completion.status_tick = model->virtual_now;
    operation->completion.frozen_fault_word = operation->fault_word;
    operation->state = C33_OP_EVENT_PENDING;
    ++model->event_count;
    c33_trace(model, FWLAB_NFC_TRACE_OUTCOME, operation, from,
              C33_OP_EVENT_PENDING, operation->completion.reason);
    return 1;
}

int c33_commit_power_prefix(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
)
{
    struct fwlab_nand_media_result media_result;
    enum fwlab_nfc_api_result result;

    if (operation->unit_index == 0) {
        return 1;
    }
    if (operation->request.kind == FWLAB_NFC_PROGRAM_EXECUTE &&
        operation->legality_reason == 0) {
        uint32_t main_applied =
            model->config.geometry.main_bytes_per_page;
        uint32_t oob_applied = operation->unit_index > 1 ?
            model->config.geometry.oob_bytes_per_page : 0;

        result = model->media.ops->program(
            model->media.context, &operation->request.ppa, operation->main,
            model->config.geometry.main_bytes_per_page, operation->oob,
            model->config.geometry.oob_bytes_per_page, main_applied,
            oob_applied, FWLAB_NFC_INTEGRITY_TORN, &media_result);
        return result == FWLAB_NFC_API_OK;
    }
    if (operation->request.kind == FWLAB_NFC_ERASE &&
        operation->legality_reason == 0) {
        uint32_t pages = operation->unit_index;

        if (pages > model->config.geometry.pages_per_block) {
            pages = model->config.geometry.pages_per_block;
        }
        result = model->media.ops->erase(
            model->media.context, &operation->request.ppa, pages,
            FWLAB_NFC_INTEGRITY_TORN, &media_result);
        return result == FWLAB_NFC_API_OK;
    }
    return 1;
}
