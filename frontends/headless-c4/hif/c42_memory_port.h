/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C42_MEMORY_PORT_H
#define FWLAB_C42_MEMORY_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define C42_MEMORY_PORT_VERSION 2u

enum c42_memory_role {
    C42_MEMORY_SQ_READ = 1,
    C42_MEMORY_CQ_PUBLISH = 2
};

enum c42_memory_result {
    C42_MEMORY_OK = 0,
    C42_MEMORY_INVALID = 1,
    C42_MEMORY_STALE = 2,
    C42_MEMORY_NO_EFFECT = 3,
    C42_MEMORY_EXACT_PREFIX = 4,
    C42_MEMORY_FULL = 5,
    C42_MEMORY_UNKNOWN = 6,
    C42_MEMORY_POISONED = 7,
    C42_MEMORY_IN_PROGRESS = 8,
    C42_MEMORY_RETIRED = 9
};

struct c42_queue_memory_cap {
    uint64_t instance_nonce;
    uint64_t owner_epoch;
    uint64_t memory_uid;
    uint32_t controller_epoch;
    uint32_t ring_generation;
    uint32_t mapping_generation;
    uint32_t exact_bytes;
    uint16_t queue_id;
    uint8_t role;
    uint8_t reserved;
};

struct c42_memory_token {
    uint64_t instance_nonce;
    uint64_t uid;
    uint32_t generation;
    uint16_t kind;
    uint16_t reserved;
};

struct c42_memory_status {
    struct c42_memory_token token;
    uint32_t result;
    uint32_t prefix;
    uint8_t committed;
    uint8_t quiescent;
    uint8_t reserved[6];
};

/*
 * Transactional operations return C42_MEMORY_OK with this exact status and
 * place their logical outcome in status.result.  C42_MEMORY_IN_PROGRESS is
 * the only direct non-error return and proves no unreported external effect;
 * every retry/query uses the caller-minted token.  Other direct returns are
 * capability or provider-contract failures.
 */

typedef enum c42_memory_result (*c42_memory_validate_fn)(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint8_t role,
    uint32_t exact_bytes
);
typedef enum c42_memory_result (*c42_memory_capture_fn)(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    uint8_t *output,
    size_t output_size
);
typedef enum c42_memory_result (*c42_memory_scrub_fn)(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth,
    uint8_t inverse_phase,
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status
);
typedef enum c42_memory_result (*c42_memory_body_fn)(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    const uint8_t expected[16],
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status
);
typedef enum c42_memory_result (*c42_memory_marker_fn)(
    void *context,
    const struct c42_queue_memory_cap *capability,
    uint16_t slot,
    uint8_t marker,
    const struct c42_memory_token *client_token,
    struct c42_memory_status *status
);
typedef enum c42_memory_result (*c42_memory_epoch_fn)(
    void *context,
    uint64_t instance_nonce,
    uint32_t old_epoch
);
typedef enum c42_memory_result (*c42_memory_quiescent_fn)(
    void *context,
    uint64_t instance_nonce,
    uint32_t epoch,
    bool *quiescent
);

struct c42_memory_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    c42_memory_validate_fn validate;
    c42_memory_capture_fn capture;
    c42_memory_scrub_fn scrub_start;
    c42_memory_scrub_fn scrub_query;
    c42_memory_scrub_fn scrub_abort;
    c42_memory_scrub_fn scrub_retire_start;
    c42_memory_scrub_fn scrub_retire_query;
    c42_memory_body_fn body_start;
    c42_memory_body_fn body_query;
    c42_memory_marker_fn marker_start;
    c42_memory_marker_fn marker_query;
    c42_memory_epoch_fn reset_begin;
    c42_memory_quiescent_fn reset_quiescent;
    c42_memory_epoch_fn teardown_begin;
    c42_memory_quiescent_fn teardown_quiescent;
};

struct c42_memory_port {
    const struct c42_memory_ops *ops;
    void *context;
};

int c42_memory_port_valid(const struct c42_memory_port *port);
int c42_queue_memory_cap_valid(
    const struct c42_queue_memory_cap *capability
);
int c42_memory_token_valid(const struct c42_memory_token *token);

#endif
