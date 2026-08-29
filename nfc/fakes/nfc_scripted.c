/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "nfc_scripted.h"

#include <string.h>

static int operation_equal(
    const struct fwlab_nfc_operation_token *left,
    const struct fwlab_nfc_operation_token *right
)
{
    return left->instance_nonce == right->instance_nonce &&
           left->operation_uid == right->operation_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->generation == right->generation;
}

static struct fwlab_nfc_submit_result scripted_submit(
    void *opaque,
    const struct fwlab_nfc_request *request
)
{
    struct c33_scripted_nfc *fake = opaque;
    struct fwlab_nfc_submit_result result;
    uint32_t scenario;
    uint32_t active;

    result.disposition = FWLAB_NFC_REJECTED;
    result.reason = FWLAB_NFC_REASON_UNSUPPORTED;
    if (fake == NULL || request == NULL ||
        request->version != FWLAB_NFC_CONTRACT_VERSION ||
        request->size != sizeof(*request)) {
        result.reason = FWLAB_NFC_REASON_RANGE;
        return result;
    }
    for (scenario = 0; scenario < fake->scenario_count; ++scenario) {
        if (fake->scenario[scenario].request_cookie == request->cookie) {
            break;
        }
    }
    if (scenario == fake->scenario_count) {
        return result;
    }
    if (fake->scenario[scenario].backpressure_count != 0) {
        --fake->scenario[scenario].backpressure_count;
        result.disposition = FWLAB_NFC_BACKPRESSURE;
        result.reason = FWLAB_NFC_REASON_NONE;
        return result;
    }
    for (active = 0; active < C33_SCRIPTED_MAX_ACTIVE; ++active) {
        if (!fake->active[active].used) {
            memset(&fake->active[active], 0, sizeof(fake->active[active]));
            fake->active[active].used = 1;
            fake->active[active].scenario_index = scenario;
            fake->active[active].remaining_steps =
                fake->scenario[scenario].delay_steps;
            fake->active[active].request = *request;
            result.disposition = FWLAB_NFC_ACCEPTED;
            result.reason = FWLAB_NFC_REASON_NONE;
            return result;
        }
    }
    result.disposition = FWLAB_NFC_BACKPRESSURE;
    result.reason = FWLAB_NFC_REASON_NONE;
    return result;
}

static enum fwlab_nfc_api_result scripted_cancel(
    void *opaque,
    const struct fwlab_nfc_operation_token *operation
)
{
    struct c33_scripted_nfc *fake = opaque;
    uint32_t index;

    if (fake == NULL || operation == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    for (index = 0; index < C33_SCRIPTED_MAX_ACTIVE; ++index) {
        if (fake->active[index].used &&
            operation_equal(&fake->active[index].request.operation,
                            operation)) {
            fake->active[index].cancel_requested = 1;
            return FWLAB_NFC_API_OK;
        }
    }
    return FWLAB_NFC_API_NOT_FOUND;
}

static enum fwlab_nfc_api_result scripted_step(
    void *opaque,
    uint32_t budget,
    struct fwlab_nfc_step_result *result
)
{
    struct c33_scripted_nfc *fake = opaque;
    uint32_t used = 0;
    uint32_t index;

    if (fake == NULL || result == NULL || budget == 0) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(result, 0, sizeof(*result));
    for (index = 0; index < C33_SCRIPTED_MAX_ACTIVE && used < budget;
         ++index) {
        struct c33_scripted_active *active = &fake->active[index];

        if (!active->used || active->pending) {
            continue;
        }
        ++fake->virtual_now;
        if (active->remaining_steps != 0) {
            --active->remaining_steps;
        } else {
            active->pending = 1;
            ++result->events_pending;
        }
        ++used;
        ++result->transitions;
    }
    result->units_used = used;
    result->virtual_now = fake->virtual_now;
    result->phase = FWLAB_NFC_MODEL_READY;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result scripted_poll(
    void *opaque,
    uint32_t budget,
    struct fwlab_nfc_completion *events,
    uint32_t event_capacity,
    uint32_t *event_count
)
{
    struct c33_scripted_nfc *fake = opaque;
    uint32_t scanned;

    if (fake == NULL || events == NULL || event_count == NULL ||
        budget == 0 || event_capacity == 0) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *event_count = 0;
    for (scanned = 0; scanned < C33_SCRIPTED_MAX_ACTIVE; ++scanned) {
        uint32_t index = (fake->poll_cursor + scanned) %
                         C33_SCRIPTED_MAX_ACTIVE;
        struct c33_scripted_active *active = &fake->active[index];
        const struct c33_scripted_scenario *scenario;

        if (!active->used || !active->pending) {
            continue;
        }
        scenario = &fake->scenario[active->scenario_index];
        events[0] = scenario->completion;
        events[0].operation = active->request.operation;
        events[0].ppa = active->request.ppa;
        events[0].cookie = active->request.cookie;
        events[0].operation_kind = active->request.kind;
        events[0].requested_region_mask = active->request.region_mask;
        events[0].accepted_tick = fake->virtual_now -
            scenario->delay_steps - 1u;
        events[0].status_tick = fake->virtual_now;
        events[0].outcome_tick = fake->virtual_now;
        if (active->cancel_requested && scenario->cancel_wins) {
            events[0].terminal = FWLAB_NFC_TERMINAL_CANCELLED;
            events[0].physical_outcome = FWLAB_NFC_PHYS_NO_EFFECT;
            events[0].integrity = FWLAB_NFC_INTEGRITY_NOT_APPLICABLE;
            events[0].reason = FWLAB_NFC_REASON_CANCELLED;
            events[0].valid_region_mask = 0;
            events[0].applied_region_mask = 0;
        }
        memset(active, 0, sizeof(*active));
        fake->poll_cursor = (index + 1u) % C33_SCRIPTED_MAX_ACTIVE;
        *event_count = 1;
        return FWLAB_NFC_API_OK;
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result scripted_reset(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch
)
{
    struct c33_scripted_nfc *fake = opaque;
    uint32_t index;

    if (fake == NULL || instance_nonce == 0 || old_epoch == 0) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    for (index = 0; index < C33_SCRIPTED_MAX_ACTIVE; ++index) {
        if (fake->active[index].used &&
            fake->active[index].request.operation.instance_nonce ==
                instance_nonce &&
            fake->active[index].request.operation.controller_epoch ==
                old_epoch) {
            fake->active[index].cancel_requested = 1;
        }
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result scripted_quiescent(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch,
    bool *quiescent
)
{
    struct c33_scripted_nfc *fake = opaque;
    uint32_t index;

    if (fake == NULL || quiescent == NULL || instance_nonce == 0 ||
        old_epoch == 0) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *quiescent = true;
    for (index = 0; index < C33_SCRIPTED_MAX_ACTIVE; ++index) {
        if (fake->active[index].used &&
            fake->active[index].request.operation.instance_nonce ==
                instance_nonce &&
            fake->active[index].request.operation.controller_epoch ==
                old_epoch) {
            *quiescent = false;
            break;
        }
    }
    return FWLAB_NFC_API_OK;
}

static const struct fwlab_nfc_provider_ops scripted_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_nfc_provider_ops),
    .reserved = 0,
    .try_submit = scripted_submit,
    .cancel = scripted_cancel,
    .step = scripted_step,
    .poll = scripted_poll,
    .reset_begin = scripted_reset,
    .quiescent = scripted_quiescent,
};

void c33_scripted_init(struct c33_scripted_nfc *fake)
{
    if (fake != NULL) {
        memset(fake, 0, sizeof(*fake));
    }
}

int c33_scripted_add(
    struct c33_scripted_nfc *fake,
    const struct c33_scripted_scenario *scenario
)
{
    if (fake == NULL || scenario == NULL ||
        fake->scenario_count >= C33_SCRIPTED_MAX_SCENARIOS ||
        scenario->request_cookie == 0 || scenario->reserved[0] != 0 ||
        scenario->reserved[1] != 0 || scenario->reserved[2] != 0 ||
        scenario->completion.version != FWLAB_NFC_CONTRACT_VERSION ||
        scenario->completion.size != sizeof(scenario->completion) ||
        scenario->cancel_wins > 1) {
        return 0;
    }
    fake->scenario[fake->scenario_count++] = *scenario;
    return 1;
}

struct fwlab_nfc_provider c33_scripted_provider(
    struct c33_scripted_nfc *fake
)
{
    struct fwlab_nfc_provider provider;

    provider.ops = &scripted_ops;
    provider.context = fake;
    return provider;
}
