/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "file_nand_internal.h"

#include <stdalign.h>
#include <string.h>

struct file_nand_wal_record {
    uint8_t record_kind;
    struct file_nand_transaction transaction;
    uint64_t lsn;
    uint64_t begin_lsn;
    uint64_t applied_lsn;
    uint64_t previous_hash;
    uint64_t record_hash;
};

static int substrate_valid(const struct file_nand_substrate *substrate)
{
    return substrate != NULL && substrate->ops != NULL &&
           substrate->ops->version == FWLAB_FILE_NAND_V0_VERSION &&
           substrate->ops->size == sizeof(*substrate->ops) &&
           substrate->ops->reserved == 0 && substrate->context != NULL &&
           substrate->ops->size_get != NULL &&
           substrate->ops->resize != NULL && substrate->ops->read != NULL &&
           substrate->ops->write != NULL &&
           substrate->ops->barrier != NULL &&
           substrate->ops->identity != NULL &&
           substrate->ops->close != NULL;
}

static enum fwlab_nfc_api_result verify_holder(
    struct fwlab_file_nand_v0 *media)
{
    struct file_nand_identity identity;
    enum fwlab_nfc_api_result result;

    if (media == NULL || media->magic != FILE_NAND_MAGIC ||
        !substrate_valid(&media->substrate) || !media->initialized ||
        media->closed || media->quarantined) {
        return FWLAB_NFC_API_WRONG_STATE;
    }
    result = media->substrate.ops->identity(media->substrate.context,
                                            &identity);
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    if (identity.device != media->holder.device ||
        identity.inode != media->holder.inode) {
        media->quarantined = 1;
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result substrate_read(
    struct fwlab_file_nand_v0 *media, uint64_t offset, void *bytes,
    size_t size)
{
    if (offset > FWLAB_FILE_NAND_V0_IMAGE_BYTES ||
        size > FWLAB_FILE_NAND_V0_IMAGE_BYTES - offset) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    return media->substrate.ops->read(media->substrate.context, offset,
                                      bytes, size);
}

static enum fwlab_nfc_api_result substrate_write(
    struct fwlab_file_nand_v0 *media, uint64_t offset, const void *bytes,
    size_t size)
{
    if (offset > FWLAB_FILE_NAND_V0_IMAGE_BYTES ||
        size > FWLAB_FILE_NAND_V0_IMAGE_BYTES - offset) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    return media->substrate.ops->write(media->substrate.context, offset,
                                       bytes, size);
}

static enum fwlab_nfc_api_result write_phase_last(
    struct fwlab_file_nand_v0 *media, uint64_t offset, const uint8_t *bytes,
    size_t size, size_t marker_offset)
{
    enum fwlab_nfc_api_result result;

    if (marker_offset > size || size - marker_offset < 4) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    result = substrate_write(media, offset, bytes, marker_offset);
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    if (marker_offset + 4 < size) {
        result = substrate_write(media, offset + marker_offset + 4,
                                 &bytes[marker_offset + 4],
                                 size - marker_offset - 4);
        if (result != FWLAB_NFC_API_OK) {
            return result;
        }
    }
    return substrate_write(media, offset + marker_offset,
                           &bytes[marker_offset], 4);
}

static enum fwlab_nfc_api_result barrier(struct fwlab_file_nand_v0 *media)
{
    return media->substrate.ops->barrier(media->substrate.context);
}

static uint32_t crc_object(uint8_t *bytes, size_t size, size_t crc_offset,
                           size_t marker_offset)
{
    uint8_t crc_bytes[4];
    uint8_t marker[4];
    uint32_t crc;

    memcpy(crc_bytes, &bytes[crc_offset], 4);
    memcpy(marker, &bytes[marker_offset], 4);
    memset(&bytes[crc_offset], 0, 4);
    memset(&bytes[marker_offset], 0, 4);
    crc = file_nand_crc32c(bytes, size);
    memcpy(&bytes[crc_offset], crc_bytes, 4);
    memcpy(&bytes[marker_offset], marker, 4);
    return crc;
}

static int object_is_erased(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != UINT8_C(0xff)) {
            return 0;
        }
    }
    return 1;
}

static uint64_t wal_bank_offset(uint8_t bank)
{
    return bank == 0 ? FILE_NAND_WAL0 : FILE_NAND_WAL1;
}

static uint64_t wal_record_offset(uint8_t bank, uint16_t ordinal)
{
    uint16_t segment = ordinal / FILE_NAND_WAL_RECORDS_PER_SEGMENT;
    uint16_t within = ordinal % FILE_NAND_WAL_RECORDS_PER_SEGMENT;

    return wal_bank_offset(bank) +
           (uint64_t)segment * FILE_NAND_WAL_SEGMENT_BYTES +
           FILE_NAND_WAL_RECORD_BYTES +
           (uint64_t)within * FILE_NAND_WAL_RECORD_BYTES;
}

static enum fwlab_nfc_api_result write_segment_header(
    struct fwlab_file_nand_v0 *media, uint8_t bank, uint8_t segment,
    uint64_t first_lsn, uint64_t previous_hash)
{
    uint8_t bytes[512];
    uint64_t offset;
    enum fwlab_nfc_api_result result;

    memset(bytes, 0, sizeof(bytes));
    file_nand_put_le32(&bytes[0], FILE_NAND_WAL_SEGMENT_MAGIC);
    file_nand_put_le16(&bytes[4], FWLAB_FILE_NAND_V0_VERSION);
    file_nand_put_le16(&bytes[6], sizeof(bytes));
    bytes[8] = bank;
    bytes[9] = segment;
    file_nand_put_le32(&bytes[12], media->wal_epoch);
    file_nand_put_le64(&bytes[16], first_lsn);
    file_nand_put_le64(&bytes[24], previous_hash);
    memcpy(&bytes[32], media->media_uuid, sizeof(media->media_uuid));
    file_nand_put_le32(&bytes[48], crc_object(bytes, sizeof(bytes), 48, 508));
    file_nand_put_le32(&bytes[508], FILE_NAND_PHASE_MARKER);
    offset = wal_bank_offset(bank) +
             (uint64_t)segment * FILE_NAND_WAL_SEGMENT_BYTES;
    result = write_phase_last(media, offset, bytes, sizeof(bytes), 508);
    return result == FWLAB_NFC_API_OK ? barrier(media) : result;
}

static int validate_segment_header(
    uint8_t bytes[512], uint8_t bank, uint8_t segment, uint32_t epoch,
    uint64_t first_lsn, uint64_t previous_hash, const uint8_t uuid[16])
{
    return file_nand_get_le32(&bytes[0]) == FILE_NAND_WAL_SEGMENT_MAGIC &&
           file_nand_get_le16(&bytes[4]) == FWLAB_FILE_NAND_V0_VERSION &&
           file_nand_get_le16(&bytes[6]) == sizeof(uint8_t) * 512 &&
           bytes[8] == bank && bytes[9] == segment &&
           file_nand_get_le16(&bytes[10]) == 0 &&
           file_nand_get_le32(&bytes[12]) == epoch &&
           file_nand_get_le64(&bytes[16]) == first_lsn &&
           file_nand_get_le64(&bytes[24]) == previous_hash &&
           memcmp(&bytes[32], uuid, 16) == 0 &&
           file_nand_bytes_zero(&bytes[52], 456) &&
           file_nand_get_le32(&bytes[508]) == FILE_NAND_PHASE_MARKER &&
           file_nand_get_le32(&bytes[48]) ==
               crc_object(bytes, 512, 48, 508);
}

static uint64_t transaction_candidate_hash(
    struct fwlab_file_nand_v0 *media,
    const struct file_nand_transaction *transaction,
    enum fwlab_nfc_api_result *status)
{
    uint8_t candidate[8192];
    uint8_t hash_bytes[12];
    uint64_t hash = UINT64_C(1469598103934665603);
    uint16_t index;

    *status = FWLAB_NFC_API_OK;
    for (index = 0; index < transaction->candidate_count; ++index) {
        const struct file_nand_candidate_desc *desc =
            &transaction->candidate[index];
        struct fwlab_nand_page_info page;
        uint8_t main[4096];
        uint8_t oob[128];
        uint64_t generation;
        uint64_t transaction_uid;
        uint32_t crc;

        *status = substrate_read(media,
            file_nand_page_slot_offset(desc->linear_page, desc->slot),
            candidate, sizeof(candidate));
        if (*status != FWLAB_NFC_API_OK ||
            file_nand_decode_page_candidate(candidate, desc->linear_page,
                desc->slot, &page, main, oob, &generation,
                &transaction_uid, &crc) != FWLAB_NFC_API_OK ||
            generation != desc->generation ||
            transaction_uid != transaction->transaction_uid) {
            *status = FWLAB_NFC_API_INVARIANT_FAILURE;
            return 0;
        }
        file_nand_put_le16(&hash_bytes[0], desc->linear_page);
        hash_bytes[2] = desc->slot;
        hash_bytes[3] = desc->kind;
        file_nand_put_le32(&hash_bytes[4], desc->generation);
        file_nand_put_le32(&hash_bytes[8], crc);
        hash ^= file_nand_hash64(hash_bytes, sizeof(hash_bytes));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void encode_transaction_fields(
    uint8_t bytes[512], const struct file_nand_transaction *transaction)
{
    uint16_t index;

    bytes[9] = transaction->operation_kind;
    bytes[10] = transaction->physical_outcome;
    bytes[11] = transaction->integrity;
    file_nand_put_le32(&bytes[12], transaction->applied_main_bytes);
    file_nand_put_le32(&bytes[16], transaction->applied_oob_bytes);
    file_nand_put_le16(&bytes[20], transaction->applied_pages);
    file_nand_put_le16(&bytes[22], transaction->candidate_count);
    file_nand_put_le64(&bytes[32], transaction->transaction_uid);
    file_nand_put_le64(&bytes[64], transaction->old_generation);
    file_nand_put_le64(&bytes[72], transaction->new_generation);
    file_nand_put_le16(&bytes[80], transaction->ppa.channel);
    file_nand_put_le16(&bytes[82], transaction->ppa.lun);
    file_nand_put_le16(&bytes[84], transaction->ppa.plane);
    file_nand_put_le16(&bytes[86], transaction->ppa.block);
    file_nand_put_le16(&bytes[88], transaction->ppa.page);
    file_nand_put_le16(&bytes[90], transaction->base_erase_generation);
    file_nand_put_le16(&bytes[92], transaction->final_erase_generation);
    file_nand_put_le64(&bytes[96], transaction->payload_digest);
    for (index = 0; index < transaction->candidate_count; ++index) {
        uint8_t *desc = &bytes[128 + index * 8u];

        file_nand_put_le16(&desc[0],
                           transaction->candidate[index].linear_page);
        desc[2] = transaction->candidate[index].slot;
        desc[3] = transaction->candidate[index].kind;
        file_nand_put_le32(&desc[4],
                           transaction->candidate[index].generation);
    }
    file_nand_put_le64(&bytes[384], transaction->candidate_set_hash);
    bytes[392] = transaction->health_present;
    bytes[393] = transaction->health_block;
    bytes[394] = transaction->health_slot;
    file_nand_put_le32(&bytes[396], transaction->health_generation);
    file_nand_put_le32(&bytes[400], transaction->health_crc);
}

static enum fwlab_nfc_api_result write_wal_record(
    struct fwlab_file_nand_v0 *media, uint8_t kind,
    const struct file_nand_transaction *transaction, uint64_t begin_lsn,
    uint64_t applied_lsn, uint64_t *written_lsn)
{
    uint8_t bytes[512];
    uint64_t lsn;
    uint64_t record_hash;
    enum fwlab_nfc_api_result result;

    if (media->wal_record_count >= 60 || media->next_lsn == 0) {
        return FWLAB_NFC_API_NO_CAPACITY;
    }
    if (media->wal_record_count % FILE_NAND_WAL_RECORDS_PER_SEGMENT == 0) {
        result = write_segment_header(
            media, media->active_wal_bank,
            (uint8_t)(media->wal_record_count /
                      FILE_NAND_WAL_RECORDS_PER_SEGMENT),
            media->next_lsn, media->previous_record_hash);
        if (result != FWLAB_NFC_API_OK) {
            return result;
        }
    }
    lsn = media->next_lsn;
    memset(bytes, 0, sizeof(bytes));
    file_nand_put_le32(&bytes[0], FILE_NAND_WAL_RECORD_MAGIC);
    file_nand_put_le16(&bytes[4], FWLAB_FILE_NAND_V0_VERSION);
    file_nand_put_le16(&bytes[6], sizeof(bytes));
    bytes[8] = kind;
    encode_transaction_fields(bytes, transaction);
    file_nand_put_le64(&bytes[24], lsn);
    file_nand_put_le64(&bytes[40], media->previous_record_hash);
    file_nand_put_le64(&bytes[48], begin_lsn);
    file_nand_put_le64(&bytes[56], applied_lsn);
    memcpy(&bytes[104], media->media_uuid, 16);
    record_hash = file_nand_hash64(bytes, sizeof(bytes));
    file_nand_put_le64(&bytes[120], record_hash);
    file_nand_put_le32(&bytes[496],
        crc_object(bytes, sizeof(bytes), 496, 508));
    file_nand_put_le32(&bytes[508], FILE_NAND_PHASE_MARKER);
    result = write_phase_last(media,
        wal_record_offset(media->active_wal_bank, media->wal_record_count),
        bytes, sizeof(bytes), 508);
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    result = barrier(media);
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    media->previous_record_hash = record_hash;
    ++media->wal_record_count;
    ++media->next_lsn;
    *written_lsn = lsn;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result decode_wal_record(
    uint8_t bytes[512], const uint8_t uuid[16], uint64_t expected_lsn,
    uint64_t expected_previous_hash, struct file_nand_wal_record *record)
{
    uint8_t hash_copy[512];
    uint16_t index;
    uint64_t stored_hash;

    if (file_nand_get_le32(&bytes[0]) != FILE_NAND_WAL_RECORD_MAGIC ||
        file_nand_get_le16(&bytes[4]) != FWLAB_FILE_NAND_V0_VERSION ||
        file_nand_get_le16(&bytes[6]) != sizeof(uint8_t) * 512 ||
        bytes[8] < FILE_NAND_WAL_BEGIN ||
        bytes[8] > FILE_NAND_WAL_ROLLBACK ||
        bytes[9] < FILE_NAND_MUTATION_PROGRAM ||
        bytes[9] > FILE_NAND_MUTATION_MARK_BAD ||
        bytes[10] > FWLAB_NFC_PHYS_APPLIED ||
        bytes[11] > FWLAB_NFC_INTEGRITY_TORN ||
        file_nand_get_le16(&bytes[22]) > 32 ||
        file_nand_get_le64(&bytes[24]) != expected_lsn ||
        file_nand_get_le64(&bytes[32]) == 0 ||
        file_nand_get_le64(&bytes[40]) != expected_previous_hash ||
        !file_nand_bytes_zero(&bytes[94], 2) ||
        memcmp(&bytes[104], uuid, 16) != 0 ||
        !file_nand_bytes_zero(&bytes[404], 92) ||
        !file_nand_bytes_zero(&bytes[500], 8) ||
        file_nand_get_le32(&bytes[508]) != FILE_NAND_PHASE_MARKER ||
        file_nand_get_le32(&bytes[496]) !=
            crc_object(bytes, 512, 496, 508)) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    memcpy(hash_copy, bytes, sizeof(hash_copy));
    stored_hash = file_nand_get_le64(&hash_copy[120]);
    memset(&hash_copy[120], 0, 8);
    memset(&hash_copy[496], 0, 4);
    memset(&hash_copy[508], 0, 4);
    if (stored_hash != file_nand_hash64(hash_copy, sizeof(hash_copy))) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    memset(record, 0, sizeof(*record));
    record->record_kind = bytes[8];
    record->lsn = expected_lsn;
    record->previous_hash = expected_previous_hash;
    record->begin_lsn = file_nand_get_le64(&bytes[48]);
    record->applied_lsn = file_nand_get_le64(&bytes[56]);
    record->record_hash = stored_hash;
    record->transaction.operation_kind = bytes[9];
    record->transaction.physical_outcome = bytes[10];
    record->transaction.integrity = bytes[11];
    record->transaction.applied_main_bytes = file_nand_get_le32(&bytes[12]);
    record->transaction.applied_oob_bytes = file_nand_get_le32(&bytes[16]);
    record->transaction.applied_pages = file_nand_get_le16(&bytes[20]);
    record->transaction.candidate_count = file_nand_get_le16(&bytes[22]);
    record->transaction.transaction_uid = file_nand_get_le64(&bytes[32]);
    record->transaction.old_generation = file_nand_get_le64(&bytes[64]);
    record->transaction.new_generation = file_nand_get_le64(&bytes[72]);
    record->transaction.ppa.channel = file_nand_get_le16(&bytes[80]);
    record->transaction.ppa.lun = file_nand_get_le16(&bytes[82]);
    record->transaction.ppa.plane = file_nand_get_le16(&bytes[84]);
    record->transaction.ppa.block = file_nand_get_le16(&bytes[86]);
    record->transaction.ppa.page = file_nand_get_le16(&bytes[88]);
    record->transaction.base_erase_generation =
        file_nand_get_le16(&bytes[90]);
    record->transaction.final_erase_generation =
        file_nand_get_le16(&bytes[92]);
    record->transaction.payload_digest = file_nand_get_le64(&bytes[96]);
    record->transaction.candidate_set_hash = file_nand_get_le64(&bytes[384]);
    record->transaction.health_present = bytes[392];
    record->transaction.health_block = bytes[393];
    record->transaction.health_slot = bytes[394];
    record->transaction.health_generation = file_nand_get_le32(&bytes[396]);
    record->transaction.health_crc = file_nand_get_le32(&bytes[400]);
    if (!file_nand_ppa_valid(&record->transaction.ppa) ||
        record->transaction.health_present > 1 ||
        (record->transaction.health_present &&
         (record->transaction.health_block >= FILE_NAND_BLOCK_COUNT ||
          record->transaction.health_slot > 1))) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    for (index = 0; index < record->transaction.candidate_count; ++index) {
        const uint8_t *desc = &bytes[128 + index * 8u];

        record->transaction.candidate[index].linear_page =
            file_nand_get_le16(&desc[0]);
        record->transaction.candidate[index].slot = desc[2];
        record->transaction.candidate[index].kind = desc[3];
        record->transaction.candidate[index].generation =
            file_nand_get_le32(&desc[4]);
        if (record->transaction.candidate[index].linear_page >=
                FILE_NAND_PAGE_COUNT ||
            record->transaction.candidate[index].slot > 1 ||
            record->transaction.candidate[index].generation == 0) {
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
    }
    if (!file_nand_bytes_zero(
            &bytes[128 + record->transaction.candidate_count * 8u],
            256u - record->transaction.candidate_count * 8u)) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    return FWLAB_NFC_API_OK;
}

static int transaction_same(const struct file_nand_transaction *left,
                            const struct file_nand_transaction *right)
{
    return left->operation_kind == right->operation_kind &&
           left->physical_outcome == right->physical_outcome &&
           left->integrity == right->integrity &&
           left->candidate_count == right->candidate_count &&
           left->transaction_uid == right->transaction_uid &&
           left->old_generation == right->old_generation &&
           left->new_generation == right->new_generation &&
           left->candidate_set_hash == right->candidate_set_hash &&
           left->health_present == right->health_present &&
           left->health_block == right->health_block &&
           left->health_slot == right->health_slot &&
           left->health_generation == right->health_generation &&
           memcmp(&left->ppa, &right->ppa, sizeof(left->ppa)) == 0 &&
           memcmp(left->candidate, right->candidate,
                  left->candidate_count * sizeof(left->candidate[0])) == 0;
}

static enum fwlab_nfc_api_result validate_transaction_candidates(
    struct fwlab_file_nand_v0 *media,
    const struct file_nand_transaction *transaction)
{
    enum fwlab_nfc_api_result status;
    uint64_t hash;

    if (transaction->physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT) {
        return transaction->candidate_count == 0 &&
                       !transaction->health_present ?
                   FWLAB_NFC_API_OK : FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    hash = transaction_candidate_hash(media, transaction, &status);
    if (status != FWLAB_NFC_API_OK ||
        hash != transaction->candidate_set_hash) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    if (transaction->health_present) {
        uint8_t bytes[256];
        struct fwlab_nand_block_info info;
        uint64_t generation;
        uint64_t transaction_uid;
        uint32_t crc;

        status = substrate_read(media,
            file_nand_health_slot_offset(transaction->health_block,
                                         transaction->health_slot),
            bytes, sizeof(bytes));
        if (status != FWLAB_NFC_API_OK ||
            file_nand_decode_health_candidate(bytes,
                transaction->health_block, transaction->health_slot, &info,
                &generation, &transaction_uid, &crc) != FWLAB_NFC_API_OK ||
            generation != transaction->health_generation ||
            transaction_uid != transaction->transaction_uid ||
            crc != transaction->health_crc) {
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result apply_transaction(
    struct fwlab_file_nand_v0 *media,
    const struct file_nand_transaction *transaction)
{
    uint16_t index;
    enum fwlab_nfc_api_result result =
        validate_transaction_candidates(media, transaction);

    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    if (transaction->physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT) {
        return FWLAB_NFC_API_OK;
    }
    if (transaction->old_generation != media->physical_generation ||
        transaction->new_generation != media->physical_generation + 1u) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    for (index = 0; index < transaction->candidate_count; ++index) {
        uint16_t page = transaction->candidate[index].linear_page;

        media->selected_page_slot[page] = transaction->candidate[index].slot;
        media->page_generation[page] = transaction->candidate[index].generation;
    }
    if (transaction->health_present) {
        media->selected_health_slot[transaction->health_block] =
            transaction->health_slot;
        media->health_generation[transaction->health_block] =
            transaction->health_generation;
    }
    media->physical_generation = transaction->new_generation;
    return FWLAB_NFC_API_OK;
}

static void encode_checkpoint(struct fwlab_file_nand_v0 *media,
                              uint8_t bytes[4096], uint64_t generation,
                              uint64_t covered_lsn, uint8_t active_bank,
                              uint32_t wal_epoch)
{
    uint16_t index;

    memset(bytes, 0, 4096);
    file_nand_put_le32(&bytes[0], FILE_NAND_CP_MAGIC);
    file_nand_put_le16(&bytes[4], FWLAB_FILE_NAND_V0_VERSION);
    file_nand_put_le16(&bytes[6], 4096);
    file_nand_put_le64(&bytes[8], generation);
    file_nand_put_le64(&bytes[16], covered_lsn);
    file_nand_put_le64(&bytes[24], media->physical_generation);
    file_nand_put_le64(&bytes[32], media->previous_record_hash);
    memcpy(&bytes[40], media->media_uuid, 16);
    for (index = 0; index < FILE_NAND_PAGE_COUNT; ++index) {
        if (media->selected_page_slot[index]) {
            bytes[56 + index / 8u] |= (uint8_t)(1u << (index % 8u));
        }
    }
    for (index = 0; index < FILE_NAND_BLOCK_COUNT; ++index) {
        if (media->selected_health_slot[index]) {
            bytes[120 + index / 8u] |= (uint8_t)(1u << (index % 8u));
        }
    }
    bytes[122] = active_bank;
    file_nand_put_le32(&bytes[124], wal_epoch);
    file_nand_put_le32(&bytes[4088],
        crc_object(bytes, 4096, 4088, 4092));
    file_nand_put_le32(&bytes[4092], FILE_NAND_PHASE_MARKER);
}

static enum fwlab_nfc_api_result validate_checkpoint(
    struct fwlab_file_nand_v0 *media, uint8_t bytes[4096],
    uint64_t expected_generation, uint32_t expected_crc,
    uint8_t expected_bank, uint32_t expected_epoch)
{
    uint16_t index;

    if (file_nand_get_le32(&bytes[0]) != FILE_NAND_CP_MAGIC ||
        file_nand_get_le16(&bytes[4]) != FWLAB_FILE_NAND_V0_VERSION ||
        file_nand_get_le16(&bytes[6]) != 4096 ||
        file_nand_get_le64(&bytes[8]) != expected_generation ||
        memcmp(&bytes[40], media->media_uuid, 16) != 0 ||
        bytes[122] != expected_bank || file_nand_get_le16(&bytes[122]) > 1 ||
        file_nand_get_le32(&bytes[124]) != expected_epoch ||
        !file_nand_bytes_zero(&bytes[123], 1) ||
        !file_nand_bytes_zero(&bytes[128], 3960) ||
        file_nand_get_le32(&bytes[4092]) != FILE_NAND_PHASE_MARKER ||
        file_nand_get_le32(&bytes[4088]) != expected_crc ||
        expected_crc != crc_object(bytes, 4096, 4088, 4092)) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    media->checkpoint_generation = expected_generation;
    media->covered_lsn = file_nand_get_le64(&bytes[16]);
    media->physical_generation = file_nand_get_le64(&bytes[24]);
    media->previous_record_hash = file_nand_get_le64(&bytes[32]);
    for (index = 0; index < FILE_NAND_PAGE_COUNT; ++index) {
        media->selected_page_slot[index] =
            (uint8_t)((bytes[56 + index / 8u] >> (index % 8u)) & 1u);
    }
    for (index = 0; index < FILE_NAND_BLOCK_COUNT; ++index) {
        media->selected_health_slot[index] =
            (uint8_t)((bytes[120 + index / 8u] >> (index % 8u)) & 1u);
    }
    return FWLAB_NFC_API_OK;
}

static void encode_super(struct fwlab_file_nand_v0 *media,
                         uint8_t bytes[4096], uint64_t generation,
                         uint8_t checkpoint_copy, uint32_t checkpoint_crc,
                         uint8_t active_bank, uint32_t wal_epoch,
                         uint64_t covered_lsn)
{
    memset(bytes, 0, 4096);
    file_nand_put_le32(&bytes[0], FILE_NAND_SUPER_MAGIC);
    file_nand_put_le16(&bytes[4], FWLAB_FILE_NAND_V0_VERSION);
    file_nand_put_le16(&bytes[6], 4096);
    file_nand_put_le64(&bytes[8], generation);
    memcpy(&bytes[16], media->media_uuid, 16);
    file_nand_put_le32(&bytes[32], 1);
    file_nand_put_le32(&bytes[36], 1);
    file_nand_put_le32(&bytes[40], 1);
    file_nand_put_le32(&bytes[44], FILE_NAND_BLOCK_COUNT);
    file_nand_put_le32(&bytes[48], FILE_NAND_PAGES_PER_BLOCK);
    file_nand_put_le32(&bytes[52], FILE_NAND_MAIN_BYTES);
    file_nand_put_le32(&bytes[56], FILE_NAND_OOB_BYTES);
    file_nand_put_le32(&bytes[60], FILE_NAND_PAGE_SLOT_BYTES);
    file_nand_put_le32(&bytes[64], FILE_NAND_HEALTH_SLOT_BYTES);
    file_nand_put_le32(&bytes[68], FILE_NAND_WAL_RECORD_BYTES);
    file_nand_put_le32(&bytes[72], FILE_NAND_WAL_BANK_BYTES);
    file_nand_put_le64(&bytes[80], FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    bytes[88] = checkpoint_copy;
    bytes[89] = active_bank;
    file_nand_put_le32(&bytes[92], (uint32_t)media->checkpoint_generation);
    file_nand_put_le32(&bytes[96],
                       (uint32_t)(media->checkpoint_generation >> 32));
    file_nand_put_le32(&bytes[100], checkpoint_crc);
    file_nand_put_le32(&bytes[104], wal_epoch);
    file_nand_put_le64(&bytes[108], covered_lsn);
    file_nand_put_le64(&bytes[116], media->physical_generation);
    file_nand_put_le64(&bytes[124], media->previous_record_hash);
    file_nand_put_le32(&bytes[4088],
        crc_object(bytes, 4096, 4088, 4092));
    file_nand_put_le32(&bytes[4092], FILE_NAND_PHASE_MARKER);
}

static int validate_super(uint8_t bytes[4096], const uint8_t uuid[16],
                          uint64_t *generation, uint8_t *checkpoint_copy,
                          uint64_t *checkpoint_generation,
                          uint32_t *checkpoint_crc, uint8_t *active_bank,
                          uint32_t *wal_epoch, uint64_t *covered_lsn,
                          uint64_t *physical_generation,
                          uint64_t *previous_hash)
{
    if (file_nand_get_le32(&bytes[0]) != FILE_NAND_SUPER_MAGIC ||
        file_nand_get_le16(&bytes[4]) != FWLAB_FILE_NAND_V0_VERSION ||
        file_nand_get_le16(&bytes[6]) != 4096 ||
        file_nand_get_le64(&bytes[8]) == 0 ||
        memcmp(&bytes[16], uuid, 16) != 0 ||
        file_nand_get_le32(&bytes[32]) != 1 ||
        file_nand_get_le32(&bytes[36]) != 1 ||
        file_nand_get_le32(&bytes[40]) != 1 ||
        file_nand_get_le32(&bytes[44]) != FILE_NAND_BLOCK_COUNT ||
        file_nand_get_le32(&bytes[48]) != FILE_NAND_PAGES_PER_BLOCK ||
        file_nand_get_le32(&bytes[52]) != FILE_NAND_MAIN_BYTES ||
        file_nand_get_le32(&bytes[56]) != FILE_NAND_OOB_BYTES ||
        file_nand_get_le32(&bytes[60]) != FILE_NAND_PAGE_SLOT_BYTES ||
        file_nand_get_le32(&bytes[64]) != FILE_NAND_HEALTH_SLOT_BYTES ||
        file_nand_get_le32(&bytes[68]) != FILE_NAND_WAL_RECORD_BYTES ||
        file_nand_get_le32(&bytes[72]) != FILE_NAND_WAL_BANK_BYTES ||
        !file_nand_bytes_zero(&bytes[76], 4) ||
        file_nand_get_le64(&bytes[80]) != FWLAB_FILE_NAND_V0_IMAGE_BYTES ||
        bytes[88] > 1 || bytes[89] > 1 ||
        file_nand_get_le16(&bytes[90]) != 0 ||
        !file_nand_bytes_zero(&bytes[132], 3956) ||
        file_nand_get_le32(&bytes[4092]) != FILE_NAND_PHASE_MARKER ||
        file_nand_get_le32(&bytes[4088]) !=
            crc_object(bytes, 4096, 4088, 4092)) {
        return 0;
    }
    *generation = file_nand_get_le64(&bytes[8]);
    *checkpoint_copy = bytes[88];
    *active_bank = bytes[89];
    *checkpoint_generation =
        (uint64_t)file_nand_get_le32(&bytes[92]) |
        ((uint64_t)file_nand_get_le32(&bytes[96]) << 32);
    *checkpoint_crc = file_nand_get_le32(&bytes[100]);
    *wal_epoch = file_nand_get_le32(&bytes[104]);
    *covered_lsn = file_nand_get_le64(&bytes[108]);
    *physical_generation = file_nand_get_le64(&bytes[116]);
    *previous_hash = file_nand_get_le64(&bytes[124]);
    return *checkpoint_generation != 0 && *wal_epoch != 0;
}

static enum fwlab_nfc_api_result write_checkpoint_and_super(
    struct fwlab_file_nand_v0 *media, uint8_t new_bank,
    uint32_t new_epoch)
{
    uint8_t checkpoint[4096];
    uint8_t super[4096];
    uint8_t erased[4096];
    uint8_t checkpoint_copy = (uint8_t)(1u - media->selected_checkpoint_copy);
    uint8_t super_copy = (uint8_t)(1u - media->selected_super_copy);
    uint64_t checkpoint_generation = media->checkpoint_generation + 1u;
    uint64_t covered_lsn = media->next_lsn - 1u;
    uint32_t checkpoint_crc;
    uint64_t bank_offset = wal_bank_offset(new_bank);
    uint64_t offset;
    enum fwlab_nfc_api_result result;

    encode_checkpoint(media, checkpoint, checkpoint_generation, covered_lsn,
                      new_bank, new_epoch);
    checkpoint_crc = file_nand_get_le32(&checkpoint[4088]);
    offset = checkpoint_copy ? FILE_NAND_CP1 : FILE_NAND_CP0;
    result = write_phase_last(media, offset, checkpoint, sizeof(checkpoint),
                              4092);
    if (result != FWLAB_NFC_API_OK ||
        (result = barrier(media)) != FWLAB_NFC_API_OK) {
        return result;
    }
    memset(erased, 0xff, sizeof(erased));
    for (offset = 0; offset < FILE_NAND_WAL_BANK_BYTES;
         offset += sizeof(erased)) {
        result = substrate_write(media, bank_offset + offset, erased,
                                 sizeof(erased));
        if (result != FWLAB_NFC_API_OK) {
            return result;
        }
    }
    if ((result = barrier(media)) != FWLAB_NFC_API_OK) {
        return result;
    }
    media->checkpoint_generation = checkpoint_generation;
    encode_super(media, super, media->super_generation + 1u,
                 checkpoint_copy, checkpoint_crc, new_bank, new_epoch,
                 covered_lsn);
    offset = super_copy ? FILE_NAND_SUPER1 : FILE_NAND_SUPER0;
    result = write_phase_last(media, offset, super, sizeof(super), 4092);
    if (result != FWLAB_NFC_API_OK ||
        (result = barrier(media)) != FWLAB_NFC_API_OK) {
        --media->checkpoint_generation;
        return result;
    }
    ++media->super_generation;
    media->covered_lsn = covered_lsn;
    media->active_wal_bank = new_bank;
    media->wal_epoch = new_epoch;
    media->selected_checkpoint_copy = checkpoint_copy;
    media->selected_super_copy = super_copy;
    media->transactions_in_bank = 0;
    media->wal_record_count = 0;
    memcpy(media->checkpoint_page_slot, media->selected_page_slot,
           sizeof(media->checkpoint_page_slot));
    memcpy(media->checkpoint_health_slot, media->selected_health_slot,
           sizeof(media->checkpoint_health_slot));
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result recycle_if_needed(
    struct fwlab_file_nand_v0 *media)
{
    if (media->transactions_in_bank < FILE_NAND_TXNS_BEFORE_RECYCLE) {
        return FWLAB_NFC_API_OK;
    }
    if (media->checkpoint_generation == UINT64_MAX ||
        media->super_generation == UINT64_MAX || media->wal_epoch == UINT32_MAX) {
        return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    }
    return write_checkpoint_and_super(media,
        (uint8_t)(1u - media->active_wal_bank), media->wal_epoch + 1u);
}

enum fwlab_nfc_api_result file_nand_prepare_mutation(
    struct fwlab_file_nand_v0 *media, const uint16_t *linear_pages,
    uint16_t page_count, int health_block)
{
    uint16_t index;
    int needs_recycle;
    enum fwlab_nfc_api_result result = verify_holder(media);

    if (result != FWLAB_NFC_API_OK || page_count > 32 ||
        (page_count != 0 && linear_pages == NULL) || health_block < -1 ||
        health_block >= 16) {
        return result == FWLAB_NFC_API_OK ?
                   FWLAB_NFC_API_INVALID_CONTRACT : result;
    }
    needs_recycle = media->transactions_in_bank >=
                    FILE_NAND_TXNS_BEFORE_RECYCLE;
    for (index = 0; index < page_count; ++index) {
        uint16_t page = linear_pages[index];

        if (page >= FILE_NAND_PAGE_COUNT) {
            return FWLAB_NFC_API_INVALID_CONTRACT;
        }
        if ((uint8_t)(1u - media->selected_page_slot[page]) ==
            media->checkpoint_page_slot[page]) {
            needs_recycle = 1;
        }
    }
    if (health_block >= 0 &&
        (uint8_t)(1u - media->selected_health_slot[health_block]) ==
            media->checkpoint_health_slot[health_block]) {
        needs_recycle = 1;
    }
    if (!needs_recycle) {
        return FWLAB_NFC_API_OK;
    }
    if (media->checkpoint_generation == UINT64_MAX ||
        media->super_generation == UINT64_MAX || media->wal_epoch == UINT32_MAX) {
        return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    }
    return write_checkpoint_and_super(media,
        (uint8_t)(1u - media->active_wal_bank), media->wal_epoch + 1u);
}

static void set_cut_fired(struct fwlab_file_nand_v0 *media,
                          const struct file_nand_transaction *transaction,
                          uint64_t begin_lsn, uint64_t applied_lsn)
{
    memset(&media->cut_status, 0, sizeof(media->cut_status));
    media->cut_status.version = FWLAB_FILE_NAND_V0_VERSION;
    media->cut_status.size = (uint16_t)sizeof(media->cut_status);
    media->cut_status.key = media->cut_key;
    media->cut_status.ppa = transaction->ppa;
    media->cut_status.state = FWLAB_FILE_NAND_CUT_FIRED_V0;
    media->cut_status.old_physical_generation =
        transaction->old_generation;
    media->cut_status.new_physical_generation =
        transaction->new_generation;
    media->cut_status.begin_lsn = begin_lsn;
    media->cut_status.applied_lsn = applied_lsn;
    media->cut_state = FWLAB_FILE_NAND_CUT_FIRED_V0;
}

static int cut_matches(const struct fwlab_file_nand_v0 *media,
                       const struct file_nand_transaction *transaction,
                       uint8_t phase)
{
    return media->cut_state == FWLAB_FILE_NAND_CUT_ARMED_V0 &&
           media->cut_key.expected_transaction_uid ==
               transaction->transaction_uid &&
           media->cut_key.phase == phase;
}

enum fwlab_nfc_api_result file_nand_commit_transaction(
    struct fwlab_file_nand_v0 *media,
    struct file_nand_transaction *transaction,
    struct fwlab_nand_media_result *result)
{
    uint64_t begin_lsn = 0;
    uint64_t applied_lsn = 0;
    uint64_t commit_lsn = 0;
    enum fwlab_nfc_api_result status;
    uint8_t applied_kind;

    if ((status = verify_holder(media)) != FWLAB_NFC_API_OK ||
        transaction == NULL || result == NULL ||
        transaction->transaction_uid != media->next_transaction_uid ||
        transaction->transaction_uid > FILE_NAND_MAX_PHYSICAL_TXNS ||
        transaction->old_generation != media->physical_generation ||
        transaction->candidate_count > 32 ||
        (transaction->physical_outcome == FWLAB_NFC_PHYS_APPLIED &&
         transaction->new_generation != media->physical_generation + 1u) ||
        (transaction->physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT &&
         transaction->new_generation != media->physical_generation)) {
        return status == FWLAB_NFC_API_OK ?
                   FWLAB_NFC_API_INVALID_CONTRACT : status;
    }
    status = recycle_if_needed(media);
    if (status != FWLAB_NFC_API_OK || media->wal_record_count > 57) {
        return status == FWLAB_NFC_API_OK ? FWLAB_NFC_API_NO_CAPACITY : status;
    }
    if (transaction->physical_outcome == FWLAB_NFC_PHYS_APPLIED) {
        transaction->candidate_set_hash =
            transaction_candidate_hash(media, transaction, &status);
        if (status != FWLAB_NFC_API_OK) {
            return status;
        }
    } else if (transaction->candidate_count != 0 ||
               transaction->health_present) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    status = write_wal_record(media, FILE_NAND_WAL_BEGIN, transaction, 0, 0,
                              &begin_lsn);
    if (status != FWLAB_NFC_API_OK) {
        return status;
    }
    if (cut_matches(media, transaction,
                    FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0)) {
        set_cut_fired(media, transaction, begin_lsn, 0);
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    applied_kind = transaction->physical_outcome == FWLAB_NFC_PHYS_APPLIED ?
        FILE_NAND_WAL_APPLIED : FILE_NAND_WAL_NO_EFFECT;
    status = write_wal_record(media, applied_kind, transaction, begin_lsn, 0,
                              &applied_lsn);
    if (status != FWLAB_NFC_API_OK) {
        return status;
    }
    if (transaction->physical_outcome == FWLAB_NFC_PHYS_APPLIED &&
        cut_matches(media, transaction,
                    FWLAB_FILE_NAND_CUT_AFTER_EFFECT_V0)) {
        set_cut_fired(media, transaction, begin_lsn, applied_lsn);
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    status = write_wal_record(media, FILE_NAND_WAL_COMMIT, transaction,
                              begin_lsn, applied_lsn, &commit_lsn);
    if (status != FWLAB_NFC_API_OK) {
        return status;
    }
    if ((status = apply_transaction(media, transaction)) !=
        FWLAB_NFC_API_OK) {
        media->quarantined = 1;
        return status;
    }
    ++media->transactions_in_bank;
    ++media->next_transaction_uid;
    (void)commit_lsn;
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_NFC_CONTRACT_VERSION;
    result->size = (uint16_t)sizeof(*result);
    result->physical_outcome = transaction->physical_outcome;
    result->integrity = transaction->integrity;
    result->block_health = FWLAB_NFC_BLOCK_GOOD;
    result->applied_region_mask =
        (uint8_t)((transaction->applied_main_bytes != 0 ?
                       FWLAB_NFC_REGION_MAIN : 0) |
                  (transaction->applied_oob_bytes != 0 ?
                       FWLAB_NFC_REGION_OOB : 0));
    result->base_erase_generation = transaction->base_erase_generation;
    result->final_erase_generation = transaction->final_erase_generation;
    result->applied_main_bytes = transaction->applied_main_bytes;
    result->applied_oob_bytes = transaction->applied_oob_bytes;
    result->applied_pages = transaction->applied_pages;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result initialize_candidates(
    struct fwlab_file_nand_v0 *media)
{
    uint8_t page_bytes[8192];
    uint8_t health_bytes[256];
    uint8_t main[4096];
    uint8_t oob[128];
    uint16_t page;
    uint8_t block;
    uint32_t crc;
    enum fwlab_nfc_api_result result;

    memset(main, 0xff, sizeof(main));
    memset(oob, 0xff, sizeof(oob));
    for (page = 0; page < FILE_NAND_PAGE_COUNT; ++page) {
        struct fwlab_nfc_ppa ppa = {0, 0, 0,
            (uint16_t)(page / FILE_NAND_PAGES_PER_BLOCK),
            (uint16_t)(page % FILE_NAND_PAGES_PER_BLOCK), 0};

        result = file_nand_encode_page_candidate(
            page_bytes, 1, 0, &ppa, 0, FWLAB_NAND_PAGE_ERASED, 0, 0,
            main, oob, &crc);
        if (result != FWLAB_NFC_API_OK ||
            (result = write_phase_last(media,
                file_nand_page_slot_offset(page, 0), page_bytes,
                sizeof(page_bytes), 124)) != FWLAB_NFC_API_OK) {
            return result;
        }
        media->page_generation[page] = 1;
    }
    for (block = 0; block < FILE_NAND_BLOCK_COUNT; ++block) {
        struct fwlab_nand_block_info info;

        memset(&info, 0, sizeof(info));
        info.version = FWLAB_NFC_CONTRACT_VERSION;
        info.size = (uint16_t)sizeof(info);
        info.health = FWLAB_NFC_BLOCK_GOOD;
        info.erase_state = FWLAB_NAND_ERASE_CLEAN;
        result = file_nand_encode_health_candidate(
            health_bytes, 1, 0, block, 0, &info, &crc);
        if (result != FWLAB_NFC_API_OK ||
            (result = write_phase_last(media,
                file_nand_health_slot_offset(block, 0), health_bytes,
                sizeof(health_bytes), 252)) != FWLAB_NFC_API_OK) {
            return result;
        }
        media->health_generation[block] = 1;
    }
    return barrier(media);
}

size_t fwlab_file_nand_v0_arena_alignment(void)
{
    return alignof(max_align_t);
}

size_t fwlab_file_nand_v0_arena_size(void)
{
    size_t alignment = alignof(max_align_t);

    return (sizeof(struct fwlab_file_nand_v0) + alignment - 1u) &
           ~(alignment - 1u);
}

enum fwlab_nfc_api_result file_nand_engine_format(
    void *arena, size_t arena_size,
    const struct file_nand_substrate *substrate,
    const uint8_t media_uuid[16], struct fwlab_file_nand_v0 **media_out)
{
    struct fwlab_file_nand_v0 *media;
    struct file_nand_identity identity;
    uint8_t erased[4096];
    uint8_t checkpoint[4096];
    uint8_t super[4096];
    uint64_t size;
    uint64_t offset;
    uint32_t checkpoint_crc;
    enum fwlab_nfc_api_result result;

    if (arena == NULL || arena_size < fwlab_file_nand_v0_arena_size() ||
        (uintptr_t)arena % fwlab_file_nand_v0_arena_alignment() != 0 ||
        !substrate_valid(substrate) || media_uuid == NULL ||
        file_nand_bytes_zero(media_uuid, 16) || media_out == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *media_out = NULL;
    if (substrate->ops->size_get(substrate->context, &size) !=
            FWLAB_NFC_API_OK || size != 0 ||
        substrate->ops->identity(substrate->context, &identity) !=
            FWLAB_NFC_API_OK || identity.device == 0 || identity.inode == 0) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(arena, 0, fwlab_file_nand_v0_arena_size());
    media = arena;
    media->magic = FILE_NAND_MAGIC;
    media->substrate = *substrate;
    media->holder = identity;
    memcpy(media->media_uuid, media_uuid, 16);
    result = substrate->ops->resize(substrate->context,
                                    FWLAB_FILE_NAND_V0_IMAGE_BYTES);
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    memset(erased, 0xff, sizeof(erased));
    for (offset = 0; offset < FWLAB_FILE_NAND_V0_IMAGE_BYTES;
         offset += sizeof(erased)) {
        result = substrate_write(media, offset, erased, sizeof(erased));
        if (result != FWLAB_NFC_API_OK) {
            return result;
        }
    }
    if ((result = barrier(media)) != FWLAB_NFC_API_OK ||
        (result = initialize_candidates(media)) != FWLAB_NFC_API_OK) {
        return result;
    }
    media->physical_generation = 0;
    media->next_transaction_uid = 1;
    media->next_lsn = 1;
    media->super_generation = 1;
    media->checkpoint_generation = 1;
    media->wal_epoch = 1;
    media->active_wal_bank = 0;
    media->selected_checkpoint_copy = 0;
    media->selected_super_copy = 0;
    memcpy(media->checkpoint_page_slot, media->selected_page_slot,
           sizeof(media->checkpoint_page_slot));
    memcpy(media->checkpoint_health_slot, media->selected_health_slot,
           sizeof(media->checkpoint_health_slot));
    encode_checkpoint(media, checkpoint, 1, 0, 0, 1);
    checkpoint_crc = file_nand_get_le32(&checkpoint[4088]);
    result = write_phase_last(media, FILE_NAND_CP0, checkpoint,
                              sizeof(checkpoint), 4092);
    if (result != FWLAB_NFC_API_OK ||
        (result = barrier(media)) != FWLAB_NFC_API_OK) {
        return result;
    }
    encode_super(media, super, 1, 0, checkpoint_crc, 0, 1, 0);
    result = write_phase_last(media, FILE_NAND_SUPER0, super, sizeof(super),
                              4092);
    if (result != FWLAB_NFC_API_OK ||
        (result = barrier(media)) != FWLAB_NFC_API_OK) {
        return result;
    }
    media->initialized = 1;
    *media_out = media;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result load_selected_candidates(
    struct fwlab_file_nand_v0 *media, uint64_t *maximum_transaction_uid)
{
    uint16_t page;
    uint8_t block;

    for (page = 0; page < FILE_NAND_PAGE_COUNT; ++page) {
        uint8_t bytes[8192];
        uint8_t main[4096];
        uint8_t oob[128];
        struct fwlab_nand_page_info info;
        uint64_t generation;
        uint64_t transaction_uid;
        uint32_t crc;
        uint8_t slot = media->selected_page_slot[page];
        enum fwlab_nfc_api_result result = substrate_read(
            media, file_nand_page_slot_offset(page, slot), bytes,
            sizeof(bytes));

        if (result != FWLAB_NFC_API_OK ||
            file_nand_decode_page_candidate(bytes, page, slot, &info, main,
                oob, &generation, &transaction_uid, &crc) !=
                FWLAB_NFC_API_OK) {
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
        media->page_generation[page] = generation;
        if (transaction_uid > *maximum_transaction_uid) {
            *maximum_transaction_uid = transaction_uid;
        }
    }
    for (block = 0; block < FILE_NAND_BLOCK_COUNT; ++block) {
        uint8_t bytes[256];
        struct fwlab_nand_block_info info;
        uint64_t generation;
        uint64_t transaction_uid;
        uint32_t crc;
        uint8_t slot = media->selected_health_slot[block];
        enum fwlab_nfc_api_result result = substrate_read(
            media, file_nand_health_slot_offset(block, slot), bytes,
            sizeof(bytes));

        if (result != FWLAB_NFC_API_OK ||
            file_nand_decode_health_candidate(bytes, block, slot, &info,
                &generation, &transaction_uid, &crc) != FWLAB_NFC_API_OK) {
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
        media->health_generation[block] = generation;
        if (transaction_uid > *maximum_transaction_uid) {
            *maximum_transaction_uid = transaction_uid;
        }
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result scan_wal(
    struct fwlab_file_nand_v0 *media, struct file_nand_wal_record records[60],
    uint16_t *count, uint64_t *maximum_transaction_uid)
{
    uint16_t ordinal = 0;
    uint64_t expected_lsn = media->covered_lsn + 1u;
    uint64_t previous_hash = media->previous_record_hash;
    uint8_t segment;

    /* A terminal tear is legal only when no later object exists. */
#define REQUIRE_ERASED_RANGE(start_offset, byte_count) do { \
        uint8_t tail_bytes[512]; \
        uint64_t tail_offset = (start_offset); \
        uint64_t tail_left = (byte_count); \
        while (tail_left != 0) { \
            size_t tail_size = tail_left < sizeof(tail_bytes) ? \
                (size_t)tail_left : sizeof(tail_bytes); \
            enum fwlab_nfc_api_result tail_result = substrate_read( \
                media, tail_offset, tail_bytes, tail_size); \
            if (tail_result != FWLAB_NFC_API_OK || \
                !object_is_erased(tail_bytes, tail_size)) { \
                return tail_result == FWLAB_NFC_API_OK ? \
                    FWLAB_NFC_API_INVARIANT_FAILURE : tail_result; \
            } \
            tail_offset += tail_size; \
            tail_left -= tail_size; \
        } \
    } while (0)

    for (segment = 0; segment < FILE_NAND_WAL_SEGMENTS_PER_BANK; ++segment) {
        uint8_t header[512];
        uint64_t header_offset = wal_bank_offset(media->active_wal_bank) +
            (uint64_t)segment * FILE_NAND_WAL_SEGMENT_BYTES;
        enum fwlab_nfc_api_result result = substrate_read(
            media, header_offset, header, sizeof(header));
        uint8_t within;

        if (result != FWLAB_NFC_API_OK) {
            return result;
        }
        if (object_is_erased(header, sizeof(header))) {
            REQUIRE_ERASED_RANGE(
                header_offset,
                FILE_NAND_WAL_BANK_BYTES -
                    (header_offset - wal_bank_offset(media->active_wal_bank)));
            break;
        }
        if (!validate_segment_header(header, media->active_wal_bank, segment,
                                     media->wal_epoch, expected_lsn,
                                     previous_hash, media->media_uuid)) {
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
        for (within = 0; within < FILE_NAND_WAL_RECORDS_PER_SEGMENT;
             ++within) {
            uint8_t bytes[512];

            result = substrate_read(media,
                wal_record_offset(media->active_wal_bank, ordinal), bytes,
                sizeof(bytes));
            if (result != FWLAB_NFC_API_OK) {
                return result;
            }
            if (object_is_erased(bytes, sizeof(bytes)) ||
                file_nand_get_le32(&bytes[508]) != FILE_NAND_PHASE_MARKER) {
                uint64_t next_offset =
                    wal_record_offset(media->active_wal_bank, ordinal) +
                    FILE_NAND_WAL_RECORD_BYTES;
                uint64_t bank_end = wal_bank_offset(media->active_wal_bank) +
                                    FILE_NAND_WAL_BANK_BYTES;

                if (next_offset < bank_end) {
                    REQUIRE_ERASED_RANGE(next_offset, bank_end - next_offset);
                }
                *count = ordinal;
                media->next_lsn = expected_lsn;
                media->previous_record_hash = previous_hash;
                media->wal_record_count = ordinal;
                return FWLAB_NFC_API_OK;
            }
            result = decode_wal_record(bytes, media->media_uuid, expected_lsn,
                                       previous_hash, &records[ordinal]);
            if (result != FWLAB_NFC_API_OK) {
                return result;
            }
            if (records[ordinal].transaction.transaction_uid >
                *maximum_transaction_uid) {
                *maximum_transaction_uid =
                    records[ordinal].transaction.transaction_uid;
            }
            previous_hash = records[ordinal].record_hash;
            ++expected_lsn;
            ++ordinal;
        }
    }
    *count = ordinal;
    media->next_lsn = expected_lsn;
    media->previous_record_hash = previous_hash;
    media->wal_record_count = ordinal;
#undef REQUIRE_ERASED_RANGE
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result replay_wal(
    struct fwlab_file_nand_v0 *media,
    const struct file_nand_wal_record records[60], uint16_t count)
{
    uint16_t index = 0;

    media->transactions_in_bank = 0;
    while (index < count) {
        const struct file_nand_wal_record *begin = &records[index++];
        const struct file_nand_wal_record *effect;
        const struct file_nand_wal_record *terminal;
        uint64_t appended;
        enum fwlab_nfc_api_result result;

        if (begin->record_kind != FILE_NAND_WAL_BEGIN ||
            begin->begin_lsn != 0 || begin->applied_lsn != 0) {
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
        if (index == count) {
            result = write_wal_record(media, FILE_NAND_WAL_ROLLBACK,
                &begin->transaction, begin->lsn, 0, &appended);
            if (result != FWLAB_NFC_API_OK) {
                return result;
            }
            ++media->transactions_in_bank;
            continue;
        }
        effect = &records[index++];
        if (!transaction_same(&begin->transaction, &effect->transaction) ||
            effect->begin_lsn != begin->lsn || effect->applied_lsn != 0) {
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
        if (effect->record_kind == FILE_NAND_WAL_ROLLBACK) {
            ++media->transactions_in_bank;
            continue;
        }
        if (effect->record_kind != FILE_NAND_WAL_APPLIED &&
            effect->record_kind != FILE_NAND_WAL_NO_EFFECT) {
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
        if (effect->record_kind == FILE_NAND_WAL_APPLIED) {
            result = apply_transaction(media, &effect->transaction);
            if (result != FWLAB_NFC_API_OK) {
                return result;
            }
        }
        if (index == count) {
            result = write_wal_record(media, FILE_NAND_WAL_COMMIT,
                &effect->transaction, begin->lsn, effect->lsn, &appended);
            if (result != FWLAB_NFC_API_OK) {
                return result;
            }
            ++media->transactions_in_bank;
            continue;
        }
        terminal = &records[index++];
        if (terminal->record_kind != FILE_NAND_WAL_COMMIT ||
            !transaction_same(&effect->transaction, &terminal->transaction) ||
            terminal->begin_lsn != begin->lsn ||
            terminal->applied_lsn != effect->lsn) {
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
        ++media->transactions_in_bank;
    }
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result file_nand_engine_restart(
    void *arena, size_t arena_size,
    const struct file_nand_substrate *substrate,
    const struct fwlab_file_nand_holder_v0 *holder,
    struct fwlab_file_nand_v0 **media_out)
{
    struct fwlab_file_nand_v0 *media;
    struct file_nand_identity identity;
    uint8_t super_bytes[2][4096];
    uint8_t checkpoint[4096];
    int valid[2] = {0, 0};
    uint64_t generation[2] = {0, 0};
    uint8_t cp_copy[2] = {0, 0};
    uint64_t cp_generation[2] = {0, 0};
    uint32_t cp_crc[2] = {0, 0};
    uint8_t bank[2] = {0, 0};
    uint32_t epoch[2] = {0, 0};
    uint64_t covered[2] = {0, 0};
    uint64_t physical[2] = {0, 0};
    uint64_t previous[2] = {0, 0};
    struct file_nand_wal_record records[60];
    uint64_t maximum_transaction_uid = 0;
    uint64_t size;
    uint16_t count = 0;
    uint8_t selected;
    unsigned int copy;
    enum fwlab_nfc_api_result result;

    if (arena == NULL || arena_size < fwlab_file_nand_v0_arena_size() ||
        (uintptr_t)arena % fwlab_file_nand_v0_arena_alignment() != 0 ||
        !substrate_valid(substrate) || holder == NULL ||
        holder->device == 0 || holder->inode == 0 ||
        file_nand_bytes_zero(holder->media_uuid, 16) || media_out == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *media_out = NULL;
    if (substrate->ops->size_get(substrate->context, &size) !=
            FWLAB_NFC_API_OK || size != FWLAB_FILE_NAND_V0_IMAGE_BYTES ||
        substrate->ops->identity(substrate->context, &identity) !=
            FWLAB_NFC_API_OK || identity.device != holder->device ||
        identity.inode != holder->inode) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(arena, 0, fwlab_file_nand_v0_arena_size());
    media = arena;
    media->magic = FILE_NAND_MAGIC;
    media->substrate = *substrate;
    media->holder = identity;
    memcpy(media->media_uuid, holder->media_uuid, 16);
    for (copy = 0; copy < 2; ++copy) {
        result = substrate_read(media, copy ? FILE_NAND_SUPER1 :
                                      FILE_NAND_SUPER0,
                                super_bytes[copy], 4096);
        if (result != FWLAB_NFC_API_OK) {
            return result;
        }
        valid[copy] = validate_super(super_bytes[copy], media->media_uuid,
            &generation[copy], &cp_copy[copy], &cp_generation[copy],
            &cp_crc[copy], &bank[copy], &epoch[copy], &covered[copy],
            &physical[copy], &previous[copy]);
    }
    if (!valid[0] && !valid[1]) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    if (valid[0] && valid[1] && generation[0] == generation[1] &&
        memcmp(super_bytes[0], super_bytes[1], 4096) != 0) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    selected = (!valid[0] || (valid[1] && generation[1] > generation[0])) ?
        1 : 0;
    media->super_generation = generation[selected];
    media->selected_super_copy = selected;
    media->selected_checkpoint_copy = cp_copy[selected];
    media->active_wal_bank = bank[selected];
    media->wal_epoch = epoch[selected];
    media->covered_lsn = covered[selected];
    media->physical_generation = physical[selected];
    media->previous_record_hash = previous[selected];
    result = substrate_read(media, media->selected_checkpoint_copy ?
        FILE_NAND_CP1 : FILE_NAND_CP0, checkpoint, sizeof(checkpoint));
    if (result != FWLAB_NFC_API_OK ||
        (result = validate_checkpoint(media, checkpoint,
            cp_generation[selected], cp_crc[selected],
            media->active_wal_bank, media->wal_epoch)) != FWLAB_NFC_API_OK ||
        media->covered_lsn != covered[selected] ||
        media->physical_generation != physical[selected] ||
        media->previous_record_hash != previous[selected]) {
        return result == FWLAB_NFC_API_OK ?
                   FWLAB_NFC_API_INVARIANT_FAILURE : result;
    }
    memcpy(media->checkpoint_page_slot, media->selected_page_slot,
           sizeof(media->checkpoint_page_slot));
    memcpy(media->checkpoint_health_slot, media->selected_health_slot,
           sizeof(media->checkpoint_health_slot));
    if ((result = load_selected_candidates(media,
                                            &maximum_transaction_uid)) !=
            FWLAB_NFC_API_OK ||
        (result = scan_wal(media, records, &count,
                           &maximum_transaction_uid)) != FWLAB_NFC_API_OK ||
        (result = replay_wal(media, records, count)) != FWLAB_NFC_API_OK) {
        media->quarantined = 1;
        return result;
    }
    if (maximum_transaction_uid >= FILE_NAND_MAX_PHYSICAL_TXNS) {
        return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    }
    media->next_transaction_uid = maximum_transaction_uid + 1u;
    media->initialized = 1;
    *media_out = media;
    return FWLAB_NFC_API_OK;
}

int file_nand_validate_live(struct fwlab_file_nand_v0 *media)
{
    return verify_holder(media) == FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_file_nand_v0_cut_arm(
    struct fwlab_file_nand_v0 *media,
    const struct fwlab_file_nand_cut_key_v0 *key)
{
    if (verify_holder(media) != FWLAB_NFC_API_OK || key == NULL ||
        key->version != FWLAB_FILE_NAND_V0_VERSION ||
        key->size != sizeof(*key) || key->reserved0 != 0 ||
        key->expected_transaction_uid == 0 ||
        key->expected_transaction_uid != media->next_transaction_uid ||
        (key->phase != FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0 &&
         key->phase != FWLAB_FILE_NAND_CUT_AFTER_EFFECT_V0) ||
        key->one_shot != 1 ||
        !file_nand_bytes_zero(key->reserved1, sizeof(key->reserved1))) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    if (media->cut_state == FWLAB_FILE_NAND_CUT_ARMED_V0) {
        return memcmp(&media->cut_key, key, sizeof(*key)) == 0 ?
                   FWLAB_NFC_API_OK : FWLAB_NFC_API_WRONG_STATE;
    }
    if (media->cut_state != FWLAB_FILE_NAND_CUT_DISARMED_V0) {
        return FWLAB_NFC_API_WRONG_STATE;
    }
    media->cut_key = *key;
    media->cut_state = FWLAB_FILE_NAND_CUT_ARMED_V0;
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_file_nand_v0_cut_query(
    const struct fwlab_file_nand_v0 *media,
    struct fwlab_file_nand_cut_status_v0 *status)
{
    if (media == NULL || status == NULL || media->magic != FILE_NAND_MAGIC ||
        media->cut_state == FWLAB_FILE_NAND_CUT_DISARMED_V0) {
        return FWLAB_NFC_API_NOT_FOUND;
    }
    if (media->cut_state == FWLAB_FILE_NAND_CUT_FIRED_V0) {
        *status = media->cut_status;
    } else {
        memset(status, 0, sizeof(*status));
        status->version = FWLAB_FILE_NAND_V0_VERSION;
        status->size = (uint16_t)sizeof(*status);
        status->key = media->cut_key;
        status->state = media->cut_state;
    }
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_file_nand_v0_receipt(
    const struct fwlab_file_nand_v0 *media,
    struct fwlab_file_nand_receipt_v0 *receipt)
{
    if (media == NULL || receipt == NULL || media->magic != FILE_NAND_MAGIC ||
        !media->initialized || media->closed) {
        return FWLAB_NFC_API_WRONG_STATE;
    }
    memset(receipt, 0, sizeof(*receipt));
    receipt->version = FWLAB_FILE_NAND_V0_VERSION;
    receipt->size = (uint16_t)sizeof(*receipt);
    memcpy(receipt->media_uuid, media->media_uuid, 16);
    receipt->physical_generation = media->physical_generation;
    receipt->next_transaction_uid = media->next_transaction_uid;
    receipt->next_lsn = media->next_lsn;
    receipt->wal_epoch = media->wal_epoch;
    receipt->transactions_in_bank = media->transactions_in_bank;
    receipt->active_wal_bank = media->active_wal_bank;
    receipt->selected_checkpoint_copy = media->selected_checkpoint_copy;
    receipt->quarantined = media->quarantined;
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_file_nand_v0_close(
    struct fwlab_file_nand_v0 *media)
{
    enum fwlab_nfc_api_result result;

    if (media == NULL || media->magic != FILE_NAND_MAGIC || !media->initialized ||
        media->closed) {
        return FWLAB_NFC_API_WRONG_STATE;
    }
    result = media->substrate.ops->close(media->substrate.context);
    if (result == FWLAB_NFC_API_OK) {
        media->closed = 1;
    }
    return result;
}
