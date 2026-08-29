/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_memory_media.h"

#include <string.h>

static int ppa_valid(const struct fwlab_nfc_ppa *ppa)
{
    return ppa != NULL && ppa->channel == 0 && ppa->lun == 0 &&
           ppa->plane == 0 && ppa->block < C34_BLOCKS &&
           ppa->page < C34_PAGES_PER_BLOCK && ppa->reserved == 0;
}

static uint64_t payload_digest(
    const uint8_t *main,
    uint32_t main_length,
    const uint8_t *oob,
    uint32_t oob_length
)
{
    uint64_t hash = c34_hash_bytes(UINT64_C(1469598103934665603),
                                   main, main_length);

    return c34_hash_bytes(hash, oob, oob_length);
}

void c34_memory_media_init(struct c34_memory_media *media)
{
    unsigned int index;

    memset(media, 0, sizeof(*media));
    memset(media->main, 0xff, sizeof(media->main));
    memset(media->oob, 0xff, sizeof(media->oob));
    for (index = 0; index < C34_TOTAL_PAGES; ++index) {
        media->page[index].version = FWLAB_NFC_CONTRACT_VERSION;
        media->page[index].size = sizeof(media->page[index]);
        media->page[index].state = FWLAB_NAND_PAGE_ERASED;
    }
    for (index = 0; index < C34_BLOCKS; ++index) {
        media->block[index].version = FWLAB_NFC_CONTRACT_VERSION;
        media->block[index].size = sizeof(media->block[index]);
        media->block[index].health = FWLAB_NFC_BLOCK_GOOD;
        media->block[index].erase_state = FWLAB_NAND_ERASE_CLEAN;
    }
}

static enum fwlab_nfc_api_result memory_read(
    void *context,
    const struct fwlab_nfc_ppa *ppa,
    uint8_t *main,
    uint32_t main_length,
    uint8_t *oob,
    uint32_t oob_length,
    struct fwlab_nand_page_info *page,
    struct fwlab_nand_block_info *block
)
{
    struct c34_memory_media *media = context;
    uint32_t page_index;

    if (media == NULL || !ppa_valid(ppa) || main == NULL || oob == NULL ||
        page == NULL || block == NULL || main_length != C34_MAIN_BYTES ||
        oob_length != C34_OOB_BYTES) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    page_index = c34_page_index(ppa);
    memcpy(main, media->main[page_index], C34_MAIN_BYTES);
    memcpy(oob, media->oob[page_index], C34_OOB_BYTES);
    *page = media->page[page_index];
    *block = media->block[ppa->block];
    return FWLAB_NFC_API_OK;
}

static void result_init(
    const struct c34_memory_media *media,
    uint16_t block,
    struct fwlab_nand_media_result *result
)
{
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_NFC_CONTRACT_VERSION;
    result->size = sizeof(*result);
    result->physical_outcome = FWLAB_NFC_PHYS_NO_EFFECT;
    result->integrity = FWLAB_NFC_INTEGRITY_NOT_APPLICABLE;
    result->block_health = media->block[block].health;
    result->base_erase_generation =
        media->block[block].erase_generation;
    result->final_erase_generation =
        media->block[block].erase_generation;
}

static int binding_matches(
    const struct c34_memory_media *media,
    const struct fwlab_nfc_ppa *ppa,
    uint8_t kind,
    uint64_t digest
)
{
    return media->binding_used &&
           c34_ppa_equal(&media->binding.ppa, ppa) &&
           media->binding.operation_kind == kind &&
           media->binding.payload_digest == digest;
}

static void make_receipt(
    struct c34_memory_media *media,
    const struct fwlab_nand_media_result *result,
    uint8_t kind
)
{
    memset(&media->receipt, 0, sizeof(media->receipt));
    media->receipt.version = C34_PHYSICAL_TXN_VERSION;
    media->receipt.size = sizeof(media->receipt);
    media->receipt.physical_op_id = media->binding.physical_op_id;
    media->receipt.commit_sequence = media->binding.commit_sequence;
    media->receipt.inner = media->binding.inner;
    media->receipt.ppa = media->binding.ppa;
    media->receipt.payload_digest = media->binding.payload_digest;
    media->receipt.physical_generation = ++media->physical_generation;
    media->receipt.applied_main_bytes = result->applied_main_bytes;
    media->receipt.applied_oob_bytes = result->applied_oob_bytes;
    media->receipt.applied_pages = result->applied_pages;
    media->receipt.base_erase_generation =
        result->base_erase_generation;
    media->receipt.final_erase_generation =
        result->final_erase_generation;
    media->receipt.operation_kind = kind;
    media->receipt.physical_outcome = result->physical_outcome;
    media->receipt.integrity = result->integrity;
    media->receipt.applied_region_mask = result->applied_region_mask;
    media->receipt.committed = 1;
    media->receipt_used = 1;
    media->binding_used = 0;
}

static enum fwlab_nfc_api_result memory_program(
    void *context,
    const struct fwlab_nfc_ppa *ppa,
    const uint8_t *main,
    uint32_t main_length,
    const uint8_t *oob,
    uint32_t oob_length,
    uint32_t applied_main_bytes,
    uint32_t applied_oob_bytes,
    uint8_t integrity,
    struct fwlab_nand_media_result *result
)
{
    struct c34_memory_media *media = context;
    uint32_t index;
    uint32_t offset;
    uint64_t digest;

    if (media == NULL || !ppa_valid(ppa) || main == NULL || oob == NULL ||
        result == NULL || main_length != C34_MAIN_BYTES ||
        oob_length != C34_OOB_BYTES || applied_main_bytes > main_length ||
        applied_oob_bytes > oob_length ||
        (integrity != FWLAB_NFC_INTEGRITY_COMPLETE &&
         integrity != FWLAB_NFC_INTEGRITY_TORN)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    digest = payload_digest(main, main_length, oob, oob_length);
    if (!binding_matches(media, ppa, FWLAB_NFC_PROGRAM_EXECUTE, digest)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    index = c34_page_index(ppa);
    result_init(media, ppa->block, result);
    if (media->block[ppa->block].health != FWLAB_NFC_BLOCK_GOOD ||
        media->block[ppa->block].erase_state != FWLAB_NAND_ERASE_CLEAN ||
        media->page[index].state != FWLAB_NAND_PAGE_ERASED ||
        media->page[index].program_count != 0 ||
        media->block[ppa->block].next_program_page != ppa->page) {
        result->reason = FWLAB_NFC_REASON_NOT_ERASED;
        make_receipt(media, result, FWLAB_NFC_PROGRAM_EXECUTE);
        return FWLAB_NFC_API_OK;
    }
    for (offset = 0; offset < applied_main_bytes; ++offset) {
        media->main[index][offset] &= main[offset];
    }
    for (offset = 0; offset < applied_oob_bytes; ++offset) {
        media->oob[index][offset] &= oob[offset];
    }
    media->page[index].program_count = 1;
    media->page[index].erase_generation_seen =
        media->block[ppa->block].erase_generation;
    media->page[index].state = integrity == FWLAB_NFC_INTEGRITY_COMPLETE ?
        FWLAB_NAND_PAGE_VALID : FWLAB_NAND_PAGE_TORN;
    media->block[ppa->block].next_program_page = (uint16_t)(ppa->page + 1u);
    result->physical_outcome = FWLAB_NFC_PHYS_APPLIED;
    result->integrity = integrity;
    result->applied_main_bytes = applied_main_bytes;
    result->applied_oob_bytes = applied_oob_bytes;
    result->applied_region_mask = applied_main_bytes != 0 ?
        FWLAB_NFC_REGION_MAIN : 0;
    if (applied_oob_bytes != 0) {
        result->applied_region_mask |= FWLAB_NFC_REGION_OOB;
    }
    make_receipt(media, result, FWLAB_NFC_PROGRAM_EXECUTE);
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result memory_erase(
    void *context,
    const struct fwlab_nfc_ppa *ppa,
    uint32_t applied_pages,
    uint8_t integrity,
    struct fwlab_nand_media_result *result
)
{
    struct c34_memory_media *media = context;
    uint16_t page;

    if (media == NULL || !ppa_valid(ppa) || ppa->page != 0 ||
        result == NULL || applied_pages > C34_PAGES_PER_BLOCK ||
        (integrity != FWLAB_NFC_INTEGRITY_COMPLETE &&
         integrity != FWLAB_NFC_INTEGRITY_TORN) ||
        !binding_matches(media, ppa, FWLAB_NFC_ERASE, 0)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    result_init(media, ppa->block, result);
    ++media->block[ppa->block].erase_attempt_count;
    for (page = 0; page < applied_pages; ++page) {
        uint32_t index = (uint32_t)ppa->block * C34_PAGES_PER_BLOCK + page;

        memset(media->main[index], 0xff, C34_MAIN_BYTES);
        memset(media->oob[index], 0xff, C34_OOB_BYTES);
        media->page[index].state = FWLAB_NAND_PAGE_ERASED;
        media->page[index].program_count = 0;
        media->page[index].erase_generation_seen =
            (uint16_t)(media->block[ppa->block].erase_generation + 1u);
    }
    media->block[ppa->block].erase_state =
        integrity == FWLAB_NFC_INTEGRITY_COMPLETE ?
            FWLAB_NAND_ERASE_CLEAN : FWLAB_NAND_ERASE_TORN;
    if (integrity == FWLAB_NFC_INTEGRITY_COMPLETE) {
        ++media->block[ppa->block].erase_generation;
        ++media->block[ppa->block].successful_erase_count;
        media->block[ppa->block].next_program_page = 0;
    }
    result->physical_outcome = FWLAB_NFC_PHYS_APPLIED;
    result->integrity = integrity;
    result->applied_pages = applied_pages;
    result->applied_region_mask = FWLAB_NFC_REGION_MASK;
    result->final_erase_generation =
        media->block[ppa->block].erase_generation;
    make_receipt(media, result, FWLAB_NFC_ERASE);
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result memory_mark_bad(
    void *context,
    const struct fwlab_nfc_ppa *ppa
)
{
    struct c34_memory_media *media = context;
    struct fwlab_nand_media_result result;

    if (media == NULL || !ppa_valid(ppa) || !media->binding_used ||
        !c34_ppa_equal(&media->binding.ppa, ppa)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    result_init(media, ppa->block, &result);
    media->block[ppa->block].health = FWLAB_NFC_BLOCK_RUNTIME_BAD;
    result.physical_outcome = FWLAB_NFC_PHYS_APPLIED;
    result.integrity = FWLAB_NFC_INTEGRITY_COMPLETE;
    result.block_health = FWLAB_NFC_BLOCK_RUNTIME_BAD;
    make_receipt(media, &result, media->binding.operation_kind);
    return FWLAB_NFC_API_OK;
}

static uint64_t memory_hash(void *context)
{
    struct c34_memory_media *media = context;
    uint64_t hash = UINT64_C(1469598103934665603);

    hash = c34_hash_bytes(hash, &media->main[0][0], sizeof(media->main));
    hash = c34_hash_bytes(hash, &media->oob[0][0], sizeof(media->oob));
    hash = c34_hash_bytes(hash, (const uint8_t *)media->page,
                          sizeof(media->page));
    return c34_hash_bytes(hash, (const uint8_t *)media->block,
                          sizeof(media->block));
}

static const struct fwlab_nand_media_ops memory_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_nand_media_ops),
    .reserved = 0,
    .read_page = memory_read,
    .program = memory_program,
    .erase = memory_erase,
    .mark_runtime_bad = memory_mark_bad,
    .hash = memory_hash,
};

static enum c34_physical_txn_result txn_bind(
    void *context,
    const struct c34_physical_binding *binding
)
{
    struct c34_memory_media *media = context;

    if (media == NULL || binding == NULL ||
        binding->version != C34_PHYSICAL_TXN_VERSION ||
        binding->size != sizeof(*binding) || binding->reserved0 != 0 ||
        !ppa_valid(&binding->ppa) || media->binding_used ||
        media->receipt_used) {
        return media != NULL && (media->binding_used || media->receipt_used) ?
            C34_PHYSICAL_TXN_BUSY : C34_PHYSICAL_TXN_INVALID;
    }
    media->binding = *binding;
    media->binding_used = 1;
    return C34_PHYSICAL_TXN_OK;
}

static int token_equal(
    const struct fwlab_nfc_operation_token *left,
    const struct fwlab_nfc_operation_token *right
)
{
    return left->instance_nonce == right->instance_nonce &&
           left->operation_uid == right->operation_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->generation == right->generation;
}

static enum c34_physical_txn_result txn_abandon(
    void *context,
    const struct fwlab_nfc_operation_token *inner
)
{
    struct c34_memory_media *media = context;

    if (media == NULL || inner == NULL || !media->binding_used ||
        !token_equal(&media->binding.inner, inner)) {
        return C34_PHYSICAL_TXN_NOT_FOUND;
    }
    memset(&media->binding, 0, sizeof(media->binding));
    media->binding_used = 0;
    return C34_PHYSICAL_TXN_OK;
}

static enum c34_physical_txn_result txn_receipt(
    void *context,
    const struct fwlab_nfc_operation_token *inner,
    struct c34_physical_receipt *receipt
)
{
    struct c34_memory_media *media = context;

    if (media == NULL || inner == NULL || receipt == NULL ||
        !media->receipt_used ||
        !token_equal(&media->receipt.inner, inner)) {
        return C34_PHYSICAL_TXN_NOT_FOUND;
    }
    *receipt = media->receipt;
    memset(&media->receipt, 0, sizeof(media->receipt));
    media->receipt_used = 0;
    return C34_PHYSICAL_TXN_OK;
}

static enum c34_physical_txn_result txn_quiescent(
    void *context,
    bool *quiescent
)
{
    struct c34_memory_media *media = context;

    if (media == NULL || quiescent == NULL) {
        return C34_PHYSICAL_TXN_INVALID;
    }
    *quiescent = !media->binding_used && !media->receipt_used;
    return C34_PHYSICAL_TXN_OK;
}

static const struct c34_physical_txn_ops txn_ops = {
    .version = C34_PHYSICAL_TXN_VERSION,
    .size = sizeof(struct c34_physical_txn_ops),
    .reserved = 0,
    .bind = txn_bind,
    .abandon = txn_abandon,
    .receipt = txn_receipt,
    .quiescent = txn_quiescent,
};

struct fwlab_nand_media c34_memory_media_provider(
    struct c34_memory_media *media
)
{
    struct fwlab_nand_media provider;

    provider.ops = &memory_ops;
    provider.context = media;
    return provider;
}

struct c34_physical_txn_provider c34_memory_txn_provider(
    struct c34_memory_media *media
)
{
    struct c34_physical_txn_provider provider;

    provider.ops = &txn_ops;
    provider.context = media;
    return provider;
}

enum c34_result c34_memory_media_put_record(
    struct c34_memory_media *media,
    const struct fwlab_nfc_ppa *ppa,
    const struct c34_record *record
)
{
    uint8_t main[C34_MAIN_BYTES];
    uint8_t oob[C34_OOB_BYTES];
    uint32_t index;

    if (media == NULL || !ppa_valid(ppa) || record == NULL ||
        c34_record_encode(record, main, oob) != C34_OK) {
        return C34_INVALID_CONTRACT;
    }
    index = c34_page_index(ppa);
    if (media->page[index].state != FWLAB_NAND_PAGE_ERASED ||
        media->block[ppa->block].next_program_page != ppa->page) {
        return C34_WRONG_STATE;
    }
    memcpy(media->main[index], main, sizeof(main));
    memcpy(media->oob[index], oob, sizeof(oob));
    media->page[index].state = FWLAB_NAND_PAGE_VALID;
    media->page[index].program_count = 1;
    media->page[index].erase_generation_seen =
        media->block[ppa->block].erase_generation;
    media->block[ppa->block].next_program_page = (uint16_t)(ppa->page + 1u);
    return C34_OK;
}
