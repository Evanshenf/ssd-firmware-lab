/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "m3p_internal.h"

#include <string.h>

static int gc_credit_available(const struct fwlab_m3p *m3p, uint32_t count)
{
    return count != 0 && m3p->next_child_uid != 0 &&
           m3p->next_child_uid <= m3p->config.nfc_operation_uid_limit &&
           count - 1u <= m3p->config.nfc_operation_uid_limit -
                            m3p->next_child_uid;
}

static int data_pool_full(const struct fwlab_m3p *m3p)
{
    uint8_t block;

    for (block = 0; block < M3P_BLOCKS; ++block) {
        if (m3p->block_role[block] == M3P_ROLE_DATA &&
            m3p->block_next_page[block] < M3P_PAGES_PER_BLOCK) {
            return 0;
        }
    }
    return 1;
}

static int select_victim(struct fwlab_m3p *m3p, uint8_t *victim,
                         uint8_t *live_count, uint8_t *reclaimable)
{
    uint8_t block;
    uint8_t best = UINT8_MAX;
    uint8_t best_reclaimable = 0;
    uint8_t best_live = 0;

    for (block = 0; block < M3P_BLOCKS; ++block) {
        uint8_t page;
        uint8_t live = 0;
        uint8_t reclaim = 0;

        if (m3p->block_role[block] != M3P_ROLE_DATA ||
            m3p->block_next_page[block] != M3P_PAGES_PER_BLOCK) {
            continue;
        }
        for (page = 0; page < M3P_PAGES_PER_BLOCK; ++page) {
            uint8_t state = m3p->p2l[m3p_physical_index(block, page)];

            if (state == M3P_P2L_LIVE) {
                ++live;
            } else if (state == M3P_P2L_STALE ||
                       state == M3P_P2L_ORPHAN) {
                ++reclaim;
            } else if (state == M3P_P2L_DURABLE_PINNED ||
                       state == M3P_P2L_VISIBLE_PENDING ||
                       state == M3P_P2L_GC_SOURCE_PINNED ||
                       state == M3P_P2L_GC_DEST_STAGED) {
                live = UINT8_MAX;
                break;
            }
        }
        if (live == UINT8_MAX) {
            continue;
        }
        if (best == UINT8_MAX || reclaim > best_reclaimable ||
            (reclaim == best_reclaimable &&
             m3p->block_erase_count[block] < m3p->block_erase_count[best]) ||
            (reclaim == best_reclaimable &&
             m3p->block_erase_count[block] == m3p->block_erase_count[best] &&
             block < best)) {
            best = block;
            best_reclaimable = reclaim;
            best_live = live;
        }
    }
    if (best == UINT8_MAX || best_reclaimable == 0 || best_live > 25) {
        return 0;
    }
    *victim = best;
    *live_count = best_live;
    *reclaimable = best_reclaimable;
    return 1;
}

int m3p_gc_collect_live_pages(struct fwlab_m3p *m3p)
{
    uint8_t page;
    uint8_t count = 0;

    for (page = 0; page < M3P_PAGES_PER_BLOCK; ++page) {
        uint16_t lpn;

        if (m3p->p2l[m3p_physical_index(m3p->gc_victim, page)] !=
            M3P_P2L_LIVE) {
            continue;
        }
        for (lpn = 0; lpn < M3P_LPN_COUNT; ++lpn) {
            if (m3p->durable[lpn].state == M3P_L2P_VALUE &&
                m3p->durable[lpn].block == m3p->gc_victim &&
                m3p->durable[lpn].page == page) {
                if (count >= 25) {
                    return 0;
                }
                m3p->gc_source_page[count] = page;
                m3p->gc_source_lpn[count] = lpn;
                ++count;
                break;
            }
        }
        if (lpn == M3P_LPN_COUNT) {
            return 0;
        }
    }
    return count == m3p->gc_live_count;
}

enum fwlab_spine_result_v0 fwlab_m3p_force_gc_start(struct fwlab_m3p *m3p)
{
    uint8_t victim;
    uint8_t live;
    uint8_t reclaimable;
    uint32_t child_bound;

    if (m3p == NULL || m3p->magic != M3P_MAGIC || !m3p->initialized ||
        !m3p->ready || m3p->quarantined || m3p->admission_closed ||
        m3p->work_kind != FWLAB_M3P_MAINTENANCE_NONE ||
        m3p->operation.state != M3P_OPERATION_FREE ||
        m3p->pending_count != 0 || m3p->child.state != M3P_CHILD_IDLE ||
        !data_pool_full(m3p) || m3p->block_role[m3p->reserve_block] !=
                                    M3P_ROLE_RESERVE ||
        m3p->block_health[m3p->reserve_block] != FWLAB_NFC_BLOCK_GOOD ||
        m3p->block_next_page[m3p->reserve_block] != 0 ||
        !select_victim(m3p, &victim, &live, &reclaimable) ||
        live + 1u > M3P_PAGES_PER_BLOCK) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    child_bound = 6u * live + 3u;
    if (!gc_credit_available(m3p, child_bound) ||
        m3p->record_sequence + 2u * live + 1u >
            m3p->config.record_sequence_limit ||
        m3p->map_sequence + live + 1u >
            m3p->config.record_sequence_limit) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    m3p->work_kind = FWLAB_M3P_MAINTENANCE_GC;
    m3p->work_state = M3P_GC_PREPARE;
    m3p->gc_uid = m3p->map_sequence + 1u;
    m3p->gc_victim = victim;
    m3p->gc_destination = m3p->reserve_block;
    m3p->gc_live_count = live;
    m3p->gc_moved = 0;
    m3p->gc_reclaimable = reclaimable;
    m3p->gc_switch_sequence = 0;
    m3p->gc_fault_code = 0;
    memset(m3p->gc_delta, 0, sizeof(m3p->gc_delta));
    memset(m3p->gc_relocation_sequence, 0,
           sizeof(m3p->gc_relocation_sequence));
    if (!m3p_gc_collect_live_pages(m3p)) {
        m3p->work_state = M3P_WORK_FAILED;
        m3p->quarantined = 1;
        return FWLAB_SPINE_V0_QUARANTINED;
    }
    return FWLAB_SPINE_V0_OK;
}

static int start_gc_data_program(struct fwlab_m3p *m3p)
{
    uint8_t moved = m3p->gc_moved;
    uint16_t lpn = m3p->gc_source_lpn[moved];
    const struct m3p_map_entry *source = &m3p->durable[lpn];
    struct m3p_delta *delta = &m3p->gc_delta[moved];
    struct m3p_oob oob;
    struct fwlab_nfc_ppa destination = {0, 0, 0, m3p->gc_destination,
                                        moved, 0};

    if (m3p->record_sequence >= m3p->config.record_sequence_limit) {
        return 0;
    }
    memset(delta, 0, sizeof(*delta));
    delta->lpn = lpn;
    delta->prior = *source;
    delta->target = *source;
    delta->target.block = m3p->gc_destination;
    delta->target.page = moved;
    delta->target.erase_generation =
        m3p->block_erase_generation[m3p->gc_destination];
    delta->target.data_record_sequence = m3p->record_sequence + 1u;
    delta->target.main_crc = m3p_crc32c(m3p->frame_main[0], M3P_PAGE_BYTES);
    memset(&oob, 0, sizeof(oob));
    oob.page_type = M3P_PAGE_DATA;
    oob.flags = 1;
    oob.copy_kind = M3P_COPY_GC;
    oob.valid_mask = source->valid_mask;
    oob.namespace_id = 1;
    oob.lpn = lpn;
    oob.erase_generation = delta->target.erase_generation;
    oob.record_sequence = delta->target.data_record_sequence;
    oob.transaction_sequence = m3p->gc_uid;
    oob.referenced_data_sequence = delta->target.data_record_sequence;
    oob.main_length = M3P_PAGE_BYTES;
    oob.main_crc = delta->target.main_crc;
    memcpy(oob.media_uuid, m3p->config.media_uuid, 16);
    oob.source_block = m3p->gc_victim;
    oob.source_page = m3p->gc_source_page[moved];
    oob.source_data_sequence = source->data_record_sequence;
    m3p_encode_oob(m3p->frame_oob[0], &oob);
    return m3p_child_program_start(m3p, 0, destination) ==
           FWLAB_SPINE_V0_OK;
}

static int start_gc_relocation(struct fwlab_m3p *m3p)
{
    uint8_t moved = m3p->gc_moved;
    struct m3p_map_record record;
    struct m3p_oob oob;
    struct fwlab_nfc_ppa ppa = {0, 0, 0, m3p->active_journal_block,
                                m3p->journal_page, 0};
    uint32_t record_sequence = m3p->record_sequence + 1u;
    uint32_t map_sequence = m3p->map_sequence + 1u;

    if (m3p->journal_page >= M3P_PAGES_PER_BLOCK ||
        record_sequence > m3p->config.record_sequence_limit ||
        map_sequence > m3p->config.record_sequence_limit) {
        return 0;
    }
    memset(&record, 0, sizeof(record));
    record.subtype = M3P_MAP_RELOCATION;
    record.delta_count = 1;
    record.flags = 1;
    record.transaction_sequence = m3p->gc_uid;
    record.predecessor_map_sequence = m3p->map_sequence;
    record.resulting_map_sequence = map_sequence;
    record.gc_uid = m3p->gc_uid;
    record.gc_source_block = m3p->gc_victim;
    record.gc_destination_block = m3p->gc_destination;
    record.gc_expected_live = m3p->gc_live_count;
    record.gc_moved = 0;
    record.delta[0] = m3p->gc_delta[moved];
    m3p_encode_map(m3p->frame_main[0], &record);
    memset(&oob, 0, sizeof(oob));
    oob.page_type = M3P_PAGE_MAP_TXN;
    oob.flags = 1;
    oob.copy_kind = M3P_COPY_GC;
    oob.namespace_id = 1;
    oob.lpn = UINT32_MAX;
    oob.erase_generation = m3p->block_erase_generation[ppa.block];
    oob.record_sequence = record_sequence;
    oob.transaction_sequence = m3p->gc_uid;
    oob.predecessor_map_sequence = m3p->map_sequence;
    oob.resulting_map_sequence = map_sequence;
    oob.main_length = M3P_PAGE_BYTES;
    oob.main_crc = m3p_crc32c(m3p->frame_main[0], M3P_PAGE_BYTES);
    memcpy(oob.media_uuid, m3p->config.media_uuid, 16);
    oob.source_block = m3p->gc_victim;
    oob.source_page = m3p->gc_source_page[moved];
    oob.source_data_sequence =
        m3p->gc_delta[moved].prior.data_record_sequence;
    m3p_encode_oob(m3p->frame_oob[0], &oob);
    return m3p_child_program_start(m3p, 0, ppa) == FWLAB_SPINE_V0_OK;
}

static int start_gc_switch(struct fwlab_m3p *m3p)
{
    struct m3p_map_record record;
    struct m3p_oob oob;
    struct fwlab_nfc_ppa ppa = {0, 0, 0, m3p->active_journal_block,
                                m3p->journal_page, 0};
    uint32_t record_sequence = m3p->record_sequence + 1u;
    uint32_t map_sequence = m3p->map_sequence + 1u;
    uint8_t index;

    memset(&record, 0, sizeof(record));
    record.subtype = M3P_MAP_GC_SWITCH;
    record.flags = 1;
    record.transaction_sequence = m3p->gc_uid;
    record.predecessor_map_sequence = m3p->map_sequence;
    record.resulting_map_sequence = map_sequence;
    record.gc_uid = m3p->gc_uid;
    record.gc_source_block = m3p->gc_victim;
    record.gc_destination_block = m3p->gc_destination;
    record.gc_expected_live = m3p->gc_live_count;
    record.gc_moved = m3p->gc_live_count;
    for (index = 0; index < m3p->gc_live_count; ++index) {
        record.relocation_sequence[index] =
            m3p->gc_relocation_sequence[index];
    }
    m3p_encode_map(m3p->frame_main[0], &record);
    memset(&oob, 0, sizeof(oob));
    oob.page_type = M3P_PAGE_MAP_TXN;
    oob.flags = 1;
    oob.copy_kind = M3P_COPY_GC;
    oob.namespace_id = 1;
    oob.lpn = UINT32_MAX;
    oob.erase_generation = m3p->block_erase_generation[ppa.block];
    oob.record_sequence = record_sequence;
    oob.transaction_sequence = m3p->gc_uid;
    oob.predecessor_map_sequence = m3p->map_sequence;
    oob.resulting_map_sequence = map_sequence;
    oob.main_length = M3P_PAGE_BYTES;
    oob.main_crc = m3p_crc32c(m3p->frame_main[0], M3P_PAGE_BYTES);
    memcpy(oob.media_uuid, m3p->config.media_uuid, 16);
    oob.source_block = m3p->gc_victim;
    oob.source_page = UINT8_MAX;
    m3p_encode_oob(m3p->frame_oob[0], &oob);
    return m3p_child_program_start(m3p, 0, ppa) == FWLAB_SPINE_V0_OK;
}

static void gc_fail(struct fwlab_m3p *m3p, uint32_t fault)
{
    m3p->gc_fault_code = fault == 0 ? 1 : fault;
    m3p->work_state = M3P_WORK_FAILED;
    m3p->quarantined = 1;
}

static int activate_destination_replacement(struct fwlab_m3p *m3p,
                                            uint32_t fault)
{
    uint8_t index;
    uint8_t old_destination = m3p->gc_destination;

    if (m3p->replacement_used ||
        m3p->replacement_block >= M3P_BLOCKS ||
        m3p->block_role[m3p->replacement_block] != M3P_ROLE_REPLACEMENT ||
        m3p->block_health[m3p->replacement_block] != FWLAB_NFC_BLOCK_GOOD ||
        m3p->block_next_page[m3p->replacement_block] != 0) {
        return 0;
    }
    if (m3p->child.state == M3P_CHILD_FAILED ||
        m3p->child.state == M3P_CHILD_DONE) {
        m3p_child_consume(m3p);
    }
    for (index = 0; index < m3p->gc_moved; ++index) {
        const struct m3p_delta *delta = &m3p->gc_delta[index];

        m3p->p2l[m3p_physical_index(delta->prior.block,
                                    delta->prior.page)] = M3P_P2L_LIVE;
        m3p->p2l[m3p_physical_index(delta->target.block,
                                    delta->target.page)] = M3P_P2L_ORPHAN;
    }
    m3p->block_role[old_destination] = M3P_ROLE_UNAVAILABLE;
    m3p->block_health[old_destination] = FWLAB_NFC_BLOCK_RUNTIME_BAD;
    m3p->gc_destination = m3p->replacement_block;
    m3p->block_role[m3p->gc_destination] = M3P_ROLE_RESERVE;
    m3p->replacement_used = 1;
    m3p->gc_uid = m3p->map_sequence + 1u;
    m3p->gc_moved = 0;
    m3p->gc_fault_code = fault;
    memset(m3p->gc_delta, 0, sizeof(m3p->gc_delta));
    memset(m3p->gc_relocation_sequence, 0,
           sizeof(m3p->gc_relocation_sequence));
    m3p->work_state = M3P_GC_PREPARE;
    return 1;
}

static int activate_erase_replacement(struct fwlab_m3p *m3p,
                                      uint32_t fault)
{
    uint8_t replacement = m3p->replacement_block;

    if (m3p->replacement_used || replacement >= M3P_BLOCKS ||
        m3p->block_role[replacement] != M3P_ROLE_REPLACEMENT ||
        m3p->block_health[replacement] != FWLAB_NFC_BLOCK_GOOD ||
        m3p->block_next_page[replacement] != 0) {
        return 0;
    }
    if (m3p->child.state == M3P_CHILD_FAILED ||
        m3p->child.state == M3P_CHILD_DONE) {
        m3p_child_consume(m3p);
    }
    m3p->block_role[m3p->gc_victim] = M3P_ROLE_UNAVAILABLE;
    m3p->block_health[m3p->gc_victim] = FWLAB_NFC_BLOCK_RUNTIME_BAD;
    m3p->block_role[replacement] = M3P_ROLE_RESERVE;
    m3p->reserve_block = replacement;
    m3p->replacement_used = 1;
    m3p->gc_fault_code = fault;
    m3p->operation.state = M3P_OPERATION_FREE;
    m3p->work_state = M3P_GC_DONE;
    m3p->work_kind = FWLAB_M3P_MAINTENANCE_NONE;
    if (m3p->recovery_resume_gc) {
        m3p->recovery_resume_gc = 0;
        m3p->recovery_completed = 1;
        m3p->ready = 1;
    }
    return 1;
}

int m3p_gc_drive(struct fwlab_m3p *m3p)
{
    if (m3p->work_state == M3P_GC_PREPARE) {
        if (m3p->checkpoint_flow != 0) {
            return m3p_checkpoint_drive(m3p);
        }
        if (m3p->journal_page != 0) {
            if (!m3p_start_checkpoint(m3p, M3P_OPERATION_FREE)) {
                gc_fail(m3p, 1);
                return 1;
            }
            return m3p_checkpoint_drive(m3p);
        }
        if (m3p->journal_page != 0 ||
            m3p->gc_live_count + 1u > M3P_PAGES_PER_BLOCK) {
            gc_fail(m3p, 2);
            return 1;
        }
        m3p->work_state = M3P_GC_READ;
        return 1;
    }
    if (m3p->work_state == M3P_GC_READ) {
        struct fwlab_nfc_ppa source = {0, 0, 0, m3p->gc_victim,
            m3p->gc_source_page[m3p->gc_moved], 0};

        if (m3p->child.state == M3P_CHILD_IDLE) {
            if (m3p_child_read_start(m3p, 0, source) != FWLAB_SPINE_V0_OK) {
                gc_fail(m3p, 3);
            }
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_FAILED) {
            gc_fail(m3p, m3p->child.fault_code);
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            uint16_t lpn = m3p->gc_source_lpn[m3p->gc_moved];

            if (!m3p_decode_oob(m3p->frame_oob[0],
                                &m3p->recovered_data[0]) ||
                m3p->recovered_data[0].page_type != M3P_PAGE_DATA ||
                m3p->recovered_data[0].lpn != lpn ||
                m3p->recovered_data[0].record_sequence !=
                    m3p->durable[lpn].data_record_sequence ||
                m3p_crc32c(m3p->frame_main[0], M3P_PAGE_BYTES) !=
                    m3p->recovered_data[0].main_crc) {
                gc_fail(m3p, 4);
                return 1;
            }
            m3p_child_consume(m3p);
            if (!start_gc_data_program(m3p)) {
                gc_fail(m3p, 5);
                return 1;
            }
            m3p->work_state = M3P_GC_PROGRAM;
            return 1;
        }
        return 0;
    }
    if (m3p->work_state == M3P_GC_PROGRAM) {
        if (m3p->child.state == M3P_CHILD_FAILED) {
            uint32_t fault = m3p->child.fault_code;

            if (!activate_destination_replacement(m3p, fault)) {
                gc_fail(m3p, fault);
            }
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            uint8_t moved = m3p->gc_moved;

            m3p_child_consume(m3p);
            ++m3p->record_sequence;
            m3p->block_next_page[m3p->gc_destination] = moved + 1u;
            m3p->p2l[m3p_physical_index(m3p->gc_victim,
                                        m3p->gc_source_page[moved])] =
                M3P_P2L_GC_SOURCE_PINNED;
            m3p->p2l[m3p_physical_index(m3p->gc_destination, moved)] =
                M3P_P2L_GC_DEST_STAGED;
            if (!start_gc_relocation(m3p)) {
                gc_fail(m3p, 6);
                return 1;
            }
            m3p->work_state = M3P_GC_RELOCATION;
            return 1;
        }
        return 0;
    }
    if (m3p->work_state == M3P_GC_RELOCATION) {
        if (m3p->child.state == M3P_CHILD_FAILED) {
            gc_fail(m3p, m3p->child.fault_code);
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            uint8_t moved = m3p->gc_moved;

            m3p_child_consume(m3p);
            ++m3p->record_sequence;
            ++m3p->map_sequence;
            ++m3p->journal_page;
            m3p->block_next_page[m3p->active_journal_block] =
                m3p->journal_page;
            m3p->gc_relocation_sequence[moved] = m3p->record_sequence;
            ++m3p->gc_moved;
            m3p->work_state = m3p->gc_moved < m3p->gc_live_count ?
                M3P_GC_READ : M3P_GC_SWITCH;
            return 1;
        }
        return 0;
    }
    if (m3p->work_state == M3P_GC_SWITCH) {
        if (m3p->child.state == M3P_CHILD_IDLE) {
            if (!start_gc_switch(m3p)) {
                gc_fail(m3p, 7);
            }
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_FAILED) {
            gc_fail(m3p, m3p->child.fault_code);
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            uint8_t index;

            m3p_child_consume(m3p);
            ++m3p->record_sequence;
            ++m3p->map_sequence;
            ++m3p->journal_page;
            m3p->block_next_page[m3p->active_journal_block] =
                m3p->journal_page;
            m3p->gc_switch_sequence = m3p->map_sequence;
            for (index = 0; index < m3p->gc_live_count; ++index) {
                m3p_publish_durable_delta(m3p, &m3p->gc_delta[index],
                                          m3p->map_sequence);
            }
            m3p->block_role[m3p->gc_destination] = M3P_ROLE_DATA;
            m3p->block_role[m3p->gc_victim] = M3P_ROLE_RESERVE;
            m3p->work_state = M3P_GC_ERASE;
            return 1;
        }
        return 0;
    }
    if (m3p->work_state == M3P_GC_ERASE) {
        struct fwlab_nfc_ppa victim = {0, 0, 0, m3p->gc_victim, 0, 0};

        if (m3p->child.state == M3P_CHILD_IDLE) {
            if (m3p_child_erase_start(m3p, victim) != FWLAB_SPINE_V0_OK) {
                gc_fail(m3p, 8);
            }
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_FAILED) {
            uint32_t fault = m3p->child.fault_code;

            if (!activate_erase_replacement(m3p, fault)) {
                gc_fail(m3p, fault);
            }
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            uint8_t victim_block = m3p->gc_victim;

            m3p->block_erase_generation[victim_block] =
                m3p->child.completion.final_erase_generation;
            ++m3p->block_erase_count[victim_block];
            m3p->block_next_page[victim_block] = 0;
            memset(&m3p->p2l[m3p_physical_index(victim_block, 0)],
                   M3P_P2L_FREE, M3P_PAGES_PER_BLOCK);
            m3p_child_consume(m3p);
            m3p->reserve_block = victim_block;
            m3p->operation.state = M3P_OPERATION_FREE;
            m3p->work_state = M3P_GC_DONE;
            m3p->work_kind = FWLAB_M3P_MAINTENANCE_NONE;
            if (m3p->recovery_resume_gc) {
                m3p->recovery_resume_gc = 0;
                m3p->recovery_completed = 1;
                m3p->ready = 1;
            }
            return 1;
        }
        return 0;
    }
    return 0;
}

enum fwlab_spine_result_v0 fwlab_m3p_force_gc_query(
    const struct fwlab_m3p *m3p, struct fwlab_m3p_gc_status *status)
{
    if (m3p == NULL || status == NULL || m3p->magic != M3P_MAGIC ||
        !m3p->initialized) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(status, 0, sizeof(*status));
    status->version = FWLAB_M3P_VERSION;
    status->size = (uint16_t)sizeof(*status);
    status->victim_block = m3p->gc_victim;
    status->destination_block = m3p->gc_destination;
    status->live_pages = m3p->gc_live_count;
    status->moved_pages = m3p->gc_moved;
    status->gc_uid = m3p->gc_uid;
    status->switch_map_sequence = m3p->gc_switch_sequence;
    status->successful_erase_count =
        m3p->block_erase_count[m3p->gc_victim];
    status->fault_code = m3p->gc_fault_code;
    status->reclaimable_pages = m3p->gc_reclaimable;
    if (m3p->quarantined || m3p->work_state == M3P_WORK_FAILED) {
        status->state = FWLAB_M3P_MAINTENANCE_QUARANTINED;
    } else if (m3p->work_state == M3P_GC_DONE) {
        status->state = FWLAB_M3P_MAINTENANCE_SUCCEEDED;
    } else if (m3p->work_kind == FWLAB_M3P_MAINTENANCE_GC) {
        status->state = FWLAB_M3P_MAINTENANCE_RUNNING;
    } else {
        status->state = FWLAB_M3P_MAINTENANCE_IDLE;
    }
    return FWLAB_SPINE_V0_OK;
}
