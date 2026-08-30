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

struct c42_fake_event {
    uint64_t sequence;
    uint64_t token_uid;
    uint32_t operation;
    uint32_t direct_result;
    uint32_t output_value;
    uint8_t provider;
    uint8_t output_written;
    uint8_t identity_valid;
    uint8_t reserved;
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
    uint8_t provider,
    uint32_t operation,
    uint32_t direct_result,
    uint32_t output_value,
    uint8_t output_written,
    uint8_t identity_valid,
    uint64_t token_uid
);

#endif
