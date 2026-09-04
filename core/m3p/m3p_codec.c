/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "m3p_internal.h"

#include <string.h>

uint16_t m3p_get_le16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

uint32_t m3p_get_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

void m3p_put_le16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

void m3p_put_le32(uint8_t *bytes, uint32_t value)
{
    unsigned int index;

    for (index = 0; index < 4; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
}

uint32_t m3p_crc32c(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_MAX;
    size_t index;

    for (index = 0; index < size; ++index) {
        unsigned int bit;

        crc ^= bytes[index];
        for (bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);

            crc = (crc >> 1) ^ (UINT32_C(0x82f63b78) & mask);
        }
    }
    return ~crc;
}

uint64_t m3p_hash64(const uint8_t *bytes, size_t size)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    for (index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

int m3p_bytes_zero(const void *value, size_t size)
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

static uint32_t oob_crc(uint8_t bytes[128])
{
    uint8_t saved[4];
    uint32_t crc;

    memcpy(saved, &bytes[88], 4);
    memset(&bytes[88], 0, 4);
    crc = m3p_crc32c(bytes, 128);
    memcpy(&bytes[88], saved, 4);
    return crc;
}

void m3p_encode_oob(uint8_t bytes[128], const struct m3p_oob *oob)
{
    memset(bytes, 0, 128);
    m3p_put_le32(&bytes[0], M3P_OOB_MAGIC);
    m3p_put_le16(&bytes[4], M3P_FORMAT_VERSION);
    m3p_put_le16(&bytes[6], M3P_OOB_HEADER_BYTES);
    bytes[8] = oob->page_type;
    bytes[9] = oob->flags;
    bytes[10] = oob->copy_kind;
    bytes[11] = oob->valid_mask;
    m3p_put_le32(&bytes[12], oob->namespace_id);
    m3p_put_le32(&bytes[16], oob->lpn);
    m3p_put_le16(&bytes[20], oob->erase_generation);
    m3p_put_le32(&bytes[24], oob->record_sequence);
    m3p_put_le32(&bytes[28], oob->transaction_sequence);
    m3p_put_le32(&bytes[32], oob->predecessor_map_sequence);
    m3p_put_le32(&bytes[36], oob->resulting_map_sequence);
    m3p_put_le32(&bytes[40], oob->referenced_data_sequence);
    m3p_put_le32(&bytes[44], oob->main_length);
    m3p_put_le32(&bytes[48], oob->main_crc);
    m3p_put_le32(&bytes[52], oob->checkpoint_generation);
    m3p_put_le32(&bytes[56], oob->checkpoint_covered_sequence);
    m3p_put_le32(&bytes[60], oob->durable_frontier);
    memcpy(&bytes[64], oob->media_uuid, 16);
    bytes[80] = oob->source_block;
    bytes[81] = oob->source_page;
    m3p_put_le32(&bytes[84], oob->source_data_sequence);
    m3p_put_le32(&bytes[88], oob_crc(bytes));
}

int m3p_decode_oob(const uint8_t bytes[128], struct m3p_oob *oob)
{
    uint8_t copy[128];
    uint32_t stored_crc;

    if (bytes == NULL || oob == NULL) {
        return 0;
    }
    memcpy(copy, bytes, sizeof(copy));
    stored_crc = m3p_get_le32(&copy[88]);
    if (m3p_get_le32(&copy[0]) != M3P_OOB_MAGIC ||
        m3p_get_le16(&copy[4]) != M3P_FORMAT_VERSION ||
        m3p_get_le16(&copy[6]) != M3P_OOB_HEADER_BYTES ||
        copy[8] < M3P_PAGE_DATA || copy[8] > M3P_PAGE_CHECKPOINT_COMMIT ||
        copy[9] != 1 || copy[10] > M3P_COPY_GC ||
        m3p_get_le16(&copy[22]) != 0 ||
        m3p_get_le16(&copy[82]) != 0 ||
        !m3p_bytes_zero(&copy[92], 36) || stored_crc != oob_crc(copy)) {
        return 0;
    }
    memset(oob, 0, sizeof(*oob));
    oob->page_type = copy[8];
    oob->flags = copy[9];
    oob->copy_kind = copy[10];
    oob->valid_mask = copy[11];
    oob->namespace_id = m3p_get_le32(&copy[12]);
    oob->lpn = m3p_get_le32(&copy[16]);
    oob->erase_generation = m3p_get_le16(&copy[20]);
    oob->record_sequence = m3p_get_le32(&copy[24]);
    oob->transaction_sequence = m3p_get_le32(&copy[28]);
    oob->predecessor_map_sequence = m3p_get_le32(&copy[32]);
    oob->resulting_map_sequence = m3p_get_le32(&copy[36]);
    oob->referenced_data_sequence = m3p_get_le32(&copy[40]);
    oob->main_length = m3p_get_le32(&copy[44]);
    oob->main_crc = m3p_get_le32(&copy[48]);
    oob->checkpoint_generation = m3p_get_le32(&copy[52]);
    oob->checkpoint_covered_sequence = m3p_get_le32(&copy[56]);
    oob->durable_frontier = m3p_get_le32(&copy[60]);
    memcpy(oob->media_uuid, &copy[64], 16);
    oob->source_block = copy[80];
    oob->source_page = copy[81];
    oob->source_data_sequence = m3p_get_le32(&copy[84]);
    if (oob->main_length != M3P_PAGE_BYTES || oob->record_sequence == 0) {
        return 0;
    }
    if (oob->page_type == M3P_PAGE_DATA) {
        return oob->namespace_id == 1 && oob->lpn < M3P_LPN_COUNT &&
               oob->valid_mask != 0 && oob->transaction_sequence != 0 &&
               oob->checkpoint_generation == 0 &&
               oob->checkpoint_covered_sequence == 0 &&
               oob->durable_frontier == 0;
    }
    if (oob->page_type == M3P_PAGE_MAP_TXN) {
        return oob->namespace_id == 1 && oob->lpn == UINT32_MAX &&
               oob->transaction_sequence != 0 &&
               oob->resulting_map_sequence != 0 &&
               oob->checkpoint_generation == 0 &&
               oob->checkpoint_covered_sequence == 0;
    }
    return oob->namespace_id == UINT32_MAX && oob->lpn == UINT32_MAX &&
           oob->checkpoint_generation != 0 && oob->valid_mask == 0 &&
           oob->transaction_sequence == 0;
}

static void encode_map_entry(uint8_t bytes[32], uint16_t lpn,
                             const struct m3p_map_entry *target,
                             const struct m3p_map_entry *prior)
{
    memset(bytes, 0, 32);
    m3p_put_le16(&bytes[0], lpn);
    bytes[2] = target->state;
    bytes[3] = target->valid_mask;
    bytes[4] = target->state == M3P_L2P_VALUE ? target->block : UINT8_MAX;
    bytes[5] = target->state == M3P_L2P_VALUE ? target->page : UINT8_MAX;
    m3p_put_le16(&bytes[6], target->state == M3P_L2P_VALUE ?
                                  target->erase_generation : 0);
    m3p_put_le32(&bytes[8], target->state == M3P_L2P_VALUE ?
                                  target->data_record_sequence : 0);
    bytes[12] = prior->state == M3P_L2P_VALUE ? prior->block : UINT8_MAX;
    bytes[13] = prior->state == M3P_L2P_VALUE ? prior->page : UINT8_MAX;
    m3p_put_le16(&bytes[14], prior->state == M3P_L2P_VALUE ?
                                   prior->erase_generation : 0);
    m3p_put_le32(&bytes[16], prior->state == M3P_L2P_VALUE ?
                                   prior->data_record_sequence : 0);
    m3p_put_le32(&bytes[20], target->state == M3P_L2P_VALUE ?
                                   target->main_crc : 0);
    m3p_put_le32(&bytes[24], target->logical_version);
}

static int decode_map_entry(const uint8_t bytes[32], struct m3p_delta *delta)
{
    memset(delta, 0, sizeof(*delta));
    delta->lpn = m3p_get_le16(&bytes[0]);
    delta->target.state = bytes[2];
    delta->target.valid_mask = bytes[3];
    delta->target.block = bytes[4];
    delta->target.page = bytes[5];
    delta->target.erase_generation = m3p_get_le16(&bytes[6]);
    delta->target.data_record_sequence = m3p_get_le32(&bytes[8]);
    delta->prior.state = bytes[12] == UINT8_MAX ?
        M3P_L2P_UNMAPPED : M3P_L2P_VALUE;
    delta->prior.block = bytes[12];
    delta->prior.page = bytes[13];
    delta->prior.erase_generation = m3p_get_le16(&bytes[14]);
    delta->prior.data_record_sequence = m3p_get_le32(&bytes[16]);
    delta->target.main_crc = m3p_get_le32(&bytes[20]);
    delta->target.logical_version = m3p_get_le32(&bytes[24]);
    if (delta->lpn >= M3P_LPN_COUNT ||
        delta->target.state > M3P_L2P_TOMBSTONE ||
        !m3p_bytes_zero(&bytes[28], 4)) {
        return 0;
    }
    if (delta->target.state == M3P_L2P_VALUE) {
        if (delta->target.valid_mask == 0 || delta->target.block >= M3P_BLOCKS ||
            delta->target.page >= M3P_PAGES_PER_BLOCK ||
            delta->target.data_record_sequence == 0 ||
            delta->target.logical_version == 0) {
            return 0;
        }
    } else if (bytes[4] != UINT8_MAX || bytes[5] != UINT8_MAX ||
               m3p_get_le16(&bytes[6]) != 0 ||
               m3p_get_le32(&bytes[8]) != 0 ||
               m3p_get_le32(&bytes[20]) != 0) {
        return 0;
    }
    return 1;
}

void m3p_encode_map(uint8_t bytes[4096], const struct m3p_map_record *record)
{
    uint8_t index;

    memset(bytes, 0, 4096);
    m3p_put_le32(&bytes[0], M3P_MAP_MAIN_MAGIC);
    m3p_put_le16(&bytes[4], M3P_FORMAT_VERSION);
    m3p_put_le16(&bytes[6], 64);
    bytes[8] = record->subtype;
    bytes[9] = record->delta_count;
    m3p_put_le16(&bytes[10], record->flags);
    m3p_put_le32(&bytes[12], record->transaction_sequence);
    m3p_put_le32(&bytes[16], record->predecessor_map_sequence);
    m3p_put_le32(&bytes[20], record->resulting_map_sequence);
    m3p_put_le32(&bytes[24], record->host_sequence);
    m3p_put_le32(&bytes[28], record->captured_frontier);
    m3p_put_le32(&bytes[32], record->gc_uid);
    bytes[36] = record->gc_source_block;
    bytes[37] = record->gc_destination_block;
    m3p_put_le16(&bytes[40], record->gc_expected_live);
    m3p_put_le16(&bytes[42], record->gc_moved);
    for (index = 0; index < record->delta_count; ++index) {
        encode_map_entry(&bytes[64 + index * 32u], record->delta[index].lpn,
                         &record->delta[index].target,
                         &record->delta[index].prior);
    }
    for (index = 0; index < record->gc_moved; ++index) {
        m3p_put_le32(&bytes[160 + index * 4u],
                     record->relocation_sequence[index]);
    }
}

int m3p_decode_map(const uint8_t bytes[4096], struct m3p_map_record *record)
{
    uint8_t index;

    if (bytes == NULL || record == NULL ||
        m3p_get_le32(&bytes[0]) != M3P_MAP_MAIN_MAGIC ||
        m3p_get_le16(&bytes[4]) != M3P_FORMAT_VERSION ||
        m3p_get_le16(&bytes[6]) != 64 || bytes[8] < M3P_MAP_WRITE ||
        bytes[8] > M3P_MAP_GC_SWITCH || bytes[9] > M3P_MAX_DELTAS ||
        m3p_get_le16(&bytes[10]) != 1 ||
        m3p_get_le32(&bytes[12]) == 0 ||
        m3p_get_le32(&bytes[20]) == 0 ||
        m3p_get_le16(&bytes[38]) != 0 ||
        !m3p_bytes_zero(&bytes[44], 20) ||
        !m3p_bytes_zero(&bytes[260], 3836)) {
        return 0;
    }
    memset(record, 0, sizeof(*record));
    record->subtype = bytes[8];
    record->delta_count = bytes[9];
    record->flags = m3p_get_le16(&bytes[10]);
    record->transaction_sequence = m3p_get_le32(&bytes[12]);
    record->predecessor_map_sequence = m3p_get_le32(&bytes[16]);
    record->resulting_map_sequence = m3p_get_le32(&bytes[20]);
    record->host_sequence = m3p_get_le32(&bytes[24]);
    record->captured_frontier = m3p_get_le32(&bytes[28]);
    record->gc_uid = m3p_get_le32(&bytes[32]);
    record->gc_source_block = bytes[36];
    record->gc_destination_block = bytes[37];
    record->gc_expected_live = m3p_get_le16(&bytes[40]);
    record->gc_moved = m3p_get_le16(&bytes[42]);
    for (index = 0; index < record->delta_count; ++index) {
        if (!decode_map_entry(&bytes[64 + index * 32u],
                              &record->delta[index])) {
            return 0;
        }
    }
    if (!m3p_bytes_zero(&bytes[64 + record->delta_count * 32u],
                        96u - record->delta_count * 32u)) {
        return 0;
    }
    if (record->subtype == M3P_MAP_GC_SWITCH) {
        if (record->delta_count != 0 || record->gc_uid == 0 ||
            record->gc_source_block >= M3P_BLOCKS ||
            record->gc_destination_block >= M3P_BLOCKS ||
            record->gc_expected_live > 25 ||
            record->gc_moved != record->gc_expected_live) {
            return 0;
        }
        for (index = 0; index < record->gc_moved; ++index) {
            record->relocation_sequence[index] =
                m3p_get_le32(&bytes[160 + index * 4u]);
            if (record->relocation_sequence[index] == 0) {
                return 0;
            }
        }
    } else {
        if (record->delta_count == 0 || record->gc_moved != 0 ||
            !m3p_bytes_zero(&bytes[160], 100)) {
            return 0;
        }
    }
    return m3p_bytes_zero(&bytes[160 + record->gc_moved * 4u],
                          100u - record->gc_moved * 4u);
}

void m3p_encode_checkpoint_body(
    uint8_t bytes[4096], const struct m3p_map_entry map[M3P_LPN_COUNT])
{
    uint16_t index;

    memset(bytes, 0, 4096);
    for (index = 0; index < M3P_LPN_COUNT; ++index) {
        uint8_t *entry = &bytes[index * 16u];

        entry[0] = map[index].state;
        entry[1] = map[index].valid_mask;
        entry[2] = map[index].state == M3P_L2P_VALUE ?
            map[index].block : UINT8_MAX;
        entry[3] = map[index].state == M3P_L2P_VALUE ?
            map[index].page : UINT8_MAX;
        m3p_put_le16(&entry[4], map[index].state == M3P_L2P_VALUE ?
                                    map[index].erase_generation : 0);
        m3p_put_le32(&entry[8], map[index].state == M3P_L2P_VALUE ?
                                    map[index].data_record_sequence : 0);
        m3p_put_le32(&entry[12], map[index].map_sequence);
    }
}

int m3p_decode_checkpoint_body(
    const uint8_t bytes[4096], struct m3p_map_entry map[M3P_LPN_COUNT])
{
    uint16_t index;

    if (bytes == NULL || map == NULL) {
        return 0;
    }
    memset(map, 0, sizeof(struct m3p_map_entry) * M3P_LPN_COUNT);
    for (index = 0; index < M3P_LPN_COUNT; ++index) {
        const uint8_t *entry = &bytes[index * 16u];

        map[index].state = entry[0];
        map[index].valid_mask = entry[1];
        map[index].block = entry[2];
        map[index].page = entry[3];
        map[index].erase_generation = m3p_get_le16(&entry[4]);
        map[index].data_record_sequence = m3p_get_le32(&entry[8]);
        map[index].map_sequence = m3p_get_le32(&entry[12]);
        if (map[index].state > M3P_L2P_TOMBSTONE ||
            m3p_get_le16(&entry[6]) != 0 ||
            (map[index].state == M3P_L2P_VALUE &&
             (map[index].valid_mask == 0 || map[index].block >= M3P_BLOCKS ||
              map[index].page >= M3P_PAGES_PER_BLOCK ||
              map[index].data_record_sequence == 0)) ||
            (map[index].state != M3P_L2P_VALUE &&
             (entry[2] != UINT8_MAX || entry[3] != UINT8_MAX ||
              m3p_get_le16(&entry[4]) != 0 ||
              m3p_get_le32(&entry[8]) != 0))) {
            return 0;
        }
    }
    return 1;
}

void m3p_encode_checkpoint_commit(
    uint8_t bytes[4096], const struct m3p_checkpoint_commit *commit)
{
    memset(bytes, 0, 4096);
    m3p_put_le32(&bytes[0], M3P_CHECKPOINT_COMMIT_MAGIC);
    m3p_put_le16(&bytes[4], M3P_FORMAT_VERSION);
    m3p_put_le16(&bytes[6], 64);
    m3p_put_le32(&bytes[8], commit->generation);
    bytes[12] = commit->body_block;
    bytes[13] = commit->body_page;
    m3p_put_le16(&bytes[14], commit->body_erase_generation);
    m3p_put_le32(&bytes[16], commit->body_main_crc);
    m3p_put_le32(&bytes[20], commit->body_oob_crc);
    m3p_put_le32(&bytes[24], commit->covered_map_sequence);
    m3p_put_le32(&bytes[28], commit->durable_frontier);
    m3p_put_le32(&bytes[32], commit->journal_generation);
    m3p_put_le32(&bytes[36], commit->commit_record_sequence);
    memcpy(&bytes[40], commit->media_uuid, 16);
}

int m3p_decode_checkpoint_commit(
    const uint8_t bytes[4096], struct m3p_checkpoint_commit *commit)
{
    if (bytes == NULL || commit == NULL ||
        m3p_get_le32(&bytes[0]) != M3P_CHECKPOINT_COMMIT_MAGIC ||
        m3p_get_le16(&bytes[4]) != M3P_FORMAT_VERSION ||
        m3p_get_le16(&bytes[6]) != 64 || m3p_get_le32(&bytes[8]) == 0 ||
        m3p_get_le32(&bytes[36]) == 0 ||
        !m3p_bytes_zero(&bytes[56], 4040)) {
        return 0;
    }
    memset(commit, 0, sizeof(*commit));
    commit->generation = m3p_get_le32(&bytes[8]);
    commit->body_block = bytes[12];
    commit->body_page = bytes[13];
    commit->body_erase_generation = m3p_get_le16(&bytes[14]);
    commit->body_main_crc = m3p_get_le32(&bytes[16]);
    commit->body_oob_crc = m3p_get_le32(&bytes[20]);
    commit->covered_map_sequence = m3p_get_le32(&bytes[24]);
    commit->durable_frontier = m3p_get_le32(&bytes[28]);
    commit->journal_generation = m3p_get_le32(&bytes[32]);
    commit->commit_record_sequence = m3p_get_le32(&bytes[36]);
    memcpy(commit->media_uuid, &bytes[40], 16);
    return commit->body_block >= 12 && commit->body_block <= 13 &&
           commit->body_page < M3P_PAGES_PER_BLOCK;
}
