/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_internal.h"

#include <string.h>

static int pair_equal(const uint64_t left[2], const uint64_t right[2])
{
    return left[0] == right[0] && left[1] == right[1];
}

static int pair_zero(const uint64_t words[2])
{
    return words[0] == 0 && words[1] == 0;
}

static struct c34_registry_entry *find_registry(
    struct c34 *instance,
    const struct fwlab_c31_request_token *token
)
{
    unsigned int index;

    for (index = 0; index < C34_REQUEST_SLOTS; ++index) {
        if (instance->registry[index].used &&
            pair_equal(instance->registry[index].token.word, token->word)) {
            return &instance->registry[index];
        }
    }
    return NULL;
}

static struct fwlab_c31_provider_submit_result reject(uint32_t reason)
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

static int outer_shape_valid(
    const struct fwlab_c31_provider_request *request
)
{
    return request != NULL &&
           request->version == FWLAB_C31_PROVIDER_CONTRACT_VERSION &&
           request->size == sizeof(*request) && request->reserved0 == 0 &&
           request->reserved1 == 0 && request->operation.reserved == 0 &&
           request->provider_kind == FWLAB_C31_PROVIDER_NFC &&
           request->dma_direction == FWLAB_C31_DMA_NONE &&
           request->ordering_flags == 0 &&
           !pair_zero(request->request.word) &&
           pair_zero(request->capability.word) &&
           request->capability_offset == 0 &&
           request->controller_region == 0 &&
           request->controller_offset == 0 && request->length == 0;
}

static struct fwlab_c31_provider_submit_result provider_submit(
    void *context,
    const struct fwlab_c31_provider_request *request
)
{
    struct c34 *instance = context;
    struct c34_registry_entry *registry;
    struct fwlab_c31_provider_submit_result result;
    enum c34_result accepted;

    if (!c34_instance_valid(instance) || !outer_shape_valid(request)) {
        return reject(FWLAB_C31_REASON_PROVIDER_REJECTED);
    }
    registry = find_registry(instance, &request->request);
    if (registry == NULL) {
        return reject(FWLAB_C31_REASON_PROVIDER_REJECTED);
    }
    accepted = c34_accept_outer(instance, request, &registry->request);
    memset(&result, 0, sizeof(result));
    if (accepted == C34_NO_CAPACITY || accepted == C34_WRONG_STATE) {
        result.disposition = FWLAB_C31_PROVIDER_BACKPRESSURE;
        return result;
    }
    if (accepted != C34_OK) {
        return reject(accepted == C34_COUNTER_EXHAUSTED ?
                          FWLAB_C31_REASON_INVARIANT :
                          FWLAB_C31_REASON_PROVIDER_REJECTED);
    }
    registry->used = 0;
    result.disposition = FWLAB_C31_PROVIDER_ACCEPTED;
    return result;
}

static enum fwlab_c31_api_result provider_cancel(
    void *context,
    const struct fwlab_c31_operation_token *operation
)
{
    enum c34_result result = c34_cancel_outer(context, operation);

    return result == C34_OK ? FWLAB_C31_API_OK :
           result == C34_NOT_FOUND ? FWLAB_C31_API_NOT_FOUND :
           result == C34_WRONG_STATE ? FWLAB_C31_API_WRONG_STATE :
                                       FWLAB_C31_API_INVARIANT_FAILURE;
}

static void fill_sidecar(struct c34 *instance)
{
    struct c34_graph *graph = &instance->graph;
    struct c34_sidecar *sidecar =
        &instance->sidecar[graph->sidecar_index];
    struct c34_command_result *result = &sidecar->result;

    memset(result, 0, sizeof(*result));
    result->version = C34_CONTRACT_VERSION;
    result->size = sizeof(*result);
    result->status = graph->maintenance_result;
    result->request_kind = graph->request.kind;
    result->atom_mask = graph->request.kind == C34_REQUEST_READ ?
        (uint8_t)(1u << graph->active_atom) : graph->request.atom_mask;
    result->present_mask = graph->result_present_mask;
    result->outer = graph->outer;
    result->witness = graph->witness;
    if (graph->request.kind == C34_REQUEST_READ &&
        graph->result_present_mask != 0) {
        memcpy(result->payload[graph->active_atom], graph->main,
               C34_ATOM_BYTES);
    }
    result->physical_digest = graph->completion.payload_digest;
    sidecar->ready = 1;
}

static void fill_event(
    struct c34 *instance,
    struct fwlab_c31_provider_event *event
)
{
    struct c34_graph *graph = &instance->graph;

    memset(event, 0, sizeof(*event));
    event->version = FWLAB_C31_PROVIDER_CONTRACT_VERSION;
    event->size = sizeof(*event);
    event->operation = graph->outer;
    event->terminal = graph->terminal;
    if (graph->terminal != FWLAB_C31_PROVIDER_SUCCESS) {
        event->fault.domain = FWLAB_C31_FAULT_MEDIA;
        event->fault.retry_class = FWLAB_C31_RETRY_NEVER;
        event->fault.effect_class = graph->failure_effect;
        event->fault.reason = graph->terminal ==
            FWLAB_C31_PROVIDER_CANCELLED ? FWLAB_C31_REASON_CANCELLED :
                                          FWLAB_C31_REASON_PROVIDER_FAILED;
    }
    fill_sidecar(instance);
    graph->outer_event_ready = 0;
    graph->outer_event_sent = 1;
}

static enum fwlab_c31_api_result provider_poll(
    void *context,
    uint32_t budget,
    struct fwlab_c31_provider_event *events,
    uint32_t event_capacity,
    uint32_t *event_count
)
{
    struct c34 *instance = context;
    uint32_t used = 0;
    uint32_t count = 0;

    if (!c34_instance_valid(instance) || budget == 0 || events == NULL ||
        event_capacity == 0 || event_count == NULL) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    while (used < budget) {
        if (instance->graph.kind == C34_GRAPH_OUTER &&
            instance->graph.outer_event_ready) {
            fill_event(instance, &events[count++]);
            ++used;
            if ((instance->graph.state == C34_GRAPH_DONE ||
                 instance->graph.state == C34_GRAPH_FAILED) &&
                instance->graph.outer_event_sent) {
                memset(&instance->graph, 0, sizeof(instance->graph));
            }
            if (count == event_capacity) {
                break;
            }
            continue;
        }
        if (instance->graph.kind == C34_GRAPH_NONE) {
            break;
        }
        if (c34_drive_one(instance) != C34_OK) {
            return FWLAB_C31_API_INVARIANT_FAILURE;
        }
        ++used;
    }
    *event_count = count;
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result provider_reset_begin(
    void *context,
    uint64_t instance_nonce,
    uint32_t old_epoch
)
{
    struct c34 *instance = context;

    if (!c34_instance_valid(instance) || instance->phase != 0 ||
        instance_nonce != instance->config.instance_nonce ||
        old_epoch != instance->current_epoch ||
        instance->current_epoch == UINT32_MAX) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    instance->phase = 1;
    instance->reset_old_epoch = old_epoch;
    ++instance->current_epoch;
    memset(instance->overlay_valid, 0, sizeof(instance->overlay_valid));
    memset(instance->overlay_kind, 0, sizeof(instance->overlay_kind));
    memset(instance->overlay_payload, 0, sizeof(instance->overlay_payload));
    memset(instance->registry, 0, sizeof(instance->registry));
    if (instance->nfc.ops->reset_begin(
            instance->nfc.context, instance_nonce, old_epoch) !=
        FWLAB_NFC_API_OK) {
        instance->phase = 2;
        return FWLAB_C31_API_INVARIANT_FAILURE;
    }
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result provider_quiescent(
    void *context,
    uint64_t instance_nonce,
    uint32_t old_epoch,
    bool *quiescent
)
{
    struct c34 *instance = context;
    bool nfc_quiescent;
    bool physical_quiescent;

    if (!c34_instance_valid(instance) || quiescent == NULL ||
        instance->phase != 1 ||
        instance_nonce != instance->config.instance_nonce ||
        old_epoch != instance->reset_old_epoch) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (instance->graph.kind != C34_GRAPH_NONE &&
        c34_drive_one(instance) != C34_OK) {
        instance->phase = 2;
        return FWLAB_C31_API_INVARIANT_FAILURE;
    }
    if (instance->nfc.ops->quiescent(
            instance->nfc.context, instance_nonce, old_epoch,
            &nfc_quiescent) != FWLAB_NFC_API_OK ||
        instance->physical.ops->quiescent(
            instance->physical.context, &physical_quiescent) !=
            C34_PHYSICAL_TXN_OK) {
        instance->phase = 2;
        return FWLAB_C31_API_INVARIANT_FAILURE;
    }
    *quiescent = nfc_quiescent && physical_quiescent &&
                 instance->graph.kind == C34_GRAPH_NONE;
    if (*quiescent) {
        memset(instance->sidecar, 0, sizeof(instance->sidecar));
        memset(instance->obligations, 0, sizeof(instance->obligations));
        instance->phase = 0;
    }
    return FWLAB_C31_API_OK;
}

static const struct fwlab_c31_provider_ops provider_ops = {
    .version = FWLAB_C31_PROVIDER_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_c31_provider_ops),
    .reserved = 0,
    .try_submit = provider_submit,
    .cancel = provider_cancel,
    .poll = provider_poll,
    .reset_begin = provider_reset_begin,
    .quiescent = provider_quiescent,
};

struct fwlab_c31_provider c34_c31_provider(struct c34 *instance)
{
    struct fwlab_c31_provider provider;

    provider.ops = c34_instance_valid(instance) ? &provider_ops : NULL;
    provider.context = c34_instance_valid(instance) ? instance : NULL;
    return provider;
}
