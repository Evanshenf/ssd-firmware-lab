/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CORE_FAKES_C31_FAKE_DMA_H
#define FWLAB_CORE_FAKES_C31_FAKE_DMA_H

#include <stdbool.h>
#include <stdint.h>

#include "fwlab/contracts/c31_provider.h"

#define C31_FAKE_DMA_MAX_CAPABILITIES 8u
#define C31_FAKE_DMA_MAX_SCENARIOS 16u
#define C31_FAKE_DMA_MAX_ACTIVE 8u
#define C31_FAKE_DMA_REGIONS 4u
#define C31_FAKE_DMA_BYTES 256u

struct c31_fake_dma_capability {
    struct fwlab_c31_capability_token token;
    struct fwlab_c31_origin_token origin;
    struct fwlab_c31_command_handle bound_command;
    uint8_t bytes[C31_FAKE_DMA_BYTES];
    uint64_t instance_nonce;
    uint32_t controller_epoch;
    uint32_t length;
    uint8_t direction;
    bool used;
    bool bound;
};

struct c31_fake_dma_scenario {
    struct fwlab_c31_request_token request;
    uint32_t backpressure_count;
    uint32_t delay_polls;
    uint32_t actual_prefix;
    uint32_t reported_prefix;
    uint8_t terminal;
    uint8_t effect_class;
    bool cancel_wins;
};

struct c31_fake_dma_active {
    struct fwlab_c31_provider_request request;
    uint8_t scratch[C31_FAKE_DMA_BYTES];
    uint32_t capability_index;
    uint32_t scenario_index;
    uint32_t remaining_polls;
    bool used;
    bool cancel_requested;
};

struct c31_fake_dma_context {
    struct c31_fake_dma_capability
        capabilities[C31_FAKE_DMA_MAX_CAPABILITIES];
    struct c31_fake_dma_scenario scenarios[C31_FAKE_DMA_MAX_SCENARIOS];
    struct c31_fake_dma_active active[C31_FAKE_DMA_MAX_ACTIVE];
    uint8_t controller[C31_FAKE_DMA_REGIONS][C31_FAKE_DMA_BYTES];
    uint32_t scenario_count;
    uint32_t poll_cursor;
};

void c31_fake_dma_init(struct c31_fake_dma_context *context);

int c31_fake_dma_register(
    struct c31_fake_dma_context *context,
    const struct fwlab_c31_capability_token *token,
    const struct fwlab_c31_origin_token *origin,
    uint64_t instance_nonce,
    uint32_t controller_epoch,
    uint8_t direction,
    const uint8_t *bytes,
    uint32_t length
);

int c31_fake_dma_add(
    struct c31_fake_dma_context *context,
    const struct c31_fake_dma_scenario *scenario
);

struct fwlab_c31_provider c31_fake_dma_provider(
    struct c31_fake_dma_context *context
);

uint8_t *c31_fake_dma_external(
    struct c31_fake_dma_context *context,
    uint32_t capability_index
);

uint8_t *c31_fake_dma_controller(
    struct c31_fake_dma_context *context,
    uint32_t region
);

#endif
