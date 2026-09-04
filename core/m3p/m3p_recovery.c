/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "m3p_internal.h"

#include <string.h>

static int recovery_credit_available(const struct fwlab_m3p *m3p)
{
    return m3p->next_child_uid != 0 &&
           m3p->next_child_uid <= m3p->config.nfc_operation_uid_limit &&
           UINT32_C(1023) <= m3p->config.nfc_operation_uid_limit -
                                 m3p->next_child_uid;
}

enum fwlab_spine_result_v0 fwlab_m3p_recover_start(struct fwlab_m3p *m3p)
{
    if (m3p == NULL || m3p->magic != M3P_MAGIC || !m3p->initialized ||
        m3p->quarantined || m3p->ready || m3p->admission_closed ||
        m3p->work_kind != FWLAB_M3P_MAINTENANCE_NONE ||
        m3p->operation.state != M3P_OPERATION_FREE ||
        m3p->child.state != M3P_CHILD_IDLE || !recovery_credit_available(m3p)) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    m3p_mapping_reset(m3p);
    memset(m3p->recovered_map, 0, sizeof(m3p->recovered_map));
    memset(m3p->recovered_checkpoint, 0,
           sizeof(m3p->recovered_checkpoint));
    memset(m3p->checkpoint_candidate, 0,
           sizeof(m3p->checkpoint_candidate));
    memset(m3p->recovery_tail_seen, 0, sizeof(m3p->recovery_tail_seen));
    memset(m3p->recovery_data_valid, 0,
           sizeof(m3p->recovery_data_valid));
    memset(m3p->recovered_data, 0, sizeof(m3p->recovered_data));
    memset(&m3p->recovered_commit, 0, sizeof(m3p->recovered_commit));
    m3p->recovery_page = 0;
    m3p->recovery_map_count = 0;
    m3p->recovery_fault_code = 0;
    m3p->recovery_completed = 0;
    m3p->recovery_resume_gc = 0;
    m3p->recovery_checkpoint_cleanup = 0;
    m3p->recovery_journal_cleanup = 0;
    m3p->recovery_resume_state = 0;
    m3p->recovery_have_body = 0;
    m3p->recovery_have_commit = 0;
    m3p->work_kind = FWLAB_M3P_MAINTENANCE_RECOVERY;
    m3p->work_state = M3P_RECOVERY_SCAN_START;
    return FWLAB_SPINE_V0_OK;
}

static int bytes_erased(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != UINT8_C(0xff)) {
            return 0;
        }
    }
    return 1;
}

static int accept_checkpoint_commit(struct fwlab_m3p *m3p,
                                    const struct m3p_oob *oob)
{
    struct m3p_checkpoint_commit commit;

    if (!m3p->recovery_have_body ||
        !m3p_decode_checkpoint_commit(m3p->frame_main[0], &commit) ||
        commit.generation != oob->checkpoint_generation ||
        commit.body_block != m3p->recovery_body_block ||
        commit.body_page != m3p->recovery_body_page ||
        commit.body_main_crc != m3p->recovery_body_crc ||
        commit.body_oob_crc != m3p->checkpoint_body_oob_crc ||
        commit.covered_map_sequence != oob->checkpoint_covered_sequence ||
        commit.durable_frontier != oob->durable_frontier ||
        memcmp(commit.media_uuid, m3p->config.media_uuid, 16) != 0) {
        return 0;
    }
    if (m3p->recovery_have_commit &&
        commit.generation == m3p->recovered_commit.generation &&
        memcmp(&commit, &m3p->recovered_commit, sizeof(commit)) != 0) {
        return 0;
    }
    if (!m3p->recovery_have_commit ||
        commit.generation > m3p->recovered_commit.generation) {
        m3p->recovered_commit = commit;
        memcpy(m3p->recovered_checkpoint, m3p->checkpoint_candidate,
               sizeof(m3p->recovered_checkpoint));
        m3p->recovery_have_commit = 1;
    }
    return 1;
}

static int process_recovered_page(struct fwlab_m3p *m3p, uint16_t linear)
{
    uint8_t block = (uint8_t)(linear / M3P_PAGES_PER_BLOCK);
    uint8_t page = (uint8_t)(linear % M3P_PAGES_PER_BLOCK);
    struct m3p_oob oob;

    m3p->block_erase_generation[block] =
        m3p->child.completion.final_erase_generation;
    m3p->block_erase_count[block] =
        m3p->child.completion.final_erase_generation;
    m3p->block_health[block] = m3p->child.completion.block_health;
    if (bytes_erased(m3p->frame_main[0], M3P_PAGE_BYTES) &&
        bytes_erased(m3p->frame_oob[0], M3P_OOB_BYTES)) {
        m3p->recovery_tail_seen[block] = 1;
        return 1;
    }
    if (m3p->recovery_tail_seen[block]) {
        m3p->recovery_fault_code = 11;
        return 0;
    }
    if (!m3p_decode_oob(m3p->frame_oob[0], &oob)) {
        m3p->recovery_fault_code = 12;
        return 0;
    }
    if (memcmp(oob.media_uuid, m3p->config.media_uuid, 16) != 0) {
        m3p->recovery_fault_code = 13;
        return 0;
    }
    if (oob.erase_generation != m3p->block_erase_generation[block]) {
        m3p->recovery_fault_code = 14;
        return 0;
    }
    if (oob.main_crc != m3p_crc32c(m3p->frame_main[0], M3P_PAGE_BYTES)) {
        m3p->recovery_fault_code = 15;
        return 0;
    }
    if (m3p->block_next_page[block] <= page) {
        m3p->block_next_page[block] = (uint8_t)(page + 1u);
    }
    if (oob.record_sequence > m3p->record_sequence) {
        m3p->record_sequence = oob.record_sequence;
    }
    if (oob.page_type == M3P_PAGE_DATA) {
        m3p->recovery_data_valid[linear] = 1;
        m3p->recovered_data[linear] = oob;
        m3p->p2l[linear] = M3P_P2L_ORPHAN;
        return 1;
    }
    if (oob.page_type == M3P_PAGE_MAP_TXN) {
        struct m3p_recovery_record *record;

        if (m3p->recovery_map_count >= M3P_MAX_RECOVERED_MAPS) {
            m3p->recovery_fault_code = 2;
            return 0;
        }
        record = &m3p->recovered_map[m3p->recovery_map_count++];
        memset(record, 0, sizeof(*record));
        record->valid = 1;
        record->block = block;
        record->page = page;
        record->oob = oob;
        if (!m3p_decode_map(m3p->frame_main[0], &record->map)) {
            m3p->recovery_fault_code = 3;
            return 0;
        }
        if (record->map.transaction_sequence != oob.transaction_sequence ||
            record->map.predecessor_map_sequence !=
                oob.predecessor_map_sequence ||
            record->map.resulting_map_sequence !=
                oob.resulting_map_sequence) {
            m3p->recovery_fault_code = 4;
            return 0;
        }
        return 1;
    }
    if (oob.page_type == M3P_PAGE_CHECKPOINT_BODY) {
        if (!m3p_decode_checkpoint_body(m3p->frame_main[0],
                                        m3p->checkpoint_candidate)) {
            m3p->recovery_fault_code = 5;
            return 0;
        }
        m3p->recovery_have_body = 1;
        m3p->recovery_body_block = block;
        m3p->recovery_body_page = page;
        m3p->recovery_body_crc = oob.main_crc;
        m3p->checkpoint_body_oob_crc =
            m3p_crc32c(m3p->frame_oob[0], M3P_OOB_BYTES);
        return 1;
    }
    if (!accept_checkpoint_commit(m3p, &oob)) {
        m3p->recovery_fault_code = 6;
        return 0;
    }
    return 1;
}

static int data_record_matches(struct fwlab_m3p *m3p, uint16_t lpn,
                               struct m3p_map_entry *entry)
{
    uint16_t physical;
    const struct m3p_oob *oob;

    if (entry->state != M3P_L2P_VALUE) {
        return entry->state == M3P_L2P_UNMAPPED ||
               entry->state == M3P_L2P_TOMBSTONE;
    }
    physical = m3p_physical_index(entry->block, entry->page);
    if (physical >= M3P_PHYSICAL_PAGES ||
        !m3p->recovery_data_valid[physical]) {
        return 0;
    }
    oob = &m3p->recovered_data[physical];
    if (oob->page_type != M3P_PAGE_DATA || oob->lpn != lpn ||
        oob->record_sequence != entry->data_record_sequence ||
        oob->erase_generation != entry->erase_generation ||
        (entry->valid_mask != 0 &&
         (oob->valid_mask & entry->valid_mask) != entry->valid_mask)) {
        return 0;
    }
    entry->main_crc = oob->main_crc;
    if (entry->logical_version == 0) {
        entry->logical_version = 1;
    }
    return 1;
}

static struct m3p_recovery_record *find_recovery_record(
    struct fwlab_m3p *m3p, uint32_t resulting_sequence, uint32_t predecessor,
    uint32_t *matches)
{
    struct m3p_recovery_record *found = NULL;
    uint32_t index;

    *matches = 0;
    for (index = 0; index < m3p->recovery_map_count; ++index) {
        struct m3p_recovery_record *record = &m3p->recovered_map[index];

        if (record->valid &&
            record->map.resulting_map_sequence == resulting_sequence &&
            record->map.predecessor_map_sequence == predecessor) {
            found = record;
            ++*matches;
        }
    }
    return found;
}

static struct m3p_recovery_record *find_relocation_record(
    struct fwlab_m3p *m3p, uint32_t record_sequence, uint32_t gc_uid)
{
    uint32_t index;

    for (index = 0; index < m3p->recovery_map_count; ++index) {
        struct m3p_recovery_record *record = &m3p->recovered_map[index];

        if (record->valid && record->oob.record_sequence == record_sequence &&
            record->map.subtype == M3P_MAP_RELOCATION &&
            record->map.gc_uid == gc_uid && record->map.delta_count == 1) {
            return record;
        }
    }
    return NULL;
}

static int apply_recovered_record(struct fwlab_m3p *m3p,
                                  struct m3p_recovery_record *record)
{
    uint8_t index;

    if (record->map.subtype == M3P_MAP_GC_SWITCH) {
        for (index = 0; index < record->map.gc_moved; ++index) {
            struct m3p_recovery_record *relocation =
                find_relocation_record(m3p,
                    record->map.relocation_sequence[index],
                    record->map.gc_uid);

            if (relocation == NULL ||
                !data_record_matches(m3p, relocation->map.delta[0].lpn,
                                     &relocation->map.delta[0].target)) {
                return 0;
            }
            m3p_publish_durable_delta(m3p, &relocation->map.delta[0],
                                      record->map.resulting_map_sequence);
        }
        m3p->block_role[record->map.gc_destination_block] = M3P_ROLE_DATA;
        m3p->block_role[record->map.gc_source_block] = M3P_ROLE_RESERVE;
        m3p->reserve_block = record->map.gc_source_block;
        return 1;
    }
    if (record->map.subtype == M3P_MAP_RELOCATION) {
        return record->map.gc_uid != 0 && record->map.delta_count == 1;
    }
    for (index = 0; index < record->map.delta_count; ++index) {
        struct m3p_delta *delta = &record->map.delta[index];

        if (delta->prior.state == M3P_L2P_VALUE &&
            (m3p->durable[delta->lpn].state != M3P_L2P_VALUE ||
             m3p->durable[delta->lpn].block != delta->prior.block ||
             m3p->durable[delta->lpn].page != delta->prior.page ||
             m3p->durable[delta->lpn].data_record_sequence !=
                 delta->prior.data_record_sequence)) {
            return 0;
        }
        if (!data_record_matches(m3p, delta->lpn, &delta->target)) {
            return 0;
        }
        m3p_publish_durable_delta(m3p, delta,
                                  record->map.resulting_map_sequence);
    }
    return 1;
}

static int finalize_recovery(struct fwlab_m3p *m3p)
{
    uint32_t current;
    uint32_t open_gc_uid = 0;
    uint16_t lpn;
    uint32_t index;
    uint8_t latest_journal;
    uint8_t latest_page;
    uint8_t open_gc_count = 0;
    uint8_t open_gc_expected = 0;
    uint8_t open_gc_source = UINT8_MAX;
    uint8_t open_gc_destination = UINT8_MAX;
    uint8_t last_switch = 0;
    uint8_t switch_source = UINT8_MAX;
    uint8_t switch_destination = UINT8_MAX;
    uint8_t switch_live = 0;
    uint32_t switch_uid = 0;
    uint32_t switch_sequence = 0;
    struct m3p_delta open_gc_delta[25];
    uint32_t open_gc_record_sequence[25];

    if (!m3p->recovery_have_commit) {
        return 0;
    }
    memcpy(m3p->durable, m3p->recovered_checkpoint,
           sizeof(m3p->durable));
    memcpy(m3p->visible, m3p->durable, sizeof(m3p->visible));
    m3p->map_sequence = m3p->recovered_commit.covered_map_sequence;
    m3p->durable_frontier = m3p->recovered_commit.durable_frontier;
    m3p->host_sequence = m3p->durable_frontier;
    m3p->checkpoint_generation = m3p->recovered_commit.generation;
    m3p->checkpoint_covered_sequence =
        m3p->recovered_commit.covered_map_sequence;
    m3p->journal_generation = m3p->recovered_commit.journal_generation;
    m3p->active_checkpoint_block = m3p->recovered_commit.body_block;
    m3p->inactive_checkpoint_block =
        (uint8_t)(m3p->active_checkpoint_block == 12 ? 13 : 12);
    m3p->checkpoint_page =
        (uint8_t)(m3p->recovered_commit.body_page + 2u);
    latest_journal =
        (m3p->recovered_commit.journal_generation & 1u) != 0 ? 10 : 11;
    latest_page = m3p->block_next_page[latest_journal];
    current = m3p->map_sequence;
    memset(open_gc_delta, 0, sizeof(open_gc_delta));
    memset(open_gc_record_sequence, 0, sizeof(open_gc_record_sequence));
    for (;;) {
        uint32_t matches;
        struct m3p_recovery_record *record =
            find_recovery_record(m3p, current + 1u, current, &matches);

        if (matches == 0) {
            break;
        }
        if (record->map.subtype == M3P_MAP_RELOCATION) {
            if (record->map.gc_uid == 0 || record->map.delta_count != 1 ||
                record->map.gc_expected_live == 0 ||
                record->map.gc_expected_live > 25 ||
                record->map.gc_source_block >= M3P_BLOCKS ||
                record->map.gc_destination_block >= M3P_BLOCKS ||
                record->map.delta[0].prior.block !=
                    record->map.gc_source_block ||
                record->map.delta[0].target.block !=
                    record->map.gc_destination_block) {
                return 0;
            }
            if (open_gc_uid == 0) {
                open_gc_uid = record->map.gc_uid;
                open_gc_expected = (uint8_t)record->map.gc_expected_live;
                open_gc_source = record->map.gc_source_block;
                open_gc_destination = record->map.gc_destination_block;
            }
            if (record->map.gc_uid != open_gc_uid ||
                record->map.gc_expected_live != open_gc_expected ||
                record->map.gc_source_block != open_gc_source ||
                record->map.gc_destination_block != open_gc_destination ||
                open_gc_count >= open_gc_expected) {
                return 0;
            }
            open_gc_delta[open_gc_count] = record->map.delta[0];
            open_gc_record_sequence[open_gc_count] =
                record->oob.record_sequence;
            ++open_gc_count;
        } else if (record->map.subtype == M3P_MAP_GC_SWITCH) {
            if (open_gc_uid == 0 || record->map.gc_uid != open_gc_uid ||
                record->map.gc_moved != open_gc_count ||
                record->map.gc_expected_live != open_gc_expected ||
                record->map.gc_source_block != open_gc_source ||
                record->map.gc_destination_block != open_gc_destination) {
                return 0;
            }
            last_switch = 1;
            switch_source = open_gc_source;
            switch_destination = open_gc_destination;
            switch_live = open_gc_expected;
            switch_uid = open_gc_uid;
            switch_sequence = record->map.resulting_map_sequence;
            open_gc_uid = 0;
            open_gc_count = 0;
        } else if (open_gc_uid != 0) {
            return 0;
        }
        if (matches != 1 || !apply_recovered_record(m3p, record)) {
            return 0;
        }
        current = record->map.resulting_map_sequence;
        if (record->map.host_sequence > m3p->host_sequence) {
            m3p->host_sequence = record->map.host_sequence;
        }
        if (record->map.host_sequence > m3p->durable_frontier &&
            record->map.subtype != M3P_MAP_RELOCATION) {
            m3p->durable_frontier = record->map.host_sequence;
        }
        latest_journal = record->block;
        latest_page = (uint8_t)(record->page + 1u);
    }
    m3p->map_sequence = current;
    for (index = 0; index < m3p->recovery_map_count; ++index) {
        if (m3p->recovered_map[index].valid &&
            m3p->recovered_map[index].map.resulting_map_sequence > current &&
            m3p->recovered_map[index].map.predecessor_map_sequence <= current) {
            return 0;
        }
    }
    for (index = 0; index < M3P_PHYSICAL_PAGES; ++index) {
        if (m3p->p2l[index] != M3P_P2L_TORN &&
            m3p->p2l[index] != M3P_P2L_UNAVAILABLE) {
            m3p->p2l[index] = m3p->recovery_data_valid[index] ?
                M3P_P2L_ORPHAN : M3P_P2L_FREE;
        }
    }
    for (lpn = 0; lpn < M3P_LPN_COUNT; ++lpn) {
        if (!data_record_matches(m3p, lpn, &m3p->durable[lpn])) {
            return 0;
        }
        if (m3p->durable[lpn].state == M3P_L2P_VALUE) {
            m3p->p2l[m3p_physical_index(m3p->durable[lpn].block,
                                        m3p->durable[lpn].page)] =
                M3P_P2L_LIVE;
        }
    }
    if (latest_journal >= 10 && latest_journal <= 11) {
        m3p->active_journal_block = latest_journal;
        m3p->inactive_journal_block = latest_journal == 10 ? 11 : 10;
        m3p->journal_page = latest_page;
    } else {
        m3p->active_journal_block =
            m3p->block_next_page[10] == 0 ? 10 : 11;
        m3p->inactive_journal_block =
            m3p->active_journal_block == 10 ? 11 : 10;
        m3p->journal_page = m3p->block_next_page[m3p->active_journal_block];
    }
    memcpy(m3p->visible, m3p->durable, sizeof(m3p->visible));
    if (open_gc_uid != 0) {
        m3p->gc_uid = open_gc_uid;
        m3p->gc_victim = open_gc_source;
        m3p->gc_destination = open_gc_destination;
        m3p->gc_live_count = open_gc_expected;
        m3p->gc_moved = open_gc_count;
        m3p->gc_reclaimable =
            (uint8_t)(M3P_PAGES_PER_BLOCK - open_gc_expected);
        memcpy(m3p->gc_delta, open_gc_delta,
               (size_t)open_gc_count * sizeof(open_gc_delta[0]));
        memcpy(m3p->gc_relocation_sequence, open_gc_record_sequence,
               (size_t)open_gc_count * sizeof(open_gc_record_sequence[0]));
        if (!m3p_gc_collect_live_pages(m3p)) {
            return 0;
        }
        for (index = 0; index < open_gc_count; ++index) {
            if (m3p->gc_source_lpn[index] !=
                    m3p->gc_delta[index].lpn ||
                m3p->gc_source_page[index] !=
                    m3p->gc_delta[index].prior.page ||
                !data_record_matches(m3p, m3p->gc_delta[index].lpn,
                                     &m3p->gc_delta[index].target)) {
                return 0;
            }
            m3p->p2l[m3p_physical_index(
                m3p->gc_delta[index].prior.block,
                m3p->gc_delta[index].prior.page)] =
                    M3P_P2L_GC_SOURCE_PINNED;
            m3p->p2l[m3p_physical_index(
                m3p->gc_delta[index].target.block,
                m3p->gc_delta[index].target.page)] =
                    M3P_P2L_GC_DEST_STAGED;
        }
        m3p->reserve_block = open_gc_destination;
        if (open_gc_destination == m3p->replacement_block) {
            m3p->replacement_used = 1;
            m3p->block_role[14] = M3P_ROLE_UNAVAILABLE;
        }
        m3p->block_role[open_gc_destination] = M3P_ROLE_RESERVE;
        m3p->work_kind = FWLAB_M3P_MAINTENANCE_GC;
        m3p->work_state = open_gc_count < open_gc_expected ?
            M3P_GC_READ : M3P_GC_SWITCH;
        m3p->recovery_resume_gc = 1;
        m3p->ready = 0;
    } else if (last_switch &&
               m3p->block_health[switch_source] != FWLAB_NFC_BLOCK_GOOD) {
        uint8_t replacement = m3p->replacement_block;

        if (switch_destination == replacement || replacement >= M3P_BLOCKS ||
            m3p->block_health[replacement] != FWLAB_NFC_BLOCK_GOOD ||
            m3p->block_next_page[replacement] != 0 ||
            m3p->block_role[replacement] != M3P_ROLE_REPLACEMENT) {
            return 0;
        }
        m3p->gc_uid = switch_uid;
        m3p->gc_victim = switch_source;
        m3p->gc_destination = switch_destination;
        m3p->gc_live_count = switch_live;
        m3p->gc_moved = switch_live;
        m3p->gc_reclaimable =
            (uint8_t)(M3P_PAGES_PER_BLOCK - switch_live);
        m3p->gc_switch_sequence = switch_sequence;
        m3p->gc_fault_code = FWLAB_NFC_REASON_BAD_BLOCK;
        m3p->block_role[switch_source] = M3P_ROLE_UNAVAILABLE;
        m3p->block_role[replacement] = M3P_ROLE_RESERVE;
        m3p->reserve_block = replacement;
        m3p->replacement_used = 1;
        m3p->work_state = M3P_GC_DONE;
    } else if (last_switch &&
               m3p->block_next_page[switch_source] != 0) {
        m3p->gc_uid = switch_uid;
        m3p->gc_victim = switch_source;
        m3p->gc_destination = switch_destination;
        m3p->gc_live_count = switch_live;
        m3p->gc_moved = switch_live;
        m3p->gc_reclaimable =
            (uint8_t)(M3P_PAGES_PER_BLOCK - switch_live);
        m3p->gc_switch_sequence = switch_sequence;
        m3p->work_kind = FWLAB_M3P_MAINTENANCE_GC;
        m3p->work_state = M3P_GC_ERASE;
        m3p->recovery_resume_gc = 1;
        m3p->ready = 0;
    }
    if (m3p->block_next_page[m3p->inactive_journal_block] != 0) {
        m3p->recovery_journal_cleanup = 1;
    }
    if (m3p->block_next_page[m3p->inactive_checkpoint_block] != 0) {
        m3p->recovery_checkpoint_cleanup = 1;
    }
    if (m3p->recovery_journal_cleanup ||
        m3p->recovery_checkpoint_cleanup) {
        if (m3p->work_kind == FWLAB_M3P_MAINTENANCE_GC) {
            m3p->recovery_resume_state = m3p->work_state;
        }
        m3p->work_kind = FWLAB_M3P_MAINTENANCE_RECOVERY;
        m3p->work_state = m3p->recovery_journal_cleanup ?
            M3P_RECOVERY_JOURNAL_ERASE : M3P_RECOVERY_CHECKPOINT_ERASE;
        m3p->ready = 0;
    }
    return 1;
}

static void finish_recovery_cleanup(struct fwlab_m3p *m3p)
{
    if (m3p->recovery_resume_state != 0) {
        m3p->work_state = m3p->recovery_resume_state;
        m3p->recovery_resume_state = 0;
        m3p->work_kind = FWLAB_M3P_MAINTENANCE_GC;
    } else {
        m3p->work_state = M3P_RECOVERY_DONE;
        m3p->work_kind = FWLAB_M3P_MAINTENANCE_NONE;
        m3p->recovery_completed = 1;
        m3p->ready = 1;
    }
}

int m3p_recovery_drive(struct fwlab_m3p *m3p)
{
    if (m3p->work_state == M3P_RECOVERY_SCAN_START) {
        struct fwlab_nfc_ppa ppa;

        if (m3p->recovery_page >= M3P_PHYSICAL_PAGES) {
            m3p->work_state = M3P_RECOVERY_FINALIZE;
            return 1;
        }
        memset(&ppa, 0, sizeof(ppa));
        ppa.block = (uint16_t)(m3p->recovery_page /
                               M3P_PAGES_PER_BLOCK);
        ppa.page = (uint16_t)(m3p->recovery_page %
                              M3P_PAGES_PER_BLOCK);
        if (m3p_child_read_start(m3p, 0, ppa) != FWLAB_SPINE_V0_OK) {
            m3p->work_state = M3P_WORK_FAILED;
            m3p->quarantined = 1;
            return 1;
        }
        m3p->work_state = M3P_RECOVERY_SCAN_WAIT;
        return 1;
    }
    if (m3p->work_state == M3P_RECOVERY_SCAN_WAIT) {
        uint16_t linear = (uint16_t)m3p->recovery_page;
        uint8_t block = (uint8_t)(linear / M3P_PAGES_PER_BLOCK);

        if (m3p->child.state == M3P_CHILD_FAILED) {
            if (m3p->child.completion.reason == FWLAB_NFC_REASON_BAD_BLOCK &&
                m3p->child.completion.block_health !=
                    FWLAB_NFC_BLOCK_GOOD &&
                (block < M3P_DATA_POOL_BLOCKS || block >= 14)) {
                m3p->block_health[block] =
                    m3p->child.completion.block_health;
                m3p->block_erase_generation[block] =
                    m3p->child.completion.final_erase_generation;
                m3p->block_erase_count[block] =
                    m3p->child.completion.final_erase_generation;
                m3p->block_role[block] = M3P_ROLE_UNAVAILABLE;
                m3p->block_next_page[block] = M3P_PAGES_PER_BLOCK;
                memset(&m3p->p2l[m3p_physical_index(block, 0)],
                       M3P_P2L_UNAVAILABLE, M3P_PAGES_PER_BLOCK);
                m3p_child_consume(m3p);
                m3p->recovery_page =
                    (uint32_t)(block + 1u) * M3P_PAGES_PER_BLOCK;
                m3p->work_state = M3P_RECOVERY_SCAN_START;
                return 1;
            }
            if (m3p->child.completion.reason ==
                    FWLAB_NFC_REASON_ECC_UNCORRECTABLE ||
                m3p->child.completion.integrity == FWLAB_NFC_INTEGRITY_TORN) {
                m3p->p2l[linear] = M3P_P2L_TORN;
                m3p->recovery_tail_seen[block] = 1;
                m3p->block_erase_generation[block] =
                    m3p->child.completion.final_erase_generation;
                if (m3p->block_next_page[block] <=
                        linear % M3P_PAGES_PER_BLOCK) {
                    m3p->block_next_page[block] = (uint8_t)(
                        linear % M3P_PAGES_PER_BLOCK + 1u);
                }
                m3p_child_consume(m3p);
                ++m3p->recovery_page;
                m3p->work_state = M3P_RECOVERY_SCAN_START;
                return 1;
            }
            m3p->work_state = M3P_WORK_FAILED;
            m3p->quarantined = 1;
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            if (!process_recovered_page(m3p, linear)) {
                m3p->work_state = M3P_WORK_FAILED;
                m3p->quarantined = 1;
                return 1;
            }
            m3p_child_consume(m3p);
            ++m3p->recovery_page;
            m3p->work_state = M3P_RECOVERY_SCAN_START;
            return 1;
        }
        return 0;
    }
    if (m3p->work_state == M3P_RECOVERY_FINALIZE) {
        if (!finalize_recovery(m3p)) {
            if (m3p->recovery_fault_code == 0) {
                m3p->recovery_fault_code = 100;
            }
            m3p->work_state = M3P_WORK_FAILED;
            m3p->quarantined = 1;
        } else if (m3p->recovery_journal_cleanup ||
                   m3p->recovery_checkpoint_cleanup) {
            /* Recovery owns READY until covered metadata blocks are reusable. */
        } else if (m3p->work_kind == FWLAB_M3P_MAINTENANCE_GC) {
            /* Recovery owns READY until the reconstructed GC phase drains. */
        } else {
            m3p->work_state = M3P_RECOVERY_DONE;
            m3p->work_kind = FWLAB_M3P_MAINTENANCE_NONE;
            m3p->recovery_completed = 1;
            m3p->ready = 1;
        }
        return 1;
    }
    if (m3p->work_state == M3P_RECOVERY_CHECKPOINT_ERASE) {
        uint8_t block = m3p->inactive_checkpoint_block;

        if (!m3p->recovery_checkpoint_cleanup || block >= M3P_BLOCKS) {
            m3p->work_state = M3P_WORK_FAILED;
            m3p->quarantined = 1;
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_IDLE) {
            struct fwlab_nfc_ppa ppa = {0, 0, 0, block, 0, 0};

            if (m3p_child_erase_start(m3p, ppa) != FWLAB_SPINE_V0_OK) {
                m3p->work_state = M3P_WORK_FAILED;
                m3p->quarantined = 1;
            }
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_FAILED) {
            m3p->work_state = M3P_WORK_FAILED;
            m3p->quarantined = 1;
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            m3p->block_erase_generation[block] =
                m3p->child.completion.final_erase_generation;
            ++m3p->block_erase_count[block];
            m3p->block_next_page[block] = 0;
            memset(&m3p->p2l[m3p_physical_index(block, 0)], M3P_P2L_FREE,
                   M3P_PAGES_PER_BLOCK);
            m3p_child_consume(m3p);
            m3p->recovery_checkpoint_cleanup = 0;
            finish_recovery_cleanup(m3p);
            return 1;
        }
    }
    if (m3p->work_state == M3P_RECOVERY_JOURNAL_ERASE) {
        uint8_t block = m3p->inactive_journal_block;

        if (!m3p->recovery_journal_cleanup || block < 10 || block > 11) {
            m3p->work_state = M3P_WORK_FAILED;
            m3p->quarantined = 1;
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_IDLE) {
            struct fwlab_nfc_ppa ppa = {0, 0, 0, block, 0, 0};

            if (m3p_child_erase_start(m3p, ppa) != FWLAB_SPINE_V0_OK) {
                m3p->work_state = M3P_WORK_FAILED;
                m3p->quarantined = 1;
            }
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_FAILED) {
            m3p->work_state = M3P_WORK_FAILED;
            m3p->quarantined = 1;
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            m3p->block_erase_generation[block] =
                m3p->child.completion.final_erase_generation;
            ++m3p->block_erase_count[block];
            m3p->block_next_page[block] = 0;
            memset(&m3p->p2l[m3p_physical_index(block, 0)], M3P_P2L_FREE,
                   M3P_PAGES_PER_BLOCK);
            m3p_child_consume(m3p);
            m3p->recovery_journal_cleanup = 0;
            if (m3p->recovery_checkpoint_cleanup) {
                m3p->work_state = M3P_RECOVERY_CHECKPOINT_ERASE;
            } else {
                finish_recovery_cleanup(m3p);
            }
            return 1;
        }
    }
    return 0;
}

enum fwlab_spine_result_v0 fwlab_m3p_recovery_query(
    const struct fwlab_m3p *m3p, struct fwlab_m3p_recovery_status *status)
{
    if (m3p == NULL || status == NULL || m3p->magic != M3P_MAGIC ||
        !m3p->initialized) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(status, 0, sizeof(*status));
    status->version = FWLAB_M3P_VERSION;
    status->size = (uint16_t)sizeof(*status);
    status->pages_scanned = m3p->recovery_page;
    status->checkpoint_generation = m3p->checkpoint_generation;
    status->durable_map_sequence = m3p->map_sequence;
    status->durable_frontier = m3p->durable_frontier;
    if (m3p->quarantined || m3p->work_state == M3P_WORK_FAILED) {
        status->state = FWLAB_M3P_MAINTENANCE_QUARANTINED;
        status->fault_code = m3p->recovery_fault_code == 0 ?
            FWLAB_NFC_REASON_INTERNAL : m3p->recovery_fault_code;
    } else if (m3p->ready && m3p->recovery_completed) {
        status->state = FWLAB_M3P_MAINTENANCE_SUCCEEDED;
    } else if (m3p->work_kind == FWLAB_M3P_MAINTENANCE_RECOVERY) {
        status->state = FWLAB_M3P_MAINTENANCE_RUNNING;
    } else {
        status->state = FWLAB_M3P_MAINTENANCE_IDLE;
    }
    return FWLAB_SPINE_V0_OK;
}
