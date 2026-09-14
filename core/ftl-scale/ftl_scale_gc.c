/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "ftl_scale_internal.h"
#include <string.h>

bool sf_record_space(const struct fwlab_ftl_scale *f, uint32_t records)
{
    return f->journal_next <= f->root.layout.journal_slots &&
        records <= f->root.layout.journal_slots - f->journal_next &&
        f->record_sequence <= f->config.record_sequence_limit &&
        records <= f->config.record_sequence_limit - f->record_sequence;
}

bool sf_child_credit(const struct fwlab_ftl_scale *f, uint64_t children)
{
    return children && f->io.next_uid &&
        f->io.next_uid <= f->config.nfc_operation_uid_limit &&
        children - 1u <= f->config.nfc_operation_uid_limit - f->io.next_uid;
}

static bool journal(struct fwlab_ftl_scale *f, uint32_t phase)
{
    if (sf_journal_start(f, &f->work.record) != FWLAB_SPINE_V0_OK) {
        sf_fail(f, SF_FAULT_METADATA);
        return true;
    }
    f->work.phase = phase;
    return true;
}

static void record_block(struct fwlab_ftl_scale *f, uint8_t kind, uint32_t b)
{
    memset(&f->work.record, 0, sizeof(f->work.record));
    f->work.record.kind = kind;
    f->work.record.block = b;
    f->work.record.block_uid = f->blocks[b].disk.block_uid;
    f->work.record.erase_generation = f->blocks[b].disk.erase_generation;
    f->work.record.durable_frontier = f->durable_frontier;
}

enum fwlab_spine_result_v0 sf_head_open_start(struct fwlab_ftl_scale *f, uint32_t domain)
{
    struct sf_head_domain_index *index;
    enum fwlab_spine_result_v0 result;
    uint32_t block, ppb;
    if (!f || f->disk_format != SF_MULTIHEAD_FORMAT_VERSION || !f->ready ||
        f->quarantined || f->admission_closed || domain >= f->heads.count ||
        !sf_maintenance_allowed(f) || sf_meta_busy(f) || !sf_io_idle(f) ||
        f->heads.gc_destination != SF_NONE) return FWLAB_SPINE_V0_WRONG_STATE;
    if (sf_head_tail(f, domain)) return FWLAB_SPINE_V0_OK;
    index = &f->heads.domain[domain]; ppb = f->config.geometry.pages_per_block;
    if (index->head != SF_NONE) return FWLAB_SPINE_V0_WRONG_STATE;
    if (!index->free_count || f->free_count <= 1 || !f->next_block_uid ||
        f->next_block_uid >= UINT64_MAX / ppb || !sf_child_credit(f, 2))
        return FWLAB_SPINE_V0_NO_CAPACITY;
    if (!sf_record_space(f, 1)) {
        result = sf_checkpoint_start(f);
        return result == FWLAB_SPINE_V0_OK ? FWLAB_SPINE_V0_IN_PROGRESS : result;
    }
    block = f->free_heap[index->heap_offset];
    record_block(f, SF_OPEN_HOST, block);
    f->work.record.block_uid = f->next_block_uid;
    result = sf_journal_start(f, &f->work.record);
    if (result != FWLAB_SPINE_V0_OK) return result;
    f->work.destination_block = block;
    f->work.kind = SF_WORK_SPACE; f->work.phase = SF_W_OPEN_WAIT;
    return FWLAB_SPINE_V0_IN_PROGRESS;
}

enum fwlab_spine_result_v0 sf_space_start(struct fwlab_ftl_scale *f,
                                         uint32_t needed, bool force_gc)
{
    enum fwlab_spine_result_v0 result;
    uint32_t phase = SF_W_SPACE;
    uint32_t smallest, ppb, head_count;
    if (!f || !f->ready || f->quarantined || f->admission_closed ||
        !sf_maintenance_allowed(f) || sf_meta_busy(f) || !sf_io_idle(f) ||
        !needed || needed > SF_MAX_HOST_DELTAS)
        return FWLAB_SPINE_V0_WRONG_STATE;
    ppb = f->config.geometry.pages_per_block;
    head_count = f->disk_format == SF_MULTIHEAD_FORMAT_VERSION ? f->heads.count : 1u;
    if (!f->next_block_uid || f->next_block_uid >= UINT64_MAX / ppb)
        return FWLAB_SPINE_V0_NO_CAPACITY;
    if (force_gc) {
        smallest = f->victim_count ? f->blocks[f->victim_heap[0]].live_pages : ppb;
        for (uint32_t d = 0; d < head_count; ++d) {
            uint32_t head = f->disk_format == SF_MULTIHEAD_FORMAT_VERSION ?
                f->heads.domain[d].head : f->host_head;
            if (head != SF_NONE && f->blocks[head].live_pages < smallest)
                smallest = f->blocks[head].live_pages;
        }
        if (smallest > 61 || smallest > ppb - needed)
            return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    /* Keep the old conservative scalar budget, plus both rails for each
     * additional head CLOSE. No per-domain NAND-space reserve is consumed. */
    if (!sf_child_credit(f, 4u * 61u + 21u + 2u * (head_count - 1u)))
        return FWLAB_SPINE_V0_NO_CAPACITY;
    if (!sf_record_space(f, head_count + 4u)) {
        result = sf_checkpoint_start(f);
        if (result != FWLAB_SPINE_V0_OK) return result;
        phase = SF_W_WAIT_CP;
    }
    f->work.kind = SF_WORK_SPACE;
    f->work.phase = phase;
    f->work.needed_pages = needed;
    f->work.force_gc = (uint8_t)force_gc;
    return FWLAB_SPINE_V0_OK;
}

static bool start_erase(struct fwlab_ftl_scale *f, uint32_t block)
{
    if (f->blocks[block].live_pages ||
        f->blocks[block].disk.erase_generation == UINT16_MAX) {
        sf_fail(f, SF_FAULT_STATE);
        return true;
    }
    f->work.reclaim_block = block;
    record_block(f, SF_ERASE_INTENT, block);
    return journal(f, SF_W_ERASE_INTENT_WAIT);
}

static bool prepare_space(struct fwlab_ftl_scale *f)
{
    struct sf_work *w = &f->work;
    uint32_t b, ppb = f->config.geometry.pages_per_block;
    uint32_t head = sf_head_first(f);
    if (!w->force_gc && f->disk_format == SF_MULTIHEAD_FORMAT_VERSION) {
        for (uint32_t d = 0; d < f->heads.count; ++d) {
            if (sf_head_tail(f, d) < w->needed_pages) continue;
            w->kind = SF_WORK_NONE;
            w->phase = SF_W_IDLE;
            return true;
        }
    }
    if (head != SF_NONE) {
        if (!w->force_gc && f->disk_format != SF_MULTIHEAD_FORMAT_VERSION &&
            ppb - f->blocks[head].disk.allocation_end >= w->needed_pages) {
            w->kind = SF_WORK_NONE;
            w->phase = SF_W_IDLE;
            return true;
        }
        /* All heads become eligible before global victim selection. The
         * caller reaches space maintenance only between unreserved waves. */
        record_block(f, SF_CLOSE, head);
        return journal(f, SF_W_CLOSE_WAIT);
    }
    if (sf_next_reclaim_pending(f, &b)) return start_erase(f, b);
    if (!w->force_gc && f->free_count > 1) {
        b = sf_free_best(f);
        record_block(f, SF_OPEN_HOST, b);
        w->record.block_uid = f->next_block_uid;
        w->destination_block = b;
        return journal(f, SF_W_OPEN_WAIT);
    }
    if (!f->victim_count) {
        /* No eligible victim is a permanent space failure, not endless BP. */
        sf_fail(f, SF_FAULT_STATE);
        return true;
    }
    b = f->victim_heap[0];
    if (!f->blocks[b].live_pages) return start_erase(f, b);
    if (!f->free_count || f->blocks[b].live_pages > 61 ||
        f->blocks[b].live_pages > ppb - w->needed_pages) {
        sf_fail(f, SF_FAULT_STATE);
        return true;
    }
    w->source_block = b;
    w->destination_block = sf_free_best(f);
    w->source_page = w->moved = 0;
    w->live_count = f->blocks[b].live_pages;
    w->kind = SF_WORK_GC;
    record_block(f, SF_OPEN_GC_DEST, w->destination_block);
    w->record.block_uid = f->next_block_uid;
    return journal(f, SF_W_OPEN_WAIT);
}

bool sf_gc_step(struct fwlab_ftl_scale *f)
{
    struct sf_work *w = &f->work;
    struct sf_io_result io;
    uint32_t ppb = f->config.geometry.pages_per_block;
    if (sf_meta_busy(f)) return false;
    if (w->phase == SF_W_WAIT_CP || w->phase == SF_W_CLOSE_WAIT) {
        if (sf_meta_result(f) != FWLAB_SPINE_V0_OK) sf_fail(f, SF_FAULT_METADATA);
        else w->phase = SF_W_SPACE;
        return true;
    }
    if (w->phase == SF_W_SPACE) return prepare_space(f);
    if (w->phase == SF_W_OPEN_WAIT) {
        if (sf_meta_result(f) != FWLAB_SPINE_V0_OK) {
            sf_fail(f, SF_FAULT_METADATA);
            return true;
        }
        if (w->kind != SF_WORK_GC) {
            w->kind = SF_WORK_NONE;
            w->phase = SF_W_IDLE;
            return true;
        }
        record_block(f, SF_GC_COMMIT, w->source_block);
        w->record.other_block = w->destination_block;
        w->record.other_block_uid = f->blocks[w->destination_block].disk.block_uid;
        w->record.count = (uint16_t)w->live_count;
        w->phase = SF_W_GC_READ;
        return true;
    }
    if (w->phase == SF_W_GC_READ) {
        uint32_t ppa;
        if (w->moved == w->live_count) return journal(f, SF_W_GC_COMMIT_WAIT);
        while (w->source_page < ppb &&
               !sf_validity_get(f, w->source_block * ppb + w->source_page))
            ++w->source_page;
        if (w->source_page >= ppb) { sf_fail(f, SF_FAULT_STATE); return true; }
        ppa = w->source_block * ppb + w->source_page;
        if (sf_io_read_start(f, ppa, 0) != FWLAB_SPINE_V0_OK)
            sf_fail(f, SF_FAULT_IO);
        else w->phase = SF_W_GC_READ_WAIT;
        return true;
    }
    if (w->phase == SF_W_GC_READ_WAIT) {
        struct sf_delta *d = &w->record.delta[w->moved];
        uint32_t lpn;
        if (!sf_io_take(f, &io)) return false;
        if (io.result != FWLAB_SPINE_V0_OK || !io.read_valid ||
            !sf_data_oob_lpn(f->io.oob[0], &lpn) || lpn >= f->root.layout.lpn_count ||
            f->map[lpn].ppa != w->source_block * ppb + w->source_page ||
            !sf_data_oob_validate(f, lpn, &f->map[lpn],
                f->blocks[w->source_block].disk.block_uid, f->io.main[0], f->io.oob[0])) {
            sf_fail(f, SF_FAULT_IO);
            return true;
        }
        memset(d, 0, sizeof(*d));
        d->lpn = lpn;
        d->before = f->map[lpn];
        d->after = d->before;
        d->after.ppa = w->destination_block * ppb + w->moved;
        d->after.erase_generation = f->blocks[w->destination_block].disk.erase_generation;
        d->after.data_uid = f->blocks[w->destination_block].disk.block_uid * ppb + w->moved;
        sf_data_oob_encode(f, lpn, &d->after,
            f->blocks[w->destination_block].disk.block_uid, f->io.main[0], f->io.oob[0]);
        if (sf_io_program_start(f, d->after.ppa, 0) != FWLAB_SPINE_V0_OK)
            sf_fail(f, SF_FAULT_IO);
        else w->phase = SF_W_GC_PROGRAM_WAIT;
        return true;
    }
    if (w->phase == SF_W_GC_PROGRAM_WAIT) {
        if (!sf_io_take(f, &io)) return false;
        if (io.result != FWLAB_SPINE_V0_OK) sf_fail(f, SF_FAULT_IO);
        else { ++w->moved; ++w->source_page; w->phase = SF_W_GC_READ; }
        return true;
    }
    if (w->phase == SF_W_GC_COMMIT_WAIT) {
        if (sf_meta_result(f) != FWLAB_SPINE_V0_OK || f->blocks[w->source_block].live_pages) {
            sf_fail(f, SF_FAULT_METADATA);
            return true;
        }
        return start_erase(f, w->source_block);
    }
    if (w->phase == SF_W_ERASE_INTENT_WAIT) {
        if (sf_meta_result(f) != FWLAB_SPINE_V0_OK ||
            sf_io_erase_start(f, w->reclaim_block) != FWLAB_SPINE_V0_OK)
            sf_fail(f, SF_FAULT_IO);
        else w->phase = SF_W_ERASE_WAIT;
        return true;
    }
    if (w->phase == SF_W_ERASE_WAIT) {
        if (!sf_io_take(f, &io)) return false;
        if (io.result != FWLAB_SPINE_V0_OK) { sf_fail(f, SF_FAULT_IO); return true; }
        record_block(f, SF_ERASE_DONE, w->reclaim_block);
        w->record.intent_sequence = f->erase_intent_sequence;
        w->record.final_erase_generation = io.completion.final_erase_generation;
        w->record.health = io.completion.block_health;
        return journal(f, SF_W_ERASE_DONE_WAIT);
    }
    if (w->phase == SF_W_ERASE_DONE_WAIT) {
        if (sf_meta_result(f) != FWLAB_SPINE_V0_OK) sf_fail(f, SF_FAULT_METADATA);
        else if (w->kind == SF_WORK_GC) {
            w->kind = SF_WORK_NONE;
            w->phase = SF_W_IDLE;
        } else { w->force_gc = 0; w->phase = SF_W_SPACE; }
        return true;
    }
    return false;
}
