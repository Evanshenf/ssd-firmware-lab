/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "adapters/nfc_c31_adapter.h"

#include <string.h>

static int pair_equal(const uint64_t left[2], const uint64_t right[2])
{
    return left[0] == right[0] && left[1] == right[1];
}

static int outer_equal(
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

static struct c33_c31_registry_entry *find_registry(
    struct c33_c31_adapter *adapter,
    const struct fwlab_c31_request_token *token
)
{
    unsigned int index;

    for (index = 0; index < C33_C31_ADAPTER_SLOTS; ++index) {
        if (adapter->registry[index].used &&
            pair_equal(adapter->registry[index].token.word, token->word)) {
            return &adapter->registry[index];
        }
    }
    return NULL;
}

static struct c33_c31_active *find_active_outer(
    struct c33_c31_adapter *adapter,
    const struct fwlab_c31_operation_token *operation
)
{
    unsigned int index;

    for (index = 0; index < C33_C31_ADAPTER_SLOTS; ++index) {
        if (adapter->active[index].used &&
            outer_equal(&adapter->active[index].outer, operation)) {
            return &adapter->active[index];
        }
    }
    return NULL;
}

static struct c33_c31_active *find_active_inner(
    struct c33_c31_adapter *adapter,
    const struct fwlab_nfc_operation_token *operation
)
{
    unsigned int index;

    for (index = 0; index < C33_C31_ADAPTER_SLOTS; ++index) {
        const struct fwlab_nfc_operation_token *inner =
            &adapter->active[index].inner;

        if (adapter->active[index].used &&
            inner->instance_nonce == operation->instance_nonce &&
            inner->operation_uid == operation->operation_uid &&
            inner->controller_epoch == operation->controller_epoch &&
            inner->generation == operation->generation) {
            return &adapter->active[index];
        }
    }
    return NULL;
}

static struct c33_c31_active *free_active(struct c33_c31_adapter *adapter)
{
    unsigned int index;

    for (index = 0; index < C33_C31_ADAPTER_SLOTS; ++index) {
        if (!adapter->active[index].used) {
            return &adapter->active[index];
        }
    }
    return NULL;
}

static struct c33_c31_sidecar *free_sidecar(struct c33_c31_adapter *adapter)
{
    unsigned int index;

    for (index = 0; index < C33_C31_ADAPTER_SLOTS; ++index) {
        if (!adapter->sidecar[index].used) {
            return &adapter->sidecar[index];
        }
    }
    return NULL;
}

static int transfer_span_matches(
    const struct fwlab_c31_provider_request *outer,
    const struct fwlab_nfc_request *inner
)
{
    uint64_t expected = 0;

    if ((inner->region_mask & FWLAB_NFC_REGION_MAIN) != 0) {
        if (inner->main.controller_region != outer->controller_region ||
            inner->main.offset != outer->controller_offset) {
            return 0;
        }
        expected = inner->main.length;
    }
    if ((inner->region_mask & FWLAB_NFC_REGION_OOB) != 0) {
        if (inner->oob.controller_region != outer->controller_region ||
            inner->oob.offset != (uint64_t)outer->controller_offset +
                                     expected) {
            return 0;
        }
        expected += inner->oob.length;
    }
    return expected <= UINT32_MAX && outer->length == expected;
}

static struct fwlab_c31_provider_submit_result adapter_reject(
    uint32_t reason
)
{
    struct fwlab_c31_provider_submit_result result;

    memset(&result, 0, sizeof(result));
    result.disposition = FWLAB_C31_PROVIDER_REJECTED;
    result.fault.domain = FWLAB_C31_FAULT_MEDIA;
    result.fault.retry_class = FWLAB_C31_RETRY_NEVER;
    result.fault.effect_class = FWLAB_C31_EFFECT_NONE;
    result.fault.reason = reason;
    return result;
}

static struct fwlab_c31_provider_submit_result adapter_submit(
    void *opaque,
    const struct fwlab_c31_provider_request *outer
)
{
    struct c33_c31_adapter *adapter = opaque;
    struct c33_c31_registry_entry *registry;
    struct c33_c31_active *active;
    struct fwlab_nfc_request inner;
    struct fwlab_nfc_submit_result submitted;
    struct fwlab_c31_provider_submit_result result;

    if (adapter == NULL || outer == NULL ||
        outer->version != FWLAB_C31_PROVIDER_CONTRACT_VERSION ||
        outer->size != sizeof(*outer) || outer->reserved0 != 0 ||
        outer->reserved1 != 0 || outer->provider_kind !=
            FWLAB_C31_PROVIDER_NFC || outer->dma_direction !=
            FWLAB_C31_DMA_NONE || outer->operation.reserved != 0) {
        return adapter_reject(FWLAB_C31_REASON_PROVIDER_REJECTED);
    }
    registry = find_registry(adapter, &outer->request);
    active = free_active(adapter);
    if (registry == NULL || active == NULL || free_sidecar(adapter) == NULL) {
        if (registry != NULL &&
            (active == NULL || free_sidecar(adapter) == NULL)) {
            memset(&result, 0, sizeof(result));
            result.disposition = FWLAB_C31_PROVIDER_BACKPRESSURE;
            return result;
        }
        return adapter_reject(FWLAB_C31_REASON_PROVIDER_REJECTED);
    }
    inner = registry->request;
    inner.operation.instance_nonce = outer->operation.command.instance_nonce;
    inner.operation.operation_uid = outer->operation.command.command_uid;
    inner.operation.controller_epoch =
        outer->operation.command.controller_epoch;
    inner.operation.generation = outer->operation.operation_generation;
    if ((inner.kind == FWLAB_NFC_READ_TRANSFER ||
         inner.kind == FWLAB_NFC_PROGRAM_TRANSFER) &&
        !transfer_span_matches(outer, &inner)) {
        return adapter_reject(FWLAB_C31_REASON_RANGE);
    }
    submitted = adapter->provider.ops->try_submit(
        adapter->provider.context, &inner);
    memset(&result, 0, sizeof(result));
    result.disposition = submitted.disposition == FWLAB_NFC_ACCEPTED ?
        FWLAB_C31_PROVIDER_ACCEPTED :
        submitted.disposition == FWLAB_NFC_BACKPRESSURE ?
        FWLAB_C31_PROVIDER_BACKPRESSURE : FWLAB_C31_PROVIDER_REJECTED;
    if (submitted.disposition == FWLAB_NFC_REJECTED) {
        result.fault.domain = FWLAB_C31_FAULT_MEDIA;
        result.fault.retry_class = FWLAB_C31_RETRY_NEVER;
        result.fault.effect_class = FWLAB_C31_EFFECT_NONE;
        result.fault.reason = FWLAB_C31_REASON_PROVIDER_REJECTED;
    } else if (submitted.disposition == FWLAB_NFC_ACCEPTED) {
        memset(active, 0, sizeof(*active));
        active->used = 1;
        active->outer = outer->operation;
        active->inner = inner.operation;
        registry->used = 0;
    }
    return result;
}

static enum fwlab_c31_api_result adapter_cancel(
    void *opaque,
    const struct fwlab_c31_operation_token *operation
)
{
    struct c33_c31_adapter *adapter = opaque;
    struct c33_c31_active *active;
    enum fwlab_nfc_api_result result;

    if (adapter == NULL || operation == NULL) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    active = find_active_outer(adapter, operation);
    if (active == NULL) {
        return FWLAB_C31_API_NOT_FOUND;
    }
    result = adapter->provider.ops->cancel(adapter->provider.context,
                                           &active->inner);
    return result == FWLAB_NFC_API_OK ? FWLAB_C31_API_OK :
           result == FWLAB_NFC_API_NOT_FOUND ? FWLAB_C31_API_NOT_FOUND :
                                               FWLAB_C31_API_INVALID_CONTRACT;
}

static void normalize_completion(
    const struct fwlab_nfc_completion *completion,
    struct fwlab_c31_provider_event *event
)
{
    event->terminal = completion->terminal == FWLAB_NFC_TERMINAL_SUCCESS ?
        FWLAB_C31_PROVIDER_SUCCESS :
        completion->terminal == FWLAB_NFC_TERMINAL_CANCELLED ?
        FWLAB_C31_PROVIDER_CANCELLED : FWLAB_C31_PROVIDER_FAILED;
    if (completion->terminal == FWLAB_NFC_TERMINAL_SUCCESS) {
        return;
    }
    event->fault.domain = FWLAB_C31_FAULT_MEDIA;
    event->fault.retry_class =
        completion->reason == FWLAB_NFC_REASON_ECC_UNCORRECTABLE ?
        FWLAB_C31_RETRY_LATER : FWLAB_C31_RETRY_NEVER;
    event->fault.effect_class =
        completion->physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT ?
        FWLAB_C31_EFFECT_NONE :
        completion->integrity == FWLAB_NFC_INTEGRITY_COMPLETE ?
        FWLAB_C31_EFFECT_FULL : FWLAB_C31_EFFECT_UNKNOWN_PREFIX;
    event->fault.reason = completion->terminal ==
        FWLAB_NFC_TERMINAL_CANCELLED ? FWLAB_C31_REASON_CANCELLED :
                                      FWLAB_C31_REASON_PROVIDER_FAILED;
}

static enum fwlab_c31_api_result adapter_poll(
    void *opaque,
    uint32_t budget,
    struct fwlab_c31_provider_event *events,
    uint32_t event_capacity,
    uint32_t *event_count
)
{
    struct c33_c31_adapter *adapter = opaque;
    struct fwlab_nfc_step_result step;
    struct fwlab_nfc_completion completion[C33_C31_ADAPTER_SLOTS];
    uint32_t count = 0;
    uint32_t index;
    uint32_t inner_budget;

    if (adapter == NULL || events == NULL || event_count == NULL ||
        budget == 0 || event_capacity == 0 ||
        budget > C33_C31_ADAPTER_SLOTS) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    inner_budget = budget < event_capacity ? budget : event_capacity;
    if (adapter->provider.ops->step(adapter->provider.context, inner_budget,
                                    &step) != FWLAB_NFC_API_OK ||
        adapter->provider.ops->poll(
            adapter->provider.context, inner_budget, completion,
            C33_C31_ADAPTER_SLOTS, &count) != FWLAB_NFC_API_OK) {
        return FWLAB_C31_API_INVARIANT_FAILURE;
    }
    if (count > event_capacity) {
        return FWLAB_C31_API_NO_CAPACITY;
    }
    for (index = 0; index < count; ++index) {
        struct c33_c31_active *active = find_active_inner(
            adapter, &completion[index].operation);
        struct c33_c31_sidecar *sidecar = free_sidecar(adapter);

        if (active == NULL || sidecar == NULL) {
            return FWLAB_C31_API_INVARIANT_FAILURE;
        }
        memset(&events[index], 0, sizeof(events[index]));
        events[index].version = FWLAB_C31_PROVIDER_CONTRACT_VERSION;
        events[index].size = (uint16_t)sizeof(events[index]);
        events[index].operation = active->outer;
        normalize_completion(&completion[index], &events[index]);
        memset(sidecar, 0, sizeof(*sidecar));
        sidecar->used = 1;
        sidecar->outer = active->outer;
        sidecar->completion = completion[index];
        active->used = 0;
    }
    *event_count = count;
    (void)step;
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result adapter_reset(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch
)
{
    struct c33_c31_adapter *adapter = opaque;
    enum fwlab_nfc_api_result result;

    if (adapter == NULL) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    result = adapter->provider.ops->reset_begin(
        adapter->provider.context, instance_nonce, old_epoch);
    return result == FWLAB_NFC_API_OK ? FWLAB_C31_API_OK :
                                       FWLAB_C31_API_INVALID_CONTRACT;
}

static enum fwlab_c31_api_result adapter_quiescent(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch,
    bool *quiescent
)
{
    struct c33_c31_adapter *adapter = opaque;
    enum fwlab_nfc_api_result result;

    if (adapter == NULL) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    result = adapter->provider.ops->quiescent(
        adapter->provider.context, instance_nonce, old_epoch, quiescent);
    return result == FWLAB_NFC_API_OK ? FWLAB_C31_API_OK :
                                       FWLAB_C31_API_INVALID_CONTRACT;
}

static const struct fwlab_c31_provider_ops adapter_ops = {
    .version = FWLAB_C31_PROVIDER_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_c31_provider_ops),
    .reserved = 0,
    .try_submit = adapter_submit,
    .cancel = adapter_cancel,
    .poll = adapter_poll,
    .reset_begin = adapter_reset,
    .quiescent = adapter_quiescent,
};

enum fwlab_nfc_api_result c33_c31_adapter_init(
    struct c33_c31_adapter *adapter,
    const struct fwlab_nfc_provider *provider
)
{
    if (adapter == NULL || provider == NULL || provider->ops == NULL ||
        provider->context == NULL ||
        provider->ops->version != FWLAB_NFC_CONTRACT_VERSION ||
        provider->ops->size != sizeof(*provider->ops) ||
        provider->ops->reserved != 0 || provider->ops->try_submit == NULL ||
        provider->ops->cancel == NULL || provider->ops->step == NULL ||
        provider->ops->poll == NULL || provider->ops->reset_begin == NULL ||
        provider->ops->quiescent == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(adapter, 0, sizeof(*adapter));
    adapter->provider = *provider;
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result c33_c31_adapter_register(
    struct c33_c31_adapter *adapter,
    const struct fwlab_c31_request_token *token,
    const struct fwlab_nfc_request *request
)
{
    unsigned int index;

    if (adapter == NULL || token == NULL || request == NULL ||
        (token->word[0] == 0 && token->word[1] == 0) ||
        request->version != FWLAB_NFC_CONTRACT_VERSION ||
        request->size != sizeof(*request)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    for (index = 0; index < C33_C31_ADAPTER_SLOTS; ++index) {
        if (adapter->registry[index].used &&
            pair_equal(adapter->registry[index].token.word, token->word)) {
            return FWLAB_NFC_API_WRONG_STATE;
        }
    }
    for (index = 0; index < C33_C31_ADAPTER_SLOTS; ++index) {
        if (!adapter->registry[index].used) {
            adapter->registry[index].used = 1;
            adapter->registry[index].token = *token;
            adapter->registry[index].request = *request;
            return FWLAB_NFC_API_OK;
        }
    }
    return FWLAB_NFC_API_NO_CAPACITY;
}

struct fwlab_c31_provider c33_c31_adapter_provider(
    struct c33_c31_adapter *adapter
)
{
    struct fwlab_c31_provider provider;

    provider.ops = &adapter_ops;
    provider.context = adapter;
    return provider;
}

enum fwlab_nfc_api_result c33_c31_adapter_result_read(
    const struct c33_c31_adapter *adapter,
    const struct fwlab_c31_operation_token *operation,
    struct fwlab_nfc_completion *completion
)
{
    unsigned int index;

    if (adapter == NULL || operation == NULL || completion == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    for (index = 0; index < C33_C31_ADAPTER_SLOTS; ++index) {
        if (adapter->sidecar[index].used &&
            outer_equal(&adapter->sidecar[index].outer, operation)) {
            *completion = adapter->sidecar[index].completion;
            return FWLAB_NFC_API_OK;
        }
    }
    return FWLAB_NFC_API_NOT_FOUND;
}

enum fwlab_nfc_api_result c33_c31_adapter_result_ack(
    struct c33_c31_adapter *adapter,
    const struct fwlab_c31_operation_token *operation
)
{
    unsigned int index;

    if (adapter == NULL || operation == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    for (index = 0; index < C33_C31_ADAPTER_SLOTS; ++index) {
        if (adapter->sidecar[index].used &&
            outer_equal(&adapter->sidecar[index].outer, operation)) {
            memset(&adapter->sidecar[index], 0,
                   sizeof(adapter->sidecar[index]));
            return FWLAB_NFC_API_OK;
        }
    }
    return FWLAB_NFC_API_NOT_FOUND;
}
