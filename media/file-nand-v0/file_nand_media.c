/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "file_nand_internal.h"

#include <string.h>

static enum fwlab_nfc_api_result media_write_phase_last(
    struct fwlab_file_nand_v0 *media, uint64_t offset, const uint8_t *bytes,
    size_t size, size_t marker_offset)
{
    enum fwlab_nfc_api_result result;

    result = media->substrate.ops->write(media->substrate.context, offset,
                                         bytes, marker_offset);
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    if (marker_offset + 4 < size) {
        result = media->substrate.ops->write(
            media->substrate.context, offset + marker_offset + 4,
            &bytes[marker_offset + 4], size - marker_offset - 4);
        if (result != FWLAB_NFC_API_OK) {
            return result;
        }
    }
    return media->substrate.ops->write(media->substrate.context,
        offset + marker_offset, &bytes[marker_offset], 4);
}

enum fwlab_nfc_api_result file_nand_read_selected_health(
    struct fwlab_file_nand_v0 *media, uint8_t block,
    struct fwlab_nand_block_info *info)
{
    uint8_t bytes[256];
    uint64_t generation;
    uint64_t transaction_uid;
    uint32_t crc;
    uint8_t slot;
    enum fwlab_nfc_api_result result;

    if (!file_nand_validate_live(media) || block >= FILE_NAND_BLOCK_COUNT ||
        info == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    slot = media->selected_health_slot[block];
    result = media->substrate.ops->read(media->substrate.context,
        file_nand_health_slot_offset(block, slot), bytes, sizeof(bytes));
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    result = file_nand_decode_health_candidate(bytes, block, slot, info,
                                                &generation,
                                                &transaction_uid, &crc);
    if (result != FWLAB_NFC_API_OK ||
        generation != media->health_generation[block]) {
        media->quarantined = 1;
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    if (info->erase_state == FWLAB_NAND_ERASE_CLEAN) {
        uint8_t page_number;

        info->next_program_page = FILE_NAND_PAGES_PER_BLOCK;
        for (page_number = 0; page_number < FILE_NAND_PAGES_PER_BLOCK;
             ++page_number) {
            uint8_t page_bytes[8192];
            uint8_t page_main[4096];
            uint8_t page_oob[128];
            struct fwlab_nand_page_info page_info;
            uint16_t linear = (uint16_t)(block * FILE_NAND_PAGES_PER_BLOCK +
                                         page_number);
            uint8_t page_slot = media->selected_page_slot[linear];
            uint64_t page_generation;
            uint64_t page_transaction;
            uint32_t page_crc;

            result = media->substrate.ops->read(media->substrate.context,
                file_nand_page_slot_offset(linear, page_slot), page_bytes,
                sizeof(page_bytes));
            if (result != FWLAB_NFC_API_OK ||
                file_nand_decode_page_candidate(page_bytes, linear, page_slot,
                    &page_info, page_main, page_oob, &page_generation,
                    &page_transaction, &page_crc) != FWLAB_NFC_API_OK ||
                page_generation != media->page_generation[linear]) {
                media->quarantined = 1;
                return FWLAB_NFC_API_INVARIANT_FAILURE;
            }
            if (page_info.state == FWLAB_NAND_PAGE_ERASED) {
                info->next_program_page = page_number;
                break;
            }
        }
    }
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result file_nand_read_selected_page(
    struct fwlab_file_nand_v0 *media, const struct fwlab_nfc_ppa *ppa,
    uint8_t main[4096], uint8_t oob[128],
    struct fwlab_nand_page_info *page, struct fwlab_nand_block_info *block)
{
    uint8_t bytes[8192];
    uint16_t linear;
    uint8_t slot;
    uint64_t generation;
    uint64_t transaction_uid;
    uint32_t crc;
    enum fwlab_nfc_api_result result;

    if (!file_nand_validate_live(media) || !file_nand_ppa_valid(ppa) ||
        main == NULL || oob == NULL || page == NULL || block == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    linear = file_nand_linear_page(ppa);
    slot = media->selected_page_slot[linear];
    result = media->substrate.ops->read(media->substrate.context,
        file_nand_page_slot_offset(linear, slot), bytes, sizeof(bytes));
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    result = file_nand_decode_page_candidate(bytes, linear, slot, page,
        main, oob, &generation, &transaction_uid, &crc);
    if (result != FWLAB_NFC_API_OK ||
        generation != media->page_generation[linear]) {
        media->quarantined = 1;
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    return file_nand_read_selected_health(media, (uint8_t)ppa->block, block);
}

static enum fwlab_nfc_api_result media_read(
    void *opaque, const struct fwlab_nfc_ppa *ppa, uint8_t *main,
    uint32_t main_length, uint8_t *oob, uint32_t oob_length,
    struct fwlab_nand_page_info *page, struct fwlab_nand_block_info *block)
{
    if (main_length != FILE_NAND_MAIN_BYTES ||
        oob_length != FILE_NAND_OOB_BYTES) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    return file_nand_read_selected_page(opaque, ppa, main, oob, page, block);
}

static enum fwlab_nfc_api_result write_page_candidate(
    struct fwlab_file_nand_v0 *media, struct file_nand_transaction *transaction,
    uint16_t descriptor, const struct fwlab_nfc_ppa *ppa, uint8_t state,
    uint8_t program_count, uint16_t erase_generation,
    const uint8_t main[4096], const uint8_t oob[128])
{
    uint8_t bytes[8192];
    uint16_t linear = file_nand_linear_page(ppa);
    uint8_t slot = (uint8_t)(1u - media->selected_page_slot[linear]);
    uint64_t generation = media->page_generation[linear] + 1u;
    uint32_t crc;
    enum fwlab_nfc_api_result result;

    if (generation > UINT32_MAX || descriptor >= 32) {
        return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    }
    result = file_nand_encode_page_candidate(bytes, generation,
        transaction->transaction_uid, ppa, slot, state, program_count,
        erase_generation, main, oob, &crc);
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    result = media_write_phase_last(media,
        file_nand_page_slot_offset(linear, slot), bytes, sizeof(bytes), 124);
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    transaction->candidate[descriptor].linear_page = linear;
    transaction->candidate[descriptor].slot = slot;
    transaction->candidate[descriptor].kind = 1;
    transaction->candidate[descriptor].generation = (uint32_t)generation;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result write_health_candidate(
    struct fwlab_file_nand_v0 *media, struct file_nand_transaction *transaction,
    uint8_t block, const struct fwlab_nand_block_info *info)
{
    uint8_t bytes[256];
    uint8_t slot = (uint8_t)(1u - media->selected_health_slot[block]);
    uint64_t generation = media->health_generation[block] + 1u;
    uint32_t crc;
    enum fwlab_nfc_api_result result;

    if (generation > UINT32_MAX) {
        return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    }
    result = file_nand_encode_health_candidate(bytes, generation,
        transaction->transaction_uid, block, slot, info, &crc);
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    result = media_write_phase_last(media,
        file_nand_health_slot_offset(block, slot), bytes, sizeof(bytes), 252);
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    transaction->health_present = 1;
    transaction->health_block = block;
    transaction->health_slot = slot;
    transaction->health_generation = (uint32_t)generation;
    transaction->health_crc = crc;
    return FWLAB_NFC_API_OK;
}

static void result_no_effect(struct fwlab_nand_media_result *result,
                             const struct fwlab_nand_block_info *block,
                             uint8_t reason)
{
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_NFC_CONTRACT_VERSION;
    result->size = (uint16_t)sizeof(*result);
    result->physical_outcome = FWLAB_NFC_PHYS_NO_EFFECT;
    result->integrity = FWLAB_NFC_INTEGRITY_NOT_APPLICABLE;
    result->reason = reason;
    result->block_health = block->health;
    result->base_erase_generation = block->erase_generation;
    result->final_erase_generation = block->erase_generation;
}

static enum fwlab_nfc_api_result media_program(
    void *opaque, const struct fwlab_nfc_ppa *ppa, const uint8_t *main,
    uint32_t main_length, const uint8_t *oob, uint32_t oob_length,
    uint32_t applied_main_bytes, uint32_t applied_oob_bytes,
    uint8_t integrity, struct fwlab_nand_media_result *result)
{
    struct fwlab_file_nand_v0 *media = opaque;
    struct fwlab_nand_page_info old_page;
    struct fwlab_nand_block_info old_block;
    struct file_nand_transaction transaction;
    uint8_t old_main[4096];
    uint8_t old_oob[128];
    uint8_t new_main[4096];
    uint8_t new_oob[128];
    uint32_t index;
    enum fwlab_nfc_api_result status;

    if (media == NULL || !file_nand_ppa_valid(ppa) || main == NULL ||
        oob == NULL || result == NULL || main_length != FILE_NAND_MAIN_BYTES ||
        oob_length != FILE_NAND_OOB_BYTES ||
        applied_main_bytes > main_length || applied_oob_bytes > oob_length ||
        (integrity != FWLAB_NFC_INTEGRITY_COMPLETE &&
         integrity != FWLAB_NFC_INTEGRITY_TORN)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    status = file_nand_read_selected_page(media, ppa, old_main, old_oob,
                                           &old_page, &old_block);
    if (status != FWLAB_NFC_API_OK) {
        return status;
    }
    result_no_effect(result, &old_block, FWLAB_NFC_REASON_NONE);
    if (old_block.health != FWLAB_NFC_BLOCK_GOOD) {
        result->reason = FWLAB_NFC_REASON_BAD_BLOCK;
        return FWLAB_NFC_API_OK;
    }
    if (old_block.erase_state != FWLAB_NAND_ERASE_CLEAN ||
        old_page.state != FWLAB_NAND_PAGE_ERASED ||
        old_page.program_count != 0) {
        result->reason = FWLAB_NFC_REASON_NOT_ERASED;
        return FWLAB_NFC_API_OK;
    }
    if (ppa->page != old_block.next_program_page) {
        result->reason = FWLAB_NFC_REASON_PROGRAM_ORDER;
        return FWLAB_NFC_API_OK;
    }
    if (applied_main_bytes == 0 && applied_oob_bytes == 0) {
        return FWLAB_NFC_API_OK;
    }
    {
        uint16_t linear = file_nand_linear_page(ppa);

        status = file_nand_prepare_mutation(media, &linear, 1, -1);
        if (status != FWLAB_NFC_API_OK) {
            return status;
        }
    }
    if (media->next_transaction_uid > FILE_NAND_MAX_PHYSICAL_TXNS ||
        media->physical_generation == UINT64_MAX ||
        old_block.next_program_page == UINT16_MAX) {
        return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    }
    memcpy(new_main, old_main, sizeof(new_main));
    memcpy(new_oob, old_oob, sizeof(new_oob));
    for (index = 0; index < applied_main_bytes; ++index) {
        new_main[index] &= main[index];
    }
    for (index = 0; index < applied_oob_bytes; ++index) {
        new_oob[index] &= oob[index];
    }
    memset(&transaction, 0, sizeof(transaction));
    transaction.operation_kind = FILE_NAND_MUTATION_PROGRAM;
    transaction.physical_outcome = FWLAB_NFC_PHYS_APPLIED;
    transaction.integrity = integrity;
    transaction.applied_main_bytes = applied_main_bytes;
    transaction.applied_oob_bytes = applied_oob_bytes;
    transaction.ppa = *ppa;
    transaction.transaction_uid = media->next_transaction_uid;
    transaction.old_generation = media->physical_generation;
    transaction.new_generation = media->physical_generation + 1u;
    transaction.base_erase_generation = old_block.erase_generation;
    transaction.final_erase_generation = old_block.erase_generation;
    transaction.payload_digest = file_nand_hash64(new_main, sizeof(new_main)) ^
        file_nand_hash64(new_oob, sizeof(new_oob));
    transaction.candidate_count = 1;
    status = write_page_candidate(media, &transaction, 0, ppa,
        integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
                applied_main_bytes == FILE_NAND_MAIN_BYTES &&
                applied_oob_bytes == FILE_NAND_OOB_BYTES ?
            FWLAB_NAND_PAGE_VALID : FWLAB_NAND_PAGE_TORN,
        1, old_block.erase_generation, new_main, new_oob);
    if (status != FWLAB_NFC_API_OK) {
        return status;
    }
    status = media->substrate.ops->barrier(media->substrate.context);
    if (status !=
            FWLAB_NFC_API_OK) {
        return status;
    }
    status = file_nand_commit_transaction(media, &transaction, result);
    if (status == FWLAB_NFC_API_OK) {
        result->block_health = old_block.health;
    }
    return status;
}

static enum fwlab_nfc_api_result media_erase(
    void *opaque, const struct fwlab_nfc_ppa *ppa, uint32_t applied_pages,
    uint8_t integrity, struct fwlab_nand_media_result *result)
{
    struct fwlab_file_nand_v0 *media = opaque;
    struct fwlab_nand_block_info old_block;
    struct fwlab_nand_block_info new_block;
    struct file_nand_transaction transaction;
    uint8_t erased_main[4096];
    uint8_t erased_oob[128];
    uint16_t page;
    enum fwlab_nfc_api_result status;

    if (media == NULL || !file_nand_ppa_valid(ppa) || ppa->page != 0 ||
        result == NULL || applied_pages > FILE_NAND_PAGES_PER_BLOCK ||
        (integrity != FWLAB_NFC_INTEGRITY_COMPLETE &&
         integrity != FWLAB_NFC_INTEGRITY_TORN)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    status = file_nand_read_selected_health(media, (uint8_t)ppa->block,
                                             &old_block);
    if (status != FWLAB_NFC_API_OK) {
        return status;
    }
    result_no_effect(result, &old_block, FWLAB_NFC_REASON_NONE);
    if (old_block.health != FWLAB_NFC_BLOCK_GOOD) {
        result->reason = FWLAB_NFC_REASON_BAD_BLOCK;
        return FWLAB_NFC_API_OK;
    }
    if (old_block.erase_attempt_count == UINT16_MAX ||
        (integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
         applied_pages == FILE_NAND_PAGES_PER_BLOCK &&
         (old_block.erase_generation == UINT16_MAX ||
          old_block.successful_erase_count == UINT16_MAX)) ||
        media->next_transaction_uid > FILE_NAND_MAX_PHYSICAL_TXNS ||
        media->physical_generation == UINT64_MAX) {
        return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    }
    if (applied_pages == 0) {
        return FWLAB_NFC_API_OK;
    }
    {
        uint16_t pages[FILE_NAND_PAGES_PER_BLOCK];

        for (page = 0; page < applied_pages; ++page) {
            pages[page] = (uint16_t)(ppa->block *
                                     FILE_NAND_PAGES_PER_BLOCK + page);
        }
        status = file_nand_prepare_mutation(media, pages,
            (uint16_t)applied_pages, ppa->block);
        if (status != FWLAB_NFC_API_OK) {
            return status;
        }
    }
    memset(&transaction, 0, sizeof(transaction));
    transaction.operation_kind = FILE_NAND_MUTATION_ERASE;
    transaction.physical_outcome = FWLAB_NFC_PHYS_APPLIED;
    transaction.integrity = integrity;
    transaction.applied_pages = (uint16_t)applied_pages;
    transaction.candidate_count = (uint16_t)applied_pages;
    transaction.ppa = *ppa;
    transaction.transaction_uid = media->next_transaction_uid;
    transaction.old_generation = media->physical_generation;
    transaction.new_generation = media->physical_generation + 1u;
    transaction.base_erase_generation = old_block.erase_generation;
    transaction.final_erase_generation = old_block.erase_generation;
    new_block = old_block;
    ++new_block.erase_attempt_count;
    if (integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
        applied_pages == FILE_NAND_PAGES_PER_BLOCK) {
        ++new_block.erase_generation;
        ++new_block.successful_erase_count;
        new_block.erase_state = FWLAB_NAND_ERASE_CLEAN;
        new_block.next_program_page = 0;
        transaction.final_erase_generation = new_block.erase_generation;
    } else {
        new_block.erase_state = FWLAB_NAND_ERASE_TORN;
    }
    memset(erased_main, 0xff, sizeof(erased_main));
    memset(erased_oob, 0xff, sizeof(erased_oob));
    for (page = 0; page < applied_pages; ++page) {
        struct fwlab_nfc_ppa page_ppa = *ppa;

        page_ppa.page = page;
        status = write_page_candidate(media, &transaction, page, &page_ppa,
            new_block.erase_state == FWLAB_NAND_ERASE_CLEAN ?
                FWLAB_NAND_PAGE_ERASED : FWLAB_NAND_PAGE_TORN,
            0, new_block.erase_generation, erased_main, erased_oob);
        if (status != FWLAB_NFC_API_OK) {
            return status;
        }
    }
    status = write_health_candidate(media, &transaction, (uint8_t)ppa->block,
                                    &new_block);
    if (status != FWLAB_NFC_API_OK ||
        (status = media->substrate.ops->barrier(media->substrate.context)) !=
            FWLAB_NFC_API_OK) {
        return status;
    }
    status = file_nand_commit_transaction(media, &transaction, result);
    if (status == FWLAB_NFC_API_OK) {
        result->block_health = new_block.health;
    }
    return status;
}

static enum fwlab_nfc_api_result media_mark_bad(
    void *opaque, const struct fwlab_nfc_ppa *ppa)
{
    struct fwlab_file_nand_v0 *media = opaque;
    struct fwlab_nand_block_info old_block;
    struct fwlab_nand_block_info new_block;
    struct file_nand_transaction transaction;
    struct fwlab_nand_media_result result;
    enum fwlab_nfc_api_result status;

    if (media == NULL || !file_nand_ppa_valid(ppa)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    status = file_nand_read_selected_health(media, (uint8_t)ppa->block,
                                             &old_block);
    if (status != FWLAB_NFC_API_OK ||
        old_block.health == FWLAB_NFC_BLOCK_RUNTIME_BAD) {
        return status;
    }
    memset(&transaction, 0, sizeof(transaction));
    transaction.operation_kind = FILE_NAND_MUTATION_MARK_BAD;
    transaction.physical_outcome = FWLAB_NFC_PHYS_APPLIED;
    transaction.integrity = FWLAB_NFC_INTEGRITY_COMPLETE;
    transaction.ppa = *ppa;
    transaction.ppa.page = 0;
    transaction.transaction_uid = media->next_transaction_uid;
    transaction.old_generation = media->physical_generation;
    transaction.new_generation = media->physical_generation + 1u;
    transaction.base_erase_generation = old_block.erase_generation;
    transaction.final_erase_generation = old_block.erase_generation;
    new_block = old_block;
    new_block.health = FWLAB_NFC_BLOCK_RUNTIME_BAD;
    status = file_nand_prepare_mutation(media, NULL, 0, ppa->block);
    if (status != FWLAB_NFC_API_OK) {
        return status;
    }
    status = write_health_candidate(media, &transaction, (uint8_t)ppa->block,
                                    &new_block);
    if (status != FWLAB_NFC_API_OK ||
        (status = media->substrate.ops->barrier(media->substrate.context)) !=
            FWLAB_NFC_API_OK) {
        return status;
    }
    return file_nand_commit_transaction(media, &transaction, &result);
}

static uint64_t media_hash(void *opaque)
{
    struct fwlab_file_nand_v0 *media = opaque;
    uint64_t hash = UINT64_C(1469598103934665603);
    uint8_t bytes[8192];
    uint16_t page;

    if (!file_nand_validate_live(media)) {
        return 0;
    }
    for (page = 0; page < FILE_NAND_PAGE_COUNT; ++page) {
        uint8_t slot = media->selected_page_slot[page];

        if (media->substrate.ops->read(media->substrate.context,
                file_nand_page_slot_offset(page, slot), bytes,
                sizeof(bytes)) != FWLAB_NFC_API_OK) {
            return 0;
        }
        hash ^= file_nand_hash64(bytes, sizeof(bytes));
        hash *= UINT64_C(1099511628211);
    }
    hash ^= media->physical_generation;
    return hash;
}

static const struct fwlab_nand_media_ops media_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_nand_media_ops),
    .reserved = 0,
    .read_page = media_read,
    .program = media_program,
    .erase = media_erase,
    .mark_runtime_bad = media_mark_bad,
    .hash = media_hash,
};

struct fwlab_nand_media fwlab_file_nand_v0_media(
    struct fwlab_file_nand_v0 *media)
{
    struct fwlab_nand_media result;

    result.ops = &media_ops;
    result.context = file_nand_validate_live(media) ? media : NULL;
    return result;
}
