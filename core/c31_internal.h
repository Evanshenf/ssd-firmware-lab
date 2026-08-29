/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CORE_C31_INTERNAL_H
#define FWLAB_CORE_C31_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fwlab/portable/c31.h"

#define FWLAB_C31_MAGIC UINT32_C(0x43333149)
#define FWLAB_C31_NO_ABORT UINT16_MAX

struct fwlab_c31_command_slot {
    struct fwlab_c31_command_handle handle;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_operation_token operation;
    struct fwlab_c31_operation_token last_terminal;
    struct fwlab_c31_completion_intent completion;
    uint32_t slot_generation;
    uint32_t operation_generation;
    uint32_t lease_generation;
    uint16_t abort_index;
    uint8_t state;
    bool in_use;
    bool provider_owned;
    bool cancel_sent;
    bool completion_valid;
    bool lease_active;
    bool last_terminal_valid;
};

struct fwlab_c31_abort_slot {
    struct fwlab_c31_abort_ticket ticket;
    uint32_t generation;
    uint8_t outcome;
    bool used;
};

struct fwlab_c31 {
    uint32_t magic;
    struct fwlab_c31_capacity capacity;
    struct fwlab_c31_provider_set providers;
    struct fwlab_c31_command_slot *commands;
    struct fwlab_c31_abort_slot *aborts;
    struct fwlab_c31_provider_event *events;
    struct fwlab_c31_trace_entry *trace;
    uint8_t *scratch;
    uint64_t instance_nonce;
    uint64_t next_command_uid;
    uint64_t trace_sequence;
    uint32_t controller_epoch;
    uint32_t drain_epoch;
    uint32_t command_cursor;
    uint32_t provider_cursor;
    uint32_t trace_head;
    uint32_t trace_count;
    uint8_t phase;
};

#endif
