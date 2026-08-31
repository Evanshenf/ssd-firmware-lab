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
    const struct c42_fake_event *source)
{
    struct c42_fake_event *event;

    if (log == NULL || source == NULL) {
        return;
    }
    if (log->count >= C42_FAKE_EVENT_MAX || log->next_sequence == 0) {
        log->overflow = 1;
        return;
    }
    event = &log->events[log->count];
    *event = *source;
    event->sequence = log->next_sequence++;
    log->count++;
}
