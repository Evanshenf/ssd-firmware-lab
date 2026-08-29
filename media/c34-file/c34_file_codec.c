/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_file_internal.h"

#include <string.h>

void c34f_put_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

void c34f_put_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void c34f_put_u64(uint8_t *bytes, uint64_t value)
{
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

uint16_t c34f_get_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)(bytes[1] << 8));
}

uint32_t c34f_get_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

uint64_t c34f_get_u64(const uint8_t *bytes)
{
    uint64_t value = 0;
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

uint32_t c34f_crc32c(const uint8_t *bytes, size_t length)
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

uint64_t c34f_hash_bytes(uint64_t hash, const uint8_t *bytes, size_t length)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

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

static enum c34_file_result io_result(enum c34_file_io_result result)
{
    return result == C34_FILE_IO_OK ? C34_FILE_OK :
           result == C34_FILE_IO_CUT ? C34_FILE_CUT : C34_FILE_IO;
}

enum c34_file_result c34f_read(
    const struct c34_file_media *media,
    uint64_t offset,
    uint8_t *bytes,
    size_t length
)
{
    if (media == NULL || bytes == NULL ||
        offset > C34_FILE_IMAGE_BYTES ||
        length > C34_FILE_IMAGE_BYTES - offset) {
        return C34_FILE_INVALID;
    }
    return io_result(media->substrate.ops->read(
        media->substrate.context, offset, bytes, length));
}

enum c34_file_result c34f_write(
    struct c34_file_media *media,
    uint64_t offset,
    const uint8_t *bytes,
    size_t length
)
{
    enum c34_file_result result;

    if (media == NULL || bytes == NULL || media->stopped ||
        offset > C34_FILE_IMAGE_BYTES ||
        length > C34_FILE_IMAGE_BYTES - offset) {
        return C34_FILE_INVALID;
    }
    result = io_result(media->substrate.ops->write(
        media->substrate.context, offset, bytes, length));
    if (result == C34_FILE_CUT) {
        media->stopped = 1;
    }
    return result;
}

enum c34_file_result c34f_barrier(struct c34_file_media *media)
{
    enum c34_file_result result;

    if (media == NULL || media->stopped) {
        return C34_FILE_INVALID;
    }
    result = io_result(media->substrate.ops->barrier(
        media->substrate.context));
    if (result == C34_FILE_CUT) {
        media->stopped = 1;
    }
    return result;
}

static uint32_t geometry_hash(void)
{
    uint8_t geometry[16];

    memset(geometry, 0, sizeof(geometry));
    c34f_put_u16(&geometry[0], 1);
    c34f_put_u16(&geometry[2], 1);
    c34f_put_u16(&geometry[4], 1);
    c34f_put_u16(&geometry[6], C34F_BLOCKS);
    c34f_put_u16(&geometry[8], C34F_PAGES_PER_BLOCK);
    c34f_put_u16(&geometry[10], C34F_MAIN_BYTES);
    c34f_put_u16(&geometry[12], C34F_OOB_BYTES);
    geometry[14] = 1;
    geometry[15] = FWLAB_NFC_PROGRAM_ASCENDING;
    return c34f_crc32c(geometry, sizeof(geometry));
}

static void encode_page_info(uint8_t bytes[16],
                             const struct fwlab_nand_page_info *info)
{
    memset(bytes, 0, 16);
    c34f_put_u16(&bytes[0], info->erase_generation_seen);
    bytes[2] = info->state;
    bytes[3] = info->program_count;
}

static int decode_page_info(const uint8_t bytes[16],
                            struct fwlab_nand_page_info *info)
{
    if (!all_bytes(&bytes[4], 12, 0)) {
        return 0;
    }
    memset(info, 0, sizeof(*info));
    info->version = FWLAB_NFC_CONTRACT_VERSION;
    info->size = sizeof(*info);
    info->erase_generation_seen = c34f_get_u16(&bytes[0]);
    info->state = bytes[2];
    info->program_count = bytes[3];
    return info->state <= FWLAB_NAND_PAGE_TORN && info->program_count <= 1;
}

static void encode_block_info(uint8_t bytes[32],
                              const struct fwlab_nand_block_info *info)
{
    memset(bytes, 0, 32);
    c34f_put_u16(&bytes[0], info->erase_generation);
    c34f_put_u16(&bytes[2], info->successful_erase_count);
    c34f_put_u16(&bytes[4], info->erase_attempt_count);
    c34f_put_u16(&bytes[6], info->next_program_page);
    bytes[8] = info->health;
    bytes[9] = info->erase_state;
}

static int decode_block_info(const uint8_t bytes[32],
                             struct fwlab_nand_block_info *info)
{
    if (!all_bytes(&bytes[10], 22, 0)) {
        return 0;
    }
    memset(info, 0, sizeof(*info));
    info->version = FWLAB_NFC_CONTRACT_VERSION;
    info->size = sizeof(*info);
    info->erase_generation = c34f_get_u16(&bytes[0]);
    info->successful_erase_count = c34f_get_u16(&bytes[2]);
    info->erase_attempt_count = c34f_get_u16(&bytes[4]);
    info->next_program_page = c34f_get_u16(&bytes[6]);
    info->health = bytes[8];
    info->erase_state = bytes[9];
    return info->health <= FWLAB_NFC_BLOCK_RUNTIME_BAD &&
           info->erase_state <= FWLAB_NAND_ERASE_TORN &&
           info->next_program_page <= C34F_PAGES_PER_BLOCK;
}

static uint64_t checkpoint_offset(uint8_t slot)
{
    return slot == 0 ? C34F_CP0_OFFSET : C34F_CP1_OFFSET;
}

enum c34_file_result c34f_write_checkpoint(
    struct c34_file_media *media,
    uint8_t slot,
    uint64_t generation,
    uint64_t covered_lsn
)
{
    uint8_t bytes[C34F_CHECKPOINT_BYTES];
    size_t body = C34F_WAL_HEADER_BYTES;
    unsigned int index;

    if (media == NULL || slot >= 2 || generation == 0) {
        return C34_FILE_INVALID;
    }
    memset(bytes, 0xff, sizeof(bytes));
    memset(bytes, 0, C34F_WAL_HEADER_BYTES);
    c34f_put_u32(&bytes[0], C34F_CP_MAGIC);
    c34f_put_u16(&bytes[4], C34_FILE_FORMAT_VERSION);
    c34f_put_u16(&bytes[6], C34F_WAL_HEADER_BYTES);
    c34f_put_u64(&bytes[8], generation);
    c34f_put_u64(&bytes[16], media->physical_generation);
    c34f_put_u64(&bytes[24], covered_lsn);
    c34f_put_u64(&bytes[32], media->next_lsn);
    c34f_put_u64(&bytes[40], media->next_op_id);
    c34f_put_u64(&bytes[48], media->next_commit_sequence);
    memcpy(&bytes[56], media->uuid, sizeof(media->uuid));
    c34f_put_u16(&bytes[72], C34F_PAGES);
    c34f_put_u16(&bytes[74], C34F_BLOCKS);
    c34f_put_u16(&bytes[76], 176);
    c34f_put_u16(&bytes[78], 32);
    c34f_put_u32(&bytes[80], C34F_WAL_HEADER_BYTES);
    c34f_put_u32(&bytes[84], 4480);
    c34f_put_u32(&bytes[88], geometry_hash());
    for (index = 0; index < C34F_PAGES; ++index) {
        memcpy(&bytes[body], media->page[index].main, C34F_MAIN_BYTES);
        memcpy(&bytes[body + C34F_MAIN_BYTES], media->page[index].oob,
               C34F_OOB_BYTES);
        encode_page_info(&bytes[body + C34F_MAIN_BYTES + C34F_OOB_BYTES],
                         &media->page[index].info);
        body += 176;
    }
    for (index = 0; index < C34F_BLOCKS; ++index) {
        encode_block_info(&bytes[body], &media->block[index]);
        body += 32;
    }
    c34f_put_u32(&bytes[96],
                  c34f_crc32c(&bytes[C34F_WAL_HEADER_BYTES],
                               8160 - C34F_WAL_HEADER_BYTES));
    c34f_put_u32(&bytes[100], 0);
    c34f_put_u32(&bytes[104], C34F_MARKER);
    c34f_put_u32(&bytes[100],
                  c34f_crc32c(bytes, C34F_WAL_HEADER_BYTES));
    c34f_put_u32(&bytes[8160], C34F_CP_MAGIC);
    c34f_put_u64(&bytes[8168], generation);
    c34f_put_u32(&bytes[8176], c34f_crc32c(bytes, 8160));
    c34f_put_u32(&bytes[8180], C34F_MARKER);
    memset(&bytes[8184], 0, 8);
    return c34f_write(media, checkpoint_offset(slot), bytes, sizeof(bytes));
}

enum c34_file_result c34f_load_checkpoint(
    struct c34_file_media *media,
    uint8_t slot,
    uint64_t expected_generation,
    uint32_t expected_hash
)
{
    uint8_t bytes[C34F_CHECKPOINT_BYTES];
    uint8_t header[C34F_WAL_HEADER_BYTES];
    size_t body = C34F_WAL_HEADER_BYTES;
    unsigned int index;
    enum c34_file_result result;

    if (media == NULL || slot >= 2 || expected_generation == 0) {
        return C34_FILE_INVALID;
    }
    result = c34f_read(media, checkpoint_offset(slot), bytes, sizeof(bytes));
    if (result != C34_FILE_OK) {
        return result;
    }
    memcpy(header, bytes, sizeof(header));
    c34f_put_u32(&header[100], 0);
    if (c34f_crc32c(bytes, sizeof(bytes)) != expected_hash ||
        c34f_get_u32(&bytes[0]) != C34F_CP_MAGIC ||
        c34f_get_u16(&bytes[4]) != C34_FILE_FORMAT_VERSION ||
        c34f_get_u16(&bytes[6]) != C34F_WAL_HEADER_BYTES ||
        c34f_get_u64(&bytes[8]) != expected_generation ||
        memcmp(&bytes[56], media->uuid, sizeof(media->uuid)) != 0 ||
        c34f_get_u16(&bytes[72]) != C34F_PAGES ||
        c34f_get_u16(&bytes[74]) != C34F_BLOCKS ||
        c34f_get_u16(&bytes[76]) != 176 ||
        c34f_get_u16(&bytes[78]) != 32 ||
        c34f_get_u32(&bytes[88]) != geometry_hash() ||
        c34f_get_u32(&bytes[100]) !=
            c34f_crc32c(header, sizeof(header)) ||
        c34f_get_u32(&bytes[104]) != C34F_MARKER ||
        c34f_get_u32(&bytes[96]) !=
            c34f_crc32c(&bytes[C34F_WAL_HEADER_BYTES],
                         8160 - C34F_WAL_HEADER_BYTES) ||
        c34f_get_u32(&bytes[8160]) != C34F_CP_MAGIC ||
        c34f_get_u64(&bytes[8168]) != expected_generation ||
        c34f_get_u32(&bytes[8176]) != c34f_crc32c(bytes, 8160) ||
        c34f_get_u32(&bytes[8180]) != C34F_MARKER ||
        !all_bytes(&bytes[8184], 8, 0)) {
        return C34_FILE_CORRUPT;
    }
    media->checkpoint_generation = expected_generation;
    media->physical_generation = c34f_get_u64(&bytes[16]);
    media->covered_lsn = c34f_get_u64(&bytes[24]);
    media->next_lsn = c34f_get_u64(&bytes[32]);
    media->next_op_id = c34f_get_u64(&bytes[40]);
    media->next_commit_sequence = c34f_get_u64(&bytes[48]);
    for (index = 0; index < C34F_PAGES; ++index) {
        memcpy(media->page[index].main, &bytes[body], C34F_MAIN_BYTES);
        memcpy(media->page[index].oob, &bytes[body + C34F_MAIN_BYTES],
               C34F_OOB_BYTES);
        if (!decode_page_info(
                &bytes[body + C34F_MAIN_BYTES + C34F_OOB_BYTES],
                &media->page[index].info)) {
            return C34_FILE_CORRUPT;
        }
        body += 176;
    }
    for (index = 0; index < C34F_BLOCKS; ++index) {
        if (!decode_block_info(&bytes[body], &media->block[index])) {
            return C34_FILE_CORRUPT;
        }
        body += 32;
    }
    memset(media->page_slot, 0, sizeof(media->page_slot));
    memset(media->health_slot, 0, sizeof(media->health_slot));
    memset(media->page_slot_lsn, 0, sizeof(media->page_slot_lsn));
    memset(media->health_slot_lsn, 0, sizeof(media->health_slot_lsn));
    memset(media->page_slot_generation, 0,
           sizeof(media->page_slot_generation));
    memset(media->health_slot_generation, 0,
           sizeof(media->health_slot_generation));
    return C34_FILE_OK;
}

enum c34_file_result c34f_write_super(
    struct c34_file_media *media,
    uint8_t copy,
    uint64_t generation,
    uint8_t checkpoint_slot,
    uint64_t checkpoint_generation,
    uint32_t checkpoint_hash,
    uint64_t covered_lsn,
    uint32_t wal_epoch
)
{
    uint8_t bytes[C34F_SUPER_BYTES];

    if (media == NULL || copy >= 2 || checkpoint_slot >= 2 ||
        generation == 0 || checkpoint_generation == 0 || wal_epoch == 0) {
        return C34_FILE_INVALID;
    }
    memset(bytes, 0xff, sizeof(bytes));
    memset(bytes, 0, 128);
    c34f_put_u32(&bytes[0], C34F_SB_MAGIC);
    c34f_put_u16(&bytes[4], C34_FILE_FORMAT_VERSION);
    c34f_put_u16(&bytes[6], 128);
    c34f_put_u64(&bytes[8], generation);
    bytes[16] = checkpoint_slot;
    c34f_put_u64(&bytes[24], checkpoint_generation);
    c34f_put_u32(&bytes[32], checkpoint_hash);
    c34f_put_u64(&bytes[40], covered_lsn);
    c34f_put_u32(&bytes[48], wal_epoch);
    c34f_put_u32(&bytes[52], C34_FILE_IMAGE_BYTES);
    c34f_put_u32(&bytes[56], geometry_hash());
    memcpy(&bytes[64], media->uuid, sizeof(media->uuid));
    c34f_put_u32(&bytes[120], 0);
    c34f_put_u32(&bytes[124], C34F_MARKER);
    c34f_put_u32(&bytes[120], c34f_crc32c(bytes, 128));
    return c34f_write(
        media, copy == 0 ? C34F_SB0_OFFSET : C34F_SB1_OFFSET, bytes,
        sizeof(bytes));
}

struct super_value {
    uint8_t valid;
    uint8_t copy;
    uint8_t checkpoint_slot;
    uint8_t reserved;
    uint64_t generation;
    uint64_t checkpoint_generation;
    uint32_t checkpoint_hash;
    uint64_t covered_lsn;
    uint32_t wal_epoch;
};

static enum c34_file_result read_super(
    struct c34_file_media *media,
    uint8_t copy,
    struct super_value *value
)
{
    uint8_t bytes[C34F_SUPER_BYTES];
    uint8_t header[128];
    enum c34_file_result result = c34f_read(
        media, copy == 0 ? C34F_SB0_OFFSET : C34F_SB1_OFFSET, bytes,
        sizeof(bytes));

    if (result != C34_FILE_OK) {
        return result;
    }
    memset(value, 0, sizeof(*value));
    if (all_bytes(bytes, sizeof(bytes), 0xff)) {
        return C34_FILE_OK;
    }
    memcpy(header, bytes, sizeof(header));
    c34f_put_u32(&header[120], 0);
    if (c34f_get_u32(&bytes[0]) != C34F_SB_MAGIC ||
        c34f_get_u16(&bytes[4]) != C34_FILE_FORMAT_VERSION ||
        c34f_get_u16(&bytes[6]) != 128 || bytes[16] >= 2 ||
        c34f_get_u64(&bytes[8]) == 0 ||
        c34f_get_u64(&bytes[24]) == 0 ||
        c34f_get_u32(&bytes[48]) == 0 ||
        c34f_get_u32(&bytes[52]) != C34_FILE_IMAGE_BYTES ||
        c34f_get_u32(&bytes[56]) != geometry_hash() ||
        memcmp(&bytes[64], media->uuid, sizeof(media->uuid)) != 0 ||
        c34f_get_u32(&bytes[120]) != c34f_crc32c(header, sizeof(header)) ||
        c34f_get_u32(&bytes[124]) != C34F_MARKER ||
        !all_bytes(&bytes[128], sizeof(bytes) - 128, 0xff)) {
        return C34_FILE_CORRUPT;
    }
    value->valid = 1;
    value->copy = copy;
    value->checkpoint_slot = bytes[16];
    value->generation = c34f_get_u64(&bytes[8]);
    value->checkpoint_generation = c34f_get_u64(&bytes[24]);
    value->checkpoint_hash = c34f_get_u32(&bytes[32]);
    value->covered_lsn = c34f_get_u64(&bytes[40]);
    value->wal_epoch = c34f_get_u32(&bytes[48]);
    return C34_FILE_OK;
}

enum c34_file_result c34f_select_super(
    struct c34_file_media *media,
    uint8_t *copy,
    uint64_t *generation,
    uint8_t *checkpoint_slot,
    uint64_t *checkpoint_generation,
    uint32_t *checkpoint_hash,
    uint64_t *covered_lsn,
    uint32_t *wal_epoch
)
{
    struct super_value value[2];
    enum c34_file_result result;
    int selected = -1;
    unsigned int index;

    if (media == NULL || copy == NULL || generation == NULL ||
        checkpoint_slot == NULL || checkpoint_generation == NULL ||
        checkpoint_hash == NULL || covered_lsn == NULL || wal_epoch == NULL) {
        return C34_FILE_INVALID;
    }
    for (index = 0; index < 2; ++index) {
        result = read_super(media, (uint8_t)index, &value[index]);
        if (result != C34_FILE_OK) {
            if (result == C34_FILE_CORRUPT) {
                continue;
            }
            return result;
        }
        if (value[index].valid &&
            (selected < 0 || value[index].generation >
                                   value[selected].generation)) {
            selected = (int)index;
        }
    }
    if (selected < 0 ||
        (value[0].valid && value[1].valid &&
         value[0].generation == value[1].generation &&
         memcmp(&value[0], &value[1], sizeof(value[0])) != 0)) {
        return C34_FILE_CORRUPT;
    }
    *copy = value[selected].copy;
    *generation = value[selected].generation;
    *checkpoint_slot = value[selected].checkpoint_slot;
    *checkpoint_generation = value[selected].checkpoint_generation;
    *checkpoint_hash = value[selected].checkpoint_hash;
    *covered_lsn = value[selected].covered_lsn;
    *wal_epoch = value[selected].wal_epoch;
    return C34_FILE_OK;
}

static uint64_t page_candidate_offset(uint8_t page_index, uint8_t slot)
{
    return C34F_PAGE_OFFSET +
           ((uint64_t)page_index * 2u + slot) * C34F_PAGE_SLOT_BYTES;
}

enum c34_file_result c34f_write_page_candidate(
    struct c34_file_media *media,
    uint8_t page_index,
    uint8_t slot,
    uint64_t source_op_id,
    uint64_t begin_lsn,
    const struct c34f_page *page,
    uint32_t *candidate_hash
)
{
    uint8_t bytes[C34F_PAGE_SLOT_BYTES];
    uint64_t generation;

    if (media == NULL || page == NULL || candidate_hash == NULL ||
        page_index >= C34F_PAGES || slot >= 2) {
        return C34_FILE_INVALID;
    }
    generation = ++media->page_slot_generation[page_index][slot];
    memset(bytes, 0, sizeof(bytes));
    c34f_put_u32(&bytes[0], C34F_PAGE_MAGIC);
    c34f_put_u16(&bytes[4], C34_FILE_FORMAT_VERSION);
    c34f_put_u16(&bytes[6], C34F_PAGE_SLOT_BYTES);
    bytes[8] = page_index;
    bytes[9] = slot;
    bytes[10] = page->info.state;
    bytes[11] = page->info.program_count;
    c34f_put_u64(&bytes[12], generation);
    c34f_put_u16(&bytes[20], page->info.erase_generation_seen);
    c34f_put_u64(&bytes[24], source_op_id);
    c34f_put_u64(&bytes[32], begin_lsn);
    c34f_put_u32(&bytes[40], c34f_crc32c(page->main, C34F_MAIN_BYTES));
    c34f_put_u32(&bytes[44], c34f_crc32c(page->oob, C34F_OOB_BYTES));
    c34f_put_u32(&bytes[48], 0);
    c34f_put_u32(&bytes[52], C34F_MARKER);
    memcpy(&bytes[64], page->main, C34F_MAIN_BYTES);
    memcpy(&bytes[160], page->oob, C34F_OOB_BYTES);
    c34f_put_u32(&bytes[480], C34F_PAGE_MAGIC);
    bytes[484] = page_index;
    bytes[485] = slot;
    c34f_put_u64(&bytes[488], generation);
    c34f_put_u32(&bytes[496], 0);
    c34f_put_u32(&bytes[500], C34F_MARKER);
    *candidate_hash = c34f_crc32c(bytes, sizeof(bytes));
    c34f_put_u32(&bytes[48], *candidate_hash);
    c34f_put_u32(&bytes[496], *candidate_hash);
    return c34f_write(media, page_candidate_offset(page_index, slot), bytes,
                       sizeof(bytes));
}

enum c34_file_result c34f_load_page_candidate(
    struct c34_file_media *media,
    uint8_t page_index,
    uint8_t slot,
    uint32_t expected_hash,
    struct c34f_page *page
)
{
    uint8_t bytes[C34F_PAGE_SLOT_BYTES];
    uint8_t copy[C34F_PAGE_SLOT_BYTES];
    enum c34_file_result result;

    if (media == NULL || page == NULL || page_index >= C34F_PAGES ||
        slot >= 2) {
        return C34_FILE_INVALID;
    }
    result = c34f_read(media, page_candidate_offset(page_index, slot), bytes,
                       sizeof(bytes));
    if (result != C34_FILE_OK) {
        return result;
    }
    memcpy(copy, bytes, sizeof(copy));
    c34f_put_u32(&copy[48], 0);
    c34f_put_u32(&copy[496], 0);
    if (c34f_get_u32(&bytes[0]) != C34F_PAGE_MAGIC ||
        c34f_get_u16(&bytes[4]) != C34_FILE_FORMAT_VERSION ||
        c34f_get_u16(&bytes[6]) != C34F_PAGE_SLOT_BYTES ||
        bytes[8] != page_index || bytes[9] != slot ||
        c34f_get_u32(&bytes[48]) != expected_hash ||
        c34f_get_u32(&bytes[496]) != expected_hash ||
        c34f_crc32c(copy, sizeof(copy)) != expected_hash ||
        c34f_get_u32(&bytes[52]) != C34F_MARKER ||
        c34f_get_u32(&bytes[480]) != C34F_PAGE_MAGIC ||
        bytes[484] != page_index || bytes[485] != slot ||
        c34f_get_u64(&bytes[12]) != c34f_get_u64(&bytes[488]) ||
        c34f_get_u32(&bytes[500]) != C34F_MARKER ||
        c34f_get_u32(&bytes[40]) != c34f_crc32c(&bytes[64], C34F_MAIN_BYTES) ||
        c34f_get_u32(&bytes[44]) != c34f_crc32c(&bytes[160], C34F_OOB_BYTES)) {
        return C34_FILE_CORRUPT;
    }
    memset(page, 0, sizeof(*page));
    memcpy(page->main, &bytes[64], C34F_MAIN_BYTES);
    memcpy(page->oob, &bytes[160], C34F_OOB_BYTES);
    page->info.version = FWLAB_NFC_CONTRACT_VERSION;
    page->info.size = sizeof(page->info);
    page->info.state = bytes[10];
    page->info.program_count = bytes[11];
    page->info.erase_generation_seen = c34f_get_u16(&bytes[20]);
    return page->info.state <= FWLAB_NAND_PAGE_TORN &&
                   page->info.program_count <= 1 ?
               C34_FILE_OK : C34_FILE_CORRUPT;
}

static uint64_t health_candidate_offset(uint8_t block, uint8_t slot)
{
    return C34F_HEALTH_OFFSET +
           ((uint64_t)block * 2u + slot) * C34F_HEALTH_SLOT_BYTES;
}

enum c34_file_result c34f_write_health_candidate(
    struct c34_file_media *media,
    uint8_t block,
    uint8_t slot,
    uint64_t source_op_id,
    uint64_t begin_lsn,
    const struct fwlab_nand_block_info *health,
    uint32_t *candidate_hash
)
{
    uint8_t bytes[C34F_HEALTH_SLOT_BYTES];
    uint64_t generation;

    if (media == NULL || health == NULL || candidate_hash == NULL ||
        block >= C34F_BLOCKS || slot >= 2) {
        return C34_FILE_INVALID;
    }
    generation = ++media->health_slot_generation[block][slot];
    memset(bytes, 0, sizeof(bytes));
    c34f_put_u32(&bytes[0], C34F_HEALTH_MAGIC);
    c34f_put_u16(&bytes[4], C34_FILE_FORMAT_VERSION);
    c34f_put_u16(&bytes[6], C34F_HEALTH_SLOT_BYTES);
    bytes[8] = block;
    bytes[9] = slot;
    c34f_put_u64(&bytes[12], generation);
    c34f_put_u64(&bytes[20], source_op_id);
    c34f_put_u64(&bytes[28], begin_lsn);
    encode_block_info(&bytes[64], health);
    c34f_put_u32(&bytes[48], 0);
    c34f_put_u32(&bytes[52], C34F_MARKER);
    c34f_put_u32(&bytes[224], C34F_HEALTH_MAGIC);
    bytes[228] = block;
    bytes[229] = slot;
    c34f_put_u64(&bytes[232], generation);
    c34f_put_u32(&bytes[240], 0);
    c34f_put_u32(&bytes[244], C34F_MARKER);
    *candidate_hash = c34f_crc32c(bytes, sizeof(bytes));
    c34f_put_u32(&bytes[48], *candidate_hash);
    c34f_put_u32(&bytes[240], *candidate_hash);
    return c34f_write(media, health_candidate_offset(block, slot), bytes,
                       sizeof(bytes));
}

enum c34_file_result c34f_load_health_candidate(
    struct c34_file_media *media,
    uint8_t block,
    uint8_t slot,
    uint32_t expected_hash,
    struct fwlab_nand_block_info *health
)
{
    uint8_t bytes[C34F_HEALTH_SLOT_BYTES];
    uint8_t copy[C34F_HEALTH_SLOT_BYTES];
    enum c34_file_result result;

    if (media == NULL || health == NULL || block >= C34F_BLOCKS || slot >= 2) {
        return C34_FILE_INVALID;
    }
    result = c34f_read(media, health_candidate_offset(block, slot), bytes,
                       sizeof(bytes));
    if (result != C34_FILE_OK) {
        return result;
    }
    memcpy(copy, bytes, sizeof(copy));
    c34f_put_u32(&copy[48], 0);
    c34f_put_u32(&copy[240], 0);
    if (c34f_get_u32(&bytes[0]) != C34F_HEALTH_MAGIC ||
        c34f_get_u16(&bytes[4]) != C34_FILE_FORMAT_VERSION ||
        c34f_get_u16(&bytes[6]) != C34F_HEALTH_SLOT_BYTES ||
        bytes[8] != block || bytes[9] != slot ||
        c34f_get_u32(&bytes[48]) != expected_hash ||
        c34f_get_u32(&bytes[240]) != expected_hash ||
        c34f_crc32c(copy, sizeof(copy)) != expected_hash ||
        c34f_get_u32(&bytes[52]) != C34F_MARKER ||
        c34f_get_u32(&bytes[224]) != C34F_HEALTH_MAGIC ||
        bytes[228] != block || bytes[229] != slot ||
        c34f_get_u64(&bytes[12]) != c34f_get_u64(&bytes[232]) ||
        c34f_get_u32(&bytes[244]) != C34F_MARKER ||
        !decode_block_info(&bytes[64], health)) {
        return C34_FILE_CORRUPT;
    }
    return C34_FILE_OK;
}
