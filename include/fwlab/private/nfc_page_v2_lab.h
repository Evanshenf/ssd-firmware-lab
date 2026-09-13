/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_PRIVATE_NFC_PAGE_V2_LAB_H
#define FWLAB_PRIVATE_NFC_PAGE_V2_LAB_H

#include "fwlab/private/nfc_page_v2_model.h"

#define FWLAB_NFC_PAGE_V2_LAB_VERSION 1u
#define FWLAB_NFC_PAGE_V2_LAB_SLOTS 4u
#define FWLAB_NFC_PAGE_V2_LAB_CHANNELS 4u
#define FWLAB_NFC_PAGE_V2_LAB_LUNS 16u
#define FWLAB_NFC_PAGE_V2_LAB_TRACE_CAPACITY 256u

/* Explicit LAB wiring: index = channel * geometry.luns_per_channel + lun.
 * target/CE are channel-local; package/die are global membership identifiers.
 * target_lun is unique within a target. No one-die/one-LUN assumption or
 * package/die performance multiplier: each LUN has one read register owner.
 * Unused entries must be zero. This is not a vendor part description. */
struct fwlab_nfc_page_v2_lab_lun {
    uint16_t target, ce, package, die, target_lun, reserved;
};
struct fwlab_nfc_page_v2_lab_config {
    uint16_t version, size;
    uint32_t reserved;
    struct fwlab_nfc_page_v2_config base;
    /* Synthetic, positive integer ns; data-out is ceil(4224e9 / rate).
     * virtual_ns_limit is checked before admission, never wall-clock pacing. */
    uint64_t command_ns, array_read_ns, channel_bytes_per_second;
    uint64_t virtual_ns_limit;
    struct fwlab_nfc_page_v2_lab_lun lun[FWLAB_NFC_PAGE_V2_LAB_LUNS];
};
enum fwlab_nfc_page_v2_lab_phase {
    FWLAB_NFC_PAGE_V2_LAB_PREP = 0,
    FWLAB_NFC_PAGE_V2_LAB_TIMED_READ = 1
};
enum fwlab_nfc_page_v2_lab_event {
    FWLAB_NFC_PAGE_V2_LAB_ADMIT = 1,
    FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN,
    FWLAB_NFC_PAGE_V2_LAB_COMMAND_END,
    FWLAB_NFC_PAGE_V2_LAB_ARRAY_READY,
    FWLAB_NFC_PAGE_V2_LAB_DATA_BEGIN,
    FWLAB_NFC_PAGE_V2_LAB_DATA_END,
    FWLAB_NFC_PAGE_V2_LAB_TERMINAL
};
/* Private bounded evidence, not a stable observer ABI. Saturation is explicit;
 * a full trace drops later entries without affecting execution. */
struct fwlab_nfc_page_v2_lab_trace {
    uint64_t now_ns, operation_uid;
    struct fwlab_nfc_ppa ppa;
    uint32_t event;
};
struct fwlab_nfc_page_v2_lab_stats {
    uint64_t now_ns, accepted_reads, materialized_pages, transitions;
    /* Completed intervals only; held_luns/busy_channels cover active ones. */
    uint64_t channel_busy_ns[FWLAB_NFC_PAGE_V2_LAB_CHANNELS];
    uint64_t array_busy_ns[FWLAB_NFC_PAGE_V2_LAB_LUNS];
    uint64_t register_busy_ns[FWLAB_NFC_PAGE_V2_LAB_LUNS];
    uint64_t trace_dropped;
    uint32_t phase, active_slots, results_pending, held_luns, busy_channels;
    uint32_t trace_count;
    uint8_t closed, quarantined, counters_saturated, reserved;
};
struct fwlab_nfc_page_v2_lab;
size_t fwlab_nfc_page_v2_lab_arena_size(void);
size_t fwlab_nfc_page_v2_lab_arena_alignment(void);
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_init(
    void *, size_t, const struct fwlab_nfc_page_v2_lab_config *,
    const struct fwlab_nand_batch_v2 *, struct fwlab_nfc_page_v2_lab **);
struct fwlab_nfc_page_v2_provider fwlab_nfc_page_v2_lab_provider(
    struct fwlab_nfc_page_v2_lab *);
/* Cancellation of a started page drains its fixed stages, then discards it.
 * A DONE outcome is immutable, including after late cancel/reset; the upper
 * caller must suppress publication and take/discard it before reporting drain. */
/* One-way and allocation-free. Caller must first drain upper FTL startup,
 * normalization, parent/window/io work; this function checks lower live-idle.
 * It never resets UIDs, closes/rebinds the provider or changes media identity.
 * PREP delegates unchanged R0 semantics; TIMED_READ rejects PROGRAM/ERASE. */
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_begin_timed_read(
    struct fwlab_nfc_page_v2_lab *);
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_live_idle(
    const struct fwlab_nfc_page_v2_lab *, bool *);
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_snapshot(
    const struct fwlab_nfc_page_v2_lab *, struct fwlab_nfc_page_v2_lab_stats *);
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_trace_at(
    const struct fwlab_nfc_page_v2_lab *, uint32_t,
    struct fwlab_nfc_page_v2_lab_trace *);

#endif
