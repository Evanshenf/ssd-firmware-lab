/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CONTRACTS_HIF_ACTION_H
#define FWLAB_CONTRACTS_HIF_ACTION_H

#include <stdint.h>

#include "fwlab/portable/nvme_types.h"

#define FWLAB_HIF_ACTION_VERSION 1u

enum fwlab_hif_action_kind {
    FWLAB_HIF_ACTION_QUEUE_EFFECT = 1,
    FWLAB_HIF_ACTION_DMA = 2,
    FWLAB_HIF_ACTION_BLOCK = 3
};

enum fwlab_hif_action_disposition {
    FWLAB_HIF_ACTION_ACCEPTED = 0,
    FWLAB_HIF_ACTION_BACKPRESSURE = 1,
    FWLAB_HIF_ACTION_REJECTED = 2
};

enum fwlab_hif_action_terminal_kind {
    FWLAB_HIF_ACTION_SUCCESS = 0,
    FWLAB_HIF_ACTION_CANCELLED = 1,
    FWLAB_HIF_ACTION_FAILED = 2
};

enum fwlab_hif_action_retry {
    FWLAB_HIF_ACTION_RETRY_NONE = 0,
    FWLAB_HIF_ACTION_RETRY_SAME_TOKEN = 1,
    FWLAB_HIF_ACTION_RETRY_LATER = 2,
    FWLAB_HIF_ACTION_RETRY_NEVER = 3
};

struct fwlab_hif_action_token {
    struct fwlab_nvme_command_handle command;
    struct fwlab_nvme_origin_token origin;
    uint64_t action_uid;
    uint32_t generation;
    uint16_t kind;
    uint16_t reserved;
};

/* Typed C4.3/C4.4 requests embed this address-free common envelope. */
struct fwlab_hif_action_envelope {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_hif_action_token token;
    uint64_t cookie;
    uint32_t dependency_ordinal;
    uint32_t requested_units;
    uint32_t reserved1[4];
};

struct fwlab_hif_action_submit_result {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_hif_action_token token;
    uint32_t disposition;
    uint32_t fault_domain;
    uint32_t fault_code;
    uint8_t retry;
    uint8_t effect_class;
    uint8_t reserved1[2];
    uint32_t reserved2[4];
};

struct fwlab_hif_action_terminal {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_hif_action_token token;
    uint64_t cookie;
    uint32_t units_completed;
    uint32_t fault_domain;
    uint32_t fault_code;
    uint8_t terminal_kind;
    uint8_t effect_class;
    uint8_t retry;
    uint8_t reserved1;
    uint32_t reserved2[2];
};

int fwlab_hif_action_token_valid(const struct fwlab_hif_action_token *token);
int fwlab_hif_action_envelope_valid(
    const struct fwlab_hif_action_envelope *envelope
);
int fwlab_hif_action_submit_result_valid(
    const struct fwlab_hif_action_submit_result *result
);
int fwlab_hif_action_terminal_valid(
    const struct fwlab_hif_action_terminal *terminal
);

#endif
