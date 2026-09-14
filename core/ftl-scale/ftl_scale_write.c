/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "ftl_scale_internal.h"
#include <stdalign.h>
#include <string.h>

#define SF_WRITE_RUNS 4u
enum write_phase { WRITE_RMW, WRITE_RMW_WAIT, WRITE_DATA, WRITE_MAP,
                   WRITE_MAP_WAIT, WRITE_FINISH, WRITE_ABORT, WRITE_ABORT_WAIT };
enum run_state { RUN_PREPARED, RUN_PENDING, RUN_RESOLVED, RUN_SKIPPED };
struct sf_write_run {
    struct fwlab_block_request_v0 request;
    struct sf_head_span destination;
    struct sf_io io;
    struct sf_record record;
    uint32_t logical_offset;
    uint16_t physical_page_count, mapping_delta_count;
    uint8_t state, accepted, reserved;
    uint8_t main[SF_MAX_DELTAS][SF_PAGE_BYTES];
    uint8_t oob[SF_MAX_DELTAS][SF_OOB_BYTES];
};
struct sf_write_pool {
    uint32_t wave_start_lbas, planned_lbas, accepted_lbas, committed_lbas;
    uint32_t fault, preferred_domain;
    uint16_t rmw_page;
    uint8_t active, phase, run_count, submit_cursor, map_cursor, rmw_run;
    uint8_t accepted_count, stop_unaccepted, data_failed;
    struct sf_write_run run[SF_WRITE_RUNS];
};
_Static_assert(alignof(struct sf_write_pool) <= alignof(max_align_t),
               "ordinary constructor-tail alignment");

size_t sf_write_pool_bytes(void)
{
    size_t a = alignof(max_align_t);
    return (sizeof(struct sf_write_pool) + a - 1u) & ~(a - 1u);
}
bool sf_write_pool_init(struct fwlab_ftl_scale *f, void *memory, size_t size)
{
    if (!f || !memory || size < sf_write_pool_bytes() ||
        (uintptr_t)memory % alignof(max_align_t) || f->writes) return false;
    memset(memory, 0, sf_write_pool_bytes());
    f->writes = memory;
    return true;
}
bool sf_write_pool_busy(const struct fwlab_ftl_scale *f)
{ return f && f->writes && f->writes->active; }
bool sf_write_control_allowed(const struct fwlab_ftl_scale *f)
{
    if (!sf_write_pool_busy(f)) return true;
    return f->writes->phase == WRITE_RMW || f->writes->phase == WRITE_RMW_WAIT ||
           f->writes->phase == WRITE_MAP || f->writes->phase == WRITE_MAP_WAIT;
}
uint32_t sf_write_known_prefix(const struct fwlab_ftl_scale *f)
{ return sf_write_pool_busy(f) ? f->writes->committed_lbas : 0; }

static uint8_t requested_mask(const struct fwlab_block_request_v0 *r, uint32_t lpn)
{
    uint64_t start = (uint64_t)lpn * SF_SECTORS_PER_PAGE;
    uint8_t mask = 0;
    for (unsigned i = 0; i < SF_SECTORS_PER_PAGE; ++i)
        if (start + i >= r->lba && start + i < r->lba + r->lba_count)
            mask |= (uint8_t)(1u << i);
    return mask;
}
static bool needs_rmw(const struct sf_write_run *r, uint32_t page)
{
    const struct sf_delta *d = &r->record.delta[page];
    return d->before.state == SF_VALUE &&
        (d->before.valid_mask & (uint8_t)~requested_mask(&r->request, d->lpn)) != 0;
}
static bool unreserve(struct fwlab_ftl_scale *f, struct sf_write_run *r)
{
    if (!r->reserved) return true;
    if (r->accepted || !sf_head_unreserve(f, &r->destination)) return false;
    r->reserved = 0;
    return true;
}
static bool abandon_unaccepted(struct fwlab_ftl_scale *f)
{
    for (unsigned i = 0; i < f->writes->run_count; ++i)
        if (!f->writes->run[i].accepted && !unreserve(f, &f->writes->run[i]))
            return false;
    return true;
}
static enum fwlab_spine_result_v0 prepare_failed(struct fwlab_ftl_scale *f,
                                                enum fwlab_spine_result_v0 result)
{
    if (!abandon_unaccepted(f)) {
        sf_fail(f, SF_FAULT_STATE);
        return FWLAB_SPINE_V0_QUARANTINED;
    }
    return result;
}

enum fwlab_spine_result_v0 sf_write_parent_prepare(struct fwlab_ftl_scale *f,
                                                  const struct sf_parent *parent)
{
    struct sf_write_pool *p;
    uint32_t domains[SF_WRITE_RUNS], count = 0, excluded = 0, physical, remaining;
    uint32_t preferred, rmw_count = 0;
    uint64_t lba;
    if (!f || !parent || !f->writes || f->disk_format != SF_MULTIHEAD_FORMAT_VERSION ||
        parent->request.operation != FWLAB_BLOCK_V0_WRITE ||
        parent->completed_lbas >= parent->request.lba_count || sf_write_pool_busy(f) ||
        !sf_io_idle(f) || !sf_heads_unreserved(f) || !f->heads.count)
        return FWLAB_SPINE_V0_WRONG_STATE;
    if (parent->host_sequence > f->config.host_sequence_limit)
        return FWLAB_SPINE_V0_NO_CAPACITY;
    p = f->writes;
    remaining = parent->request.lba_count - parent->completed_lbas;
    lba = parent->request.lba + parent->completed_lbas;
    physical = (uint32_t)((lba % SF_SECTORS_PER_PAGE + remaining +
                           SF_SECTORS_PER_PAGE - 1u) / SF_SECTORS_PER_PAGE);
    preferred = p->preferred_domain % f->heads.count;
    /* No final map snapshot/reservation survives maintenance. OPEN one needed
     * head at a time, then replan from the current committed state. */
    while (count < SF_WRITE_RUNS && count < physical) {
        bool open = false;
        uint32_t domain = sf_head_select(f, excluded, preferred, &open);
        if (domain == SF_NONE) break;
        if (open) return sf_head_open_start(f, domain);
        domains[count++] = domain;
        excluded |= UINT32_C(1) << domain;
        preferred = (domain + 1u) % f->heads.count;
    }
    if (!count) {
        enum fwlab_spine_result_v0 result = sf_space_start(f, 1, false);
        return result == FWLAB_SPINE_V0_OK ? FWLAB_SPINE_V0_IN_PROGRESS : result;
    }
    if (!sf_record_space(f, count)) {
        enum fwlab_spine_result_v0 result = sf_checkpoint_start(f);
        return result == FWLAB_SPINE_V0_OK ? FWLAB_SPINE_V0_IN_PROGRESS : result;
    }
    preferred = p->preferred_domain;
    memset(p, 0, offsetof(struct sf_write_pool, run));
    p->preferred_domain = preferred;
    p->wave_start_lbas = parent->completed_lbas;
    for (unsigned i = 0; i < count; ++i) {
        struct sf_write_run *r = &p->run[i];
        uint32_t pages = (physical + count - i - 1u) / (count - i);
        uint32_t tail = sf_head_tail(f, domains[i]), lbas;
        uint64_t offset = (uint64_t)parent->request.buffer_span.offset +
            (uint64_t)(parent->completed_lbas + p->planned_lbas) * FWLAB_FTL_SCALE_LBA_BYTES;
        memset(r, 0, offsetof(struct sf_write_run, main));
        ++p->run_count;
        if (pages > SF_MAX_DELTAS) pages = SF_MAX_DELTAS;
        if (pages > tail) pages = tail;
        if (!pages || offset > UINT32_MAX ||
            !sf_head_reserve(f, domains[i], (uint16_t)pages, &r->destination))
            return prepare_failed(f, FWLAB_SPINE_V0_INVALID);
        r->reserved = 1;
        lbas = pages * SF_SECTORS_PER_PAGE - (uint32_t)(lba % SF_SECTORS_PER_PAGE);
        if (lbas > remaining) lbas = remaining;
        r->request = parent->request;
        r->request.lba = lba; r->request.lba_count = lbas;
        r->request.buffer_span.offset = (uint32_t)offset;
        r->request.buffer_span.length = lbas * FWLAB_FTL_SCALE_LBA_BYTES;
        if (!fwlab_controller_buffer_span_v0_valid_for_lease(&r->request.buffer_span,
                &r->request.buffer, FWLAB_CONTROLLER_BUFFER_V0_READ))
            return prepare_failed(f, FWLAB_SPINE_V0_INVALID);
        r->logical_offset = parent->completed_lbas + p->planned_lbas;
        r->physical_page_count = (uint16_t)pages;
        r->mapping_delta_count = (uint16_t)pages;
        r->record.kind = SF_MAP_WINDOW;
        r->record.count = r->mapping_delta_count;
        r->record.block = r->destination.block;
        r->record.block_uid = r->destination.block_uid;
        r->record.erase_generation = r->destination.erase_generation;
        for (uint32_t page = 0; page < pages; ++page) {
            struct sf_delta *d = &r->record.delta[page];
            d->lpn = (uint32_t)(lba / SF_SECTORS_PER_PAGE) + page;
            d->before = f->map[d->lpn];
            if (!sf_read_map_valid(f, &d->before))
                return prepare_failed(f, FWLAB_SPINE_V0_INVALID);
            d->after.ppa = r->destination.first_ppa + page;
            d->after.erase_generation = r->destination.erase_generation;
            d->after.data_uid = r->destination.block_uid * f->config.geometry.pages_per_block +
                d->after.ppa % f->config.geometry.pages_per_block;
            d->after.valid_mask = d->before.valid_mask | requested_mask(&r->request, d->lpn);
            d->after.state = SF_VALUE;
            rmw_count += needs_rmw(r, page);
        }
        p->planned_lbas += lbas;
        lba += lbas; remaining -= lbas; physical -= pages;
    }
    if (!sf_child_credit(f, (uint64_t)p->run_count * 3u + rmw_count))
        return prepare_failed(f, FWLAB_SPINE_V0_NO_CAPACITY);
    for (unsigned i = 0; i < p->run_count; ++i) {
        struct sf_write_run *r = &p->run[i];
        memset(r->main, 0, (size_t)r->physical_page_count * SF_PAGE_BYTES);
        if (f->controller_buffer.ops->read(f->controller_buffer.context,
                &r->request.buffer, &r->request.buffer_span,
                &r->main[0][0] + (r->request.lba % SF_SECTORS_PER_PAGE) * FWLAB_FTL_SCALE_LBA_BYTES,
                r->request.buffer_span.length) != FWLAB_CONTROLLER_BUFFER_V0_OK)
            return prepare_failed(f, FWLAB_SPINE_V0_INVALID);
    }
    p->active = 1; p->phase = WRITE_RMW;
    f->work.request = parent->request;
    f->work.request.lba += parent->completed_lbas;
    f->work.request.lba_count = p->planned_lbas;
    f->work.request.buffer_span.offset += parent->completed_lbas * FWLAB_FTL_SCALE_LBA_BYTES;
    f->work.request.buffer_span.length = p->planned_lbas * FWLAB_FTL_SCALE_LBA_BYTES;
    f->work.kind = SF_WORK_HOST; f->work.phase = SF_W_HOST_PAGE;
    f->work.effect_seen = 0;
    return FWLAB_SPINE_V0_OK;
}

static bool fail_unprogrammed(struct fwlab_ftl_scale *f, uint32_t fault)
{
    if (!abandon_unaccepted(f)) sf_fail(f, SF_FAULT_STATE);
    else {
        f->writes->active = 0;
        f->work.effect_seen = 0;
        sf_parent_fail(f, fault);
    }
    return true;
}
static bool rmw_step(struct fwlab_ftl_scale *f)
{
    struct sf_write_pool *p = f->writes;
    if (p->phase == WRITE_RMW_WAIT) {
        struct sf_write_run *r = &p->run[p->rmw_run];
        const struct sf_delta *d = &r->record.delta[p->rmw_page];
        struct sf_io_result io;
        if (f->io.phase == SF_IO_DONE && f->io.lower_owned) { sf_fail(f, SF_FAULT_IO); return true; }
        if (!sf_io_take(f, &io)) return false;
        if (f->parent.cancelled || f->admission_closed)
            return fail_unprogrammed(f, FWLAB_NFC_REASON_CANCELLED);
        if (io.result != FWLAB_SPINE_V0_OK || !io.read_valid || io.kind != SF_IO_READ ||
            io.count != 1 || io.ppa != d->before.ppa ||
            !sf_read_page_valid(f, d->lpn, &d->before,
                f->blocks[d->before.ppa / f->config.geometry.pages_per_block].disk.block_uid,
                &io.completion, f->io.main[0], f->io.oob[0]))
            return fail_unprogrammed(f, SF_FAULT_IO);
        uint8_t preserve = d->before.valid_mask & (uint8_t)~requested_mask(&r->request, d->lpn);
        for (unsigned sector = 0; sector < SF_SECTORS_PER_PAGE; ++sector)
            if (preserve & (1u << sector))
                memcpy(r->main[p->rmw_page] + sector * FWLAB_FTL_SCALE_LBA_BYTES,
                       f->io.main[0] + sector * FWLAB_FTL_SCALE_LBA_BYTES, FWLAB_FTL_SCALE_LBA_BYTES);
        ++p->rmw_page; p->phase = WRITE_RMW;
        return true;
    }
    if (f->parent.cancelled || f->admission_closed)
        return fail_unprogrammed(f, FWLAB_NFC_REASON_CANCELLED);
    while (p->rmw_run < p->run_count) {
        struct sf_write_run *r = &p->run[p->rmw_run];
        while (p->rmw_page < r->physical_page_count) {
            if (needs_rmw(r, p->rmw_page)) {
                if (sf_io_read_start(f, r->record.delta[p->rmw_page].before.ppa, 0) != FWLAB_SPINE_V0_OK)
                    return fail_unprogrammed(f, SF_FAULT_IO);
                p->phase = WRITE_RMW_WAIT;
                return true;
            }
            ++p->rmw_page;
        }
        ++p->rmw_run; p->rmw_page = 0;
    }
    for (unsigned i = 0; i < p->run_count; ++i) {
        struct sf_write_run *r = &p->run[i];
        for (uint32_t page = 0; page < r->physical_page_count; ++page) {
            const struct sf_delta *d = &r->record.delta[page];
            if (memcmp(&d->before, &f->map[d->lpn], sizeof(d->before)))
                return fail_unprogrammed(f, SF_FAULT_STATE);
            sf_data_oob_encode(f, d->lpn, &d->after, r->destination.block_uid, r->main[page], r->oob[page]);
        }
    }
    /* No DATA UID exists until all RMW reads have been resolved. */
    p->phase = WRITE_DATA;
    return true;
}
static void data_failure(struct sf_write_pool *p, uint32_t fault)
{
    p->stop_unaccepted = 1; p->data_failed = 1;
    if (!p->fault) p->fault = fault;
}
static bool program_valid(const struct sf_write_run *r)
{
    const struct sf_io_result *io = &r->io.result;
    if (io->result != FWLAB_SPINE_V0_OK || io->effect != SF_EFFECT_COMPLETE ||
        io->kind != SF_IO_PROGRAM || io->ppa != r->destination.first_ppa ||
        io->count != r->physical_page_count) return false;
    for (uint32_t page = 0; page < r->physical_page_count; ++page)
        if (!(r->io.page_facts[page].available & FWLAB_NFC_PAGE_V2_FACT_GENERATION) ||
            r->io.page_facts[page].base_erase_generation != r->destination.erase_generation ||
            r->io.page_facts[page].final_erase_generation != r->destination.erase_generation)
            return false;
    return true;
}
static bool data_step(struct fwlab_ftl_scale *f)
{
    struct sf_write_pool *p = f->writes;
    bool advanced = false, pending = false;
    if (f->parent.cancelled || f->admission_closed) p->stop_unaccepted = 1;
    /* Only this ordered fill pass may submit; collectors never bypass a BP UID. */
    while (p->submit_cursor < p->run_count) {
        struct sf_write_run *r = &p->run[p->submit_cursor];
        if (r->state == RUN_PREPARED && !p->stop_unaccepted) {
            if (sf_page_start_io(f, &r->io, r->destination.first_ppa, 0, SF_IO_PROGRAM,
                    r->physical_page_count, true, &r->main[0][0], &r->oob[0][0], true) != FWLAB_SPINE_V0_OK)
                data_failure(p, SF_FAULT_IO);
            else r->state = RUN_PENDING;
            advanced = true;
        }
        if (r->state == RUN_PENDING) {
            advanced = sf_page_step_io(f, &r->io, p->stop_unaccepted != 0, false) || advanced;
            if (r->io.phase == SF_IO_SUBMIT_FIRST) break;
            if (r->io.phase == SF_IO_WAIT_FIRST) {
                r->accepted = 1; ++p->accepted_count;
                p->accepted_lbas += r->request.lba_count;
                f->work.effect_seen = 1;
            } else if (r->io.phase != SF_IO_DONE || r->io.lower_owned || r->io.result.effect != SF_EFFECT_NONE) {
                sf_fail(f, SF_FAULT_IO); return true;
            } else if (!p->stop_unaccepted) data_failure(p, SF_FAULT_IO);
        }
        if (!r->accepted) {
            if (!unreserve(f, r)) { sf_fail(f, SF_FAULT_STATE); return true; }
            r->state = RUN_SKIPPED;
            r->io.phase = SF_IO_IDLE;
            advanced = true;
        }
        ++p->submit_cursor;
    }
    for (unsigned i = 0; i < p->run_count; ++i)
        pending = pending || (p->run[i].state == RUN_PENDING &&
            (p->run[i].io.phase == SF_IO_WAIT_FIRST || p->run[i].io.phase == SF_IO_SUBMIT_FIRST));
    if (pending) {
        struct fwlab_nfc_page_v2_step_result step = {0};
        if (f->page_nfc.ops->step(f->page_nfc.context, 1, &step) != FWLAB_NFC_API_OK || step.units_used > 1) {
            sf_fail(f, SF_FAULT_IO); return true;
        }
        advanced = step.units_used != 0 || advanced;
    }
    pending = false;
    for (unsigned i = 0; i < p->run_count; ++i) {
        struct sf_write_run *r = &p->run[i];
        if (r->state != RUN_PENDING) continue;
        if (r->io.phase == SF_IO_SUBMIT_FIRST) { pending = true; continue; }
        /* Accepted PROGRAM is drain-only, even when a sibling failed/closed. */
        advanced = sf_page_step_io(f, &r->io, false, false) || advanced;
        if (r->io.phase != SF_IO_DONE) { pending = true; continue; }
        if (r->io.lower_owned) { sf_fail(f, SF_FAULT_IO); return true; }
        if (!program_valid(r)) data_failure(p, SF_FAULT_IO);
        else {
            ++f->window.program_groups;
            if (f->window.max_program_pages < r->physical_page_count)
                f->window.max_program_pages = r->physical_page_count;
        }
        r->io.phase = SF_IO_IDLE; r->state = RUN_RESOLVED;
        advanced = true;
    }
    if (!pending && p->submit_cursor == p->run_count) {
        if (p->data_failed) p->phase = WRITE_ABORT;
        else if (!p->accepted_count) return fail_unprogrammed(f, FWLAB_NFC_REASON_CANCELLED);
        else p->phase = WRITE_MAP;
        advanced = true;
    }
    return advanced;
}
static bool abort_step(struct fwlab_ftl_scale *f)
{
    struct sf_write_pool *p = f->writes;
    if (p->phase == WRITE_ABORT) {
        if (f->nfc_adapter->reset(f) != FWLAB_NFC_API_OK) { sf_fail(f, SF_FAULT_IO); return true; }
        f->nfc_close_started = 1; p->phase = WRITE_ABORT_WAIT;
        return true;
    }
    struct fwlab_nfc_page_v2_step_result step = {0};
    bool quiet = false;
    if (f->page_nfc.ops->step(f->page_nfc.context, 1, &step) != FWLAB_NFC_API_OK || step.units_used > 1 ||
        f->nfc_adapter->quiescent(f, &quiet) != FWLAB_NFC_API_OK) {
        sf_fail(f, SF_FAULT_IO); return true;
    }
    if (quiet) {
        f->nfc_quiescent = 1;
        /* Keep orphan/reservation facts owned in recovery-required state. */
        sf_fail(f, p->fault ? p->fault : SF_FAULT_IO);
        return true;
    }
    return step.units_used != 0;
}
bool sf_write_pool_step(struct fwlab_ftl_scale *f)
{
    struct sf_write_pool *p;
    if (!sf_write_pool_busy(f) || f->quarantined || sf_meta_busy(f)) return false;
    p = f->writes;
    if (p->phase == WRITE_RMW || p->phase == WRITE_RMW_WAIT) return rmw_step(f);
    if (p->phase == WRITE_DATA) return data_step(f);
    if (p->phase == WRITE_ABORT || p->phase == WRITE_ABORT_WAIT) return abort_step(f);
    if (p->phase == WRITE_MAP) {
        struct sf_write_run *r = &p->run[p->map_cursor];
        /* All accepted DATA results are retained here. Descriptor preparation
         * is not NAND admission: the lower BP/ACK barrier protects actual MAP. */
        r->record.durable_frontier = p->map_cursor + 1u == p->accepted_count &&
            p->wave_start_lbas + p->accepted_lbas == f->parent.request.lba_count ?
            f->parent.host_sequence : f->parent.base_frontier;
        f->work.record = r->record;
        if (sf_journal_start(f, &r->record) != FWLAB_SPINE_V0_OK) sf_fail(f, SF_FAULT_METADATA);
        else p->phase = WRITE_MAP_WAIT;
        return true;
    }
    if (p->phase == WRITE_MAP_WAIT) {
        if (sf_meta_result(f) != FWLAB_SPINE_V0_OK) { sf_fail(f, SF_FAULT_METADATA); return true; }
        struct sf_write_run *r = &p->run[p->map_cursor];
        r->reserved = 0;
        p->committed_lbas += r->request.lba_count;
        ++p->map_cursor;
        p->phase = p->map_cursor == p->accepted_count ? WRITE_FINISH : WRITE_MAP;
        return true;
    }
    if (p->phase == WRITE_FINISH) {
        if (p->committed_lbas != p->accepted_lbas || !sf_heads_unreserved(f)) {
            sf_fail(f, SF_FAULT_STATE); return true;
        }
        f->work.request.lba_count = p->committed_lbas;
        f->work.request.buffer_span.length = p->committed_lbas * FWLAB_FTL_SCALE_LBA_BYTES;
        p->preferred_domain = (p->run[p->accepted_count - 1u].destination.domain + 1u) % f->heads.count;
        /* No accepted sibling is abandoned by the parent's next boundary. */
        sf_parent_group_success(f);
        if (!f->quarantined) p->active = 0;
        return true;
    }
    sf_fail(f, SF_FAULT_STATE);
    return true;
}
bool sf_write_pool_runnable(const struct fwlab_ftl_scale *f)
{
    if (!sf_write_pool_busy(f) || sf_meta_busy(f)) return false;
    const struct sf_write_pool *p = f->writes;
    if (p->phase == WRITE_RMW_WAIT) return f->io.phase == SF_IO_DONE;
    if (p->phase != WRITE_DATA && p->phase != WRITE_ABORT_WAIT) return true;
    if (p->phase == WRITE_DATA) {
        if (p->submit_cursor < p->run_count &&
            (p->stop_unaccepted || p->run[p->submit_cursor].state == RUN_PREPARED)) return true;
        for (unsigned i = 0; i < p->run_count; ++i)
            if (p->run[i].state == RUN_PENDING && p->run[i].io.phase == SF_IO_DONE) return true;
    }
    return false;
}
