/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CONTRACTS_HIF_COMMAND_PORT_H
#define FWLAB_CONTRACTS_HIF_COMMAND_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "fwlab/portable/nvme_types.h"

#define FWLAB_HIF_COMMAND_PORT_VERSION 1u

enum fwlab_hif_command_port_result {
    FWLAB_HIF_PORT_OK = 0,
    FWLAB_HIF_PORT_INVALID = 1,
    FWLAB_HIF_PORT_WRONG_STATE = 2,
    FWLAB_HIF_PORT_STALE = 3,
    FWLAB_HIF_PORT_NO_CAPACITY = 4,
    FWLAB_HIF_PORT_IN_PROGRESS = 5,
    FWLAB_HIF_PORT_POISONED = 6,
    FWLAB_HIF_PORT_COUNTER_EXHAUSTED = 7
};

enum fwlab_hif_prepare_disposition {
    FWLAB_HIF_PREPARE_RESERVED = 0,
    FWLAB_HIF_PREPARE_BACKPRESSURE = 1,
    FWLAB_HIF_PREPARE_REJECTED = 2
};

enum fwlab_hif_admission_state {
    FWLAB_HIF_ADMISSION_NOT_STARTED = 0,
    FWLAB_HIF_ADMISSION_COMMITTED = 1,
    FWLAB_HIF_ADMISSION_ABORTED = 2,
    FWLAB_HIF_ADMISSION_POISONED = 3
};

enum fwlab_hif_consume_state {
    FWLAB_HIF_CONSUME_NOT_STARTED = 0,
    FWLAB_HIF_CONSUME_PREPARED = 1,
    FWLAB_HIF_CONSUME_COMMITTED = 2,
    FWLAB_HIF_CONSUME_CLEANUP_PENDING = 3,
    FWLAB_HIF_CONSUME_RETIRED = 4,
    FWLAB_HIF_CONSUME_ABORTED = 5,
    FWLAB_HIF_CONSUME_POISONED = 6
};

struct fwlab_hif_prepare_key {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nvme_origin_token origin;
    uint64_t client_uid;
    uint64_t instance_nonce;
    uint32_t controller_epoch;
    uint32_t client_generation;
    uint16_t queue_class;
    uint16_t worst_case_actions;
    uint32_t reserved1[2];
};

struct fwlab_hif_prepared_token {
    struct fwlab_nvme_command_handle handle;
    struct fwlab_nvme_origin_token origin;
    uint64_t reservation_uid;
    uint32_t generation;
    uint32_t reserved;
};

struct fwlab_hif_prepare_result {
    uint16_t version;
    uint16_t size;
    uint32_t disposition;
    struct fwlab_hif_prepared_token prepared;
    uint32_t fault_domain;
    uint32_t fault_code;
    uint32_t reserved[2];
};

struct fwlab_hif_admission_key {
    struct fwlab_hif_prepared_token prepared;
    uint64_t client_uid;
    uint32_t generation;
    uint32_t reserved;
};

struct fwlab_hif_command_ticket {
    struct fwlab_nvme_command_handle handle;
    struct fwlab_nvme_origin_token origin;
    uint64_t ticket_uid;
    uint32_t generation;
    uint32_t reserved;
};

struct fwlab_hif_ready_event {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_hif_command_ticket ticket;
    uint64_t sequence;
};

struct fwlab_hif_completion_lease {
    struct fwlab_hif_command_ticket ticket;
    uint64_t lease_uid;
    uint32_t generation;
    uint32_t reserved;
};

struct fwlab_hif_consume_token {
    struct fwlab_hif_completion_lease lease;
    uint64_t publication_uid;
    uint64_t consume_uid;
    uint32_t generation;
    uint32_t reserved;
};

typedef enum fwlab_hif_command_port_result
(*fwlab_hif_prepare_fn)(
    void *context,
    const struct fwlab_hif_prepare_key *key,
    struct fwlab_hif_prepare_result *result
);
typedef enum fwlab_hif_command_port_result
(*fwlab_hif_prepare_abort_fn)(
    void *context,
    const struct fwlab_hif_prepared_token *prepared,
    bool *aborted
);
typedef enum fwlab_hif_command_port_result
(*fwlab_hif_admit_fn)(
    void *context,
    const struct fwlab_hif_admission_key *key,
    const struct fwlab_nvme_command *command,
    enum fwlab_hif_admission_state *state,
    struct fwlab_hif_command_ticket *ticket
);
typedef enum fwlab_hif_command_port_result
(*fwlab_hif_poll_fn)(
    void *context,
    uint32_t budget,
    struct fwlab_hif_ready_event *events,
    uint32_t capacity,
    uint32_t *count
);
typedef enum fwlab_hif_command_port_result
(*fwlab_hif_completion_acquire_fn)(
    void *context,
    const struct fwlab_hif_command_ticket *ticket,
    struct fwlab_nvme_completion_intent *intent,
    struct fwlab_hif_completion_lease *lease
);
typedef enum fwlab_hif_command_port_result
(*fwlab_hif_completion_release_fn)(
    void *context,
    const struct fwlab_hif_completion_lease *lease,
    uint64_t client_uid,
    bool *released
);
typedef enum fwlab_hif_command_port_result
(*fwlab_hif_consume_prepare_fn)(
    void *context,
    const struct fwlab_hif_completion_lease *lease,
    uint64_t publication_uid,
    struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state
);
typedef enum fwlab_hif_command_port_result
(*fwlab_hif_consume_control_fn)(
    void *context,
    const struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state
);
typedef enum fwlab_hif_command_port_result
(*fwlab_hif_epoch_begin_fn)(
    void *context,
    uint64_t instance_nonce,
    uint32_t old_epoch
);
typedef enum fwlab_hif_command_port_result
(*fwlab_hif_quiescent_fn)(
    void *context,
    uint64_t instance_nonce,
    uint32_t epoch,
    bool *quiescent
);

struct fwlab_hif_command_port_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    fwlab_hif_prepare_fn prepare_start;
    fwlab_hif_prepare_fn prepare_query;
    fwlab_hif_prepare_abort_fn prepare_abort;
    fwlab_hif_prepare_abort_fn prepare_abort_query;
    fwlab_hif_admit_fn admit_start;
    fwlab_hif_admit_fn admit_query;
    fwlab_hif_poll_fn poll;
    fwlab_hif_completion_acquire_fn completion_acquire;
    fwlab_hif_completion_release_fn completion_release_start;
    fwlab_hif_completion_release_fn completion_release_query;
    fwlab_hif_consume_prepare_fn consume_prepare;
    fwlab_hif_consume_control_fn consume_abort;
    fwlab_hif_consume_control_fn consume_abort_query;
    fwlab_hif_consume_control_fn consume_commit;
    fwlab_hif_consume_control_fn consume_query;
    fwlab_hif_consume_control_fn consume_retire;
    fwlab_hif_epoch_begin_fn reset_begin;
    fwlab_hif_quiescent_fn reset_quiescent;
    fwlab_hif_epoch_begin_fn teardown_begin;
    fwlab_hif_quiescent_fn teardown_quiescent;
};

/*
 * IN_PROGRESS is legal only where the request contains a preknown stable
 * query key (prepare/admit/release/consume/reset/teardown).  Completion
 * acquire is synchronous: it returns a complete one-use lease or no effect.
 */

struct fwlab_hif_command_port {
    const struct fwlab_hif_command_port_ops *ops;
    void *context;
};

int fwlab_hif_command_port_valid(
    const struct fwlab_hif_command_port *port
);
int fwlab_hif_prepared_token_valid(
    const struct fwlab_hif_prepared_token *token
);
int fwlab_hif_command_ticket_valid(
    const struct fwlab_hif_command_ticket *ticket
);
int fwlab_hif_completion_lease_valid(
    const struct fwlab_hif_completion_lease *lease
);
int fwlab_hif_consume_token_valid(
    const struct fwlab_hif_consume_token *token
);

#endif
