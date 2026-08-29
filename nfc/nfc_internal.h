/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_NFC_INTERNAL_H
#define FWLAB_NFC_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fwlab/portable/nfc_model.h"

#define C33_MODEL_MAGIC UINT64_C(0x4e46434d4f44454c)

enum c33_operation_state {
    C33_OP_FREE = 0,
    C33_OP_QUEUED = 1,
    C33_OP_SCHEDULED = 2,
    C33_OP_RUNNING = 3,
    C33_OP_OUTCOME_FIXED = 4,
    C33_OP_EVENT_PENDING = 5
};

enum c33_fault_class {
    C33_FAULT_CLEAN = 0,
    C33_FAULT_NO_EFFECT = 1,
    C33_FAULT_TORN = 2,
    C33_FAULT_GROWN_BAD = 3
};

struct c33_plane_cache {
    uint8_t kind;
    uint8_t valid_region_mask;
    uint8_t ecc_status;
    uint8_t retry_step;
    uint16_t corrected_main_bits;
    uint16_t corrected_oob_bits;
    uint16_t erase_generation;
    uint16_t reserved1;
    uint32_t generation;
    uint32_t controller_epoch;
    struct fwlab_nfc_ppa ppa;
    uint64_t fault_word;
    uint8_t *main;
    uint8_t *oob;
};

struct c33_operation {
    uint8_t state;
    uint8_t cancel_requested;
    uint8_t reset_owned;
    uint8_t fault_class;
    uint8_t legality_reason;
    uint8_t unit_index;
    uint8_t unit_count;
    uint8_t reserved0;
    uint32_t slot_generation;
    uint32_t submit_sequence;
    uint32_t array_sequence;
    uint32_t resource_kind;
    uint64_t start_tick;
    uint64_t next_tick;
    uint64_t finish_tick;
    uint64_t fault_word;
    struct fwlab_nfc_request request;
    struct fwlab_nfc_completion completion;
    uint8_t *main;
    uint8_t *oob;
};

struct fwlab_nfc_model {
    uint64_t magic;
    struct fwlab_nfc_model_config config;
    uint64_t instance_nonce;
    uint64_t virtual_now;
    uint64_t next_trace_sequence;
    uint64_t next_operation_uid;
    uint32_t current_epoch;
    uint32_t next_submit_sequence;
    uint32_t next_array_sequence;
    uint32_t phase;
    uint32_t trace_count;
    uint32_t event_count;
    struct fwlab_nfc_buffer_provider buffers;
    struct fwlab_nand_media media;
    struct c33_operation *operation;
    struct c33_plane_cache *cache;
    struct fwlab_nfc_trace_entry *trace;
    uint64_t *channel_tail;
    uint64_t *array_tail;
    uint64_t *cache_tail;
    uint8_t *operation_main;
    uint8_t *operation_oob;
    uint8_t *cache_main;
    uint8_t *cache_oob;
};

int c33_checked_add_size(size_t left, size_t right, size_t *result);
int c33_checked_mul_size(size_t left, size_t right, size_t *result);
int c33_model_valid(const struct fwlab_nfc_model *model);
int c33_operation_equal(
    const struct fwlab_nfc_operation_token *left,
    const struct fwlab_nfc_operation_token *right
);
int c33_ppa_valid(
    const struct fwlab_nfc_geometry *geometry,
    const struct fwlab_nfc_ppa *ppa
);
int c33_request_shape_valid(
    const struct fwlab_nfc_model *model,
    const struct fwlab_nfc_request *request,
    uint8_t *reason
);
uint32_t c33_plane_index(
    const struct fwlab_nfc_geometry *geometry,
    const struct fwlab_nfc_ppa *ppa
);
uint32_t c33_lun_index(
    const struct fwlab_nfc_geometry *geometry,
    const struct fwlab_nfc_ppa *ppa
);
uint64_t c33_hash_bytes(const uint8_t *bytes, size_t length);
uint64_t c33_fault_word(
    const struct fwlab_nfc_model *model,
    const struct c33_operation *operation,
    uint16_t erase_generation,
    uint8_t program_count,
    uint32_t read_ordinal
);
enum c33_fault_class c33_fault_classify(
    const struct fwlab_nfc_model *model,
    const struct c33_operation *operation,
    uint64_t fault_word
);
void c33_fault_read_counts(
    const struct fwlab_nfc_model *model,
    const struct c33_operation *operation,
    uint64_t fault_word,
    uint16_t *main_errors,
    uint16_t *oob_errors
);

uint64_t c33_operation_duration(
    const struct fwlab_nfc_model *model,
    const struct fwlab_nfc_request *request
);
int c33_schedule_operation(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
);
int c33_begin_operation(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
);
int c33_finish_operation(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
);
int c33_commit_power_prefix(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
);

void c33_trace(
    struct fwlab_nfc_model *model,
    uint32_t kind,
    const struct c33_operation *operation,
    uint32_t from_state,
    uint32_t to_state,
    uint32_t detail
);

#endif
