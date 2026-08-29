/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c31_fake_dma.h"

#include <string.h>

static int pair_equal(const uint64_t left[2], const uint64_t right[2])
{
    return left[0] == right[0] && left[1] == right[1];
}

static int command_equal(
    const struct fwlab_c31_command_handle *left,
    const struct fwlab_c31_command_handle *right
)
{
    return left->instance_nonce == right->instance_nonce &&
           left->command_uid == right->command_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->slot == right->slot &&
           left->slot_generation == right->slot_generation;
}

static int operation_equal(
    const struct fwlab_c31_operation_token *left,
    const struct fwlab_c31_operation_token *right
)
{
    return command_equal(&left->command, &right->command) &&
           left->cookie == right->cookie &&
           left->operation_generation == right->operation_generation &&
           left->reserved == right->reserved;
}

static struct fwlab_c31_provider_submit_result reject_with(uint32_t reason)
{
    struct fwlab_c31_provider_submit_result result;

    memset(&result, 0, sizeof(result));
    result.disposition = FWLAB_C31_PROVIDER_REJECTED;
    result.fault.domain = FWLAB_C31_FAULT_DMA;
    result.fault.retry_class = FWLAB_C31_RETRY_NEVER;
    result.fault.effect_class = FWLAB_C31_EFFECT_NONE;
    result.fault.reason = reason;
    return result;
}

static int range_valid(uint32_t offset, uint32_t length, uint32_t capacity)
{
    return offset <= capacity && length <= capacity - offset;
}

static struct fwlab_c31_provider_submit_result dma_try_submit(
    void *opaque,
    const struct fwlab_c31_provider_request *request
)
{
    struct c31_fake_dma_context *context = opaque;
    struct fwlab_c31_provider_submit_result result;
    uint32_t scenario_index;
    uint32_t capability_index;
    uint32_t active_index;

    if (context == NULL || request == NULL ||
        request->version != FWLAB_C31_PROVIDER_CONTRACT_VERSION ||
        request->size != sizeof(*request) || request->reserved0 != 0 ||
        request->reserved1 != 0 ||
        request->provider_kind != FWLAB_C31_PROVIDER_DMA) {
        return reject_with(FWLAB_C31_REASON_INVARIANT);
    }
    for (scenario_index = 0; scenario_index < context->scenario_count;
         ++scenario_index) {
        if (pair_equal(context->scenarios[scenario_index].request.word,
                       request->request.word)) {
            break;
        }
    }
    if (scenario_index == context->scenario_count) {
        return reject_with(FWLAB_C31_REASON_PROVIDER_REJECTED);
    }
    if (context->scenarios[scenario_index].backpressure_count > 0) {
        --context->scenarios[scenario_index].backpressure_count;
        memset(&result, 0, sizeof(result));
        result.disposition = FWLAB_C31_PROVIDER_BACKPRESSURE;
        return result;
    }
    for (capability_index = 0;
         capability_index < C31_FAKE_DMA_MAX_CAPABILITIES;
         ++capability_index) {
        struct c31_fake_dma_capability *capability =
            &context->capabilities[capability_index];

        if (capability->used &&
            pair_equal(capability->token.word, request->capability.word)) {
            break;
        }
    }
    if (capability_index == C31_FAKE_DMA_MAX_CAPABILITIES) {
        return reject_with(FWLAB_C31_REASON_CAPABILITY);
    }
    {
        struct c31_fake_dma_capability *capability =
            &context->capabilities[capability_index];

        if (capability->instance_nonce !=
                request->operation.command.instance_nonce ||
            capability->controller_epoch !=
                request->operation.command.controller_epoch ||
            !pair_equal(capability->origin.word, request->origin.word) ||
            capability->direction != request->dma_direction) {
            return reject_with(FWLAB_C31_REASON_CAPABILITY);
        }
        if (!range_valid(request->capability_offset, request->length,
                         capability->length) ||
            request->controller_region >= C31_FAKE_DMA_REGIONS ||
            !range_valid(request->controller_offset, request->length,
                         C31_FAKE_DMA_BYTES)) {
            return reject_with(FWLAB_C31_REASON_RANGE);
        }
        if (capability->bound &&
            !command_equal(&capability->bound_command,
                           &request->operation.command)) {
            return reject_with(FWLAB_C31_REASON_CAPABILITY);
        }
    }
    for (active_index = 0; active_index < C31_FAKE_DMA_MAX_ACTIVE;
         ++active_index) {
        if (!context->active[active_index].used) {
            struct c31_fake_dma_active *active =
                &context->active[active_index];
            struct c31_fake_dma_capability *capability =
                &context->capabilities[capability_index];

            memset(active, 0, sizeof(*active));
            active->used = true;
            active->request = *request;
            active->capability_index = capability_index;
            active->scenario_index = scenario_index;
            active->remaining_polls =
                context->scenarios[scenario_index].delay_polls;
            capability->bound = true;
            capability->bound_command = request->operation.command;
            if (request->dma_direction == FWLAB_C31_DMA_TO_CONTROLLER) {
                memcpy(active->scratch,
                       capability->bytes + request->capability_offset,
                       request->length);
            } else {
                memcpy(active->scratch,
                       context->controller[request->controller_region] +
                           request->controller_offset,
                       request->length);
            }
            memset(&result, 0, sizeof(result));
            result.disposition = FWLAB_C31_PROVIDER_ACCEPTED;
            return result;
        }
    }
    memset(&result, 0, sizeof(result));
    result.disposition = FWLAB_C31_PROVIDER_BACKPRESSURE;
    return result;
}

static enum fwlab_c31_api_result dma_cancel(
    void *opaque,
    const struct fwlab_c31_operation_token *operation
)
{
    struct c31_fake_dma_context *context = opaque;
    uint32_t index;

    if (context == NULL || operation == NULL || operation->reserved != 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    for (index = 0; index < C31_FAKE_DMA_MAX_ACTIVE; ++index) {
        if (context->active[index].used &&
            operation_equal(&context->active[index].request.operation,
                            operation)) {
            context->active[index].cancel_requested = true;
            break;
        }
    }
    return FWLAB_C31_API_OK;
}

static void apply_and_build_event(
    struct c31_fake_dma_context *context,
    struct c31_fake_dma_active *active,
    struct fwlab_c31_provider_event *event
)
{
    const struct c31_fake_dma_scenario *scenario =
        &context->scenarios[active->scenario_index];
    struct c31_fake_dma_capability *capability =
        &context->capabilities[active->capability_index];
    uint32_t terminal = scenario->terminal;
    uint32_t actual = 0;
    uint32_t reported = 0;
    uint8_t effect = scenario->effect_class;

    if (active->cancel_requested && scenario->cancel_wins) {
        terminal = FWLAB_C31_PROVIDER_CANCELLED;
    }
    if (terminal == FWLAB_C31_PROVIDER_SUCCESS) {
        effect = FWLAB_C31_EFFECT_FULL;
    }
    if (active->request.dma_direction == FWLAB_C31_DMA_TO_CONTROLLER) {
        if (terminal == FWLAB_C31_PROVIDER_SUCCESS) {
            memcpy(context->controller[active->request.controller_region] +
                       active->request.controller_offset,
                   active->scratch, active->request.length);
        } else {
            effect = FWLAB_C31_EFFECT_NONE;
        }
    } else {
        if (effect == FWLAB_C31_EFFECT_FULL) {
            actual = active->request.length;
        } else if (effect == FWLAB_C31_EFFECT_EXACT_PREFIX ||
                   effect == FWLAB_C31_EFFECT_UNKNOWN_PREFIX) {
            actual = scenario->actual_prefix;
        }
        if (actual > active->request.length) {
            actual = active->request.length;
        }
        if (actual > 0) {
            memcpy(capability->bytes + active->request.capability_offset,
                   active->scratch, actual);
        }
        if (effect == FWLAB_C31_EFFECT_EXACT_PREFIX) {
            reported = actual;
        } else if (effect == FWLAB_C31_EFFECT_UNKNOWN_PREFIX) {
            reported = scenario->reported_prefix;
            if (reported < actual) {
                reported = actual;
            }
            if (reported > active->request.length) {
                reported = active->request.length;
            }
        }
    }

    memset(event, 0, sizeof(*event));
    event->version = FWLAB_C31_PROVIDER_CONTRACT_VERSION;
    event->size = (uint16_t)sizeof(*event);
    event->operation = active->request.operation;
    event->terminal = terminal;
    event->fault.effect_class = effect;
    event->fault.prefix_length = reported;
    if (terminal != FWLAB_C31_PROVIDER_SUCCESS) {
        event->fault.domain = FWLAB_C31_FAULT_DMA;
        event->fault.retry_class = FWLAB_C31_RETRY_NEVER;
        event->fault.reason = terminal == FWLAB_C31_PROVIDER_CANCELLED ?
                              FWLAB_C31_REASON_CANCELLED :
                              FWLAB_C31_REASON_PROVIDER_FAILED;
    }
}

static enum fwlab_c31_api_result dma_poll(
    void *opaque,
    uint32_t budget,
    struct fwlab_c31_provider_event *events,
    uint32_t event_capacity,
    uint32_t *event_count
)
{
    struct c31_fake_dma_context *context = opaque;
    uint32_t scanned;

    if (context == NULL || events == NULL || event_count == NULL ||
        budget == 0 || event_capacity == 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    *event_count = 0;
    for (scanned = 0; scanned < C31_FAKE_DMA_MAX_ACTIVE; ++scanned) {
        uint32_t index = (context->poll_cursor + scanned) %
                         C31_FAKE_DMA_MAX_ACTIVE;
        struct c31_fake_dma_active *active = &context->active[index];

        if (!active->used) {
            continue;
        }
        context->poll_cursor = (index + 1) % C31_FAKE_DMA_MAX_ACTIVE;
        if (active->remaining_polls > 0) {
            --active->remaining_polls;
            return FWLAB_C31_API_OK;
        }
        apply_and_build_event(context, active, &events[0]);
        active->used = false;
        *event_count = 1;
        return FWLAB_C31_API_OK;
    }
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result dma_reset_begin(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch
)
{
    struct c31_fake_dma_context *context = opaque;
    uint32_t index;

    if (context == NULL || instance_nonce == 0 || old_epoch == 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    for (index = 0; index < C31_FAKE_DMA_MAX_ACTIVE; ++index) {
        struct c31_fake_dma_active *active = &context->active[index];

        if (active->used &&
            active->request.operation.command.instance_nonce ==
                instance_nonce &&
            active->request.operation.command.controller_epoch == old_epoch) {
            active->cancel_requested = true;
        }
    }
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result dma_quiescent(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch,
    bool *quiescent
)
{
    struct c31_fake_dma_context *context = opaque;
    uint32_t index;

    if (context == NULL || quiescent == NULL || instance_nonce == 0 ||
        old_epoch == 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    *quiescent = true;
    for (index = 0; index < C31_FAKE_DMA_MAX_ACTIVE; ++index) {
        const struct c31_fake_dma_active *active = &context->active[index];

        if (active->used &&
            active->request.operation.command.instance_nonce ==
                instance_nonce &&
            active->request.operation.command.controller_epoch == old_epoch) {
            *quiescent = false;
            break;
        }
    }
    return FWLAB_C31_API_OK;
}

static const struct fwlab_c31_provider_ops dma_ops = {
    .version = FWLAB_C31_PROVIDER_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_c31_provider_ops),
    .reserved = 0,
    .try_submit = dma_try_submit,
    .cancel = dma_cancel,
    .poll = dma_poll,
    .reset_begin = dma_reset_begin,
    .quiescent = dma_quiescent,
};

void c31_fake_dma_init(struct c31_fake_dma_context *context)
{
    memset(context, 0, sizeof(*context));
}

int c31_fake_dma_register(
    struct c31_fake_dma_context *context,
    const struct fwlab_c31_capability_token *token,
    const struct fwlab_c31_origin_token *origin,
    uint64_t instance_nonce,
    uint32_t controller_epoch,
    uint8_t direction,
    const uint8_t *bytes,
    uint32_t length
)
{
    uint32_t index;

    if (context == NULL || token == NULL || origin == NULL ||
        instance_nonce == 0 || controller_epoch == 0 ||
        (direction != FWLAB_C31_DMA_TO_CONTROLLER &&
         direction != FWLAB_C31_DMA_FROM_CONTROLLER) ||
        length == 0 || length > C31_FAKE_DMA_BYTES || bytes == NULL) {
        return -1;
    }
    for (index = 0; index < C31_FAKE_DMA_MAX_CAPABILITIES; ++index) {
        struct c31_fake_dma_capability *capability =
            &context->capabilities[index];

        if (!capability->used) {
            memset(capability, 0, sizeof(*capability));
            capability->used = true;
            capability->token = *token;
            capability->origin = *origin;
            capability->instance_nonce = instance_nonce;
            capability->controller_epoch = controller_epoch;
            capability->direction = direction;
            capability->length = length;
            memcpy(capability->bytes, bytes, length);
            return (int)index;
        }
    }
    return -1;
}

int c31_fake_dma_add(
    struct c31_fake_dma_context *context,
    const struct c31_fake_dma_scenario *scenario
)
{
    if (context == NULL || scenario == NULL ||
        context->scenario_count >= C31_FAKE_DMA_MAX_SCENARIOS ||
        scenario->terminal > FWLAB_C31_PROVIDER_FAILED ||
        scenario->effect_class > FWLAB_C31_EFFECT_UNKNOWN_PREFIX) {
        return 0;
    }
    context->scenarios[context->scenario_count++] = *scenario;
    return 1;
}

struct fwlab_c31_provider c31_fake_dma_provider(
    struct c31_fake_dma_context *context
)
{
    struct fwlab_c31_provider provider;

    provider.ops = &dma_ops;
    provider.context = context;
    return provider;
}

uint8_t *c31_fake_dma_external(
    struct c31_fake_dma_context *context,
    uint32_t capability_index
)
{
    if (context == NULL ||
        capability_index >= C31_FAKE_DMA_MAX_CAPABILITIES ||
        !context->capabilities[capability_index].used) {
        return NULL;
    }
    return context->capabilities[capability_index].bytes;
}

uint8_t *c31_fake_dma_controller(
    struct c31_fake_dma_context *context,
    uint32_t region
)
{
    if (context == NULL || region >= C31_FAKE_DMA_REGIONS) {
        return NULL;
    }
    return context->controller[region];
}
