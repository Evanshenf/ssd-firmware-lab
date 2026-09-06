/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_FTL_SCALE_WINDOW_H
#define FWLAB_FTL_SCALE_WINDOW_H
#define SF_WINDOW_BYTES (SF_MAX_DELTAS * (SF_PAGE_BYTES + SF_OOB_BYTES))
enum sf_window_phase {
    SF_W_WINDOW_BUILD = 32, SF_W_WINDOW_RMW_WAIT, SF_W_WINDOW_PROGRAM_WAIT,
    SF_W_WINDOW_READ_START, SF_W_WINDOW_READ_WAIT
};
struct sf_window {
    uint8_t (*main)[SF_PAGE_BYTES];
    uint8_t (*oob)[SF_OOB_BYTES];
    uint64_t block_uid;
    uint64_t program_groups;
    uint64_t read_groups;
    uint32_t first_ppa;
    uint16_t max_program_pages;
    uint16_t max_read_pages;
    uint8_t zero_read;
};
enum fwlab_spine_result_v0 sf_window_prepare(struct fwlab_ftl_scale *, const struct sf_parent *);
bool sf_window_step(struct fwlab_ftl_scale *);
#endif
