/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_fake_services.h"

#include <string.h>

static void record_event(void *context, enum c43_fake_event event)
{
    struct c43_fake_services *services = context;

    if (services->event_count < C43_FAKE_EVENT_CAPACITY) {
        services->events[services->event_count] = (uint32_t)event;
        ++services->event_count;
    } else {
        services->overflow = 1;
    }
}

static enum fwlab_hif_action_disposition queue_prepare_start(
    void *context,
    const struct fwlab_c43_queue_effect_request *request,
    struct fwlab_hif_action_submit_result *result)
{
    (void)request;
    (void)result;
    record_event(context, C43_FAKE_QUEUE_PREPARE_START);
    return FWLAB_HIF_ACTION_REJECTED;
}

static enum fwlab_c43_api_result queue_prepare_query(
    void *context,
    const struct fwlab_hif_action_token *token,
    struct fwlab_c43_queue_effect_terminal *terminal,
    bool *ready)
{
    (void)token;
    (void)terminal;
    (void)ready;
    record_event(context, C43_FAKE_QUEUE_PREPARE_QUERY);
    return FWLAB_C43_API_NOT_IMPLEMENTED;
}

static enum fwlab_hif_action_disposition queue_finish_start(
    void *context,
    const struct fwlab_c43_queue_finish_request *request,
    struct fwlab_hif_action_submit_result *result)
{
    (void)request;
    (void)result;
    record_event(context, C43_FAKE_QUEUE_FINISH_START);
    return FWLAB_HIF_ACTION_REJECTED;
}

static enum fwlab_c43_api_result queue_finish_query(
    void *context,
    const struct fwlab_hif_action_token *token,
    struct fwlab_c43_queue_effect_terminal *terminal,
    bool *ready)
{
    (void)token;
    (void)terminal;
    (void)ready;
    record_event(context, C43_FAKE_QUEUE_FINISH_QUERY);
    return FWLAB_C43_API_NOT_IMPLEMENTED;
}

#define DEFINE_TOKEN_CONTROL(name, event_id)                                    \
    static enum fwlab_c43_api_result name(                                     \
        void *context, const struct fwlab_hif_action_token *token)              \
    {                                                                           \
        (void)token;                                                            \
        record_event(context, event_id);                                        \
        return FWLAB_C43_API_NOT_IMPLEMENTED;                                   \
    }

#define DEFINE_EPOCH_CONTROL(name, event_id)                                    \
    static enum fwlab_c43_api_result name(void *context, uint32_t old_epoch)    \
    {                                                                           \
        (void)old_epoch;                                                        \
        record_event(context, event_id);                                        \
        return FWLAB_C43_API_NOT_IMPLEMENTED;                                   \
    }

#define DEFINE_QUIESCENT_CONTROL(name, event_id)                                \
    static enum fwlab_c43_api_result name(                                     \
        void *context, uint32_t epoch, bool *quiescent)                         \
    {                                                                           \
        (void)epoch;                                                            \
        (void)quiescent;                                                        \
        record_event(context, event_id);                                        \
        return FWLAB_C43_API_NOT_IMPLEMENTED;                                   \
    }

DEFINE_TOKEN_CONTROL(queue_cancel, C43_FAKE_QUEUE_CANCEL)
DEFINE_TOKEN_CONTROL(queue_retire, C43_FAKE_QUEUE_RETIRE)
DEFINE_EPOCH_CONTROL(queue_reset_begin, C43_FAKE_QUEUE_RESET_BEGIN)
DEFINE_QUIESCENT_CONTROL(queue_quiescent, C43_FAKE_QUEUE_QUIESCENT)

static enum fwlab_hif_action_disposition target_submit(
    void *context,
    const struct fwlab_c43_target_request *request,
    struct fwlab_hif_action_submit_result *result)
{
    (void)request;
    (void)result;
    record_event(context, C43_FAKE_TARGET_SUBMIT);
    return FWLAB_HIF_ACTION_REJECTED;
}

static enum fwlab_c43_api_result target_query(
    void *context,
    const struct fwlab_hif_action_token *token,
    struct fwlab_c43_target_terminal *terminal,
    bool *ready)
{
    (void)token;
    (void)terminal;
    (void)ready;
    record_event(context, C43_FAKE_TARGET_QUERY);
    return FWLAB_C43_API_NOT_IMPLEMENTED;
}

DEFINE_TOKEN_CONTROL(target_cancel, C43_FAKE_TARGET_CANCEL)
DEFINE_TOKEN_CONTROL(target_release, C43_FAKE_TARGET_RELEASE)
DEFINE_TOKEN_CONTROL(target_release_query, C43_FAKE_TARGET_RELEASE_QUERY)
DEFINE_EPOCH_CONTROL(target_reset_begin, C43_FAKE_TARGET_RESET_BEGIN)
DEFINE_QUIESCENT_CONTROL(target_quiescent, C43_FAKE_TARGET_QUIESCENT)

static enum fwlab_hif_action_disposition block_submit(
    void *context,
    const struct fwlab_c43_block_action_request *request,
    struct fwlab_hif_action_submit_result *result)
{
    (void)request;
    (void)result;
    record_event(context, C43_FAKE_BLOCK_SUBMIT);
    return FWLAB_HIF_ACTION_REJECTED;
}

static enum fwlab_c43_api_result block_query(
    void *context,
    const struct fwlab_hif_action_token *token,
    struct fwlab_c43_block_action_terminal *terminal,
    bool *ready)
{
    (void)token;
    (void)terminal;
    (void)ready;
    record_event(context, C43_FAKE_BLOCK_QUERY);
    return FWLAB_C43_API_NOT_IMPLEMENTED;
}

DEFINE_TOKEN_CONTROL(block_cancel, C43_FAKE_BLOCK_CANCEL)
DEFINE_TOKEN_CONTROL(block_retire, C43_FAKE_BLOCK_RETIRE)
DEFINE_EPOCH_CONTROL(block_reset_begin, C43_FAKE_BLOCK_RESET_BEGIN)
DEFINE_QUIESCENT_CONTROL(block_quiescent, C43_FAKE_BLOCK_QUIESCENT)

static const struct fwlab_c43_queue_effect_port_ops queue_ops = {
    .version = FWLAB_C43_QUEUE_EFFECT_PORT_VERSION,
    .size = sizeof(queue_ops),
    .prepare_start = queue_prepare_start,
    .prepare_query = queue_prepare_query,
    .finish_start = queue_finish_start,
    .finish_query = queue_finish_query,
    .cancel = queue_cancel,
    .retire = queue_retire,
    .reset_begin = queue_reset_begin,
    .quiescent = queue_quiescent,
};

static const struct fwlab_c43_target_resolver_port_ops target_ops = {
    .version = FWLAB_C43_TARGET_RESOLVER_PORT_VERSION,
    .size = sizeof(target_ops),
    .submit = target_submit,
    .query = target_query,
    .cancel = target_cancel,
    .release = target_release,
    .release_query = target_release_query,
    .reset_begin = target_reset_begin,
    .quiescent = target_quiescent,
};

static const struct fwlab_c43_block_action_port_ops block_ops = {
    .version = FWLAB_C43_BLOCK_ACTION_PORT_VERSION,
    .size = sizeof(block_ops),
    .submit = block_submit,
    .query = block_query,
    .cancel = block_cancel,
    .retire = block_retire,
    .reset_begin = block_reset_begin,
    .quiescent = block_quiescent,
};

void c43_fake_services_init(struct c43_fake_services *services)
{
    if (services != NULL) {
        memset(services, 0, sizeof(*services));
    }
}

void c43_fake_services_providers(
    struct c43_fake_services *services,
    struct fwlab_c43_graph_providers *providers)
{
    struct fwlab_c43_graph_providers local = {0};

    if (services == NULL || providers == NULL) {
        return;
    }
    local.version = FWLAB_C43_PROVIDER_BUNDLE_VERSION;
    local.size = sizeof(local);
    local.queue.ops = &queue_ops;
    local.queue.context = services;
    local.queue.generation = 1;
    local.target.ops = &target_ops;
    local.target.context = services;
    local.target.generation = 1;
    local.block.ops = &block_ops;
    local.block.context = services;
    local.block.generation = 1;
    local.block.capability_bits = FWLAB_C43_BLOCK_CAP_VALIDATION_ONLY;
    local.dma_generation = 1;
    *providers = local;
}
