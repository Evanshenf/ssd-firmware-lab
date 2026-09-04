/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_fake_services.h"

#include <string.h>

static struct c43_fake_event_record *record_event(
    void *context,
    enum c43_fake_event event)
{
    struct c43_fake_services *services = context;
    struct c43_fake_event_record *record;

    if (services->event_count < C43_FAKE_EVENT_CAPACITY) {
        record = &services->events[services->event_count];
        memset(record, 0, sizeof(*record));
        record->sequence = services->event_count + 1;
        record->kind = (uint32_t)event;
        ++services->event_count;
        return record;
    } else {
        services->overflow = 1;
        return NULL;
    }
}

static int token_equal(
    const struct fwlab_hif_action_token *left,
    const struct fwlab_hif_action_token *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static void submit_result_write(
    struct fwlab_hif_action_submit_result *result,
    const struct fwlab_hif_action_token *token)
{
    struct fwlab_hif_action_submit_result local = {0};

    local.version = FWLAB_HIF_ACTION_VERSION;
    local.size = sizeof(local);
    local.token = *token;
    local.disposition = FWLAB_HIF_ACTION_ACCEPTED;
    memcpy(result, &local, sizeof(local));
}

static void queue_terminal_build(
    struct c43_fake_services *services,
    int finish,
    struct fwlab_c43_queue_effect_terminal *terminal)
{
    struct fwlab_c43_queue_effect_terminal local = {0};
    const struct fwlab_hif_action_token *token =
        finish ? &services->queue_script.first_finish_request.token
               : &services->queue_script.first_prepare_request.common.token;

    local.version = FWLAB_C43_QUEUE_EFFECT_PORT_VERSION;
    local.size = sizeof(local);
    local.common.version = FWLAB_HIF_ACTION_VERSION;
    local.common.size = sizeof(local.common);
    local.common.token = *token;
    local.common.cookie =
        services->queue_script.first_prepare_request.common.cookie;
    local.common.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
    local.facts = services->queue_script.facts;
    if (!finish) {
        local.state = FWLAB_C43_QUEUE_EFFECT_PREPARED;
    } else {
        local.state = services->queue_script.finish_state;
        local.decision =
            services->queue_script.first_finish_request.decision;
        if (local.state == FWLAB_C43_QUEUE_EFFECT_COMMITTED ||
            local.state == FWLAB_C43_QUEUE_EFFECT_TOO_LATE) {
            local.common.effect_class = FWLAB_NVME_EFFECT_FULL;
            local.common.units_completed = 1;
        } else if (local.state == FWLAB_C43_QUEUE_EFFECT_POISONED) {
            local.common.terminal_kind = FWLAB_HIF_ACTION_FAILED;
            local.common.effect_class = FWLAB_NVME_EFFECT_UNKNOWN_PREFIX;
        }
    }
    memcpy(terminal, &local, sizeof(local));
}

static enum fwlab_hif_action_disposition queue_prepare_start(
    void *context,
    const struct fwlab_c43_queue_effect_request *request,
    struct fwlab_hif_action_submit_result *result)
{
    struct c43_fake_services *services = context;
    struct c43_fake_event_record *event =
        record_event(context, C43_FAKE_QUEUE_PREPARE_START);

    if (event != NULL && request != NULL) {
        event->input.queue_prepare = *request;
    }
    if (!services->queue_script.enabled) {
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_REJECTED;
        }
        return FWLAB_HIF_ACTION_REJECTED;
    }
    ++services->queue_script.prepare_start_calls;
    if (services->queue_script.prepare_start_calls == 1) {
        services->queue_script.first_prepare_request = *request;
    } else if (memcmp(&services->queue_script.first_prepare_request, request,
                      sizeof(*request)) != 0) {
        services->queue_script.fault = 1;
    }
    services->queue_script.last_prepare_request = *request;
    if (services->queue_script.fault) {
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_REJECTED;
        }
        return FWLAB_HIF_ACTION_REJECTED;
    }
    if (services->queue_script.prepare_backpressure_remaining != 0) {
        --services->queue_script.prepare_backpressure_remaining;
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_BACKPRESSURE;
        }
        return FWLAB_HIF_ACTION_BACKPRESSURE;
    }
    if (services->queue_script.prepare_accepted) {
        services->queue_script.fault = 1;
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_REJECTED;
        }
        return FWLAB_HIF_ACTION_REJECTED;
    }
    submit_result_write(result, &request->common.token);
    if (services->queue_script.corrupt_prepare_submit_token) {
        result->token.action_uid ^= UINT64_C(1);
    }
    services->queue_script.prepare_accepted = 1;
    if (event != NULL) {
        event->returned = FWLAB_HIF_ACTION_ACCEPTED;
        event->output_written = 1;
        event->output.submit = *result;
    }
    return FWLAB_HIF_ACTION_ACCEPTED;
}

static enum fwlab_c43_api_result queue_prepare_query(
    void *context,
    const struct fwlab_hif_action_token *token,
    struct fwlab_c43_queue_effect_terminal *terminal,
    bool *ready)
{
    struct c43_fake_services *services = context;
    struct c43_fake_event_record *event =
        record_event(context, C43_FAKE_QUEUE_PREPARE_QUERY);

    if (!services->queue_script.enabled) {
        if (event != NULL) {
            event->returned = FWLAB_C43_API_NOT_IMPLEMENTED;
        }
        return FWLAB_C43_API_NOT_IMPLEMENTED;
    }
    ++services->queue_script.prepare_query_calls;
    if (event != NULL) {
        event->input.token = *token;
    }
    if (!services->queue_script.prepare_accepted ||
        !token_equal(token,
                     &services->queue_script.first_prepare_request.common.token)) {
        if (event != NULL) {
            event->returned = FWLAB_C43_API_STALE;
        }
        return FWLAB_C43_API_STALE;
    }
    if (services->queue_script.prepare_not_ready_remaining != 0) {
        --services->queue_script.prepare_not_ready_remaining;
        *ready = false;
        if (event != NULL) {
            event->returned = FWLAB_C43_API_OK;
            event->ready_written = 1;
        }
        return FWLAB_C43_API_OK;
    }
    queue_terminal_build(services, 0, terminal);
    services->queue_script.prepared_terminal = *terminal;
    *ready = true;
    if (event != NULL) {
        event->returned = FWLAB_C43_API_OK;
        event->ready_written = 1;
        event->ready_value = 1;
        event->output_written = 1;
        event->output.queue_terminal = *terminal;
    }
    return FWLAB_C43_API_OK;
}

static enum fwlab_hif_action_disposition queue_finish_start(
    void *context,
    const struct fwlab_c43_queue_finish_request *request,
    struct fwlab_hif_action_submit_result *result)
{
    struct c43_fake_services *services = context;
    struct c43_fake_event_record *event =
        record_event(context, C43_FAKE_QUEUE_FINISH_START);

    if (event != NULL && request != NULL) {
        event->input.queue_finish = *request;
    }
    if (!services->queue_script.enabled) {
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_REJECTED;
        }
        return FWLAB_HIF_ACTION_REJECTED;
    }
    ++services->queue_script.finish_start_calls;
    if (services->queue_script.finish_start_calls == 1) {
        services->queue_script.first_finish_request = *request;
    } else if (memcmp(&services->queue_script.first_finish_request, request,
                      sizeof(*request)) != 0) {
        services->queue_script.fault = 1;
    }
    services->queue_script.last_finish_request = *request;
    if (services->queue_script.fault) {
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_REJECTED;
        }
        return FWLAB_HIF_ACTION_REJECTED;
    }
    if (services->queue_script.finish_backpressure_remaining != 0) {
        --services->queue_script.finish_backpressure_remaining;
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_BACKPRESSURE;
        }
        return FWLAB_HIF_ACTION_BACKPRESSURE;
    }
    if (services->queue_script.finish_accepted) {
        services->queue_script.fault = 1;
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_REJECTED;
        }
        return FWLAB_HIF_ACTION_REJECTED;
    }
    submit_result_write(result, &request->token);
    if (services->queue_script.corrupt_finish_submit_token) {
        result->token.action_uid ^= UINT64_C(1);
    }
    services->queue_script.finish_accepted = 1;
    if (event != NULL) {
        event->returned = FWLAB_HIF_ACTION_ACCEPTED;
        event->output_written = 1;
        event->output.submit = *result;
    }
    return FWLAB_HIF_ACTION_ACCEPTED;
}

static enum fwlab_c43_api_result queue_finish_query(
    void *context,
    const struct fwlab_hif_action_token *token,
    struct fwlab_c43_queue_effect_terminal *terminal,
    bool *ready)
{
    struct c43_fake_services *services = context;
    struct c43_fake_event_record *event =
        record_event(context, C43_FAKE_QUEUE_FINISH_QUERY);

    if (!services->queue_script.enabled) {
        if (event != NULL) {
            event->returned = FWLAB_C43_API_NOT_IMPLEMENTED;
        }
        return FWLAB_C43_API_NOT_IMPLEMENTED;
    }
    ++services->queue_script.finish_query_calls;
    if (event != NULL) {
        event->input.token = *token;
    }
    if (!services->queue_script.finish_accepted ||
        !token_equal(token,
                     &services->queue_script.first_finish_request.token)) {
        if (event != NULL) {
            event->returned = FWLAB_C43_API_STALE;
        }
        return FWLAB_C43_API_STALE;
    }
    if (services->queue_script.finish_not_ready_remaining != 0) {
        --services->queue_script.finish_not_ready_remaining;
        *ready = false;
        if (event != NULL) {
            event->returned = FWLAB_C43_API_OK;
            event->ready_written = 1;
        }
        return FWLAB_C43_API_OK;
    }
    queue_terminal_build(services, 1, terminal);
    services->queue_script.finish_terminal = *terminal;
    *ready = true;
    if (event != NULL) {
        event->returned = FWLAB_C43_API_OK;
        event->ready_written = 1;
        event->ready_value = 1;
        event->output_written = 1;
        event->output.queue_terminal = *terminal;
    }
    return FWLAB_C43_API_OK;
}

#define DEFINE_TOKEN_CONTROL(name, event_id)                                    \
    static enum fwlab_c43_api_result name(                                     \
        void *context, const struct fwlab_hif_action_token *token)              \
    {                                                                           \
        struct c43_fake_event_record *event = record_event(context, event_id);  \
        if (event != NULL) {                                                    \
            if (token != NULL) {                                                \
                event->input.token = *token;                                    \
            }                                                                   \
            event->returned = FWLAB_C43_API_NOT_IMPLEMENTED;                   \
        }                                                                       \
        return FWLAB_C43_API_NOT_IMPLEMENTED;                                   \
    }

#define DEFINE_EPOCH_CONTROL(name, event_id)                                    \
    static enum fwlab_c43_api_result name(void *context, uint32_t old_epoch)    \
    {                                                                           \
        struct c43_fake_event_record *event = record_event(context, event_id);  \
        if (event != NULL) {                                                    \
            event->input.epoch = old_epoch;                                     \
            event->returned = FWLAB_C43_API_NOT_IMPLEMENTED;                   \
        }                                                                       \
        return FWLAB_C43_API_NOT_IMPLEMENTED;                                   \
    }

#define DEFINE_QUIESCENT_CONTROL(name, event_id)                                \
    static enum fwlab_c43_api_result name(                                     \
        void *context, uint32_t epoch, bool *quiescent)                         \
    {                                                                           \
        struct c43_fake_event_record *event = record_event(context, event_id);  \
        (void)quiescent;                                                        \
        if (event != NULL) {                                                    \
            event->input.epoch = epoch;                                         \
            event->returned = FWLAB_C43_API_NOT_IMPLEMENTED;                   \
        }                                                                       \
        return FWLAB_C43_API_NOT_IMPLEMENTED;                                   \
    }

DEFINE_TOKEN_CONTROL(queue_cancel, C43_FAKE_QUEUE_CANCEL)

static enum fwlab_c43_api_result queue_retire(
    void *context,
    const struct fwlab_hif_action_token *token)
{
    struct c43_fake_services *services = context;
    struct c43_fake_event_record *event =
        record_event(context, C43_FAKE_QUEUE_RETIRE);
    enum fwlab_c43_api_result result = FWLAB_C43_API_NOT_IMPLEMENTED;

    if (services->queue_script.enabled && token != NULL) {
        ++services->queue_script.retire_calls;
        if (!token_equal(token,
                         &services->queue_script.first_prepare_request.common.token)) {
            result = FWLAB_C43_API_STALE;
        } else if (services->queue_script.retire_in_progress_remaining != 0) {
            --services->queue_script.retire_in_progress_remaining;
            result = FWLAB_C43_API_IN_PROGRESS;
        } else {
            result = FWLAB_C43_API_OK;
        }
    }
    if (event != NULL) {
        if (token != NULL) {
            event->input.token = *token;
        }
        event->returned = (uint32_t)result;
    }
    return result;
}

DEFINE_EPOCH_CONTROL(queue_reset_begin, C43_FAKE_QUEUE_RESET_BEGIN)
DEFINE_QUIESCENT_CONTROL(queue_quiescent, C43_FAKE_QUEUE_QUIESCENT)

static void target_terminal_build(
    struct c43_fake_services *services,
    struct fwlab_c43_target_terminal *terminal)
{
    struct fwlab_c43_target_terminal local = {0};

    local.version = FWLAB_C43_TARGET_RESOLVER_PORT_VERSION;
    local.size = sizeof(local);
    local.common.version = FWLAB_HIF_ACTION_VERSION;
    local.common.size = sizeof(local.common);
    local.common.token = services->target_script.first_request.common.token;
    local.common.cookie = services->target_script.first_request.common.cookie;
    local.outcome = services->target_script.outcome;
    if (local.outcome == FWLAB_C43_TARGET_FOUND) {
        local.common.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
        local.target = services->target_script.target;
        local.reference = services->target_script.reference;
    } else if (local.outcome == FWLAB_C43_TARGET_NOT_FOUND ||
               local.outcome == FWLAB_C43_TARGET_TOO_LATE ||
               local.outcome == FWLAB_C43_TARGET_RELEASED) {
        local.common.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
    } else if (local.outcome == FWLAB_C43_TARGET_STALE ||
               local.outcome == FWLAB_C43_TARGET_SUPERSEDED) {
        local.common.terminal_kind = FWLAB_HIF_ACTION_CANCELLED;
    } else {
        local.common.terminal_kind = FWLAB_HIF_ACTION_FAILED;
    }
    memcpy(terminal, &local, sizeof(local));
}

static enum fwlab_hif_action_disposition target_submit(
    void *context,
    const struct fwlab_c43_target_request *request,
    struct fwlab_hif_action_submit_result *result)
{
    struct c43_fake_services *services = context;
    struct c43_fake_event_record *event =
        record_event(context, C43_FAKE_TARGET_SUBMIT);

    if (event != NULL && request != NULL) {
        event->input.target = *request;
    }
    if (!services->target_script.enabled || request == NULL) {
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_REJECTED;
        }
        return FWLAB_HIF_ACTION_REJECTED;
    }
    ++services->target_script.submit_calls;
    if (services->target_script.submit_calls == 1) {
        services->target_script.first_request = *request;
    } else if (memcmp(&services->target_script.first_request, request,
                      sizeof(*request)) != 0) {
        services->target_script.fault = 1;
    }
    services->target_script.last_request = *request;
    if (services->target_script.fault) {
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_REJECTED;
        }
        return FWLAB_HIF_ACTION_REJECTED;
    }
    if (services->target_script.backpressure_remaining != 0) {
        --services->target_script.backpressure_remaining;
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_BACKPRESSURE;
        }
        return FWLAB_HIF_ACTION_BACKPRESSURE;
    }
    if (services->target_script.accepted) {
        services->target_script.fault = 1;
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_REJECTED;
        }
        return FWLAB_HIF_ACTION_REJECTED;
    }
    submit_result_write(result, &request->common.token);
    if (services->target_script.corrupt_submit_token) {
        result->token.action_uid ^= UINT64_C(1);
    }
    services->target_script.accepted = 1;
    if (event != NULL) {
        event->returned = FWLAB_HIF_ACTION_ACCEPTED;
        event->output_written = 1;
        event->output.submit = *result;
    }
    return FWLAB_HIF_ACTION_ACCEPTED;
}

static enum fwlab_c43_api_result target_query(
    void *context,
    const struct fwlab_hif_action_token *token,
    struct fwlab_c43_target_terminal *terminal,
    bool *ready)
{
    struct c43_fake_services *services = context;
    struct c43_fake_event_record *event =
        record_event(context, C43_FAKE_TARGET_QUERY);

    if (event != NULL && token != NULL) {
        event->input.token = *token;
    }
    if (!services->target_script.enabled) {
        if (event != NULL) {
            event->returned = FWLAB_C43_API_NOT_IMPLEMENTED;
        }
        return FWLAB_C43_API_NOT_IMPLEMENTED;
    }
    ++services->target_script.query_calls;
    if (!services->target_script.accepted || token == NULL ||
        !token_equal(token,
                     &services->target_script.first_request.common.token)) {
        if (event != NULL) {
            event->returned = FWLAB_C43_API_STALE;
        }
        return FWLAB_C43_API_STALE;
    }
    if (services->target_script.not_ready_remaining != 0) {
        --services->target_script.not_ready_remaining;
        *ready = false;
        if (event != NULL) {
            event->returned = FWLAB_C43_API_OK;
            event->ready_written = 1;
        }
        return FWLAB_C43_API_OK;
    }
    target_terminal_build(services, terminal);
    services->target_script.terminal = *terminal;
    *ready = true;
    if (event != NULL) {
        event->returned = FWLAB_C43_API_OK;
        event->ready_written = 1;
        event->ready_value = 1;
        event->output_written = 1;
        event->output.target_terminal = *terminal;
    }
    return FWLAB_C43_API_OK;
}

DEFINE_TOKEN_CONTROL(target_cancel, C43_FAKE_TARGET_CANCEL)
DEFINE_TOKEN_CONTROL(target_release, C43_FAKE_TARGET_RELEASE)
DEFINE_TOKEN_CONTROL(target_release_query, C43_FAKE_TARGET_RELEASE_QUERY)
DEFINE_EPOCH_CONTROL(target_reset_begin, C43_FAKE_TARGET_RESET_BEGIN)
DEFINE_QUIESCENT_CONTROL(target_quiescent, C43_FAKE_TARGET_QUIESCENT)

static void block_terminal_build(
    struct c43_fake_services *services,
    struct fwlab_c43_block_action_terminal *terminal)
{
    const struct fwlab_c43_block_action_request *request =
        &services->block_script.first_request;
    struct fwlab_c43_block_action_terminal local = {0};

    local.version = FWLAB_C43_BLOCK_ACTION_PORT_VERSION;
    local.size = sizeof(local);
    local.common.version = FWLAB_HIF_ACTION_VERSION;
    local.common.size = sizeof(local.common);
    local.common.token = request->common.token;
    local.common.cookie = request->common.cookie;
    local.witness.version = FWLAB_C43_POLICY_VERSION;
    local.witness.size = sizeof(local.witness);
    local.witness.command = request->common.token.command;
    local.witness.origin = request->common.token.origin;
    local.witness.provider_generation =
        services->block_script.witness_provider_generation;
    local.block_terminal_kind = services->block_script.terminal_kind;
    if (local.block_terminal_kind == FWLAB_C43_BLOCK_VALIDATED_ONLY) {
        local.common.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
        local.witness.witness_mask = FWLAB_C43_WITNESS_VALIDATED_ONLY;
        local.witness.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
    } else if (local.block_terminal_kind ==
               FWLAB_C43_BLOCK_FAILED_NO_EFFECT) {
        local.common.terminal_kind = FWLAB_HIF_ACTION_FAILED;
        local.witness.terminal_kind = FWLAB_HIF_ACTION_FAILED;
    } else if (local.block_terminal_kind == FWLAB_C43_BLOCK_CANCELLED) {
        local.common.terminal_kind = FWLAB_HIF_ACTION_CANCELLED;
        local.witness.terminal_kind = FWLAB_HIF_ACTION_CANCELLED;
    } else {
        local.common.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
        local.common.effect_class = FWLAB_NVME_EFFECT_FULL;
        local.common.units_completed = request->common.requested_units;
        local.witness.predecessor.word[0] = 1;
        local.witness.witness_mask = FWLAB_C43_WITNESS_BLOCK_READ_READY;
        local.witness.units_completed = request->common.requested_units;
        local.witness.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
        local.witness.effect_class = FWLAB_NVME_EFFECT_FULL;
    }
    memcpy(terminal, &local, sizeof(local));
}

static enum fwlab_hif_action_disposition block_submit(
    void *context,
    const struct fwlab_c43_block_action_request *request,
    struct fwlab_hif_action_submit_result *result)
{
    struct c43_fake_services *services = context;
    struct c43_fake_event_record *event =
        record_event(context, C43_FAKE_BLOCK_SUBMIT);

    if (event != NULL && request != NULL) {
        event->input.block = *request;
    }
    if (!services->block_script.enabled || request == NULL) {
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_REJECTED;
        }
        return FWLAB_HIF_ACTION_REJECTED;
    }
    ++services->block_script.submit_calls;
    if (services->block_script.submit_calls == 1) {
        services->block_script.first_request = *request;
    } else if (memcmp(&services->block_script.first_request, request,
                      sizeof(*request)) != 0) {
        services->block_script.fault = 1;
    }
    services->block_script.last_request = *request;
    if (services->block_script.fault) {
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_REJECTED;
        }
        return FWLAB_HIF_ACTION_REJECTED;
    }
    if (services->block_script.backpressure_remaining != 0) {
        --services->block_script.backpressure_remaining;
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_BACKPRESSURE;
        }
        return FWLAB_HIF_ACTION_BACKPRESSURE;
    }
    if (services->block_script.accepted) {
        services->block_script.fault = 1;
        if (event != NULL) {
            event->returned = FWLAB_HIF_ACTION_REJECTED;
        }
        return FWLAB_HIF_ACTION_REJECTED;
    }
    submit_result_write(result, &request->common.token);
    if (services->block_script.corrupt_submit_token) {
        result->token.action_uid ^= UINT64_C(1);
    }
    services->block_script.accepted = 1;
    if (event != NULL) {
        event->returned = FWLAB_HIF_ACTION_ACCEPTED;
        event->output_written = 1;
        event->output.submit = *result;
    }
    return FWLAB_HIF_ACTION_ACCEPTED;
}

static enum fwlab_c43_api_result block_query(
    void *context,
    const struct fwlab_hif_action_token *token,
    struct fwlab_c43_block_action_terminal *terminal,
    bool *ready)
{
    struct c43_fake_services *services = context;
    struct c43_fake_event_record *event =
        record_event(context, C43_FAKE_BLOCK_QUERY);

    if (event != NULL && token != NULL) {
        event->input.token = *token;
    }
    if (!services->block_script.enabled) {
        if (event != NULL) {
            event->returned = FWLAB_C43_API_NOT_IMPLEMENTED;
        }
        return FWLAB_C43_API_NOT_IMPLEMENTED;
    }
    ++services->block_script.query_calls;
    if (!services->block_script.accepted || token == NULL ||
        !token_equal(token,
                     &services->block_script.first_request.common.token)) {
        if (event != NULL) {
            event->returned = FWLAB_C43_API_STALE;
        }
        return FWLAB_C43_API_STALE;
    }
    if (services->block_script.not_ready_remaining != 0) {
        --services->block_script.not_ready_remaining;
        *ready = false;
        if (event != NULL) {
            event->returned = FWLAB_C43_API_OK;
            event->ready_written = 1;
        }
        return FWLAB_C43_API_OK;
    }
    block_terminal_build(services, terminal);
    services->block_script.terminal = *terminal;
    *ready = true;
    if (event != NULL) {
        event->returned = FWLAB_C43_API_OK;
        event->ready_written = 1;
        event->ready_value = 1;
        event->output_written = 1;
        event->output.block_terminal = *terminal;
    }
    return FWLAB_C43_API_OK;
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

void c43_fake_queue_script_configure(
    struct c43_fake_services *services,
    const struct fwlab_c43_queue_facts *facts,
    uint32_t finish_state,
    uint32_t prepare_backpressure,
    uint32_t prepare_not_ready,
    uint32_t finish_backpressure,
    uint32_t finish_not_ready)
{
    struct c43_fake_queue_script script = {0};

    if (services == NULL || facts == NULL) {
        return;
    }
    script.enabled = 1;
    script.prepare_backpressure_remaining = prepare_backpressure;
    script.prepare_not_ready_remaining = prepare_not_ready;
    script.finish_backpressure_remaining = finish_backpressure;
    script.finish_not_ready_remaining = finish_not_ready;
    script.finish_state = finish_state;
    script.facts = *facts;
    services->queue_script = script;
}

void c43_fake_target_script_configure(
    struct c43_fake_services *services,
    uint32_t outcome,
    const struct fwlab_hif_command_ticket *target,
    const struct fwlab_c43_abort_target_ref *reference,
    uint32_t backpressure,
    uint32_t not_ready)
{
    struct c43_fake_target_script script = {0};

    if (services == NULL) {
        return;
    }
    script.enabled = 1;
    script.outcome = outcome;
    script.backpressure_remaining = backpressure;
    script.not_ready_remaining = not_ready;
    if (target != NULL) {
        script.target = *target;
    }
    if (reference != NULL) {
        script.reference = *reference;
    }
    services->target_script = script;
}

void c43_fake_block_script_configure(
    struct c43_fake_services *services,
    uint32_t terminal_kind,
    uint32_t backpressure,
    uint32_t not_ready)
{
    struct c43_fake_block_script script = {0};

    if (services == NULL) {
        return;
    }
    script.enabled = 1;
    script.terminal_kind = terminal_kind;
    script.backpressure_remaining = backpressure;
    script.not_ready_remaining = not_ready;
    script.witness_provider_generation = 1;
    services->block_script = script;
}
