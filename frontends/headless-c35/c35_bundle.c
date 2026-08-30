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

static void endpoint_descriptor_make(
    struct c35_endpoint_descriptor *descriptor,
    uint32_t role,
    const struct c35_profile_descriptor *profile,
    uint64_t coherence_cookie
)
{
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->version = C35_ENDPOINT_VERSION;
    descriptor->size = sizeof(*descriptor);
    descriptor->role = role;
    descriptor->media_profile_id = profile->media_profile_id;
    descriptor->geometry_id = profile->geometry_id;
    descriptor->coherence_cookie = coherence_cookie;
    descriptor->profile = *profile;
}

void c35_media_endpoint_make(
    struct c35_media_endpoint *endpoint,
    const struct c35_profile_descriptor *profile,
    uint64_t coherence_cookie,
    const struct fwlab_nand_media *provider
)
{
    memset(endpoint, 0, sizeof(*endpoint));
    if (profile == NULL || provider == NULL) return;
    endpoint_descriptor_make(
        &endpoint->descriptor, C35_ENDPOINT_RAW_MEDIA, profile,
        coherence_cookie);
    endpoint->provider = *provider;
}

void c35_physical_endpoint_make(
    struct c35_physical_endpoint *endpoint,
    const struct c35_profile_descriptor *profile,
    uint64_t coherence_cookie,
    const struct c34_physical_txn_provider *provider
)
{
    memset(endpoint, 0, sizeof(*endpoint));
    if (profile == NULL || provider == NULL) return;
    endpoint_descriptor_make(
        &endpoint->descriptor, C35_ENDPOINT_PHYSICAL_TXN, profile,
        coherence_cookie);
    endpoint->provider = *provider;
}

static int endpoint_valid(
    const struct c35_endpoint_descriptor *descriptor,
    uint32_t role
)
{
    return descriptor != NULL &&
           descriptor->version == C35_ENDPOINT_VERSION &&
           descriptor->size == sizeof(*descriptor) &&
           descriptor->reserved == 0 && descriptor->role == role &&
           descriptor->feature_bits == 0 &&
           descriptor->media_profile_id != 0 &&
           descriptor->geometry_id != 0 &&
           descriptor->coherence_cookie != 0 &&
           c35_profile_valid(&descriptor->profile) &&
           descriptor->media_profile_id ==
               descriptor->profile.media_profile_id &&
           descriptor->geometry_id == descriptor->profile.geometry_id;
}

static int media_valid(const struct fwlab_nand_media *media)
{
    return media != NULL && media->ops != NULL && media->context != NULL &&
           media->ops->version == FWLAB_NFC_CONTRACT_VERSION &&
           media->ops->size == sizeof(*media->ops) &&
           media->ops->reserved == 0 && media->ops->read_page != NULL &&
           media->ops->program != NULL && media->ops->erase != NULL &&
           media->ops->mark_runtime_bad != NULL && media->ops->hash != NULL;
}

static int physical_valid(const struct c34_physical_txn_provider *physical)
{
    return physical != NULL && physical->ops != NULL &&
           physical->context != NULL &&
           physical->ops->version == C34_PHYSICAL_TXN_VERSION &&
           physical->ops->size == sizeof(*physical->ops) &&
           physical->ops->reserved == 0 && physical->ops->bind != NULL &&
           physical->ops->abandon != NULL && physical->ops->receipt != NULL &&
           physical->ops->quiescent != NULL;
}

enum c35_result c35_bundle_init(
    struct c35_bundle *bundle,
    const struct c35_media_endpoint *media,
    const struct c35_physical_endpoint *physical
)
{
    bool quiescent;

    if (bundle == NULL || media == NULL || physical == NULL ||
        !endpoint_valid(&media->descriptor, C35_ENDPOINT_RAW_MEDIA) ||
        !endpoint_valid(
            &physical->descriptor, C35_ENDPOINT_PHYSICAL_TXN) ||
        media->descriptor.media_profile_id !=
            physical->descriptor.media_profile_id ||
        media->descriptor.geometry_id != physical->descriptor.geometry_id ||
        media->descriptor.coherence_cookie !=
            physical->descriptor.coherence_cookie ||
        memcmp(media->descriptor.profile.geometry_wire,
               physical->descriptor.profile.geometry_wire,
               C35_GEOMETRY_WIRE_BYTES) != 0 ||
        memcmp(media->descriptor.profile.media_wire,
               physical->descriptor.profile.media_wire,
               C35_MEDIA_WIRE_BYTES) != 0 ||
        !media_valid(&media->provider) ||
        !physical_valid(&physical->provider) ||
        media->provider.context != physical->provider.context ||
        physical->provider.ops->quiescent(
            physical->provider.context, &quiescent) != C34_PHYSICAL_TXN_OK ||
        !quiescent) {
        return C35_INVALID;
    }
    memset(bundle, 0, sizeof(*bundle));
    bundle->profile = media->descriptor.profile;
    bundle->media = media->provider;
    bundle->physical = physical->provider;
    return C35_OK;
}

enum c35_result c35_bundle_claim(struct c35_bundle *bundle, uint64_t claimant)
{
    if (bundle == NULL || !c35_profile_valid(&bundle->profile) ||
        claimant == 0) return C35_INVALID;
    if (bundle->claimed) return C35_WRONG_STATE;
    bundle->claimed = 1;
    bundle->claimant = claimant;
    bundle->last_released_claimant = 0;
    return C35_OK;
}

enum c35_result c35_bundle_release(struct c35_bundle *bundle, uint64_t claimant)
{
    bool quiescent;
    enum c34_physical_txn_result result;

    if (bundle == NULL || claimant == 0) return C35_INVALID;
    if (!bundle->claimed) {
        return bundle->last_released_claimant == claimant ?
            C35_OK : C35_STALE;
    }
    if (bundle->claimant != claimant) return C35_STALE;
    result = bundle->physical.ops->quiescent(
        bundle->physical.context, &quiescent);
    if (result != C34_PHYSICAL_TXN_OK) return C35_PROVIDER_FAILURE;
    if (!quiescent) return C35_IN_PROGRESS;
    bundle->claimed = 0;
    bundle->claimant = 0;
    bundle->last_released_claimant = claimant;
    return C35_OK;
}

enum c35_result c35_bundle_release_query(
    const struct c35_bundle *bundle,
    uint64_t claimant,
    bool *released
)
{
    if (bundle == NULL || claimant == 0 || released == NULL)
        return C35_INVALID;
    if (bundle->claimed && bundle->claimant == claimant) {
        *released = false;
        return C35_OK;
    }
    if (!bundle->claimed && bundle->last_released_claimant == claimant) {
        *released = true;
        return C35_OK;
    }
    return C35_STALE;
}

enum c35_result c35_bundle_projection(
    const struct c35_bundle *bundle,
    uint8_t bytes[C35_RAW_PROJECTION_BYTES]
)
{
    uint32_t offset = 16;
    uint16_t block;

    if (bundle == NULL || bytes == NULL || !bundle->claimed ||
        !c35_profile_valid(&bundle->profile)) return C35_INVALID;
    memset(bytes, 0, C35_RAW_PROJECTION_BYTES);
    bytes[0] = 'C'; bytes[1] = '3'; bytes[2] = '5'; bytes[3] = 'R';
    put_u16(&bytes[4], C35_BUNDLE_VERSION);
    put_u16(&bytes[6], C35_RAW_PROJECTION_BYTES);
    put_u32(&bytes[8], bundle->profile.geometry_id);
    put_u32(&bytes[12], bundle->profile.media_profile_id);
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
                FWLAB_NFC_API_OK) return C35_PROVIDER_FAILURE;
            offset += C35_MAIN_BYTES + C35_OOB_BYTES;
            put_u16(&bytes[offset], page_info.erase_generation_seen);
            bytes[offset + 2u] = page_info.state;
            bytes[offset + 3u] = page_info.program_count;
            offset += 16;
            if (page + 1u == C35_PAGES_PER_BLOCK) {
                put_u16(&bytes[offset], block_info.erase_generation);
                put_u16(&bytes[offset + 2u],
                         block_info.successful_erase_count);
                put_u16(&bytes[offset + 4u],
                         block_info.erase_attempt_count);
                put_u16(&bytes[offset + 6u], block_info.next_program_page);
                bytes[offset + 8u] = block_info.health;
                bytes[offset + 9u] = block_info.erase_state;
                offset += 32;
            }
        }
    }
    return offset == C35_RAW_PROJECTION_BYTES ? C35_OK : C35_INVARIANT;
}
