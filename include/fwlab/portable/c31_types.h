/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PORTABLE_C31_TYPES_H
#define FWLAB_PORTABLE_C31_TYPES_H

#include <stdint.h>

#define FWLAB_C31_CONTRACT_VERSION 1u

#define FWLAB_C31_HARD_MAX_COMMANDS 64u
#define FWLAB_C31_HARD_MAX_ABORT_TICKETS 64u
#define FWLAB_C31_HARD_MAX_EVENT_BATCH 32u
#define FWLAB_C31_HARD_MAX_TRACE_ENTRIES 1024u
#define FWLAB_C31_HARD_MAX_SCRATCH_BYTES 65536u

enum fwlab_c31_api_result {
    FWLAB_C31_API_OK = 0,
    FWLAB_C31_API_NO_CAPACITY = 1,
    FWLAB_C31_API_INVALID_CONTRACT = 2,
    FWLAB_C31_API_WRONG_STATE = 3,
    FWLAB_C31_API_STALE_TOKEN = 4,
    FWLAB_C31_API_UNSUPPORTED_VERSION = 5,
    FWLAB_C31_API_COUNTER_EXHAUSTED = 6,
    FWLAB_C31_API_INVARIANT_FAILURE = 7,
    FWLAB_C31_API_NOT_FOUND = 8
};

enum fwlab_c31_provider_kind {
    FWLAB_C31_PROVIDER_NONE = 0,
    FWLAB_C31_PROVIDER_DMA = 1,
    FWLAB_C31_PROVIDER_NFC = 2
};

enum fwlab_c31_dma_direction {
    FWLAB_C31_DMA_NONE = 0,
    FWLAB_C31_DMA_TO_CONTROLLER = 1,
    FWLAB_C31_DMA_FROM_CONTROLLER = 2
};

enum fwlab_c31_lifecycle_state {
    FWLAB_C31_CMD_FREE = 0,
    FWLAB_C31_CMD_ACCEPTED = 1,
    FWLAB_C31_CMD_DISPATCHED = 2,
    FWLAB_C31_CMD_HELD = 3,
    FWLAB_C31_CMD_RUNNING = 4,
    FWLAB_C31_CMD_CANCEL_PENDING = 5,
    FWLAB_C31_CMD_COMPLETION_READY = 6,
    FWLAB_C31_CMD_COMPLETION_LEASED = 7,
    FWLAB_C31_CMD_RESET_DRAIN = 8,
    FWLAB_C31_CMD_RETIRED = 9
};

enum fwlab_c31_instance_phase {
    FWLAB_C31_INSTANCE_READY = 0,
    FWLAB_C31_INSTANCE_RESET_DRAIN = 1,
    FWLAB_C31_INSTANCE_RESET_ACK = 2,
    FWLAB_C31_INSTANCE_TEARDOWN_DRAIN = 3,
    FWLAB_C31_INSTANCE_TEARDOWN_ACK = 4,
    FWLAB_C31_INSTANCE_FAULTED = 5,
    FWLAB_C31_INSTANCE_DEAD = 6
};

enum fwlab_c31_completion_result {
    FWLAB_C31_COMPLETION_SUCCESS = 0,
    FWLAB_C31_COMPLETION_INVALID_COMMAND = 1,
    FWLAB_C31_COMPLETION_UNSUPPORTED_COMMAND = 2,
    FWLAB_C31_COMPLETION_ABORTED = 3,
    FWLAB_C31_COMPLETION_TRANSFER_FAILURE = 4,
    FWLAB_C31_COMPLETION_MEDIA_FAILURE = 5,
    FWLAB_C31_COMPLETION_RESOURCE_FAILURE = 6,
    FWLAB_C31_COMPLETION_INTERNAL_FAILURE = 7
};

enum fwlab_c31_fault_domain {
    FWLAB_C31_FAULT_NONE = 0,
    FWLAB_C31_FAULT_CORE = 1,
    FWLAB_C31_FAULT_DMA = 2,
    FWLAB_C31_FAULT_MEDIA = 3,
    FWLAB_C31_FAULT_RESOURCE = 4,
    FWLAB_C31_FAULT_PROVIDER = 5
};

enum fwlab_c31_retry_class {
    FWLAB_C31_RETRY_NONE = 0,
    FWLAB_C31_RETRY_IMMEDIATE = 1,
    FWLAB_C31_RETRY_LATER = 2,
    FWLAB_C31_RETRY_NEVER = 3
};

enum fwlab_c31_effect_class {
    FWLAB_C31_EFFECT_NONE = 0,
    FWLAB_C31_EFFECT_FULL = 1,
    FWLAB_C31_EFFECT_EXACT_PREFIX = 2,
    FWLAB_C31_EFFECT_UNKNOWN_PREFIX = 3
};

enum fwlab_c31_reason {
    FWLAB_C31_REASON_NONE = 0,
    FWLAB_C31_REASON_PROVIDER_REJECTED = 1,
    FWLAB_C31_REASON_PROVIDER_FAILED = 2,
    FWLAB_C31_REASON_CANCELLED = 3,
    FWLAB_C31_REASON_RANGE = 4,
    FWLAB_C31_REASON_DIRECTION = 5,
    FWLAB_C31_REASON_CAPABILITY = 6,
    FWLAB_C31_REASON_UNSUPPORTED_WORK = 7,
    FWLAB_C31_REASON_DUPLICATE_EVENT = 8,
    FWLAB_C31_REASON_INVARIANT = 9
};

enum fwlab_c31_abort_outcome {
    FWLAB_C31_ABORT_PENDING = 0,
    FWLAB_C31_ABORT_TERMINAL = 1,
    FWLAB_C31_ABORT_TOO_LATE = 2,
    FWLAB_C31_ABORT_RESET_SUPERSEDED = 3,
    FWLAB_C31_ABORT_STALE = 4,
    FWLAB_C31_ABORT_FAULTED = 5
};

enum fwlab_c31_trace_kind {
    FWLAB_C31_TRACE_INIT = 1,
    FWLAB_C31_TRACE_SUBMIT = 2,
    FWLAB_C31_TRACE_STATE = 3,
    FWLAB_C31_TRACE_PROVIDER_ACCEPT = 4,
    FWLAB_C31_TRACE_PROVIDER_EVENT = 5,
    FWLAB_C31_TRACE_COMPLETION = 6,
    FWLAB_C31_TRACE_LEASE = 7,
    FWLAB_C31_TRACE_ABORT = 8,
    FWLAB_C31_TRACE_RESET = 9,
    FWLAB_C31_TRACE_TEARDOWN = 10,
    FWLAB_C31_TRACE_FAULT = 11
};

struct fwlab_c31_origin_token {
    uint64_t word[2];
};

struct fwlab_c31_request_token {
    uint64_t word[2];
};

struct fwlab_c31_capability_token {
    uint64_t word[2];
};

struct fwlab_c31_command_handle {
    uint64_t instance_nonce;
    uint64_t command_uid;
    uint32_t controller_epoch;
    uint16_t slot;
    uint16_t slot_generation;
};

struct fwlab_c31_operation_token {
    struct fwlab_c31_command_handle command;
    uint64_t cookie;
    uint32_t operation_generation;
    uint32_t reserved;
};

struct fwlab_c31_completion_lease {
    struct fwlab_c31_command_handle command;
    uint32_t lease_generation;
    uint32_t reserved;
};

struct fwlab_c31_abort_ticket {
    struct fwlab_c31_command_handle command;
    uint32_t ticket_generation;
    uint32_t reserved;
};

struct fwlab_c31_fault {
    uint16_t domain;
    uint8_t retry_class;
    uint8_t effect_class;
    uint32_t reason;
    uint32_t prefix_length;
    uint32_t reserved;
};

struct fwlab_c31_command_descriptor {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_c31_origin_token origin;
    uint64_t trace_cookie;
    struct fwlab_c31_request_token provider_request;
    struct fwlab_c31_capability_token capability;
    uint32_t capability_offset;
    uint32_t controller_region;
    uint32_t controller_offset;
    uint32_t length;
    uint8_t provider_kind;
    uint8_t dma_direction;
    uint16_t ordering_flags;
    uint32_t reserved1[2];
};

/* Native descriptors are versioned values, not packed serialized records. */

struct fwlab_c31_capacity {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint16_t commands;
    uint16_t abort_tickets;
    uint16_t event_batch;
    uint16_t trace_entries;
    uint32_t scratch_bytes;
    uint32_t slot_generation_limit;
    uint32_t operation_generation_limit;
    uint32_t lease_generation_limit;
    uint32_t ticket_generation_limit;
    uint32_t controller_epoch_limit;
    uint32_t reserved1;
    uint64_t command_uid_limit;
};

struct fwlab_c31_completion_intent {
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_origin_token origin;
    struct fwlab_c31_fault fault;
    uint64_t trace_cookie;
    uint32_t result;
    uint32_t reserved;
};

/* No persistence or durability field exists in the C3.1 completion intent. */

struct fwlab_c31_step_result {
    uint32_t units_used;
    uint32_t transitions;
    uint32_t provider_events;
    uint32_t phase;
};

struct fwlab_c31_trace_entry {
    uint64_t sequence;
    struct fwlab_c31_command_handle command;
    uint32_t kind;
    uint32_t from_state;
    uint32_t to_state;
    uint32_t detail;
};

#endif
