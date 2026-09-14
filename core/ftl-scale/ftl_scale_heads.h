/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_FTL_SCALE_HEADS_H
#define FWLAB_FTL_SCALE_HEADS_H

#define SF_MULTIHEAD_FORMAT_VERSION 3u
#define SF_HEAD_DOMAINS 4u

/* Volatile indices derived from exact format3 policy and persisted geometry.
 * Map/block records retain their existing 16-byte representations. */
struct sf_head_domain_index {
    uint32_t head, heap_offset, heap_capacity, free_count;
};
struct sf_heads {
    struct sf_head_domain_index domain[SF_HEAD_DOMAINS];
    uint32_t count, per_channel, gc_destination;
};
struct sf_head_span {
    uint64_t block_uid;
    uint32_t domain, block, first_ppa;
    uint16_t page_count, erase_generation;
    uint16_t reservation_end, reserved;
};

/* Construction/rebuild only: requires validated geometry/physical counts,
 * not ready/initialized. Resets volatile head/GC/free-domain indices. */
bool sf_heads_init(struct fwlab_ftl_scale *);
uint32_t sf_head_domain(const struct fwlab_ftl_scale *, uint32_t block);
uint32_t sf_head_select(const struct fwlab_ftl_scale *, uint32_t excluded_domains,
                        uint32_t preferred_domain, bool *needs_open);
uint32_t sf_head_tail(const struct fwlab_ftl_scale *, uint32_t domain);
bool sf_head_reserve(struct fwlab_ftl_scale *, uint32_t domain, uint16_t pages,
                     struct sf_head_span *);
/* Abandoned unaccepted DATA only, not release of programmed orphan space. */
bool sf_head_unreserve(struct fwlab_ftl_scale *, const struct sf_head_span *);
/* OK: existing usable tail. IN_PROGRESS: CP or durable OPEN started; the
 * ordinary meta/work driver completes it before selection is retried.
 * Does not issue DATA/GC, acquire a second parent or spend the last free block. */
enum fwlab_spine_result_v0 sf_head_open_start(struct fwlab_ftl_scale *, uint32_t domain);
bool sf_heads_empty(const struct fwlab_ftl_scale *);
bool sf_heads_unreserved(const struct fwlab_ftl_scale *);
uint32_t sf_head_first(const struct fwlab_ftl_scale *);
uint32_t sf_free_best(const struct fwlab_ftl_scale *);
/* Shared window/format discriminator, not selection of a second executor. */
bool sf_format_windowed(uint16_t format);

#endif
