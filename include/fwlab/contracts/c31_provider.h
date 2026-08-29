/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CONTRACTS_C31_PROVIDER_H
#define FWLAB_CONTRACTS_C31_PROVIDER_H

#include <stdbool.h>
#include <stdint.h>

#include "fwlab/portable/c31_types.h"

#define FWLAB_C31_PROVIDER_CONTRACT_VERSION 1u

/* Providers take ownership only on ACCEPTED and never call back into core. */

enum fwlab_c31_provider_disposition {
    FWLAB_C31_PROVIDER_ACCEPTED = 0,
    FWLAB_C31_PROVIDER_BACKPRESSURE = 1,
    FWLAB_C31_PROVIDER_REJECTED = 2
};

enum fwlab_c31_provider_terminal {
    FWLAB_C31_PROVIDER_SUCCESS = 0,
    FWLAB_C31_PROVIDER_CANCELLED = 1,
    FWLAB_C31_PROVIDER_FAILED = 2
};

struct fwlab_c31_provider_request {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_c31_operation_token operation;
    struct fwlab_c31_origin_token origin;
    struct fwlab_c31_request_token request;
    struct fwlab_c31_capability_token capability;
    uint32_t capability_offset;
    uint32_t controller_region;
    uint32_t controller_offset;
    uint32_t length;
    uint8_t provider_kind;
    uint8_t dma_direction;
    uint16_t ordering_flags;
    uint32_t reserved1;
};

struct fwlab_c31_provider_submit_result {
    uint32_t disposition;
    struct fwlab_c31_fault fault;
};

struct fwlab_c31_provider_event {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_c31_operation_token operation;
    struct fwlab_c31_fault fault;
    uint32_t terminal;
    uint32_t reserved1;
};

/* Events are immutable values. Even zero-delay work appears in a later poll. */

typedef struct fwlab_c31_provider_submit_result
(*fwlab_c31_provider_try_submit_fn)(
    void *context,
    const struct fwlab_c31_provider_request *request
);

typedef enum fwlab_c31_api_result
(*fwlab_c31_provider_cancel_fn)(
    void *context,
    const struct fwlab_c31_operation_token *operation
);

typedef enum fwlab_c31_api_result
(*fwlab_c31_provider_poll_fn)(
    void *context,
    uint32_t budget,
    struct fwlab_c31_provider_event *events,
    uint32_t event_capacity,
    uint32_t *event_count
);

typedef enum fwlab_c31_api_result
(*fwlab_c31_provider_reset_begin_fn)(
    void *context,
    uint64_t instance_nonce,
    uint32_t old_epoch
);

typedef enum fwlab_c31_api_result
(*fwlab_c31_provider_quiescent_fn)(
    void *context,
    uint64_t instance_nonce,
    uint32_t old_epoch,
    bool *quiescent
);

struct fwlab_c31_provider_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    fwlab_c31_provider_try_submit_fn try_submit;
    fwlab_c31_provider_cancel_fn cancel;
    fwlab_c31_provider_poll_fn poll;
    fwlab_c31_provider_reset_begin_fn reset_begin;
    fwlab_c31_provider_quiescent_fn quiescent;
};

/* Context and function pointers are initialization bindings, never data tokens. */
struct fwlab_c31_provider {
    const struct fwlab_c31_provider_ops *ops;
    void *context;
};

struct fwlab_c31_provider_set {
    struct fwlab_c31_provider dma;
    struct fwlab_c31_provider nfc;
};

#endif
