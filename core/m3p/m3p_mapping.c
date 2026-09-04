/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "m3p_internal.h"

#include <string.h>

uint16_t m3p_physical_index(uint8_t block, uint8_t page)
{
    return (uint16_t)(block * M3P_PAGES_PER_BLOCK + page);
}

int m3p_map_entry_equal(const struct m3p_map_entry *left,
                        const struct m3p_map_entry *right)
{
    return left != NULL && right != NULL &&
           left->state == right->state &&
           left->valid_mask == right->valid_mask &&
           left->block == right->block && left->page == right->page &&
           left->erase_generation == right->erase_generation &&
           left->data_record_sequence == right->data_record_sequence &&
           left->map_sequence == right->map_sequence &&
           left->main_crc == right->main_crc &&
           left->logical_version == right->logical_version;
}

int m3p_namespace_equal(const struct fwlab_block_namespace_ref_v0 *left,
                        const struct fwlab_block_namespace_ref_v0 *right)
{
    return left != NULL && right != NULL && left->word[0] == right->word[0] &&
           left->word[1] == right->word[1];
}

int m3p_token_equal(const struct fwlab_block_op_token_v0 *left,
                    const struct fwlab_block_op_token_v0 *right)
{
    return left != NULL && right != NULL &&
           left->version == right->version && left->size == right->size &&
           left->type_tag == right->type_tag &&
           left->provider_nonce == right->provider_nonce &&
           left->generation == right->generation &&
           memcmp(&left->action, &right->action, sizeof(left->action)) == 0;
}

void m3p_mapping_reset(struct fwlab_m3p *m3p)
{
    uint16_t lpn;
    uint8_t block;

    memset(m3p->durable, 0, sizeof(m3p->durable));
    memset(m3p->visible, 0, sizeof(m3p->visible));
    for (lpn = 0; lpn < M3P_LPN_COUNT; ++lpn) {
        m3p->durable[lpn].block = UINT8_MAX;
        m3p->durable[lpn].page = UINT8_MAX;
        m3p->visible[lpn] = m3p->durable[lpn];
    }
    memset(m3p->p2l, M3P_P2L_FREE, sizeof(m3p->p2l));
    memset(m3p->block_next_page, 0, sizeof(m3p->block_next_page));
    memset(m3p->block_erase_generation, 0,
           sizeof(m3p->block_erase_generation));
    memset(m3p->block_erase_count, 0, sizeof(m3p->block_erase_count));
    memset(m3p->block_health, FWLAB_NFC_BLOCK_GOOD,
           sizeof(m3p->block_health));
    for (block = 0; block < M3P_BLOCKS; ++block) {
        if (block < 10) {
            m3p->block_role[block] = M3P_ROLE_DATA;
        } else if (block < 12) {
            m3p->block_role[block] = M3P_ROLE_JOURNAL;
        } else if (block < 14) {
            m3p->block_role[block] = M3P_ROLE_CHECKPOINT;
        } else if (block == 14) {
            m3p->block_role[block] = M3P_ROLE_RESERVE;
        } else {
            m3p->block_role[block] = M3P_ROLE_REPLACEMENT;
        }
    }
    memset(m3p->pending, 0, sizeof(m3p->pending));
    m3p->pending_count = 0;
    m3p->pending_head = 0;
    m3p->pending_tail = 0;
    m3p->active_journal_block = 10;
    m3p->inactive_journal_block = 11;
    m3p->active_checkpoint_block = 12;
    m3p->inactive_checkpoint_block = 13;
    m3p->journal_page = 0;
    m3p->checkpoint_page = 0;
    m3p->reserve_block = 14;
    m3p->replacement_block = 15;
    m3p->replacement_used = 0;
    m3p->record_sequence = 0;
    m3p->map_sequence = 0;
    m3p->host_sequence = 0;
    m3p->durable_frontier = 0;
    m3p->checkpoint_generation = 0;
    m3p->checkpoint_covered_sequence = 0;
    m3p->journal_generation = 1;
}

int m3p_allocate_data_page(struct fwlab_m3p *m3p,
                           struct fwlab_nfc_ppa *ppa)
{
    uint8_t selected = UINT8_MAX;
    uint8_t block;

    if (m3p == NULL || ppa == NULL) {
        return 0;
    }
    for (block = 0; block < M3P_BLOCKS; ++block) {
        if (m3p->block_role[block] != M3P_ROLE_DATA ||
            m3p->block_next_page[block] >= M3P_PAGES_PER_BLOCK) {
            continue;
        }
        if (selected == UINT8_MAX ||
            m3p->block_erase_count[block] <
                m3p->block_erase_count[selected] ||
            (m3p->block_erase_count[block] ==
                 m3p->block_erase_count[selected] && block < selected)) {
            selected = block;
        }
    }
    if (selected == UINT8_MAX) {
        return 0;
    }
    memset(ppa, 0, sizeof(*ppa));
    ppa->block = selected;
    ppa->page = m3p->block_next_page[selected]++;
    m3p->p2l[m3p_physical_index(selected, (uint8_t)ppa->page)] =
        M3P_P2L_VISIBLE_PENDING;
    return 1;
}

void m3p_publish_visible_delta(struct fwlab_m3p *m3p,
                               const struct m3p_delta *delta)
{
    if (delta->prior.state == M3P_L2P_VALUE) {
        m3p->p2l[m3p_physical_index(delta->prior.block,
                                    delta->prior.page)] =
            M3P_P2L_DURABLE_PINNED;
    }
    if (delta->target.state == M3P_L2P_VALUE) {
        m3p->p2l[m3p_physical_index(delta->target.block,
                                    delta->target.page)] =
            M3P_P2L_VISIBLE_PENDING;
    }
    m3p->visible[delta->lpn] = delta->target;
}

void m3p_publish_durable_delta(struct fwlab_m3p *m3p,
                               const struct m3p_delta *delta,
                               uint32_t map_sequence)
{
    struct m3p_map_entry target = delta->target;

    if (delta->prior.state == M3P_L2P_VALUE &&
        (delta->target.state != M3P_L2P_VALUE ||
         delta->prior.block != delta->target.block ||
         delta->prior.page != delta->target.page)) {
        m3p->p2l[m3p_physical_index(delta->prior.block,
                                    delta->prior.page)] = M3P_P2L_STALE;
    }
    if (target.state == M3P_L2P_VALUE) {
        m3p->p2l[m3p_physical_index(target.block, target.page)] =
            M3P_P2L_LIVE;
    }
    target.map_sequence = map_sequence;
    m3p->durable[delta->lpn] = target;
    m3p->visible[delta->lpn] = target;
}

int m3p_pending_append(struct fwlab_m3p *m3p, uint8_t subtype,
                       uint32_t host_sequence,
                       const struct m3p_delta *delta, uint8_t delta_count)
{
    struct m3p_pending *pending;

    if (m3p == NULL || delta == NULL || delta_count == 0 ||
        delta_count > M3P_MAX_DELTAS ||
        m3p->pending_count >= M3P_MAX_PENDING) {
        return 0;
    }
    pending = &m3p->pending[m3p->pending_tail];
    if (pending->active) {
        return 0;
    }
    memset(pending, 0, sizeof(*pending));
    pending->active = 1;
    pending->subtype = subtype;
    pending->delta_count = delta_count;
    pending->host_sequence = host_sequence;
    pending->transaction_sequence = host_sequence;
    memcpy(pending->delta, delta,
           (size_t)delta_count * sizeof(pending->delta[0]));
    m3p->pending_tail = (uint8_t)((m3p->pending_tail + 1u) % M3P_MAX_PENDING);
    ++m3p->pending_count;
    return 1;
}

struct m3p_pending *m3p_pending_front(struct fwlab_m3p *m3p)
{
    if (m3p == NULL || m3p->pending_count == 0 ||
        !m3p->pending[m3p->pending_head].active) {
        return NULL;
    }
    return &m3p->pending[m3p->pending_head];
}

void m3p_pending_pop(struct fwlab_m3p *m3p)
{
    if (m3p != NULL && m3p->pending_count != 0) {
        memset(&m3p->pending[m3p->pending_head], 0,
               sizeof(m3p->pending[m3p->pending_head]));
        m3p->pending_head =
            (uint8_t)((m3p->pending_head + 1u) % M3P_MAX_PENDING);
        --m3p->pending_count;
    }
}

int m3p_pending_overlaps(const struct fwlab_m3p *m3p, uint16_t first_lpn,
                         uint16_t last_lpn)
{
    uint8_t ordinal;

    if (m3p == NULL) {
        return 1;
    }
    for (ordinal = 0; ordinal < m3p->pending_count; ++ordinal) {
        uint8_t slot = (uint8_t)((m3p->pending_head + ordinal) %
                                 M3P_MAX_PENDING);
        const struct m3p_pending *pending = &m3p->pending[slot];
        uint8_t delta;

        for (delta = 0; delta < pending->delta_count; ++delta) {
            if (pending->delta[delta].lpn >= first_lpn &&
                pending->delta[delta].lpn <= last_lpn) {
                return 1;
            }
        }
    }
    return 0;
}
