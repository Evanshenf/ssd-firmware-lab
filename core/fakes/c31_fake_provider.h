/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CORE_FAKES_C31_FAKE_PROVIDER_H
#define FWLAB_CORE_FAKES_C31_FAKE_PROVIDER_H

#include <stdbool.h>
#include <stdint.h>

#include "fwlab/contracts/c31_provider.h"

#define C31_FAKE_MAX_SCENARIOS 32u
#define C31_FAKE_MAX_ACTIVE 32u

struct c31_fake_scenario {
    struct fwlab_c31_request_token request;
    struct fwlab_c31_fault submit_fault;
    struct fwlab_c31_fault terminal_fault;
    uint32_t backpressure_count;
    uint32_t delay_polls;
    uint8_t submit_disposition;
    uint8_t terminal;
    bool cancel_wins;
    bool duplicate_terminal;
};

struct c31_fake_active {
    struct fwlab_c31_provider_request request;
    struct fwlab_c31_provider_event event;
    uint32_t scenario_index;
    uint32_t remaining_polls;
    bool used;
    bool cancel_requested;
    bool emitted_once;
    bool duplicate_pending;
};

struct c31_fake_provider_context {
    struct c31_fake_scenario scenarios[C31_FAKE_MAX_SCENARIOS];
    struct c31_fake_active active[C31_FAKE_MAX_ACTIVE];
    uint32_t scenario_count;
    uint32_t poll_cursor;
    uint8_t provider_kind;
};

void c31_fake_provider_init(
    struct c31_fake_provider_context *context,
    uint8_t provider_kind
);

int c31_fake_provider_add(
    struct c31_fake_provider_context *context,
    const struct c31_fake_scenario *scenario
);

struct fwlab_c31_provider c31_fake_provider(
    struct c31_fake_provider_context *context
);

uint32_t c31_fake_provider_active(
    const struct c31_fake_provider_context *context
);

#endif
