/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C42_FAKE_MEMORY_H
#define FWLAB_C42_FAKE_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#include "c42_event.h"
#include "hif/c42.h"

#define C42_FAKE_MEMORY_SCRIPT_MAX 128u
#define C42_FAKE_MEMORY_DIRECT_MAX 128u

enum c42_fake_memory_operation {
    C42_FAKE_MEMORY_SCRUB = 1,
    C42_FAKE_MEMORY_BODY = 2,
    C42_FAKE_MEMORY_MARKER = 3,
    C42_FAKE_MEMORY_SCRUB_RETIRE = 4,
    C42_FAKE_MEMORY_VALIDATE = 5,
    C42_FAKE_MEMORY_CAPTURE = 6,
    C42_FAKE_MEMORY_RESET_BEGIN = 7,
    C42_FAKE_MEMORY_RESET_QUIESCENT = 8,
    C42_FAKE_MEMORY_TEARDOWN_BEGIN = 9,
    C42_FAKE_MEMORY_TEARDOWN_QUIESCENT = 10,
    C42_FAKE_MEMORY_SCRUB_ABORT = 11
};

struct c42_fake_memory_outcome {
    uint8_t operation;
    uint8_t effect;
    uint8_t prefix;
    uint8_t committed;
    uint8_t status_committed;
    uint8_t status_quiescent;
    uint8_t status_override;
    uint8_t reserved;
};

struct c42_fake_memory_direct_injection {
    uint8_t operation;
    uint8_t result;
    uint8_t omit_status;
    uint8_t write_status;
    uint8_t apply_effect;
    uint8_t logical_effect;
    uint8_t applied_effect;
    uint8_t prefix;
    uint8_t committed;
    uint8_t quiescent;
    uint8_t reserved;
};

struct c42_fake_memory_mapping {
    struct c42_queue_memory_cap capability;
    uint16_t depth;
    uint8_t present;
    uint8_t reserved;
};

struct c42_fake_memory_operation_record {
    struct c42_memory_token token;
    struct c42_queue_memory_cap capability;
    uint16_t slot;
    uint16_t prefix;
    uint8_t kind;
    uint8_t active;
    uint8_t committed;
    uint8_t retired;
    uint8_t expected[C42_CQE_BYTES];
};

struct c42_fake_memory {
    struct c42_fake_event_log *event_log;
    uint64_t instance_nonce;
    uint64_t owner_epoch;
    uint32_t controller_epoch;
    uint32_t capture_count;
    uint32_t body_call_count;
    uint32_t marker_call_count;
    uint32_t scrub_call_count;
    uint32_t teardown_old_epoch;
    uint32_t script_count;
    uint32_t script_index;
    uint32_t direct_count;
    uint32_t direct_index;
    uint8_t reset_active;
    uint8_t teardown_active;
    uint8_t reserved[6];
    struct c42_fake_memory_mapping sq_map[C42_MAX_QUEUE_PAIRS];
    struct c42_fake_memory_mapping cq_map[C42_MAX_QUEUE_PAIRS];
    struct c42_fake_memory_operation_record scrub[C42_MAX_QUEUE_PAIRS];
    struct c42_fake_memory_operation_record publication[C42_MAX_QUEUE_PAIRS];
    struct c42_fake_memory_outcome script[C42_FAKE_MEMORY_SCRIPT_MAX];
    struct c42_fake_memory_direct_injection direct[C42_FAKE_MEMORY_DIRECT_MAX];
    uint8_t sq[C42_MAX_QUEUE_PAIRS][C42_MAX_QUEUE_DEPTH][C42_SQE_BYTES];
    uint8_t cq[C42_MAX_QUEUE_PAIRS][C42_MAX_QUEUE_DEPTH][C42_CQE_BYTES];
};

void c42_fake_memory_init(
    struct c42_fake_memory *memory,
    uint64_t instance_nonce,
    uint64_t owner_epoch,
    uint32_t controller_epoch
);
struct c42_memory_port c42_fake_memory_port(
    struct c42_fake_memory *memory
);
void c42_fake_memory_bind_event_log(
    struct c42_fake_memory *memory,
    struct c42_fake_event_log *log
);
enum c42_result c42_fake_memory_map(
    struct c42_fake_memory *memory,
    const struct c42_queue_memory_cap *capability,
    uint16_t depth
);
enum c42_result c42_fake_memory_write_sqe(
    struct c42_fake_memory *memory,
    uint16_t queue_id,
    uint16_t slot,
    const uint8_t bytes[C42_SQE_BYTES]
);
enum c42_result c42_fake_memory_read_cqe(
    const struct c42_fake_memory *memory,
    uint16_t queue_id,
    uint16_t slot,
    uint8_t bytes[C42_CQE_BYTES]
);
enum c42_result c42_fake_memory_script_push(
    struct c42_fake_memory *memory,
    const struct c42_fake_memory_outcome *outcome
);
enum c42_result c42_fake_memory_direct_push(
    struct c42_fake_memory *memory,
    const struct c42_fake_memory_direct_injection *injection
);

#endif
