/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_HEADLESS_SCALE_STORAGE_H
#define FWLAB_HEADLESS_SCALE_STORAGE_H

#include "../headless-j0/j0_internal.h"
#include "ftl_scale.h"
#include "fwlab/private/nand_batch_v2.h"

/* A resource ceiling, not a format/recovery capacity override. Zero allocates
 * at most one map slot per physical page. One factory selects the new engine. */
struct scale_storage_options {
    uint32_t mapping_slots;
    /* Required only by the explicit window-v2 factory. Bound to the SAME
     * physical instance as j0_runtime_config.media_binding, never inferred. */
    const struct fwlab_nand_batch_v2 *page_v2_media;
};

void scale_storage_factory_init(struct j0_storage_factory *factory,
                                 struct scale_storage_options *options);
void scale_storage_window_v2_factory_init(struct j0_storage_factory *factory,
                                           struct scale_storage_options *options);
enum fwlab_spine_result_v0 scale_storage_query(
    const struct j0_runtime *runtime, struct fwlab_ftl_scale_status *status);
enum fwlab_spine_result_v0 scale_storage_checkpoint(struct j0_runtime *runtime);
enum fwlab_spine_result_v0 scale_storage_gc(
    struct j0_runtime *runtime, uint32_t needed_pages);

#endif
