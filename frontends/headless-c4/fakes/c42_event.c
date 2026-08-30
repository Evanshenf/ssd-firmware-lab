/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_event.h"

#include <string.h>

void c42_fake_event_log_init(struct c42_fake_event_log *log)
{
    if (log == NULL) {
        return;
    }
    memset(log, 0, sizeof(*log));
    log->next_sequence = 1;
}

void c42_fake_event_append(
    struct c42_fake_event_log *log,
    uint8_t provider,
    uint32_t operation,
    uint32_t direct_result,
    uint32_t output_value,
    uint8_t output_written,
    uint8_t identity_valid,
    uint64_t token_uid)
{
    struct c42_fake_event *event;

    if (log == NULL) {
        return;
    }
    if (log->count >= C42_FAKE_EVENT_MAX || log->next_sequence == 0) {
        log->overflow = 1;
        return;
    }
    event = &log->events[log->count];
    event->sequence = log->next_sequence++;
    event->token_uid = token_uid;
    event->operation = operation;
    event->direct_result = direct_result;
    event->output_value = output_value;
    event->provider = provider;
    event->output_written = output_written;
    event->identity_valid = identity_valid;
    log->count++;
}
