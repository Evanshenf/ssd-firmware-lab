/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_BINDING_H
#define FWLAB_C35_BINDING_H

#include <stdbool.h>
#include <stdint.h>

#include "fwlab/portable/c31_types.h"

#define C35_BINDING_VERSION 1u
#define C35_ATOMS 2u
#define C35_ATOM_BYTES 16u

enum c35_result {
    C35_OK = 0,
    C35_INVALID = 1,
    C35_NO_CAPACITY = 2,
    C35_WRONG_STATE = 3,
    C35_STALE = 4,
    C35_PROVIDER_FAILURE = 5,
    C35_INVARIANT = 6,
    C35_LIMIT = 7
};

enum c35_request_kind {
    C35_READ = 0,
    C35_WRITE = 1,
    C35_TRIM = 2,
    C35_FENCE = 3
};

struct c35_request {
    uint16_t version;
    uint16_t size;
    uint8_t kind;
    uint8_t durability_kind;
    uint8_t atom_mask;
    uint8_t atom;
    uint32_t scope;
    uint32_t sequence;
    uint32_t frontier;
    uint32_t reserved[2];
    uint8_t payload[C35_ATOMS][C35_ATOM_BYTES];
};

struct c35_semantic_result {
    uint8_t status;
    uint8_t request_kind;
    uint8_t atom_mask;
    uint8_t present_mask;
    uint8_t witness_class;
    uint8_t witness_reason;
    uint8_t logical_kind[C35_ATOMS];
    uint8_t logical_version[C35_ATOMS];
    uint8_t logical_copy[C35_ATOMS];
    uint8_t reserved0[2];
    uint32_t value_crc[C35_ATOMS];
    uint8_t payload[C35_ATOMS][C35_ATOM_BYTES];
};

typedef enum c35_result (*c35_binding_register_fn)(
    void *context,
    const struct fwlab_c31_request_token *token,
    const struct fwlab_c31_command_handle *command,
    uint32_t owner_epoch,
    const struct c35_request *request
);

typedef enum c35_result (*c35_binding_result_fn)(
    void *context,
    const struct fwlab_c31_command_handle *command,
    const struct fwlab_c31_completion_intent *intent,
    struct c35_semantic_result *result
);

typedef enum c35_result (*c35_binding_ack_fn)(
    void *context,
    const struct fwlab_c31_command_handle *command
);

typedef enum c35_result (*c35_binding_post_reset_fn)(void *context);
typedef enum c35_result (*c35_binding_snapshot_fn)(
    void *context,
    struct c35_semantic_result *snapshot
);
typedef enum c35_result (*c35_binding_quiescent_fn)(
    void *context,
    bool *quiescent
);

struct c35_binding_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    c35_binding_register_fn register_after_submit;
    c35_binding_result_fn result_copy_before_consume;
    c35_binding_ack_fn result_ack_after_consume;
    c35_binding_post_reset_fn post_reset_recover;
    c35_binding_snapshot_fn semantic_snapshot;
    c35_binding_quiescent_fn quiescent;
};

struct c35_binding {
    const struct c35_binding_ops *ops;
    void *context;
};

#endif
