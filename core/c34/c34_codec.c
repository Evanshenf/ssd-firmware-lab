/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_internal.h"

#include <string.h>

static void put_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint16_t get_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)(bytes[1] << 8));
}

static uint32_t get_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

uint32_t c34_crc32c(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;
    size_t index;

    if (bytes == NULL && length != 0) {
        return 0;
    }
    for (index = 0; index < length; ++index) {
        unsigned int bit;

        crc ^= bytes[index];
        for (bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)(0u - (crc & 1u));

            crc = (crc >> 1) ^ (UINT32_C(0x82f63b78) & mask);
        }
    }
    return ~crc;
}

uint64_t c34_hash_bytes(uint64_t hash, const uint8_t *bytes, size_t length)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

int c34_ppa_equal(
    const struct fwlab_nfc_ppa *left,
    const struct fwlab_nfc_ppa *right
)
{
    return left->channel == right->channel && left->lun == right->lun &&
           left->plane == right->plane && left->block == right->block &&
           left->page == right->page && left->reserved == right->reserved;
}

uint32_t c34_page_index(const struct fwlab_nfc_ppa *ppa)
{
    return (uint32_t)ppa->block * C34_PAGES_PER_BLOCK + ppa->page;
}

struct fwlab_nfc_ppa c34_ppa(uint16_t block, uint16_t page)
{
    struct fwlab_nfc_ppa ppa;

    memset(&ppa, 0, sizeof(ppa));
    ppa.block = block;
    ppa.page = page;
    return ppa;
}

static int ppa_valid(const struct fwlab_nfc_ppa *ppa)
{
    return ppa->channel == 0 && ppa->lun == 0 && ppa->plane == 0 &&
           ppa->block < C34_BLOCKS && ppa->page < C34_PAGES_PER_BLOCK &&
           ppa->reserved == 0;
}

static void ppa_encode(
    uint8_t bytes[12],
    const struct fwlab_nfc_ppa *ppa,
    uint16_t erase_generation
)
{
    put_u16(&bytes[0], ppa->channel);
    put_u16(&bytes[2], ppa->lun);
    put_u16(&bytes[4], ppa->plane);
    put_u16(&bytes[6], ppa->block);
    put_u16(&bytes[8], ppa->page);
    put_u16(&bytes[10], erase_generation);
}

static int ppa_decode(
    const uint8_t bytes[12],
    struct fwlab_nfc_ppa *ppa,
    uint16_t *erase_generation
)
{
    memset(ppa, 0, sizeof(*ppa));
    ppa->channel = get_u16(&bytes[0]);
    ppa->lun = get_u16(&bytes[2]);
    ppa->plane = get_u16(&bytes[4]);
    ppa->block = get_u16(&bytes[6]);
    ppa->page = get_u16(&bytes[8]);
    *erase_generation = get_u16(&bytes[10]);
    return ppa_valid(ppa);
}

static int bytes_are(const uint8_t *bytes, size_t length, uint8_t value)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        if (bytes[index] != value) {
            return 0;
        }
    }
    return 1;
}

static uint16_t expected_payload_length(uint8_t type)
{
    switch ((enum c34_record_type)type) {
    case C34_RECORD_DATA:
        return C34_ATOM_BYTES;
    case C34_RECORD_MAP:
    case C34_RECORD_RELOCATION:
        return 64;
    case C34_RECORD_TOMBSTONE:
        return 0;
    case C34_RECORD_CHECKPOINT:
        return C34_MAIN_BYTES;
    case C34_RECORD_ANCHOR:
        return 32;
    default:
        return UINT16_MAX;
    }
}

static int common_record_valid(const struct c34_record *record)
{
    uint16_t length = expected_payload_length(record->type);

    if (length == UINT16_MAX || record->payload_length != length ||
        record->record_id == 0 || record->record_id > C34_RECORD_LIMIT ||
        record->commit_sequence == 0 ||
        record->commit_sequence > C34_RECORD_LIMIT) {
        return 0;
    }
    if (record->type <= C34_RECORD_RELOCATION) {
        if (record->atom >= C34_ATOMS || record->logical_version == 0 ||
            record->logical_version > 3 || record->copy_sequence > 1 ||
            record->logical_state_id == 0) {
            return 0;
        }
    }
    if (record->type == C34_RECORD_DATA) {
        return ppa_valid(&record->target_ppa) &&
               record->target_ppa.block < C34_DATA_BLOCKS &&
               record->value_crc32c ==
                   c34_crc32c(record->data, C34_ATOM_BYTES);
    }
    if (record->type == C34_RECORD_MAP) {
        return record->copy_sequence == 0 &&
               ppa_valid(&record->target_ppa) &&
               record->target_ppa.block < C34_DATA_BLOCKS &&
               record->target_data_record_id != 0 &&
               record->target_data_record_id <= C34_RECORD_LIMIT;
    }
    if (record->type == C34_RECORD_TOMBSTONE) {
        return record->copy_sequence == 0 && record->value_crc32c == 0;
    }
    if (record->type == C34_RECORD_RELOCATION) {
        return record->copy_sequence == 1 &&
               ppa_valid(&record->target_ppa) &&
               ppa_valid(&record->source_ppa) &&
               record->target_ppa.block < C34_DATA_BLOCKS &&
               record->source_ppa.block < C34_DATA_BLOCKS &&
               record->target_data_record_id != 0 &&
               record->source_authority_record_id != 0;
    }
    if (record->type == C34_RECORD_CHECKPOINT) {
        unsigned int atom;

        if (record->checkpoint_generation == 0 ||
            record->checkpoint_generation > C34_CHECKPOINT_LIMIT ||
            record->next_record_id == 0 ||
            record->next_logical_state_id == 0) {
            return 0;
        }
        for (atom = 0; atom < C34_ATOMS; ++atom) {
            const struct c34_checkpoint_entry *entry =
                &record->checkpoint[atom];

            if (entry->atom != atom || entry->kind > C34_LOGICAL_TOMBSTONE ||
                entry->version > 3 || entry->copy_sequence > 1 ||
                entry->reserved0 != 0) {
                return 0;
            }
            if (entry->kind == C34_LOGICAL_VALUE) {
                if (entry->version == 0 || entry->logical_state_id == 0 ||
                    entry->authority_record_id == 0 ||
                    entry->data_record_id == 0 ||
                    !ppa_valid(&entry->data_ppa) ||
                    entry->data_ppa.block >= C34_DATA_BLOCKS) {
                    return 0;
                }
            } else if (entry->data_record_id != 0 ||
                       entry->value_crc32c != 0) {
                return 0;
            }
        }
        return 1;
    }
    if (record->type == C34_RECORD_ANCHOR) {
        return record->checkpoint_generation != 0 &&
               record->checkpoint_generation <= C34_CHECKPOINT_LIMIT &&
               record->checkpoint_slot < 2 &&
               ppa_valid(&record->checkpoint_ppa) &&
               record->checkpoint_ppa.block ==
                   (uint16_t)(C34_CHECKPOINT_BLOCK0 +
                              record->checkpoint_slot) &&
               record->checkpoint_ppa.page == 0 &&
               record->checkpoint_record_id != 0;
    }
    return 0;
}

static void encode_checkpoint_entry(
    uint8_t bytes[36],
    const struct c34_checkpoint_entry *entry
)
{
    memset(bytes, 0, 36);
    bytes[0] = entry->atom;
    bytes[1] = entry->kind;
    bytes[2] = entry->version;
    bytes[3] = entry->copy_sequence;
    put_u32(&bytes[4], entry->logical_state_id);
    put_u32(&bytes[8], entry->authority_record_id);
    put_u32(&bytes[12], entry->data_record_id);
    if (entry->kind == C34_LOGICAL_VALUE) {
        ppa_encode(&bytes[16], &entry->data_ppa,
                   entry->data_erase_generation);
    } else {
        memset(&bytes[16], 0xff, 12);
    }
    put_u32(&bytes[28], entry->value_crc32c);
}

static int decode_checkpoint_entry(
    const uint8_t bytes[36],
    struct c34_checkpoint_entry *entry
)
{
    memset(entry, 0, sizeof(*entry));
    entry->atom = bytes[0];
    entry->kind = bytes[1];
    entry->version = bytes[2];
    entry->copy_sequence = bytes[3];
    entry->logical_state_id = get_u32(&bytes[4]);
    entry->authority_record_id = get_u32(&bytes[8]);
    entry->data_record_id = get_u32(&bytes[12]);
    if (entry->kind == C34_LOGICAL_VALUE) {
        if (!ppa_decode(&bytes[16], &entry->data_ppa,
                        &entry->data_erase_generation)) {
            return 0;
        }
    } else if (!bytes_are(&bytes[16], 12, 0xff)) {
        return 0;
    }
    entry->value_crc32c = get_u32(&bytes[28]);
    return bytes_are(&bytes[32], 4, 0);
}

enum c34_result c34_record_encode(
    const struct c34_record *record,
    uint8_t main[C34_MAIN_BYTES],
    uint8_t oob[C34_OOB_BYTES]
)
{
    unsigned int atom;

    if (record == NULL || main == NULL || oob == NULL ||
        !common_record_valid(record)) {
        return C34_INVALID_CONTRACT;
    }
    memset(main, 0xff, C34_MAIN_BYTES);
    memset(oob, 0, C34_OOB_BYTES);
    if (record->type == C34_RECORD_DATA) {
        memcpy(main, record->data, C34_ATOM_BYTES);
    } else if (record->type == C34_RECORD_MAP ||
               record->type == C34_RECORD_RELOCATION) {
        ppa_encode(&main[0], &record->target_ppa,
                   record->target_erase_generation);
        if (record->type == C34_RECORD_MAP) {
            memset(&main[12], 0xff, 12);
        } else {
            ppa_encode(&main[12], &record->source_ppa,
                       record->source_erase_generation);
        }
        put_u32(&main[24], record->target_data_record_id);
        put_u32(&main[28], record->source_authority_record_id);
        put_u32(&main[32], record->value_crc32c);
        memset(&main[36], 0, 28);
    } else if (record->type == C34_RECORD_CHECKPOINT) {
        put_u32(&main[0], record->checkpoint_generation);
        put_u32(&main[4], record->covered_commit_sequence);
        put_u32(&main[8], record->next_record_id);
        put_u32(&main[12], record->next_logical_state_id);
        put_u16(&main[16], C34_ATOMS);
        put_u16(&main[18], 36);
        memset(&main[20], 0, 4);
        for (atom = 0; atom < C34_ATOMS; ++atom) {
            encode_checkpoint_entry(&main[24 + atom * 36],
                                    &record->checkpoint[atom]);
        }
    } else if (record->type == C34_RECORD_ANCHOR) {
        put_u32(&main[0], record->checkpoint_generation);
        main[4] = record->checkpoint_slot;
        memset(&main[5], 0, 3);
        ppa_encode(&main[8], &record->checkpoint_ppa,
                   record->checkpoint_erase_generation);
        put_u32(&main[20], record->checkpoint_record_id);
        put_u32(&main[24], record->checkpoint_payload_crc32c);
        put_u32(&main[28], record->covered_commit_sequence);
    }

    put_u32(&oob[0], C34_OOB_MAGIC);
    put_u16(&oob[4], C34_CONTRACT_VERSION);
    put_u16(&oob[6], C34_OOB_BYTES);
    oob[8] = record->type;
    oob[9] = C34_FLAG_COMMIT;
    if (record->type <= C34_RECORD_RELOCATION) {
        oob[10] = record->atom;
        oob[11] = record->logical_version;
        oob[12] = record->copy_sequence;
    } else {
        oob[10] = 0xff;
        oob[11] = 0xff;
        oob[12] = 0xff;
    }
    put_u32(&oob[16], record->record_id);
    put_u32(&oob[20], record->logical_state_id);
    put_u32(&oob[24], record->predecessor_state_id);
    put_u32(&oob[28], record->commit_sequence);
    put_u16(&oob[32], record->erase_generation);
    put_u16(&oob[34], record->payload_length);
    put_u32(&oob[36], c34_crc32c(main, record->payload_length));
    put_u32(&oob[44], record->mutation_id);
    put_u32(&oob[48], record->value_crc32c);
    put_u32(&oob[40], 0);
    put_u32(&oob[40], c34_crc32c(oob, C34_OOB_BYTES));
    return C34_OK;
}

enum c34_record_decode_result c34_record_decode(
    const uint8_t main[C34_MAIN_BYTES],
    const uint8_t oob[C34_OOB_BYTES],
    const struct fwlab_nand_page_info *page,
    const struct fwlab_nand_block_info *block,
    struct c34_record *record
)
{
    uint8_t header[C34_OOB_BYTES];
    uint16_t expected;
    unsigned int atom;

    if (main == NULL || oob == NULL || page == NULL || block == NULL ||
        record == NULL || page->version != FWLAB_NFC_CONTRACT_VERSION ||
        page->size != sizeof(*page) ||
        block->version != FWLAB_NFC_CONTRACT_VERSION ||
        block->size != sizeof(*block)) {
        return C34_DECODE_INVALID;
    }
    if (page->state == FWLAB_NAND_PAGE_ERASED) {
        return bytes_are(main, C34_MAIN_BYTES, 0xff) &&
                       bytes_are(oob, C34_OOB_BYTES, 0xff) ?
                   C34_DECODE_ERASED : C34_DECODE_INVALID;
    }
    if (page->state == FWLAB_NAND_PAGE_TORN ||
        block->erase_state == FWLAB_NAND_ERASE_TORN) {
        return C34_DECODE_TORN;
    }
    if (page->state != FWLAB_NAND_PAGE_VALID || page->program_count != 1 ||
        block->health != FWLAB_NFC_BLOCK_GOOD ||
        block->erase_state != FWLAB_NAND_ERASE_CLEAN ||
        get_u32(&oob[0]) != C34_OOB_MAGIC ||
        get_u16(&oob[4]) != C34_CONTRACT_VERSION ||
        get_u16(&oob[6]) != C34_OOB_BYTES ||
        oob[9] != C34_FLAG_COMMIT || !bytes_are(&oob[13], 3, 0) ||
        !bytes_are(&oob[52], 12, 0)) {
        return C34_DECODE_INVALID;
    }
    memcpy(header, oob, sizeof(header));
    put_u32(&header[40], 0);
    if (get_u32(&oob[40]) != c34_crc32c(header, sizeof(header))) {
        return C34_DECODE_INVALID;
    }
    expected = expected_payload_length(oob[8]);
    if (expected == UINT16_MAX || get_u16(&oob[34]) != expected ||
        get_u16(&oob[32]) != block->erase_generation ||
        page->erase_generation_seen != block->erase_generation ||
        get_u32(&oob[36]) != c34_crc32c(main, expected) ||
        !bytes_are(&main[expected], C34_MAIN_BYTES - expected, 0xff)) {
        return C34_DECODE_INVALID;
    }
    memset(record, 0, sizeof(*record));
    record->type = oob[8];
    record->atom = oob[10];
    record->logical_version = oob[11];
    record->copy_sequence = oob[12];
    record->record_id = get_u32(&oob[16]);
    record->logical_state_id = get_u32(&oob[20]);
    record->predecessor_state_id = get_u32(&oob[24]);
    record->commit_sequence = get_u32(&oob[28]);
    record->erase_generation = get_u16(&oob[32]);
    record->payload_length = get_u16(&oob[34]);
    record->mutation_id = get_u32(&oob[44]);
    record->value_crc32c = get_u32(&oob[48]);
    if (record->type <= C34_RECORD_RELOCATION) {
        if (record->atom >= C34_ATOMS || record->logical_version == 0 ||
            record->logical_version > 3 || record->copy_sequence > 1) {
            return C34_DECODE_INVALID;
        }
    } else if (oob[10] != 0xff || oob[11] != 0xff || oob[12] != 0xff) {
        return C34_DECODE_INVALID;
    }

    if (record->type == C34_RECORD_DATA) {
        memcpy(record->data, main, C34_ATOM_BYTES);
        record->target_ppa = c34_ppa(0, 0);
        if (record->value_crc32c !=
            c34_crc32c(record->data, C34_ATOM_BYTES)) {
            return C34_DECODE_INVALID;
        }
    } else if (record->type == C34_RECORD_MAP ||
               record->type == C34_RECORD_RELOCATION) {
        if (!ppa_decode(&main[0], &record->target_ppa,
                        &record->target_erase_generation)) {
            return C34_DECODE_INVALID;
        }
        if (record->type == C34_RECORD_MAP) {
            if (!bytes_are(&main[12], 12, 0xff)) {
                return C34_DECODE_INVALID;
            }
        } else if (!ppa_decode(&main[12], &record->source_ppa,
                               &record->source_erase_generation)) {
            return C34_DECODE_INVALID;
        }
        record->target_data_record_id = get_u32(&main[24]);
        record->source_authority_record_id = get_u32(&main[28]);
        if (get_u32(&main[32]) != record->value_crc32c ||
            !bytes_are(&main[36], 28, 0)) {
            return C34_DECODE_INVALID;
        }
    } else if (record->type == C34_RECORD_CHECKPOINT) {
        record->checkpoint_generation = get_u32(&main[0]);
        record->covered_commit_sequence = get_u32(&main[4]);
        record->next_record_id = get_u32(&main[8]);
        record->next_logical_state_id = get_u32(&main[12]);
        if (get_u16(&main[16]) != C34_ATOMS || get_u16(&main[18]) != 36 ||
            !bytes_are(&main[20], 4, 0)) {
            return C34_DECODE_INVALID;
        }
        for (atom = 0; atom < C34_ATOMS; ++atom) {
            if (!decode_checkpoint_entry(&main[24 + atom * 36],
                                         &record->checkpoint[atom])) {
                return C34_DECODE_INVALID;
            }
        }
    } else if (record->type == C34_RECORD_ANCHOR) {
        record->checkpoint_generation = get_u32(&main[0]);
        record->checkpoint_slot = main[4];
        if (!bytes_are(&main[5], 3, 0) ||
            !ppa_decode(&main[8], &record->checkpoint_ppa,
                        &record->checkpoint_erase_generation)) {
            return C34_DECODE_INVALID;
        }
        record->checkpoint_record_id = get_u32(&main[20]);
        record->checkpoint_payload_crc32c = get_u32(&main[24]);
        record->covered_commit_sequence = get_u32(&main[28]);
    }
    return common_record_valid(record) ? C34_DECODE_OK :
                                         C34_DECODE_INVALID;
}
