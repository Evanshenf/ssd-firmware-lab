/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PORTABLE_NFC_TYPES_H
#define FWLAB_PORTABLE_NFC_TYPES_H

#include <stdint.h>

#define FWLAB_NFC_CONTRACT_VERSION 1u
#define FWLAB_NFC_FAULT_PROFILE_VERSION 1u

#define FWLAB_NFC_REGION_MAIN UINT8_C(0x01)
#define FWLAB_NFC_REGION_OOB UINT8_C(0x02)
#define FWLAB_NFC_REGION_MASK UINT8_C(0x03)

#define FWLAB_NFC_MODEL_MAX_CHANNELS 4u
#define FWLAB_NFC_MODEL_MAX_LUNS_PER_CHANNEL 4u
#define FWLAB_NFC_MODEL_MAX_PLANES_PER_LUN 4u
#define FWLAB_NFC_MODEL_MAX_BLOCKS_PER_PLANE 32u
#define FWLAB_NFC_MODEL_MAX_PAGES_PER_BLOCK 64u
#define FWLAB_NFC_MODEL_MAX_MAIN_BYTES 16384u
#define FWLAB_NFC_MODEL_MAX_OOB_BYTES 2048u
#define FWLAB_NFC_MODEL_MAX_TOTAL_PAGES 8192u
#define FWLAB_NFC_MODEL_MAX_MEDIA_BYTES UINT64_C(67108864)
#define FWLAB_NFC_MODEL_MAX_OPERATIONS 32u
#define FWLAB_NFC_MODEL_MAX_RETRY_STEP 7u
#define FWLAB_NFC_MODEL_MAX_DURATION_TICKS UINT32_C(1000000)

enum fwlab_nfc_api_result {
    FWLAB_NFC_API_OK = 0,
    FWLAB_NFC_API_INVALID_CONTRACT = 1,
    FWLAB_NFC_API_NO_CAPACITY = 2,
    FWLAB_NFC_API_WRONG_STATE = 3,
    FWLAB_NFC_API_STALE_TOKEN = 4,
    FWLAB_NFC_API_UNSUPPORTED_VERSION = 5,
    FWLAB_NFC_API_COUNTER_EXHAUSTED = 6,
    FWLAB_NFC_API_INVARIANT_FAILURE = 7,
    FWLAB_NFC_API_NOT_FOUND = 8
};

enum fwlab_nfc_disposition {
    FWLAB_NFC_ACCEPTED = 0,
    FWLAB_NFC_BACKPRESSURE = 1,
    FWLAB_NFC_REJECTED = 2
};

enum fwlab_nfc_terminal {
    FWLAB_NFC_TERMINAL_SUCCESS = 0,
    FWLAB_NFC_TERMINAL_CANCELLED = 1,
    FWLAB_NFC_TERMINAL_FAILED = 2
};

enum fwlab_nfc_operation_kind {
    FWLAB_NFC_READ_TRIGGER = 0,
    FWLAB_NFC_READ_TRANSFER = 1,
    FWLAB_NFC_PROGRAM_TRANSFER = 2,
    FWLAB_NFC_PROGRAM_EXECUTE = 3,
    FWLAB_NFC_ERASE = 4,
    FWLAB_NFC_STATUS = 5
};

enum fwlab_nfc_physical_outcome {
    FWLAB_NFC_PHYS_NO_EFFECT = 0,
    FWLAB_NFC_PHYS_APPLIED = 1
};

enum fwlab_nfc_integrity {
    FWLAB_NFC_INTEGRITY_NOT_APPLICABLE = 0,
    FWLAB_NFC_INTEGRITY_COMPLETE = 1,
    FWLAB_NFC_INTEGRITY_TORN = 2
};

enum fwlab_nfc_reason {
    FWLAB_NFC_REASON_NONE = 0,
    FWLAB_NFC_REASON_RANGE = 1,
    FWLAB_NFC_REASON_STALE = 2,
    FWLAB_NFC_REASON_BAD_BLOCK = 3,
    FWLAB_NFC_REASON_NOT_ERASED = 4,
    FWLAB_NFC_REASON_PROGRAM_ORDER = 5,
    FWLAB_NFC_REASON_PROGRAM_FAILURE = 6,
    FWLAB_NFC_REASON_ERASE_FAILURE = 7,
    FWLAB_NFC_REASON_ECC_UNCORRECTABLE = 8,
    FWLAB_NFC_REASON_WEAR_OUT = 9,
    FWLAB_NFC_REASON_CANCELLED = 10,
    FWLAB_NFC_REASON_RESET = 11,
    FWLAB_NFC_REASON_FAULT_PROFILE = 12,
    FWLAB_NFC_REASON_UNSUPPORTED = 13,
    FWLAB_NFC_REASON_INTERNAL = 14
};

enum fwlab_nfc_ecc_status {
    FWLAB_NFC_ECC_NOT_APPLICABLE = 0,
    FWLAB_NFC_ECC_CLEAN = 1,
    FWLAB_NFC_ECC_CORRECTED = 2,
    FWLAB_NFC_ECC_UNCORRECTABLE = 3
};

enum fwlab_nfc_block_health {
    FWLAB_NFC_BLOCK_GOOD = 0,
    FWLAB_NFC_BLOCK_FACTORY_BAD = 1,
    FWLAB_NFC_BLOCK_RUNTIME_BAD = 2
};

enum fwlab_nfc_program_order {
    FWLAB_NFC_PROGRAM_ANY_ORDER = 0,
    FWLAB_NFC_PROGRAM_ASCENDING = 1
};

enum fwlab_nfc_cache_kind {
    FWLAB_NFC_CACHE_NONE = 0,
    FWLAB_NFC_CACHE_READ = 1,
    FWLAB_NFC_CACHE_PROGRAM = 2
};

enum fwlab_nfc_model_phase {
    FWLAB_NFC_MODEL_READY = 0,
    FWLAB_NFC_MODEL_RESET_DRAIN = 1,
    FWLAB_NFC_MODEL_POWERED_OFF = 2,
    FWLAB_NFC_MODEL_FAULTED = 3
};

enum fwlab_nfc_cut_kind {
    FWLAB_NFC_CUT_CONTROLLER_RESET = 0,
    FWLAB_NFC_CUT_SSD_POWER_LOSS = 1
};

enum fwlab_nfc_trace_kind {
    FWLAB_NFC_TRACE_INIT = 1,
    FWLAB_NFC_TRACE_ACCEPT = 2,
    FWLAB_NFC_TRACE_START = 3,
    FWLAB_NFC_TRACE_EFFECT = 4,
    FWLAB_NFC_TRACE_OUTCOME = 5,
    FWLAB_NFC_TRACE_EVENT = 6,
    FWLAB_NFC_TRACE_CANCEL = 7,
    FWLAB_NFC_TRACE_RESET = 8,
    FWLAB_NFC_TRACE_POWER = 9,
    FWLAB_NFC_TRACE_FAULT = 10
};

struct fwlab_nfc_operation_token {
    uint64_t instance_nonce;
    uint64_t operation_uid;
    uint32_t controller_epoch;
    uint32_t generation;
};

struct fwlab_nfc_ppa {
    uint16_t channel;
    uint16_t lun;
    uint16_t plane;
    uint16_t block;
    uint16_t page;
    uint16_t reserved;
};

struct fwlab_nfc_buffer_ref {
    uint32_t controller_region;
    uint32_t offset;
    uint32_t length;
    uint32_t reserved;
};

struct fwlab_nfc_cache_token {
    uint64_t instance_nonce;
    uint32_t controller_epoch;
    uint32_t generation;
    uint16_t channel;
    uint16_t lun;
    uint16_t plane;
    uint16_t reserved;
};

struct fwlab_nfc_geometry {
    uint16_t version;
    uint16_t size;
    uint16_t channels;
    uint16_t luns_per_channel;
    uint16_t planes_per_lun;
    uint16_t blocks_per_plane;
    uint16_t pages_per_block;
    uint16_t plane_parallelism_per_lun;
    uint32_t main_bytes_per_page;
    uint32_t oob_bytes_per_page;
    uint8_t max_programs_per_erase;
    uint8_t program_order;
    uint16_t reserved0;
    uint32_t reserved1[2];
};

struct fwlab_nfc_ecc_profile {
    uint16_t version;
    uint16_t size;
    uint32_t main_covered_bytes;
    uint32_t oob_covered_bytes;
    uint16_t main_step_bytes;
    uint16_t oob_step_bytes;
    uint16_t main_strength_bits;
    uint16_t oob_strength_bits;
    uint8_t max_retry_step;
    uint8_t reserved0[3];
    uint32_t reserved1[2];
};

struct fwlab_nfc_timing_profile {
    uint16_t version;
    uint16_t size;
    uint32_t command_ticks;
    uint32_t transfer_ticks_per_unit;
    uint32_t read_array_ticks;
    uint32_t program_setup_ticks;
    uint32_t program_ticks_per_unit;
    uint32_t program_status_ticks;
    uint32_t erase_setup_ticks;
    uint32_t erase_ticks_per_page;
    uint32_t erase_status_ticks;
    uint32_t status_ticks;
    uint32_t reserved[2];
};

struct fwlab_nfc_fault_profile {
    uint16_t version;
    uint16_t size;
    uint32_t profile_version;
    uint64_t seed;
    uint32_t read_error_modulus;
    uint32_t program_no_effect_modulus;
    uint32_t program_torn_modulus;
    uint32_t erase_no_effect_modulus;
    uint32_t erase_torn_modulus;
    uint32_t grown_bad_modulus;
    uint32_t reserved[2];
};

struct fwlab_nfc_capacity {
    uint16_t version;
    uint16_t size;
    uint16_t operations;
    uint16_t request_registry;
    uint16_t terminal_events;
    uint16_t result_slots;
    uint16_t trace_entries;
    uint16_t reserved0;
    uint32_t scratch_main_bytes;
    uint32_t scratch_oob_bytes;
    uint32_t operation_generation_limit;
    uint32_t cache_generation_limit;
    uint32_t controller_epoch_limit;
    uint32_t submit_sequence_limit;
    uint64_t operation_uid_limit;
    uint64_t virtual_tick_limit;
    uint32_t reserved1[2];
};

struct fwlab_nfc_model_config {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nfc_geometry geometry;
    struct fwlab_nfc_ecc_profile ecc;
    struct fwlab_nfc_timing_profile timing;
    struct fwlab_nfc_fault_profile fault;
    struct fwlab_nfc_capacity capacity;
    uint16_t successful_erase_limit;
    uint16_t reserved1;
    uint32_t reserved2[2];
};

struct fwlab_nfc_request {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nfc_operation_token operation;
    struct fwlab_nfc_ppa ppa;
    struct fwlab_nfc_buffer_ref main;
    struct fwlab_nfc_buffer_ref oob;
    struct fwlab_nfc_cache_token cache;
    uint64_t cookie;
    uint64_t fault_tag;
    uint32_t scheduling_group;
    uint16_t priority;
    uint8_t kind;
    uint8_t region_mask;
    uint8_t retry_step;
    uint8_t reserved1[3];
    uint32_t reserved2[2];
};

struct fwlab_nfc_submit_result {
    uint32_t disposition;
    uint32_t reason;
};

struct fwlab_nfc_completion {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nfc_operation_token operation;
    struct fwlab_nfc_ppa ppa;
    struct fwlab_nfc_cache_token cache;
    uint64_t cookie;
    uint64_t frozen_fault_word;
    uint64_t payload_digest;
    uint64_t accepted_tick;
    uint64_t begin_tick;
    uint64_t outcome_tick;
    uint64_t status_tick;
    uint32_t submit_sequence;
    uint32_t array_sequence;
    uint16_t base_erase_generation;
    uint16_t final_erase_generation;
    uint16_t corrected_main_bits;
    uint16_t corrected_oob_bits;
    uint8_t terminal;
    uint8_t physical_outcome;
    uint8_t integrity;
    uint8_t reason;
    uint8_t ecc_status;
    uint8_t requested_region_mask;
    uint8_t valid_region_mask;
    uint8_t applied_region_mask;
    uint8_t retry_step;
    uint8_t block_health;
    uint8_t operation_kind;
    uint8_t reserved1;
    uint32_t reserved2[2];
};

struct fwlab_nfc_step_result {
    uint32_t units_used;
    uint32_t transitions;
    uint32_t events_pending;
    uint32_t phase;
    uint64_t virtual_now;
};

struct fwlab_nfc_trace_entry {
    uint16_t version;
    uint16_t size;
    uint32_t kind;
    uint64_t sequence;
    uint64_t virtual_tick;
    struct fwlab_nfc_operation_token operation;
    struct fwlab_nfc_ppa ppa;
    uint32_t from_state;
    uint32_t to_state;
    uint32_t detail;
    uint32_t reserved;
};

struct fwlab_nfc_factory_bad {
    uint16_t version;
    uint16_t size;
    struct fwlab_nfc_ppa block;
    uint32_t reserved[2];
};

#endif
