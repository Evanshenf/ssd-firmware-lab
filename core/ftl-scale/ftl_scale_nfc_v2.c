/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "ftl_scale_internal.h"
#include "fwlab/contracts/nand_media.h"
#include <string.h>

static void failed(struct sf_io *io)
{
    io->result.result = FWLAB_SPINE_V0_POISONED;
    io->result.read_valid = 0;
    io->result.effect = io->result.kind == SF_IO_READ ? SF_EFFECT_NONE : SF_EFFECT_UNKNOWN;
    io->result.completion.reason = FWLAB_NFC_REASON_INTERNAL;
    io->phase = SF_IO_DONE;
}
enum fwlab_spine_result_v0 sf_page_start_io(struct fwlab_ftl_scale *f,
    struct sf_io *io, uint32_t ppa, uint8_t frame, uint8_t kind,
    uint32_t count, bool window, uint8_t *main, uint8_t *oob, bool cancellable)
{
    struct fwlab_nfc_page_v2_request *r;
    if (!f || !f->initialized || !io || io->phase != SF_IO_IDLE || io->lower_owned || frame >= SF_FRAMES ||
        !count || count > SF_MAX_DELTAS || ppa >= f->physical_pages ||
        !main || !oob || (!window && count != 1) ||
        count > (uint32_t)f->config.geometry.pages_per_block - ppa % f->config.geometry.pages_per_block ||
        (kind != SF_IO_READ && kind != SF_IO_PROGRAM && kind != SF_IO_ERASE) ||
        (kind == SF_IO_ERASE && (window || count != 1 || ppa % f->config.geometry.pages_per_block)))
        return FWLAB_SPINE_V0_INVALID;
    if (!f->io.next_uid || f->io.next_uid > f->config.nfc_operation_uid_limit)
        return FWLAB_SPINE_V0_COUNTER_EXHAUSTED;
    memset(&io->result, 0, sizeof(io->result));
    io->result.result = FWLAB_SPINE_V0_IN_PROGRESS;
    io->result.kind = kind; io->result.frame = frame; io->result.ppa = ppa;
    io->result.count = (uint16_t)count;
    io->window_transfer = (uint8_t)window; io->cancel_sent = 0;
    io->cancel_allowed = (uint8_t)cancellable;
    io->transfer_main = main; io->transfer_oob = oob;
    r = &io->page_request; memset(r, 0, sizeof(*r));
    r->version = FWLAB_NFC_PAGE_V2_VERSION; r->size = sizeof(*r);
    r->operation.instance_nonce = f->config.nfc_instance_nonce;
    r->operation.operation_uid = f->io.next_uid;
    r->operation.controller_epoch = f->config.nfc_epoch;
    r->operation.generation = f->config.generation;
    f->io.next_uid = f->io.next_uid == UINT64_MAX ? 0 : f->io.next_uid + 1u;
    ++f->nfc_children;
    r->first = sf_ppa(f, ppa); r->page_count = count;
    r->kind = kind == SF_IO_READ ? FWLAB_NFC_PAGE_V2_READ_GROUP :
        kind == SF_IO_PROGRAM ? FWLAB_NFC_PAGE_V2_PROGRAM_GROUP : FWLAB_NFC_PAGE_V2_ERASE;
    if (kind == SF_IO_PROGRAM) {
        r->main = main;
        r->oob = oob;
        r->main_bytes = (size_t)count * SF_PAGE_BYTES;
        r->oob_bytes = (size_t)count * SF_OOB_BYTES;
    }
    io->phase = SF_IO_SUBMIT_FIRST;
    return FWLAB_SPINE_V0_OK;
}
static enum fwlab_spine_result_v0 page_start(struct fwlab_ftl_scale *f,
    uint32_t ppa, uint8_t frame, uint8_t kind, uint32_t count, bool window)
{
    if (!f || frame >= SF_FRAMES || !sf_io_idle(f) ||
        (window && (!f->window.main || !f->window.oob))) return FWLAB_SPINE_V0_INVALID;
    return sf_page_start_io(f, &f->io, ppa, frame, kind, count, window,
        window ? &f->window.main[0][0] : f->io.main[frame],
        window ? &f->window.oob[0][0] : f->io.oob[frame],
        f->work.kind == SF_WORK_HOST && (kind == SF_IO_READ || (kind == SF_IO_PROGRAM && window)));
}

static bool result_shape(const struct sf_io *io, const struct fwlab_nfc_page_v2_result *r)
{
    if (r->version != FWLAB_NFC_PAGE_V2_VERSION || r->size != sizeof(*r) || r->reserved0 || r->reserved1 ||
        memcmp(&r->operation, &io->page_request.operation, sizeof(r->operation)) ||
        memcmp(&r->first, &io->page_request.first, sizeof(r->first)) ||
        r->kind != io->page_request.kind || r->page_count != io->result.count ||
        r->terminal > FWLAB_NFC_TERMINAL_FAILED || r->reason > FWLAB_NFC_REASON_INTERNAL ||
        r->effect > FWLAB_NFC_PAGE_V2_EFFECT_NONCOMPLETE || r->read_valid > 1 ||
        r->backend_status > FWLAB_NFC_API_NOT_FOUND) return false;
    for (uint32_t p = 0; p < r->page_count; ++p) {
        const struct fwlab_nfc_page_v2_page_result *v = &r->page[p];
        if ((v->facts_valid & (uint8_t)~31u) || !sf_bytes_zero(v->reserved, sizeof(v->reserved)) ||
            v->reason > FWLAB_NFC_REASON_INTERNAL || v->effect > FWLAB_NFC_PAGE_V2_EFFECT_NONCOMPLETE ||
            v->integrity > FWLAB_NFC_INTEGRITY_TORN || v->block_health > FWLAB_NFC_BLOCK_RUNTIME_BAD ||
            v->ecc_status > FWLAB_NFC_ECC_UNCORRECTABLE ||
            (v->valid_region_mask & (uint8_t)~FWLAB_NFC_REGION_MASK) ||
            (v->applied_region_mask & (uint8_t)~FWLAB_NFC_REGION_MASK)) return false;
    }
    return sf_bytes_zero(r->page + r->page_count,
                        (SF_MAX_DELTAS - r->page_count) * sizeof(r->page[0]));
}
static struct sf_io_facts facts(const struct fwlab_nfc_page_v2_page_result *p, uint8_t terminal)
{
    struct sf_io_facts out = {0};
    out.available = p->facts_valid; out.terminal = terminal; out.reason = p->reason;
    if (p->facts_valid & FWLAB_NFC_PAGE_V2_FACT_GENERATION) {
        out.base_erase_generation = p->base_erase_generation;
        out.final_erase_generation = p->final_erase_generation;
    }
    if (p->facts_valid & FWLAB_NFC_PAGE_V2_FACT_HEALTH) out.block_health = p->block_health;
    if (p->facts_valid & FWLAB_NFC_PAGE_V2_FACT_ECC) {
        out.ecc_status = p->ecc_status; out.corrected_main_bits = p->corrected_main_bits;
        out.corrected_oob_bits = p->corrected_oob_bits; out.valid_region_mask = p->valid_region_mask;
    }
    out.integrity = p->integrity;
    if (p->facts_valid & FWLAB_NFC_PAGE_V2_FACT_EFFECT)
        out.physical_outcome = p->effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE ? FWLAB_NFC_PHYS_APPLIED : FWLAB_NFC_PHYS_NO_EFFECT;
    return out;
}
static bool complete_facts(const struct fwlab_ftl_scale *f, const struct sf_io *io,
                           const struct fwlab_nfc_page_v2_result *r)
{
    uint8_t required = FWLAB_NFC_PAGE_V2_FACT_GENERATION | FWLAB_NFC_PAGE_V2_FACT_HEALTH;
    bool reading = io->result.kind == SF_IO_READ;
    required |= reading ? FWLAB_NFC_PAGE_V2_FACT_CELL | FWLAB_NFC_PAGE_V2_FACT_ECC : FWLAB_NFC_PAGE_V2_FACT_EFFECT;
    if (r->backend_status != FWLAB_NFC_API_OK || r->terminal != FWLAB_NFC_TERMINAL_SUCCESS ||
        r->reason != FWLAB_NFC_REASON_NONE ||
        (reading ? !r->read_valid || r->effect != FWLAB_NFC_PAGE_V2_EFFECT_NONE :
                   r->effect != FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE)) return false;
    for (uint32_t i = 0; i < r->page_count; ++i) {
        const struct fwlab_nfc_page_v2_page_result *p = &r->page[i];
        if ((p->facts_valid & required) != required || p->block_health != FWLAB_NFC_BLOCK_GOOD ||
            p->integrity != FWLAB_NFC_INTEGRITY_COMPLETE || p->reason != FWLAB_NFC_REASON_NONE) return false;
        if (reading) {
            if (p->valid_region_mask != FWLAB_NFC_REGION_MASK ||
                (p->ecc_status != FWLAB_NFC_ECC_CLEAN && p->ecc_status != FWLAB_NFC_ECC_CORRECTED) ||
                p->page_state > FWLAB_NAND_PAGE_VALID || p->program_count != (p->page_state == FWLAB_NAND_PAGE_VALID)) return false;
        } else if (io->result.kind == SF_IO_PROGRAM) {
            if (p->effect != FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE ||
                p->applied_main_bytes != SF_PAGE_BYTES || p->applied_oob_bytes != SF_OOB_BYTES ||
                p->applied_region_mask != FWLAB_NFC_REGION_MASK ||
                p->base_erase_generation != p->final_erase_generation) return false;
        } else if (p->effect != FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE ||
                   p->applied_pages != f->config.geometry.pages_per_block ||
                   p->final_erase_generation <= p->base_erase_generation) return false;
    }
    return true;
}
bool sf_page_step_io(struct fwlab_ftl_scale *f, struct sf_io *io, bool cancelled, bool drive)
{
    const struct fwlab_nfc_page_v2_provider *n = &f->page_nfc;
    struct fwlab_nfc_page_v2_result result;
    struct fwlab_nfc_page_v2_output output;
    enum fwlab_nfc_api_result status;
    bool cancel = io->cancel_allowed && cancelled;
    if (io->phase == SF_IO_IDLE || io->phase == SF_IO_DONE) return false;
    if (io->phase == SF_IO_SUBMIT_FIRST) {
        struct fwlab_nfc_submit_result submitted;
        if (cancel) {
            io->result.result = FWLAB_SPINE_V0_WRONG_STATE;
            io->result.completion.terminal = FWLAB_NFC_TERMINAL_CANCELLED;
            io->result.completion.reason = FWLAB_NFC_REASON_CANCELLED;
            io->result.effect = SF_EFFECT_NONE; io->phase = SF_IO_DONE;
            return true;
        }
        submitted = n->ops->try_submit(n->context, &io->page_request);
        if (submitted.disposition == FWLAB_NFC_ACCEPTED) {
            io->lower_owned = 1;
            io->phase = SF_IO_WAIT_FIRST;
            if (io->result.kind == SF_IO_PROGRAM && io->cancel_allowed) f->work.effect_seen = 1;
            return true;
        }
        if (submitted.disposition == FWLAB_NFC_REJECTED) {
            /* REJECTED never crossed lower admission, so no DATA cell is owned. */
            io->result.result = FWLAB_SPINE_V0_INVALID;
            io->result.completion.reason = submitted.reason ? (uint8_t)submitted.reason : FWLAB_NFC_REASON_INTERNAL;
            io->result.completion.terminal = FWLAB_NFC_TERMINAL_FAILED;
            io->result.effect = SF_EFFECT_NONE; io->phase = SF_IO_DONE; return true;
        }
        if (submitted.disposition != FWLAB_NFC_BACKPRESSURE) { failed(io); return true; }
        return false;
    }
    if (io->phase != SF_IO_WAIT_FIRST) { failed(io); return true; }
    if (cancel && !io->cancel_sent) {
        status = n->ops->cancel(n->context, &io->page_request.operation);
        if (status != FWLAB_NFC_API_OK) { failed(io); return true; }
        io->cancel_sent = 1;
        return true;
    }
    memset(&output, 0, sizeof(output));
    if (io->result.kind == SF_IO_READ && !cancel) {
        output.main = io->transfer_main;
        output.oob = io->transfer_oob;
        output.main_bytes = (size_t)io->result.count * SF_PAGE_BYTES;
        output.oob_bytes = (size_t)io->result.count * SF_OOB_BYTES;
    }
    status = n->ops->take_result(n->context, &io->page_request.operation, &result,
                                output.main ? &output : NULL);
    if (status == FWLAB_NFC_API_OK) io->lower_owned = 0;
    if (status == FWLAB_NFC_API_WRONG_STATE) {
        struct fwlab_nfc_page_v2_step_result step = {0};
        if (!drive) return false;
        if (n->ops->step(n->context, 1, &step) != FWLAB_NFC_API_OK || step.units_used > 1) {
            failed(io); return true;
        }
        return step.units_used != 0;
    }
    if (status != FWLAB_NFC_API_OK || !result_shape(io, &result)) { failed(io); return true; }
    for (uint32_t p = 0; p < result.page_count; ++p) io->page_facts[p] = facts(&result.page[p], result.terminal);
    io->result.completion = io->page_facts[0]; io->result.completion.reason = result.reason;
    if (result.reason == FWLAB_NFC_REASON_ECC_UNCORRECTABLE) {
        for (uint32_t p = 0; p < result.page_count; ++p)
            if (io->page_facts[p].ecc_status == FWLAB_NFC_ECC_UNCORRECTABLE) {
                io->result.completion = io->page_facts[p]; io->result.completion.reason = result.reason; break;
            }
    }
    io->result.effect = result.effect == FWLAB_NFC_PAGE_V2_EFFECT_NONE ? SF_EFFECT_NONE :
        result.effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE ? SF_EFFECT_COMPLETE : SF_EFFECT_UNKNOWN;
    io->result.read_valid = (uint8_t)(io->result.kind == SF_IO_READ && result.read_valid &&
                                     result.delivered_pages == result.page_count && output.main != NULL);
    if (result.delivered_pages != (io->result.kind == SF_IO_READ && result.read_valid && output.main ? result.page_count : 0)) {
        failed(io); return true;
    }
    io->result.result = complete_facts(f, io, &result) &&
        (io->result.kind != SF_IO_READ || io->result.read_valid) ? FWLAB_SPINE_V0_OK : FWLAB_SPINE_V0_QUARANTINED;
    /* A pre-dispatch failure may include a backend error and still prove NONE.
     * Only the typed effect says whether this operation may have changed DATA. */
    if (io->result.kind != SF_IO_READ && io->result.result != FWLAB_SPINE_V0_OK &&
        io->result.effect != SF_EFFECT_NONE)
        io->result.effect = SF_EFFECT_UNKNOWN;
    io->phase = SF_IO_DONE; return true;
}
static bool page_step(struct fwlab_ftl_scale *f)
{ return sf_page_step_io(f, &f->io, f->parent.cancelled != 0, true); }
static enum fwlab_nfc_api_result page_reset(struct fwlab_ftl_scale *f)
{ return f->page_nfc.ops->reset_begin(f->page_nfc.context, f->config.nfc_instance_nonce, f->config.nfc_epoch); }
static enum fwlab_nfc_api_result page_drive(struct fwlab_ftl_scale *f)
{ struct fwlab_nfc_page_v2_step_result result; return f->page_nfc.ops->step(f->page_nfc.context, 1, &result); }
static enum fwlab_nfc_api_result page_quiet(struct fwlab_ftl_scale *f, bool *quiet)
{ return f->page_nfc.ops->quiescent(f->page_nfc.context, f->config.nfc_instance_nonce, f->config.nfc_epoch, quiet); }
const struct sf_nfc_adapter sf_nfc_page2_adapter = { page_start, page_step, page_reset, page_drive, page_quiet };
