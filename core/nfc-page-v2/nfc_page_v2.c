/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "fwlab/private/nfc_page_v2_model.h"

#include <stdalign.h>
#include <string.h>

#define PAGE2_MAGIC UINT64_C(0x50414745324e4643)
enum page2_state { PAGE2_EMPTY, PAGE2_QUEUED, PAGE2_RUNNING, PAGE2_DONE };
struct fwlab_nfc_page_v2_model {
    uint64_t magic;
    struct fwlab_nfc_page_v2_config config;
    struct fwlab_nand_batch_v2 media;
    struct fwlab_nfc_page_v2_request request;
    struct fwlab_nfc_page_v2_result result;
    uint64_t last_uid;
    uint8_t state;
    uint8_t closed;
    uint8_t quarantined;
    uint8_t cancel_requested;
    /* Private window alignment; caller payload spans retain their contract. */
    _Alignas(64) uint8_t main[FWLAB_NFC_PAGE_V2_MAX_PAGES * FWLAB_NFC_PAGE_V2_MAIN_BYTES];
    uint8_t oob[FWLAB_NFC_PAGE_V2_MAX_PAGES * FWLAB_NFC_PAGE_V2_OOB_BYTES];
    uint8_t scratch_main[FWLAB_NFC_PAGE_V2_MAIN_BYTES];
    uint8_t scratch_oob[FWLAB_NFC_PAGE_V2_OOB_BYTES];
    union {
        struct fwlab_nand_page_info page[FWLAB_NFC_PAGE_V2_MAX_PAGES];
        struct fwlab_nand_media_result effect[FWLAB_NFC_PAGE_V2_MAX_PAGES];
    } lower;
};

_Static_assert(sizeof(struct fwlab_nfc_page_v2_result) <= UINT16_MAX,
               "result size fits its versioned envelope");
_Static_assert(offsetof(struct fwlab_nfc_page_v2_model, main) % 64u == 0 &&
               offsetof(struct fwlab_nfc_page_v2_model, oob) % 64u == 0,
               "private payload windows are cache-line aligned");

static bool live(const struct fwlab_nfc_page_v2_model *m)
{ return m && m->magic == PAGE2_MAGIC; }

static bool zeros(const void *bytes, size_t length)
{
    const uint8_t *p = bytes;
    for (size_t i = 0; i < length; ++i) if (p[i]) return false;
    return true;
}

static bool span(const void *p, size_t length)
{ return p && length && length <= UINTPTR_MAX - (uintptr_t)p; }

static bool overlap(const void *a, size_t an, const void *b, size_t bn)
{
    if (!an || !bn) return false;
    return (uintptr_t)a < (uintptr_t)b + bn &&
           (uintptr_t)b < (uintptr_t)a + an;
}

static bool outside(const struct fwlab_nfc_page_v2_model *m,
                    const void *p, size_t n)
{ return span(p, n) && !overlap(m, sizeof(*m), p, n); }

static bool geometry_valid(const struct fwlab_nfc_geometry *g)
{
    return g->version == FWLAB_NFC_CONTRACT_VERSION && g->size == sizeof(*g) &&
        g->channels && g->channels <= 4 && g->luns_per_channel &&
        g->luns_per_channel <= 4 && g->planes_per_lun && g->planes_per_lun <= 4 &&
        g->blocks_per_plane && (g->pages_per_block == 32 || g->pages_per_block == 64) &&
        g->plane_parallelism_per_lun && g->plane_parallelism_per_lun <= g->planes_per_lun &&
        g->main_bytes_per_page == FWLAB_NFC_PAGE_V2_MAIN_BYTES &&
        g->oob_bytes_per_page == FWLAB_NFC_PAGE_V2_OOB_BYTES &&
        g->max_programs_per_erase == 1 && g->program_order == FWLAB_NFC_PROGRAM_ASCENDING &&
        !g->reserved0 && zeros(g->reserved1, sizeof(g->reserved1));
}

static bool key_equal(const struct fwlab_nfc_operation_token *a,
                      const struct fwlab_nfc_operation_token *b)
{
    return a->instance_nonce == b->instance_nonce && a->operation_uid == b->operation_uid &&
        a->controller_epoch == b->controller_epoch && a->generation == b->generation;
}

static bool key_valid(const struct fwlab_nfc_page_v2_model *m,
                      const struct fwlab_nfc_operation_token *key)
{
    return key && key->instance_nonce == m->config.instance_nonce &&
        key->controller_epoch == m->config.controller_epoch &&
        key->generation == m->config.generation && key->operation_uid &&
        key->operation_uid <= m->config.operation_uid_limit;
}

static uint8_t shape_reason(const struct fwlab_nfc_page_v2_model *m,
                            const struct fwlab_nfc_page_v2_request *r)
{
    const struct fwlab_nfc_geometry *g = &m->config.geometry;
    if (!r || r->version != FWLAB_NFC_PAGE_V2_VERSION || r->size != sizeof(*r) ||
        r->reserved0 || r->reserved1) return FWLAB_NFC_REASON_INTERNAL;
    if (!key_valid(m, &r->operation)) return FWLAB_NFC_REASON_STALE;
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
        if (!span(r->main, r->main_bytes) || !span(r->oob, r->oob_bytes) ||
            r->main_bytes != (size_t)r->page_count * FWLAB_NFC_PAGE_V2_MAIN_BYTES ||
            r->oob_bytes != (size_t)r->page_count * FWLAB_NFC_PAGE_V2_OOB_BYTES)
            return FWLAB_NFC_REASON_RANGE;
    } else if (r->main || r->main_bytes || r->oob || r->oob_bytes)
        return FWLAB_NFC_REASON_RANGE;
    return FWLAB_NFC_REASON_NONE;
}

static bool canonical_equal(const struct fwlab_nfc_page_v2_request *a,
                            const struct fwlab_nfc_page_v2_request *b)
{
    return a->kind == b->kind && a->page_count == b->page_count &&
        memcmp(&a->first, &b->first, sizeof(a->first)) == 0 &&
        a->main_bytes == b->main_bytes && a->oob_bytes == b->oob_bytes &&
        a->retry_step == b->retry_step && a->fault_tag == b->fault_tag;
}

static struct fwlab_nfc_submit_result disposition(uint32_t value, uint32_t reason)
{
    struct fwlab_nfc_submit_result r = {value, reason};
    return r;
}

static struct fwlab_nfc_submit_result try_submit(void *opaque,
    const struct fwlab_nfc_page_v2_request *r)
{
    struct fwlab_nfc_page_v2_model *m = opaque;
    uint8_t reason;
    if (!live(m)) return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_INTERNAL);
    reason = shape_reason(m, r);
    if (reason) return disposition(FWLAB_NFC_REJECTED, reason);
    if (m->state != PAGE2_EMPTY && key_equal(&r->operation, &m->request.operation))
        return disposition(canonical_equal(r, &m->request) ? FWLAB_NFC_ACCEPTED : FWLAB_NFC_REJECTED,
                           canonical_equal(r, &m->request) ? FWLAB_NFC_REASON_NONE : FWLAB_NFC_REASON_STALE);
    if (m->closed || m->quarantined)
        return disposition(FWLAB_NFC_REJECTED, m->closed ? FWLAB_NFC_REASON_RESET : FWLAB_NFC_REASON_INTERNAL);
    if (r->operation.operation_uid <= m->last_uid)
        return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_STALE);
    if (m->state != PAGE2_EMPTY) return disposition(FWLAB_NFC_BACKPRESSURE, FWLAB_NFC_REASON_NONE);
    if (!outside(m, r, sizeof(*r)) ||
        (r->kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP &&
         (!outside(m, r->main, r->main_bytes) || !outside(m, r->oob, r->oob_bytes))))
        return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_RANGE);
    m->request = *r;
    if (r->kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP) {
        memcpy(m->main, r->main, r->main_bytes);
        memcpy(m->oob, r->oob, r->oob_bytes);
    }
    /* Never retain caller payload pointers after the synchronous snapshot. */
    m->request.main = NULL;
    m->request.oob = NULL;
    memset(&m->result, 0, sizeof(m->result));
    m->result.version = FWLAB_NFC_PAGE_V2_VERSION;
    m->result.size = sizeof(m->result);
    m->result.operation = r->operation;
    m->result.first = r->first;
    m->result.page_count = r->page_count;
    m->result.kind = r->kind;
    m->last_uid = r->operation.operation_uid;
    m->cancel_requested = 0;
    m->state = PAGE2_QUEUED;
    return disposition(FWLAB_NFC_ACCEPTED, FWLAB_NFC_REASON_NONE);
}

static void failure(struct fwlab_nfc_page_v2_model *m, uint8_t reason,
                    enum fwlab_nfc_api_result status, bool uncertain)
{
    m->result.terminal = FWLAB_NFC_TERMINAL_FAILED;
    m->result.reason = reason;
    m->result.backend_status = status;
    m->result.read_valid = 0;
    if (uncertain) {
        m->result.effect = FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN;
        memset(m->result.page, 0, sizeof(m->result.page));
        for (uint32_t i = 0; i < m->request.page_count; ++i) {
            m->result.page[i].effect = FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN;
            m->result.page[i].facts_valid = FWLAB_NFC_PAGE_V2_FACT_EFFECT;
        }
    }
    if (status != FWLAB_NFC_API_OK || uncertain) m->quarantined = 1;
}

static bool block_valid(const struct fwlab_nfc_page_v2_model *m,
                        const struct fwlab_nand_block_info *b)
{
    return b->version == FWLAB_NFC_CONTRACT_VERSION && b->size == sizeof(*b) &&
        !b->reserved0 && zeros(b->reserved1, sizeof(b->reserved1)) &&
        b->health <= FWLAB_NFC_BLOCK_RUNTIME_BAD && b->erase_state <= FWLAB_NAND_ERASE_TORN &&
        b->next_program_page <= m->config.geometry.pages_per_block &&
        b->successful_erase_count <= b->erase_attempt_count;
}

static bool page_valid(const struct fwlab_nand_page_info *p,
                       const struct fwlab_nand_block_info *b, uint32_t index)
{
    if (p->version != FWLAB_NFC_CONTRACT_VERSION || p->size != sizeof(*p) ||
        !zeros(p->reserved, sizeof(p->reserved)) || p->state > FWLAB_NAND_PAGE_TORN ||
        p->program_count > 1 || p->erase_generation_seen != b->erase_generation)
        return false;
    if (b->erase_state == FWLAB_NAND_ERASE_TORN) return true;
    if (p->state == FWLAB_NAND_PAGE_ERASED)
        return !p->program_count && index >= b->next_program_page;
    return p->program_count == 1 && index < b->next_program_page;
}

static void generation_health(struct fwlab_nfc_page_v2_page_result *p,
                              const struct fwlab_nand_block_info *b)
{
    p->facts_valid = FWLAB_NFC_PAGE_V2_FACT_GENERATION | FWLAB_NFC_PAGE_V2_FACT_HEALTH;
    p->base_erase_generation = p->final_erase_generation = b->erase_generation;
    p->block_health = b->health;
}

static void read_group(struct fwlab_nfc_page_v2_model *m)
{
    const struct fwlab_nfc_page_v2_request *r = &m->request;
    struct fwlab_nand_block_info b = {0};
    enum fwlab_nfc_api_result status;
    bool valid = true;
    memset(&m->lower, 0, sizeof(m->lower));
    status = m->media.ops->read_pages(m->media.scalar.context, &r->first, r->page_count,
        m->main, (size_t)r->page_count * FWLAB_NFC_PAGE_V2_MAIN_BYTES,
        m->oob, (size_t)r->page_count * FWLAB_NFC_PAGE_V2_OOB_BYTES,
        m->lower.page, FWLAB_NFC_PAGE_V2_MAX_PAGES, &b);
    if (status != FWLAB_NFC_API_OK || !block_valid(m, &b)) {
        failure(m, FWLAB_NFC_REASON_INTERNAL,
                status == FWLAB_NFC_API_OK ? FWLAB_NFC_API_INVARIANT_FAILURE : status, false);
        return;
    }
    for (uint32_t i = 0; i < r->page_count; ++i) {
        const struct fwlab_nand_page_info *p = &m->lower.page[i];
        struct fwlab_nfc_page_v2_page_result *out = &m->result.page[i];
        if (!page_valid(p, &b, r->first.page + i)) {
            failure(m, FWLAB_NFC_REASON_INTERNAL, FWLAB_NFC_API_INVARIANT_FAILURE, false);
            return;
        }
        generation_health(out, &b);
        out->facts_valid |= FWLAB_NFC_PAGE_V2_FACT_CELL | FWLAB_NFC_PAGE_V2_FACT_ECC |
                            FWLAB_NFC_PAGE_V2_FACT_EFFECT;
        out->page_state = p->state;
        out->program_count = p->program_count;
        out->integrity = p->state == FWLAB_NAND_PAGE_TORN || b.erase_state == FWLAB_NAND_ERASE_TORN ?
            FWLAB_NFC_INTEGRITY_TORN : FWLAB_NFC_INTEGRITY_COMPLETE;
        if (b.health != FWLAB_NFC_BLOCK_GOOD) {
            out->reason = FWLAB_NFC_REASON_BAD_BLOCK;
            valid = false;
        } else if (b.erase_state == FWLAB_NAND_ERASE_TORN || p->state == FWLAB_NAND_PAGE_TORN) {
            out->ecc_status = FWLAB_NFC_ECC_UNCORRECTABLE;
            out->reason = FWLAB_NFC_REASON_ECC_UNCORRECTABLE;
            valid = false;
        } else {
            out->ecc_status = FWLAB_NFC_ECC_CLEAN;
            out->valid_region_mask = FWLAB_NFC_REGION_MASK;
        }
        if (!m->result.reason && out->reason) m->result.reason = out->reason;
    }
    m->result.read_valid = (uint8_t)valid;
    m->result.terminal = valid ? FWLAB_NFC_TERMINAL_SUCCESS : FWLAB_NFC_TERMINAL_FAILED;
}

static bool preflight(struct fwlab_nfc_page_v2_model *m, struct fwlab_nand_block_info *b)
{
    struct fwlab_nand_page_info p = {0};
    enum fwlab_nfc_api_result status = m->media.ops->read_pages(m->media.scalar.context,
        &m->request.first, 1, m->scratch_main, sizeof(m->scratch_main),
        m->scratch_oob, sizeof(m->scratch_oob), &p, 1, b);
    if (status != FWLAB_NFC_API_OK || !block_valid(m, b) ||
        !page_valid(&p, b, m->request.first.page)) {
        failure(m, FWLAB_NFC_REASON_INTERNAL,
                status == FWLAB_NFC_API_OK ? FWLAB_NFC_API_INVARIANT_FAILURE : status, false);
        return false;
    }
    for (uint32_t i = 0; i < m->request.page_count; ++i)
        generation_health(&m->result.page[i], b);
    if (b->health != FWLAB_NFC_BLOCK_GOOD) {
        failure(m, FWLAB_NFC_REASON_BAD_BLOCK, FWLAB_NFC_API_OK, false);
        return false;
    }
    if (m->request.kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP &&
        (b->erase_state != FWLAB_NAND_ERASE_CLEAN || p.state != FWLAB_NAND_PAGE_ERASED ||
         p.program_count || m->request.first.page != b->next_program_page)) {
        failure(m, m->request.first.page != b->next_program_page ?
            FWLAB_NFC_REASON_PROGRAM_ORDER : FWLAB_NFC_REASON_NOT_ERASED, FWLAB_NFC_API_OK, false);
        return false;
    }
    return true;
}

static bool effect_valid(const struct fwlab_nand_media_result *r)
{
    return r->version == FWLAB_NFC_CONTRACT_VERSION && r->size == sizeof(*r) &&
        zeros(r->reserved0, sizeof(r->reserved0)) && !r->reserved1 &&
        r->physical_outcome <= FWLAB_NFC_PHYS_APPLIED && r->integrity <= FWLAB_NFC_INTEGRITY_TORN &&
        r->block_health <= FWLAB_NFC_BLOCK_RUNTIME_BAD && r->reason <= FWLAB_NFC_REASON_INTERNAL &&
        !(r->applied_region_mask & (uint8_t)~FWLAB_NFC_REGION_MASK) &&
        (r->physical_outcome != FWLAB_NFC_PHYS_NO_EFFECT ||
         (!r->applied_main_bytes && !r->applied_oob_bytes && !r->applied_pages &&
          !r->applied_region_mask && r->integrity == FWLAB_NFC_INTEGRITY_NOT_APPLICABLE &&
          r->base_erase_generation == r->final_erase_generation));
}

static void copy_effect(struct fwlab_nfc_page_v2_page_result *p,
                        const struct fwlab_nand_media_result *r, bool complete)
{
    memset(p, 0, sizeof(*p));
    p->facts_valid = FWLAB_NFC_PAGE_V2_FACT_GENERATION | FWLAB_NFC_PAGE_V2_FACT_HEALTH |
                     FWLAB_NFC_PAGE_V2_FACT_EFFECT;
    p->base_erase_generation = r->base_erase_generation;
    p->final_erase_generation = r->final_erase_generation;
    p->block_health = r->block_health;
    p->integrity = r->integrity;
    p->applied_region_mask = r->applied_region_mask;
    p->applied_main_bytes = r->applied_main_bytes;
    p->applied_oob_bytes = r->applied_oob_bytes;
    p->applied_pages = r->applied_pages;
    p->reason = r->reason;
    p->effect = r->physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT ? FWLAB_NFC_PAGE_V2_EFFECT_NONE :
        complete ? FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE : FWLAB_NFC_PAGE_V2_EFFECT_NONCOMPLETE;
}

static void mutate(struct fwlab_nfc_page_v2_model *m)
{
    const struct fwlab_nfc_page_v2_request *r = &m->request;
    struct fwlab_nand_block_info b = {0};
    enum fwlab_nfc_api_result status;
    bool all_complete = true, all_none = true;
    bool erase = r->kind == FWLAB_NFC_PAGE_V2_ERASE;
    if (!preflight(m, &b)) return;
    memset(&m->lower, 0, sizeof(m->lower));
    if (erase)
        status = m->media.scalar.ops->erase(m->media.scalar.context, &r->first,
            m->config.geometry.pages_per_block, FWLAB_NFC_INTEGRITY_COMPLETE, m->lower.effect);
    else
        status = m->media.ops->program_pages(m->media.scalar.context, &r->first, r->page_count,
            m->main, r->main_bytes, m->oob, r->oob_bytes, m->lower.effect, FWLAB_NFC_PAGE_V2_MAX_PAGES);
    if (status != FWLAB_NFC_API_OK) {
        failure(m, FWLAB_NFC_REASON_INTERNAL, status, true);
        return;
    }
    for (uint32_t i = 0; i < r->page_count; ++i) {
        const struct fwlab_nand_media_result *p = &m->lower.effect[i];
        bool complete;
        if (!effect_valid(p) || p->base_erase_generation != b.erase_generation ||
            (!erase && (p->final_erase_generation != b.erase_generation ||
             p->applied_main_bytes > FWLAB_NFC_PAGE_V2_MAIN_BYTES ||
             p->applied_oob_bytes > FWLAB_NFC_PAGE_V2_OOB_BYTES)) ||
            (erase && p->applied_pages > m->config.geometry.pages_per_block)) {
            failure(m, FWLAB_NFC_REASON_INTERNAL, FWLAB_NFC_API_INVARIANT_FAILURE, true);
            return;
        }
        complete = p->physical_outcome == FWLAB_NFC_PHYS_APPLIED &&
            p->integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
            p->block_health == FWLAB_NFC_BLOCK_GOOD && p->reason == FWLAB_NFC_REASON_NONE;
        if (erase) complete = complete && p->applied_pages == m->config.geometry.pages_per_block &&
            p->final_erase_generation > p->base_erase_generation;
        else complete = complete && p->applied_main_bytes == FWLAB_NFC_PAGE_V2_MAIN_BYTES &&
            p->applied_oob_bytes == FWLAB_NFC_PAGE_V2_OOB_BYTES &&
            p->applied_region_mask == FWLAB_NFC_REGION_MASK;
        copy_effect(&m->result.page[i], p, complete);
        all_complete = all_complete && complete;
        all_none = all_none && p->physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT;
        if (!m->result.reason && p->reason) m->result.reason = p->reason;
    }
    if (all_complete) {
        m->result.effect = FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE;
        m->result.terminal = FWLAB_NFC_TERMINAL_SUCCESS;
    } else {
        m->result.effect = all_none ? FWLAB_NFC_PAGE_V2_EFFECT_NONE : FWLAB_NFC_PAGE_V2_EFFECT_NONCOMPLETE;
        m->result.terminal = FWLAB_NFC_TERMINAL_FAILED;
        if (!m->result.reason) m->result.reason = erase ? FWLAB_NFC_REASON_ERASE_FAILURE : FWLAB_NFC_REASON_PROGRAM_FAILURE;
        if (!all_none) m->quarantined = 1;
    }
}

static enum fwlab_nfc_api_result cancel(void *opaque,
    const struct fwlab_nfc_operation_token *key)
{
    struct fwlab_nfc_page_v2_model *m = opaque;
    if (!live(m) || !key) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!key_valid(m, key) || m->state == PAGE2_EMPTY || !key_equal(key, &m->request.operation))
        return FWLAB_NFC_API_STALE_TOKEN;
    if (m->state == PAGE2_RUNNING) return FWLAB_NFC_API_WRONG_STATE;
    if (m->state == PAGE2_QUEUED) m->cancel_requested = 1;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result step(void *opaque, uint32_t budget,
    struct fwlab_nfc_page_v2_step_result *out)
{
    struct fwlab_nfc_page_v2_model *m = opaque;
    if (!live(m) || !budget || !out) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (m->state == PAGE2_RUNNING) return FWLAB_NFC_API_WRONG_STATE;
    memset(out, 0, sizeof(*out));
    if (m->state == PAGE2_QUEUED) {
        m->state = PAGE2_RUNNING;
        if (m->cancel_requested || m->closed) {
            m->result.terminal = FWLAB_NFC_TERMINAL_CANCELLED;
            m->result.reason = m->closed ? FWLAB_NFC_REASON_RESET : FWLAB_NFC_REASON_CANCELLED;
        } else if (m->request.kind == FWLAB_NFC_PAGE_V2_READ_GROUP) read_group(m);
        else mutate(m);
        m->state = PAGE2_DONE;
        out->units_used = 1;
    }
    out->results_pending = m->state == PAGE2_DONE;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result take_result(void *opaque,
    const struct fwlab_nfc_operation_token *key, struct fwlab_nfc_page_v2_result *out,
    const struct fwlab_nfc_page_v2_output *destination)
{
    struct fwlab_nfc_page_v2_model *m = opaque;
    struct fwlab_nfc_page_v2_output d = {0};
    bool discard;
    if (!live(m) || !key || !out || !outside(m, out, sizeof(*out)))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!key_valid(m, key) || m->state == PAGE2_EMPTY || !key_equal(key, &m->request.operation))
        return FWLAB_NFC_API_STALE_TOKEN;
    if (m->state != PAGE2_DONE) return FWLAB_NFC_API_WRONG_STATE;
    if (destination) d = *destination;
    discard = !d.main && !d.main_bytes && !d.oob && !d.oob_bytes;
    if (!discard && (m->request.kind != FWLAB_NFC_PAGE_V2_READ_GROUP ||
        d.main_bytes != (size_t)m->request.page_count * FWLAB_NFC_PAGE_V2_MAIN_BYTES ||
        d.oob_bytes != (size_t)m->request.page_count * FWLAB_NFC_PAGE_V2_OOB_BYTES ||
        !outside(m, d.main, d.main_bytes) || !outside(m, d.oob, d.oob_bytes) ||
        overlap(d.main, d.main_bytes, d.oob, d.oob_bytes) ||
        overlap(out, sizeof(*out), d.main, d.main_bytes) ||
        overlap(out, sizeof(*out), d.oob, d.oob_bytes)))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!discard && m->result.read_valid) {
        memcpy(d.main, m->main, d.main_bytes);
        memcpy(d.oob, m->oob, d.oob_bytes);
        m->result.delivered_pages = m->request.page_count;
    }
    *out = m->result;
    m->state = PAGE2_EMPTY;
    m->cancel_requested = 0;
    memset(&m->request, 0, sizeof(m->request));
    memset(&m->result, 0, sizeof(m->result));
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result reset_begin(void *opaque, uint64_t instance, uint32_t epoch)
{
    struct fwlab_nfc_page_v2_model *m = opaque;
    if (!live(m)) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (instance != m->config.instance_nonce || epoch != m->config.controller_epoch)
        return FWLAB_NFC_API_STALE_TOKEN;
    if (m->state == PAGE2_RUNNING) return FWLAB_NFC_API_WRONG_STATE;
    m->closed = 1;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result quiescent(void *opaque, uint64_t instance, uint32_t epoch, bool *out)
{
    struct fwlab_nfc_page_v2_model *m = opaque;
    if (!live(m) || !out) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (instance != m->config.instance_nonce || epoch != m->config.controller_epoch)
        return FWLAB_NFC_API_STALE_TOKEN;
    *out = m->closed && m->state == PAGE2_EMPTY;
    return FWLAB_NFC_API_OK;
}

static const struct fwlab_nfc_page_v2_provider_ops provider_ops = {
    FWLAB_NFC_PAGE_V2_VERSION, sizeof(struct fwlab_nfc_page_v2_provider_ops), 0,
    try_submit, cancel, step, take_result, reset_begin, quiescent
};

size_t fwlab_nfc_page_v2_arena_size(void)
{ return sizeof(struct fwlab_nfc_page_v2_model); }
size_t fwlab_nfc_page_v2_arena_alignment(void)
{ return alignof(struct fwlab_nfc_page_v2_model); }

enum fwlab_nfc_api_result fwlab_nfc_page_v2_init(void *arena, size_t arena_bytes,
    const struct fwlab_nfc_page_v2_config *config, const struct fwlab_nand_batch_v2 *media,
    struct fwlab_nfc_page_v2_model **out)
{
    struct fwlab_nfc_page_v2_model *m = arena;
    struct fwlab_nfc_page_v2_config c;
    struct fwlab_nand_batch_v2 binding;
    if (!arena || arena_bytes < sizeof(*m) || (uintptr_t)arena % alignof(struct fwlab_nfc_page_v2_model) ||
        !out || !config || !media) return FWLAB_NFC_API_INVALID_CONTRACT;
    c = *config; binding = *media;
    if (c.version != FWLAB_NFC_PAGE_V2_VERSION || c.size != sizeof(c) ||
        c.profile != FWLAB_NFC_PAGE_V2_PROFILE_R0 || !geometry_valid(&c.geometry) ||
        !c.instance_nonce || !c.operation_uid_limit || !c.controller_epoch || !c.generation ||
        c.fault_flags || c.timing_flags || !zeros(c.reserved, sizeof(c.reserved)) ||
        zeros(c.media_uuid, sizeof(c.media_uuid)) ||
        binding.version != FWLAB_NAND_BATCH_V2_VERSION || binding.size != sizeof(binding) || binding.reserved ||
        !binding.ops || binding.ops->version != FWLAB_NAND_BATCH_V2_VERSION ||
        binding.ops->size != sizeof(*binding.ops) || binding.ops->reserved ||
        !binding.ops->read_pages || !binding.ops->program_pages || !binding.scalar.context ||
        !binding.scalar.ops || binding.scalar.ops->version != FWLAB_NFC_CONTRACT_VERSION ||
        binding.scalar.ops->size != sizeof(*binding.scalar.ops) || binding.scalar.ops->reserved ||
        !binding.scalar.ops->read_page || !binding.scalar.ops->program || !binding.scalar.ops->erase ||
        !binding.scalar.ops->mark_runtime_bad || !binding.scalar.ops->hash ||
        memcmp(&c.geometry, &binding.geometry, sizeof(c.geometry)) ||
        memcmp(c.media_uuid, binding.media_uuid, sizeof(c.media_uuid)))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    memset(m, 0, sizeof(*m));
    m->config = c;
    m->media = binding;
    m->magic = PAGE2_MAGIC;
    *out = m;
    return FWLAB_NFC_API_OK;
}

struct fwlab_nfc_page_v2_provider fwlab_nfc_page_v2_provider(struct fwlab_nfc_page_v2_model *m)
{
    struct fwlab_nfc_page_v2_provider p = {NULL, NULL};
    if (live(m)) { p.ops = &provider_ops; p.context = m; }
    return p;
}
