/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C42_FAKE_COMMAND_H
#define FWLAB_C42_FAKE_COMMAND_H

#include <stdint.h>

#include "hif/c42.h"

#define C42_FAKE_COMMAND_RECORDS 64u

struct c42_fake_command_script {
    uint32_t completion_result;
    uint32_t acquire_in_progress;
    uint32_t prepare_backpressure;
    uint32_t prepare_delay;
    uint32_t admit_delay;
    uint32_t poll_delay;
    uint32_t consume_commit_delay;
    uint32_t cleanup_delay;
    uint8_t reverse_ready;
    uint8_t cleanup_pending;
    uint8_t reserved[6];
};

struct c42_fake_command_record {
    struct fwlab_hif_prepare_key prepare_key;
    struct fwlab_hif_prepared_token prepared;
    struct fwlab_nvme_command command;
    struct fwlab_hif_command_ticket ticket;
    struct fwlab_nvme_completion_intent intent;
    struct fwlab_hif_completion_lease lease;
    struct fwlab_hif_consume_token consume;
    uint64_t publication_uid;
    uint32_t prepare_queries;
    uint32_t admit_queries;
    uint32_t poll_queries;
    uint32_t consume_queries;
    uint32_t cleanup_queries;
    uint8_t in_use;
    uint8_t admitted;
    uint8_t ready_sent;
    uint8_t leased;
    uint8_t released;
    uint8_t consume_prepared;
    uint8_t consume_committed;
    uint8_t retired;
};

struct c42_fake_command {
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
    uint32_t reset_old_epoch;
    uint32_t teardown_old_epoch;
    uint8_t reset_active;
    uint8_t teardown_active;
    uint8_t reserved[6];
    struct c42_fake_command_script script;
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

#endif
