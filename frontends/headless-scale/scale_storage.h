/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_HEADLESS_SCALE_STORAGE_H
#define FWLAB_HEADLESS_SCALE_STORAGE_H

#include "../headless-j0/j0_internal.h"
#include "ftl_scale.h"
#include "fwlab/private/nand_batch_v2.h"
#include "fwlab/private/nfc_page_v2_lab.h"
#include "fwlab/private/nand_channel_v2.h"
#include "fwlab/private/nfc_channel_v2.h"
#include "fwlab/private/nfc_channel_v2_job.h"

enum scale_storage_read_schedule {
    SCALE_STORAGE_READ_SERIAL = 0,
    SCALE_STORAGE_READ_PARALLEL = 1
};

/* A resource ceiling, not a format/recovery capacity override. Zero allocates
 * at most one map slot per physical page. One factory selects the new engine. */
struct scale_storage_options {
    uint32_t mapping_slots;
    /* Required only by the explicit window-v2 factory. Bound to the SAME
     * physical instance as j0_runtime_config.media_binding, never inferred. */
    const struct fwlab_nand_batch_v2 *page_v2_media;
    /* Explicit LAB factory only: version/size, timing and wiring template.
     * base is rebuilt from the same composition's geometry/UUID/identities. */
    const struct fwlab_nfc_page_v2_lab_config *read_lab_config;
    /* Explicit always-timed READ/PROGRAM/ERASE LAB construction, with the
     * ordinary serial format2 FTL. No runtime provider/timing replacement. */
    const struct fwlab_nfc_page_v2_lab_mutation_config *mutation_lab_config;
    /* Explicit WAVE4-LAB4K construction only. Global aggregate identity is
     * the J0 media binding; actual channel children are independently owned.
     * This field does not change native defaults automatically. MQ2's explicit
     * channel-lab4k option binds this assembly at native construction. */
    const struct fwlab_nand_channel_v2 *channel_media;
    /* Optional construction-time executor for explicit multihead or parallel
     * channel-READ factories.
     * NULL keeps the cooperative actor implementation. Caller owns transport
     * context until runner release; successful close includes its shutdown and
     * actual joins. NULL preserves native defaults; explicit native runtime
     * composition can supply this transport. */
    const struct fwlab_nfc_channel_executor *channel_executor;
    /* Explicit external executor only. If true, release of a NEVER-STEPPED
     * runner returns only its unused FTL/NFC arenas; the caller retains the
     * executor and owes actual STOP/join/destroy. No job has been submitted.
     * Once any runner step occurs, normal shutdown/fini remains mandatory.
     * False preserves the existing synchronous pre-step cleanup contract. */
    bool executor_pre_step_cleanup_by_caller;
    /* Explicit parallel-channel or combined multihead construction only.
     * Zero is LUN-exclusive control;
     * IPR must be selected deliberately and is independent of OS placement. */
    enum fwlab_nfc_page_v2_lab_read_policy read_policy;
    /* Existing multihead factory only. PARALLEL selects one mutable format3
     * instance with disjoint read/write pools; zero preserves serial B/C reads.
     * This is construction-time scheduling, never a read-only transition. */
    enum scale_storage_read_schedule multihead_read_schedule;
};

/* Shared construction presets, not FTL capacity truth. Recovery still validates
 * the stored volume. MiB choices: 64, 256, 65536; no existing-image resize. */
int scale_storage_capacity_mib(uint32_t logical_mib,
    struct fwlab_nfc_geometry *geometry, uint64_t *lba_count);

void scale_storage_factory_init(struct j0_storage_factory *factory,
                                 struct scale_storage_options *options);
void scale_storage_window_v2_factory_init(struct j0_storage_factory *factory,
                                           struct scale_storage_options *options);
void scale_storage_parallel_read_lab_factory_init(struct j0_storage_factory *factory,
                                                   struct scale_storage_options *options);
void scale_storage_mutation_lab_factory_init(struct j0_storage_factory *factory,
                                              struct scale_storage_options *options);
void scale_storage_channel_lab_factory_init(struct j0_storage_factory *factory,
                                             struct scale_storage_options *options);
/* Same actual channel assembly/hub, explicit format3 multi-head FTL. The
 * read-schedule option selects serial or continuously mutable parallel READ. */
void scale_storage_multihead_lab_factory_init(struct j0_storage_factory *factory,
                                              struct scale_storage_options *options);
/* Always-timed policy-selected channel hub and the existing format2 READ pool.
 * Preparation uses ordinary serial writes; this is not B3's write-pool FTL. */
void scale_storage_parallel_channel_lab_factory_init(struct j0_storage_factory *factory,
                                                     struct scale_storage_options *options);
/* Upper/FTL-idle only: drain at most one hub retirement-control step per call.
 * IN_PROGRESS requires retry. At actual lower live-idle, enter FTL read-only
 * without changing provider, timing, UID issuer, media or durable frontier. */
enum fwlab_spine_result_v0 scale_storage_begin_parallel_read(struct j0_runtime *runtime);
/* Serialized coordinator: requires J0 admission, FTL and NFC live-idle before
 * the one-way lower timing/upper read-only transitions. No provider rebinding. */
enum fwlab_spine_result_v0 scale_storage_begin_timed_read(struct j0_runtime *runtime);
/* Private LAB evidence only, no stable observer or FTL timing interface. */
enum fwlab_spine_result_v0 scale_storage_lab_snapshot(const struct j0_runtime *,
    struct fwlab_nfc_page_v2_lab_stats *);
enum fwlab_spine_result_v0 scale_storage_lab_trace_at(const struct j0_runtime *,
    uint32_t, struct fwlab_nfc_page_v2_lab_trace *);
enum fwlab_spine_result_v0 scale_storage_channel_snapshot(const struct j0_runtime *,
    struct fwlab_nfc_channel_v2_stats *);
/* Private composition-only access for the Linux completion wait helper, never
 * an FTL callback or permission to issue a second PAGE2 consumer's commands. */
const struct fwlab_nfc_channel_v2 *scale_storage_channel_hub(const struct j0_runtime *);
enum fwlab_spine_result_v0 scale_storage_query(
    const struct j0_runtime *runtime, struct fwlab_ftl_scale_status *status);
enum fwlab_spine_result_v0 scale_storage_checkpoint(struct j0_runtime *runtime);
enum fwlab_spine_result_v0 scale_storage_gc(
    struct j0_runtime *runtime, uint32_t needed_pages);

#endif
