/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_BINDING_H
#define FWLAB_C35_BINDING_H

#include <stdbool.h>
#include <stdint.h>

#include "fwlab/portable/c31_types.h"

#define C35_REQUEST_VERSION 1u
#define C35_BINDING_OPS_VERSION 2u
#define C35_BINDING_VERSION C35_BINDING_OPS_VERSION
#define C35_ATOMS 2u
#define C35_ATOM_BYTES 16u
#define C35_BINDING_SLOTS 2u

enum c35_result {
    C35_OK = 0,
    C35_INVALID = 1,
    C35_NO_CAPACITY = 2,
    C35_WRONG_STATE = 3,
    C35_STALE = 4,
    C35_PROVIDER_FAILURE = 5,
    C35_INVARIANT = 6,
    C35_LIMIT = 7,
    C35_IN_PROGRESS = 8,
    C35_UNSUPPORTED_VERSION = 9,
    C35_MEDIA_CAPACITY = 10,
    C35_COUNTER_EXHAUSTED = 11,
    C35_NOT_FOUND = 12,
    C35_CORRUPT = 13,
    C35_POISONED = 14
};

#define C35_CAUSE_VERSION 1u

enum c35_retry_class {
    C35_RETRY_NONE = 0,
    C35_RETRY_SAME_TOKEN = 1,
    C35_RETRY_REPAIR_REQUIRED = 2
};

enum c35_cause_domain {
    C35_CAUSE_NONE = 0,
    C35_CAUSE_C35 = 1,
    C35_CAUSE_C31 = 2,
    C35_CAUSE_BINDING = 3,
    C35_CAUSE_C34 = 4,
    C35_CAUSE_NFC = 5,
    C35_CAUSE_MEDIA = 6,
    C35_CAUSE_BUNDLE = 7,
    C35_CAUSE_OBSERVER = 8
};

struct c35_cause_detail {
    uint16_t version;
    uint16_t size;
    uint8_t domain;
    uint8_t retry_class;
    uint16_t reserved;
    uint32_t code;
};

void c35_cause_clear(struct c35_cause_detail *cause);
void c35_cause_record(
    struct c35_cause_detail *cause,
    uint8_t domain,
    uint32_t code,
    uint8_t retry_class
);
int c35_cause_valid(const struct c35_cause_detail *cause);

#define C35_NO_TRANSIENT_CAPACITY C35_NO_CAPACITY
#define C35_INVALID_CONTRACT C35_INVALID
#define C35_MEDIA_CAPACITY_EXHAUSTED C35_MEDIA_CAPACITY
#define C35_WRONG_PHASE C35_WRONG_STATE
#define C35_STALE_TOKEN C35_STALE
#define C35_PROVIDER_FAILED C35_PROVIDER_FAILURE
#define C35_INVARIANT_FAILED C35_INVARIANT
#define C35_POISONED_REPAIR_REQUIRED C35_POISONED

enum c35_request_kind {
    C35_READ = 0,
    C35_WRITE = 1,
    C35_TRIM = 2,
    C35_FENCE = 3
};

enum c35_registration_state {
    C35_REG_ABSENT = 0,
    C35_REG_PREPARED = 1,
    C35_REG_PARTIAL = 2,
    C35_REG_COMMITTED = 3,
    C35_REG_ABORTED = 4,
    C35_REG_POISONED = 5
};

enum c35_result_state {
    C35_RESULT_ABSENT = 0,
    C35_RESULT_PREPARED = 1,
    C35_RESULT_PRESENT = 2,
    C35_RESULT_ACKED = 3,
    C35_RESULT_CLEARED_BY_RESET = 4,
    C35_RESULT_ABORTED = 5,
    C35_RESULT_POISONED = 6
};

enum c35_reset_state {
    C35_RESET_ABSENT = 0,
    C35_RESET_RECOVERED = 1,
    C35_RESET_POISONED = 2
};

struct c35_txid {
    uint64_t instance_nonce;
    uint64_t uid;
    uint32_t owner_epoch;
    uint32_t generation;
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

typedef enum c35_result (*c35_registration_prepare_fn)(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_request_token *token,
    uint32_t owner_epoch,
    const struct c35_request *request
);
typedef enum c35_result (*c35_registration_commit_fn)(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command
);
typedef enum c35_result (*c35_registration_query_fn)(
    void *context,
    const struct c35_txid *txid,
    enum c35_registration_state *state
);
typedef enum c35_result (*c35_registration_abort_fn)(
    void *context,
    const struct c35_txid *txid
);
typedef enum c35_result (*c35_result_prepare_fn)(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command,
    const struct fwlab_c31_completion_intent *intent,
    struct c35_semantic_result *candidate
);
typedef enum c35_result (*c35_result_query_fn)(
    void *context,
    const struct c35_txid *txid,
    enum c35_result_state *state
);
typedef enum c35_result (*c35_result_abort_fn)(
    void *context,
    const struct c35_txid *txid
);
typedef enum c35_result (*c35_result_ack_fn)(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command
);
typedef enum c35_result (*c35_reset_recover_fn)(
    void *context,
    const struct c35_txid *txid,
    uint32_t old_epoch,
    uint32_t new_epoch
);
typedef enum c35_result (*c35_reset_query_fn)(
    void *context,
    const struct c35_txid *txid,
    enum c35_reset_state *state
);
typedef enum c35_result (*c35_transaction_retire_fn)(
    void *context,
    const struct c35_txid *txid
);
typedef enum c35_result (*c35_binding_finalize_fn)(void *context);
typedef enum c35_result (*c35_binding_snapshot_fn)(
    void *context,
    struct c35_semantic_result *snapshot
);
typedef enum c35_result (*c35_binding_quiescent_fn)(
    void *context,
    bool *quiescent
);
typedef enum c35_result (*c35_binding_cause_query_fn)(
    void *context,
    struct c35_cause_detail *cause
);

struct c35_binding_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    c35_registration_prepare_fn registration_prepare;
    c35_registration_commit_fn registration_commit;
    c35_registration_query_fn registration_query;
    c35_registration_abort_fn registration_abort;
    c35_result_prepare_fn result_prepare;
    c35_result_query_fn result_query;
    c35_result_abort_fn result_abort;
    c35_result_ack_fn result_ack;
    c35_reset_recover_fn reset_recover;
    c35_reset_query_fn reset_query;
    c35_transaction_retire_fn transaction_retire;
    c35_binding_finalize_fn teardown_finalize;
    c35_binding_snapshot_fn semantic_snapshot;
    c35_binding_quiescent_fn quiescent;
    c35_binding_cause_query_fn cause_query;
};

struct c35_binding {
    const struct c35_binding_ops *ops;
    void *context;
};

int c35_txid_equal(const struct c35_txid *left, const struct c35_txid *right);
int c35_binding_valid(const struct c35_binding *binding);

#endif
