/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_FTL_SCALE_WORK_H
#define FWLAB_FTL_SCALE_WORK_H
/* Included after shared map/record types by ftl_scale_internal.h. */
enum sf_work_kind { SF_WORK_NONE, SF_WORK_HOST, SF_WORK_SPACE, SF_WORK_GC,
                    SF_WORK_RECLAIM };
enum sf_work_phase {
    SF_W_IDLE, SF_W_WAIT_CP, SF_W_SPACE, SF_W_CLOSE_WAIT, SF_W_OPEN_WAIT,
    SF_W_HOST_PAGE, SF_W_HOST_READ_WAIT, SF_W_HOST_PROGRAM_WAIT,
    SF_W_HOST_MAP_WAIT, SF_W_READ_PAGE, SF_W_READ_WAIT,
    SF_W_GC_READ, SF_W_GC_READ_WAIT, SF_W_GC_PROGRAM_WAIT,
    SF_W_GC_COMMIT_WAIT, SF_W_ERASE_INTENT_WAIT, SF_W_ERASE_WAIT,
    SF_W_ERASE_DONE_WAIT
};
struct sf_work {
    /* Private execution span. Its token is never submitted as a second Block
     * operation; externally visible ownership/status live in sf_parent. */
    struct fwlab_block_request_v0 request;
    struct sf_record record;
    uint8_t host_bytes[8192];
    uint32_t kind;
    uint32_t phase;
    uint32_t step_cursor;
    uint32_t first_lpn;
    uint32_t page_index;
    uint32_t page_count;
    uint32_t needed_pages;
    uint32_t source_block;
    uint32_t destination_block;
    uint32_t source_page;
    uint32_t moved;
    uint32_t live_count;
    uint32_t reclaim_block;
    uint8_t effect_seen;
    uint8_t force_gc;
};

bool sf_validity_get(const struct fwlab_ftl_scale *ftl, uint32_t ppa);
void sf_heap_refresh(struct fwlab_ftl_scale *ftl, uint32_t block);
bool sf_gc_step(struct fwlab_ftl_scale *ftl);
enum fwlab_spine_result_v0 sf_space_start(struct fwlab_ftl_scale *ftl,
                                         uint32_t needed, bool force_gc);
bool sf_record_space(const struct fwlab_ftl_scale *ftl, uint32_t records);
bool sf_child_credit(const struct fwlab_ftl_scale *ftl, uint64_t children);
void sf_host_fail(struct fwlab_ftl_scale *ftl, uint32_t fault);
#endif
