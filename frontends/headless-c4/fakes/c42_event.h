/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C42_FAKE_EVENT_H
#define FWLAB_C42_FAKE_EVENT_H

#include <stdint.h>

#define C42_FAKE_EVENT_MAX 4096u

enum c42_fake_event_provider {
    C42_FAKE_EVENT_COMMAND = 1,
    C42_FAKE_EVENT_MEMORY = 2
};

enum c42_fake_event_call {
    C42_FAKE_CALL_ACTION = 0,
    C42_FAKE_CALL_START = 1,
    C42_FAKE_CALL_QUERY = 2
};

#define C42_FAKE_EVENT_WRITE_VALUE 1u
#define C42_FAKE_EVENT_WRITE_OBJECT 2u
#define C42_FAKE_EVENT_EFFECT_APPLIED 1u
#define C42_FAKE_EVENT_RESPONSE_LOST 2u

struct c42_fake_event {
    uint64_t sequence;
    uint64_t token_uid;
    uint64_t object_uid;
    uint32_t operation;
    uint32_t direct_result;
    uint32_t output_value;
    uint32_t requested_effect;
    uint32_t reported_effect;
    uint32_t applied_effect;
    uint32_t parameter0;
    uint32_t parameter1;
    uint8_t provider;
    uint8_t call_kind;
    uint8_t output_write_mask;
    uint8_t input_structural_valid;
    uint8_t input_record_match;
    uint8_t output_structural_valid;
    uint8_t output_record_match;
    uint8_t committed;
    uint8_t quiescent;
    uint8_t flags;
};

struct c42_fake_event_log {
    uint64_t next_sequence;
    uint32_t count;
    uint32_t overflow;
    struct c42_fake_event events[C42_FAKE_EVENT_MAX];
};

void c42_fake_event_log_init(struct c42_fake_event_log *log);
void c42_fake_event_append(
    struct c42_fake_event_log *log,
    const struct c42_fake_event *event
);

#endif
