/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_file_oracle.h"

#include <string.h>

static uint16_t get_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)(bytes[1] << 8));
}

static uint32_t get_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t get_u64(const uint8_t *bytes)
{
    uint64_t value = 0;
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static uint32_t crc32c(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;
    size_t index;

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

static uint64_t hash_bytes(uint64_t hash, const uint8_t *bytes, size_t length)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

struct super {
    uint8_t valid;
    uint8_t cp_slot;
    uint64_t generation;
    uint64_t cp_generation;
    uint32_t cp_hash;
};

static int decode_super(
    const uint8_t *bytes,
    const uint8_t uuid[16],
    struct super *super
)
{
    uint8_t header[128];

    memset(super, 0, sizeof(*super));
    memcpy(header, bytes, sizeof(header));
    put_u32(&header[120], 0);
    if (get_u32(&bytes[0]) != C34F_SB_MAGIC ||
        get_u16(&bytes[4]) != C34_FILE_FORMAT_VERSION ||
        get_u16(&bytes[6]) != 128 || bytes[16] >= 2 ||
        get_u64(&bytes[8]) == 0 || get_u64(&bytes[24]) == 0 ||
        get_u32(&bytes[52]) != C34_FILE_IMAGE_BYTES ||
        memcmp(&bytes[64], uuid, 16) != 0 ||
        get_u32(&bytes[120]) != crc32c(header, sizeof(header)) ||
        get_u32(&bytes[124]) != C34F_MARKER) {
        return 0;
    }
    super->valid = 1;
    super->cp_slot = bytes[16];
    super->generation = get_u64(&bytes[8]);
    super->cp_generation = get_u64(&bytes[24]);
    super->cp_hash = get_u32(&bytes[32]);
    return 1;
}

static int checkpoint_page0(
    const uint8_t *checkpoint,
    const struct super *super,
    const uint8_t uuid[16],
    struct c34fo_page0 *page
)
{
    uint8_t header[C34F_WAL_HEADER_BYTES];
    const uint8_t *body = &checkpoint[C34F_WAL_HEADER_BYTES];

    memcpy(header, checkpoint, sizeof(header));
    put_u32(&header[100], 0);
    if (crc32c(checkpoint, C34F_CHECKPOINT_BYTES) != super->cp_hash ||
        get_u32(&checkpoint[0]) != C34F_CP_MAGIC ||
        get_u64(&checkpoint[8]) != super->cp_generation ||
        memcmp(&checkpoint[56], uuid, 16) != 0 ||
        get_u32(&checkpoint[100]) != crc32c(header, sizeof(header)) ||
        get_u32(&checkpoint[104]) != C34F_MARKER ||
        get_u32(&checkpoint[8160]) != C34F_CP_MAGIC ||
        get_u64(&checkpoint[8168]) != super->cp_generation ||
        get_u32(&checkpoint[8176]) != crc32c(checkpoint, 8160) ||
        get_u32(&checkpoint[8180]) != C34F_MARKER) {
        return 0;
    }
    memcpy(page->main, &body[0], C34F_MAIN_BYTES);
    memcpy(page->oob, &body[C34F_MAIN_BYTES], C34F_OOB_BYTES);
    page->erase_generation = get_u16(&body[160]);
    page->state = body[162];
    page->program_count = body[163];
    return page->state <= FWLAB_NAND_PAGE_TORN && page->program_count <= 1;
}

static int apply_begin_candidate(
    const uint8_t image[C34_FILE_IMAGE_BYTES],
    struct c34fo_page0 *page
)
{
    const uint8_t *record =
        &image[C34F_WAL0_OFFSET + C34F_WAL_HEADER_BYTES];
    uint8_t record_copy[C34F_WAL_RECORD_BYTES];
    uint8_t slot;
    uint32_t candidate_hash;
    const uint8_t *candidate;
    uint8_t candidate_copy[C34F_PAGE_SLOT_BYTES];

    if (get_u32(&record[0]) != C34F_REC_MAGIC) {
        return 1;
    }
    memcpy(record_copy, record, sizeof(record_copy));
    put_u32(&record_copy[240], 0);
    if (get_u16(&record[4]) != C34_FILE_FORMAT_VERSION ||
        record[6] != C34F_WAL_BEGIN ||
        get_u32(&record[240]) != crc32c(record_copy, sizeof(record_copy)) ||
        get_u32(&record[244]) != C34F_MARKER ||
        get_u64(&record[8]) != get_u64(&record[248])) {
        return 1;
    }
    if (record[128] == 0 || record[152] != 0 || record[133] !=
            FWLAB_NFC_PHYS_APPLIED) {
        return 1;
    }
    slot = record[156];
    candidate_hash = get_u32(&record[160]);
    if (slot >= 2) {
        return 0;
    }
    candidate = &image[C34F_PAGE_OFFSET + slot * C34F_PAGE_SLOT_BYTES];
    memcpy(candidate_copy, candidate, sizeof(candidate_copy));
    put_u32(&candidate_copy[48], 0);
    put_u32(&candidate_copy[496], 0);
    if (get_u32(&candidate[0]) != C34F_PAGE_MAGIC || candidate[8] != 0 ||
        candidate[9] != slot || get_u32(&candidate[48]) != candidate_hash ||
        get_u32(&candidate[496]) != candidate_hash ||
        crc32c(candidate_copy, sizeof(candidate_copy)) != candidate_hash ||
        get_u32(&candidate[52]) != C34F_MARKER ||
        get_u32(&candidate[500]) != C34F_MARKER) {
        return 0;
    }
    memcpy(page->main, &candidate[64], C34F_MAIN_BYTES);
    memcpy(page->oob, &candidate[160], C34F_OOB_BYTES);
    page->state = candidate[10];
    page->program_count = candidate[11];
    page->erase_generation = get_u16(&candidate[20]);
    return 1;
}

int c34fo_recover_page0(
    const uint8_t image[C34_FILE_IMAGE_BYTES],
    const uint8_t uuid[16],
    struct c34fo_page0 *page
)
{
    struct super super[2];
    int valid[2];
    int selected;
    const uint8_t *checkpoint;
    uint8_t fields[4];

    if (image == NULL || uuid == NULL || page == NULL) {
        return 0;
    }
    valid[0] = decode_super(&image[C34F_SB0_OFFSET], uuid, &super[0]);
    valid[1] = decode_super(&image[C34F_SB1_OFFSET], uuid, &super[1]);
    if (!valid[0] && !valid[1]) {
        return 0;
    }
    selected = valid[1] &&
                       (!valid[0] || super[1].generation > super[0].generation) ?
                   1 : 0;
    memset(page, 0, sizeof(*page));
    checkpoint = &image[super[selected].cp_slot == 0 ?
                            C34F_CP0_OFFSET : C34F_CP1_OFFSET];
    if (!checkpoint_page0(checkpoint, &super[selected], uuid, page) ||
        !apply_begin_candidate(image, page)) {
        return 0;
    }
    page->hash = hash_bytes(UINT64_C(1469598103934665603), page->main,
                            C34F_MAIN_BYTES);
    page->hash = hash_bytes(page->hash, page->oob, C34F_OOB_BYTES);
    fields[0] = page->state;
    fields[1] = page->program_count;
    fields[2] = (uint8_t)page->erase_generation;
    fields[3] = (uint8_t)(page->erase_generation >> 8);
    page->hash = hash_bytes(page->hash, fields, sizeof(fields));
    return 1;
}
