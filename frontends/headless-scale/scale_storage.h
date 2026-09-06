/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_HEADLESS_SCALE_STORAGE_H
#define FWLAB_HEADLESS_SCALE_STORAGE_H

#include "../headless-j0/j0_internal.h"
#include "ftl_scale.h"

/* A resource ceiling, not a format/recovery capacity override. Zero allocates
 * at most one map slot per physical page. One factory selects the new engine. */
struct scale_storage_options {
    uint32_t mapping_slots;
};

void scale_storage_factory_init(struct j0_storage_factory *factory,
                                 struct scale_storage_options *options);
enum fwlab_spine_result_v0 scale_storage_query(
    const struct j0_runtime *runtime, struct fwlab_ftl_scale_status *status);
enum fwlab_spine_result_v0 scale_storage_checkpoint(struct j0_runtime *runtime);
enum fwlab_spine_result_v0 scale_storage_gc(
    struct j0_runtime *runtime, uint32_t needed_pages);

#endif
