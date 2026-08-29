/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_file_internal.h"

#include <string.h>

static uint64_t segment_offset(uint8_t segment);

static int all_bytes(const uint8_t *bytes, size_t length, uint8_t value)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        if (bytes[index] != value) {
            return 0;
        }
    }
    return 1;
}

static int remaining_wal_erased(
    struct c34_file_media *media,
    unsigned int segment,
    unsigned int slot
)
{
    unsigned int current_segment;

    for (current_segment = segment;
         current_segment < C34F_WAL_SEGMENTS; ++current_segment) {
        unsigned int current_slot = current_segment == segment ? slot + 1u : 0;

        if (current_segment != segment) {
            uint8_t header[C34F_WAL_HEADER_BYTES];

            if (c34f_read(
                    media, segment_offset((uint8_t)current_segment), header,
                    sizeof(header)) != C34_FILE_OK ||
                !all_bytes(header, sizeof(header), 0xff)) {
                return 0;
            }
        }
        for (; current_slot < C34F_WAL_RECORDS; ++current_slot) {
            uint8_t bytes[C34F_WAL_RECORD_BYTES];
            uint64_t offset = segment_offset((uint8_t)current_segment) +
                              C34F_WAL_HEADER_BYTES +
                              (uint64_t)current_slot *
                                  C34F_WAL_RECORD_BYTES;

            if (c34f_read(media, offset, bytes, sizeof(bytes)) !=
                    C34_FILE_OK ||
                !all_bytes(bytes, sizeof(bytes), 0xff)) {
                return 0;
            }
        }
    }
    return 1;
}

static uint64_t segment_offset(uint8_t segment)
{
    return C34F_WAL0_OFFSET +
           (uint64_t)segment * C34F_WAL_SEGMENT_BYTES;
}

static enum c34_file_result write_segment_header(
    struct c34_file_media *media,
    uint8_t segment,
    uint64_t first_lsn,
    uint64_t previous_hash
)
{
    uint8_t bytes[C34F_WAL_HEADER_BYTES];

    memset(bytes, 0, sizeof(bytes));
    c34f_put_u32(&bytes[0], C34F_WAL_MAGIC);
    c34f_put_u16(&bytes[4], C34_FILE_FORMAT_VERSION);
    c34f_put_u16(&bytes[6], C34F_WAL_HEADER_BYTES);
    bytes[8] = segment;
    c34f_put_u32(&bytes[12], media->wal_epoch);
    c34f_put_u64(&bytes[16], first_lsn);
    c34f_put_u64(&bytes[24], previous_hash);
    c34f_put_u32(&bytes[48], 0);
    c34f_put_u32(&bytes[52], C34F_MARKER);
    c34f_put_u32(&bytes[48], c34f_crc32c(bytes, sizeof(bytes)));
    return c34f_write(media, segment_offset(segment), bytes, sizeof(bytes));
}

static enum c34_file_result read_segment_header(
    struct c34_file_media *media,
    uint8_t segment,
    uint32_t *epoch,
    uint64_t *first_lsn,
    uint64_t *previous_hash,
    bool *present
)
{
    uint8_t bytes[C34F_WAL_HEADER_BYTES];
    uint8_t copy[C34F_WAL_HEADER_BYTES];
    enum c34_file_result result = c34f_read(
        media, segment_offset(segment), bytes, sizeof(bytes));

    if (result != C34_FILE_OK) {
        return result;
    }
    *present = false;
    if (all_bytes(bytes, sizeof(bytes), 0xff)) {
        return C34_FILE_OK;
    }
    memcpy(copy, bytes, sizeof(copy));
    c34f_put_u32(&copy[48], 0);
    if (c34f_get_u32(&bytes[0]) != C34F_WAL_MAGIC ||
        c34f_get_u16(&bytes[4]) != C34_FILE_FORMAT_VERSION ||
        c34f_get_u16(&bytes[6]) != C34F_WAL_HEADER_BYTES ||
        bytes[8] != segment || !all_bytes(&bytes[9], 3, 0) ||
        c34f_get_u32(&bytes[48]) != c34f_crc32c(copy, sizeof(copy)) ||
        c34f_get_u32(&bytes[52]) != C34F_MARKER ||
        !all_bytes(&bytes[56], sizeof(bytes) - 56, 0)) {
        return C34_FILE_CORRUPT;
    }
    *epoch = c34f_get_u32(&bytes[12]);
    *first_lsn = c34f_get_u64(&bytes[16]);
    *previous_hash = c34f_get_u64(&bytes[24]);
    *present = true;
    return C34_FILE_OK;
}

enum c34_file_result c34f_recycle_wal(struct c34_file_media *media)
{
    uint8_t erased[C34F_WAL_SEGMENT_BYTES];
    unsigned int segment;
    enum c34_file_result result;

    if (media == NULL || media->wal_epoch == UINT32_MAX) {
        return C34_FILE_INVALID;
    }
    memset(erased, 0xff, sizeof(erased));
    for (segment = 0; segment < C34F_WAL_SEGMENTS; ++segment) {
        result = c34f_write(media, segment_offset((uint8_t)segment), erased,
                            sizeof(erased));
        if (result != C34_FILE_OK) {
            return result;
        }
    }
    ++media->wal_epoch;
    result = write_segment_header(media, 0, media->next_lsn, 0);
    if (result != C34_FILE_OK) {
        return result;
    }
    result = c34f_barrier(media);
    if (result != C34_FILE_OK) {
        return result;
    }
    media->active_segment = 0;
    media->active_record = 0;
    media->previous_record_hash = 0;
    memset(media->page_slot_lsn, 0, sizeof(media->page_slot_lsn));
    memset(media->health_slot_lsn, 0, sizeof(media->health_slot_lsn));
    return C34_FILE_OK;
}

static void encode_delta(uint8_t bytes[64], const struct c34f_delta *delta)
{
    unsigned int index;

    memset(bytes, 0, 64);
    bytes[0] = delta->page_count;
    bytes[1] = delta->health_valid;
    bytes[2] = delta->health_block;
    bytes[3] = delta->health_slot;
    bytes[4] = delta->operation_kind;
    bytes[5] = delta->physical_outcome;
    bytes[6] = delta->integrity;
    bytes[7] = delta->applied_region_mask;
    c34f_put_u32(&bytes[8], delta->applied_main_bytes);
    c34f_put_u32(&bytes[12], delta->applied_oob_bytes);
    c34f_put_u32(&bytes[16], delta->applied_pages);
    c34f_put_u16(&bytes[20], delta->base_erase_generation);
    c34f_put_u16(&bytes[22], delta->final_erase_generation);
    for (index = 0; index < C34F_PAGES_PER_BLOCK; ++index) {
        bytes[24 + index] = delta->page_index[index];
        bytes[28 + index] = delta->page_slot[index];
        c34f_put_u32(&bytes[32 + index * 4], delta->page_hash[index]);
    }
    c34f_put_u32(&bytes[48], delta->health_hash);
}

static int decode_delta(const uint8_t bytes[64], struct c34f_delta *delta)
{
    unsigned int index;

    memset(delta, 0, sizeof(*delta));
    delta->page_count = bytes[0];
    delta->health_valid = bytes[1];
    delta->health_block = bytes[2];
    delta->health_slot = bytes[3];
    delta->operation_kind = bytes[4];
    delta->physical_outcome = bytes[5];
    delta->integrity = bytes[6];
    delta->applied_region_mask = bytes[7];
    delta->applied_main_bytes = c34f_get_u32(&bytes[8]);
    delta->applied_oob_bytes = c34f_get_u32(&bytes[12]);
    delta->applied_pages = c34f_get_u32(&bytes[16]);
    delta->base_erase_generation = c34f_get_u16(&bytes[20]);
    delta->final_erase_generation = c34f_get_u16(&bytes[22]);
    for (index = 0; index < C34F_PAGES_PER_BLOCK; ++index) {
        delta->page_index[index] = bytes[24 + index];
        delta->page_slot[index] = bytes[28 + index];
        delta->page_hash[index] = c34f_get_u32(&bytes[32 + index * 4]);
    }
    delta->health_hash = c34f_get_u32(&bytes[48]);
    return delta->page_count <= C34F_PAGES_PER_BLOCK &&
           delta->health_valid <= 1 &&
           (!delta->health_valid ||
            (delta->health_block < C34F_BLOCKS &&
             delta->health_slot < 2)) &&
           all_bytes(&bytes[52], 12, 0);
}

static void encode_record(
    const struct c34f_wal_record *record,
    uint8_t bytes[C34F_WAL_RECORD_BYTES]
)
{
    memset(bytes, 0, C34F_WAL_RECORD_BYTES);
    c34f_put_u32(&bytes[0], C34F_REC_MAGIC);
    c34f_put_u16(&bytes[4], C34_FILE_FORMAT_VERSION);
    bytes[6] = record->type;
    c34f_put_u64(&bytes[8], record->lsn);
    c34f_put_u64(&bytes[16], record->op_id);
    c34f_put_u64(&bytes[24], record->commit_sequence);
    c34f_put_u64(&bytes[32], record->previous_hash);
    c34f_put_u64(&bytes[40], record->begin_lsn);
    c34f_put_u64(&bytes[48], record->applied_lsn);
    c34f_put_u64(&bytes[56], record->old_physical_generation);
    c34f_put_u64(&bytes[64], record->new_physical_generation);
    c34f_put_u64(&bytes[72], record->inner.instance_nonce);
    c34f_put_u64(&bytes[80], record->inner.operation_uid);
    c34f_put_u32(&bytes[88], record->inner.controller_epoch);
    c34f_put_u32(&bytes[92], record->inner.generation);
    c34f_put_u16(&bytes[96], record->ppa.channel);
    c34f_put_u16(&bytes[98], record->ppa.lun);
    c34f_put_u16(&bytes[100], record->ppa.plane);
    c34f_put_u16(&bytes[102], record->ppa.block);
    c34f_put_u16(&bytes[104], record->ppa.page);
    c34f_put_u16(&bytes[106], record->ppa.reserved);
    c34f_put_u64(&bytes[108], record->payload_digest);
    c34f_put_u64(&bytes[116], record->identity_hash);
    encode_delta(&bytes[128], &record->delta);
    c34f_put_u32(&bytes[240], 0);
    c34f_put_u32(&bytes[244], C34F_MARKER);
    c34f_put_u64(&bytes[248], record->lsn);
    c34f_put_u32(&bytes[240], c34f_crc32c(bytes, sizeof(uint8_t) * 256));
}

static int decode_record(
    const uint8_t bytes[C34F_WAL_RECORD_BYTES],
    struct c34f_wal_record *record,
    uint64_t *hash
)
{
    uint8_t copy[C34F_WAL_RECORD_BYTES];

    memcpy(copy, bytes, sizeof(copy));
    c34f_put_u32(&copy[240], 0);
    if (c34f_get_u32(&bytes[0]) != C34F_REC_MAGIC ||
        c34f_get_u16(&bytes[4]) != C34_FILE_FORMAT_VERSION ||
        bytes[6] < C34F_WAL_BEGIN || bytes[6] > C34F_WAL_C_COMMIT ||
        bytes[7] != 0 || c34f_get_u64(&bytes[8]) == 0 ||
        c34f_get_u64(&bytes[8]) != c34f_get_u64(&bytes[248]) ||
        c34f_get_u32(&bytes[240]) != c34f_crc32c(copy, sizeof(copy)) ||
        c34f_get_u32(&bytes[244]) != C34F_MARKER ||
        !all_bytes(&bytes[192], 48, 0)) {
        return 0;
    }
    memset(record, 0, sizeof(*record));
    record->type = bytes[6];
    record->lsn = c34f_get_u64(&bytes[8]);
    record->op_id = c34f_get_u64(&bytes[16]);
    record->commit_sequence = c34f_get_u64(&bytes[24]);
    record->previous_hash = c34f_get_u64(&bytes[32]);
    record->begin_lsn = c34f_get_u64(&bytes[40]);
    record->applied_lsn = c34f_get_u64(&bytes[48]);
    record->old_physical_generation = c34f_get_u64(&bytes[56]);
    record->new_physical_generation = c34f_get_u64(&bytes[64]);
    record->inner.instance_nonce = c34f_get_u64(&bytes[72]);
    record->inner.operation_uid = c34f_get_u64(&bytes[80]);
    record->inner.controller_epoch = c34f_get_u32(&bytes[88]);
    record->inner.generation = c34f_get_u32(&bytes[92]);
    record->ppa.channel = c34f_get_u16(&bytes[96]);
    record->ppa.lun = c34f_get_u16(&bytes[98]);
    record->ppa.plane = c34f_get_u16(&bytes[100]);
    record->ppa.block = c34f_get_u16(&bytes[102]);
    record->ppa.page = c34f_get_u16(&bytes[104]);
    record->ppa.reserved = c34f_get_u16(&bytes[106]);
    record->payload_digest = c34f_get_u64(&bytes[108]);
    record->identity_hash = c34f_get_u64(&bytes[116]);
    if (!decode_delta(&bytes[128], &record->delta)) {
        return 0;
    }
    *hash = c34f_hash_bytes(UINT64_C(1469598103934665603), bytes,
                            C34F_WAL_RECORD_BYTES);
    return record->op_id != 0 && record->commit_sequence != 0;
}

enum c34_file_result c34f_append_wal(
    struct c34_file_media *media,
    struct c34f_wal_record *record,
    uint64_t *record_hash
)
{
    uint8_t bytes[C34F_WAL_RECORD_BYTES];
    enum c34_file_result result;
    uint64_t offset;

    if (media == NULL || record == NULL || record_hash == NULL ||
        record->type < C34F_WAL_BEGIN ||
        record->type > C34F_WAL_C_COMMIT) {
        return C34_FILE_INVALID;
    }
    if (media->active_record == C34F_WAL_RECORDS) {
        if (media->active_segment + 1u >= C34F_WAL_SEGMENTS) {
            return C34_FILE_NO_CAPACITY;
        }
        ++media->active_segment;
        media->active_record = 0;
        result = write_segment_header(
            media, media->active_segment, media->next_lsn,
            media->previous_record_hash);
        if (result != C34_FILE_OK ||
            (result = c34f_barrier(media)) != C34_FILE_OK) {
            return result;
        }
    }
    record->lsn = media->next_lsn;
    record->previous_hash = media->previous_record_hash;
    encode_record(record, bytes);
    *record_hash = c34f_hash_bytes(UINT64_C(1469598103934665603), bytes,
                                   sizeof(bytes));
    offset = segment_offset(media->active_segment) + C34F_WAL_HEADER_BYTES +
             (uint64_t)media->active_record * C34F_WAL_RECORD_BYTES;
    result = c34f_write(media, offset, bytes, sizeof(bytes));
    if (result != C34_FILE_OK) {
        return result;
    }
    ++media->active_record;
    ++media->next_lsn;
    media->previous_record_hash = *record_hash;
    return C34_FILE_OK;
}

enum c34_file_result c34f_apply_commit(
    struct c34_file_media *media,
    const struct c34f_wal_record *record
)
{
    unsigned int index;

    if (record->old_physical_generation != media->physical_generation ||
        record->new_physical_generation != media->physical_generation + 1u) {
        return C34_FILE_CORRUPT;
    }
    for (index = 0; index < record->delta.page_count; ++index) {
        uint8_t page_index = record->delta.page_index[index];
        uint8_t slot = record->delta.page_slot[index];
        enum c34_file_result result;

        if (page_index >= C34F_PAGES || slot >= 2) {
            return C34_FILE_CORRUPT;
        }
        result = c34f_load_page_candidate(
            media, page_index, slot, record->delta.page_hash[index],
            &media->page[page_index]);
        if (result != C34_FILE_OK) {
            return result;
        }
        media->page_slot[page_index] = slot;
        media->page_slot_lsn[page_index][slot] = record->lsn;
    }
    if (record->delta.health_valid) {
        uint8_t block = record->delta.health_block;
        uint8_t slot = record->delta.health_slot;
        enum c34_file_result result = c34f_load_health_candidate(
            media, block, slot, record->delta.health_hash,
            &media->block[block]);

        if (result != C34_FILE_OK) {
            return result;
        }
        media->health_slot[block] = slot;
        media->health_slot_lsn[block][slot] = record->lsn;
    }
    media->physical_generation = record->new_physical_generation;
    return C34_FILE_OK;
}

static int record_matches_begin(
    const struct c34f_wal_record *record,
    const struct c34f_wal_record *begin
)
{
    uint8_t left[64];
    uint8_t right[64];

    encode_delta(left, &record->delta);
    encode_delta(right, &begin->delta);
    return record->op_id == begin->op_id &&
           record->commit_sequence == begin->commit_sequence &&
           record->begin_lsn == begin->lsn &&
           record->old_physical_generation ==
               begin->old_physical_generation &&
           record->new_physical_generation ==
               begin->new_physical_generation &&
           memcmp(left, right, sizeof(left)) == 0;
}

static enum c34_file_result settle_incomplete(
    struct c34_file_media *media,
    const struct c34f_wal_record *begin,
    const struct c34f_wal_record *applied
)
{
    struct c34f_wal_record a;
    struct c34f_wal_record commit;
    uint64_t hash;
    enum c34_file_result result;

    if (applied == NULL) {
        a = *begin;
        a.type = begin->delta.physical_outcome == FWLAB_NFC_PHYS_APPLIED ?
            C34F_WAL_A_APPLIED : C34F_WAL_A_NO_EFFECT;
        a.begin_lsn = begin->lsn;
        a.applied_lsn = 0;
        result = c34f_append_wal(media, &a, &hash);
        if (result != C34_FILE_OK ||
            (result = c34f_barrier(media)) != C34_FILE_OK) {
            return result;
        }
        a.applied_lsn = a.lsn;
        applied = &a;
    }
    commit = *begin;
    commit.type = C34F_WAL_C_COMMIT;
    commit.begin_lsn = begin->lsn;
    commit.applied_lsn = applied->lsn;
    result = c34f_append_wal(media, &commit, &hash);
    if (result != C34_FILE_OK ||
        (result = c34f_barrier(media)) != C34_FILE_OK) {
        return result;
    }
    return c34f_apply_commit(media, &commit);
}

enum c34_file_result c34f_recover_wal(struct c34_file_media *media)
{
    struct c34f_wal_record records[C34F_WAL_SEGMENTS * C34F_WAL_RECORDS];
    uint64_t hashes[C34F_WAL_SEGMENTS * C34F_WAL_RECORDS];
    size_t count = 0;
    uint64_t expected_lsn = media->covered_lsn + 1u;
    uint64_t previous_hash = 0;
    unsigned int segment;
    enum c34_file_result result;

    for (segment = 0; segment < C34F_WAL_SEGMENTS; ++segment) {
        uint32_t epoch;
        uint64_t first_lsn;
        uint64_t header_previous;
        bool present;
        unsigned int slot;

        result = read_segment_header(
            media, (uint8_t)segment, &epoch, &first_lsn,
            &header_previous, &present);
        if (result != C34_FILE_OK) {
            return result;
        }
        if (!present) {
            if (segment == 0) {
                return C34_FILE_CORRUPT;
            }
            break;
        }
        if (epoch > media->wal_epoch) {
            return C34_FILE_CORRUPT;
        }
        if (epoch < media->wal_epoch) {
            uint32_t expected_epoch = media->wal_epoch;

            if (segment != 0) {
                return C34_FILE_CORRUPT;
            }
            media->wal_epoch = expected_epoch - 1u;
            result = c34f_recycle_wal(media);
            return result == C34_FILE_OK &&
                           media->wal_epoch == expected_epoch ?
                       C34_FILE_OK :
                       result != C34_FILE_OK ? result : C34_FILE_CORRUPT;
        }
        if (first_lsn != expected_lsn || header_previous != previous_hash) {
            return C34_FILE_CORRUPT;
        }
        for (slot = 0; slot < C34F_WAL_RECORDS; ++slot) {
            uint8_t bytes[C34F_WAL_RECORD_BYTES];
            uint64_t offset = segment_offset((uint8_t)segment) +
                              C34F_WAL_HEADER_BYTES +
                              (uint64_t)slot * C34F_WAL_RECORD_BYTES;

            result = c34f_read(media, offset, bytes, sizeof(bytes));
            if (result != C34_FILE_OK) {
                return result;
            }
            if (all_bytes(bytes, sizeof(bytes), 0xff)) {
                media->active_segment = (uint8_t)segment;
                media->active_record = (uint8_t)slot;
                segment = C34F_WAL_SEGMENTS;
                break;
            }
            if (!decode_record(bytes, &records[count], &hashes[count])) {
                if ((c34f_get_u32(&bytes[244]) != C34F_MARKER ||
                     memchr(&bytes[248], 0xff, 8) != NULL) &&
                    remaining_wal_erased(media, segment, slot)) {
                    media->active_segment = (uint8_t)segment;
                    media->active_record = (uint8_t)slot;
                    segment = C34F_WAL_SEGMENTS;
                    break;
                }
                return C34_FILE_CORRUPT;
            }
            if (records[count].lsn != expected_lsn ||
                records[count].previous_hash != previous_hash) {
                return C34_FILE_CORRUPT;
            }
            previous_hash = hashes[count];
            ++expected_lsn;
            ++count;
        }
        if (slot == C34F_WAL_RECORDS) {
            media->active_segment = (uint8_t)segment;
            media->active_record = C34F_WAL_RECORDS;
        }
    }
    media->next_lsn = expected_lsn;
    media->previous_record_hash = previous_hash;
    {
        size_t index = 0;
        uint64_t expected_op = media->next_op_id;
        uint64_t expected_commit = media->next_commit_sequence;

        while (index < count) {
            const struct c34f_wal_record *begin = &records[index];
            const struct c34f_wal_record *applied = NULL;
            const struct c34f_wal_record *commit = NULL;

            if (begin->type != C34F_WAL_BEGIN ||
                begin->op_id != expected_op ||
                begin->commit_sequence != expected_commit) {
                return C34_FILE_CORRUPT;
            }
            if (index + 1u < count &&
                (records[index + 1u].type == C34F_WAL_A_APPLIED ||
                 records[index + 1u].type == C34F_WAL_A_NO_EFFECT)) {
                applied = &records[++index];
                if (!record_matches_begin(applied, begin) ||
                    (applied->type == C34F_WAL_A_APPLIED) !=
                        (begin->delta.physical_outcome ==
                         FWLAB_NFC_PHYS_APPLIED)) {
                    return C34_FILE_CORRUPT;
                }
            }
            if (index + 1u < count &&
                records[index + 1u].type == C34F_WAL_C_COMMIT) {
                commit = &records[++index];
                if (!record_matches_begin(commit, begin) || applied == NULL ||
                    commit->applied_lsn != applied->lsn) {
                    return C34_FILE_CORRUPT;
                }
            }
            if (commit != NULL) {
                result = c34f_apply_commit(media, commit);
            } else {
                if (index + 1u != count) {
                    return C34_FILE_CORRUPT;
                }
                result = settle_incomplete(media, begin, applied);
            }
            if (result != C34_FILE_OK) {
                return result;
            }
            if (begin->op_id >= media->next_op_id) {
                media->next_op_id = begin->op_id + 1u;
            }
            if (begin->commit_sequence >= media->next_commit_sequence) {
                media->next_commit_sequence = begin->commit_sequence + 1u;
            }
            ++expected_op;
            ++expected_commit;
            ++index;
        }
    }
    return C34_FILE_OK;
}
