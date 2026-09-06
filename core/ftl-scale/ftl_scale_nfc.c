/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "ftl_scale_internal.h"
#include <string.h>

static uint8_t *staging_span(struct sf_io *io,
                             const struct fwlab_nfc_buffer_ref *ref,
                             uint32_t length)
{
    uint32_t frame;
    if (!io || !ref || ref->reserved || ref->controller_region == 0 ||
        ref->controller_region > SF_FRAMES || ref->length != length)
        return NULL;
    frame = ref->controller_region - 1u;
    if (ref->offset == 0 && length == SF_PAGE_BYTES)
        return io->main[frame];
    if (ref->offset == SF_PAGE_BYTES && length == SF_OOB_BYTES)
        return io->oob[frame];
    return NULL;
}

static enum fwlab_nfc_api_result staging_read(
    void *context, const struct fwlab_nfc_buffer_ref *ref,
    uint8_t *destination, uint32_t length)
{
    uint8_t *source = staging_span(context, ref, length);
    if (!source || !destination)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    memcpy(destination, source, length);
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result staging_write(
    void *context, const struct fwlab_nfc_buffer_ref *ref,
    const uint8_t *source, uint32_t length)
{
    uint8_t *destination = staging_span(context, ref, length);
    if (!source || !destination)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    memcpy(destination, source, length);
    return FWLAB_NFC_API_OK;
}

static const struct fwlab_nfc_buffer_ops staging_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_nfc_buffer_ops),
    .read = staging_read,
    .write = staging_write,
};

struct fwlab_nfc_buffer_provider fwlab_ftl_scale_staging_provider(
    void *arena, size_t arena_size)
{
    struct fwlab_nfc_buffer_provider result = {0};
    if (arena && arena_size >= sizeof(struct fwlab_ftl_scale) &&
        (uintptr_t)arena % _Alignof(struct fwlab_ftl_scale) == 0) {
        struct fwlab_ftl_scale *ftl = arena;
        result.ops = &staging_ops;
        result.context = &ftl->io;
    }
    return result;
}

struct fwlab_nfc_ppa sf_ppa(const struct fwlab_ftl_scale *ftl, uint32_t linear)
{
    const struct fwlab_nfc_geometry *g = &ftl->config.geometry;
    struct fwlab_nfc_ppa ppa = {0};
    ppa.page = (uint16_t)(linear % g->pages_per_block);
    linear /= g->pages_per_block;
    ppa.block = (uint16_t)(linear % g->blocks_per_plane);
    linear /= g->blocks_per_plane;
    ppa.plane = (uint16_t)(linear % g->planes_per_lun);
    linear /= g->planes_per_lun;
    ppa.lun = (uint16_t)(linear % g->luns_per_channel);
    linear /= g->luns_per_channel;
    ppa.channel = (uint16_t)linear;
    return ppa;
}

bool sf_io_idle(const struct fwlab_ftl_scale *ftl)
{
    return ftl && ftl->io.phase == SF_IO_IDLE;
}

static void request_init(struct fwlab_ftl_scale *ftl,
                          struct fwlab_nfc_request *request,
                          uint8_t kind, uint32_t ppa)
{
    memset(request, 0, sizeof(*request));
    request->version = FWLAB_NFC_CONTRACT_VERSION;
    request->size = (uint16_t)sizeof(*request);
    request->operation.instance_nonce = ftl->config.nfc_instance_nonce;
    request->operation.operation_uid = ftl->io.next_uid;
    request->operation.controller_epoch = ftl->config.nfc_epoch;
    /* UID provides per-operation uniqueness. Generation is an incarnation,
     * not the low32 bits of a wide operation UID. */
    request->operation.generation = ftl->config.generation;
    request->cookie = ftl->io.next_uid;
    ftl->io.next_uid = ftl->io.next_uid == UINT64_MAX
                           ? 0 : ftl->io.next_uid + 1u;
    request->ppa = sf_ppa(ftl, ppa);
    request->scheduling_group = 1;
    request->kind = kind;
    ++ftl->nfc_children;
}

static struct fwlab_nfc_buffer_ref frame_ref(uint8_t frame, bool oob)
{
    struct fwlab_nfc_buffer_ref ref = {0};
    ref.controller_region = (uint32_t)frame + 1u;
    ref.offset = oob ? SF_PAGE_BYTES : 0;
    ref.length = oob ? SF_OOB_BYTES : SF_PAGE_BYTES;
    return ref;
}

static enum fwlab_spine_result_v0 c3_start(
    struct fwlab_ftl_scale *ftl, uint32_t ppa, uint8_t frame, uint8_t kind,
    uint32_t pages, bool window)
{
    uint32_t count = kind == SF_IO_ERASE ? 1u : 2u;
    struct sf_io *io;
    if (!ftl || !ftl->initialized || !sf_io_idle(ftl) || pages != 1 || window ||
        frame >= SF_FRAMES || ppa >= ftl->physical_pages)
        return FWLAB_SPINE_V0_INVALID;
    io = &ftl->io;
    if (io->next_uid == 0 ||
        io->next_uid > ftl->config.nfc_operation_uid_limit ||
        count - 1u > ftl->config.nfc_operation_uid_limit - io->next_uid)
        return FWLAB_SPINE_V0_COUNTER_EXHAUSTED;
    memset(&io->result, 0, sizeof(io->result));
    io->result.result = FWLAB_SPINE_V0_IN_PROGRESS;
    io->result.kind = kind;
    io->result.ppa = ppa;
    io->result.frame = frame;
    io->result.count = 1;
    if (kind == SF_IO_READ) {
        /* A failed read must not make stale prior staging bytes look erased. */
        memset(io->main[frame], 0, SF_PAGE_BYTES);
        memset(io->oob[frame], 0, SF_OOB_BYTES);
        request_init(ftl, &io->first, FWLAB_NFC_READ_TRIGGER, ppa);
        request_init(ftl, &io->second, FWLAB_NFC_READ_TRANSFER, ppa);
        io->first.region_mask = FWLAB_NFC_REGION_MASK;
        io->second.region_mask = FWLAB_NFC_REGION_MASK;
        io->second.main = frame_ref(frame, false);
        io->second.oob = frame_ref(frame, true);
    } else if (kind == SF_IO_PROGRAM) {
        request_init(ftl, &io->first, FWLAB_NFC_PROGRAM_TRANSFER, ppa);
        request_init(ftl, &io->second, FWLAB_NFC_PROGRAM_EXECUTE, ppa);
        io->first.region_mask = FWLAB_NFC_REGION_MASK;
        io->second.region_mask = FWLAB_NFC_REGION_MASK;
        io->first.main = frame_ref(frame, false);
        io->first.oob = frame_ref(frame, true);
    } else {
        request_init(ftl, &io->first, FWLAB_NFC_ERASE, ppa);
        memset(&io->second, 0, sizeof(io->second));
    }
    io->phase = SF_IO_SUBMIT_FIRST;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 sf_io_read_start(
    struct fwlab_ftl_scale *ftl, uint32_t ppa, uint8_t frame)
{
    return ftl && ftl->nfc_adapter ? ftl->nfc_adapter->start(ftl, ppa, frame, SF_IO_READ, 1, false) :
        FWLAB_SPINE_V0_INVALID;
}

enum fwlab_spine_result_v0 sf_io_program_start(
    struct fwlab_ftl_scale *ftl, uint32_t ppa, uint8_t frame)
{
    return ftl && ftl->nfc_adapter ? ftl->nfc_adapter->start(ftl, ppa, frame, SF_IO_PROGRAM, 1, false) :
        FWLAB_SPINE_V0_INVALID;
}

enum fwlab_spine_result_v0 sf_io_erase_start(
    struct fwlab_ftl_scale *ftl, uint32_t block)
{
    if (!ftl || block >= ftl->physical_blocks)
        return FWLAB_SPINE_V0_INVALID;
    return ftl->nfc_adapter->start(ftl, block * (uint32_t)ftl->config.geometry.pages_per_block,
                                  0, SF_IO_ERASE, 1, false);
}

static bool token_equal(const struct fwlab_nfc_operation_token *a,
                         const struct fwlab_nfc_operation_token *b)
{
    return a->instance_nonce == b->instance_nonce &&
           a->operation_uid == b->operation_uid &&
           a->controller_epoch == b->controller_epoch &&
           a->generation == b->generation;
}

static void internal_failure(struct sf_io *io, uint8_t reason)
{
    io->result.result = FWLAB_SPINE_V0_POISONED;
    io->result.read_valid = 0;
    io->result.completion.reason = reason;
    io->phase = SF_IO_DONE;
}

static bool c3_step(struct fwlab_ftl_scale *ftl)
{
    struct sf_io *io = &ftl->io;
    struct fwlab_nfc_completion event;
    struct fwlab_nfc_request *request;
    struct fwlab_nfc_step_result step;
    enum fwlab_nfc_api_result result;
    uint32_t count = 0;
    bool first;
    if (io->phase == SF_IO_IDLE || io->phase == SF_IO_DONE)
        return false;
    /* start() only prepares local requests. Before the first program transfer
     * has been accepted, cancellation may discard that staged request without
     * a lower effect. Never discard a later page: prior pages must still reach
     * their mapping commit. Allocated operation UIDs remain consumed. */
    if (io->phase == SF_IO_SUBMIT_FIRST && io->result.kind == SF_IO_PROGRAM &&
        ftl->work.kind == SF_WORK_HOST && ftl->work.phase == SF_W_HOST_PROGRAM_WAIT &&
        ftl->work.page_index == 0 && !ftl->work.effect_seen &&
        ftl->parent.owned && (ftl->parent.cancelled || ftl->admission_closed)) {
        ftl->parent.cancelled = 1;
        io->phase = SF_IO_IDLE;
        sf_host_fail(ftl, FWLAB_NFC_REASON_CANCELLED);
        return true;
    }
    first = io->phase == SF_IO_SUBMIT_FIRST || io->phase == SF_IO_WAIT_FIRST;
    request = first ? &io->first : &io->second;
    if (io->phase == SF_IO_SUBMIT_FIRST || io->phase == SF_IO_SUBMIT_SECOND) {
        struct fwlab_nfc_submit_result submit =
            ftl->nfc.ops->try_submit(ftl->nfc.context, request);
        if (submit.disposition == FWLAB_NFC_ACCEPTED) {
            /* An accepted transfer owns lower cache/frame work even before
             * PROGRAM_EXECUTE. From here the subgroup must reconcile/drain. */
            if (first && io->result.kind == SF_IO_PROGRAM &&
                ftl->work.kind == SF_WORK_HOST &&
                ftl->work.phase == SF_W_HOST_PROGRAM_WAIT)
                ftl->work.effect_seen = 1;
            io->phase = first ? SF_IO_WAIT_FIRST : SF_IO_WAIT_SECOND;
            return true;
        }
        if (submit.disposition == FWLAB_NFC_REJECTED) {
            internal_failure(io, submit.reason == 0
                                      ? FWLAB_NFC_REASON_INTERNAL
                                      : (uint8_t)submit.reason);
            return true;
        }
        return false;
    }
    memset(&event, 0, sizeof(event));
    result = ftl->nfc.ops->poll(ftl->nfc.context, 1, &event, 1, &count);
    if (result != FWLAB_NFC_API_OK || count > 1) {
        internal_failure(io, FWLAB_NFC_REASON_INTERNAL);
        return true;
    }
    if (count == 0) {
        memset(&step, 0, sizeof(step));
        if (ftl->nfc.ops->step(ftl->nfc.context, 1, &step) != FWLAB_NFC_API_OK) {
            internal_failure(io, FWLAB_NFC_REASON_INTERNAL);
            return true;
        }
        return step.units_used != 0 || step.transitions != 0;
    }
    if (event.version != FWLAB_NFC_CONTRACT_VERSION ||
        event.size != sizeof(event) ||
        !token_equal(&event.operation, &request->operation) ||
        event.operation_kind != request->kind || event.cookie != request->cookie ||
        memcmp(&event.ppa, &request->ppa, sizeof(event.ppa)) != 0) {
        internal_failure(io, FWLAB_NFC_REASON_INTERNAL);
        return true;
    }
    if (!first && io->result.kind == SF_IO_READ) {
        /* READ_TRANSFER describes transfer, not new physical health truth. */
        const struct sf_io_facts *trigger = &io->result.completion;
        event.base_erase_generation = trigger->base_erase_generation;
        event.final_erase_generation = trigger->final_erase_generation;
        event.block_health = trigger->block_health;
        event.ecc_status = trigger->ecc_status;
        event.corrected_main_bits = trigger->corrected_main_bits;
        event.corrected_oob_bits = trigger->corrected_oob_bits;
    }
    io->result.completion = (struct sf_io_facts){
        .base_erase_generation = event.base_erase_generation,
        .final_erase_generation = event.final_erase_generation,
        .corrected_main_bits = event.corrected_main_bits,
        .corrected_oob_bits = event.corrected_oob_bits,
        .terminal = event.terminal, .physical_outcome = event.physical_outcome,
        .integrity = event.integrity, .reason = event.reason,
        .block_health = event.block_health, .ecc_status = event.ecc_status,
        .valid_region_mask = event.valid_region_mask, .available = 1
    };
    if (event.terminal != FWLAB_NFC_TERMINAL_SUCCESS ||
        ((request->kind == FWLAB_NFC_PROGRAM_EXECUTE ||
          request->kind == FWLAB_NFC_ERASE) &&
         (event.physical_outcome != FWLAB_NFC_PHYS_APPLIED ||
          event.integrity != FWLAB_NFC_INTEGRITY_COMPLETE))) {
        io->result.result = FWLAB_SPINE_V0_QUARANTINED;
        io->result.effect = io->result.kind == SF_IO_READ ? SF_EFFECT_NONE : SF_EFFECT_UNKNOWN;
        io->phase = SF_IO_DONE;
        return true;
    }
    if (first && io->result.kind != SF_IO_ERASE) {
        io->second.cache = event.cache;
        io->phase = SF_IO_SUBMIT_SECOND;
        return true;
    }
    io->result.result = FWLAB_SPINE_V0_OK;
    io->result.effect = io->result.kind == SF_IO_READ ? SF_EFFECT_NONE : SF_EFFECT_COMPLETE;
    if (io->result.kind == SF_IO_READ) {
        io->result.read_valid = (uint8_t)(
            event.ecc_status != FWLAB_NFC_ECC_UNCORRECTABLE &&
            event.valid_region_mask == FWLAB_NFC_REGION_MASK);
        if (!io->result.read_valid)
            io->result.result = FWLAB_SPINE_V0_QUARANTINED;
    }
    io->phase = SF_IO_DONE;
    return true;
}

static enum fwlab_nfc_api_result c3_reset(struct fwlab_ftl_scale *f)
{ return f->nfc.ops->reset_begin(f->nfc.context, f->config.nfc_instance_nonce, f->config.nfc_epoch); }
static enum fwlab_nfc_api_result c3_drive(struct fwlab_ftl_scale *f)
{ struct fwlab_nfc_step_result step; return f->nfc.ops->step(f->nfc.context, 1, &step); }
static enum fwlab_nfc_api_result c3_quiescent(struct fwlab_ftl_scale *f, bool *quiet)
{ return f->nfc.ops->quiescent(f->nfc.context, f->config.nfc_instance_nonce, f->config.nfc_epoch, quiet); }
const struct sf_nfc_adapter sf_nfc_c3_adapter = { c3_start, c3_step, c3_reset, c3_drive, c3_quiescent };

bool sf_io_step(struct fwlab_ftl_scale *f)
{ return f && f->nfc_adapter ? f->nfc_adapter->step(f) : false; }
enum fwlab_spine_result_v0 sf_io_read_group_start(struct fwlab_ftl_scale *f, uint32_t ppa, uint32_t count)
{ return f && f->nfc_adapter ? f->nfc_adapter->start(f, ppa, 0, SF_IO_READ, count, true) : FWLAB_SPINE_V0_INVALID; }
enum fwlab_spine_result_v0 sf_io_program_group_start(struct fwlab_ftl_scale *f, uint32_t ppa, uint32_t count)
{ return f && f->nfc_adapter ? f->nfc_adapter->start(f, ppa, 0, SF_IO_PROGRAM, count, true) : FWLAB_SPINE_V0_INVALID; }

bool sf_io_take(struct fwlab_ftl_scale *ftl, struct sf_io_result *result)
{
    if (!ftl || !result || ftl->io.phase != SF_IO_DONE)
        return false;
    *result = ftl->io.result;
    ftl->io.phase = SF_IO_IDLE;
    /* Staging belongs to the caller until the next explicit IO starts. */
    return true;
}
