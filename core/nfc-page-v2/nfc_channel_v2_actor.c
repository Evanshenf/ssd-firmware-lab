/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "nfc_channel_v2_internal.h"
#include "nfc_page_v2_internal.h"
#include <string.h>

#define SLOTS FWLAB_NFC_CHANNEL_JOB_SLOTS
#define MAIN FWLAB_NFC_PAGE_V2_MAIN_BYTES
#define OOB FWLAB_NFC_PAGE_V2_OOB_BYTES

enum fwlab_nfc_api_result fwlab_nfc_channel_actor_init(
    struct fwlab_nfc_channel_actor *a, uint32_t channel, void *arena, size_t bytes,
    const struct fwlab_nfc_page_v2_lab_mutation_config *timing,
    const struct fwlab_nand_batch_v2 *media, enum fwlab_nfc_page_v2_lab_read_policy policy,
    struct fwlab_nfc_page_v2_lab_stats *stats)
{
    memset(a, 0, sizeof(*a));
    enum fwlab_nfc_api_result r = fwlab_nfc_page_v2_lab_mutation_init_policy(arena, bytes, timing, media, policy, &a->model);
    if (r != FWLAB_NFC_API_OK) return r;
    a->provider = fwlab_nfc_page_v2_lab_provider(a->model);
    a->channel = channel; a->instance_nonce = timing->read.base.instance_nonce;
    a->epoch = timing->read.base.controller_epoch;
    return fwlab_nfc_page_v2_lab_snapshot(a->model, stats);
}

static enum fwlab_nfc_api_result finish(struct fwlab_nfc_channel_actor *a,
    struct fwlab_nfc_channel_job *j, uint32_t status, bool *advanced, bool *complete)
{
    struct fwlab_nfc_channel_reply *r = &j->reply;
    r->status = status;
    r->accepted_mask = a->accepted; r->completed_mask = a->completed;
    r->lower_owned_mask = a->lower_owned; r->reports_held_mask = a->reports;
    r->closed = a->closed; r->quiet = a->quiet;
    if (fwlab_nfc_page_v2_lab_snapshot(a->model, &r->stats) != FWLAB_NFC_API_OK)
        r->status = FWLAB_NFC_API_INVARIANT_FAILURE;
    else {
        r->stats.quarantined |= a->quarantined;
        r->terminal_ns = r->stats.now_ns;
        r->trace_start = a->trace_sent;
        if (r->stats.trace_count < a->trace_sent ||
            r->stats.trace_count > FWLAB_NFC_PAGE_V2_LAB_TRACE_CAPACITY)
            r->status = FWLAB_NFC_API_INVARIANT_FAILURE;
        else {
            r->trace_count = r->stats.trace_count - a->trace_sent;
            for (uint32_t n = 0; n < r->trace_count; ++n)
                if (fwlab_nfc_page_v2_lab_trace_at(a->model, a->trace_sent + n, &r->trace[n]) != FWLAB_NFC_API_OK)
                    r->status = FWLAB_NFC_API_INVARIANT_FAILURE;
            a->trace_sent = r->stats.trace_count;
        }
    }
    a->current = NULL;
    *advanced = true; *complete = true;
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result broken(struct fwlab_nfc_channel_actor *a,
    struct fwlab_nfc_channel_job *j, bool *advanced, bool *complete)
{
    a->fault = FWLAB_NFC_API_INVARIANT_FAILURE; a->quarantined = 1;
    return finish(a, j, a->fault, advanced, complete);
}
static void rejected(struct fwlab_nfc_channel_actor *a, unsigned i,
                     struct fwlab_nfc_submit_result submitted)
{
    struct fwlab_nfc_channel_job_entry *e = &a->entry[i];
    struct fwlab_nfc_page_v2_result *r = &e->frame->result;
    memset(r, 0, sizeof(*r));
    r->version = FWLAB_NFC_PAGE_V2_VERSION; r->size = sizeof(*r);
    r->operation = e->request.operation; r->first = e->request.first;
    r->page_count = e->request.page_count; r->kind = e->request.kind;
    r->terminal = FWLAB_NFC_TERMINAL_FAILED;
    r->reason = submitted.reason && submitted.reason <= FWLAB_NFC_REASON_INTERNAL ?
        (uint8_t)submitted.reason : FWLAB_NFC_REASON_INTERNAL;
    r->backend_status = submitted.disposition == FWLAB_NFC_BACKPRESSURE ?
        FWLAB_NFC_API_NO_CAPACITY : FWLAB_NFC_API_INVALID_CONTRACT;
    if (r->kind != FWLAB_NFC_PAGE_V2_READ_GROUP)
        for (uint32_t p = 0; p < r->page_count; ++p) r->page[p].facts_valid = FWLAB_NFC_PAGE_V2_FACT_EFFECT;
    a->completed |= (uint8_t)(1u << i); a->reports |= (uint8_t)(1u << i);
    a->quarantined = 1;
}
static bool identity(const struct fwlab_nfc_channel_job_entry *e)
{
    struct fwlab_nfc_ppa local = e->request.first;
    const struct fwlab_nfc_page_v2_result *r = &e->frame->result;
    local.channel = 0;
    return r->version == FWLAB_NFC_PAGE_V2_VERSION && r->size == sizeof(*r) &&
        !r->reserved0 && !r->reserved1 && page2_key_equal(&r->operation, &e->request.operation) &&
        !memcmp(&r->first, &local, sizeof(local)) && r->kind == e->request.kind &&
        r->page_count == e->request.page_count && r->terminal <= FWLAB_NFC_TERMINAL_FAILED &&
        r->read_valid <= 1 && r->delivered_pages ==
            (r->kind == FWLAB_NFC_PAGE_V2_READ_GROUP && r->read_valid ? r->page_count : 0);
}
static bool begin(struct fwlab_nfc_channel_actor *a, struct fwlab_nfc_channel_job *j)
{
    memset(&j->reply, 0, sizeof(j->reply));
    j->reply.job_sequence = j->job_sequence; j->reply.batch_uid = j->batch_uid;
    j->reply.channel = j->channel; j->reply.command = j->command;
    if (!j->job_sequence || a->last_sequence == UINT64_MAX ||
        j->job_sequence != a->last_sequence + 1u || j->channel != a->channel ||
        (j->slot_mask & (uint8_t)~15u)) return false;
    a->last_sequence = j->job_sequence; a->current = j; a->stage = a->cursor = 0;
    if (j->command == FWLAB_CHANNEL_PREP) {
        if (a->batch_active || a->granted || a->lower_owned || a->reports || a->closed || !j->batch_uid)
            return false;
        uint64_t prior = 0;
        for (unsigned i = 0; i < SLOTS; ++i) if (j->slot_mask & (1u << i)) {
            const struct fwlab_nfc_channel_job_entry *e = &j->entry[i];
            if (!e->frame || e->request.first.channel != a->channel ||
                e->request.operation.operation_uid <= prior) return false;
            prior = e->request.operation.operation_uid;
        }
        memcpy(a->entry, j->entry, sizeof(a->entry));
        a->granted = j->slot_mask; a->accepted = a->completed = 0;
        a->batch_uid = j->batch_uid; a->batch_active = 1;
        return true;
    }
    if (j->command == FWLAB_CHANNEL_RUN)
        return a->batch_active && j->batch_uid == a->batch_uid && j->slot_mask == a->granted;
    if (j->command == FWLAB_CHANNEL_RETIRE)
        return a->batch_active && j->batch_uid == a->batch_uid && j->slot_mask &&
            !(j->slot_mask & (uint8_t)~a->reports) && !(j->slot_mask & a->lower_owned);
    if (j->command == FWLAB_CHANNEL_CLOSE)
        return !a->batch_active && !a->granted && !a->lower_owned && !a->reports && !j->slot_mask;
    return false;
}

enum fwlab_nfc_api_result fwlab_nfc_channel_v2_actor_job_step(
    struct fwlab_nfc_channel_job *j, bool *advanced, bool *complete)
{
    if (!j || !j->actor || !advanced || !complete) return FWLAB_NFC_API_INVALID_CONTRACT;
    struct fwlab_nfc_channel_actor *a = j->actor;
    *advanced = *complete = false;
    if (!a->current) {
        if (!begin(a, j)) return broken(a, j, advanced, complete);
        *advanced = true;
        return FWLAB_NFC_API_OK;
    }
    if (a->current != j) return FWLAB_NFC_API_INVARIANT_FAILURE;
    if (j->command == FWLAB_CHANNEL_PREP) {
        if (!a->stage) {
            if (fwlab_nfc_page_v2_lab_admission_floor(a->model, j->admission_floor_ns) != FWLAB_NFC_API_OK)
                return broken(a, j, advanced, complete);
            a->stage = 1; *advanced = true;
            return FWLAB_NFC_API_OK;
        }
        while (a->cursor < SLOTS && !(a->granted & (1u << a->cursor))) ++a->cursor;
        if (a->cursor == SLOTS) return finish(a, j, FWLAB_NFC_API_OK, advanced, complete);
        unsigned i = a->cursor++;
        struct fwlab_nfc_channel_job_entry *e = &a->entry[i];
        struct fwlab_nfc_page_v2_request local = e->request;
        local.first.channel = 0;
        if (local.kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP) { local.main = e->frame->main; local.oob = e->frame->oob; }
        struct fwlab_nfc_submit_result r = a->provider.ops->try_submit(a->provider.context, &local);
        if (r.disposition == FWLAB_NFC_ACCEPTED) {
            a->accepted |= (uint8_t)(1u << i); a->lower_owned |= (uint8_t)(1u << i);
        } else if (r.disposition == FWLAB_NFC_REJECTED || r.disposition == FWLAB_NFC_BACKPRESSURE)
            rejected(a, i, r);
        else return broken(a, j, advanced, complete);
        *advanced = true;
        return FWLAB_NFC_API_OK;
    }
    if (j->command == FWLAB_CHANNEL_RUN) {
        for (unsigned i = 0; i < SLOTS; ++i) if (a->lower_owned & (1u << i)) {
            struct fwlab_nfc_channel_job_entry *e = &a->entry[i];
            struct fwlab_nfc_page_v2_output output = {e->frame->main, (size_t)e->request.page_count * MAIN,
                                                     e->frame->oob, (size_t)e->request.page_count * OOB};
            enum fwlab_nfc_api_result r = a->provider.ops->take_result(a->provider.context,
                &e->request.operation, &e->frame->result,
                e->request.kind == FWLAB_NFC_PAGE_V2_READ_GROUP ? &output : NULL);
            if (r == FWLAB_NFC_API_OK) {
                a->lower_owned &= (uint8_t)~(1u << i);
                a->completed |= (uint8_t)(1u << i); a->reports |= (uint8_t)(1u << i);
                if (!identity(e)) return broken(a, j, advanced, complete);
                e->frame->result.first = e->request.first;
                e->frame->result.delivered_pages = 0;
                *advanced = true;
                return FWLAB_NFC_API_OK;
            }
            if (r != FWLAB_NFC_API_WRONG_STATE) return broken(a, j, advanced, complete);
        }
        if (!a->lower_owned) {
            if (!a->granted) a->batch_active = 0;
            return finish(a, j, a->fault ? a->fault : FWLAB_NFC_API_OK, advanced, complete);
        }
        struct fwlab_nfc_page_v2_step_result result = {0};
        if (a->provider.ops->step(a->provider.context, 1, &result) != FWLAB_NFC_API_OK || result.units_used > 1)
            return broken(a, j, advanced, complete);
        /* This explicit local model always has a next event for an owned
         * nonterminal operation. Missing progress preserves lower ownership. */
        if (!result.units_used) return broken(a, j, advanced, complete);
        *advanced = true;
        return FWLAB_NFC_API_OK;
    }
    if (j->command == FWLAB_CHANNEL_RETIRE) {
        while (a->cursor < SLOTS && !(j->slot_mask & (1u << a->cursor))) ++a->cursor;
        if (a->cursor == SLOTS) {
            if (!a->granted) a->batch_active = 0;
            return finish(a, j, FWLAB_NFC_API_OK, advanced, complete);
        }
        unsigned i = a->cursor++;
        a->granted &= (uint8_t)~(1u << i); a->reports &= (uint8_t)~(1u << i);
        a->accepted &= (uint8_t)~(1u << i); a->completed &= (uint8_t)~(1u << i);
        memset(&a->entry[i], 0, sizeof(a->entry[i]));
        *advanced = true;
        return FWLAB_NFC_API_OK;
    }
    if (j->command == FWLAB_CHANNEL_CLOSE) {
        if (!a->closed) {
            if (a->provider.ops->reset_begin(a->provider.context, a->instance_nonce, a->epoch) != FWLAB_NFC_API_OK)
                return broken(a, j, advanced, complete);
            a->closed = 1; *advanced = true;
            return FWLAB_NFC_API_OK;
        }
        bool quiet = false;
        if (a->provider.ops->quiescent(a->provider.context, a->instance_nonce, a->epoch, &quiet) != FWLAB_NFC_API_OK)
            return broken(a, j, advanced, complete);
        if (quiet) { a->quiet = 1; return finish(a, j, FWLAB_NFC_API_OK, advanced, complete); }
        struct fwlab_nfc_page_v2_step_result result = {0};
        if (a->provider.ops->step(a->provider.context, 1, &result) != FWLAB_NFC_API_OK || result.units_used > 1)
            return broken(a, j, advanced, complete);
        *advanced = result.units_used != 0;
        return FWLAB_NFC_API_OK;
    }
    return broken(a, j, advanced, complete);
}
