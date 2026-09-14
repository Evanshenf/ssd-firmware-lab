/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "fwlab/private/nfc_page_v2_lab.h"
#include "nfc_page_v2_internal.h"

#include <stdalign.h>
#include <string.h>

#define LAB_MAGIC UINT64_C(0x4c41425245414431)
#define SLOTS FWLAB_NFC_PAGE_V2_LAB_SLOTS
#define MAIN FWLAB_NFC_PAGE_V2_MAIN_BYTES
#define OOB FWLAB_NFC_PAGE_V2_OOB_BYTES
#define PAGES FWLAB_NFC_PAGE_V2_MAX_PAGES

enum lab_state { LAB_EMPTY, LAB_WAIT_COMMAND, LAB_COMMAND, LAB_ARRAY,
                 LAB_WAIT_DATA, LAB_DATA, LAB_DONE, LAB_LOAD, LAB_WAIT_INPUT,
                 LAB_INPUT, LAB_WAIT_CONFIRM, LAB_CONFIRM, LAB_PROGRAM_ARRAY,
                 LAB_ERASE_COMMAND, LAB_ERASE_ARRAY, LAB_WAIT_STATUS, LAB_STATUS };
struct lab_slot {
    struct fwlab_nfc_page_v2_request request;
    struct fwlab_nfc_page_v2_result result;
    uint64_t due, register_start, time_charge;
    uint32_t page;
    uint16_t base_generation, group_generation;
    uint8_t state, cancelled, mutation_committed, hard_failed, generation_known;
    _Alignas(64) uint8_t main[PAGES * MAIN];
    uint8_t oob[PAGES * OOB];
};
struct fwlab_nfc_page_v2_lab {
    uint64_t magic, last_uid, transfer_ns, page_ns, program_page_ns, erase_ns, status_ns;
    struct fwlab_nfc_page_v2_lab_config config;
    struct fwlab_nfc_page_v2_lab_mutation_config mutation;
    struct fwlab_nand_batch_v2 media;
    struct fwlab_nfc_page_v2_model *prep_model;
    struct fwlab_nfc_page_v2_provider prep;
    struct fwlab_nfc_page_v2_request prep_request;
    struct fwlab_nfc_page_v2_lab_stats stats;
    struct fwlab_nfc_page_v2_lab_trace trace[FWLAB_NFC_PAGE_V2_LAB_TRACE_CAPACITY];
    uint8_t channel_owner[FWLAB_NFC_PAGE_V2_LAB_CHANNELS];
    uint8_t lun_owner[FWLAB_NFC_PAGE_V2_LAB_LUNS];
    uint8_t busy, prep_active, prep_pending;
    /* Serialized validation scratch must never overwrite an accepted PROGRAM
     * snapshot, including when a later page is checked after an earlier effect. */
    _Alignas(64) uint8_t scratch_main[MAIN];
    uint8_t scratch_oob[OOB];
    struct lab_slot slot[SLOTS];
    _Alignas(64) uint8_t prep_arena[];
};
_Static_assert(offsetof(struct fwlab_nfc_page_v2_lab, prep_arena) ==
               sizeof(struct fwlab_nfc_page_v2_lab), "aligned trailing R0 arena");

static bool live(const struct fwlab_nfc_page_v2_lab *m)
{ return m && m->magic == LAB_MAGIC; }
static bool overlap(const void *a, size_t an, const void *b, size_t bn)
{
    return an && bn && (uintptr_t)a < (uintptr_t)b + bn &&
           (uintptr_t)b < (uintptr_t)a + an;
}
static bool outside(const struct fwlab_nfc_page_v2_lab *m, const void *p, size_t n)
{ return page2_span(p, n) && !overlap(m, fwlab_nfc_page_v2_lab_arena_size(), p, n); }
static bool add(uint64_t a, uint64_t b, uint64_t *out)
{ if (b > UINT64_MAX - a) return false; *out = a + b; return true; }
static void count(struct fwlab_nfc_page_v2_lab *m, uint64_t *value, uint64_t increment)
{
    if (!add(*value, increment, value)) {
        *value = UINT64_MAX;
        m->stats.counters_saturated = 1;
    }
}
static uint32_t lun_index(const struct fwlab_nfc_page_v2_lab *m, const struct lab_slot *s)
{ return s->request.first.channel * m->config.base.geometry.luns_per_channel + s->request.first.lun; }
static bool mutating(const struct lab_slot *s)
{ return s->request.kind != FWLAB_NFC_PAGE_V2_READ_GROUP; }
static bool interrupted(const struct fwlab_nfc_page_v2_lab *m, const struct lab_slot *s)
{
    return m->stats.quarantined || s->hard_failed ||
        ((s->cancelled || m->stats.closed) && (!mutating(s) || !s->mutation_committed));
}
static void trace(struct fwlab_nfc_page_v2_lab *m, const struct lab_slot *s, uint32_t event)
{
    struct fwlab_nfc_page_v2_lab_trace *t;
    if (m->stats.trace_count == FWLAB_NFC_PAGE_V2_LAB_TRACE_CAPACITY) {
        count(m, &m->stats.trace_dropped, 1);
        return;
    }
    t = &m->trace[m->stats.trace_count++];
    t->now_ns = m->stats.now_ns;
    t->operation_uid = s->request.operation.operation_uid;
    t->ppa = s->request.first;
    t->ppa.page = (uint16_t)(t->ppa.page + s->page);
    t->event = event;
}
static struct fwlab_nfc_submit_result disposition(uint32_t value, uint32_t reason)
{ return (struct fwlab_nfc_submit_result){value, reason}; }
static struct lab_slot *find(struct fwlab_nfc_page_v2_lab *m,
                             const struct fwlab_nfc_operation_token *key)
{
    for (unsigned i = 0; i < SLOTS; ++i)
        if (m->slot[i].state != LAB_EMPTY && page2_key_equal(&m->slot[i].request.operation, key))
            return &m->slot[i];
    return NULL;
}
static uint32_t pending(const struct fwlab_nfc_page_v2_lab *m)
{
    uint32_t n = 0;
    for (unsigned i = 0; i < SLOTS; ++i) n += m->slot[i].state == LAB_DONE;
    return n;
}
static struct fwlab_nfc_submit_result duplicate(const struct fwlab_nfc_page_v2_request *a,
                                               const struct fwlab_nfc_page_v2_request *b)
{
    bool equal = page2_canonical_equal(a, b);
    return disposition(equal ? FWLAB_NFC_ACCEPTED : FWLAB_NFC_REJECTED,
                       equal ? FWLAB_NFC_REASON_NONE : FWLAB_NFC_REASON_STALE);
}
static struct fwlab_nfc_submit_result submit(void *opaque,
                                           const struct fwlab_nfc_page_v2_request *r)
{
    struct fwlab_nfc_page_v2_lab *m = opaque;
    struct lab_slot *s;
    uint8_t reason;
    uint64_t charge, remaining, duration;
    if (!live(m) || m->busy) return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_INTERNAL);
    reason = page2_shape_reason(&m->config.base, r);
    if (reason) return disposition(FWLAB_NFC_REJECTED, reason);
    if (m->prep_active && page2_key_equal(&m->prep_request.operation, &r->operation))
        return duplicate(r, &m->prep_request);
    s = find(m, &r->operation);
    if (s) return duplicate(r, &s->request);
    if (m->stats.closed || m->stats.quarantined)
        return disposition(FWLAB_NFC_REJECTED,
            m->stats.closed ? FWLAB_NFC_REASON_RESET : FWLAB_NFC_REASON_INTERNAL);
    if (r->operation.operation_uid <= m->last_uid)
        return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_STALE);
    if (!outside(m, r, sizeof(*r)) || (r->kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP &&
        (!outside(m, r->main, r->main_bytes) || !outside(m, r->oob, r->oob_bytes))))
        return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_RANGE);
    if (m->stats.phase == FWLAB_NFC_PAGE_V2_LAB_PREP) {
        struct fwlab_nfc_submit_result result;
        m->busy = 1;
        result = m->prep.ops->try_submit(m->prep.context, r);
        m->busy = 0;
        if (result.disposition == FWLAB_NFC_ACCEPTED) {
            m->prep_request = *r;
            m->prep_request.main = NULL; m->prep_request.oob = NULL;
            m->prep_active = 1; m->prep_pending = 0;
            m->last_uid = r->operation.operation_uid;
        }
        return result;
    }
    if (r->kind != FWLAB_NFC_PAGE_V2_READ_GROUP && m->stats.phase != FWLAB_NFC_PAGE_V2_LAB_TIMED_RW)
        return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_UNSUPPORTED);
    s = NULL;
    for (unsigned i = 0; i < SLOTS; ++i)
        if (m->slot[i].state == LAB_EMPTY) { s = &m->slot[i]; break; }
    if (!s) return disposition(FWLAB_NFC_BACKPRESSURE, FWLAB_NFC_REASON_NONE);
    /* Reserve a conservative fully serialized time bound for every admitted
     * group. Thus later stage additions cannot exhaust time while owning a
     * register/bus. No backdated admission and no wraparound on long runs. */
    duration = r->kind == FWLAB_NFC_PAGE_V2_READ_GROUP ? m->page_ns :
        r->kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP ? m->program_page_ns : m->erase_ns;
    if (duration > UINT64_MAX / r->page_count)
        return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_RANGE);
    charge = duration * r->page_count;
    remaining = m->config.virtual_ns_limit - m->stats.now_ns;
    for (unsigned i = 0; i < SLOTS; ++i) {
        if (m->slot[i].time_charge > remaining)
            return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_RANGE);
        remaining -= m->slot[i].time_charge;
    }
    if (charge > remaining) return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_RANGE);
    memset(&s->result, 0, sizeof(s->result));
    s->request = *r;
    s->result.version = FWLAB_NFC_PAGE_V2_VERSION;
    s->result.size = sizeof(s->result);
    s->result.operation = r->operation; s->result.first = r->first;
    s->result.page_count = r->page_count; s->result.kind = r->kind;
    s->state = LAB_WAIT_COMMAND; s->page = 0; s->cancelled = 0;
    s->mutation_committed = s->hard_failed = s->generation_known = 0;
    s->time_charge = charge;
    if (r->kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP) {
        memcpy(s->main, r->main, r->main_bytes); memcpy(s->oob, r->oob, r->oob_bytes);
        s->request.main = NULL; s->request.oob = NULL;
    }
    if (r->kind != FWLAB_NFC_PAGE_V2_READ_GROUP)
        for (uint32_t i = 0; i < r->page_count; ++i)
            s->result.page[i].facts_valid = FWLAB_NFC_PAGE_V2_FACT_EFFECT;
    m->last_uid = r->operation.operation_uid;
    count(m, r->kind == FWLAB_NFC_PAGE_V2_READ_GROUP ? &m->stats.accepted_reads :
        r->kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP ? &m->stats.accepted_program_groups : &m->stats.accepted_erases, 1);
    trace(m, s, FWLAB_NFC_PAGE_V2_LAB_ADMIT);
    return disposition(FWLAB_NFC_ACCEPTED, FWLAB_NFC_REASON_NONE);
}

static void terminal(struct fwlab_nfc_page_v2_lab *m, struct lab_slot *s)
{
    if (s->cancelled || m->stats.closed) {
        s->result.terminal = FWLAB_NFC_TERMINAL_CANCELLED;
        s->result.reason = m->stats.closed ? FWLAB_NFC_REASON_RESET : FWLAB_NFC_REASON_CANCELLED;
    } else if (s->result.reason || m->stats.quarantined) {
        s->result.terminal = FWLAB_NFC_TERMINAL_FAILED;
        if (!s->result.reason) s->result.reason = FWLAB_NFC_REASON_INTERNAL;
    } else {
        s->result.terminal = FWLAB_NFC_TERMINAL_SUCCESS;
        s->result.read_valid = 1;
    }
    if (s->result.terminal != FWLAB_NFC_TERMINAL_SUCCESS) s->result.read_valid = 0;
    s->state = LAB_DONE; s->time_charge = 0;
    trace(m, s, FWLAB_NFC_PAGE_V2_LAB_TERMINAL);
}
static void mutation_terminal(struct fwlab_nfc_page_v2_lab *m, struct lab_slot *s)
{
    bool all_complete = true, all_none = true, unknown = false;
    for (uint32_t i = 0; i < s->request.page_count; ++i) {
        uint8_t effect = s->result.page[i].effect;
        all_complete = all_complete && effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE;
        all_none = all_none && effect == FWLAB_NFC_PAGE_V2_EFFECT_NONE;
        unknown = unknown || effect == FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN;
    }
    s->result.effect = unknown ? FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN :
        all_complete ? FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE :
        all_none ? FWLAB_NFC_PAGE_V2_EFFECT_NONE : FWLAB_NFC_PAGE_V2_EFFECT_NONCOMPLETE;
    if (all_complete && !s->hard_failed) {
        /* Physical completion is independent of the outer publication winner.
         * Even an unrelated slot's quarantine cannot undo completed effects. */
        s->result.terminal = FWLAB_NFC_TERMINAL_SUCCESS;
        s->result.reason = FWLAB_NFC_REASON_NONE;
    } else if (s->hard_failed || m->stats.quarantined || !all_none) {
        s->result.terminal = FWLAB_NFC_TERMINAL_FAILED;
        if (!s->result.reason) s->result.reason = FWLAB_NFC_REASON_INTERNAL;
    } else {
        s->result.terminal = FWLAB_NFC_TERMINAL_CANCELLED;
        s->result.reason = m->stats.closed ? FWLAB_NFC_REASON_RESET : FWLAB_NFC_REASON_CANCELLED;
    }
    unsigned lun = lun_index(m, s);
    uint8_t owner = (uint8_t)((s - m->slot) + 1);
    if (m->lun_owner[lun] == owner) {
        m->lun_owner[lun] = 0;
        count(m, &m->stats.mutation_reservation_ns[lun], m->stats.now_ns - s->register_start);
    }
    s->state = LAB_DONE; s->time_charge = 0;
    trace(m, s, FWLAB_NFC_PAGE_V2_LAB_TERMINAL);
}
static void mutation_failure(struct fwlab_nfc_page_v2_lab *m, struct lab_slot *s,
                             uint8_t reason, enum fwlab_nfc_api_result status, bool unknown)
{
    s->hard_failed = 1;
    s->result.reason = reason; s->result.backend_status = status;
    if (unknown) {
        struct fwlab_nfc_page_v2_page_result *p = &s->result.page[s->page];
        memset(p, 0, sizeof(*p));
        p->facts_valid = FWLAB_NFC_PAGE_V2_FACT_EFFECT;
        p->effect = FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN;
    }
    if (status != FWLAB_NFC_API_OK || unknown) m->stats.quarantined = 1;
}
static bool mutation_preflight(struct fwlab_nfc_page_v2_lab *m, struct lab_slot *s)
{
    struct fwlab_nand_block_info block = {0};
    struct fwlab_nand_page_info page = {0};
    struct fwlab_nfc_ppa ppa = s->request.first;
    enum fwlab_nfc_api_result status;
    uint8_t reason;
    ppa.page = (uint16_t)(ppa.page + s->page);
    if (m->stats.quarantined) return false;
    status = m->media.ops->read_pages(m->media.scalar.context, &ppa, 1,
        m->scratch_main, MAIN, m->scratch_oob, OOB, &page, 1, &block);
    count(m, &m->stats.preflight_reads, 1);
    if (status != FWLAB_NFC_API_OK || !page2_block_valid(&m->config.base.geometry, &block) ||
        !page2_cell_valid(&page, &block, ppa.page) ||
        (s->generation_known && block.erase_generation != s->group_generation)) {
        mutation_failure(m, s, FWLAB_NFC_REASON_INTERNAL,
            status == FWLAB_NFC_API_OK ? FWLAB_NFC_API_INVARIANT_FAILURE : status, false);
        return false; /* No mutation callback was invoked. */
    }
    reason = page2_mutation_reason(s->request.kind, ppa.page, &block, &page);
    if (reason) { mutation_failure(m, s, reason, FWLAB_NFC_API_OK, false); return false; }
    s->base_generation = s->group_generation = block.erase_generation;
    s->generation_known = 1;
    return true;
}
static void mutation_effect(struct fwlab_nfc_page_v2_lab *m, struct lab_slot *s)
{
    struct fwlab_nand_media_result result = {0};
    struct fwlab_nfc_ppa ppa = s->request.first;
    enum fwlab_nfc_api_result status;
    bool erase = s->request.kind == FWLAB_NFC_PAGE_V2_ERASE, complete;
    ppa.page = (uint16_t)(ppa.page + s->page);
    if (m->stats.quarantined) {
        mutation_failure(m, s, FWLAB_NFC_REASON_INTERNAL, FWLAB_NFC_API_OK, false);
        return; /* This slot's uninvoked effect remains known NONE. */
    }
    if (erase) {
        count(m, &m->stats.attempted_erases, 1);
        status = m->media.scalar.ops->erase(m->media.scalar.context, &ppa,
            m->config.base.geometry.pages_per_block, FWLAB_NFC_INTEGRITY_COMPLETE, &result);
    } else {
        count(m, &m->stats.attempted_program_pages, 1);
        status = m->media.ops->program_pages(m->media.scalar.context, &ppa, 1,
            s->main + (size_t)s->page * MAIN, MAIN, s->oob + (size_t)s->page * OOB, OOB, &result, 1);
    }
    if (status != FWLAB_NFC_API_OK ||
        !page2_effect_matches(&m->config.base.geometry, &result, s->base_generation, erase)) {
        mutation_failure(m, s, FWLAB_NFC_REASON_INTERNAL,
            status == FWLAB_NFC_API_OK ? FWLAB_NFC_API_INVARIANT_FAILURE : status, true);
        return;
    }
    complete = page2_effect_complete(&m->config.base.geometry, &result, erase);
    page2_copy_effect(&s->result.page[s->page], &result, complete);
    if (erase) {
        count(m, &m->stats.erased_pages, result.applied_pages);
        if (complete) count(m, &m->stats.successful_erases, 1);
    } else {
        count(m, &m->stats.program_main_bytes, result.applied_main_bytes);
        count(m, &m->stats.program_oob_bytes, result.applied_oob_bytes);
        if (complete) count(m, &m->stats.successful_program_pages, 1);
    }
    if (!complete) {
        mutation_failure(m, s, result.reason ? result.reason :
            erase ? FWLAB_NFC_REASON_ERASE_FAILURE : FWLAB_NFC_REASON_PROGRAM_FAILURE,
            FWLAB_NFC_API_OK, false);
        if (result.physical_outcome != FWLAB_NFC_PHYS_NO_EFFECT) m->stats.quarantined = 1;
    }
}
static enum fwlab_nfc_api_result cancel(void *opaque,
                                      const struct fwlab_nfc_operation_token *key)
{
    struct fwlab_nfc_page_v2_lab *m = opaque;
    struct lab_slot *s;
    if (!live(m) || !key) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (m->busy) return FWLAB_NFC_API_WRONG_STATE;
    if (!page2_key_valid(&m->config.base, key)) return FWLAB_NFC_API_STALE_TOKEN;
    if (m->stats.phase == FWLAB_NFC_PAGE_V2_LAB_PREP)
        return m->prep.ops->cancel(m->prep.context, key);
    s = find(m, key);
    if (!s) return FWLAB_NFC_API_STALE_TOKEN;
    /* Outcome already fixed, as in R0. A late upper cancellation must discard
     * this retained result/payload at its publication boundary. */
    if (s->state == LAB_DONE) return FWLAB_NFC_API_OK;
    s->cancelled = 1;
    s->result.read_valid = 0;
    return FWLAB_NFC_API_OK;
}

/* Releases/materialization precede new acquisition at an equal time. UID is
 * the admission sequence because new UIDs must be strictly increasing. */
static unsigned rank(uint8_t state)
{
    switch (state) {
    case LAB_COMMAND: case LAB_LOAD: case LAB_INPUT: case LAB_CONFIRM:
    case LAB_ERASE_COMMAND: return 0;
    case LAB_ARRAY: case LAB_PROGRAM_ARRAY: case LAB_ERASE_ARRAY: return 1;
    case LAB_DATA: case LAB_STATUS: return 2;
    case LAB_WAIT_DATA: case LAB_WAIT_STATUS: return 3;
    default: return 4;
    }
}
static struct lab_slot *next(struct fwlab_nfc_page_v2_lab *m, uint64_t *at)
{
    struct lab_slot *selected = NULL;
    for (unsigned i = 0; i < SLOTS; ++i) {
        struct lab_slot *s = &m->slot[i];
        uint64_t due;
        if (s->state == LAB_EMPTY || s->state == LAB_DONE) continue;
        due = m->stats.now_ns;
        if (s->state == LAB_WAIT_COMMAND) {
            uint8_t owner = (uint8_t)(i + 1u), lun_owner = m->lun_owner[lun_index(m, s)];
            if (!interrupted(m, s) &&
                (m->channel_owner[s->request.first.channel] || (lun_owner && lun_owner != owner)))
                continue;
        } else if (s->state == LAB_WAIT_DATA || s->state == LAB_WAIT_STATUS) {
            if (m->channel_owner[s->request.first.channel]) continue;
        } else if (s->state == LAB_WAIT_INPUT || s->state == LAB_WAIT_CONFIRM) {
            if (!interrupted(m, s) && m->channel_owner[s->request.first.channel]) continue;
        } else due = s->due;
        if (!selected || due < *at || (due == *at &&
            (rank(s->state) < rank(selected->state) ||
             (rank(s->state) == rank(selected->state) &&
              s->request.operation.operation_uid < selected->request.operation.operation_uid)))) {
            selected = s; *at = due;
        }
    }
    return selected;
}
static void materialize(struct fwlab_nfc_page_v2_lab *m, struct lab_slot *s)
{
    struct fwlab_nand_block_info block = {0};
    struct fwlab_nand_page_info page = {0};
    struct fwlab_nfc_ppa ppa = s->request.first;
    enum fwlab_nfc_api_result status;
    ppa.page = (uint16_t)(ppa.page + s->page);
    if (m->stats.phase == FWLAB_NFC_PAGE_V2_LAB_TIMED_RW && m->stats.quarantined) {
        s->result.reason = FWLAB_NFC_REASON_INTERNAL;
        return;
    }
    status = m->media.ops->read_pages(m->media.scalar.context, &ppa, 1,
        s->main + (size_t)s->page * MAIN, MAIN, s->oob + (size_t)s->page * OOB, OOB,
        &page, 1, &block);
    count(m, &m->stats.materialized_pages, 1);
    if (status != FWLAB_NFC_API_OK || !page2_block_valid(&m->config.base.geometry, &block) ||
        !page2_cell_valid(&page, &block, ppa.page)) {
        s->result.reason = FWLAB_NFC_REASON_INTERNAL;
        s->result.backend_status = status == FWLAB_NFC_API_OK ? FWLAB_NFC_API_INVARIANT_FAILURE : status;
        m->stats.quarantined = 1;
        return;
    }
    page2_read_fact(&s->result.page[s->page], &page, &block);
    if (s->result.page[s->page].reason) s->result.reason = s->result.page[s->page].reason;
}
static void advance(struct fwlab_nfc_page_v2_lab *m, struct lab_slot *s)
{
    unsigned channel = s->request.first.channel, lun = lun_index(m, s);
    uint8_t owner = (uint8_t)((s - m->slot) + 1);
    switch (s->state) {
    case LAB_WAIT_COMMAND:
        if (mutating(s)) {
            if (interrupted(m, s)) { mutation_terminal(m, s); break; }
            if (!m->lun_owner[lun]) {
                m->lun_owner[lun] = owner; s->register_start = m->stats.now_ns;
            }
            if (!mutation_preflight(m, s)) { mutation_terminal(m, s); break; }
            m->channel_owner[channel] = owner;
            if (s->request.kind == FWLAB_NFC_PAGE_V2_ERASE) {
                s->mutation_committed = 1;
                count(m, &m->stats.issued_erases, 1);
                s->state = LAB_ERASE_COMMAND;
                s->due = m->stats.now_ns + m->mutation.erase_command_ns;
                trace(m, s, FWLAB_NFC_PAGE_V2_LAB_ERASE_BEGIN);
            } else {
                s->state = LAB_LOAD;
                s->due = m->stats.now_ns + m->config.command_ns;
                trace(m, s, FWLAB_NFC_PAGE_V2_LAB_LOAD_BEGIN);
            }
            break;
        }
        if (interrupted(m, s)) { terminal(m, s); break; }
        m->channel_owner[channel] = owner; m->lun_owner[lun] = owner;
        s->register_start = m->stats.now_ns;
        s->due = m->stats.now_ns + m->config.command_ns;
        s->state = LAB_COMMAND;
        trace(m, s, FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN);
        break;
    case LAB_COMMAND:
        m->channel_owner[channel] = 0;
        s->due = m->stats.now_ns + m->config.array_read_ns;
        s->state = LAB_ARRAY;
        count(m, &m->stats.channel_busy_ns[channel], m->config.command_ns);
        trace(m, s, FWLAB_NFC_PAGE_V2_LAB_COMMAND_END);
        break;
    case LAB_ARRAY:
        materialize(m, s);
        count(m, &m->stats.array_busy_ns[lun], m->config.array_read_ns);
        s->state = LAB_WAIT_DATA;
        trace(m, s, FWLAB_NFC_PAGE_V2_LAB_ARRAY_READY);
        break;
    case LAB_WAIT_DATA:
        m->channel_owner[channel] = owner;
        s->due = m->stats.now_ns + m->transfer_ns;
        s->state = LAB_DATA;
        trace(m, s, FWLAB_NFC_PAGE_V2_LAB_DATA_BEGIN);
        break;
    case LAB_DATA:
        m->channel_owner[channel] = 0; m->lun_owner[lun] = 0;
        count(m, &m->stats.channel_busy_ns[channel], m->transfer_ns);
        count(m, &m->stats.data_out_main_bytes, MAIN);
        count(m, &m->stats.data_out_oob_bytes, OOB);
        count(m, &m->stats.register_busy_ns[lun], m->stats.now_ns - s->register_start);
        trace(m, s, FWLAB_NFC_PAGE_V2_LAB_DATA_END);
        if (s->cancelled || m->stats.closed || m->stats.quarantined || s->result.reason ||
            s->page + 1u == s->request.page_count) terminal(m, s);
        else { ++s->page; s->state = LAB_WAIT_COMMAND; }
        break;
    case LAB_LOAD:
        m->channel_owner[channel] = 0;
        count(m, &m->stats.channel_busy_ns[channel], m->config.command_ns);
        trace(m, s, FWLAB_NFC_PAGE_V2_LAB_LOAD_END);
        if (interrupted(m, s)) mutation_terminal(m, s);
        else s->state = LAB_WAIT_INPUT;
        break;
    case LAB_WAIT_INPUT:
        if (interrupted(m, s)) { mutation_terminal(m, s); break; }
        m->channel_owner[channel] = owner; s->state = LAB_INPUT;
        s->due = m->stats.now_ns + m->transfer_ns;
        trace(m, s, FWLAB_NFC_PAGE_V2_LAB_INPUT_BEGIN);
        break;
    case LAB_INPUT:
        m->channel_owner[channel] = 0;
        count(m, &m->stats.channel_busy_ns[channel], m->transfer_ns);
        count(m, &m->stats.data_in_main_bytes, MAIN);
        count(m, &m->stats.data_in_oob_bytes, OOB);
        trace(m, s, FWLAB_NFC_PAGE_V2_LAB_INPUT_END);
        if (interrupted(m, s)) mutation_terminal(m, s);
        else s->state = LAB_WAIT_CONFIRM;
        break;
    case LAB_WAIT_CONFIRM:
        if (interrupted(m, s)) { mutation_terminal(m, s); break; }
        m->channel_owner[channel] = owner;
        if (!s->mutation_committed) count(m, &m->stats.confirmed_program_groups, 1);
        s->mutation_committed = 1; s->state = LAB_CONFIRM;
        s->due = m->stats.now_ns + m->mutation.program_confirm_ns;
        trace(m, s, FWLAB_NFC_PAGE_V2_LAB_CONFIRM_BEGIN);
        break;
    case LAB_CONFIRM:
        m->channel_owner[channel] = 0;
        count(m, &m->stats.channel_busy_ns[channel], m->mutation.program_confirm_ns);
        s->state = LAB_PROGRAM_ARRAY;
        s->due = m->stats.now_ns + m->mutation.array_program_ns;
        trace(m, s, FWLAB_NFC_PAGE_V2_LAB_CONFIRM_END);
        break;
    case LAB_ERASE_COMMAND:
        m->channel_owner[channel] = 0;
        count(m, &m->stats.channel_busy_ns[channel], m->mutation.erase_command_ns);
        s->state = LAB_ERASE_ARRAY;
        s->due = m->stats.now_ns + m->mutation.array_erase_ns;
        trace(m, s, FWLAB_NFC_PAGE_V2_LAB_ERASE_COMMAND_END);
        break;
    case LAB_PROGRAM_ARRAY: case LAB_ERASE_ARRAY: {
        bool erase = s->state == LAB_ERASE_ARRAY;
        mutation_effect(m, s);
        count(m, erase ? &m->stats.erase_array_busy_ns[lun] : &m->stats.program_array_busy_ns[lun],
              erase ? m->mutation.array_erase_ns : m->mutation.array_program_ns);
        s->state = LAB_WAIT_STATUS;
        trace(m, s, erase ? FWLAB_NFC_PAGE_V2_LAB_ERASE_EFFECT : FWLAB_NFC_PAGE_V2_LAB_PROGRAM_EFFECT);
        break;
    }
    case LAB_WAIT_STATUS:
        /* The retained LUN belongs to this group and never blocks its STATUS.
         * A failed/quarantined operation still releases its bus reservation. */
        m->channel_owner[channel] = owner; s->state = LAB_STATUS;
        s->due = m->stats.now_ns + m->status_ns;
        trace(m, s, FWLAB_NFC_PAGE_V2_LAB_STATUS_BEGIN);
        break;
    case LAB_STATUS:
        m->channel_owner[channel] = 0;
        count(m, &m->stats.channel_busy_ns[channel], m->status_ns);
        count(m, &m->stats.status_response_bytes, m->mutation.status_response_bytes);
        trace(m, s, FWLAB_NFC_PAGE_V2_LAB_STATUS_END);
        if (s->hard_failed || m->stats.quarantined || s->page + 1u == s->request.page_count)
            mutation_terminal(m, s);
        else { ++s->page; s->state = LAB_WAIT_COMMAND; }
        break;
    default: break;
    }
}
static enum fwlab_nfc_api_result step(void *opaque, uint32_t budget,
                                    struct fwlab_nfc_page_v2_step_result *out)
{
    struct fwlab_nfc_page_v2_lab *m = opaque;
    if (!live(m) || !budget || !outside(m, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (m->busy) return FWLAB_NFC_API_WRONG_STATE;
    m->busy = 1;
    if (m->stats.phase == FWLAB_NFC_PAGE_V2_LAB_PREP) {
        enum fwlab_nfc_api_result r = m->prep.ops->step(m->prep.context, budget, out);
        if (r == FWLAB_NFC_API_OK) m->prep_pending = (uint8_t)(out->results_pending != 0);
        m->busy = 0;
        return r;
    }
    memset(out, 0, sizeof(*out));
    while (out->units_used < budget) {
        uint64_t at = 0;
        struct lab_slot *s = next(m, &at);
        if (!s) break;
        m->stats.now_ns = at;
        advance(m, s);
        ++out->units_used;
        count(m, &m->stats.transitions, 1);
    }
    out->results_pending = pending(m);
    m->busy = 0;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result take(void *opaque, const struct fwlab_nfc_operation_token *key,
                                    struct fwlab_nfc_page_v2_result *out,
                                    const struct fwlab_nfc_page_v2_output *destination)
{
    struct fwlab_nfc_page_v2_lab *m = opaque;
    struct lab_slot *s;
    struct fwlab_nfc_page_v2_output d = {0};
    bool discard;
    if (!live(m) || !key || !outside(m, out, sizeof(*out)) ||
        (destination && !outside(m, destination, sizeof(*destination)))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (m->busy) return FWLAB_NFC_API_WRONG_STATE;
    if (!page2_key_valid(&m->config.base, key)) return FWLAB_NFC_API_STALE_TOKEN;
    if (destination) d = *destination;
    discard = !d.main && !d.main_bytes && !d.oob && !d.oob_bytes;
    if (!discard && (!outside(m, d.main, d.main_bytes) || !outside(m, d.oob, d.oob_bytes) ||
        overlap(d.main, d.main_bytes, d.oob, d.oob_bytes) ||
        overlap(out, sizeof(*out), d.main, d.main_bytes) || overlap(out, sizeof(*out), d.oob, d.oob_bytes)))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    if (m->stats.phase == FWLAB_NFC_PAGE_V2_LAB_PREP) {
        enum fwlab_nfc_api_result r = m->prep.ops->take_result(m->prep.context, key, out, destination);
        if (r == FWLAB_NFC_API_OK) {
            bool idle = false;
            m->prep_active = 0; m->prep_pending = 0;
            memset(&m->prep_request, 0, sizeof(m->prep_request));
            if (!m->stats.closed &&
                fwlab_nfc_page_v2_live_idle(m->prep_model, &idle) == FWLAB_NFC_API_OK && !idle)
                m->stats.quarantined = 1;
        }
        return r;
    }
    s = find(m, key);
    if (!s) return FWLAB_NFC_API_STALE_TOKEN;
    if (s->state != LAB_DONE) return FWLAB_NFC_API_WRONG_STATE;
    if (!discard && (s->request.kind != FWLAB_NFC_PAGE_V2_READ_GROUP ||
                     d.main_bytes != (size_t)s->request.page_count * MAIN ||
                     d.oob_bytes != (size_t)s->request.page_count * OOB)) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!discard && s->result.read_valid) {
        memcpy(d.main, s->main, d.main_bytes); memcpy(d.oob, s->oob, d.oob_bytes);
        s->result.delivered_pages = s->request.page_count;
    }
    *out = s->result;
    memset(&s->request, 0, sizeof(s->request));
    memset(&s->result, 0, sizeof(s->result));
    s->state = LAB_EMPTY; s->cancelled = 0;
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result reset(void *opaque, uint64_t instance, uint32_t epoch)
{
    struct fwlab_nfc_page_v2_lab *m = opaque;
    if (!live(m)) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (m->busy) return FWLAB_NFC_API_WRONG_STATE;
    if (instance != m->config.base.instance_nonce || epoch != m->config.base.controller_epoch)
        return FWLAB_NFC_API_STALE_TOKEN;
    if (m->stats.phase == FWLAB_NFC_PAGE_V2_LAB_PREP) {
        enum fwlab_nfc_api_result r = m->prep.ops->reset_begin(m->prep.context, instance, epoch);
        if (r != FWLAB_NFC_API_OK) return r;
    }
    m->stats.closed = 1;
    for (unsigned i = 0; i < SLOTS; ++i)
        if (m->slot[i].state != LAB_EMPTY) (void)cancel(m, &m->slot[i].request.operation);
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result quiet(void *opaque, uint64_t instance, uint32_t epoch, bool *out)
{
    struct fwlab_nfc_page_v2_lab *m = opaque;
    if (!live(m) || !outside(m, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (m->busy) return FWLAB_NFC_API_WRONG_STATE;
    if (instance != m->config.base.instance_nonce || epoch != m->config.base.controller_epoch)
        return FWLAB_NFC_API_STALE_TOKEN;
    if (m->stats.phase == FWLAB_NFC_PAGE_V2_LAB_PREP)
        return m->prep.ops->quiescent(m->prep.context, instance, epoch, out);
    *out = m->stats.closed;
    for (unsigned i = 0; i < SLOTS; ++i) if (m->slot[i].state != LAB_EMPTY) *out = false;
    for (unsigned i = 0; i < FWLAB_NFC_PAGE_V2_LAB_CHANNELS; ++i) if (m->channel_owner[i]) *out = false;
    for (unsigned i = 0; i < FWLAB_NFC_PAGE_V2_LAB_LUNS; ++i) if (m->lun_owner[i]) *out = false;
    return FWLAB_NFC_API_OK;
}
static const struct fwlab_nfc_page_v2_provider_ops operations = {
    FWLAB_NFC_PAGE_V2_VERSION, sizeof(struct fwlab_nfc_page_v2_provider_ops), 0,
    submit, cancel, step, take, reset, quiet
};

size_t fwlab_nfc_page_v2_lab_arena_alignment(void)
{ return alignof(struct fwlab_nfc_page_v2_lab); }
size_t fwlab_nfc_page_v2_lab_arena_size(void)
{ return sizeof(struct fwlab_nfc_page_v2_lab) + fwlab_nfc_page_v2_arena_size(); }

static bool wiring(const struct fwlab_nfc_page_v2_lab_config *c)
{
    uint32_t luns;
    if (!c->base.geometry.channels || c->base.geometry.channels > FWLAB_NFC_PAGE_V2_LAB_CHANNELS ||
        !c->base.geometry.luns_per_channel || c->base.geometry.luns_per_channel > 4u) return false;
    luns = (uint32_t)c->base.geometry.channels * c->base.geometry.luns_per_channel;
    if (!luns || luns > FWLAB_NFC_PAGE_V2_LAB_LUNS) return false;
    for (unsigned i = 0; i < luns; ++i) {
        const struct fwlab_nfc_page_v2_lab_lun *a = &c->lun[i];
        if (a->reserved) return false;
        for (unsigned j = 0; j < i; ++j) {
            const struct fwlab_nfc_page_v2_lab_lun *b = &c->lun[j];
            if (a->die == b->die && a->package != b->package) return false;
            if (i / c->base.geometry.luns_per_channel != j / c->base.geometry.luns_per_channel) continue;
            if (a->target == b->target) {
                if (a->ce != b->ce || a->package != b->package || a->target_lun == b->target_lun) return false;
            } else if (a->ce == b->ce) return false;
        }
    }
    return page2_zero(c->lun + luns, (FWLAB_NFC_PAGE_V2_LAB_LUNS - luns) * sizeof(c->lun[0]));
}
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_init(void *arena, size_t bytes,
    const struct fwlab_nfc_page_v2_lab_config *config, const struct fwlab_nand_batch_v2 *media,
    struct fwlab_nfc_page_v2_lab **out)
{
    struct fwlab_nfc_page_v2_lab *m = arena;
    struct fwlab_nfc_page_v2_lab_config c;
    struct fwlab_nand_batch_v2 b;
    uint64_t transfer, total;
    const uint64_t byte_ns = (uint64_t)(MAIN + OOB) * UINT64_C(1000000000);
    if (!arena || bytes < fwlab_nfc_page_v2_lab_arena_size() ||
        (uintptr_t)arena % alignof(struct fwlab_nfc_page_v2_lab) || !config || !media || !out ||
        !page2_span(arena, fwlab_nfc_page_v2_lab_arena_size()) || !outside(m, out, sizeof(*out)))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    c = *config; b = *media;
    if (c.version != FWLAB_NFC_PAGE_V2_LAB_VERSION || c.size != sizeof(c) || c.reserved ||
        !c.command_ns || !c.array_read_ns || !c.channel_bytes_per_second || !c.virtual_ns_limit || !wiring(&c))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    transfer = byte_ns / c.channel_bytes_per_second + (byte_ns % c.channel_bytes_per_second != 0);
    if (!add(c.command_ns, c.array_read_ns, &total) || !add(total, transfer, &total) ||
        total > c.virtual_ns_limit) return FWLAB_NFC_API_INVALID_CONTRACT;
    memset(m, 0, fwlab_nfc_page_v2_lab_arena_size());
    if (fwlab_nfc_page_v2_init(m->prep_arena, fwlab_nfc_page_v2_arena_size(), &c.base, &b,
                             &m->prep_model) != FWLAB_NFC_API_OK) return FWLAB_NFC_API_INVALID_CONTRACT;
    m->prep = fwlab_nfc_page_v2_provider(m->prep_model);
    m->config = c; m->media = b; m->transfer_ns = transfer; m->page_ns = total;
    m->magic = LAB_MAGIC;
    *out = m;
    return FWLAB_NFC_API_OK;
}
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_mutation_init(void *arena, size_t bytes,
    const struct fwlab_nfc_page_v2_lab_mutation_config *config,
    const struct fwlab_nand_batch_v2 *media, struct fwlab_nfc_page_v2_lab **out)
{
    struct fwlab_nfc_page_v2_lab_mutation_config c;
    struct fwlab_nfc_page_v2_lab *m;
    uint64_t status, program, erase, transfer, response;
    const uint64_t page_byte_ns = (uint64_t)(MAIN + OOB) * UINT64_C(1000000000);
    if (!config || !out) return FWLAB_NFC_API_INVALID_CONTRACT;
    c = *config;
    if (c.version != FWLAB_NFC_PAGE_V2_LAB_MUTATION_VERSION || c.size != sizeof(c) ||
        c.reserved || c.reserved1 || !c.program_confirm_ns || !c.array_program_ns ||
        !c.erase_command_ns || !c.array_erase_ns || !c.status_command_ns ||
        !c.status_response_bytes || !c.read.channel_bytes_per_second)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    /* uint32 response bytes times 1e9 fits uint64; quotient/remainder rounding
     * avoids the overflow-prone numerator + rate - 1 idiom. */
    response = (uint64_t)c.status_response_bytes * UINT64_C(1000000000);
    response = response / c.read.channel_bytes_per_second +
               (response % c.read.channel_bytes_per_second != 0);
    transfer = page_byte_ns / c.read.channel_bytes_per_second +
               (page_byte_ns % c.read.channel_bytes_per_second != 0);
    if (!add(c.status_command_ns, response, &status) ||
        !add(c.read.command_ns, transfer, &program) ||
        !add(program, c.program_confirm_ns, &program) ||
        !add(program, c.array_program_ns, &program) || !add(program, status, &program) ||
        !add(c.erase_command_ns, c.array_erase_ns, &erase) || !add(erase, status, &erase) ||
        program > c.read.virtual_ns_limit || erase > c.read.virtual_ns_limit)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    /* Validate all shared geometry/identity/media rules in the original LAB
     * constructor. Publish only after its entire common initialization. */
    if (fwlab_nfc_page_v2_lab_init(arena, bytes, &c.read, media, &m) != FWLAB_NFC_API_OK)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!outside(m, out, sizeof(*out))) { m->magic = 0; return FWLAB_NFC_API_INVALID_CONTRACT; }
    m->mutation = c; m->program_page_ns = program; m->erase_ns = erase; m->status_ns = status;
    m->stats.phase = FWLAB_NFC_PAGE_V2_LAB_TIMED_RW;
    *out = m;
    return FWLAB_NFC_API_OK;
}
struct fwlab_nfc_page_v2_provider fwlab_nfc_page_v2_lab_provider(struct fwlab_nfc_page_v2_lab *m)
{ return (struct fwlab_nfc_page_v2_provider){live(m) ? &operations : NULL, live(m) ? m : NULL}; }
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_live_idle(const struct fwlab_nfc_page_v2_lab *m, bool *out)
{
    if (!live(m) || !outside(m, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (m->busy) return FWLAB_NFC_API_WRONG_STATE;
    *out = false;
    if (m->stats.closed || m->stats.quarantined) return FWLAB_NFC_API_OK;
    if (m->stats.phase == FWLAB_NFC_PAGE_V2_LAB_PREP)
        return fwlab_nfc_page_v2_live_idle(m->prep_model, out);
    for (unsigned i = 0; i < SLOTS; ++i) if (m->slot[i].state != LAB_EMPTY) return FWLAB_NFC_API_OK;
    *out = true;
    return FWLAB_NFC_API_OK;
}
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_begin_timed_read(struct fwlab_nfc_page_v2_lab *m)
{
    bool idle = false;
    enum fwlab_nfc_api_result r;
    if (!live(m)) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (m->stats.phase != FWLAB_NFC_PAGE_V2_LAB_PREP) return FWLAB_NFC_API_WRONG_STATE;
    r = fwlab_nfc_page_v2_lab_live_idle(m, &idle);
    if (r != FWLAB_NFC_API_OK) return r;
    if (!idle || m->prep_active) return FWLAB_NFC_API_WRONG_STATE;
    m->stats.phase = FWLAB_NFC_PAGE_V2_LAB_TIMED_READ;
    return FWLAB_NFC_API_OK;
}
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_admission_floor(
    struct fwlab_nfc_page_v2_lab *m, uint64_t time)
{
    bool idle = false;
    enum fwlab_nfc_api_result r;
    if (!live(m)) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (m->stats.phase == FWLAB_NFC_PAGE_V2_LAB_PREP ||
        time < m->stats.now_ns || time > m->config.virtual_ns_limit)
        return FWLAB_NFC_API_WRONG_STATE;
    r = fwlab_nfc_page_v2_lab_live_idle(m, &idle);
    if (r != FWLAB_NFC_API_OK) return r;
    if (!idle) return FWLAB_NFC_API_WRONG_STATE;
    m->stats.now_ns = time;
    return FWLAB_NFC_API_OK;
}
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_snapshot(const struct fwlab_nfc_page_v2_lab *m,
                                                       struct fwlab_nfc_page_v2_lab_stats *out)
{
    if (!live(m) || !outside(m, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (m->busy) return FWLAB_NFC_API_WRONG_STATE;
    *out = m->stats;
    if (m->stats.phase == FWLAB_NFC_PAGE_V2_LAB_PREP) {
        out->active_slots = m->prep_active; out->results_pending = m->prep_pending;
    } else {
        out->results_pending = pending(m);
        for (unsigned i = 0; i < SLOTS; ++i) out->active_slots += m->slot[i].state != LAB_EMPTY;
        for (unsigned i = 0; i < FWLAB_NFC_PAGE_V2_LAB_CHANNELS; ++i) out->busy_channels += m->channel_owner[i] != 0;
        for (unsigned i = 0; i < FWLAB_NFC_PAGE_V2_LAB_LUNS; ++i) out->held_luns += m->lun_owner[i] != 0;
    }
    return FWLAB_NFC_API_OK;
}
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_trace_at(const struct fwlab_nfc_page_v2_lab *m,
    uint32_t index, struct fwlab_nfc_page_v2_lab_trace *out)
{
    if (!live(m) || !outside(m, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (m->busy) return FWLAB_NFC_API_WRONG_STATE;
    if (index >= m->stats.trace_count) return FWLAB_NFC_API_NOT_FOUND;
    *out = m->trace[index];
    return FWLAB_NFC_API_OK;
}
