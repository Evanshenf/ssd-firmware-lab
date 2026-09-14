/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "ftl_scale_internal.h"
#include <stdalign.h>
#include <string.h>

static bool live(const struct fwlab_ftl_scale *f)
{
    return f && f->magic == SF_MAGIC && f->initialized;
}

static bool token_equal(const struct fwlab_block_op_token_v0 *a,
                        const struct fwlab_block_op_token_v0 *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static size_t arena_bytes(const struct fwlab_ftl_scale_config *c,
                          uint32_t blocks, uint32_t pages)
{
    uint64_t size = sizeof(struct fwlab_ftl_scale);
    const uint64_t align = alignof(max_align_t);
    size = (size + align - 1u) & ~(align - 1u);
    size += (uint64_t)c->mapping_slots * sizeof(struct sf_map_entry);
    size += (uint64_t)blocks * sizeof(struct sf_block);
    size += (uint64_t)blocks * 2u * sizeof(uint32_t);
    size += ((uint64_t)pages + 7u) / 8u;
    size = (size + align - 1u) & ~(align - 1u);
    return size <= SIZE_MAX ? (size_t)size : 0;
}

int fwlab_ftl_scale_config_valid(const struct fwlab_ftl_scale_config *c)
{
    uint32_t blocks, pages;
    return c && c->version == FWLAB_FTL_SCALE_VERSION && c->size == sizeof(*c) &&
        !c->reserved0 && sf_bytes_zero(c->reserved1, sizeof(c->reserved1)) &&
        sf_geometry_counts(&c->geometry, &blocks, &pages) &&
        c->mapping_slots && c->mapping_slots <= pages &&
        !sf_bytes_zero(c->media_uuid, 16) &&
        (c->namespace_ref.word[0] || c->namespace_ref.word[1]) &&
        c->instance_nonce && c->provider_nonce && c->nfc_instance_nonce &&
        c->instance_nonce != c->provider_nonce && c->instance_nonce != c->nfc_instance_nonce &&
        c->provider_nonce != c->nfc_instance_nonce &&
        c->generation && c->execution_epoch && c->nfc_epoch &&
        c->nfc_operation_uid_limit && c->host_sequence_limit && c->record_sequence_limit &&
        arena_bytes(c, blocks, pages) != 0;
}

int fwlab_ftl_scale_extended_config_valid(const struct fwlab_ftl_scale_extended_config *c)
{
    return c && c->version == FWLAB_FTL_SCALE_EXTENDED_VERSION &&
        c->size == sizeof(*c) && !c->reserved0 &&
        sf_bytes_zero(c->reserved1, sizeof(c->reserved1)) &&
        fwlab_ftl_scale_config_valid(&c->base) && c->max_transfer_lbas &&
        c->max_transfer_lbas <= FWLAB_FTL_SCALE_EXTENDED_MAX_LBAS;
}

size_t fwlab_ftl_scale_arena_alignment(void) { return alignof(max_align_t); }

size_t fwlab_ftl_scale_arena_size(const struct fwlab_ftl_scale_config *c)
{
    uint32_t blocks, pages;
    if (!fwlab_ftl_scale_config_valid(c) || !sf_geometry_counts(&c->geometry, &blocks, &pages))
        return 0;
    return arena_bytes(c, blocks, pages);
}

size_t fwlab_ftl_scale_extended_arena_size(const struct fwlab_ftl_scale_extended_config *c)
{
    return fwlab_ftl_scale_extended_config_valid(c) ? fwlab_ftl_scale_arena_size(&c->base) : 0;
}

size_t fwlab_ftl_scale_window_v2_arena_size(const struct fwlab_ftl_scale_extended_config *c)
{
    size_t base = fwlab_ftl_scale_extended_arena_size(c);
    return base && base <= SIZE_MAX - SF_WINDOW_BYTES ? base + SF_WINDOW_BYTES : 0;
}
size_t fwlab_ftl_scale_parallel_read_arena_size(const struct fwlab_ftl_scale_extended_config *c)
{
    size_t base = fwlab_ftl_scale_window_v2_arena_size(c);
    return base && base <= SIZE_MAX - sizeof(struct sf_read_pool) ? base + sizeof(struct sf_read_pool) : 0;
}

size_t fwlab_ftl_scale_multihead_v3_arena_size(const struct fwlab_ftl_scale_extended_config *c)
{
    size_t base = fwlab_ftl_scale_window_v2_arena_size(c), extra = sf_write_pool_bytes();
    return base && extra && base <= SIZE_MAX - extra ? base + extra : 0;
}

static void submit_result(struct fwlab_block_submit_result_v0 *out,
                          const struct fwlab_block_request_v0 *r,
                          uint32_t disposition, uint32_t fault)
{
    memset(out, 0, sizeof(*out));
    out->version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    out->size = (uint16_t)sizeof(*out);
    out->operation_token = r->operation_token;
    out->disposition = disposition;
    if (disposition == FWLAB_HOST_ACTION_V0_REJECTED) {
        out->fault_domain = fault == FWLAB_BLOCK_V0_FAULT_RESOURCE ? fault : 1;
        out->fault_code = fault ? fault : SF_FAULT_STATE;
    }
}

static bool request_valid(const struct fwlab_ftl_scale *f,
                          const struct fwlab_block_request_v0 *r)
{
    if (!fwlab_block_request_v0_valid(r) ||
        r->operation_token.provider_nonce != f->service.provider_nonce ||
        r->operation_token.generation != f->service.generation ||
        memcmp(&r->namespace_ref, &f->config.namespace_ref, sizeof(r->namespace_ref)) != 0)
        return false;
    if (r->operation == FWLAB_BLOCK_V0_FLUSH) return true;
    if (r->operation != FWLAB_BLOCK_V0_READ && r->operation != FWLAB_BLOCK_V0_WRITE)
        return false;
    return r->lba_count <= f->max_transfer_lbas &&
        r->lba < f->root.layout.lba_count &&
        r->lba_count <= f->root.layout.lba_count - r->lba &&
        r->buffer.issuer_nonce == f->controller_buffer.issuer_nonce &&
        r->buffer.generation == f->controller_buffer.generation &&
        r->buffer_span.length == r->lba_count * FWLAB_FTL_SCALE_LBA_BYTES;
}

static enum fwlab_spine_result_v0 block_submit(void *opaque,
    const struct fwlab_block_request_v0 *r, struct fwlab_block_submit_result_v0 *out)
{
    struct fwlab_ftl_scale *f = opaque;
    struct sf_parent *p;
    enum fwlab_spine_result_v0 result;
    if (!live(f) || !r || !out) return FWLAB_SPINE_V0_INVALID;
    p = &f->parent;
    if (f->quarantined) return FWLAB_SPINE_V0_QUARANTINED;
    if (f->admission_closed || !f->ready) {
        submit_result(out, r, FWLAB_HOST_ACTION_V0_BACKPRESSURE, 0);
        return FWLAB_SPINE_V0_OK;
    }
    if (!request_valid(f, r) || (f->read_only && r->operation != FWLAB_BLOCK_V0_READ)) {
        submit_result(out, r, FWLAB_HOST_ACTION_V0_REJECTED, SF_FAULT_STATE);
        return FWLAB_SPINE_V0_OK;
    }
    if (p->owned && token_equal(&p->request.operation_token, &r->operation_token)) {
        if (memcmp(&p->request, r, sizeof(*r)) != 0) {
            sf_fail(f, SF_FAULT_STATE);
            return FWLAB_SPINE_V0_POISONED;
        }
        submit_result(out, r, FWLAB_HOST_ACTION_V0_ACCEPTED, 0);
        return FWLAB_SPINE_V0_OK;
    }
    if (p->retired_valid && token_equal(&p->retired.operation_token, &r->operation_token)) {
        submit_result(out, r, FWLAB_HOST_ACTION_V0_REJECTED, SF_FAULT_STATE);
        return FWLAB_SPINE_V0_OK;
    }
    if (sf_work_busy(f) || sf_meta_busy(f) || !sf_io_idle(f)) {
        submit_result(out, r, FWLAB_HOST_ACTION_V0_BACKPRESSURE, 0);
        return FWLAB_SPINE_V0_OK;
    }
    result = sf_parent_admit(f, r);
    if (result == FWLAB_SPINE_V0_QUARANTINED) return result;
    submit_result(out, r, result == FWLAB_SPINE_V0_OK ? FWLAB_HOST_ACTION_V0_ACCEPTED :
                  result == FWLAB_SPINE_V0_IN_PROGRESS ? FWLAB_HOST_ACTION_V0_BACKPRESSURE :
                  FWLAB_HOST_ACTION_V0_REJECTED,
                  result == FWLAB_SPINE_V0_INVALID ? SF_FAULT_STATE : FWLAB_BLOCK_V0_FAULT_RESOURCE);
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 block_query(void *opaque,
    const struct fwlab_block_op_token_v0 *token, struct fwlab_block_status_v0 *out)
{
    struct fwlab_ftl_scale *f = opaque;
    if (!live(f) || !token || !out) return FWLAB_SPINE_V0_INVALID;
    if (f->parent.owned && token_equal(token, &f->parent.request.operation_token)) {
        *out = f->parent.status;
        return FWLAB_SPINE_V0_OK;
    }
    if (f->parent.retired_valid && token_equal(token, &f->parent.retired.operation_token)) {
        *out = f->parent.retired;
        return FWLAB_SPINE_V0_OK;
    }
    return FWLAB_SPINE_V0_STALE;
}

static enum fwlab_spine_result_v0 block_cancel(void *opaque,
    const struct fwlab_block_op_token_v0 *token)
{
    struct fwlab_ftl_scale *f = opaque;
    if (!live(f) || !token) return FWLAB_SPINE_V0_INVALID;
    if (!f->parent.owned || !token_equal(token, &f->parent.request.operation_token))
        return FWLAB_SPINE_V0_STALE;
    f->parent.cancelled = 1;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 block_retire_start(void *opaque,
    const struct fwlab_block_op_token_v0 *token)
{
    struct fwlab_ftl_scale *f = opaque;
    if (!live(f) || !token) return FWLAB_SPINE_V0_INVALID;
    if (f->parent.retired_valid && token_equal(token, &f->parent.retired.operation_token))
        return FWLAB_SPINE_V0_OK;
    if (!f->parent.owned || !token_equal(token, &f->parent.request.operation_token))
        return FWLAB_SPINE_V0_STALE;
    if (f->parent.status.state == FWLAB_BLOCK_V0_STATE_ACCEPTED)
        return FWLAB_SPINE_V0_WRONG_STATE;
    if (f->quarantined) return FWLAB_SPINE_V0_QUARANTINED;
    f->parent.status.state = FWLAB_BLOCK_V0_STATE_DRAINING;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 block_retire_query(void *opaque,
    const struct fwlab_block_op_token_v0 *token, struct fwlab_block_status_v0 *out)
{
    struct fwlab_ftl_scale *f = opaque;
    if (!live(f) || !token || !out) return FWLAB_SPINE_V0_INVALID;
    if (f->parent.retired_valid && token_equal(token, &f->parent.retired.operation_token)) {
        *out = f->parent.retired;
        return FWLAB_SPINE_V0_OK;
    }
    if (!f->parent.owned || !token_equal(token, &f->parent.request.operation_token))
        return FWLAB_SPINE_V0_STALE;
    if (f->parent.status.state != FWLAB_BLOCK_V0_STATE_DRAINING)
        return FWLAB_SPINE_V0_WRONG_STATE;
    if (f->work.kind != SF_WORK_NONE || sf_meta_busy(f) || !sf_io_idle(f))
        return FWLAB_SPINE_V0_IN_PROGRESS;
    f->parent.status.state = FWLAB_BLOCK_V0_STATE_RETIRED;
    f->parent.retired = f->parent.status;
    f->parent.retired_valid = 1;
    f->parent.owned = 0;
    *out = f->parent.retired;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 block_close(void *opaque, uint64_t nonce, uint32_t epoch)
{
    struct fwlab_ftl_scale *f = opaque;
    if (!live(f) || !nonce || !epoch) return FWLAB_SPINE_V0_INVALID;
    if (f->admission_closed)
        return f->close_lifecycle_nonce == nonce && f->close_execution_epoch == epoch ?
            FWLAB_SPINE_V0_OK : FWLAB_SPINE_V0_STALE;
    f->admission_closed = 1;
    f->close_lifecycle_nonce = nonce;
    f->close_execution_epoch = epoch;
    if (f->parent.owned) f->parent.cancelled = 1;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 block_quiescent(void *opaque, uint64_t nonce,
    uint32_t epoch, struct fwlab_block_epoch_status_v0 *out)
{
    struct fwlab_ftl_scale *f = opaque;
    if (!live(f) || !out || !f->admission_closed ||
        nonce != f->close_lifecycle_nonce || epoch != f->close_execution_epoch)
        return FWLAB_SPINE_V0_INVALID;
    if (f->quarantined) return FWLAB_SPINE_V0_QUARANTINED;
    memset(out, 0, sizeof(*out));
    out->version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    out->size = (uint16_t)sizeof(*out);
    out->lifecycle_instance_nonce = nonce;
    out->execution_epoch = epoch;
    out->aggregate_operations = f->parent.owned;
    out->admission_closed = 1;
    out->quiescent = (uint8_t)(!sf_work_busy(f) && !sf_meta_busy(f) &&
                             sf_io_idle(f) && f->nfc_quiescent);
    if (out->quiescent) {
        out->aggregate_proof[0] = f->config.provider_nonce;
        out->aggregate_proof[1] = ((uint64_t)f->config.generation << 32) | epoch;
    }
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_block_service_ops_v0 block_ops = {
    .version = FWLAB_BLOCK_SERVICE_V0_VERSION,
    .size = sizeof(struct fwlab_block_service_ops_v0),
    .submit = block_submit, .query = block_query, .cancel = block_cancel,
    .retire_start = block_retire_start, .retire_query = block_retire_query,
    .epoch_close = block_close, .epoch_quiescent = block_quiescent
};

static enum fwlab_spine_result_v0 initialize(void *arena, size_t size,
    const struct fwlab_ftl_scale_config *c,
    const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_nfc_provider *nfc, const struct fwlab_nfc_page_v2_provider *page,
    struct fwlab_ftl_scale **out)
{
    struct fwlab_ftl_scale *f = arena;
    uint8_t *cursor;
    uint32_t b, p, i;
    size_t bytes = fwlab_ftl_scale_arena_size(c), alignment = alignof(max_align_t);
    size_t base_bytes = bytes;
    if (page) {
        if (!bytes || bytes > SIZE_MAX - SF_WINDOW_BYTES || !page->context || !page->ops ||
            page->ops->version != FWLAB_NFC_PAGE_V2_VERSION || page->ops->size != sizeof(*page->ops) ||
            page->ops->reserved || !page->ops->try_submit || !page->ops->cancel || !page->ops->step ||
            !page->ops->take_result || !page->ops->reset_begin || !page->ops->quiescent)
            return FWLAB_SPINE_V0_INVALID;
        bytes += SF_WINDOW_BYTES;
    } else if (!nfc || !nfc->context || !nfc->ops ||
        nfc->ops->version != FWLAB_NFC_CONTRACT_VERSION || nfc->ops->size != sizeof(*nfc->ops) ||
        nfc->ops->reserved || !nfc->ops->try_submit || !nfc->ops->cancel || !nfc->ops->step ||
        !nfc->ops->poll || !nfc->ops->reset_begin || !nfc->ops->quiescent)
        return FWLAB_SPINE_V0_INVALID;
    if (!arena || !out || !bytes || size < bytes || (uintptr_t)arena % alignment ||
        !fwlab_controller_buffer_port_v0_valid(buffer) ||
        c->provider_nonce == buffer->issuer_nonce || !sf_geometry_counts(&c->geometry, &b, &p))
        return FWLAB_SPINE_V0_INVALID;
    memset(arena, 0, bytes);
    f->magic = SF_MAGIC;
    f->config = *c;
    f->controller_buffer = *buffer;
    if (page) {
        f->page_nfc = *page; f->nfc_adapter = &sf_nfc_page2_adapter;
        f->disk_format = SF_WINDOW_FORMAT_VERSION;
        f->window.main = (uint8_t (*)[SF_PAGE_BYTES])((uint8_t *)arena + base_bytes);
        f->window.oob = (uint8_t (*)[SF_OOB_BYTES])((uint8_t *)f->window.main + SF_MAX_DELTAS * SF_PAGE_BYTES);
    } else {
        f->nfc = *nfc; f->nfc_adapter = &sf_nfc_c3_adapter; f->disk_format = SF_FORMAT_VERSION;
    }
    f->physical_blocks = b;
    f->physical_pages = p;
    f->max_transfer_lbas = FWLAB_FTL_SCALE_MAX_LBAS;
    f->arena_bytes = bytes;
    cursor = (uint8_t *)arena + ((sizeof(*f) + alignment - 1u) & ~(alignment - 1u));
    f->map = (struct sf_map_entry *)cursor;
    cursor += (size_t)c->mapping_slots * sizeof(*f->map);
    f->blocks = (struct sf_block *)cursor;
    cursor += (size_t)b * sizeof(*f->blocks);
    f->free_heap = (uint32_t *)cursor;
    cursor += (size_t)b * sizeof(uint32_t);
    f->victim_heap = (uint32_t *)cursor;
    cursor += (size_t)b * sizeof(uint32_t);
    f->validity = cursor;
    for (i = 0; i < c->mapping_slots; ++i) f->map[i].ppa = SF_NONE;
    f->host_head = f->erase_intent_block = SF_NONE;
    if (!sf_heads_init(f)) return FWLAB_SPINE_V0_INVALID;
    f->next_block_uid = f->io.next_uid = 1;
    f->service.ops = &block_ops;
    f->service.context = f;
    f->service.provider_nonce = c->provider_nonce;
    f->service.generation = c->generation;
    f->initialized = 1;
    *out = f;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_init(void *arena, size_t size,
    const struct fwlab_ftl_scale_config *c,
    const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_nfc_provider *nfc, struct fwlab_ftl_scale **out)
{ return initialize(arena, size, c, buffer, nfc, NULL, out); }

enum fwlab_spine_result_v0 fwlab_ftl_scale_init_extended(void *arena, size_t size,
    const struct fwlab_ftl_scale_extended_config *c,
    const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_nfc_provider *nfc, struct fwlab_ftl_scale **out)
{
    enum fwlab_spine_result_v0 result;
    uint32_t maximum;
    if (!fwlab_ftl_scale_extended_config_valid(c)) return FWLAB_SPINE_V0_INVALID;
    maximum = c->max_transfer_lbas;
    result = fwlab_ftl_scale_init(arena, size, &c->base, buffer, nfc, out);
    if (result == FWLAB_SPINE_V0_OK) (*out)->max_transfer_lbas = maximum;
    return result;
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_init_window_v2(void *arena, size_t size,
    const struct fwlab_ftl_scale_extended_config *c,
    const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_nfc_page_v2_provider *nfc, struct fwlab_ftl_scale **out)
{
    enum fwlab_spine_result_v0 result;
    uint32_t maximum;
    if (!fwlab_ftl_scale_extended_config_valid(c) || !nfc) return FWLAB_SPINE_V0_INVALID;
    maximum = c->max_transfer_lbas;
    result = initialize(arena, size, &c->base, buffer, NULL, nfc, out);
    if (result == FWLAB_SPINE_V0_OK) (*out)->max_transfer_lbas = maximum;
    return result;
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_init_parallel_read(void *arena, size_t size,
    const struct fwlab_ftl_scale_extended_config *c,
    const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_nfc_page_v2_provider *nfc, struct fwlab_ftl_scale **out)
{
    size_t total = fwlab_ftl_scale_parallel_read_arena_size(c);
    size_t base = fwlab_ftl_scale_window_v2_arena_size(c);
    struct fwlab_ftl_scale *f;
    enum fwlab_spine_result_v0 result;
    if (!total || size < total || !out) return FWLAB_SPINE_V0_INVALID;
    result = fwlab_ftl_scale_init_window_v2(arena, size, c, buffer, nfc, &f);
    if (result != FWLAB_SPINE_V0_OK) return result;
    f->reads = (struct sf_read_pool *)((uint8_t *)arena + base);
    memset(f->reads, 0, sizeof(*f->reads));
    f->arena_bytes = total;
    *out = f;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_init_multihead_v3(void *arena, size_t size,
    const struct fwlab_ftl_scale_extended_config *c,
    const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_nfc_page_v2_provider *nfc, struct fwlab_ftl_scale **out)
{
    size_t total = fwlab_ftl_scale_multihead_v3_arena_size(c);
    size_t base = fwlab_ftl_scale_window_v2_arena_size(c);
    struct fwlab_ftl_scale *f;
    enum fwlab_spine_result_v0 result;
    if (!total || size < total || !out) return FWLAB_SPINE_V0_INVALID;
    *out = NULL;
    result = fwlab_ftl_scale_init_window_v2(arena, size, c, buffer, nfc, &f);
    if (result != FWLAB_SPINE_V0_OK) return result;
    f->disk_format = SF_MULTIHEAD_FORMAT_VERSION;
    if (!sf_heads_init(f) || !sf_write_pool_init(f, (uint8_t *)arena + base, total - base))
        return FWLAB_SPINE_V0_INVALID;
    f->arena_bytes = total;
    *out = f;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_can_enter_read_only(const struct fwlab_ftl_scale *f)
{
    if (!live(f) || !f->reads) return FWLAB_SPINE_V0_INVALID;
    return f->ready && !f->read_only && !f->admission_closed && !f->quarantined &&
        !sf_work_busy(f) && sf_parent_clean_boundary(f) && !f->erase_intent_sequence ?
        FWLAB_SPINE_V0_OK : FWLAB_SPINE_V0_WRONG_STATE;
}
enum fwlab_spine_result_v0 fwlab_ftl_scale_enter_read_only(struct fwlab_ftl_scale *f)
{
    enum fwlab_spine_result_v0 result = fwlab_ftl_scale_can_enter_read_only(f);
    if (result == FWLAB_SPINE_V0_OK) f->read_only = 1;
    return result;
}

void sf_host_fail(struct fwlab_ftl_scale *f, uint32_t fault)
{
    sf_parent_fail(f, fault);
}

void sf_fail(struct fwlab_ftl_scale *f, uint32_t fault)
{
    f->quarantined = 1;
    f->ready = 0;
    f->fault_code = fault;
    sf_host_fail(f, fault);
}

static uint8_t requested_mask(const struct sf_work *w, uint32_t lpn)
{
    uint64_t first = (uint64_t)lpn * SF_SECTORS_PER_PAGE;
    uint64_t end = w->request.lba + w->request.lba_count;
    uint8_t mask = 0;
    unsigned i;
    for (i = 0; i < SF_SECTORS_PER_PAGE; ++i)
        if (first + i >= w->request.lba && first + i < end) mask |= (uint8_t)(1u << i);
    return mask;
}

static void clear_invalid(uint8_t *bytes, uint8_t mask)
{
    unsigned i;
    for (i = 0; i < SF_SECTORS_PER_PAGE; ++i)
        if (!(mask & (1u << i))) memset(bytes + i * 512u, 0, 512);
}

static void copy_host_span(struct sf_work *w, uint32_t lpn, uint8_t *page, bool writing)
{
    uint64_t start = (uint64_t)lpn * SF_SECTORS_PER_PAGE;
    unsigned sector;
    uint8_t mask = requested_mask(w, lpn);
    for (sector = 0; sector < SF_SECTORS_PER_PAGE; ++sector) {
        size_t host_offset;
        if (!(mask & (1u << sector))) continue;
        host_offset = (size_t)(start + sector - w->request.lba) * 512u;
        if (writing) memcpy(page + sector * 512u, w->host_bytes + host_offset, 512);
        else memcpy(w->host_bytes + host_offset, page + sector * 512u, 512);
    }
}

static bool program_host_page(struct fwlab_ftl_scale *f)
{
    struct sf_work *w = &f->work;
    struct sf_delta *d = &w->record.delta[w->page_index];
    uint32_t b = f->host_head;
    copy_host_span(w, d->lpn, f->io.main[0], true);
    sf_data_oob_encode(f, d->lpn, &d->after, f->blocks[b].disk.block_uid,
                       f->io.main[0], f->io.oob[0]);
    if (sf_io_program_start(f, d->after.ppa, 0) != FWLAB_SPINE_V0_OK)
        sf_fail(f, SF_FAULT_IO);
    else w->phase = SF_W_HOST_PROGRAM_WAIT;
    return true;
}

bool sf_work_step(struct fwlab_ftl_scale *f)
{
    struct sf_work *w = &f->work;
    struct sf_io_result io;
    uint32_t lpn = w->first_lpn + w->page_index;
    if (f->quarantined) return false;
    if (sf_write_pool_busy(f)) return sf_write_pool_step(f);
    if (w->kind == SF_WORK_NONE) return sf_parent_step(f);
    if (w->kind != SF_WORK_HOST) return sf_gc_step(f);
    if (sf_format_windowed(f->disk_format)) return sf_window_step(f);
    if (sf_meta_busy(f)) return false;
    if (f->parent.cancelled && !w->effect_seen && sf_io_idle(f)) {
        sf_host_fail(f, FWLAB_NFC_REASON_CANCELLED);
        return true;
    }
    if (w->request.operation == FWLAB_BLOCK_V0_FLUSH) { sf_parent_group_success(f); return true; }
    if (w->phase == SF_W_HOST_PAGE) {
        struct sf_delta *d;
        uint32_t b = f->host_head, page;
        if (w->page_index == w->page_count) {
            if (sf_journal_start(f, &w->record) != FWLAB_SPINE_V0_OK)
                sf_fail(f, SF_FAULT_METADATA);
            else w->phase = SF_W_HOST_MAP_WAIT;
            return true;
        }
        d = &w->record.delta[w->page_index];
        memset(d, 0, sizeof(*d));
        d->lpn = lpn;
        d->before = f->map[lpn];
        page = f->blocks[b].disk.allocation_end + w->page_index;
        d->after.ppa = b * f->config.geometry.pages_per_block + page;
        d->after.erase_generation = f->blocks[b].disk.erase_generation;
        d->after.data_uid = f->blocks[b].disk.block_uid * f->config.geometry.pages_per_block + page;
        d->after.state = SF_VALUE;
        d->after.valid_mask = requested_mask(w, lpn) | d->before.valid_mask;
        memset(f->io.main[0], 0, SF_PAGE_BYTES);
        if (d->before.state == SF_VALUE &&
            (d->before.valid_mask & (uint8_t)~requested_mask(w, lpn))) {
            if (sf_io_read_start(f, d->before.ppa, 0) != FWLAB_SPINE_V0_OK)
                sf_fail(f, SF_FAULT_IO);
            else w->phase = SF_W_HOST_READ_WAIT;
            return true;
        }
        return program_host_page(f);
    }
    if (w->phase == SF_W_HOST_READ_WAIT || w->phase == SF_W_READ_WAIT) {
        const struct sf_map_entry *e = &f->map[lpn];
        if (!sf_io_take(f, &io)) return false;
        if (io.result != FWLAB_SPINE_V0_OK || !io.read_valid ||
            !sf_data_oob_validate(f, lpn, e,
                f->blocks[e->ppa / f->config.geometry.pages_per_block].disk.block_uid,
                f->io.main[0], f->io.oob[0])) {
            if (w->effect_seen) sf_fail(f, SF_FAULT_IO);
            else sf_host_fail(f, SF_FAULT_IO);
            return true;
        }
        clear_invalid(f->io.main[0], e->valid_mask);
        if (f->parent.cancelled && !w->effect_seen) {
            sf_host_fail(f, FWLAB_NFC_REASON_CANCELLED);
            return true;
        }
        if (w->phase == SF_W_HOST_READ_WAIT) return program_host_page(f);
        copy_host_span(w, lpn, f->io.main[0], false);
        ++w->page_index;
        w->phase = SF_W_READ_PAGE;
        return true;
    }
    if (w->phase == SF_W_HOST_PROGRAM_WAIT) {
        if (!sf_io_take(f, &io)) return false;
        if (io.result != FWLAB_SPINE_V0_OK) sf_fail(f, SF_FAULT_IO);
        else { ++w->page_index; w->phase = SF_W_HOST_PAGE; }
        return true;
    }
    if (w->phase == SF_W_HOST_MAP_WAIT) {
        if (sf_meta_result(f) != FWLAB_SPINE_V0_OK) sf_fail(f, SF_FAULT_METADATA);
        else sf_parent_group_success(f);
        return true;
    }
    if (w->phase == SF_W_READ_PAGE) {
        const struct sf_map_entry *e;
        if (w->page_index == w->page_count) {
            if (f->controller_buffer.ops->write(f->controller_buffer.context,
                &w->request.buffer, &w->request.buffer_span, w->host_bytes,
                w->request.buffer_span.length) != FWLAB_CONTROLLER_BUFFER_V0_OK)
                sf_host_fail(f, SF_FAULT_STATE);
            else sf_parent_group_success(f);
            return true;
        }
        e = &f->map[lpn];
        if (e->state != SF_VALUE) {
            memset(f->io.main[0], 0, SF_PAGE_BYTES);
            copy_host_span(w, lpn, f->io.main[0], false);
            ++w->page_index;
        } else if (sf_io_read_start(f, e->ppa, 0) != FWLAB_SPINE_V0_OK)
            sf_fail(f, SF_FAULT_IO);
        else w->phase = SF_W_READ_WAIT;
        return true;
    }
    sf_fail(f, SF_FAULT_STATE);
    return true;
}

static bool close_step(struct fwlab_ftl_scale *f)
{
    bool quiescent = false;
    enum fwlab_nfc_api_result r;
    if (!f->admission_closed || sf_work_busy(f) || sf_meta_busy(f) || !sf_io_idle(f))
        return false;
    if (!f->nfc_close_started) {
        r = f->nfc_adapter->reset(f);
        if (r != FWLAB_NFC_API_OK) sf_fail(f, SF_FAULT_IO);
        else f->nfc_close_started = 1;
        return true;
    }
    if (!f->nfc_quiescent) {
        r = f->nfc_adapter->drive(f);
        if (r != FWLAB_NFC_API_OK || f->nfc_adapter->quiescent(f, &quiescent) != FWLAB_NFC_API_OK) {
            sf_fail(f, SF_FAULT_IO);
            return true;
        }
        f->nfc_quiescent = (uint8_t)quiescent;
        return quiescent; /* An unchanged external-wait poll is not progress. */
    }
    return false;
}

static enum fwlab_spine_result_v0 step_internal(struct fwlab_ftl_scale *f,
    uint32_t budget, uint32_t *used, struct fwlab_execution_progress *progress)
{
    uint32_t count = 0;
    if (!live(f) || !budget || !used) return FWLAB_SPINE_V0_INVALID;
    if (progress) memset(progress, 0, sizeof(*progress));
    while (count < budget && !f->quarantined) {
        bool advanced;
        if (sf_read_pool_busy(f)) advanced = sf_read_pool_step(f);
        else if (f->work.step_cursor == 0) advanced = sf_io_step(f);
        else if (f->work.step_cursor == 1) advanced = sf_meta_step(f);
        else {
            advanced = sf_work_step(f);
            advanced = close_step(f) || advanced;
        }
        if (progress && advanced) progress->advanced = 1;
        f->work.step_cursor = (f->work.step_cursor + 1u) % 3u;
        ++count;
    }
    /* The FTL owns both the retained result and its consumer. Do not infer
     * readiness from an occupied parent or from an attempted NFC submission. */
    if (progress && !f->quarantined && f->io.phase == SF_IO_DONE &&
        (sf_work_busy(f) || sf_meta_busy(f)))
        progress->runnable = 1;
    if (progress && !f->quarantined && sf_read_pool_runnable(f)) progress->runnable = 1;
    if (progress && !f->quarantined && sf_write_pool_runnable(f)) progress->runnable = 1;
    *used = count;
    return f->quarantined ? FWLAB_SPINE_V0_QUARANTINED : FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_step(struct fwlab_ftl_scale *f,
                                              uint32_t budget, uint32_t *used)
{
    return step_internal(f, budget, used, NULL);
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_step_report(struct fwlab_ftl_scale *f,
    uint32_t budget, uint32_t *used, struct fwlab_execution_progress *progress)
{
    if (!progress || !live(f) || f->nfc_adapter != &sf_nfc_page2_adapter)
        return FWLAB_SPINE_V0_INVALID;
    return step_internal(f, budget, used, progress);
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_format_start(struct fwlab_ftl_scale *f,
                                                       uint64_t lbas)
{
    if (!live(f) || f->admission_closed || f->quarantined || f->ready ||
        sf_work_busy(f) || sf_meta_busy(f) || !sf_io_idle(f)) return FWLAB_SPINE_V0_WRONG_STATE;
    return sf_format_start(f, lbas);
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_recover_start(struct fwlab_ftl_scale *f,
                                                        uint64_t expected)
{
    if (!live(f) || f->admission_closed || f->quarantined || f->ready ||
        sf_work_busy(f) || sf_meta_busy(f) || !sf_io_idle(f)) return FWLAB_SPINE_V0_WRONG_STATE;
    return sf_recover_start(f, expected);
}

struct fwlab_block_service_v0 fwlab_ftl_scale_block_service(struct fwlab_ftl_scale *f)
{
    struct fwlab_block_service_v0 empty = {0};
    return live(f) ? f->service : empty;
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_volume_query(const struct fwlab_ftl_scale *f,
    struct fwlab_block_volume_binding_v0 *out)
{
    struct fwlab_block_volume_binding_v0 result = {0};
    if (!live(f) || !out) return FWLAB_SPINE_V0_INVALID;
    if (f->quarantined) return FWLAB_SPINE_V0_QUARANTINED;
    if (f->admission_closed) return FWLAB_SPINE_V0_WRONG_STATE;
    if (!f->ready) return FWLAB_SPINE_V0_IN_PROGRESS;
    result.volume.version = FWLAB_BLOCK_VOLUME_V0_VERSION;
    result.volume.size = (uint16_t)sizeof(result.volume);
    result.volume.namespace_ref = f->config.namespace_ref;
    result.volume.lba_count = f->root.layout.lba_count;
    result.volume.lba_bytes = FWLAB_FTL_SCALE_LBA_BYTES;
    result.service = f->service;
    *out = result;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_query(const struct fwlab_ftl_scale *f,
                                               struct fwlab_ftl_scale_status *out)
{
    if (!live(f) || !out) return FWLAB_SPINE_V0_INVALID;
    memset(out, 0, sizeof(*out));
    out->record_sequence = f->record_sequence;
    out->map_sequence = f->map_sequence;
    out->durable_frontier = f->durable_frontier;
    out->next_block_uid = f->next_block_uid;
    out->nfc_children = f->nfc_children;
    out->checkpoints = f->checkpoints;
    out->garbage_collections = f->garbage_collections;
    out->arena_bytes = f->arena_bytes;
    out->free_blocks = f->free_count;
    out->victim_blocks = f->victim_count;
    out->fault_code = f->fault_code;
    out->ready = f->ready;
    out->busy = (uint8_t)(sf_work_busy(f) || sf_meta_busy(f) || !sf_io_idle(f));
    out->admission_closed = f->admission_closed;
    out->quarantined = f->quarantined;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_checkpoint_start(struct fwlab_ftl_scale *f)
{
    if (!live(f) || !f->ready || f->admission_closed || f->quarantined || f->read_only ||
        sf_work_busy(f) || sf_meta_busy(f) || !sf_io_idle(f)) return FWLAB_SPINE_V0_WRONG_STATE;
    return sf_checkpoint_start(f);
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_gc_start(struct fwlab_ftl_scale *f,
                                                 uint32_t needed)
{
    if (live(f) && (sf_parent_owned(f) || f->read_only)) return FWLAB_SPINE_V0_WRONG_STATE;
    return live(f) ? sf_space_start(f, needed, true) : FWLAB_SPINE_V0_INVALID;
}

enum fwlab_spine_result_v0 fwlab_ftl_scale_fini(struct fwlab_ftl_scale *f)
{
    if (!live(f) || f->quarantined || !f->admission_closed || !f->nfc_quiescent ||
        sf_work_busy(f) || sf_meta_busy(f) || !sf_io_idle(f)) return FWLAB_SPINE_V0_WRONG_STATE;
    f->initialized = 0;
    return FWLAB_SPINE_V0_OK;
}
