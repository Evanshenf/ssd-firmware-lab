/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_FTL_SCALE_READ_H
#define FWLAB_FTL_SCALE_READ_H

#define SF_READ_RUNS 4u
enum sf_read_run_state { SF_READ_EMPTY, SF_READ_PENDING, SF_READ_READY };
struct sf_read_run {
    struct fwlab_block_request_v0 request;
    struct sf_io io;
    struct sf_map_entry map[SF_MAX_DELTAS];
    uint64_t block_uid;
    uint32_t first_lpn, first_ppa, pages, logical_offset;
    uint8_t state, zero;
    uint8_t main[SF_MAX_DELTAS][SF_PAGE_BYTES];
    uint8_t oob[SF_MAX_DELTAS][SF_OOB_BYTES];
};
/* Allocated only by the explicit constructor, never per request. */
struct sf_read_pool {
    uint32_t issued_lbas, fault;
    uint8_t active, stopping;
    struct sf_read_run run[SF_READ_RUNS];
};
bool sf_read_pool_busy(const struct fwlab_ftl_scale *);
bool sf_read_pool_runnable(const struct fwlab_ftl_scale *);
enum fwlab_spine_result_v0 sf_read_parent_prepare(
    struct fwlab_ftl_scale *, const struct sf_parent *);
bool sf_read_pool_step(struct fwlab_ftl_scale *);
bool sf_read_map_valid(const struct fwlab_ftl_scale *, const struct sf_map_entry *);
bool sf_read_page_valid(const struct fwlab_ftl_scale *, uint32_t,
    const struct sf_map_entry *, uint64_t, const struct sf_io_facts *,
    const uint8_t *, const uint8_t *);
void sf_read_zero_invalid(uint8_t *, uint8_t);
#endif
