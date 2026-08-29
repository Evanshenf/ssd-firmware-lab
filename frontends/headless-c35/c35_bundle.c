/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_bundle.h"

#include <string.h>

#define C35_MAIN_BYTES 96u
#define C35_OOB_BYTES 64u
#define C35_BLOCKS 6u
#define C35_PAGES_PER_BLOCK 4u

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

enum c35_result c35_bundle_init(
    struct c35_bundle *bundle,
    uint64_t owner_cookie,
    uint32_t profile_id,
    uint32_t geometry_id,
    const struct fwlab_nand_media *media,
    const struct c34_physical_txn_provider *physical
)
{
    if (bundle == NULL || owner_cookie == 0 || media == NULL ||
        media->ops == NULL || media->context == NULL || physical == NULL ||
        physical->ops == NULL || physical->context == NULL ||
        media->context != physical->context ||
        media->ops->version != FWLAB_NFC_CONTRACT_VERSION ||
        physical->ops->version != C34_PHYSICAL_TXN_VERSION) {
        return C35_INVALID;
    }
    memset(bundle, 0, sizeof(*bundle));
    bundle->owner_cookie = owner_cookie;
    bundle->profile_id = profile_id;
    bundle->geometry_id = geometry_id;
    bundle->media = *media;
    bundle->physical = *physical;
    return C35_OK;
}

enum c35_result c35_bundle_claim(struct c35_bundle *bundle, uint64_t claimant)
{
    if (bundle == NULL || bundle->owner_cookie == 0 || claimant == 0) {
        return C35_INVALID;
    }
    if (bundle->claimed) {
        return C35_WRONG_STATE;
    }
    bundle->claimed = 1;
    bundle->claimant = claimant;
    return C35_OK;
}

enum c35_result c35_bundle_release(struct c35_bundle *bundle, uint64_t claimant)
{
    bool quiescent;

    if (bundle == NULL || !bundle->claimed || bundle->claimant != claimant ||
        bundle->physical.ops->quiescent(
            bundle->physical.context, &quiescent) != C34_PHYSICAL_TXN_OK ||
        !quiescent) {
        return C35_WRONG_STATE;
    }
    bundle->claimed = 0;
    bundle->claimant = 0;
    return C35_OK;
}

enum c35_result c35_bundle_projection(
    const struct c35_bundle *bundle,
    uint8_t bytes[C35_RAW_PROJECTION_BYTES]
)
{
    uint32_t offset = 16;
    uint16_t block;

    if (bundle == NULL || bytes == NULL || !bundle->claimed) {
        return C35_INVALID;
    }
    memset(bytes, 0, C35_RAW_PROJECTION_BYTES);
    bytes[0] = 'C';
    bytes[1] = '3';
    bytes[2] = '5';
    bytes[3] = 'R';
    put_u16(&bytes[4], C35_BUNDLE_VERSION);
    put_u16(&bytes[6], C35_RAW_PROJECTION_BYTES);
    put_u32(&bytes[8], bundle->geometry_id);
    put_u32(&bytes[12], bundle->profile_id);
    for (block = 0; block < C35_BLOCKS; ++block) {
        uint16_t page;

        for (page = 0; page < C35_PAGES_PER_BLOCK; ++page) {
            struct fwlab_nfc_ppa ppa;
            struct fwlab_nand_page_info page_info;
            struct fwlab_nand_block_info block_info;

            memset(&ppa, 0, sizeof(ppa));
            ppa.block = block;
            ppa.page = page;
            if (bundle->media.ops->read_page(
                    bundle->media.context, &ppa, &bytes[offset],
                    C35_MAIN_BYTES, &bytes[offset + C35_MAIN_BYTES],
                    C35_OOB_BYTES, &page_info, &block_info) !=
                FWLAB_NFC_API_OK) {
                return C35_PROVIDER_FAILURE;
            }
            offset += C35_MAIN_BYTES + C35_OOB_BYTES;
            put_u16(&bytes[offset], page_info.erase_generation_seen);
            bytes[offset + 2u] = page_info.state;
            bytes[offset + 3u] = page_info.program_count;
            offset += 16;
            if (page + 1u == C35_PAGES_PER_BLOCK) {
                put_u16(&bytes[offset], block_info.erase_generation);
                put_u16(&bytes[offset + 2u],
                         block_info.successful_erase_count);
                put_u16(&bytes[offset + 4u], block_info.erase_attempt_count);
                put_u16(&bytes[offset + 6u], block_info.next_program_page);
                bytes[offset + 8u] = block_info.health;
                bytes[offset + 9u] = block_info.erase_state;
                offset += 32;
            }
        }
    }
    return offset == C35_RAW_PROJECTION_BYTES ? C35_OK : C35_INVARIANT;
}
