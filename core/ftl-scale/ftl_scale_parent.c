/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "ftl_scale_internal.h"
#include <string.h>

bool sf_parent_owned(const struct fwlab_ftl_scale *f)
{
    return f->parent.owned != 0;
}

bool sf_parent_clean_boundary(const struct fwlab_ftl_scale *f)
{
    return f->work.kind == SF_WORK_NONE && !f->work.effect_seen &&
        !sf_meta_busy(f) && sf_io_idle(f) &&
        (f->host_head == SF_NONE || !f->blocks[f->host_head].reserved_pages);
}

bool sf_maintenance_allowed(const struct fwlab_ftl_scale *f)
{
    return sf_parent_clean_boundary(f) &&
        (!sf_parent_owned(f) || (f->parent.maintenance_permitted &&
          f->parent.status.state == FWLAB_BLOCK_V0_STATE_ACCEPTED &&
          !f->parent.cancelled && !f->admission_closed));
}

bool sf_work_busy(const struct fwlab_ftl_scale *f)
{
    return f->work.kind != SF_WORK_NONE || sf_parent_owned(f);
}

static bool subgroup_request(const struct sf_parent *p,
                             struct fwlab_block_request_v0 *group)
{
    uint32_t remaining, count;
    uint64_t offset;
    *group = p->request;
    if (group->operation == FWLAB_BLOCK_V0_FLUSH) return p->completed_lbas == 0;
    if (p->completed_lbas >= p->request.lba_count) return false;
    remaining = p->request.lba_count - p->completed_lbas;
    count = remaining < FWLAB_FTL_SCALE_MAX_LBAS ? remaining : FWLAB_FTL_SCALE_MAX_LBAS;
    group->lba += p->completed_lbas;
    /* Keep the old atomic shape for small parents. Large parents resolve a
     * partial head page first, then use aligned interiors and a final tail. */
    if (p->request.lba_count > FWLAB_FTL_SCALE_MAX_LBAS &&
        group->lba % SF_SECTORS_PER_PAGE != 0) {
        uint32_t head = SF_SECTORS_PER_PAGE - (uint32_t)(group->lba % SF_SECTORS_PER_PAGE);
        if (count > head) count = head;
    }
    group->lba_count = count;
    offset = (uint64_t)p->request.buffer_span.offset +
             (uint64_t)p->completed_lbas * FWLAB_FTL_SCALE_LBA_BYTES;
    if (offset > UINT32_MAX) return false;
    group->buffer_span.offset = (uint32_t)offset;
    group->buffer_span.length = count * FWLAB_FTL_SCALE_LBA_BYTES;
    return fwlab_controller_buffer_span_v0_valid_for_lease(
        &group->buffer_span, &group->buffer,
        group->operation == FWLAB_BLOCK_V0_WRITE ?
            FWLAB_CONTROLLER_BUFFER_V0_READ : FWLAB_CONTROLLER_BUFFER_V0_WRITE) != 0;
}

/* Prepare a private group directly, never by recursively submitting Block IO.
 * IN_PROGRESS means maintenance was started and the same parent must wait. */
static enum fwlab_spine_result_v0 prepare_group(struct fwlab_ftl_scale *f,
                                               const struct sf_parent *p)
{
    struct fwlab_block_request_v0 group;
    struct sf_work *w = &f->work;
    uint32_t pages = 0;
    enum fwlab_spine_result_v0 result;
    if (!subgroup_request(p, &group)) return FWLAB_SPINE_V0_INVALID;
    if (group.operation != FWLAB_BLOCK_V0_FLUSH)
        pages = (uint32_t)((group.lba % SF_SECTORS_PER_PAGE + group.lba_count +
                           SF_SECTORS_PER_PAGE - 1u) / SF_SECTORS_PER_PAGE);
    if (group.operation == FWLAB_BLOCK_V0_WRITE) {
        if (p->host_sequence > f->config.host_sequence_limit ||
            f->record_sequence >= f->config.record_sequence_limit ||
            !sf_child_credit(f, (uint64_t)pages * 4u + 4u))
            return FWLAB_SPINE_V0_NO_CAPACITY;
        if (!sf_record_space(f, 1)) {
            result = sf_checkpoint_start(f);
            return result == FWLAB_SPINE_V0_OK ? FWLAB_SPINE_V0_IN_PROGRESS : result;
        }
        if (f->host_head == SF_NONE ||
            pages > (uint32_t)f->config.geometry.pages_per_block -
                        f->blocks[f->host_head].disk.allocation_end) {
            result = sf_space_start(f, pages, false);
            return result == FWLAB_SPINE_V0_OK ? FWLAB_SPINE_V0_IN_PROGRESS : result;
        }
        if (f->controller_buffer.ops->read(f->controller_buffer.context,
            &group.buffer, &group.buffer_span, w->host_bytes, group.buffer_span.length) !=
            FWLAB_CONTROLLER_BUFFER_V0_OK)
            return FWLAB_SPINE_V0_INVALID;
        f->blocks[f->host_head].reserved_pages = (uint16_t)pages;
    } else if (group.operation == FWLAB_BLOCK_V0_READ &&
               !sf_child_credit(f, (uint64_t)pages * 2u))
        return FWLAB_SPINE_V0_NO_CAPACITY;
    w->request = group;
    w->effect_seen = 0;
    w->kind = SF_WORK_HOST;
    w->phase = group.operation == FWLAB_BLOCK_V0_READ ? SF_W_READ_PAGE : SF_W_HOST_PAGE;
    w->first_lpn = (uint32_t)(group.lba / SF_SECTORS_PER_PAGE);
    w->page_count = pages;
    w->page_index = 0;
    memset(&w->record, 0, sizeof(w->record));
    if (group.operation == FWLAB_BLOCK_V0_WRITE) {
        w->record.kind = SF_MAP_GROUP;
        w->record.block = f->host_head;
        w->record.block_uid = f->blocks[f->host_head].disk.block_uid;
        w->record.count = (uint16_t)pages;
        w->record.durable_frontier = p->completed_lbas + group.lba_count ==
            p->request.lba_count ? p->host_sequence : p->base_frontier;
    }
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 sf_parent_admit(struct fwlab_ftl_scale *f,
                                          const struct fwlab_block_request_v0 *request)
{
    struct sf_parent candidate;
    enum fwlab_spine_result_v0 result;
    if (sf_parent_owned(f) || !sf_parent_clean_boundary(f))
        return FWLAB_SPINE_V0_WRONG_STATE;
    if (request->operation == FWLAB_BLOCK_V0_WRITE &&
        f->durable_frontier >= f->config.host_sequence_limit)
        return FWLAB_SPINE_V0_NO_CAPACITY;
    memset(&candidate, 0, sizeof(candidate));
    candidate.request = *request;
    candidate.retired = f->parent.retired;
    candidate.retired_valid = f->parent.retired_valid;
    candidate.base_frontier = f->durable_frontier;
    candidate.host_sequence = f->durable_frontier +
        (request->operation == FWLAB_BLOCK_V0_WRITE ? 1u : 0u);
    result = prepare_group(f, &candidate);
    if (result != FWLAB_SPINE_V0_OK) return result;
    candidate.status.version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    candidate.status.size = (uint16_t)sizeof(candidate.status);
    candidate.status.operation_token = request->operation_token;
    candidate.status.state = FWLAB_BLOCK_V0_STATE_ACCEPTED;
    candidate.owned = 1;
    f->parent = candidate;
    return FWLAB_SPINE_V0_OK;
}

void sf_parent_fail(struct fwlab_ftl_scale *f, uint32_t fault)
{
    struct sf_parent *p = &f->parent;
    struct sf_work *w = &f->work;
    if (!p->owned || p->status.state != FWLAB_BLOCK_V0_STATE_ACCEPTED) return;
    if (p->request.operation == FWLAB_BLOCK_V0_WRITE && f->host_head != SF_NONE)
        f->blocks[f->host_head].reserved_pages = 0;
    p->status.state = FWLAB_BLOCK_V0_STATE_TERMINAL;
    p->status.outcome = p->cancelled ? FWLAB_BLOCK_V0_CANCELLED : FWLAB_BLOCK_V0_FAILED;
    p->status.effect = w->effect_seen || f->quarantined ? FWLAB_BLOCK_V0_EFFECT_UNKNOWN_PREFIX :
        p->completed_lbas ? FWLAB_BLOCK_V0_EFFECT_EXACT_PREFIX : FWLAB_BLOCK_V0_EFFECT_NONE;
    p->status.completed_lbas = p->completed_lbas;
    p->status.data_bytes = p->request.buffer_present ?
        p->completed_lbas * FWLAB_FTL_SCALE_LBA_BYTES : 0;
    p->status.durability_witness = FWLAB_BLOCK_V0_WITNESS_NONE;
    memset(&p->status.frontier, 0, sizeof(p->status.frontier));
    p->status.fault_domain = 1;
    p->status.fault_code = fault;
    p->maintenance_permitted = 0;
    w->kind = SF_WORK_NONE;
    w->phase = SF_W_IDLE;
    if (!f->quarantined) w->effect_seen = 0;
}

void sf_parent_group_success(struct fwlab_ftl_scale *f)
{
    struct sf_parent *p = &f->parent;
    struct sf_work *w = &f->work;
    bool final;
    if (!p->owned || p->status.state != FWLAB_BLOCK_V0_STATE_ACCEPTED ||
        w->kind != SF_WORK_HOST || p->completed_lbas > p->request.lba_count ||
        w->request.lba_count > p->request.lba_count - p->completed_lbas) {
        sf_fail(f, SF_FAULT_STATE);
        return;
    }
    final = p->completed_lbas + w->request.lba_count == p->request.lba_count;
    if (p->request.operation == FWLAB_BLOCK_V0_WRITE &&
        f->durable_frontier != (final ? p->host_sequence : p->base_frontier)) {
        sf_fail(f, SF_FAULT_STATE);
        return;
    }
    p->completed_lbas += w->request.lba_count;
    ++p->completed_groups;
    w->kind = SF_WORK_NONE;
    w->phase = SF_W_IDLE;
    w->effect_seen = 0;
    if (!final) return;
    p->status.state = FWLAB_BLOCK_V0_STATE_TERMINAL;
    p->status.outcome = FWLAB_BLOCK_V0_SUCCEEDED;
    p->status.effect = FWLAB_BLOCK_V0_EFFECT_FULL;
    p->status.completed_lbas = p->request.lba_count;
    p->status.data_bytes = p->request.buffer_present ? p->request.buffer_span.length : 0;
    if (p->request.operation == FWLAB_BLOCK_V0_WRITE)
        p->status.durability_witness = p->request.durability == FWLAB_BLOCK_V0_DURABILITY_SELF ?
            FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE : FWLAB_BLOCK_V0_WITNESS_VOLATILE;
    else if (p->request.operation == FWLAB_BLOCK_V0_FLUSH)
        p->status.durability_witness = FWLAB_BLOCK_V0_WITNESS_FRONTIER_DURABLE;
    if (p->status.durability_witness >= FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE) {
        p->status.frontier.word[0] = f->config.provider_nonce;
        p->status.frontier.word[1] = p->host_sequence;
    }
}

bool sf_parent_step(struct fwlab_ftl_scale *f)
{
    struct sf_parent *p = &f->parent;
    enum fwlab_spine_result_v0 result;
    if (!p->owned || p->status.state != FWLAB_BLOCK_V0_STATE_ACCEPTED ||
        !sf_parent_clean_boundary(f)) return false;
    if (p->cancelled || f->admission_closed) {
        p->cancelled = 1;
        sf_parent_fail(f, FWLAB_NFC_REASON_CANCELLED);
        return true;
    }
    /* This permit is synchronous and private. Public maintenance remains
     * blocked by parent ownership; only this clean-boundary caller can enter. */
    p->maintenance_permitted = 1;
    result = prepare_group(f, p);
    p->maintenance_permitted = 0;
    if (result != FWLAB_SPINE_V0_OK && result != FWLAB_SPINE_V0_IN_PROGRESS)
        sf_parent_fail(f, result == FWLAB_SPINE_V0_NO_CAPACITY ?
                       FWLAB_BLOCK_V0_FAULT_RESOURCE : SF_FAULT_STATE);
    return true;
}
