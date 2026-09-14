/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "fwlab/private/nfc_channel_v2.h"
#include "nfc_page_v2_internal.h"

#include <stdalign.h>
#include <string.h>

#define HUB_MAGIC UINT64_C(0x43484e5741564534)
#define CREDITS FWLAB_NFC_CHANNEL_V2_CREDITS
#define CHANNELS FWLAB_NAND_CHANNEL_V2_MAX
#define MAIN FWLAB_NFC_PAGE_V2_MAIN_BYTES
#define OOB FWLAB_NFC_PAGE_V2_OOB_BYTES
#define PAGES FWLAB_NFC_PAGE_V2_MAX_PAGES

enum hub_slot_state { HUB_FREE, HUB_QUEUED, HUB_CHILD, HUB_REPORT, HUB_READY, HUB_ACK };
struct hub_slot {
    struct fwlab_nfc_page_v2_request request;
    struct fwlab_nfc_page_v2_result result;
    uint64_t charge;
    uint8_t state;
    _Alignas(64) uint8_t main[PAGES * MAIN];
    uint8_t oob[PAGES * OOB];
};
struct hub_actor {
    struct fwlab_nfc_page_v2_lab *model;
    struct fwlab_nfc_page_v2_provider provider;
    uint8_t reports_held, reset_done, quiet_done;
};
struct fwlab_nfc_channel_v2 {
    uint64_t magic, last_uid, duration[4];
    struct fwlab_nfc_page_v2_lab_mutation_config timing;
    struct fwlab_nand_channel_v2 assembly;
    struct fwlab_nfc_channel_v2_stats stats;
    struct hub_actor actor[CHANNELS];
    uint32_t cursor;
    uint8_t busy;
    struct hub_slot slot[CREDITS];
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
        bool equal = s->state != HUB_ACK && page2_canonical_equal(r, &s->request);
        return disposition(equal ? FWLAB_NFC_ACCEPTED : FWLAB_NFC_REJECTED,
                           equal ? FWLAB_NFC_REASON_NONE : FWLAB_NFC_REASON_STALE);
    }
    if (h->stats.closed || h->stats.quarantined)
        return disposition(FWLAB_NFC_REJECTED, h->stats.closed ? FWLAB_NFC_REASON_RESET : FWLAB_NFC_REASON_INTERNAL);
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
        memcpy(s->main, r->main, r->main_bytes); memcpy(s->oob, r->oob, r->oob_bytes);
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
    if (!s || s->state == HUB_ACK) return FWLAB_NFC_API_STALE_TOKEN;
    /* WAVE4 drain-only policy: no child cancel, even before child admission.
     * The upper owner decides whether to publish or discard the result. */
    return FWLAB_NFC_API_OK;
}
static void rejected_child(struct fwlab_nfc_channel_v2 *h, unsigned index,
                            struct fwlab_nfc_submit_result submitted)
{
    struct hub_slot *s = &h->slot[index];
    struct fwlab_nfc_page_v2_result *r = &s->result;
    memset(r, 0, sizeof(*r));
    r->version = FWLAB_NFC_PAGE_V2_VERSION; r->size = sizeof(*r);
    r->operation = s->request.operation; r->first = s->request.first;
    r->page_count = s->request.page_count; r->kind = s->request.kind;
    r->terminal = FWLAB_NFC_TERMINAL_FAILED;
    r->reason = submitted.reason && submitted.reason <= FWLAB_NFC_REASON_INTERNAL ?
        (uint8_t)submitted.reason : FWLAB_NFC_REASON_INTERNAL;
    r->backend_status = submitted.disposition == FWLAB_NFC_BACKPRESSURE ?
        FWLAB_NFC_API_NO_CAPACITY : FWLAB_NFC_API_INVALID_CONTRACT;
    if (r->kind != FWLAB_NFC_PAGE_V2_READ_GROUP)
        for (uint32_t i = 0; i < r->page_count; ++i) r->page[i].facts_valid = FWLAB_NFC_PAGE_V2_FACT_EFFECT;
    s->state = HUB_REPORT;
    h->actor[r->first.channel].reports_held |= (uint8_t)(1u << index);
    h->stats.quarantined = 1;
}
static bool result_identity(const struct hub_slot *s)
{
    struct fwlab_nfc_ppa local = s->request.first;
    const struct fwlab_nfc_page_v2_result *r = &s->result;
    local.channel = 0;
    return r->version == FWLAB_NFC_PAGE_V2_VERSION && r->size == sizeof(*r) &&
        !r->reserved0 && !r->reserved1 && page2_key_equal(&r->operation, &s->request.operation) &&
        !memcmp(&r->first, &local, sizeof(local)) && r->kind == s->request.kind &&
        r->page_count == s->request.page_count && r->terminal <= FWLAB_NFC_TERMINAL_FAILED &&
        r->read_valid <= 1 && r->delivered_pages ==
            (r->kind == FWLAB_NFC_PAGE_V2_READ_GROUP && r->read_valid ? r->page_count : 0);
}
static enum fwlab_nfc_api_result run_one(struct fwlab_nfc_channel_v2 *h, bool *advanced)
{
    unsigned outstanding = 0;
    /* Collection is internal, not conditional on caller take/discard. The
     * actor reports remain owned until their later retirement acknowledgment. */
    for (unsigned i = 0; i < CREDITS; ++i) {
        struct hub_slot *s = &h->slot[i];
        struct fwlab_nfc_page_v2_output output = {s->main, (size_t)s->request.page_count * MAIN,
                                                 s->oob, (size_t)s->request.page_count * OOB};
        if (s->state != HUB_CHILD) continue;
        struct hub_actor *a = &h->actor[s->request.first.channel];
        enum fwlab_nfc_api_result r = a->provider.ops->take_result(a->provider.context,
            &s->request.operation, &s->result, s->request.kind == FWLAB_NFC_PAGE_V2_READ_GROUP ? &output : NULL);
        if (r == FWLAB_NFC_API_OK) {
            --h->stats.actor_owned[s->request.first.channel];
            s->state = HUB_REPORT; a->reports_held |= (uint8_t)(1u << i);
            if (!result_identity(s)) return poison(h);
            s->result.first = s->request.first;
            s->result.delivered_pages = 0; /* No caller output has occurred. */
            *advanced = true;
            return FWLAB_NFC_API_OK;
        }
        if (r != FWLAB_NFC_API_WRONG_STATE) return poison(h);
        ++outstanding;
    }
    if (!outstanding) {
        uint64_t joined = h->stats.now_ns;
        for (unsigned c = 0; c < h->assembly.geometry.channels; ++c) {
            if (fwlab_nfc_page_v2_lab_snapshot(h->actor[c].model, &h->stats.channel[c]) != FWLAB_NFC_API_OK ||
                h->stats.channel[c].active_slots || h->stats.channel[c].held_luns || h->stats.channel[c].busy_channels)
                return poison(h);
            if (joined < h->stats.channel[c].now_ns) joined = h->stats.channel[c].now_ns;
            if (h->stats.channel[c].quarantined) h->stats.quarantined = 1;
        }
        h->stats.now_ns = joined;
        for (unsigned i = 0; i < CREDITS; ++i)
            if (h->slot[i].state == HUB_REPORT) h->slot[i].state = HUB_READY;
        h->stats.phase = FWLAB_NFC_CHANNEL_V2_JOINED;
        count(h, &h->stats.joined_batches, 1); *advanced = true;
        return FWLAB_NFC_API_OK;
    }
    for (unsigned n = 0; n < h->assembly.geometry.channels; ++n) {
        unsigned c = h->cursor++ % h->assembly.geometry.channels;
        if (!h->stats.actor_owned[c]) continue;
        struct fwlab_nfc_page_v2_step_result result = {0};
        enum fwlab_nfc_api_result r = h->actor[c].provider.ops->step(h->actor[c].provider.context, 1, &result);
        if (r != FWLAB_NFC_API_OK || result.units_used > 1) return poison(h);
        *advanced = result.units_used != 0;
        return FWLAB_NFC_API_OK;
    }
    return poison(h);
}
static enum fwlab_nfc_api_result step_one(struct fwlab_nfc_channel_v2 *h, bool *advanced)
{
    enum fwlab_nfc_api_result r;
    *advanced = false;
    if (h->stats.poisoned) return FWLAB_NFC_API_INVARIANT_FAILURE;
    if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_JOINED) {
        for (unsigned i = 0; i < CREDITS; ++i) {
            struct hub_slot *s = &h->slot[i];
            if (s->state != HUB_ACK) continue;
            struct hub_actor *a = &h->actor[s->request.first.channel];
            if (!(a->reports_held & (1u << i))) return poison(h);
            a->reports_held &= (uint8_t)~(1u << i);
            memset(&s->request, 0, sizeof(s->request)); memset(&s->result, 0, sizeof(s->result));
            s->charge = 0; s->state = HUB_FREE;
            count(h, &h->stats.retired_acks, 1); *advanced = true;
            if (!occupied(h)) h->stats.phase = FWLAB_NFC_CHANNEL_V2_BUILD;
            return FWLAB_NFC_API_OK;
        }
        return FWLAB_NFC_API_OK; /* Caller still owns one or more results. */
    }
    if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_BUILD) {
        if (occupied(h)) {
            memset(h->stats.batch_requests, 0, sizeof(h->stats.batch_requests));
            for (unsigned i = 0; i < CREDITS; ++i)
                if (h->slot[i].state == HUB_QUEUED) ++h->stats.batch_requests[h->slot[i].request.first.channel];
            h->cursor = 0; h->stats.phase = FWLAB_NFC_CHANNEL_V2_FLOOR;
            count(h, &h->stats.sealed_batches, 1); *advanced = true;
        } else if (h->stats.closed) {
            h->cursor = 0; h->stats.phase = FWLAB_NFC_CHANNEL_V2_CLOSING; *advanced = true;
        }
        return FWLAB_NFC_API_OK;
    }
    if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_FLOOR) {
        r = fwlab_nfc_page_v2_lab_admission_floor(h->actor[h->cursor].model, h->stats.now_ns);
        if (r != FWLAB_NFC_API_OK) return poison(h);
        if (++h->cursor == h->assembly.geometry.channels) {
            h->cursor = 0; h->stats.phase = FWLAB_NFC_CHANNEL_V2_ADMIT;
        }
        *advanced = true; return FWLAB_NFC_API_OK;
    }
    if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_ADMIT) {
        /* BUILD can only start with all credits free: occupied slot order is
         * global admission/UID order. All ingress completes before RUN. */
        while (h->cursor < CREDITS && h->slot[h->cursor].state != HUB_QUEUED) ++h->cursor;
        if (h->cursor < CREDITS) {
            unsigned i = h->cursor++;
            struct hub_slot *s = &h->slot[i];
            struct fwlab_nfc_page_v2_request local = s->request;
            struct hub_actor *a = &h->actor[local.first.channel];
            local.first.channel = 0;
            if (local.kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP) { local.main = s->main; local.oob = s->oob; }
            struct fwlab_nfc_submit_result result = a->provider.ops->try_submit(a->provider.context, &local);
            if (result.disposition == FWLAB_NFC_ACCEPTED) {
                s->state = HUB_CHILD; ++h->stats.actor_owned[s->request.first.channel];
            } else if (result.disposition == FWLAB_NFC_REJECTED || result.disposition == FWLAB_NFC_BACKPRESSURE)
                rejected_child(h, i, result); /* Accepted by hub, never by child: known NONE. */
            else return poison(h);
        } else { h->cursor = 0; h->stats.phase = FWLAB_NFC_CHANNEL_V2_RUN; }
        *advanced = true; return FWLAB_NFC_API_OK;
    }
    if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_RUN) return run_one(h, advanced);
    if (h->stats.phase == FWLAB_NFC_CHANNEL_V2_CLOSING) {
        if (h->cursor == h->assembly.geometry.channels) {
            h->stats.phase = FWLAB_NFC_CHANNEL_V2_CLOSED; *advanced = true; return FWLAB_NFC_API_OK;
        }
        struct hub_actor *a = &h->actor[h->cursor];
        if (a->reports_held || h->stats.actor_owned[h->cursor]) return poison(h);
        if (!a->reset_done) {
            r = a->provider.ops->reset_begin(a->provider.context,
                h->timing.read.base.instance_nonce, h->timing.read.base.controller_epoch);
            if (r != FWLAB_NFC_API_OK) return poison(h);
            a->reset_done = 1; *advanced = true;
        } else {
            bool quiet = false;
            r = a->provider.ops->quiescent(a->provider.context, h->timing.read.base.instance_nonce,
                h->timing.read.base.controller_epoch, &quiet);
            if (r != FWLAB_NFC_API_OK) return poison(h);
            if (quiet) {
                if (fwlab_nfc_page_v2_lab_snapshot(a->model, &h->stats.channel[h->cursor]) != FWLAB_NFC_API_OK)
                    return poison(h);
                a->quiet_done = 1; ++h->cursor; *advanced = true;
            }
        }
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
    if (!s || s->state == HUB_ACK) return FWLAB_NFC_API_STALE_TOKEN;
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
        memcpy(d.main, s->main, d.main_bytes); memcpy(d.oob, s->oob, d.oob_bytes);
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
    *out = h->stats.closed && h->stats.phase == FWLAB_NFC_CHANNEL_V2_CLOSED && !occupied(h);
    for (unsigned c = 0; c < h->assembly.geometry.channels; ++c)
        if (!h->actor[c].quiet_done || h->actor[c].reports_held || h->stats.actor_owned[c]) *out = false;
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
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_init(void *arena, size_t bytes,
    const struct fwlab_nfc_page_v2_lab_mutation_config *timing,
    const struct fwlab_nand_channel_v2 *assembly, struct fwlab_nfc_channel_v2 **out)
{
    struct fwlab_nfc_channel_v2 *h = arena;
    struct fwlab_nfc_page_v2_lab_mutation_config t;
    struct fwlab_nand_channel_v2 a;
    struct fwlab_nfc_geometry local;
    if (!arena || bytes < fwlab_nfc_channel_v2_arena_size() ||
        !page2_span(arena, fwlab_nfc_channel_v2_arena_size()) ||
        (uintptr_t)arena % alignof(struct fwlab_nfc_channel_v2) || !timing || !assembly ||
        !out || !outside(h, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
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
    if (!durations(h)) return FWLAB_NFC_API_INVALID_CONTRACT;
    for (unsigned c = 0; c < a.geometry.channels; ++c) {
        struct fwlab_nfc_page_v2_lab_mutation_config child = t;
        child.read.base.geometry = local; memcpy(child.read.base.media_uuid, a.channel[c].media_uuid, 16);
        memset(child.read.lun, 0, sizeof(child.read.lun));
        memcpy(child.read.lun, t.read.lun + c * local.luns_per_channel,
               local.luns_per_channel * sizeof(child.read.lun[0]));
        if (fwlab_nfc_page_v2_lab_mutation_init(h->actors + c * fwlab_nfc_page_v2_lab_arena_size(),
            fwlab_nfc_page_v2_lab_arena_size(), &child, &a.channel[c], &h->actor[c].model) != FWLAB_NFC_API_OK)
            return FWLAB_NFC_API_INVALID_CONTRACT;
        h->actor[c].provider = fwlab_nfc_page_v2_lab_provider(h->actor[c].model);
        if (fwlab_nfc_page_v2_lab_snapshot(h->actor[c].model, &h->stats.channel[c]) != FWLAB_NFC_API_OK)
            return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    h->magic = HUB_MAGIC; *out = h;
    return FWLAB_NFC_API_OK;
}
struct fwlab_nfc_page_v2_provider fwlab_nfc_channel_v2_provider(struct fwlab_nfc_channel_v2 *h)
{ return (struct fwlab_nfc_page_v2_provider){live(h) ? &operations : NULL, live(h) ? h : NULL}; }
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_live_idle(const struct fwlab_nfc_channel_v2 *h, bool *out)
{
    if (!live(h) || !outside(h, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (h->busy) return FWLAB_NFC_API_WRONG_STATE;
    *out = h->stats.phase == FWLAB_NFC_CHANNEL_V2_BUILD && !h->stats.closed && !h->stats.quarantined && !occupied(h);
    for (unsigned c = 0; c < h->assembly.geometry.channels; ++c)
        if (h->actor[c].reports_held || h->stats.actor_owned[c]) *out = false;
    return FWLAB_NFC_API_OK;
}
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_snapshot(const struct fwlab_nfc_channel_v2 *h,
                                                      struct fwlab_nfc_channel_v2_stats *out)
{
    if (!live(h) || !outside(h, out, sizeof(*out))) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (h->busy) return FWLAB_NFC_API_WRONG_STATE;
    *out = h->stats; out->occupied_credits = occupied(h); out->results_pending = ready(h);
    for (unsigned i = 0; i < CREDITS; ++i) out->retirement_pending += h->slot[i].state == HUB_ACK;
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
    enum fwlab_nfc_api_result r = fwlab_nfc_page_v2_lab_trace_at(h->actor[channel].model, index, out);
    if (r == FWLAB_NFC_API_OK) out->ppa.channel = (uint16_t)channel;
    return r;
}
