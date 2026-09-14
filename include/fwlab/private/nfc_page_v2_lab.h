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
#define FWLAB_NFC_PAGE_V2_LAB_MUTATION_VERSION 1u

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
/* LAB-RW-R2: always timed from construction, including preparation/recovery.
 * All costs are explicit and positive. read.command_ns is PROGRAM LOAD_CA;
 * its channel byte rate accounts for full main/OOB input and status response.
 * No vendor defaults, wall-clock pacing, multiplane or cache-program claim. */
struct fwlab_nfc_page_v2_lab_mutation_config {
    uint16_t version, size;
    uint32_t reserved;
    struct fwlab_nfc_page_v2_lab_config read;
    uint64_t program_confirm_ns, array_program_ns;
    uint64_t erase_command_ns, array_erase_ns, status_command_ns;
    uint32_t status_response_bytes, reserved1;
};
enum fwlab_nfc_page_v2_lab_phase {
    FWLAB_NFC_PAGE_V2_LAB_PREP = 0,
    FWLAB_NFC_PAGE_V2_LAB_TIMED_READ = 1,
    FWLAB_NFC_PAGE_V2_LAB_TIMED_RW = 2
};
enum fwlab_nfc_page_v2_lab_event {
    FWLAB_NFC_PAGE_V2_LAB_ADMIT = 1,
    FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN,
    FWLAB_NFC_PAGE_V2_LAB_COMMAND_END,
    FWLAB_NFC_PAGE_V2_LAB_ARRAY_READY,
    FWLAB_NFC_PAGE_V2_LAB_DATA_BEGIN,
    FWLAB_NFC_PAGE_V2_LAB_DATA_END,
    FWLAB_NFC_PAGE_V2_LAB_TERMINAL,
    FWLAB_NFC_PAGE_V2_LAB_LOAD_BEGIN,
    FWLAB_NFC_PAGE_V2_LAB_LOAD_END,
    FWLAB_NFC_PAGE_V2_LAB_INPUT_BEGIN,
    FWLAB_NFC_PAGE_V2_LAB_INPUT_END,
    FWLAB_NFC_PAGE_V2_LAB_CONFIRM_BEGIN,
    FWLAB_NFC_PAGE_V2_LAB_CONFIRM_END,
    FWLAB_NFC_PAGE_V2_LAB_PROGRAM_EFFECT,
    FWLAB_NFC_PAGE_V2_LAB_ERASE_BEGIN,
    FWLAB_NFC_PAGE_V2_LAB_ERASE_COMMAND_END,
    FWLAB_NFC_PAGE_V2_LAB_ERASE_EFFECT,
    FWLAB_NFC_PAGE_V2_LAB_STATUS_BEGIN,
    FWLAB_NFC_PAGE_V2_LAB_STATUS_END
};
/* Mutation EFFECT entries mark the array-completion boundary. Result facts
 * and attempted counters distinguish an invoked effect from a quarantine skip. */
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
    uint64_t accepted_program_groups, confirmed_program_groups;
    uint64_t attempted_program_pages, successful_program_pages;
    uint64_t accepted_erases, issued_erases, attempted_erases, successful_erases;
    /* Program/erase bytes/pages count authoritative API_OK applied facts only;
     * an uncertain callback contributes an attempt, not invented byte counts. */
    uint64_t preflight_reads, program_main_bytes, program_oob_bytes, erased_pages;
    uint64_t data_in_main_bytes, data_in_oob_bytes;
    uint64_t data_out_main_bytes, data_out_oob_bytes, status_response_bytes;
    uint64_t program_array_busy_ns[FWLAB_NFC_PAGE_V2_LAB_LUNS];
    uint64_t erase_array_busy_ns[FWLAB_NFC_PAGE_V2_LAB_LUNS];
    uint64_t mutation_reservation_ns[FWLAB_NFC_PAGE_V2_LAB_LUNS];
};
struct fwlab_nfc_page_v2_lab;
size_t fwlab_nfc_page_v2_lab_arena_size(void);
size_t fwlab_nfc_page_v2_lab_arena_alignment(void);
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_init(
    void *, size_t, const struct fwlab_nfc_page_v2_lab_config *,
    const struct fwlab_nand_batch_v2 *, struct fwlab_nfc_page_v2_lab **);
/* Same slots/provider/event engine. No PREP transition or FTL read-only mode.
 * PROGRAM retains the LUN through the complete group. Before first CONFIRM
 * cancellation drains only started transfers and makes no physical mutation;
 * after it, ordinary cancellation drains the accepted group. Physical success
 * remains SUCCESS/COMPLETE. Real failure retains prefix/current/suffix facts.
 * Scratch is separate from the input snapshot. No callbacks after quarantine. */
enum fwlab_nfc_api_result fwlab_nfc_page_v2_lab_mutation_init(
    void *, size_t, const struct fwlab_nfc_page_v2_lab_mutation_config *,
    const struct fwlab_nand_batch_v2 *, struct fwlab_nfc_page_v2_lab **);
struct fwlab_nfc_page_v2_provider fwlab_nfc_page_v2_lab_provider(
    struct fwlab_nfc_page_v2_lab *);
/* READ cancellation of a started page drains its fixed stages, then discards it.
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
