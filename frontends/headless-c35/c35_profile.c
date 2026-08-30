/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_profile.h"

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

static uint32_t crc32c_add(uint32_t crc, const uint8_t *bytes, uint32_t length)
{
    uint32_t index;

    for (index = 0; index < length; ++index) {
        unsigned int bit;

        crc ^= bytes[index];
        for (bit = 0; bit < 8; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0x82f63b78) & mask);
        }
    }
    return crc;
}

static uint32_t domain_crc(
    const char *domain,
    const uint8_t *wire,
    uint32_t length
)
{
    uint32_t crc = UINT32_MAX;

    crc = crc32c_add(crc, (const uint8_t *)domain, (uint32_t)strlen(domain));
    crc = crc32c_add(crc, wire, length);
    return ~crc;
}

void c35_profile_fixed(struct c35_profile_descriptor *profile)
{
    uint8_t *geometry;
    uint8_t *media;

    if (profile == NULL) return;
    memset(profile, 0, sizeof(*profile));
    geometry = profile->geometry_wire;
    geometry[0] = 'G'; geometry[1] = '3'; geometry[2] = '5'; geometry[3] = 'A';
    put_u16(&geometry[4], C35_PROFILE_VERSION);
    put_u16(&geometry[6], C35_GEOMETRY_WIRE_BYTES);
    put_u16(&geometry[8], 1);  /* channels */
    put_u16(&geometry[10], 1); /* LUNs */
    put_u16(&geometry[12], 1); /* planes */
    put_u16(&geometry[14], 6); /* blocks */
    put_u16(&geometry[16], 4); /* pages */
    put_u16(&geometry[18], 1); /* parallelism */
    put_u16(&geometry[20], 96);
    put_u16(&geometry[22], 64);
    put_u16(&geometry[24], 1); /* programs/erase */
    geometry[26] = 1;          /* ascending */
    profile->geometry_id = domain_crc(
        "C35A-GEOMETRY-v2", geometry, C35_GEOMETRY_WIRE_BYTES);

    media = profile->media_wire;
    media[0] = 'P'; media[1] = '3'; media[2] = '5'; media[3] = 'A';
    put_u16(&media[4], C35_PROFILE_VERSION);
    put_u16(&media[6], C35_MEDIA_WIRE_BYTES);
    memcpy(&media[8], geometry, C35_GEOMETRY_WIRE_BYTES);
    put_u16(&media[40], 1); /* C34 contract */
    put_u16(&media[42], 1); /* C34 file format */
    put_u16(&media[44], 2); /* bundle schema */
    put_u16(&media[46], 2); /* raw projection schema */
    put_u32(&media[48], 65536);
    put_u16(&media[52], 96);
    put_u16(&media[54], 64);
    put_u16(&media[56], 0);  /* reserved: lifetime is not media identity */
    put_u16(&media[58], 6);
    put_u16(&media[60], 4);
    profile->media_profile_id = domain_crc(
        "C35A-MEDIA-v2", media, C35_MEDIA_WIRE_BYTES);
}

int c35_fixed_capacity_dominance_valid(
    const struct fwlab_c31_capacity *c31,
    const struct fwlab_nfc_model_config *nfc,
    uint32_t c34_inner_uid_limit,
    uint32_t c34_physical_op_limit,
    uint32_t c34_physical_sequence_limit,
    uint8_t exclusive_nfc_producer
)
{
    uint64_t cache_required;
    uint64_t trace_required;

    if (c31 == NULL || nfc == NULL ||
        c31->version != FWLAB_C31_CONTRACT_VERSION ||
        c31->size != sizeof(*c31) || c31->reserved0 != 0 ||
        c31->reserved1 != 0 || c31->commands != 2 ||
        c31->abort_tickets != 2 || c31->event_batch != 2 ||
        c31->trace_entries != 256 || c31->scratch_bytes != 256 ||
        c31->slot_generation_limit != 512 ||
        c31->operation_generation_limit != 512 ||
        c31->lease_generation_limit != 512 ||
        c31->ticket_generation_limit != 512 ||
        c31->controller_epoch_limit != 16 ||
        c31->command_uid_limit != 512 ||
        nfc->version != FWLAB_NFC_CONTRACT_VERSION ||
        nfc->size != sizeof(*nfc) || nfc->reserved0 != 0 ||
        nfc->reserved1 != 0 || nfc->reserved2[0] != 0 ||
        nfc->reserved2[1] != 0 ||
        nfc->capacity.version != FWLAB_NFC_CONTRACT_VERSION ||
        nfc->capacity.size != sizeof(nfc->capacity) ||
        nfc->capacity.reserved0 != 0 ||
        nfc->capacity.reserved1[0] != 0 ||
        nfc->capacity.reserved1[1] != 0 ||
        nfc->capacity.operations != 2 ||
        nfc->capacity.request_registry != 2 ||
        nfc->capacity.terminal_events != 2 ||
        nfc->capacity.result_slots != 2 ||
        nfc->capacity.trace_entries != 512 ||
        nfc->capacity.scratch_main_bytes != 96 ||
        nfc->capacity.scratch_oob_bytes != 64 ||
        nfc->capacity.operation_generation_limit != 32 ||
        nfc->capacity.cache_generation_limit != 32 ||
        nfc->capacity.controller_epoch_limit != 17 ||
        nfc->capacity.submit_sequence_limit != 32 ||
        nfc->capacity.operation_uid_limit != 32 ||
        nfc->capacity.virtual_tick_limit != UINT64_C(1000000) ||
        nfc->successful_erase_limit != 4 ||
        c34_inner_uid_limit != 32 || c34_physical_op_limit != 16 ||
        c34_physical_sequence_limit != 16 || exclusive_nfc_producer != 1)
        return 0;

    cache_required = ((uint64_t)c34_inner_uid_limit + 1u) / 2u +
                     ((uint64_t)c31->controller_epoch_limit - 1u) + 1u;
    trace_required = 1u + 9u * (uint64_t)c34_inner_uid_limit +
                     (uint64_t)c31->controller_epoch_limit;
    return nfc->capacity.controller_epoch_limit >=
               (uint64_t)c31->controller_epoch_limit + 1u &&
           nfc->capacity.cache_generation_limit >= cache_required &&
           nfc->capacity.trace_entries >= trace_required &&
           nfc->capacity.virtual_tick_limit >=
               6u * (uint64_t)c34_inner_uid_limit &&
           nfc->capacity.operation_uid_limit >= c34_inner_uid_limit &&
           nfc->capacity.operation_generation_limit >= c34_inner_uid_limit &&
           nfc->capacity.submit_sequence_limit >= c34_inner_uid_limit &&
           c34_physical_sequence_limit >= c34_physical_op_limit;
}

int c35_profile_valid(const struct c35_profile_descriptor *profile)
{
    struct c35_profile_descriptor expected;

    if (profile == NULL) return 0;
    c35_profile_fixed(&expected);
    return profile->geometry_id != 0 && profile->media_profile_id != 0 &&
           profile->geometry_id == expected.geometry_id &&
           profile->media_profile_id == expected.media_profile_id &&
           memcmp(profile->geometry_wire, expected.geometry_wire,
                  sizeof(expected.geometry_wire)) == 0 &&
           memcmp(profile->media_wire, expected.media_wire,
                  sizeof(expected.media_wire)) == 0;
}

void c35_profile_uuid(
    const struct c35_profile_descriptor *profile,
    uint32_t image_serial,
    uint8_t uuid[16]
)
{
    memset(uuid, 0, 16);
    if (!c35_profile_valid(profile) || image_serial == 0) return;
    uuid[0] = 'F'; uuid[1] = '3'; uuid[2] = '5'; uuid[3] = 'A';
    put_u32(&uuid[4], profile->media_profile_id);
    put_u32(&uuid[8], profile->geometry_id);
    put_u32(&uuid[12], image_serial);
}
