/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c31_fake_provider.h"

#include <string.h>

static int request_equal(
    const struct fwlab_c31_request_token *left,
    const struct fwlab_c31_request_token *right
)
{
    return left->word[0] == right->word[0] &&
           left->word[1] == right->word[1];
}

static int operation_equal(
    const struct fwlab_c31_operation_token *left,
    const struct fwlab_c31_operation_token *right
)
{
    return left->command.instance_nonce == right->command.instance_nonce &&
           left->command.command_uid == right->command.command_uid &&
           left->command.controller_epoch ==
               right->command.controller_epoch &&
           left->command.slot == right->command.slot &&
           left->command.slot_generation ==
               right->command.slot_generation &&
           left->cookie == right->cookie &&
           left->operation_generation == right->operation_generation &&
           left->reserved == right->reserved;
}

static struct fwlab_c31_provider_submit_result rejected_result(void)
{
    struct fwlab_c31_provider_submit_result result;

    memset(&result, 0, sizeof(result));
    result.disposition = FWLAB_C31_PROVIDER_REJECTED;
    result.fault.domain = FWLAB_C31_FAULT_PROVIDER;
    result.fault.retry_class = FWLAB_C31_RETRY_NEVER;
    result.fault.effect_class = FWLAB_C31_EFFECT_NONE;
    result.fault.reason = FWLAB_C31_REASON_PROVIDER_REJECTED;
    return result;
}

static struct fwlab_c31_provider_submit_result fake_try_submit(
    void *opaque,
    const struct fwlab_c31_provider_request *request
)
{
    struct c31_fake_provider_context *context = opaque;
    struct fwlab_c31_provider_submit_result result;
    uint32_t scenario_index;
    uint32_t active_index;

    if (context == NULL || request == NULL ||
        request->version != FWLAB_C31_PROVIDER_CONTRACT_VERSION ||
        request->size != sizeof(*request) || request->reserved0 != 0 ||
        request->reserved1 != 0 ||
        request->provider_kind != context->provider_kind) {
        return rejected_result();
    }
    for (scenario_index = 0; scenario_index < context->scenario_count;
         ++scenario_index) {
        if (request_equal(&context->scenarios[scenario_index].request,
                          &request->request)) {
            break;
        }
    }
    if (scenario_index == context->scenario_count) {
        return rejected_result();
    }

    memset(&result, 0, sizeof(result));
    if (context->scenarios[scenario_index].backpressure_count > 0) {
        --context->scenarios[scenario_index].backpressure_count;
        result.disposition = FWLAB_C31_PROVIDER_BACKPRESSURE;
        return result;
    }
    if (context->scenarios[scenario_index].submit_disposition ==
        FWLAB_C31_PROVIDER_REJECTED) {
        result.disposition = FWLAB_C31_PROVIDER_REJECTED;
        result.fault = context->scenarios[scenario_index].submit_fault;
        return result;
    }
    for (active_index = 0; active_index < C31_FAKE_MAX_ACTIVE;
         ++active_index) {
        if (!context->active[active_index].used) {
            struct c31_fake_active *active = &context->active[active_index];

            memset(active, 0, sizeof(*active));
            active->used = true;
            active->request = *request;
            active->scenario_index = scenario_index;
            active->remaining_polls =
                context->scenarios[scenario_index].delay_polls;
            result.disposition = FWLAB_C31_PROVIDER_ACCEPTED;
            return result;
        }
    }
    result.disposition = FWLAB_C31_PROVIDER_BACKPRESSURE;
    return result;
}

static enum fwlab_c31_api_result fake_cancel(
    void *opaque,
    const struct fwlab_c31_operation_token *operation
)
{
    struct c31_fake_provider_context *context = opaque;
    uint32_t index;

    if (context == NULL || operation == NULL || operation->reserved != 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    for (index = 0; index < C31_FAKE_MAX_ACTIVE; ++index) {
        if (context->active[index].used &&
            operation_equal(&context->active[index].request.operation,
                            operation)) {
            context->active[index].cancel_requested = true;
            return FWLAB_C31_API_OK;
        }
    }
    return FWLAB_C31_API_OK;
}

static void build_event(
    struct c31_fake_provider_context *context,
    struct c31_fake_active *active
)
{
    const struct c31_fake_scenario *scenario =
        &context->scenarios[active->scenario_index];

    memset(&active->event, 0, sizeof(active->event));
    active->event.version = FWLAB_C31_PROVIDER_CONTRACT_VERSION;
    active->event.size = (uint16_t)sizeof(active->event);
    active->event.operation = active->request.operation;
    if (active->cancel_requested && scenario->cancel_wins) {
        active->event.terminal = FWLAB_C31_PROVIDER_CANCELLED;
        active->event.fault.domain = FWLAB_C31_FAULT_PROVIDER;
        active->event.fault.retry_class = FWLAB_C31_RETRY_NEVER;
        active->event.fault.effect_class =
            scenario->terminal_fault.effect_class;
        active->event.fault.prefix_length =
            scenario->terminal_fault.prefix_length;
        active->event.fault.reason = FWLAB_C31_REASON_CANCELLED;
    } else {
        active->event.terminal = scenario->terminal;
        active->event.fault = scenario->terminal_fault;
    }
}

static enum fwlab_c31_api_result fake_poll(
    void *opaque,
    uint32_t budget,
    struct fwlab_c31_provider_event *events,
    uint32_t event_capacity,
    uint32_t *event_count
)
{
    struct c31_fake_provider_context *context = opaque;
    uint32_t scanned;

    if (context == NULL || events == NULL || event_count == NULL ||
        budget == 0 || event_capacity == 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    *event_count = 0;
    for (scanned = 0; scanned < C31_FAKE_MAX_ACTIVE; ++scanned) {
        uint32_t index = (context->poll_cursor + scanned) %
                         C31_FAKE_MAX_ACTIVE;
        struct c31_fake_active *active = &context->active[index];
        const struct c31_fake_scenario *scenario;

        if (!active->used) {
            continue;
        }
        context->poll_cursor = (index + 1) % C31_FAKE_MAX_ACTIVE;
        if (active->remaining_polls > 0) {
            --active->remaining_polls;
            return FWLAB_C31_API_OK;
        }
        scenario = &context->scenarios[active->scenario_index];
        if (!active->emitted_once) {
            build_event(context, active);
            active->emitted_once = true;
            active->duplicate_pending = scenario->duplicate_terminal;
        }
        events[0] = active->event;
        *event_count = 1;
        if (active->duplicate_pending) {
            active->duplicate_pending = false;
        } else {
            active->used = false;
        }
        return FWLAB_C31_API_OK;
    }
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result fake_reset_begin(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch
)
{
    struct c31_fake_provider_context *context = opaque;
    uint32_t index;

    if (context == NULL || instance_nonce == 0 || old_epoch == 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    for (index = 0; index < C31_FAKE_MAX_ACTIVE; ++index) {
        struct c31_fake_active *active = &context->active[index];

        if (active->used &&
            active->request.operation.command.instance_nonce ==
                instance_nonce &&
            active->request.operation.command.controller_epoch == old_epoch) {
            active->cancel_requested = true;
        }
    }
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result fake_quiescent(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch,
    bool *quiescent
)
{
    struct c31_fake_provider_context *context = opaque;
    uint32_t index;

    if (context == NULL || quiescent == NULL || instance_nonce == 0 ||
        old_epoch == 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    *quiescent = true;
    for (index = 0; index < C31_FAKE_MAX_ACTIVE; ++index) {
        const struct c31_fake_active *active = &context->active[index];

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

static const struct fwlab_c31_provider_ops fake_ops = {
    .version = FWLAB_C31_PROVIDER_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_c31_provider_ops),
    .reserved = 0,
    .try_submit = fake_try_submit,
    .cancel = fake_cancel,
    .poll = fake_poll,
    .reset_begin = fake_reset_begin,
    .quiescent = fake_quiescent,
};

void c31_fake_provider_init(
    struct c31_fake_provider_context *context,
    uint8_t provider_kind
)
{
    memset(context, 0, sizeof(*context));
    context->provider_kind = provider_kind;
}

int c31_fake_provider_add(
    struct c31_fake_provider_context *context,
    const struct c31_fake_scenario *scenario
)
{
    if (context == NULL || scenario == NULL ||
        context->scenario_count >= C31_FAKE_MAX_SCENARIOS ||
        scenario->submit_disposition > FWLAB_C31_PROVIDER_REJECTED ||
        scenario->terminal > FWLAB_C31_PROVIDER_FAILED) {
        return 0;
    }
    context->scenarios[context->scenario_count++] = *scenario;
    return 1;
}

struct fwlab_c31_provider c31_fake_provider(
    struct c31_fake_provider_context *context
)
{
    struct fwlab_c31_provider provider;

    provider.ops = &fake_ops;
    provider.context = context;
    return provider;
}

uint32_t c31_fake_provider_active(
    const struct c31_fake_provider_context *context
)
{
    uint32_t index;
    uint32_t count = 0;

    for (index = 0; index < C31_FAKE_MAX_ACTIVE; ++index) {
        if (context->active[index].used) {
            ++count;
        }
    }
    return count;
}
