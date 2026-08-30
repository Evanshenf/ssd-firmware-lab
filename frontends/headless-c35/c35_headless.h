/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_HEADLESS_H
#define FWLAB_C35_HEADLESS_H

#include "c35_binding.h"
#include "c35_lifecycle_port.h"
#include "c35_publication.h"

#define C35_OPERATION_VERSION 2u
#define C35_OPERATION_SLOTS 2u

enum c35_service_phase {
    C35_SERVICE_READY = 0,
    C35_SERVICE_SUBMIT_RECONCILE = 1,
    C35_SERVICE_COMPLETION_RECONCILE = 2,
    C35_SERVICE_RESETTING = 3,
    C35_SERVICE_TEARING_DOWN = 4,
    C35_SERVICE_FAULTED_CLEANUP = 5,
    C35_SERVICE_POISONED = 6,
    C35_SERVICE_DEAD = 7
};

enum c35_operation_kind {
    C35_OPERATION_SUBMIT = 1,
    C35_OPERATION_COMPLETION = 2,
    C35_OPERATION_RESET = 3,
    C35_OPERATION_TEARDOWN = 4,
    C35_OPERATION_DMA = 5
};

enum c35_commit_state {
    C35_COMMIT_NOT_STARTED = 0,
    C35_COMMIT_IN_PROGRESS = 1,
    C35_COMMIT_COMMITTED = 2,
    C35_COMMIT_ABORTED = 3,
    C35_COMMIT_SUPERSEDED = 4,
    C35_COMMIT_UNKNOWN = 5
};

enum c35_cleanup_state {
    C35_CLEANUP_NONE = 0,
    C35_CLEANUP_PENDING = 1,
    C35_CLEANUP_COMPLETE = 2,
    C35_CLEANUP_POISONED = 3
};

enum c35_call_state {
    C35_CALL_DONE = 0,
    C35_CALL_IN_PROGRESS = 1,
    C35_CALL_INVALID_TOKEN = 2
};

struct c35_operation_token {
    uint64_t instance_nonce;
    uint64_t uid;
    uint32_t generation;
    uint8_t kind;
    uint8_t reserved[3];
};

struct c35_operation_status {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    struct c35_operation_token token;
    uint8_t call_state;
    uint8_t operation_kind;
    uint8_t commit_state;
    uint8_t cleanup_state;
    uint32_t outcome;
    uint32_t service_phase;
    uint32_t internal_phase;
    uint32_t units_used;
    uint32_t cause_domain;
    uint32_t cause_code;
    uint8_t retry_class;
    uint8_t publication_valid;
    uint8_t reserved1[6];
    struct c35_publication publication;
};

enum c35_internal_phase {
    C35_INTERNAL_NONE = 0,
    C35_SUBMIT_PREPARE,
    C35_SUBMIT_C31,
    C35_SUBMIT_C31_QUERY,
    C35_SUBMIT_COMMIT,
    C35_SUBMIT_QUERY,
    C35_SUBMIT_BINDING_ABORT,
    C35_SUBMIT_ABORT_REQUEST,
    C35_SUBMIT_ABORT_REQUEST_QUERY,
    C35_SUBMIT_ABORT_ACQUIRE,
    C35_SUBMIT_ABORT_ACQUIRE_QUERY,
    C35_SUBMIT_ABORT_CONSUME,
    C35_SUBMIT_ABORT_ACK,
    C35_COMPLETE_WAIT_READY,
    C35_COMPLETE_ACQUIRE,
    C35_COMPLETE_ACQUIRE_QUERY,
    C35_COMPLETE_COPY,
    C35_COMPLETE_COPY_QUERY,
    C35_COMPLETE_RELEASE,
    C35_COMPLETE_RELEASE_QUERY,
    C35_COMPLETE_RESULT_ABORT,
    C35_COMPLETE_RESULT_ABORT_QUERY,
    C35_COMPLETE_CONSUME,
    C35_COMPLETE_CONSUME_QUERY,
    C35_COMPLETE_ACK,
    C35_COMPLETE_ACK_QUERY,
    C35_RESET_BEGIN,
    C35_RESET_DRAIN,
    C35_RESET_ACK,
    C35_RESET_RECOVER,
    C35_RESET_RECOVER_QUERY,
    C35_RESET_QUIESCENT,
    C35_TEARDOWN_ALIGN,
    C35_TEARDOWN_BEGIN,
    C35_TEARDOWN_DRAIN,
    C35_TEARDOWN_ACK,
    C35_TEARDOWN_BINDING,
    C35_TEARDOWN_QUIESCENT,
    C35_INTERNAL_DONE,
    C35_INTERNAL_POISONED
};

struct c35_operation_record {
    uint8_t used;
    uint8_t finished;
    uint8_t kind;
    uint8_t command_valid;
    uint8_t abort_ticket_valid;
    uint8_t lease_valid;
    uint8_t binding_retire;
    uint8_t superseded;
    uint8_t consume_attempted;
    uint8_t ack_attempted;
    uint8_t release_attempted;
    uint8_t retry_class;
    uint32_t phase;
    uint32_t outcome;
    uint32_t commit_state;
    uint32_t cleanup_state;
    uint32_t cause_domain;
    uint32_t cause_code;
    uint32_t units_used;
    uint32_t old_epoch;
    uint32_t new_epoch;
    struct c35_operation_token token;
    struct c35_txid registration_txid;
    struct c35_txid result_txid;
    struct fwlab_c31_request_token request_token;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_abort_ticket abort_ticket;
    enum fwlab_c31_abort_outcome abort_outcome;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;
    struct c35_request request;
    struct c35_semantic_result semantic;
    struct c35_publication publication;
};

struct c35_headless {
    struct c35_lifecycle_port lifecycle;
    struct c35_binding binding;
    uint64_t instance_nonce;
    uint64_t next_request;
    uint64_t next_operation_uid;
    uint64_t next_control_uid;
    uint64_t request_uid_limit;
    uint64_t operation_uid_limit;
    uint64_t control_uid_limit;
    uint32_t owner_epoch;
    uint32_t controller_epoch_limit;
    uint8_t actor;
    uint8_t service_phase;
    uint8_t active_slot;
    uint8_t control_active;
    uint8_t previous_control_used;
    uint8_t reserved[3];
    struct c35_operation_record operation[C35_OPERATION_SLOTS];
    struct c35_operation_record control;
    struct c35_operation_record previous_control;
};

struct c35_submission {
    struct fwlab_c31_request_token request;
    struct fwlab_c31_command_handle command;
    uint32_t owner_epoch;
};

enum c35_result c35_headless_init(
    struct c35_headless *headless,
    const struct c35_lifecycle_port *lifecycle,
    const struct c35_binding *binding,
    uint64_t instance_nonce,
    uint32_t owner_epoch,
    uint32_t controller_epoch_limit,
    uint64_t request_uid_limit,
    uint64_t operation_uid_limit,
    uint8_t actor
);

enum c35_result c35_submit_start(
    struct c35_headless *headless,
    const struct c35_request *request,
    struct c35_operation_token *token
);
enum c35_result c35_completion_start(
    struct c35_headless *headless,
    const struct fwlab_c31_command_handle *command,
    struct c35_operation_token *token
);
enum c35_result c35_reset_start(
    struct c35_headless *headless,
    struct c35_operation_token *token
);
enum c35_result c35_teardown_start(
    struct c35_headless *headless,
    struct c35_operation_token *token
);
enum c35_result c35_operation_progress(
    struct c35_headless *headless,
    const struct c35_operation_token *token,
    uint32_t budget,
    struct c35_operation_status *status
);
enum c35_result c35_operation_query(
    const struct c35_headless *headless,
    const struct c35_operation_token *token,
    struct c35_operation_status *status
);
enum c35_result c35_operation_finalize(
    const struct c35_headless *headless,
    const struct c35_operation_token *token,
    struct c35_operation_status *status
);
enum c35_result c35_operation_retire(
    struct c35_headless *headless,
    const struct c35_operation_token *token
);

enum c35_result c35_headless_submit(
    struct c35_headless *headless,
    const struct c35_request *request,
    struct fwlab_c31_command_handle *command
);
enum c35_result c35_headless_submit_observed(
    struct c35_headless *headless,
    const struct c35_request *request,
    struct c35_submission *submission
);
enum c35_result c35_headless_complete_observed(
    struct c35_headless *headless,
    const struct fwlab_c31_command_handle *command,
    struct c35_semantic_result *semantic,
    struct fwlab_c31_completion_intent *intent,
    struct c35_publication *publication
);
enum c35_result c35_headless_complete(
    struct c35_headless *headless,
    const struct fwlab_c31_command_handle *command,
    struct c35_semantic_result *semantic,
    struct fwlab_c31_completion_intent *intent
);
enum c35_result c35_headless_pump_quiescent(
    struct c35_headless *headless,
    uint32_t limit
);
enum c35_result c35_headless_reset_observed(
    struct c35_headless *headless,
    uint32_t limit,
    struct c35_publication *publication
);
enum c35_result c35_headless_reset(
    struct c35_headless *headless,
    uint32_t limit
);
enum c35_result c35_headless_teardown_observed(
    struct c35_headless *headless,
    uint32_t limit,
    struct c35_publication *publication
);
enum c35_result c35_headless_teardown(
    struct c35_headless *headless,
    uint32_t limit
);

#endif
