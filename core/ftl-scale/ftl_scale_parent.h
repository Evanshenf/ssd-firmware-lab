/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_FTL_SCALE_PARENT_H
#define FWLAB_FTL_SCALE_PARENT_H

/* One whole Block parent; no inline payload and no additional outer token. */
struct sf_parent {
    struct fwlab_block_request_v0 request;
    struct fwlab_block_status_v0 status;
    struct fwlab_block_status_v0 retired;
    uint64_t base_frontier;
    uint64_t host_sequence;
    uint32_t completed_lbas;
    uint32_t completed_groups;
    uint8_t owned;
    uint8_t cancelled;
    uint8_t retired_valid;
    uint8_t maintenance_permitted;
};

bool sf_parent_owned(const struct fwlab_ftl_scale *ftl);
bool sf_parent_clean_boundary(const struct fwlab_ftl_scale *ftl);
bool sf_maintenance_allowed(const struct fwlab_ftl_scale *ftl);
enum fwlab_spine_result_v0 sf_parent_admit(
    struct fwlab_ftl_scale *ftl, const struct fwlab_block_request_v0 *request);
bool sf_parent_step(struct fwlab_ftl_scale *ftl);
void sf_parent_group_success(struct fwlab_ftl_scale *ftl);
void sf_parent_fail(struct fwlab_ftl_scale *ftl, uint32_t fault);

#endif
