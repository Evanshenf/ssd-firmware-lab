/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_NFC_PAGE_V2_INTERNAL_H
#define FWLAB_NFC_PAGE_V2_INTERNAL_H

#include "fwlab/private/nfc_page_v2_model.h"
#include <string.h>

static inline bool page2_zero(const void *bytes, size_t length)
{
    const uint8_t *p = bytes;
    for (size_t i = 0; i < length; ++i) if (p[i]) return false;
    return true;
}
static inline bool page2_span(const void *p, size_t n)
{ return p && n && n <= UINTPTR_MAX - (uintptr_t)p; }
static inline bool page2_key_equal(const struct fwlab_nfc_operation_token *a,
                                  const struct fwlab_nfc_operation_token *b)
{
    return a->instance_nonce == b->instance_nonce && a->operation_uid == b->operation_uid &&
        a->controller_epoch == b->controller_epoch && a->generation == b->generation;
}
static inline bool page2_key_valid(const struct fwlab_nfc_page_v2_config *c,
                                  const struct fwlab_nfc_operation_token *k)
{
    return k && k->instance_nonce == c->instance_nonce &&
        k->controller_epoch == c->controller_epoch && k->generation == c->generation &&
        k->operation_uid && k->operation_uid <= c->operation_uid_limit;
}
static inline uint8_t page2_shape_reason(const struct fwlab_nfc_page_v2_config *c,
                                        const struct fwlab_nfc_page_v2_request *r)
{
    const struct fwlab_nfc_geometry *g = &c->geometry;
    if (!r || r->version != FWLAB_NFC_PAGE_V2_VERSION || r->size != sizeof(*r) ||
        r->reserved0 || r->reserved1) return FWLAB_NFC_REASON_INTERNAL;
    if (!page2_key_valid(c, &r->operation)) return FWLAB_NFC_REASON_STALE;
    if (r->retry_step || r->fault_tag) return FWLAB_NFC_REASON_UNSUPPORTED;
    if (r->kind < FWLAB_NFC_PAGE_V2_READ_GROUP || r->kind > FWLAB_NFC_PAGE_V2_ERASE)
        return FWLAB_NFC_REASON_UNSUPPORTED;
    if (r->first.reserved || r->first.channel >= g->channels ||
        r->first.lun >= g->luns_per_channel || r->first.plane >= g->planes_per_lun ||
        r->first.block >= g->blocks_per_plane || r->first.page >= g->pages_per_block ||
        !r->page_count || r->page_count > FWLAB_NFC_PAGE_V2_MAX_PAGES ||
        r->page_count > (uint32_t)g->pages_per_block - r->first.page)
        return FWLAB_NFC_REASON_RANGE;
    if (r->kind == FWLAB_NFC_PAGE_V2_ERASE && (r->page_count != 1 || r->first.page))
        return FWLAB_NFC_REASON_RANGE;
    if (r->kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP) {
        if (!page2_span(r->main, r->main_bytes) || !page2_span(r->oob, r->oob_bytes) ||
            r->main_bytes != (size_t)r->page_count * FWLAB_NFC_PAGE_V2_MAIN_BYTES ||
            r->oob_bytes != (size_t)r->page_count * FWLAB_NFC_PAGE_V2_OOB_BYTES)
            return FWLAB_NFC_REASON_RANGE;
    } else if (r->main || r->main_bytes || r->oob || r->oob_bytes)
        return FWLAB_NFC_REASON_RANGE;
    return FWLAB_NFC_REASON_NONE;
}
static inline bool page2_canonical_equal(const struct fwlab_nfc_page_v2_request *a,
                                        const struct fwlab_nfc_page_v2_request *b)
{
    return a->kind == b->kind && a->page_count == b->page_count &&
        memcmp(&a->first, &b->first, sizeof(a->first)) == 0 &&
        a->main_bytes == b->main_bytes && a->oob_bytes == b->oob_bytes &&
        a->retry_step == b->retry_step && a->fault_tag == b->fault_tag;
}
static inline bool page2_block_valid(const struct fwlab_nfc_geometry *g,
                                    const struct fwlab_nand_block_info *b)
{
    return b->version == FWLAB_NFC_CONTRACT_VERSION && b->size == sizeof(*b) &&
        !b->reserved0 && page2_zero(b->reserved1, sizeof(b->reserved1)) &&
        b->health <= FWLAB_NFC_BLOCK_RUNTIME_BAD && b->erase_state <= FWLAB_NAND_ERASE_TORN &&
        b->next_program_page <= g->pages_per_block &&
        b->successful_erase_count <= b->erase_attempt_count;
}
static inline bool page2_cell_valid(const struct fwlab_nand_page_info *p,
                                   const struct fwlab_nand_block_info *b, uint32_t index)
{
    if (p->version != FWLAB_NFC_CONTRACT_VERSION || p->size != sizeof(*p) ||
        !page2_zero(p->reserved, sizeof(p->reserved)) || p->state > FWLAB_NAND_PAGE_TORN ||
        p->program_count > 1 || p->erase_generation_seen != b->erase_generation)
        return false;
    if (b->erase_state == FWLAB_NAND_ERASE_TORN) return true;
    if (p->state == FWLAB_NAND_PAGE_ERASED)
        return !p->program_count && index >= b->next_program_page;
    return p->program_count == 1 && index < b->next_program_page;
}
static inline void page2_generation_health(struct fwlab_nfc_page_v2_page_result *p,
                                          const struct fwlab_nand_block_info *b)
{
    p->facts_valid = FWLAB_NFC_PAGE_V2_FACT_GENERATION | FWLAB_NFC_PAGE_V2_FACT_HEALTH;
    p->base_erase_generation = p->final_erase_generation = b->erase_generation;
    p->block_health = b->health;
}
/* Shared R0/LAB cell interpretation. Caller first validates block and page
 * envelopes; CRC/backing-byte integrity remains the media's responsibility. */
static inline void page2_read_fact(struct fwlab_nfc_page_v2_page_result *out,
                                  const struct fwlab_nand_page_info *p,
                                  const struct fwlab_nand_block_info *b)
{
    page2_generation_health(out, b);
    out->facts_valid |= FWLAB_NFC_PAGE_V2_FACT_CELL | FWLAB_NFC_PAGE_V2_FACT_ECC |
                        FWLAB_NFC_PAGE_V2_FACT_EFFECT;
    out->page_state = p->state;
    out->program_count = p->program_count;
    out->integrity = p->state == FWLAB_NAND_PAGE_TORN || b->erase_state == FWLAB_NAND_ERASE_TORN ?
        FWLAB_NFC_INTEGRITY_TORN : FWLAB_NFC_INTEGRITY_COMPLETE;
    if (b->health != FWLAB_NFC_BLOCK_GOOD) out->reason = FWLAB_NFC_REASON_BAD_BLOCK;
    else if (b->erase_state == FWLAB_NAND_ERASE_TORN || p->state == FWLAB_NAND_PAGE_TORN) {
        out->ecc_status = FWLAB_NFC_ECC_UNCORRECTABLE;
        out->reason = FWLAB_NFC_REASON_ECC_UNCORRECTABLE;
    } else {
        out->ecc_status = FWLAB_NFC_ECC_CLEAN;
        out->valid_region_mask = FWLAB_NFC_REGION_MASK;
    }
}

#endif
