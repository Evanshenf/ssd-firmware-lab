/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "ftl_scale_internal.h"
#include <string.h>

bool sf_heads_init(struct fwlab_ftl_scale *f)
{
    uint32_t blocks, pages, offset = 0;
    const struct fwlab_nfc_geometry *g;
    if (!f || !sf_geometry_counts(&f->config.geometry, &blocks, &pages) ||
        blocks != f->physical_blocks || pages != f->physical_pages ||
        (f->disk_format != SF_FORMAT_VERSION && !sf_format_windowed(f->disk_format))) return false;
    g = &f->config.geometry;
    memset(&f->heads, 0, sizeof(f->heads));
    f->heads.gc_destination = SF_NONE;
    for (uint32_t d = 0; d < SF_HEAD_DOMAINS; ++d) f->heads.domain[d].head = SF_NONE;
    if (f->disk_format != SF_MULTIHEAD_FORMAT_VERSION) {
        f->heads.count = f->heads.per_channel = 1;
        f->heads.domain[0].heap_capacity = blocks;
        return true;
    }
    f->heads.per_channel = SF_HEAD_DOMAINS / g->channels;
    if (f->heads.per_channel > g->luns_per_channel) f->heads.per_channel = g->luns_per_channel;
    f->heads.count = g->channels * f->heads.per_channel;
    for (uint32_t d = 0; d < f->heads.count; ++d) {
        uint32_t lun_class = d % f->heads.per_channel;
        uint32_t luns = 1u + (g->luns_per_channel - 1u - lun_class) / f->heads.per_channel;
        struct sf_head_domain_index *index = &f->heads.domain[d];
        index->heap_offset = offset;
        index->heap_capacity = luns * g->planes_per_lun * g->blocks_per_plane;
        offset += index->heap_capacity;
    }
    /* Segments cover all blocks, including metadata slots that remain unused.
     * Their capacity does not depend on a checkpoint or runtime head choice. */
    return offset == blocks;
}

uint32_t sf_head_domain(const struct fwlab_ftl_scale *f, uint32_t block)
{
    uint32_t linear_lun, channel, lun;
    const struct fwlab_nfc_geometry *g;
    if (!f || block >= f->physical_blocks || !f->heads.count) return SF_NONE;
    if (f->disk_format != SF_MULTIHEAD_FORMAT_VERSION) return 0;
    g = &f->config.geometry;
    linear_lun = block / ((uint32_t)g->planes_per_lun * g->blocks_per_plane);
    channel = linear_lun / g->luns_per_channel;
    lun = linear_lun % g->luns_per_channel;
    return channel * f->heads.per_channel + lun % f->heads.per_channel;
}

uint32_t sf_head_first(const struct fwlab_ftl_scale *f)
{
    if (!f) return SF_NONE;
    if (f->disk_format != SF_MULTIHEAD_FORMAT_VERSION) return f->host_head;
    for (uint32_t d = 0; d < f->heads.count; ++d)
        if (f->heads.domain[d].head != SF_NONE) return f->heads.domain[d].head;
    return SF_NONE;
}

bool sf_heads_empty(const struct fwlab_ftl_scale *f)
{ return f && sf_head_first(f) == SF_NONE; }

bool sf_heads_unreserved(const struct fwlab_ftl_scale *f)
{
    if (!f) return false;
    if (f->disk_format != SF_MULTIHEAD_FORMAT_VERSION)
        return f->host_head == SF_NONE || !f->blocks[f->host_head].reserved_pages;
    for (uint32_t d = 0; d < f->heads.count; ++d) {
        uint32_t block = f->heads.domain[d].head;
        if (block != SF_NONE && f->blocks[block].reserved_pages) return false;
    }
    return true;
}

uint32_t sf_head_tail(const struct fwlab_ftl_scale *f, uint32_t domain)
{
    uint32_t block, ppb;
    const struct sf_block *b;
    if (!f || domain >= f->heads.count) return 0;
    block = f->disk_format == SF_MULTIHEAD_FORMAT_VERSION ? f->heads.domain[domain].head : f->host_head;
    if (block == SF_NONE || block >= f->physical_blocks) return 0;
    b = &f->blocks[block]; ppb = f->config.geometry.pages_per_block;
    if (b->disk.role != SF_HOST_OPEN || b->disk.health != FWLAB_NFC_BLOCK_GOOD ||
        b->reserved_pages || b->disk.allocation_end > ppb) return 0;
    return ppb - b->disk.allocation_end;
}

uint32_t sf_head_select(const struct fwlab_ftl_scale *f, uint32_t excluded,
                        uint32_t preferred, bool *needs_open)
{
    if (needs_open) *needs_open = false;
    if (!f || !needs_open || f->disk_format != SF_MULTIHEAD_FORMAT_VERSION ||
        !f->heads.count || f->heads.gc_destination != SF_NONE) return SF_NONE;
    preferred %= f->heads.count;
    for (uint32_t n = 0; n < f->heads.count; ++n) {
        uint32_t d = (preferred + n) % f->heads.count;
        if (excluded & (1u << d)) continue;
        if (sf_head_tail(f, d)) return d;
        if (f->heads.domain[d].head == SF_NONE && f->free_count > 1 &&
            f->heads.domain[d].free_count) {
            *needs_open = true; return d;
        }
    }
    return SF_NONE;
}

bool sf_head_reserve(struct fwlab_ftl_scale *f, uint32_t domain, uint16_t pages,
                     struct sf_head_span *span)
{
    struct sf_block *block;
    uint32_t b, ppb;
    if (!f || !span || f->disk_format != SF_MULTIHEAD_FORMAT_VERSION ||
        !pages || pages > sf_head_tail(f, domain)) return false;
    b = f->heads.domain[domain].head; block = &f->blocks[b];
    ppb = f->config.geometry.pages_per_block;
    if (!block->disk.block_uid || block->disk.block_uid > (UINT64_MAX - (ppb - 1u)) / ppb)
        return false;
    memset(span, 0, sizeof(*span));
    span->block_uid = block->disk.block_uid; span->domain = domain; span->block = b;
    span->first_ppa = b * ppb + block->disk.allocation_end;
    span->page_count = pages; span->erase_generation = block->disk.erase_generation;
    span->reservation_end = (uint16_t)(block->disk.allocation_end + pages);
    block->reserved_pages = pages;
    return true;
}

bool sf_head_unreserve(struct fwlab_ftl_scale *f, const struct sf_head_span *span)
{
    struct sf_block *block;
    uint32_t ppb;
    if (!f || !span || f->disk_format != SF_MULTIHEAD_FORMAT_VERSION ||
        span->reserved || !span->page_count || span->domain >= f->heads.count ||
        span->block >= f->physical_blocks ||
        f->heads.domain[span->domain].head != span->block) return false;
    block = &f->blocks[span->block]; ppb = f->config.geometry.pages_per_block;
    if (block->disk.role != SF_HOST_OPEN || block->disk.block_uid != span->block_uid ||
        block->disk.erase_generation != span->erase_generation ||
        span->first_ppa != span->block * ppb + block->disk.allocation_end ||
        block->reserved_pages != span->page_count ||
        span->reservation_end != block->disk.allocation_end + span->page_count ||
        span->reservation_end > ppb) return false;
    block->reserved_pages = 0;
    return true;
}
