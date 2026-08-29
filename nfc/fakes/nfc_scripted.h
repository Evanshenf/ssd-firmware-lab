/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_NFC_FAKES_SCRIPTED_H
#define FWLAB_NFC_FAKES_SCRIPTED_H

#include <stdbool.h>
#include <stdint.h>

#include "fwlab/contracts/nfc_provider.h"

#define C33_SCRIPTED_MAX_SCENARIOS 16u
#define C33_SCRIPTED_MAX_ACTIVE 16u

struct c33_scripted_scenario {
    uint64_t request_cookie;
    struct fwlab_nfc_completion completion;
    uint32_t backpressure_count;
    uint32_t delay_steps;
    uint8_t cancel_wins;
    uint8_t reserved[3];
};

struct c33_scripted_active {
    uint8_t used;
    uint8_t pending;
    uint8_t cancel_requested;
    uint8_t reserved0;
    uint32_t scenario_index;
    uint32_t remaining_steps;
    struct fwlab_nfc_request request;
};

struct c33_scripted_nfc {
    struct c33_scripted_scenario scenario[C33_SCRIPTED_MAX_SCENARIOS];
    struct c33_scripted_active active[C33_SCRIPTED_MAX_ACTIVE];
    uint32_t scenario_count;
    uint32_t poll_cursor;
    uint64_t virtual_now;
};

void c33_scripted_init(struct c33_scripted_nfc *fake);

int c33_scripted_add(
    struct c33_scripted_nfc *fake,
    const struct c33_scripted_scenario *scenario
);

struct fwlab_nfc_provider c33_scripted_provider(
    struct c33_scripted_nfc *fake
);

#endif
