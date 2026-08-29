/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_file_internal.h"

#include <string.h>

static int ppa_valid(const struct fwlab_nfc_ppa *ppa)
{
    return ppa != NULL && ppa->channel == 0 && ppa->lun == 0 &&
           ppa->plane == 0 && ppa->block < C34F_BLOCKS &&
           ppa->page < C34F_PAGES_PER_BLOCK && ppa->reserved == 0;
}

static uint8_t page_index(const struct fwlab_nfc_ppa *ppa)
{
    return (uint8_t)(ppa->block * C34F_PAGES_PER_BLOCK + ppa->page);
}

static int ppa_equal(
    const struct fwlab_nfc_ppa *left,
    const struct fwlab_nfc_ppa *right
)
{
    return left->channel == right->channel && left->lun == right->lun &&
           left->plane == right->plane && left->block == right->block &&
           left->page == right->page && left->reserved == right->reserved;
}

static int inner_equal(
    const struct fwlab_nfc_operation_token *left,
    const struct fwlab_nfc_operation_token *right
)
{
    return left->instance_nonce == right->instance_nonce &&
           left->operation_uid == right->operation_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->generation == right->generation;
}

static uint64_t identity_hash(const struct c34_physical_binding *binding)
{
    uint8_t bytes[80];

    memset(bytes, 0, sizeof(bytes));
    c34f_put_u64(&bytes[0], binding->inner.instance_nonce);
    c34f_put_u64(&bytes[8], binding->inner.operation_uid);
    c34f_put_u32(&bytes[16], binding->inner.controller_epoch);
    c34f_put_u32(&bytes[20], binding->inner.generation);
    c34f_put_u64(&bytes[24], binding->outer.command.instance_nonce);
    c34f_put_u64(&bytes[32], binding->outer.command.command_uid);
    c34f_put_u32(&bytes[40], binding->outer.command.controller_epoch);
    c34f_put_u16(&bytes[44], binding->outer.command.slot);
    c34f_put_u16(&bytes[46], binding->outer.command.slot_generation);
    c34f_put_u64(&bytes[48], binding->mutation.word[0]);
    c34f_put_u64(&bytes[56], binding->mutation.word[1]);
    c34f_put_u64(&bytes[64], binding->payload_digest);
    bytes[72] = binding->operation_kind;
    return c34f_hash_bytes(UINT64_C(1469598103934665603), bytes,
                           sizeof(bytes));
}

static uint64_t program_digest(
    const uint8_t *main,
    const uint8_t *oob
)
{
    uint64_t hash = c34f_hash_bytes(UINT64_C(1469598103934665603), main,
                                    C34F_MAIN_BYTES);

    return c34f_hash_bytes(hash, oob, C34F_OOB_BYTES);
}

static unsigned int wal_remaining(const struct c34_file_media *media)
{
    return (C34F_WAL_SEGMENTS - media->active_segment) *
               C34F_WAL_RECORDS -
           media->active_record;
}

static int needs_checkpoint(
    const struct c34_file_media *media,
    const uint8_t *pages,
    uint8_t page_count,
    int health_block
)
{
    unsigned int index;

    if (wal_remaining(media) < 3) {
        return 1;
    }
    for (index = 0; index < page_count; ++index) {
        uint8_t page = pages[index];
        uint8_t next_slot = (uint8_t)(media->page_slot[page] ^ 1u);

        if (media->page_slot_lsn[page][next_slot] > media->covered_lsn) {
            return 1;
        }
    }
    if (health_block >= 0) {
        uint8_t next_slot =
            (uint8_t)(media->health_slot[health_block] ^ 1u);

        if (media->health_slot_lsn[health_block][next_slot] >
            media->covered_lsn) {
            return 1;
        }
    }
    return 0;
}

static void result_init(
    const struct c34_file_media *media,
    uint8_t block,
    struct fwlab_nand_media_result *result
)
{
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_NFC_CONTRACT_VERSION;
    result->size = sizeof(*result);
    result->physical_outcome = FWLAB_NFC_PHYS_NO_EFFECT;
    result->integrity = FWLAB_NFC_INTEGRITY_NOT_APPLICABLE;
    result->block_health = media->block[block].health;
    result->base_erase_generation = media->block[block].erase_generation;
    result->final_erase_generation = media->block[block].erase_generation;
}

static enum c34_file_result stage_candidates(
    struct c34_file_media *media,
    struct c34f_delta *delta,
    const struct c34f_page candidate[C34F_PAGES_PER_BLOCK],
    const struct fwlab_nand_block_info *health,
    uint64_t source_op,
    uint64_t begin_lsn
)
{
    unsigned int index;
    enum c34_file_result result;

    for (index = 0; index < delta->page_count; ++index) {
        uint8_t page = delta->page_index[index];
        uint8_t slot = (uint8_t)(media->page_slot[page] ^ 1u);

        delta->page_slot[index] = slot;
        result = c34f_write_page_candidate(
            media, page, slot, source_op, begin_lsn, &candidate[index],
            &delta->page_hash[index]);
        if (result != C34_FILE_OK) {
            return result;
        }
    }
    if (delta->health_valid) {
        uint8_t block = delta->health_block;
        uint8_t slot = (uint8_t)(media->health_slot[block] ^ 1u);

        delta->health_slot = slot;
        result = c34f_write_health_candidate(
            media, block, slot, source_op, begin_lsn, health,
            &delta->health_hash);
        if (result != C34_FILE_OK) {
            return result;
        }
    }
    return c34f_barrier(media);
}

static void make_receipt(
    struct c34_file_media *media,
    const struct c34f_delta *delta
)
{
    const struct c34_physical_binding *binding = &media->binding;

    memset(&media->receipt, 0, sizeof(media->receipt));
    media->receipt.version = C34_PHYSICAL_TXN_VERSION;
    media->receipt.size = sizeof(media->receipt);
    media->receipt.physical_op_id = binding->physical_op_id;
    media->receipt.commit_sequence = binding->commit_sequence;
    media->receipt.inner = binding->inner;
    media->receipt.ppa = binding->ppa;
    media->receipt.payload_digest = binding->payload_digest;
    media->receipt.physical_generation = media->physical_generation;
    media->receipt.applied_main_bytes = delta->applied_main_bytes;
    media->receipt.applied_oob_bytes = delta->applied_oob_bytes;
    media->receipt.applied_pages = delta->applied_pages;
    media->receipt.base_erase_generation = delta->base_erase_generation;
    media->receipt.final_erase_generation = delta->final_erase_generation;
    media->receipt.operation_kind = binding->operation_kind;
    media->receipt.physical_outcome = delta->physical_outcome;
    media->receipt.integrity = delta->integrity;
    media->receipt.applied_region_mask = delta->applied_region_mask;
    media->receipt.committed = 1;
    media->receipt_used = 1;
    media->binding_used = 0;
}

static enum c34_file_result commit_transaction(
    struct c34_file_media *media,
    struct c34f_delta *delta,
    const struct c34f_page candidate[C34F_PAGES_PER_BLOCK],
    const struct fwlab_nand_block_info *health
)
{
    struct c34f_wal_record begin;
    struct c34f_wal_record applied;
    struct c34f_wal_record commit;
    uint64_t hash;
    enum c34_file_result result;
    uint8_t pages[C34F_PAGES_PER_BLOCK];
    unsigned int index;

    for (index = 0; index < delta->page_count; ++index) {
        pages[index] = delta->page_index[index];
    }
    if (needs_checkpoint(
            media, pages, delta->page_count,
            delta->health_valid ? delta->health_block : -1)) {
        result = c34_file_physical_checkpoint(media);
        if (result != C34_FILE_OK) {
            return result;
        }
    }
    result = stage_candidates(
        media, delta, candidate, health, media->next_op_id, media->next_lsn);
    if (result != C34_FILE_OK) {
        return result;
    }
    memset(&begin, 0, sizeof(begin));
    begin.type = C34F_WAL_BEGIN;
    begin.op_id = media->next_op_id;
    begin.commit_sequence = media->next_commit_sequence;
    begin.old_physical_generation = media->physical_generation;
    begin.new_physical_generation = media->physical_generation + 1u;
    begin.inner = media->binding.inner;
    begin.ppa = media->binding.ppa;
    begin.payload_digest = media->binding.payload_digest;
    begin.identity_hash = identity_hash(&media->binding);
    begin.delta = *delta;
    result = c34f_append_wal(media, &begin, &hash);
    if (result != C34_FILE_OK ||
        (result = c34f_barrier(media)) != C34_FILE_OK) {
        return result;
    }
    applied = begin;
    applied.type = delta->physical_outcome == FWLAB_NFC_PHYS_APPLIED ?
        C34F_WAL_A_APPLIED : C34F_WAL_A_NO_EFFECT;
    applied.begin_lsn = begin.lsn;
    result = c34f_append_wal(media, &applied, &hash);
    if (result != C34_FILE_OK ||
        (result = c34f_barrier(media)) != C34_FILE_OK) {
        return result;
    }
    commit = begin;
    commit.type = C34F_WAL_C_COMMIT;
    commit.begin_lsn = begin.lsn;
    commit.applied_lsn = applied.lsn;
    result = c34f_append_wal(media, &commit, &hash);
    if (result != C34_FILE_OK ||
        (result = c34f_barrier(media)) != C34_FILE_OK ||
        (result = c34f_apply_commit(media, &commit)) != C34_FILE_OK) {
        return result;
    }
    ++media->next_op_id;
    ++media->next_commit_sequence;
    make_receipt(media, delta);
    return C34_FILE_OK;
}

static enum fwlab_nfc_api_result file_read_page(
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
    struct c34_file_media *media = context;
    uint8_t index;

    if (media == NULL || media->magic != C34F_MAGIC || media->stopped ||
        !ppa_valid(ppa) || main == NULL || oob == NULL || page == NULL ||
        block == NULL || main_length != C34F_MAIN_BYTES ||
        oob_length != C34F_OOB_BYTES) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    index = page_index(ppa);
    memcpy(main, media->page[index].main, C34F_MAIN_BYTES);
    memcpy(oob, media->page[index].oob, C34F_OOB_BYTES);
    *page = media->page[index].info;
    *block = media->block[ppa->block];
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result file_program(
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
    struct c34_file_media *media = context;
    struct c34f_page candidate[C34F_PAGES_PER_BLOCK];
    struct fwlab_nand_block_info health;
    struct c34f_delta delta;
    uint8_t index;
    uint32_t offset;
    enum c34_file_result committed;

    if (media == NULL || media->magic != C34F_MAGIC || !ppa_valid(ppa) ||
        main == NULL || oob == NULL || result == NULL ||
        main_length != C34F_MAIN_BYTES || oob_length != C34F_OOB_BYTES ||
        applied_main_bytes > main_length || applied_oob_bytes > oob_length ||
        !media->binding_used ||
        media->binding.operation_kind != FWLAB_NFC_PROGRAM_EXECUTE ||
        !ppa_equal(&media->binding.ppa, ppa) ||
        media->binding.payload_digest != program_digest(main, oob) ||
        media->binding.main_length != main_length ||
        media->binding.oob_length != oob_length ||
        (integrity != FWLAB_NFC_INTEGRITY_COMPLETE &&
         integrity != FWLAB_NFC_INTEGRITY_TORN)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(candidate, 0, sizeof(candidate));
    memset(&delta, 0, sizeof(delta));
    index = page_index(ppa);
    result_init(media, (uint8_t)ppa->block, result);
    delta.operation_kind = FWLAB_NFC_PROGRAM_EXECUTE;
    delta.base_erase_generation = media->block[ppa->block].erase_generation;
    delta.final_erase_generation = delta.base_erase_generation;
    if (media->block[ppa->block].health == FWLAB_NFC_BLOCK_GOOD &&
        media->block[ppa->block].erase_state == FWLAB_NAND_ERASE_CLEAN &&
        media->page[index].info.state == FWLAB_NAND_PAGE_ERASED &&
        media->page[index].info.program_count == 0 &&
        media->block[ppa->block].next_program_page == ppa->page) {
        candidate[0] = media->page[index];
        for (offset = 0; offset < applied_main_bytes; ++offset) {
            candidate[0].main[offset] &= main[offset];
        }
        for (offset = 0; offset < applied_oob_bytes; ++offset) {
            candidate[0].oob[offset] &= oob[offset];
        }
        candidate[0].info.program_count = 1;
        candidate[0].info.erase_generation_seen =
            media->block[ppa->block].erase_generation;
        candidate[0].info.state =
            integrity == FWLAB_NFC_INTEGRITY_COMPLETE ?
                FWLAB_NAND_PAGE_VALID : FWLAB_NAND_PAGE_TORN;
        health = media->block[ppa->block];
        health.next_program_page = (uint16_t)(ppa->page + 1u);
        delta.page_count = 1;
        delta.page_index[0] = index;
        delta.health_valid = 1;
        delta.health_block = (uint8_t)ppa->block;
        delta.physical_outcome = FWLAB_NFC_PHYS_APPLIED;
        delta.integrity = integrity;
        delta.applied_main_bytes = applied_main_bytes;
        delta.applied_oob_bytes = applied_oob_bytes;
        delta.applied_region_mask = applied_main_bytes != 0 ?
            FWLAB_NFC_REGION_MAIN : 0;
        if (applied_oob_bytes != 0) {
            delta.applied_region_mask |= FWLAB_NFC_REGION_OOB;
        }
    } else {
        memset(&health, 0, sizeof(health));
        delta.physical_outcome = FWLAB_NFC_PHYS_NO_EFFECT;
        delta.integrity = FWLAB_NFC_INTEGRITY_NOT_APPLICABLE;
        result->reason = FWLAB_NFC_REASON_NOT_ERASED;
    }
    committed = commit_transaction(media, &delta, candidate, &health);
    if (committed != C34_FILE_OK) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    result->physical_outcome = delta.physical_outcome;
    result->integrity = delta.integrity;
    result->applied_main_bytes = delta.applied_main_bytes;
    result->applied_oob_bytes = delta.applied_oob_bytes;
    result->applied_region_mask = delta.applied_region_mask;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result file_erase(
    void *context,
    const struct fwlab_nfc_ppa *ppa,
    uint32_t applied_pages,
    uint8_t integrity,
    struct fwlab_nand_media_result *result
)
{
    struct c34_file_media *media = context;
    struct c34f_page candidate[C34F_PAGES_PER_BLOCK];
    struct fwlab_nand_block_info health;
    struct c34f_delta delta;
    uint32_t page;
    enum c34_file_result committed;

    if (media == NULL || media->magic != C34F_MAGIC || !ppa_valid(ppa) ||
        ppa->page != 0 || result == NULL ||
        applied_pages > C34F_PAGES_PER_BLOCK || !media->binding_used ||
        media->binding.operation_kind != FWLAB_NFC_ERASE ||
        !ppa_equal(&media->binding.ppa, ppa) ||
        (integrity != FWLAB_NFC_INTEGRITY_COMPLETE &&
         integrity != FWLAB_NFC_INTEGRITY_TORN)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(candidate, 0, sizeof(candidate));
    memset(&delta, 0, sizeof(delta));
    result_init(media, (uint8_t)ppa->block, result);
    health = media->block[ppa->block];
    ++health.erase_attempt_count;
    for (page = 0; page < applied_pages; ++page) {
        uint8_t index = (uint8_t)(ppa->block * C34F_PAGES_PER_BLOCK + page);

        candidate[page] = media->page[index];
        memset(candidate[page].main, 0xff, C34F_MAIN_BYTES);
        memset(candidate[page].oob, 0xff, C34F_OOB_BYTES);
        candidate[page].info.state = FWLAB_NAND_PAGE_ERASED;
        candidate[page].info.program_count = 0;
        candidate[page].info.erase_generation_seen =
            (uint16_t)(health.erase_generation + 1u);
        delta.page_index[page] = index;
    }
    health.erase_state = integrity == FWLAB_NFC_INTEGRITY_COMPLETE ?
        FWLAB_NAND_ERASE_CLEAN : FWLAB_NAND_ERASE_TORN;
    if (integrity == FWLAB_NFC_INTEGRITY_COMPLETE) {
        ++health.erase_generation;
        ++health.successful_erase_count;
        health.next_program_page = 0;
    }
    delta.page_count = (uint8_t)applied_pages;
    delta.health_valid = 1;
    delta.health_block = (uint8_t)ppa->block;
    delta.operation_kind = FWLAB_NFC_ERASE;
    delta.physical_outcome = FWLAB_NFC_PHYS_APPLIED;
    delta.integrity = integrity;
    delta.applied_region_mask = FWLAB_NFC_REGION_MASK;
    delta.applied_pages = applied_pages;
    delta.base_erase_generation = media->block[ppa->block].erase_generation;
    delta.final_erase_generation = health.erase_generation;
    committed = commit_transaction(media, &delta, candidate, &health);
    if (committed != C34_FILE_OK) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    result->physical_outcome = delta.physical_outcome;
    result->integrity = delta.integrity;
    result->applied_region_mask = delta.applied_region_mask;
    result->applied_pages = applied_pages;
    result->final_erase_generation = health.erase_generation;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result file_mark_bad(
    void *context,
    const struct fwlab_nfc_ppa *ppa
)
{
    struct c34_file_media *media = context;
    struct c34f_page candidate[C34F_PAGES_PER_BLOCK];
    struct fwlab_nand_block_info health;
    struct c34f_delta delta;

    if (media == NULL || media->magic != C34F_MAGIC || !ppa_valid(ppa) ||
        !media->binding_used || !ppa_equal(&media->binding.ppa, ppa)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(candidate, 0, sizeof(candidate));
    memset(&delta, 0, sizeof(delta));
    health = media->block[ppa->block];
    health.health = FWLAB_NFC_BLOCK_RUNTIME_BAD;
    delta.health_valid = 1;
    delta.health_block = (uint8_t)ppa->block;
    delta.operation_kind = media->binding.operation_kind;
    delta.physical_outcome = FWLAB_NFC_PHYS_APPLIED;
    delta.integrity = FWLAB_NFC_INTEGRITY_COMPLETE;
    delta.base_erase_generation = health.erase_generation;
    delta.final_erase_generation = health.erase_generation;
    return commit_transaction(media, &delta, candidate, &health) ==
                   C34_FILE_OK ?
               FWLAB_NFC_API_OK : FWLAB_NFC_API_INVARIANT_FAILURE;
}

static uint64_t file_media_hash(void *context)
{
    return c34_file_physical_hash(context);
}

static const struct fwlab_nand_media_ops media_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_nand_media_ops),
    .reserved = 0,
    .read_page = file_read_page,
    .program = file_program,
    .erase = file_erase,
    .mark_runtime_bad = file_mark_bad,
    .hash = file_media_hash,
};

static int binding_valid(const struct c34_physical_binding *binding)
{
    unsigned int index;

    if (binding == NULL ||
        binding->version != C34_PHYSICAL_TXN_VERSION ||
        binding->size != sizeof(*binding) || binding->reserved0 != 0 ||
        binding->physical_op_id == 0 || binding->commit_sequence == 0 ||
        binding->inner.instance_nonce == 0 ||
        binding->inner.operation_uid == 0 ||
        binding->inner.controller_epoch == 0 ||
        binding->inner.generation == 0 || !ppa_valid(&binding->ppa) ||
        (binding->operation_kind != FWLAB_NFC_PROGRAM_EXECUTE &&
         binding->operation_kind != FWLAB_NFC_ERASE)) {
        return 0;
    }
    for (index = 0; index < sizeof(binding->reserved1); ++index) {
        if (binding->reserved1[index] != 0) {
            return 0;
        }
    }
    return binding->operation_kind == FWLAB_NFC_PROGRAM_EXECUTE ?
        binding->main_length == C34F_MAIN_BYTES &&
            binding->oob_length == C34F_OOB_BYTES :
        binding->main_length == 0 && binding->oob_length == 0 &&
            binding->payload_digest == 0 && binding->ppa.page == 0;
}

static enum c34_physical_txn_result txn_bind(
    void *context,
    const struct c34_physical_binding *binding
)
{
    struct c34_file_media *media = context;

    if (media == NULL || media->magic != C34F_MAGIC || media->stopped ||
        !binding_valid(binding)) {
        return C34_PHYSICAL_TXN_INVALID;
    }
    if (media->binding_used || media->receipt_used) {
        return C34_PHYSICAL_TXN_BUSY;
    }
    media->binding = *binding;
    media->binding_used = 1;
    return C34_PHYSICAL_TXN_OK;
}

static enum c34_physical_txn_result txn_abandon(
    void *context,
    const struct fwlab_nfc_operation_token *inner
)
{
    struct c34_file_media *media = context;

    if (media == NULL || inner == NULL || !media->binding_used ||
        !inner_equal(&media->binding.inner, inner)) {
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
    struct c34_file_media *media = context;

    if (media == NULL || inner == NULL || receipt == NULL ||
        !media->receipt_used ||
        !inner_equal(&media->receipt.inner, inner)) {
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
    struct c34_file_media *media = context;

    if (media == NULL || quiescent == NULL || media->magic != C34F_MAGIC) {
        return C34_PHYSICAL_TXN_INVALID;
    }
    *quiescent = !media->binding_used && !media->receipt_used &&
                 !media->stopped;
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

struct fwlab_nand_media c34_file_nand_media(
    struct c34_file_media *media
)
{
    struct fwlab_nand_media provider;

    provider.ops = media != NULL && media->magic == C34F_MAGIC ?
        &media_ops : NULL;
    provider.context = provider.ops != NULL ? media : NULL;
    return provider;
}

struct c34_physical_txn_provider c34_file_txn_provider(
    struct c34_file_media *media
)
{
    struct c34_physical_txn_provider provider;

    provider.ops = media != NULL && media->magic == C34F_MAGIC ?
        &txn_ops : NULL;
    provider.context = provider.ops != NULL ? media : NULL;
    return provider;
}
