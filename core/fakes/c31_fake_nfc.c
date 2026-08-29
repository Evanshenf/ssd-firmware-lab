/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c31_fake_nfc.h"

#include <string.h>

static int pair_equal(const uint64_t left[2], const uint64_t right[2])
{
    return left[0] == right[0] && left[1] == right[1];
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

static struct fwlab_c31_provider_submit_result reject_result(void)
{
    struct fwlab_c31_provider_submit_result result;

    memset(&result, 0, sizeof(result));
    result.disposition = FWLAB_C31_PROVIDER_REJECTED;
    result.fault.domain = FWLAB_C31_FAULT_MEDIA;
    result.fault.retry_class = FWLAB_C31_RETRY_NEVER;
    result.fault.effect_class = FWLAB_C31_EFFECT_NONE;
    result.fault.reason = FWLAB_C31_REASON_PROVIDER_REJECTED;
    return result;
}

static struct fwlab_c31_provider_submit_result nfc_submit(
    void *opaque,
    const struct fwlab_c31_provider_request *request
)
{
    struct c31_fake_nfc_context *context = opaque;
    struct fwlab_c31_provider_submit_result result;
    uint32_t scenario_index;
    uint32_t active_index;

    if (context == NULL || request == NULL ||
        request->version != FWLAB_C31_PROVIDER_CONTRACT_VERSION ||
        request->size != sizeof(*request) || request->reserved0 != 0 ||
        request->reserved1 != 0 ||
        request->provider_kind != FWLAB_C31_PROVIDER_NFC ||
        request->dma_direction != FWLAB_C31_DMA_NONE ||
        request->length != 0) {
        return reject_result();
    }
    for (scenario_index = 0; scenario_index < context->scenario_count;
         ++scenario_index) {
        if (pair_equal(context->scenarios[scenario_index].request.word,
                       request->request.word)) {
            break;
        }
    }
    if (scenario_index == context->scenario_count) {
        return reject_result();
    }
    memset(&result, 0, sizeof(result));
    if (context->scenarios[scenario_index].backpressure_count > 0) {
        --context->scenarios[scenario_index].backpressure_count;
        result.disposition = FWLAB_C31_PROVIDER_BACKPRESSURE;
        return result;
    }
    for (active_index = 0; active_index < C31_FAKE_NFC_MAX_ACTIVE;
         ++active_index) {
        struct c31_fake_nfc_active *active = &context->active[active_index];

        if (!active->used) {
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

static enum fwlab_c31_api_result nfc_cancel(
    void *opaque,
    const struct fwlab_c31_operation_token *operation
)
{
    struct c31_fake_nfc_context *context = opaque;
    uint32_t index;

    if (context == NULL || operation == NULL || operation->reserved != 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    for (index = 0; index < C31_FAKE_NFC_MAX_ACTIVE; ++index) {
        if (context->active[index].used &&
            operation_equal(&context->active[index].request.operation,
                            operation)) {
            context->active[index].cancel_requested = true;
            break;
        }
    }
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result nfc_poll(
    void *opaque,
    uint32_t budget,
    struct fwlab_c31_provider_event *events,
    uint32_t event_capacity,
    uint32_t *event_count
)
{
    struct c31_fake_nfc_context *context = opaque;
    uint32_t scanned;

    if (context == NULL || events == NULL || event_count == NULL ||
        budget == 0 || event_capacity == 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    *event_count = 0;
    for (scanned = 0; scanned < C31_FAKE_NFC_MAX_ACTIVE; ++scanned) {
        uint32_t index = (context->poll_cursor + scanned) %
                         C31_FAKE_NFC_MAX_ACTIVE;
        struct c31_fake_nfc_active *active = &context->active[index];
        const struct c31_fake_nfc_scenario *scenario;

        if (!active->used) {
            continue;
        }
        context->poll_cursor = (index + 1) % C31_FAKE_NFC_MAX_ACTIVE;
        if (active->remaining_polls > 0) {
            --active->remaining_polls;
            return FWLAB_C31_API_OK;
        }
        scenario = &context->scenarios[active->scenario_index];
        memset(&events[0], 0, sizeof(events[0]));
        events[0].version = FWLAB_C31_PROVIDER_CONTRACT_VERSION;
        events[0].size = (uint16_t)sizeof(events[0]);
        events[0].operation = active->request.operation;
        if (active->cancel_requested && scenario->cancel_wins) {
            events[0].terminal = FWLAB_C31_PROVIDER_CANCELLED;
            events[0].fault.domain = FWLAB_C31_FAULT_MEDIA;
            events[0].fault.retry_class = FWLAB_C31_RETRY_NEVER;
            events[0].fault.effect_class = FWLAB_C31_EFFECT_NONE;
            events[0].fault.reason = FWLAB_C31_REASON_CANCELLED;
        } else {
            events[0].terminal = scenario->terminal;
            events[0].fault = scenario->fault;
        }
        active->used = false;
        *event_count = 1;
        return FWLAB_C31_API_OK;
    }
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result nfc_reset_begin(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch
)
{
    struct c31_fake_nfc_context *context = opaque;
    uint32_t index;

    if (context == NULL || instance_nonce == 0 || old_epoch == 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    for (index = 0; index < C31_FAKE_NFC_MAX_ACTIVE; ++index) {
        struct c31_fake_nfc_active *active = &context->active[index];

        if (active->used &&
            active->request.operation.command.instance_nonce ==
                instance_nonce &&
            active->request.operation.command.controller_epoch == old_epoch) {
            active->cancel_requested = true;
        }
    }
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result nfc_quiescent(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch,
    bool *quiescent
)
{
    struct c31_fake_nfc_context *context = opaque;
    uint32_t index;

    if (context == NULL || quiescent == NULL || instance_nonce == 0 ||
        old_epoch == 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    *quiescent = true;
    for (index = 0; index < C31_FAKE_NFC_MAX_ACTIVE; ++index) {
        const struct c31_fake_nfc_active *active = &context->active[index];

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

static const struct fwlab_c31_provider_ops nfc_ops = {
    .version = FWLAB_C31_PROVIDER_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_c31_provider_ops),
    .reserved = 0,
    .try_submit = nfc_submit,
    .cancel = nfc_cancel,
    .poll = nfc_poll,
    .reset_begin = nfc_reset_begin,
    .quiescent = nfc_quiescent,
};

void c31_fake_nfc_init(struct c31_fake_nfc_context *context)
{
    memset(context, 0, sizeof(*context));
}

int c31_fake_nfc_add(
    struct c31_fake_nfc_context *context,
    const struct c31_fake_nfc_scenario *scenario
)
{
    if (context == NULL || scenario == NULL ||
        context->scenario_count >= C31_FAKE_NFC_MAX_SCENARIOS ||
        scenario->terminal > FWLAB_C31_PROVIDER_FAILED) {
        return 0;
    }
    context->scenarios[context->scenario_count++] = *scenario;
    return 1;
}

struct fwlab_c31_provider c31_fake_nfc_provider(
    struct c31_fake_nfc_context *context
)
{
    struct fwlab_c31_provider provider;

    provider.ops = &nfc_ops;
    provider.context = context;
    return provider;
}
