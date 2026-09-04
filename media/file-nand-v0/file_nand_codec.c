/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "file_nand_internal.h"

#include <string.h>

uint16_t file_nand_get_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

uint32_t file_nand_get_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

uint64_t file_nand_get_le64(const uint8_t *bytes)
{
    uint64_t value = 0;
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

void file_nand_put_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

void file_nand_put_le32(uint8_t *bytes, uint32_t value)
{
    unsigned int index;

    for (index = 0; index < 4; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

void file_nand_put_le64(uint8_t *bytes, uint64_t value)
{
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

uint32_t file_nand_crc32c(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_MAX;
    size_t index;

    for (index = 0; index < size; ++index) {
        unsigned int bit;

        crc ^= bytes[index];
        for (bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & UINT32_C(1));

            crc = (crc >> 1) ^ (UINT32_C(0x82f63b78) & mask);
        }
    }
    return ~crc;
}

uint64_t file_nand_hash64(const uint8_t *bytes, size_t size)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    for (index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

int file_nand_bytes_zero(const void *value, size_t size)
{
    const uint8_t *bytes = value;
    size_t index;

    if (value == NULL) {
        return 0;
    }
    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

struct fwlab_nfc_geometry fwlab_file_nand_v0_geometry(void)
{
    struct fwlab_nfc_geometry geometry;

    memset(&geometry, 0, sizeof(geometry));
    geometry.version = FWLAB_NFC_CONTRACT_VERSION;
    geometry.size = (uint16_t)sizeof(geometry);
    geometry.channels = 1;
    geometry.luns_per_channel = 1;
    geometry.planes_per_lun = 1;
    geometry.blocks_per_plane = FILE_NAND_BLOCK_COUNT;
    geometry.pages_per_block = FILE_NAND_PAGES_PER_BLOCK;
    geometry.plane_parallelism_per_lun = 1;
    geometry.main_bytes_per_page = FILE_NAND_MAIN_BYTES;
    geometry.oob_bytes_per_page = FILE_NAND_OOB_BYTES;
    geometry.max_programs_per_erase = 1;
    geometry.program_order = FWLAB_NFC_PROGRAM_ASCENDING;
    return geometry;
}

int file_nand_geometry_valid(const struct fwlab_nfc_geometry *geometry)
{
    const struct fwlab_nfc_geometry expected =
        fwlab_file_nand_v0_geometry();

    return geometry != NULL && memcmp(geometry, &expected, sizeof(expected)) == 0;
}

int file_nand_ppa_valid(const struct fwlab_nfc_ppa *ppa)
{
    return ppa != NULL && ppa->channel == 0 && ppa->lun == 0 &&
           ppa->plane == 0 && ppa->block < FILE_NAND_BLOCK_COUNT &&
           ppa->page < FILE_NAND_PAGES_PER_BLOCK && ppa->reserved == 0;
}

uint16_t file_nand_linear_page(const struct fwlab_nfc_ppa *ppa)
{
    return (uint16_t)(ppa->block * FILE_NAND_PAGES_PER_BLOCK + ppa->page);
}

uint64_t file_nand_page_slot_offset(uint16_t linear_page, uint8_t slot)
{
    return FILE_NAND_PAGE_CANDIDATES +
           ((uint64_t)linear_page * 2u + slot) * FILE_NAND_PAGE_SLOT_BYTES;
}

uint64_t file_nand_health_slot_offset(uint8_t block, uint8_t slot)
{
    return FILE_NAND_HEALTH_CANDIDATES +
           ((uint64_t)block * 2u + slot) * FILE_NAND_HEALTH_SLOT_BYTES;
}

static uint32_t object_crc(uint8_t *bytes, size_t size, size_t crc_offset,
                           size_t marker_offset)
{
    uint8_t saved_crc[4];
    uint8_t saved_marker[4];
    uint32_t crc;

    memcpy(saved_crc, &bytes[crc_offset], sizeof(saved_crc));
    memcpy(saved_marker, &bytes[marker_offset], sizeof(saved_marker));
    memset(&bytes[crc_offset], 0, sizeof(saved_crc));
    memset(&bytes[marker_offset], 0, sizeof(saved_marker));
    crc = file_nand_crc32c(bytes, size);
    memcpy(&bytes[crc_offset], saved_crc, sizeof(saved_crc));
    memcpy(&bytes[marker_offset], saved_marker, sizeof(saved_marker));
    return crc;
}

enum fwlab_nfc_api_result file_nand_encode_page_candidate(
    uint8_t bytes[8192], uint64_t generation, uint64_t transaction_uid,
    const struct fwlab_nfc_ppa *ppa, uint8_t slot, uint8_t state,
    uint8_t program_count, uint16_t erase_generation,
    const uint8_t main[4096], const uint8_t oob[128],
    uint32_t *candidate_crc)
{
    uint32_t crc;

    if (bytes == NULL || generation == 0 || ppa == NULL ||
        !file_nand_ppa_valid(ppa) || slot > 1 ||
        state > FWLAB_NAND_PAGE_TORN || program_count > 1 || main == NULL ||
        oob == NULL || candidate_crc == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(bytes, 0, FILE_NAND_PAGE_SLOT_BYTES);
    file_nand_put_le32(&bytes[0], FILE_NAND_PAGE_MAGIC);
    file_nand_put_le16(&bytes[4], FWLAB_FILE_NAND_V0_VERSION);
    file_nand_put_le16(&bytes[6], 128);
    file_nand_put_le64(&bytes[8], generation);
    file_nand_put_le64(&bytes[16], transaction_uid);
    file_nand_put_le16(&bytes[24], ppa->channel);
    file_nand_put_le16(&bytes[26], ppa->lun);
    file_nand_put_le16(&bytes[28], ppa->plane);
    file_nand_put_le16(&bytes[30], ppa->block);
    file_nand_put_le16(&bytes[32], ppa->page);
    bytes[34] = slot;
    bytes[35] = state;
    bytes[36] = program_count;
    file_nand_put_le16(&bytes[38], erase_generation);
    file_nand_put_le32(&bytes[40], file_nand_crc32c(main, 4096));
    file_nand_put_le32(&bytes[44], file_nand_crc32c(oob, 128));
    memcpy(&bytes[128], main, 4096);
    memcpy(&bytes[4224], oob, 128);
    crc = object_crc(bytes, FILE_NAND_PAGE_SLOT_BYTES, 48, 124);
    file_nand_put_le32(&bytes[48], crc);
    file_nand_put_le32(&bytes[124], FILE_NAND_PHASE_MARKER);
    *candidate_crc = crc;
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result file_nand_decode_page_candidate(
    const uint8_t bytes[8192], uint16_t expected_linear_page,
    uint8_t expected_slot, struct fwlab_nand_page_info *page,
    uint8_t main[4096], uint8_t oob[128], uint64_t *generation,
    uint64_t *transaction_uid, uint32_t *candidate_crc)
{
    uint8_t copy[8192];
    struct fwlab_nfc_ppa ppa;
    uint32_t stored_crc;

    if (bytes == NULL || expected_linear_page >= FILE_NAND_PAGE_COUNT ||
        expected_slot > 1 || page == NULL || main == NULL || oob == NULL ||
        generation == NULL || transaction_uid == NULL || candidate_crc == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memcpy(copy, bytes, sizeof(copy));
    ppa.channel = file_nand_get_le16(&copy[24]);
    ppa.lun = file_nand_get_le16(&copy[26]);
    ppa.plane = file_nand_get_le16(&copy[28]);
    ppa.block = file_nand_get_le16(&copy[30]);
    ppa.page = file_nand_get_le16(&copy[32]);
    ppa.reserved = 0;
    stored_crc = file_nand_get_le32(&copy[48]);
    if (file_nand_get_le32(&copy[0]) != FILE_NAND_PAGE_MAGIC ||
        file_nand_get_le16(&copy[4]) != FWLAB_FILE_NAND_V0_VERSION ||
        file_nand_get_le16(&copy[6]) != 128 ||
        file_nand_get_le64(&copy[8]) == 0 || !file_nand_ppa_valid(&ppa) ||
        file_nand_linear_page(&ppa) != expected_linear_page ||
        copy[34] != expected_slot || copy[35] > FWLAB_NAND_PAGE_TORN ||
        copy[36] > 1 || copy[37] != 0 ||
        !file_nand_bytes_zero(&copy[52], 72) ||
        file_nand_get_le32(&copy[124]) != FILE_NAND_PHASE_MARKER ||
        !file_nand_bytes_zero(&copy[4352], 3840) ||
        stored_crc != object_crc(copy, sizeof(copy), 48, 124) ||
        file_nand_get_le32(&copy[40]) !=
            file_nand_crc32c(&copy[128], 4096) ||
        file_nand_get_le32(&copy[44]) !=
            file_nand_crc32c(&copy[4224], 128)) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    memset(page, 0, sizeof(*page));
    page->version = FWLAB_NFC_CONTRACT_VERSION;
    page->size = (uint16_t)sizeof(*page);
    page->state = copy[35];
    page->program_count = copy[36];
    page->erase_generation_seen = file_nand_get_le16(&copy[38]);
    memcpy(main, &copy[128], 4096);
    memcpy(oob, &copy[4224], 128);
    *generation = file_nand_get_le64(&copy[8]);
    *transaction_uid = file_nand_get_le64(&copy[16]);
    *candidate_crc = stored_crc;
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result file_nand_encode_health_candidate(
    uint8_t bytes[256], uint64_t generation, uint64_t transaction_uid,
    uint8_t block, uint8_t slot, const struct fwlab_nand_block_info *info,
    uint32_t *candidate_crc)
{
    uint32_t crc;

    if (bytes == NULL || generation == 0 || block >= FILE_NAND_BLOCK_COUNT ||
        slot > 1 || info == NULL || candidate_crc == NULL ||
        info->version != FWLAB_NFC_CONTRACT_VERSION ||
        info->size != sizeof(*info) || info->health > FWLAB_NFC_BLOCK_RUNTIME_BAD ||
        info->erase_state > FWLAB_NAND_ERASE_TORN ||
        info->next_program_page > FILE_NAND_PAGES_PER_BLOCK) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(bytes, 0, FILE_NAND_HEALTH_SLOT_BYTES);
    file_nand_put_le32(&bytes[0], FILE_NAND_HEALTH_MAGIC);
    file_nand_put_le16(&bytes[4], FWLAB_FILE_NAND_V0_VERSION);
    file_nand_put_le16(&bytes[6], 64);
    file_nand_put_le64(&bytes[8], generation);
    file_nand_put_le64(&bytes[16], transaction_uid);
    file_nand_put_le16(&bytes[30], block);
    bytes[32] = slot;
    bytes[33] = info->health;
    bytes[34] = info->erase_state;
    file_nand_put_le16(&bytes[36], info->erase_generation);
    file_nand_put_le16(&bytes[38], info->successful_erase_count);
    file_nand_put_le16(&bytes[40], info->erase_attempt_count);
    file_nand_put_le16(&bytes[42], info->next_program_page);
    file_nand_put_le32(&bytes[44], file_nand_crc32c(&bytes[32], 12));
    crc = object_crc(bytes, sizeof(uint8_t) * 256, 48, 252);
    file_nand_put_le32(&bytes[48], crc);
    file_nand_put_le32(&bytes[252], FILE_NAND_PHASE_MARKER);
    *candidate_crc = crc;
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result file_nand_decode_health_candidate(
    const uint8_t bytes[256], uint8_t expected_block, uint8_t expected_slot,
    struct fwlab_nand_block_info *info, uint64_t *generation,
    uint64_t *transaction_uid, uint32_t *candidate_crc)
{
    uint8_t copy[256];
    uint32_t stored_crc;

    if (bytes == NULL || expected_block >= FILE_NAND_BLOCK_COUNT ||
        expected_slot > 1 || info == NULL || generation == NULL ||
        transaction_uid == NULL || candidate_crc == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memcpy(copy, bytes, sizeof(copy));
    stored_crc = file_nand_get_le32(&copy[48]);
    if (file_nand_get_le32(&copy[0]) != FILE_NAND_HEALTH_MAGIC ||
        file_nand_get_le16(&copy[4]) != FWLAB_FILE_NAND_V0_VERSION ||
        file_nand_get_le16(&copy[6]) != 64 ||
        file_nand_get_le64(&copy[8]) == 0 ||
        !file_nand_bytes_zero(&copy[24], 6) ||
        file_nand_get_le16(&copy[30]) != expected_block ||
        copy[32] != expected_slot ||
        copy[33] > FWLAB_NFC_BLOCK_RUNTIME_BAD ||
        copy[34] > FWLAB_NAND_ERASE_TORN || copy[35] != 0 ||
        file_nand_get_le16(&copy[42]) > FILE_NAND_PAGES_PER_BLOCK ||
        file_nand_get_le32(&copy[44]) !=
            file_nand_crc32c(&copy[32], 12) ||
        !file_nand_bytes_zero(&copy[52], 200) ||
        file_nand_get_le32(&copy[252]) != FILE_NAND_PHASE_MARKER ||
        stored_crc != object_crc(copy, sizeof(copy), 48, 252)) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    memset(info, 0, sizeof(*info));
    info->version = FWLAB_NFC_CONTRACT_VERSION;
    info->size = (uint16_t)sizeof(*info);
    info->health = copy[33];
    info->erase_state = copy[34];
    info->erase_generation = file_nand_get_le16(&copy[36]);
    info->successful_erase_count = file_nand_get_le16(&copy[38]);
    info->erase_attempt_count = file_nand_get_le16(&copy[40]);
    info->next_program_page = file_nand_get_le16(&copy[42]);
    *generation = file_nand_get_le64(&copy[8]);
    *transaction_uid = file_nand_get_le64(&copy[16]);
    *candidate_crc = stored_crc;
    return FWLAB_NFC_API_OK;
}
