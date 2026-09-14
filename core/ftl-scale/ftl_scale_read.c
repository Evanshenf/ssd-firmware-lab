/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "ftl_scale_internal.h"
#include <string.h>

bool sf_read_pool_busy(const struct fwlab_ftl_scale *f)
{ return f && f->reads && f->reads->active; }

static struct sf_read_run *empty_run(struct sf_read_pool *p)
{
    for (unsigned i = 0; i < SF_READ_RUNS; ++i)
        if (p->run[i].state == SF_READ_EMPTY) return &p->run[i];
    return NULL;
}
static struct sf_read_run *prefix_run(struct fwlab_ftl_scale *f)
{
    for (unsigned i = 0; i < SF_READ_RUNS; ++i) {
        struct sf_read_run *r = &f->reads->run[i];
        if (r->state == SF_READ_READY && r->logical_offset == f->parent.completed_lbas)
            return r;
    }
    return NULL;
}
bool sf_read_pool_runnable(const struct fwlab_ftl_scale *f)
{
    if (!sf_read_pool_busy(f)) return false;
    for (unsigned i = 0; i < SF_READ_RUNS; ++i) {
        const struct sf_read_run *r = &f->reads->run[i];
        if ((r->state == SF_READ_READY && (f->reads->stopping ||
             r->logical_offset == f->parent.completed_lbas)) ||
            (r->state == SF_READ_PENDING && r->io.phase == SF_IO_DONE)) return true;
        if (!f->reads->stopping && r->state == SF_READ_EMPTY &&
            f->reads->issued_lbas < f->parent.request.lba_count) return true;
    }
    return false;
}
static void stop(struct fwlab_ftl_scale *f, uint32_t fault)
{
    f->reads->stopping = 1;
    if (!f->reads->fault) f->reads->fault = fault;
}
enum fwlab_spine_result_v0 sf_read_parent_prepare(struct fwlab_ftl_scale *f,
                                                const struct sf_parent *p)
{
    /* The sole parent and active-pool maintenance exclusion keep these map
     * snapshots immutable even when later Host requests may write. */
    if (!f->reads || !f->parallel_reads || sf_read_pool_busy(f) || !sf_io_idle(f) ||
        p->request.operation != FWLAB_BLOCK_V0_READ) return FWLAB_SPINE_V0_WRONG_STATE;
    f->reads->issued_lbas = f->reads->fault = 0;
    f->reads->stopping = 0; f->reads->active = 1;
    f->work.request = p->request; f->work.kind = SF_WORK_HOST;
    f->work.phase = SF_W_READ_PAGE; f->work.effect_seen = 0;
    return FWLAB_SPINE_V0_OK;
}

static bool prepare(struct fwlab_ftl_scale *f, struct sf_read_run *r)
{
    struct sf_read_pool *p = f->reads;
    const struct fwlab_block_request_v0 *parent = &f->parent.request;
    uint64_t lba = parent->lba + p->issued_lbas;
    uint32_t remaining = parent->lba_count - p->issued_lbas;
    uint32_t ppb = f->config.geometry.pages_per_block;
    bool partial = lba % SF_SECTORS_PER_PAGE != 0 || remaining < SF_SECTORS_PER_PAGE;
    uint32_t lbas = partial ? SF_SECTORS_PER_PAGE - (uint32_t)(lba % SF_SECTORS_PER_PAGE) : remaining;
    uint32_t pages, count = 0;
    uint64_t offset = (uint64_t)parent->buffer_span.offset + (uint64_t)p->issued_lbas * FWLAB_FTL_SCALE_LBA_BYTES;
    if (lbas > remaining) lbas = remaining;
    pages = partial ? 1 : lbas / SF_SECTORS_PER_PAGE;
    if (pages > SF_MAX_DELTAS) pages = SF_MAX_DELTAS;
    r->first_lpn = (uint32_t)(lba / SF_SECTORS_PER_PAGE);
    const struct sf_map_entry *first = &f->map[r->first_lpn];
    if (!sf_read_map_valid(f, first)) return false;
    r->zero = (uint8_t)(first->state != SF_VALUE); r->first_ppa = first->ppa;
    r->block_uid = r->zero ? 0 : f->blocks[first->ppa / ppb].disk.block_uid;
    while (count < pages) {
        const struct sf_map_entry *e = &f->map[r->first_lpn + count];
        if (!sf_read_map_valid(f, e)) return false;
        if (r->zero) { if (e->state == SF_VALUE) break; }
        else if (e->state != SF_VALUE || e->ppa != first->ppa + count ||
                 e->ppa / ppb != first->ppa / ppb || e->erase_generation != first->erase_generation) break;
        r->map[count++] = *e;
    }
    pages = count;
    if (!partial) lbas = pages * SF_SECTORS_PER_PAGE;
    if (!pages || offset > UINT32_MAX) return false;
    r->request = *parent; r->request.lba = lba; r->request.lba_count = lbas;
    r->request.buffer_span.offset = (uint32_t)offset;
    r->request.buffer_span.length = lbas * FWLAB_FTL_SCALE_LBA_BYTES;
    if (!fwlab_controller_buffer_span_v0_valid_for_lease(&r->request.buffer_span,
            &r->request.buffer, FWLAB_CONTROLLER_BUFFER_V0_WRITE)) return false;
    r->pages = pages; r->logical_offset = p->issued_lbas;
    if (r->zero) {
        memset(r->main, 0, (size_t)pages * SF_PAGE_BYTES);
        r->state = SF_READ_READY;
    } else {
        if (!sf_child_credit(f, 1)) { stop(f, FWLAB_BLOCK_V0_FAULT_RESOURCE); return false; }
        if (sf_page_start_io(f, &r->io, r->first_ppa, 0, SF_IO_READ, pages, true,
                            &r->main[0][0], &r->oob[0][0], true) != FWLAB_SPINE_V0_OK) return false;
        r->state = SF_READ_PENDING;
    }
    p->issued_lbas += lbas;
    return true;
}
static bool valid_read(const struct fwlab_ftl_scale *f, const struct sf_read_run *r)
{
    const struct sf_io_result *io = &r->io.result;
    if (io->result != FWLAB_SPINE_V0_OK || !io->read_valid || io->kind != SF_IO_READ ||
        io->ppa != r->first_ppa || io->count != r->pages) return false;
    for (uint32_t i = 0; i < r->pages; ++i)
        if (!sf_read_page_valid(f, r->first_lpn + i, &r->map[i], r->block_uid,
                               &r->io.page_facts[i], r->main[i], r->oob[i])) return false;
    return true;
}
static void release(struct sf_read_run *r)
{
    /* No accepted lower operation or retained lower result remains here. */
    r->state = SF_READ_EMPTY; r->io.phase = SF_IO_IDLE;
    r->io.transfer_main = r->io.transfer_oob = NULL;
}

bool sf_read_pool_step(struct fwlab_ftl_scale *f)
{
    struct sf_read_pool *p = f->reads;
    bool advanced = false, pending = false;
    if (!sf_read_pool_busy(f)) return false;
    if (f->parent.cancelled || f->admission_closed) stop(f, FWLAB_NFC_REASON_CANCELLED);

    /* Bounded A2 cycle: fill/submit every eligible run BEFORE advancing NFC.
     * A backpressured UID remains ahead of all newly issued UIDs. */
    for (unsigned i = 0; i < SF_READ_RUNS; ++i) {
        struct sf_read_run *r = NULL;
        for (unsigned j = 0; j < SF_READ_RUNS; ++j)
            if (p->run[j].state == SF_READ_PENDING && p->run[j].io.phase == SF_IO_SUBMIT_FIRST &&
                (!r || p->run[j].io.page_request.operation.operation_uid <
                       r->io.page_request.operation.operation_uid))
                r = &p->run[j];
        if (!r && !p->stopping && p->issued_lbas < f->parent.request.lba_count) {
            r = empty_run(p);
            if (r) {
                if (!prepare(f, r)) { stop(f, SF_FAULT_STATE); break; }
                advanced = true;
            }
        }
        if (!r) break;
        if (r->state == SF_READ_READY) continue;
        advanced = sf_page_step_io(f, &r->io, p->stopping != 0, false) || advanced;
        if (r->io.phase == SF_IO_SUBMIT_FIRST) break;
        if (r->io.phase == SF_IO_DONE && r->io.result.result != FWLAB_SPINE_V0_OK)
            stop(f, SF_FAULT_IO);
    }
    if (p->stopping) {
        for (unsigned i = 0; i < SF_READ_RUNS; ++i) {
            struct sf_read_run *r = &p->run[i];
            if (r->state == SF_READ_PENDING)
                advanced = sf_page_step_io(f, &r->io, true, false) || advanced;
        }
    }
    for (unsigned i = 0; i < SF_READ_RUNS; ++i)
        pending = pending || (p->run[i].state == SF_READ_PENDING &&
            (p->run[i].io.phase == SF_IO_WAIT_FIRST || p->run[i].io.phase == SF_IO_SUBMIT_FIRST));
    if (pending) {
        struct fwlab_nfc_page_v2_step_result result = {0};
        if (f->page_nfc.ops->step(f->page_nfc.context, 1, &result) != FWLAB_NFC_API_OK || result.units_used > 1) {
            /* Unusable provider: retain outstanding slots, never certify zero. */
            sf_fail(f, SF_FAULT_IO); return true;
        }
        advanced = result.units_used != 0 || advanced;
    }
    /* Collect all tokens, including later logical runs. Error/cancel does not
     * disable this pass or subsequent lower advancement. */
    for (unsigned i = 0; i < SF_READ_RUNS; ++i) {
        struct sf_read_run *r = &p->run[i];
        if (r->state != SF_READ_PENDING) continue;
        /* Only ordered fill admits fresh UIDs. A control ACK consumed above
         * must not let this physical-slot-order collector bypass an older
         * unaccepted run when credits become available. */
        if (r->io.phase == SF_IO_SUBMIT_FIRST) continue;
        advanced = sf_page_step_io(f, &r->io, p->stopping != 0, false) || advanced;
        if (r->io.phase != SF_IO_DONE) continue;
        if (r->io.lower_owned) {
            /* A provider control/API failure is not result consumption. */
            sf_fail(f, SF_FAULT_IO); return true;
        }
        if (!p->stopping && !valid_read(f, r)) stop(f, SF_FAULT_IO);
        r->io.phase = SF_IO_IDLE;
        r->state = SF_READ_READY;
        advanced = true;
    }
    if (!p->stopping) {
        for (unsigned i = 0; i < SF_READ_RUNS; ++i) {
            struct sf_read_run *r = prefix_run(f);
            if (!r) break;
            for (uint32_t page = 0; page < r->pages; ++page) {
                if (memcmp(&r->map[page], &f->map[r->first_lpn + page], sizeof(r->map[page]))) {
                    stop(f, SF_FAULT_STATE); break;
                }
                sf_read_zero_invalid(r->main[page], r->map[page].valid_mask);
            }
            if (p->stopping) break;
            uint32_t offset = (uint32_t)(r->request.lba % SF_SECTORS_PER_PAGE) * FWLAB_FTL_SCALE_LBA_BYTES;
            if (f->controller_buffer.ops->write(f->controller_buffer.context, &r->request.buffer,
                &r->request.buffer_span, &r->main[0][0] + offset, r->request.buffer_span.length) != FWLAB_CONTROLLER_BUFFER_V0_OK) {
                stop(f, SF_FAULT_STATE); break;
            }
            f->work.request = r->request; f->work.kind = SF_WORK_HOST;
            release(r);
            sf_parent_group_success(f);
            advanced = true;
        }
    }
    bool occupied = false;
    for (unsigned i = 0; i < SF_READ_RUNS; ++i) {
        struct sf_read_run *r = &p->run[i];
        if (p->stopping && r->state == SF_READ_READY) { release(r); advanced = true; }
        occupied = occupied || r->state != SF_READ_EMPTY;
    }
    if (!occupied && (p->stopping || f->parent.completed_lbas == f->parent.request.lba_count)) {
        p->active = 0;
        if (p->stopping) sf_parent_fail(f, p->fault);
        advanced = true;
    }
    return advanced;
}
