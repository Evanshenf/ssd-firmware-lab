/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "fwlab/private/nfc_channel_v2.h"
#include "nfc_page_v2_internal.h"
#include "nfc_channel_v2_internal.h"

#include <stdalign.h>
#include <string.h>

#define HUB_MAGIC UINT64_C(0x43484e5741564534)
#define CREDITS FWLAB_NFC_CHANNEL_V2_CREDITS
#define CHANNELS FWLAB_NAND_CHANNEL_V2_MAX
#define MAIN FWLAB_NFC_PAGE_V2_MAIN_BYTES
#define OOB FWLAB_NFC_PAGE_V2_OOB_BYTES
#define PAGES FWLAB_NFC_PAGE_V2_MAX_PAGES

enum hub_slot_state { HUB_FREE, HUB_QUEUED, HUB_CHILD, HUB_REPORT, HUB_READY, HUB_ACK, HUB_RETIRING };
struct hub_slot {
    struct fwlab_nfc_page_v2_request request;
    struct fwlab_nfc_page_v2_result result;
    uint64_t charge;
    uint8_t state;
};
struct hub_actor_view {
    struct fwlab_nfc_page_v2_lab_stats stats;
    uint64_t sequence;
    uint8_t batch_mask, accepted, lower_owned, reports, quiet;
};
struct fwlab_nfc_channel_v2 {
    uint64_t magic, last_uid, duration[4];
    struct fwlab_nfc_page_v2_lab_mutation_config timing;
    struct fwlab_nand_channel_v2 assembly;
    struct fwlab_nfc_channel_v2_stats stats;
    struct fwlab_nfc_channel_actor actor[CHANNELS];
    struct hub_actor_view view[CHANNELS];
    struct fwlab_nfc_channel_executor executor;
    struct fwlab_nfc_channel_job job[CHANNELS];
    struct fwlab_nfc_channel_job *cooperative_job[CHANNELS];
    struct fwlab_nfc_page_v2_lab_trace trace[CHANNELS][FWLAB_NFC_PAGE_V2_LAB_TRACE_CAPACITY];
    uint32_t trace_count[CHANNELS];
    uint64_t batch_uid;
    uint32_t command;
    uint8_t required, posted, replied, lost, fault_pending;
    uint8_t cooperative, shutdown_wait, executor_done;
    uint32_t cursor;
    uint8_t busy;
    struct hub_slot slot[CREDITS];
    struct fwlab_nfc_channel_frame frame[CREDITS];
    _Alignas(64) uint8_t actors[];
};
_Static_assert(offsetof(struct fwlab_nfc_channel_v2, actors) ==
               sizeof(struct fwlab_nfc_channel_v2), "aligned child arenas");
static bool live(const struct fwlab_nfc_channel_v2 *h)
{ return h && h->magic == HUB_MAGIC; }
static bool overlap(const void *a, size_t an, const void *b, size_t bn)
{
    return an && bn && (uintptr_t)a < (uintptr_t)b + bn &&
           (uintptr_t)b < (uintptr_t)a + an;
}
static bool outside(const struct fwlab_nfc_channel_v2 *h, const void *p, size_t n)
{ return page2_span(p, n) && !overlap(h, fwlab_nfc_channel_v2_arena_size(), p, n); }
static bool add(uint64_t a, uint64_t b, uint64_t *out)
{ if (b > UINT64_MAX - a) return false; *out = a + b; return true; }
static void count(struct fwlab_nfc_channel_v2 *h, uint64_t *value, uint64_t n)
{
    if (!add(*value, n, value)) { *value = UINT64_MAX; h->stats.counters_saturated = 1; }
}
static struct fwlab_nfc_submit_result disposition(uint32_t d, uint32_t r)
{ return (struct fwlab_nfc_submit_result){d, r}; }
static unsigned occupied(const struct fwlab_nfc_channel_v2 *h)
{
    unsigned count = 0;
    for (unsigned i = 0; i < CREDITS; ++i) count += h->slot[i].state != HUB_FREE;
    return count;
}
static unsigned ready(const struct fwlab_nfc_channel_v2 *h)
{
    unsigned count = 0;
    for (unsigned i = 0; i < CREDITS; ++i) count += h->slot[i].state == HUB_READY;
    return count;
}
static struct hub_slot *find(struct fwlab_nfc_channel_v2 *h,
                             const struct fwlab_nfc_operation_token *key)
{
    for (unsigned i = 0; i < CREDITS; ++i)
        if (h->slot[i].state != HUB_FREE && page2_key_equal(&h->slot[i].request.operation, key))
            return &h->slot[i];
    return NULL;
}
static enum fwlab_nfc_api_result poison(struct fwlab_nfc_channel_v2 *h)
{
    /* A missing/nonconsuming child report cannot be converted into drain. */
    h->stats.poisoned = h->stats.quarantined = 1;
    return FWLAB_NFC_API_INVARIANT_FAILURE;
}
static struct fwlab_nfc_submit_result submit(void *opaque,
                                           const struct fwlab_nfc_page_v2_request *r)
{
    struct fwlab_nfc_channel_v2 *h = opaque;
    struct hub_slot *s;
    uint8_t reason;
    uint64_t remaining, charge;
    if (!live(h) || h->busy) return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_INTERNAL);
    reason = page2_shape_reason(&h->timing.read.base, r);
    if (reason) return disposition(FWLAB_NFC_REJECTED, reason);
    s = find(h, &r->operation);
    if (s) {
        bool equal = s->state != HUB_ACK && s->state != HUB_RETIRING && page2_canonical_equal(r, &s->request);
        return disposition(equal ? FWLAB_NFC_ACCEPTED : FWLAB_NFC_REJECTED,
                           equal ? FWLAB_NFC_REASON_NONE : FWLAB_NFC_REASON_STALE);
    }
    if (h->stats.closed || h->stats.quarantined)
        return disposition(FWLAB_NFC_REJECTED, h->stats.closed ? FWLAB_NFC_REASON_RESET : FWLAB_NFC_REASON_INTERNAL);
    if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_BUILD && !occupied(h))
        for (unsigned c = 0; c < h->assembly.geometry.channels; ++c)
            if (h->view[c].sequence > UINT64_MAX - 7u)
                return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_RANGE);
    if (r->operation.operation_uid <= h->last_uid)
        return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_STALE);
    if (h->stats.phase != FWLAB_NFC_CHANNEL_V2_BUILD || occupied(h) == CREDITS)
        return disposition(FWLAB_NFC_BACKPRESSURE, FWLAB_NFC_REASON_NONE);
    if (!outside(h, r, sizeof(*r)) || (r->kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP &&
        (!outside(h, r->main, r->main_bytes) || !outside(h, r->oob, r->oob_bytes))))
        return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_RANGE);
    if (h->duration[r->kind] > UINT64_MAX / r->page_count)
        return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_RANGE);
    charge = h->duration[r->kind] * r->page_count;
    remaining = h->timing.read.virtual_ns_limit - h->stats.now_ns;
    for (unsigned i = 0; i < CREDITS; ++i) {
        if (h->slot[i].state == HUB_FREE || h->slot[i].request.first.channel != r->first.channel) continue;
        if (h->slot[i].charge > remaining) return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_RANGE);
        remaining -= h->slot[i].charge;
    }
    if (charge > remaining) return disposition(FWLAB_NFC_REJECTED, FWLAB_NFC_REASON_RANGE);
    s = NULL;
    for (unsigned i = 0; i < CREDITS; ++i)
        if (h->slot[i].state == HUB_FREE) { s = &h->slot[i]; break; }
    if (!s) return disposition(FWLAB_NFC_BACKPRESSURE, FWLAB_NFC_REASON_NONE);
    s->request = *r;
    memset(&s->result, 0, sizeof(s->result));
    if (r->kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP) {
        struct fwlab_nfc_channel_frame *frame = &h->frame[s - h->slot];
        memcpy(frame->main, r->main, r->main_bytes); memcpy(frame->oob, r->oob, r->oob_bytes);
        count(h, &h->stats.snapshot_main_bytes, r->main_bytes);
        count(h, &h->stats.snapshot_oob_bytes, r->oob_bytes);
    }
    s->request.main = NULL; s->request.oob = NULL;
    s->charge = charge; s->state = HUB_QUEUED;
    h->last_uid = r->operation.operation_uid;
    count(h, &h->stats.accepted_requests, 1);
    return disposition(FWLAB_NFC_ACCEPTED, FWLAB_NFC_REASON_NONE);
}
static enum fwlab_nfc_api_result cancel(void *opaque, const struct fwlab_nfc_operation_token *key)
{
    struct fwlab_nfc_channel_v2 *h = opaque;
    struct hub_slot *s;
    if (!live(h) || !key) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (h->busy) return FWLAB_NFC_API_WRONG_STATE;
    if (!page2_key_valid(&h->timing.read.base, key)) return FWLAB_NFC_API_STALE_TOKEN;
    s = find(h, key);
    if (!s || s->state == HUB_ACK || s->state == HUB_RETIRING) return FWLAB_NFC_API_STALE_TOKEN;
    /* WAVE4 drain-only policy: no child cancel, even before child admission.
     * The upper owner decides whether to publish or discard the result. */
    return FWLAB_NFC_API_OK;
}

static unsigned bits(uint8_t mask)
{
    unsigned n = 0;
    for (; mask; mask &= (uint8_t)(mask - 1u)) ++n;
    return n;
}
static enum fwlab_nfc_api_result cooperative_submit(void *opaque, struct fwlab_nfc_channel_job *j)
{
    struct fwlab_nfc_channel_v2 *h = opaque;
    if (!j || j->channel >= h->assembly.geometry.channels) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (h->cooperative_job[j->channel]) return FWLAB_NFC_API_NO_CAPACITY;
    h->cooperative_job[j->channel] = j;
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result cooperative_poll(void *opaque, uint32_t channel,
    struct fwlab_nfc_channel_job **out, bool *advanced)
{
    struct fwlab_nfc_channel_v2 *h = opaque;
    bool complete = false;
    *out = NULL; *advanced = false;
    if (channel >= h->assembly.geometry.channels) return FWLAB_NFC_API_INVALID_CONTRACT;
    struct fwlab_nfc_channel_job *j = h->cooperative_job[channel];
    if (!j) return FWLAB_NFC_API_OK;
    enum fwlab_nfc_api_result r = fwlab_nfc_channel_v2_actor_job_step(j, advanced, &complete);
    if (r == FWLAB_NFC_API_OK && complete) { h->cooperative_job[channel] = NULL; *out = j; }
    return r;
}
static enum fwlab_nfc_api_result cooperative_shutdown(void *opaque, bool *advanced, bool *complete)
{
    struct fwlab_nfc_channel_v2 *h = opaque;
    *advanced = false; *complete = false;
    for (unsigned c = 0; c < h->assembly.geometry.channels; ++c)
        if (h->cooperative_job[c]) return FWLAB_NFC_API_WRONG_STATE;
    *complete = true;
    return FWLAB_NFC_API_OK;
}
static const struct fwlab_nfc_channel_executor_ops cooperative_ops = {
    cooperative_submit, cooperative_poll, cooperative_shutdown
};
static void fault(struct fwlab_nfc_channel_v2 *h, unsigned c, bool lost)
{
    h->fault_pending = h->stats.quarantined = 1;
    if (lost) h->lost |= (uint8_t)(1u << c);
}
static bool setup_jobs(struct fwlab_nfc_channel_v2 *h, uint32_t command, uint8_t mask)
{
    h->command = command; h->required = mask; h->posted = h->replied = 0; h->cursor = 0;
    for (unsigned c = 0; c < h->assembly.geometry.channels; ++c) {
        if (!(mask & (1u << c))) continue;
        struct fwlab_nfc_channel_job *j = &h->job[c];
        if (h->view[c].sequence == UINT64_MAX) return false;
        memset(j, 0, sizeof(*j));
        j->actor = &h->actor[c]; j->channel = c; j->command = command;
        j->job_sequence = ++h->view[c].sequence;
        j->batch_uid = h->batch_uid; j->admission_floor_ns = h->stats.now_ns;
        if (command == FWLAB_CHANNEL_PREP || command == FWLAB_CHANNEL_RUN)
            j->slot_mask = h->view[c].batch_mask;
        if (command == FWLAB_CHANNEL_PREP) {
            for (unsigned i = 0; i < CREDITS; ++i) if (j->slot_mask & (1u << i)) {
                j->entry[i].request = h->slot[i].request;
                j->entry[i].frame = &h->frame[i];
            }
        } else if (command == FWLAB_CHANNEL_RETIRE) {
            for (unsigned i = 0; i < CREDITS; ++i)
                if (h->slot[i].state == HUB_ACK && h->slot[i].request.first.channel == c) {
                    j->slot_mask |= (uint8_t)(1u << i); h->slot[i].state = HUB_RETIRING;
                }
        }
    }
    return true;
}
static bool import_reply(struct fwlab_nfc_channel_v2 *h, unsigned c)
{
    struct fwlab_nfc_channel_job *j = &h->job[c];
    const struct fwlab_nfc_channel_reply *r = &j->reply;
    struct hub_actor_view *v = &h->view[c];
    uint8_t prior_accepted = v->accepted;
    if (r->job_sequence != v->sequence || r->batch_uid != h->batch_uid ||
        r->channel != c || r->command != h->command ||
        (r->accepted_mask | r->completed_mask | r->lower_owned_mask | r->reports_held_mask) & (uint8_t)~v->batch_mask ||
        r->closed > 1 || r->quiet > 1 || r->reserved[0] || r->reserved[1] ||
        r->trace_start != h->trace_count[c] ||
        r->trace_count > FWLAB_NFC_PAGE_V2_LAB_TRACE_CAPACITY - h->trace_count[c] ||
        r->stats.trace_count != r->trace_start + r->trace_count)
        return false;
    memcpy(h->trace[c] + r->trace_start, r->trace, r->trace_count * sizeof(r->trace[0]));
    h->trace_count[c] += r->trace_count;
    v->stats = r->stats; v->accepted = r->accepted_mask;
    v->lower_owned = r->lower_owned_mask; v->reports = r->reports_held_mask; v->quiet = r->quiet;
    h->stats.actor_owned[c] = bits(v->lower_owned);
    if (r->stats.quarantined) h->stats.quarantined = 1;
    if (r->status != FWLAB_NFC_API_OK) { fault(h, c, false); return true; }
    if (h->command == FWLAB_CHANNEL_PREP)
        return r->accepted_mask == r->lower_owned_mask &&
            (uint8_t)(r->lower_owned_mask | r->completed_mask) == v->batch_mask &&
            !(r->lower_owned_mask & r->completed_mask) && r->reports_held_mask == r->completed_mask;
    if (h->command == FWLAB_CHANNEL_RUN) {
        if (r->accepted_mask != prior_accepted || r->lower_owned_mask ||
            r->completed_mask != v->batch_mask || r->reports_held_mask != v->batch_mask ||
            r->stats.active_slots || r->stats.held_luns || r->stats.busy_channels ||
            r->terminal_ns != r->stats.now_ns || r->terminal_ns < h->stats.now_ns) return false;
        for (unsigned i = 0; i < CREDITS; ++i) if (v->batch_mask & (1u << i)) {
            struct hub_slot *s = &h->slot[i];
            const struct fwlab_nfc_page_v2_result *result = &h->frame[i].result;
            if (result->version != FWLAB_NFC_PAGE_V2_VERSION || result->size != sizeof(*result) ||
                result->reserved0 || result->reserved1 ||
                !page2_key_equal(&result->operation, &s->request.operation) ||
                memcmp(&result->first, &s->request.first, sizeof(result->first)) ||
                result->kind != s->request.kind || result->page_count != s->request.page_count ||
                result->terminal > FWLAB_NFC_TERMINAL_FAILED || result->read_valid > 1 || result->delivered_pages)
                return false;
            s->result = *result; s->state = HUB_REPORT;
        }
    } else if (h->command == FWLAB_CHANNEL_RETIRE) {
        if (r->lower_owned_mask || r->accepted_mask != (uint8_t)(prior_accepted & (uint8_t)~j->slot_mask) ||
            r->completed_mask != r->reports_held_mask ||
            r->reports_held_mask != (uint8_t)(v->batch_mask & (uint8_t)~j->slot_mask))
            return false;
        for (unsigned i = 0; i < CREDITS; ++i) if (j->slot_mask & (1u << i)) {
            struct hub_slot *s = &h->slot[i];
            if (s->state != HUB_RETIRING || s->request.first.channel != c) return false;
            memset(s, 0, sizeof(*s)); count(h, &h->stats.retired_acks, 1);
        }
        v->batch_mask &= (uint8_t)~j->slot_mask;
    } else if (h->command == FWLAB_CHANNEL_CLOSE) {
        if (!r->closed || !r->quiet || r->accepted_mask || r->completed_mask ||
            r->lower_owned_mask || r->reports_held_mask ||
            r->stats.active_slots || r->stats.held_luns || r->stats.busy_channels) return false;
        h->stats.channel[c] = r->stats;
    }
    return true;
}
static bool jobs_complete(const struct fwlab_nfc_channel_v2 *h)
{ return (uint8_t)((h->replied | h->lost) & h->required) == h->required; }
static enum fwlab_nfc_api_result jobs_step(struct fwlab_nfc_channel_v2 *h, bool *advanced)
{
    /* Dispatch the whole phase before polling. In particular, a blocked real
     * overlap witness cannot strand an unsent channel by putting us to sleep. */
    for (unsigned c = 0; c < h->assembly.geometry.channels; ++c) {
        uint8_t bit = (uint8_t)(1u << c);
        if (!(h->required & bit) || (h->posted & bit) || (h->lost & bit)) continue;
        enum fwlab_nfc_api_result r = h->executor.ops->submit(h->executor.context, &h->job[c]);
        if (r == FWLAB_NFC_API_NO_CAPACITY) return FWLAB_NFC_API_OK;
        if (r != FWLAB_NFC_API_OK) fault(h, c, true);
        else h->posted |= bit;
        *advanced = true;
        return FWLAB_NFC_API_OK;
    }
    for (unsigned n = 0; n < h->assembly.geometry.channels; ++n) {
        unsigned c = h->cursor++ % h->assembly.geometry.channels;
        uint8_t bit = (uint8_t)(1u << c);
        if (!(h->required & bit) || !(h->posted & bit) || ((h->replied | h->lost) & bit)) continue;
        struct fwlab_nfc_channel_job *j = NULL;
        bool progress = false;
        enum fwlab_nfc_api_result r = h->executor.ops->poll(h->executor.context, c, &j, &progress);
        if (r != FWLAB_NFC_API_OK) { fault(h, c, true); *advanced = true; return FWLAB_NFC_API_OK; }
        if (j) {
            if (j != &h->job[c]) fault(h, c, true);
            else { h->replied |= bit; if (!import_reply(h, c)) fault(h, c, false); }
            *advanced = true;
            return FWLAB_NFC_API_OK;
        }
        if (progress) { *advanced = true; return FWLAB_NFC_API_OK; }
    }
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result step_one(struct fwlab_nfc_channel_v2 *h, bool *advanced)
{
    uint8_t all = (uint8_t)((1u << h->assembly.geometry.channels) - 1u);
    *advanced = false;
    if (h->stats.poisoned) return FWLAB_NFC_API_INVARIANT_FAILURE;
    if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_BUILD) {
        if (occupied(h)) {
            memset(h->stats.batch_requests, 0, sizeof(h->stats.batch_requests));
            h->batch_uid = h->slot[0].request.operation.operation_uid;
            for (unsigned c = 0; c < h->assembly.geometry.channels; ++c) h->view[c].batch_mask = 0;
            for (unsigned i = 0; i < CREDITS; ++i) if (h->slot[i].state == HUB_QUEUED) {
                unsigned c = h->slot[i].request.first.channel;
                ++h->stats.batch_requests[c]; h->view[c].batch_mask |= (uint8_t)(1u << i);
            }
            h->stats.phase = FWLAB_NFC_CHANNEL_V2_FLOOR;
            count(h, &h->stats.sealed_batches, 1); *advanced = true;
        } else if (h->stats.closed) {
            if (!setup_jobs(h, FWLAB_CHANNEL_CLOSE, all)) return poison(h);
            h->stats.phase = FWLAB_NFC_CHANNEL_V2_CLOSING; *advanced = true;
        }
        return FWLAB_NFC_API_OK;
    }
    if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_FLOOR) {
        if (!setup_jobs(h, FWLAB_CHANNEL_PREP, all)) return poison(h);
        h->stats.phase = FWLAB_NFC_CHANNEL_V2_ADMIT; *advanced = true;
        return FWLAB_NFC_API_OK;
    }
    if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_ADMIT || h->stats.phase == FWLAB_NFC_CHANNEL_V2_RUN ||
        h->stats.phase == FWLAB_NFC_CHANNEL_V2_RETIRING || h->stats.phase == FWLAB_NFC_CHANNEL_V2_CLOSING) {
        if (!jobs_complete(h)) return jobs_step(h, advanced);
        if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_ADMIT) {
            /* Even an error in one PREP cannot suppress accepted healthy siblings. */
            if (!setup_jobs(h, FWLAB_CHANNEL_RUN, (uint8_t)(all & (uint8_t)~h->lost))) return poison(h);
            h->stats.phase = FWLAB_NFC_CHANNEL_V2_RUN;
        } else if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_RUN) {
            if (h->fault_pending) return poison(h);
            for (unsigned c = 0; c < h->assembly.geometry.channels; ++c) {
                h->stats.channel[c] = h->view[c].stats;
                if (h->stats.now_ns < h->view[c].stats.now_ns) h->stats.now_ns = h->view[c].stats.now_ns;
            }
            for (unsigned i = 0; i < CREDITS; ++i)
                if (h->slot[i].state == HUB_REPORT) h->slot[i].state = HUB_READY;
            h->stats.phase = FWLAB_NFC_CHANNEL_V2_JOINED;
            count(h, &h->stats.joined_batches, 1);
        } else if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_RETIRING) {
            if (h->fault_pending) return poison(h);
            h->stats.phase = occupied(h) ? FWLAB_NFC_CHANNEL_V2_JOINED : FWLAB_NFC_CHANNEL_V2_BUILD;
        } else {
            if (h->fault_pending) return poison(h);
            h->stats.phase = FWLAB_NFC_CHANNEL_V2_SHUTDOWN;
        }
        *advanced = true;
        return FWLAB_NFC_API_OK;
    }
    if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_JOINED) {
        uint8_t mask = 0;
        for (unsigned i = 0; i < CREDITS; ++i)
            if (h->slot[i].state == HUB_ACK) mask |= (uint8_t)(1u << h->slot[i].request.first.channel);
        if (mask) {
            if (!setup_jobs(h, FWLAB_CHANNEL_RETIRE, mask)) return poison(h);
            h->stats.phase = FWLAB_NFC_CHANNEL_V2_RETIRING; *advanced = true;
        }
        return FWLAB_NFC_API_OK;
    }
    if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_SHUTDOWN) {
        bool complete = false, progress = false;
        enum fwlab_nfc_api_result r = h->executor.ops->shutdown(h->executor.context, &progress, &complete);
        if (r != FWLAB_NFC_API_OK) return poison(h);
        h->shutdown_wait = (uint8_t)(!progress && !complete);
        if (complete) { h->executor_done = 1; h->stats.phase = FWLAB_NFC_CHANNEL_V2_CLOSED; }
        *advanced = progress || complete;
    }
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result step(void *opaque, uint32_t budget,
                                    struct fwlab_nfc_page_v2_step_result *out)
{
    struct fwlab_nfc_channel_v2 *h = opaque;
    enum fwlab_nfc_api_result result = FWLAB_NFC_API_OK;
    if (!live(h) || !budget || !outside(h, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (h->busy) return FWLAB_NFC_API_WRONG_STATE;
    memset(out, 0, sizeof(*out)); h->busy = 1;
    while (out->units_used < budget) {
        bool advanced = false;
        result = step_one(h, &advanced);
        if (advanced) ++out->units_used;
        if (result != FWLAB_NFC_API_OK || !advanced) break;
    }
    out->results_pending = ready(h); h->busy = 0;
    return result;
}
static enum fwlab_nfc_api_result take(void *opaque, const struct fwlab_nfc_operation_token *key,
                                    struct fwlab_nfc_page_v2_result *out,
                                    const struct fwlab_nfc_page_v2_output *destination)
{
    struct fwlab_nfc_channel_v2 *h = opaque;
    struct fwlab_nfc_page_v2_output d = {0};
    struct hub_slot *s;
    if (!live(h) || !key || !outside(h, out, sizeof(*out)) ||
        (destination && !outside(h, destination, sizeof(*destination)))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (h->busy) return FWLAB_NFC_API_WRONG_STATE;
    if (!page2_key_valid(&h->timing.read.base, key)) return FWLAB_NFC_API_STALE_TOKEN;
    s = find(h, key);
    if (!s || s->state == HUB_ACK || s->state == HUB_RETIRING) return FWLAB_NFC_API_STALE_TOKEN;
    if (h->stats.poisoned) return FWLAB_NFC_API_INVARIANT_FAILURE;
    if (s->state != HUB_READY) return FWLAB_NFC_API_WRONG_STATE;
    if (destination) d = *destination;
    bool discard = !d.main && !d.main_bytes && !d.oob && !d.oob_bytes;
    if (!discard && (s->request.kind != FWLAB_NFC_PAGE_V2_READ_GROUP ||
        d.main_bytes != (size_t)s->request.page_count * MAIN || d.oob_bytes != (size_t)s->request.page_count * OOB ||
        !outside(h, d.main, d.main_bytes) || !outside(h, d.oob, d.oob_bytes) ||
        overlap(d.main, d.main_bytes, d.oob, d.oob_bytes) || overlap(out, sizeof(*out), d.main, d.main_bytes) ||
        overlap(out, sizeof(*out), d.oob, d.oob_bytes))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!discard && s->result.read_valid) {
        const struct fwlab_nfc_channel_frame *frame = &h->frame[s - h->slot];
        memcpy(d.main, frame->main, d.main_bytes); memcpy(d.oob, frame->oob, d.oob_bytes);
        s->result.delivered_pages = s->request.page_count;
    }
    *out = s->result; s->state = HUB_ACK;
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result reset(void *opaque, uint64_t nonce, uint32_t epoch)
{
    struct fwlab_nfc_channel_v2 *h = opaque;
    if (!live(h)) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (h->busy) return FWLAB_NFC_API_WRONG_STATE;
    if (nonce != h->timing.read.base.instance_nonce || epoch != h->timing.read.base.controller_epoch)
        return FWLAB_NFC_API_STALE_TOKEN;
    h->stats.closed = 1;
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result quiet(void *opaque, uint64_t nonce, uint32_t epoch, bool *out)
{
    struct fwlab_nfc_channel_v2 *h = opaque;
    if (!live(h) || !outside(h, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (h->busy) return FWLAB_NFC_API_WRONG_STATE;
    if (nonce != h->timing.read.base.instance_nonce || epoch != h->timing.read.base.controller_epoch)
        return FWLAB_NFC_API_STALE_TOKEN;
    if (h->stats.poisoned) return FWLAB_NFC_API_INVARIANT_FAILURE;
    *out = h->stats.closed && h->stats.phase == FWLAB_NFC_CHANNEL_V2_CLOSED && h->executor_done && !occupied(h);
    for (unsigned c = 0; c < h->assembly.geometry.channels; ++c)
        if (!h->view[c].quiet || h->view[c].reports || h->stats.actor_owned[c]) *out = false;
    return FWLAB_NFC_API_OK;
}
static const struct fwlab_nfc_page_v2_provider_ops operations = {
    FWLAB_NFC_PAGE_V2_VERSION, sizeof(struct fwlab_nfc_page_v2_provider_ops), 0,
    submit, cancel, step, take, reset, quiet
};
size_t fwlab_nfc_channel_v2_arena_alignment(void)
{ return alignof(struct fwlab_nfc_channel_v2); }
size_t fwlab_nfc_channel_v2_arena_size(void)
{ return sizeof(struct fwlab_nfc_channel_v2) + CHANNELS * fwlab_nfc_page_v2_lab_arena_size(); }

static bool durations(struct fwlab_nfc_channel_v2 *h)
{
    const struct fwlab_nfc_page_v2_lab_mutation_config *c = &h->timing;
    uint64_t bytes = (uint64_t)(MAIN + OOB) * UINT64_C(1000000000), transfer, response, status;
    if (!c->read.channel_bytes_per_second) return false;
    transfer = bytes / c->read.channel_bytes_per_second + (bytes % c->read.channel_bytes_per_second != 0);
    bytes = (uint64_t)c->status_response_bytes * UINT64_C(1000000000);
    response = bytes / c->read.channel_bytes_per_second + (bytes % c->read.channel_bytes_per_second != 0);
    return add(c->status_command_ns, response, &status) &&
        add(c->read.command_ns, c->read.array_read_ns, &h->duration[1]) &&
        add(h->duration[1], transfer, &h->duration[1]) &&
        add(c->read.command_ns, transfer, &h->duration[2]) &&
        add(h->duration[2], c->program_confirm_ns, &h->duration[2]) &&
        add(h->duration[2], c->array_program_ns, &h->duration[2]) &&
        add(h->duration[2], status, &h->duration[2]) &&
        add(c->erase_command_ns, c->array_erase_ns, &h->duration[3]) &&
        add(h->duration[3], status, &h->duration[3]);
}
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_init_executor(void *arena, size_t bytes,
    const struct fwlab_nfc_page_v2_lab_mutation_config *timing,
    const struct fwlab_nand_channel_v2 *assembly, const struct fwlab_nfc_channel_executor *executor,
    struct fwlab_nfc_channel_v2 **out)
{
    struct fwlab_nfc_channel_v2 *h = arena;
    struct fwlab_nfc_page_v2_lab_mutation_config t;
    struct fwlab_nand_channel_v2 a;
    struct fwlab_nfc_geometry local;
    if (!arena || bytes < fwlab_nfc_channel_v2_arena_size() ||
        !page2_span(arena, fwlab_nfc_channel_v2_arena_size()) ||
        (uintptr_t)arena % alignof(struct fwlab_nfc_channel_v2) || !timing || !assembly ||
        !out || !outside(h, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (executor && (!executor->context || !executor->ops || !executor->ops->submit ||
        !executor->ops->poll || !executor->ops->shutdown)) return FWLAB_NFC_API_INVALID_CONTRACT;
    struct fwlab_nfc_channel_executor execution = executor ? *executor :
        (struct fwlab_nfc_channel_executor){&cooperative_ops, h};
    t = *timing; a = *assembly;
    if (a.version != FWLAB_NAND_CHANNEL_V2_VERSION || a.size != sizeof(a) || a.reserved ||
        !a.geometry.channels || a.geometry.channels > CHANNELS ||
        !a.geometry.luns_per_channel || a.geometry.luns_per_channel > 4 ||
        memcmp(&a.geometry, &t.read.base.geometry, sizeof(a.geometry)) ||
        page2_zero(a.media_uuid, sizeof(a.media_uuid)) || memcmp(a.media_uuid, t.read.base.media_uuid, 16) ||
        !a.aggregate.context || !a.aggregate.ops || a.aggregate.ops->version != FWLAB_NFC_CONTRACT_VERSION ||
        a.aggregate.ops->size != sizeof(*a.aggregate.ops) || a.aggregate.ops->reserved ||
        !a.aggregate.ops->read_page || !a.aggregate.ops->program || !a.aggregate.ops->erase ||
        !a.aggregate.ops->mark_runtime_bad || !a.aggregate.ops->hash ||
        !page2_zero(a.channel + a.geometry.channels, (CHANNELS - a.geometry.channels) * sizeof(a.channel[0])))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    unsigned luns = a.geometry.channels * a.geometry.luns_per_channel;
    if (!page2_zero(t.read.lun + luns, (FWLAB_NFC_PAGE_V2_LAB_LUNS - luns) * sizeof(t.read.lun[0])))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    for (unsigned i = 0; i < luns; ++i) for (unsigned j = 0; j < i; ++j)
        if (t.read.lun[i].die == t.read.lun[j].die && t.read.lun[i].package != t.read.lun[j].package)
            return FWLAB_NFC_API_INVALID_CONTRACT;
    local = a.geometry; local.channels = 1;
    for (unsigned c = 0; c < a.geometry.channels; ++c) {
        if (memcmp(&a.channel[c].geometry, &local, sizeof(local)) || !memcmp(a.channel[c].media_uuid, a.media_uuid, 16))
            return FWLAB_NFC_API_INVALID_CONTRACT;
        for (unsigned j = 0; j < c; ++j)
            if (!memcmp(a.channel[c].media_uuid, a.channel[j].media_uuid, 16) ||
                a.channel[c].scalar.context == a.channel[j].scalar.context) return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(h, 0, fwlab_nfc_channel_v2_arena_size()); h->timing = t; h->assembly = a;
    h->executor = execution; h->cooperative = (uint8_t)(executor == NULL);
    if (!durations(h)) return FWLAB_NFC_API_INVALID_CONTRACT;
    for (unsigned c = 0; c < a.geometry.channels; ++c) {
        struct fwlab_nfc_page_v2_lab_mutation_config child = t;
        child.read.base.geometry = local; memcpy(child.read.base.media_uuid, a.channel[c].media_uuid, 16);
        memset(child.read.lun, 0, sizeof(child.read.lun));
        memcpy(child.read.lun, t.read.lun + c * local.luns_per_channel,
               local.luns_per_channel * sizeof(child.read.lun[0]));
        if (fwlab_nfc_channel_actor_init(&h->actor[c], c,
            h->actors + c * fwlab_nfc_page_v2_lab_arena_size(), fwlab_nfc_page_v2_lab_arena_size(),
            &child, &a.channel[c], &h->stats.channel[c]) != FWLAB_NFC_API_OK)
            return FWLAB_NFC_API_INVALID_CONTRACT;
        h->view[c].stats = h->stats.channel[c];
    }
    h->magic = HUB_MAGIC; *out = h;
    return FWLAB_NFC_API_OK;
}
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_init(void *arena, size_t bytes,
    const struct fwlab_nfc_page_v2_lab_mutation_config *timing,
    const struct fwlab_nand_channel_v2 *assembly, struct fwlab_nfc_channel_v2 **out)
{ return fwlab_nfc_channel_v2_init_executor(arena, bytes, timing, assembly, NULL, out); }
struct fwlab_nfc_page_v2_provider fwlab_nfc_channel_v2_provider(struct fwlab_nfc_channel_v2 *h)
{ return (struct fwlab_nfc_page_v2_provider){live(h) ? &operations : NULL, live(h) ? h : NULL}; }
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_live_idle(const struct fwlab_nfc_channel_v2 *h, bool *out)
{
    if (!live(h) || !outside(h, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (h->busy) return FWLAB_NFC_API_WRONG_STATE;
    *out = h->stats.phase == FWLAB_NFC_CHANNEL_V2_BUILD && !h->stats.closed && !h->stats.quarantined && !occupied(h);
    for (unsigned c = 0; c < h->assembly.geometry.channels; ++c)
        if (h->view[c].reports || h->stats.actor_owned[c]) *out = false;
    return FWLAB_NFC_API_OK;
}
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_snapshot(const struct fwlab_nfc_channel_v2 *h,
                                                      struct fwlab_nfc_channel_v2_stats *out)
{
    if (!live(h) || !outside(h, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (h->busy) return FWLAB_NFC_API_WRONG_STATE;
    *out = h->stats; out->occupied_credits = occupied(h); out->results_pending = ready(h);
    for (unsigned i = 0; i < CREDITS; ++i) out->retirement_pending += h->slot[i].state == HUB_ACK || h->slot[i].state == HUB_RETIRING;
    return FWLAB_NFC_API_OK;
}
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_trace_at(const struct fwlab_nfc_channel_v2 *h,
    uint32_t channel, uint32_t index, struct fwlab_nfc_page_v2_lab_trace *out)
{
    if (!live(h) || channel >= h->assembly.geometry.channels || !outside(h, out, sizeof(*out)))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    if (h->busy || (h->stats.phase != FWLAB_NFC_CHANNEL_V2_BUILD &&
        h->stats.phase != FWLAB_NFC_CHANNEL_V2_JOINED && h->stats.phase != FWLAB_NFC_CHANNEL_V2_CLOSED))
        return FWLAB_NFC_API_WRONG_STATE;
    if (index >= h->trace_count[channel]) return FWLAB_NFC_API_NOT_FOUND;
    *out = h->trace[channel][index]; out->ppa.channel = (uint16_t)channel;
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_nfc_channel_v2_external_wait(const struct fwlab_nfc_channel_v2 *h, bool *out)
{
    if (!live(h) || !outside(h, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (h->busy) return FWLAB_NFC_API_WRONG_STATE;
    *out = false;
    if (h->cooperative || h->stats.poisoned || h->lost || ready(h)) return FWLAB_NFC_API_OK;
    if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_SHUTDOWN) *out = h->shutdown_wait != 0;
    else if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_ADMIT || h->stats.phase == FWLAB_NFC_CHANNEL_V2_RUN ||
             h->stats.phase == FWLAB_NFC_CHANNEL_V2_RETIRING || h->stats.phase == FWLAB_NFC_CHANNEL_V2_CLOSING)
        *out = h->posted == h->required && !jobs_complete(h);
    return FWLAB_NFC_API_OK;
}
