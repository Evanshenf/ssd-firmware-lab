/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C42_FAKE_COMMAND_H
#define FWLAB_C42_FAKE_COMMAND_H

#include <stdint.h>

#include "c42_event.h"
#include "hif/c42.h"

#define C42_FAKE_COMMAND_RECORDS 64u
#define C42_FAKE_COMMAND_INJECTIONS 128u
#define C42_FAKE_WRITE_VALUE 1u
#define C42_FAKE_WRITE_OBJECT 2u
#define C42_FAKE_APPLY_EFFECT 1u
#define C42_FAKE_REQUEST_EFFECT 2u
#define C42_FAKE_OBJECT_ZERO 0u
#define C42_FAKE_OBJECT_EXACT 1u
#define C42_FAKE_OBJECT_MISMATCH 2u

enum c42_fake_command_effect {
    C42_FAKE_COMMAND_EFFECT_NONE = 0,
    C42_FAKE_COMMAND_EFFECT_PREPARED = 1,
    C42_FAKE_COMMAND_EFFECT_PREPARE_ABORTED = 2,
    C42_FAKE_COMMAND_EFFECT_ADMITTED = 3,
    C42_FAKE_COMMAND_EFFECT_READY = 4,
    C42_FAKE_COMMAND_EFFECT_LEASED = 5,
    C42_FAKE_COMMAND_EFFECT_RELEASED = 6,
    C42_FAKE_COMMAND_EFFECT_CONSUME_PREPARED = 7,
    C42_FAKE_COMMAND_EFFECT_CONSUME_ABORTED = 8,
    C42_FAKE_COMMAND_EFFECT_CONSUME_COMMITTED = 9,
    C42_FAKE_COMMAND_EFFECT_CONSUME_RETIRED = 10,
    C42_FAKE_COMMAND_EFFECT_RESET_BEGUN = 11,
    C42_FAKE_COMMAND_EFFECT_TEARDOWN_BEGUN = 12
};

enum c42_fake_command_operation {
    C42_FAKE_COMMAND_PREPARE = 1,
    C42_FAKE_COMMAND_PREPARE_ABORT = 2,
    C42_FAKE_COMMAND_ADMIT = 3,
    C42_FAKE_COMMAND_POLL = 4,
    C42_FAKE_COMMAND_COMPLETION_ACQUIRE = 5,
    C42_FAKE_COMMAND_COMPLETION_RELEASE = 6,
    C42_FAKE_COMMAND_CONSUME_PREPARE = 7,
    C42_FAKE_COMMAND_CONSUME_ABORT = 8,
    C42_FAKE_COMMAND_CONSUME_COMMIT = 9,
    C42_FAKE_COMMAND_CONSUME_QUERY = 10,
    C42_FAKE_COMMAND_CONSUME_RETIRE = 11,
    C42_FAKE_COMMAND_RESET_BEGIN = 12,
    C42_FAKE_COMMAND_RESET_QUIESCENT = 13,
    C42_FAKE_COMMAND_TEARDOWN_BEGIN = 14,
    C42_FAKE_COMMAND_TEARDOWN_QUIESCENT = 15
};

struct c42_fake_command_script {
    uint32_t completion_result;
    uint32_t acquire_in_progress;
    uint32_t prepare_backpressure;
    uint32_t prepare_delay;
    uint32_t admit_delay;
    uint32_t poll_delay;
    uint32_t consume_commit_delay;
    uint32_t cleanup_delay;
    uint32_t inject_operation;
    uint32_t inject_result;
    uint32_t inject_value;
    uint32_t inject_count;
    uint8_t reverse_ready;
    uint8_t cleanup_pending;
    uint8_t inject_omit_outputs;
    uint8_t completion_status_code;
    uint8_t completion_status_type;
    uint8_t completion_retry_delay;
    uint8_t completion_more;
    uint8_t completion_do_not_retry;
};

struct c42_fake_command_injection {
    uint32_t operation;
    uint32_t result;
    uint32_t value;
    uint32_t requested_effect;
    uint8_t omit_outputs;
    uint8_t write_mask;
    uint8_t flags;
    uint8_t object_variant;
};

struct c42_fake_command_record {
    struct fwlab_hif_prepare_key prepare_key;
    struct fwlab_hif_prepared_token prepared;
    struct fwlab_hif_admission_key admission_key;
    struct fwlab_nvme_command command;
    struct fwlab_hif_command_ticket ticket;
    struct fwlab_nvme_completion_intent intent;
    struct fwlab_hif_completion_lease lease;
    struct fwlab_hif_consume_token consume;
    uint64_t publication_uid;
    uint64_t release_client_uid;
    uint32_t prepare_queries;
    uint32_t admit_queries;
    uint32_t poll_queries;
    uint32_t consume_queries;
    uint32_t consume_prepare_queries;
    uint32_t cleanup_queries;
    uint8_t in_use;
    uint8_t admit_started;
    uint8_t admitted;
    uint8_t ready_sent;
    uint8_t leased;
    uint8_t release_started;
    uint8_t released;
    uint8_t consume_prepared;
    uint8_t consume_committed;
    uint8_t retired;
};

struct c42_fake_command {
    struct c42_fake_event_log *event_log;
    uint64_t instance_nonce;
    uint64_t next_command_uid;
    uint64_t next_reservation_uid;
    uint64_t next_ticket_uid;
    uint64_t next_lease_uid;
    uint64_t next_consume_uid;
    uint64_t next_ready_sequence;
    uint32_t controller_epoch;
    uint32_t next_generation;
    uint32_t active_limit;
    uint32_t prepare_attempts;
    uint32_t acquire_count;
    uint32_t prepare_abort_call_count;
    uint32_t reset_old_epoch;
    uint32_t teardown_old_epoch;
    uint8_t reset_active;
    uint8_t teardown_active;
    uint8_t reserved[6];
    uint32_t injection_count;
    uint32_t injection_index;
    uint8_t injection_active;
    uint8_t injection_write_mask;
    uint8_t injection_flags;
    uint8_t injection_object_variant;
    uint8_t provider_write_mask;
    uint8_t reserved_event[3];
    uint32_t injection_event_value;
    uint32_t injection_requested_effect;
    uint32_t injection_applied_effect;
    struct c42_fake_command_script script;
    struct c42_fake_command_injection
        injections[C42_FAKE_COMMAND_INJECTIONS];
    struct c42_fake_command_record records[C42_FAKE_COMMAND_RECORDS];
};

void c42_fake_command_init(
    struct c42_fake_command *command,
    uint64_t instance_nonce,
    uint32_t controller_epoch,
    uint32_t active_limit
);
struct fwlab_hif_command_port c42_fake_command_port(
    struct c42_fake_command *command
);
void c42_fake_command_set_script(
    struct c42_fake_command *command,
    const struct c42_fake_command_script *script
);
void c42_fake_command_bind_event_log(
    struct c42_fake_command *command,
    struct c42_fake_event_log *log
);
enum c42_result c42_fake_command_injection_push(
    struct c42_fake_command *command,
    const struct c42_fake_command_injection *injection
);

#endif
