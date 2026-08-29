/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CORE_FAKES_C31_FAKE_NFC_H
#define FWLAB_CORE_FAKES_C31_FAKE_NFC_H

#include <stdbool.h>
#include <stdint.h>

#include "fwlab/contracts/c31_provider.h"

#define C31_FAKE_NFC_MAX_SCENARIOS 16u
#define C31_FAKE_NFC_MAX_ACTIVE 16u

struct c31_fake_nfc_scenario {
    struct fwlab_c31_request_token request;
    struct fwlab_c31_fault fault;
    uint32_t backpressure_count;
    uint32_t delay_polls;
    uint8_t terminal;
    bool cancel_wins;
};

struct c31_fake_nfc_active {
    struct fwlab_c31_provider_request request;
    uint32_t scenario_index;
    uint32_t remaining_polls;
    bool used;
    bool cancel_requested;
};

struct c31_fake_nfc_context {
    struct c31_fake_nfc_scenario scenarios[C31_FAKE_NFC_MAX_SCENARIOS];
    struct c31_fake_nfc_active active[C31_FAKE_NFC_MAX_ACTIVE];
    uint32_t scenario_count;
    uint32_t poll_cursor;
};

void c31_fake_nfc_init(struct c31_fake_nfc_context *context);

int c31_fake_nfc_add(
    struct c31_fake_nfc_context *context,
    const struct c31_fake_nfc_scenario *scenario
);

struct fwlab_c31_provider c31_fake_nfc_provider(
    struct c31_fake_nfc_context *context
);

#endif
