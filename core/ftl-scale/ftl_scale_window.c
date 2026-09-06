/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "ftl_scale_internal.h"
#include <string.h>

static bool map_valid(const struct fwlab_ftl_scale *f, const struct sf_map_entry *e)
{
    uint32_t ppb = f->config.geometry.pages_per_block;
    const struct sf_block_disk *b;
    uint64_t page;
    if (e->state != SF_VALUE)
        return e->state <= SF_TOMBSTONE && e->ppa == SF_NONE && !e->valid_mask && !e->erase_generation && !e->data_uid;
    if (e->ppa >= f->physical_pages || e->ppa / ppb < f->root.layout.data_first_block || !e->valid_mask) return false;
    b = &f->blocks[e->ppa / ppb].disk; page = e->ppa % ppb;
    return b->block_uid && b->block_uid <= (UINT64_MAX - page) / ppb &&
        e->erase_generation == b->erase_generation && e->data_uid == b->block_uid * ppb + page;
}
static uint8_t mask_for(const struct fwlab_block_request_v0 *r, uint32_t lpn)
{
    uint64_t first = (uint64_t)lpn * SF_SECTORS_PER_PAGE;
    uint64_t end = r->lba + r->lba_count;
    uint8_t mask = 0;
    for (unsigned n = 0; n < SF_SECTORS_PER_PAGE; ++n)
        if (first + n >= r->lba && first + n < end) mask |= (uint8_t)(1u << n);
    return mask;
}
static void zero_invalid(uint8_t *main, uint8_t mask)
{
    for (unsigned n = 0; n < SF_SECTORS_PER_PAGE; ++n)
        if (!(mask & (1u << n))) memset(main + n * FWLAB_FTL_SCALE_LBA_BYTES, 0, FWLAB_FTL_SCALE_LBA_BYTES);
}
static bool subspan(const struct sf_parent *p, struct fwlab_block_request_v0 *r, uint32_t lbas)
{
    uint64_t offset = (uint64_t)p->request.buffer_span.offset + (uint64_t)p->completed_lbas * FWLAB_FTL_SCALE_LBA_BYTES;
    *r = p->request; r->lba += p->completed_lbas; r->lba_count = lbas;
    if (offset > UINT32_MAX) return false;
    r->buffer_span.offset = (uint32_t)offset; r->buffer_span.length = lbas * FWLAB_FTL_SCALE_LBA_BYTES;
    return fwlab_controller_buffer_span_v0_valid_for_lease(&r->buffer_span, &r->buffer,
        r->operation == FWLAB_BLOCK_V0_WRITE ? FWLAB_CONTROLLER_BUFFER_V0_READ : FWLAB_CONTROLLER_BUFFER_V0_WRITE) != 0;
}

enum fwlab_spine_result_v0 sf_window_prepare(struct fwlab_ftl_scale *f, const struct sf_parent *p)
{
    struct sf_work *w = &f->work;
    struct fwlab_block_request_v0 r;
    uint64_t lba = p->request.lba + p->completed_lbas;
    uint32_t remaining = p->request.lba_count - p->completed_lbas;
    uint32_t ppb = f->config.geometry.pages_per_block, pages, lbas, first_lpn;
    bool partial;
    if (!f->window.main || !f->window.oob) return FWLAB_SPINE_V0_INVALID;
    if (p->request.operation == FWLAB_BLOCK_V0_FLUSH) {
        w->request = p->request; w->kind = SF_WORK_HOST; w->phase = SF_W_HOST_PAGE;
        w->page_count = w->page_index = 0; w->effect_seen = 0; return FWLAB_SPINE_V0_OK;
    }
    if (!remaining) return FWLAB_SPINE_V0_INVALID;
    first_lpn = (uint32_t)(lba / SF_SECTORS_PER_PAGE);
    partial = lba % SF_SECTORS_PER_PAGE != 0 || remaining < SF_SECTORS_PER_PAGE;
    lbas = partial ? SF_SECTORS_PER_PAGE - (uint32_t)(lba % SF_SECTORS_PER_PAGE) : remaining;
    if (lbas > remaining) lbas = remaining;
    pages = partial ? 1u : lbas / SF_SECTORS_PER_PAGE;
    if (pages > SF_MAX_DELTAS) pages = SF_MAX_DELTAS;
    f->window.zero_read = 0;
    if (p->request.operation == FWLAB_BLOCK_V0_WRITE) {
        enum fwlab_spine_result_v0 result;
        uint32_t tail;
        if (p->host_sequence > f->config.host_sequence_limit ||
            f->record_sequence >= f->config.record_sequence_limit) return FWLAB_SPINE_V0_NO_CAPACITY;
        if (!sf_record_space(f, 1)) {
            result = sf_checkpoint_start(f);
            return result == FWLAB_SPINE_V0_OK ? FWLAB_SPINE_V0_IN_PROGRESS : result;
        }
        tail = f->host_head == SF_NONE ? 0 : ppb - f->blocks[f->host_head].disk.allocation_end;
        if (!tail) {
            result = sf_space_start(f, pages < 3u ? pages : 3u, false);
            return result == FWLAB_SPINE_V0_OK ? FWLAB_SPINE_V0_IN_PROGRESS : result;
        }
        if (pages > tail) pages = tail;
        f->window.first_ppa = f->host_head * ppb + f->blocks[f->host_head].disk.allocation_end;
        f->window.block_uid = f->blocks[f->host_head].disk.block_uid;
    } else if (p->request.operation == FWLAB_BLOCK_V0_READ) {
        const struct sf_map_entry *first = &f->map[first_lpn];
        uint32_t count = 0;
        if (!map_valid(f, first)) { sf_fail(f, SF_FAULT_STATE); return FWLAB_SPINE_V0_QUARANTINED; }
        f->window.zero_read = (uint8_t)(first->state != SF_VALUE);
        f->window.first_ppa = first->ppa;
        f->window.block_uid = f->window.zero_read ? 0 : f->blocks[first->ppa / ppb].disk.block_uid;
        while (count < pages) {
            const struct sf_map_entry *e = &f->map[first_lpn + count];
            if (!map_valid(f, e)) { sf_fail(f, SF_FAULT_STATE); return FWLAB_SPINE_V0_QUARANTINED; }
            if (f->window.zero_read) { if (e->state == SF_VALUE) break; }
            else if (e->state != SF_VALUE || e->ppa != first->ppa + count ||
                     e->ppa / ppb != first->ppa / ppb || e->erase_generation != first->erase_generation) break;
            ++count;
        }
        pages = count;
    } else return FWLAB_SPINE_V0_INVALID;
    if (!partial) lbas = pages * SF_SECTORS_PER_PAGE;
    if (!pages || !subspan(p, &r, lbas)) return FWLAB_SPINE_V0_INVALID;
    memset(&w->record, 0, sizeof(w->record));
    for (uint32_t n = 0; n < pages; ++n) {
        struct sf_delta *d = &w->record.delta[n];
        d->lpn = first_lpn + n; d->before = f->map[d->lpn];
        if (!map_valid(f, &d->before)) { sf_fail(f, SF_FAULT_STATE); return FWLAB_SPINE_V0_QUARANTINED; }
        if (r.operation == FWLAB_BLOCK_V0_WRITE) {
            d->after.ppa = f->window.first_ppa + n;
            d->after.erase_generation = f->blocks[f->host_head].disk.erase_generation;
            d->after.data_uid = f->window.block_uid * ppb + d->after.ppa % ppb;
            d->after.state = SF_VALUE; d->after.valid_mask = d->before.valid_mask | mask_for(&r, d->lpn);
        }
    }
    if (r.operation == FWLAB_BLOCK_V0_WRITE) {
        const struct sf_map_entry *before = &w->record.delta[0].before;
        uint64_t credit = 3u + (uint64_t)(partial && before->state == SF_VALUE &&
            (before->valid_mask & (uint8_t)~mask_for(&r, first_lpn)) != 0);
        void *destination = partial ? (void *)w->host_bytes : (void *)&f->window.main[0][0];
        if (!sf_child_credit(f, credit)) return FWLAB_SPINE_V0_NO_CAPACITY;
        if (f->controller_buffer.ops->read(f->controller_buffer.context, &r.buffer, &r.buffer_span,
                                          destination, r.buffer_span.length) != FWLAB_CONTROLLER_BUFFER_V0_OK)
            return FWLAB_SPINE_V0_INVALID;
        w->record.kind = SF_MAP_WINDOW; w->record.count = (uint16_t)pages;
        w->record.block = f->host_head; w->record.block_uid = f->window.block_uid;
        w->record.durable_frontier = p->completed_lbas + lbas == p->request.lba_count ? p->host_sequence : p->base_frontier;
        f->blocks[f->host_head].reserved_pages = (uint16_t)pages;
    } else if (!f->window.zero_read && !sf_child_credit(f, 1)) return FWLAB_SPINE_V0_NO_CAPACITY;
    w->request = r; w->first_lpn = first_lpn; w->page_count = pages; w->page_index = 0;
    w->kind = SF_WORK_HOST; w->effect_seen = 0;
    w->phase = r.operation == FWLAB_BLOCK_V0_READ ? SF_W_WINDOW_READ_START : SF_W_WINDOW_BUILD;
    return FWLAB_SPINE_V0_OK;
}

static bool issue_program(struct fwlab_ftl_scale *f)
{
    struct sf_work *w = &f->work;
    for (uint32_t n = 0; n < w->page_count; ++n) {
        struct sf_delta *d = &w->record.delta[n];
        if (memcmp(&d->before, &f->map[d->lpn], sizeof(d->before))) { sf_fail(f, SF_FAULT_STATE); return true; }
        sf_data_oob_encode(f, d->lpn, &d->after, f->window.block_uid, f->window.main[n], f->window.oob[n]);
    }
    if (sf_io_program_group_start(f, f->window.first_ppa, w->page_count) != FWLAB_SPINE_V0_OK) sf_fail(f, SF_FAULT_IO);
    else w->phase = SF_W_HOST_PROGRAM_WAIT;
    return true;
}
static void merge_partial(struct fwlab_ftl_scale *f)
{
    struct sf_work *w = &f->work;
    uint32_t offset = (uint32_t)(w->request.lba % SF_SECTORS_PER_PAGE) * FWLAB_FTL_SCALE_LBA_BYTES;
    zero_invalid(f->window.main[0], w->record.delta[0].before.valid_mask);
    memcpy(f->window.main[0] + offset, w->host_bytes, w->request.buffer_span.length);
}
static bool publish_read(struct fwlab_ftl_scale *f)
{
    struct sf_work *w = &f->work;
    uint32_t offset = (uint32_t)(w->request.lba % SF_SECTORS_PER_PAGE) * FWLAB_FTL_SCALE_LBA_BYTES;
    if (f->parent.cancelled) { sf_parent_fail(f, FWLAB_NFC_REASON_CANCELLED); return true; }
    for (uint32_t n = 0; n < w->page_count; ++n) {
        const struct sf_delta *d = &w->record.delta[n];
        if (memcmp(&d->before, &f->map[d->lpn], sizeof(d->before))) { sf_fail(f, SF_FAULT_STATE); return true; }
        if (!f->window.zero_read) {
            const struct sf_io_facts *facts = &f->io.page_facts[n];
            if ((facts->available & FWLAB_NFC_PAGE_V2_FACT_GENERATION) == 0 ||
                facts->final_erase_generation != d->before.erase_generation ||
                f->blocks[d->before.ppa / f->config.geometry.pages_per_block].disk.block_uid != f->window.block_uid ||
                !sf_data_oob_validate(f, d->lpn, &d->before, f->window.block_uid,
                                      f->window.main[n], f->window.oob[n])) {
                sf_parent_fail(f, SF_FAULT_IO); return true;
            }
        }
    }
    for (uint32_t n = 0; n < w->page_count; ++n) zero_invalid(f->window.main[n], w->record.delta[n].before.valid_mask);
    if (f->controller_buffer.ops->write(f->controller_buffer.context, &w->request.buffer,
        &w->request.buffer_span, &f->window.main[0][0] + offset, w->request.buffer_span.length) != FWLAB_CONTROLLER_BUFFER_V0_OK)
        sf_parent_fail(f, SF_FAULT_STATE);
    else sf_parent_group_success(f);
    return true;
}
bool sf_window_step(struct fwlab_ftl_scale *f)
{
    struct sf_work *w = &f->work; struct sf_io_result io;
    if (sf_meta_busy(f)) return false;
    if (f->parent.cancelled && !w->effect_seen && sf_io_idle(f)) {
        sf_parent_fail(f, FWLAB_NFC_REASON_CANCELLED); return true;
    }
    if (w->request.operation == FWLAB_BLOCK_V0_FLUSH) { sf_parent_group_success(f); return true; }
    if (w->phase == SF_W_WINDOW_BUILD) {
        struct sf_delta *d = &w->record.delta[0];
        if (w->request.lba % SF_SECTORS_PER_PAGE || w->request.lba_count < SF_SECTORS_PER_PAGE) {
            memset(f->window.main[0], 0, SF_PAGE_BYTES);
            if (d->before.state == SF_VALUE && (d->before.valid_mask & (uint8_t)~mask_for(&w->request, d->lpn))) {
                if (sf_io_read_start(f, d->before.ppa, 0) != FWLAB_SPINE_V0_OK) sf_fail(f, SF_FAULT_IO);
                else w->phase = SF_W_WINDOW_RMW_WAIT;
                return true;
            }
            merge_partial(f);
        }
        return issue_program(f);
    }
    if (w->phase == SF_W_WINDOW_RMW_WAIT) {
        const struct sf_delta *d = &w->record.delta[0];
        if (!sf_io_take(f, &io)) return false;
        if (f->parent.cancelled) { sf_parent_fail(f, FWLAB_NFC_REASON_CANCELLED); return true; }
        if (io.result != FWLAB_SPINE_V0_OK || !io.read_valid || io.count != 1 || io.ppa != d->before.ppa ||
            io.completion.final_erase_generation != d->before.erase_generation ||
            !sf_data_oob_validate(f, d->lpn, &d->before,
                f->blocks[d->before.ppa / f->config.geometry.pages_per_block].disk.block_uid, f->io.main[0], f->io.oob[0])) {
            sf_parent_fail(f, SF_FAULT_IO); return true;
        }
        memcpy(f->window.main[0], f->io.main[0], SF_PAGE_BYTES); merge_partial(f); return issue_program(f);
    }
    if (w->phase == SF_W_HOST_PROGRAM_WAIT) {
        if (!sf_io_take(f, &io)) return false;
        if (io.effect == SF_EFFECT_NONE) { w->effect_seen = 0; sf_parent_fail(f, SF_FAULT_IO); return true; }
        if (io.result != FWLAB_SPINE_V0_OK || io.effect != SF_EFFECT_COMPLETE || io.kind != SF_IO_PROGRAM ||
            io.ppa != f->window.first_ppa || io.count != w->page_count) { sf_fail(f, SF_FAULT_IO); return true; }
        for (uint32_t n = 0; n < w->page_count; ++n)
            if (f->io.page_facts[n].final_erase_generation != w->record.delta[n].after.erase_generation) {
                sf_fail(f, SF_FAULT_IO); return true;
            }
        ++f->window.program_groups;
        if (f->window.max_program_pages < w->page_count) f->window.max_program_pages = (uint16_t)w->page_count;
        w->effect_seen = 1; w->page_index = w->page_count; w->phase = SF_W_HOST_PAGE;
        return true;
    }
    if (w->phase == SF_W_HOST_PAGE) {
        if (sf_journal_start(f, &w->record) != FWLAB_SPINE_V0_OK) sf_fail(f, SF_FAULT_METADATA);
        else w->phase = SF_W_HOST_MAP_WAIT;
        return true;
    }
    if (w->phase == SF_W_HOST_MAP_WAIT) {
        if (sf_meta_result(f) != FWLAB_SPINE_V0_OK) sf_fail(f, SF_FAULT_METADATA);
        else sf_parent_group_success(f);
        return true;
    }
    if (w->phase == SF_W_WINDOW_READ_START) {
        if (f->window.zero_read) {
            memset(f->window.main, 0, (size_t)w->page_count * SF_PAGE_BYTES); return publish_read(f);
        }
        if (sf_io_read_group_start(f, f->window.first_ppa, w->page_count) != FWLAB_SPINE_V0_OK) sf_fail(f, SF_FAULT_IO);
        else w->phase = SF_W_WINDOW_READ_WAIT;
        return true;
    }
    if (w->phase == SF_W_WINDOW_READ_WAIT) {
        if (!sf_io_take(f, &io)) return false;
        if (f->parent.cancelled) { sf_parent_fail(f, FWLAB_NFC_REASON_CANCELLED); return true; }
        if (io.result != FWLAB_SPINE_V0_OK || !io.read_valid || io.kind != SF_IO_READ ||
            io.ppa != f->window.first_ppa || io.count != w->page_count) { sf_parent_fail(f, SF_FAULT_IO); return true; }
        ++f->window.read_groups;
        if (f->window.max_read_pages < w->page_count) f->window.max_read_pages = (uint16_t)w->page_count;
        return publish_read(f);
    }
    sf_fail(f, SF_FAULT_STATE); return true;
}
