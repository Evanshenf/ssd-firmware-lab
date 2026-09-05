/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "j0_internal.h"

#include <string.h>

static uint32_t witness_for_kind(uint16_t kind)
{
    switch (kind) {
    case FWLAB_HOST_ACTION_V0_PAYLOAD_FILL:
        return FWLAB_HOST_WITNESS_V0_PAYLOAD_READY;
    case FWLAB_HOST_ACTION_V0_QUEUE_EFFECT:
        return FWLAB_HOST_WITNESS_V0_QUEUE_EFFECT;
    case FWLAB_HOST_ACTION_V0_TARGET_RESOLVE:
        return FWLAB_HOST_WITNESS_V0_TARGET_RESOLVED;
    case FWLAB_HOST_ACTION_V0_DMA_IN:
        return FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE;
    case FWLAB_HOST_ACTION_V0_DMA_OUT:
        return FWLAB_HOST_WITNESS_V0_DMA_OUT_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_READ:
        return FWLAB_HOST_WITNESS_V0_BLOCK_READ_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_WRITE:
        return FWLAB_HOST_WITNESS_V0_BLOCK_WRITE_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_FLUSH:
        return FWLAB_HOST_WITNESS_V0_BLOCK_FLUSH_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_TRIM:
        return FWLAB_HOST_WITNESS_V0_BLOCK_TRIM_COMPLETE;
    default:
        return 0;
    }
}

static struct j0_admission_record *admission_for_token(
    struct j0_runtime *runtime,
    const struct fwlab_host_action_token_v0 *token)
{
    struct j0_admission_record *match = NULL;
    uint32_t index;

    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        struct j0_admission_record *record = &runtime->admission[index];

        if (!record->occupied) {
            continue;
        }
        if (j0_handle_equal(&record->command.handle, &token->command) ||
            j0_origin_equal(&record->command.origin, &token->origin)) {
            if (!j0_handle_equal(&record->command.handle, &token->command) ||
                !j0_origin_equal(&record->command.origin, &token->origin) ||
                match != NULL) {
                runtime->poisoned = 1;
                return NULL;
            }
            match = record;
        }
    }
    return match;
}

static struct j0_action_record *action_for(
    struct j0_driver_lane *lane,
    const struct fwlab_host_action_token_v0 *token,
    const struct fwlab_host_action_argument_ref_v0 *argument,
    int create)
{
    struct j0_admission_record *admission;
    struct j0_action_record *action;

    if (lane == NULL || lane->runtime == NULL || token == NULL ||
        !fwlab_host_action_token_v0_valid(token) || token->kind != lane->kind ||
        token->ordinal >= FWLAB_HOST_ACTION_V0_MAX_ACTIONS) {
        return NULL;
    }
    admission = admission_for_token(lane->runtime, token);
    if (admission == NULL || !admission->lifecycle_owned ||
        token->ordinal >= admission->program.action_count) {
        return NULL;
    }
    action = &admission->action[token->ordinal];
    if (argument != NULL &&
        (memcmp(argument,
                &admission->program.action[token->ordinal].argument,
                sizeof(*argument)) != 0 ||
         memcmp(argument, &admission->argument[token->ordinal].reference,
                sizeof(*argument)) != 0)) {
        lane->runtime->poisoned = 1;
        return NULL;
    }
    if (!action->token_valid) {
        if (!create) {
            return NULL;
        }
        memset(action, 0, sizeof(*action));
        action->token = *token;
        action->token_valid = 1;
        action->state = J0_DRIVER_PREPARED;
        action->argument_ref =
            admission->program.action[token->ordinal].argument;
        action->argument = admission->argument[token->ordinal];
    } else if (!j0_action_token_equal(&action->token, token)) {
        lane->runtime->poisoned = 1;
        return NULL;
    }
    return action;
}

static struct j0_admission_record *admission_for_action(
    struct j0_runtime *runtime, const struct j0_action_record *action)
{
    return admission_for_token(runtime, &action->token);
}

static void submit_result_make(
    struct fwlab_host_action_submit_result_v0 *result,
    const struct fwlab_host_action_token_v0 *token, uint32_t disposition,
    uint32_t fault_domain, uint32_t fault_code)
{
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    result->size = (uint16_t)sizeof(*result);
    result->token = *token;
    result->disposition = disposition;
    result->fault_domain = fault_domain;
    result->fault_code = fault_code;
}

static void terminal_make(
    struct j0_action_record *action, uint32_t state, uint32_t terminal_kind,
    uint32_t effect, uint32_t units, uint32_t fault_domain,
    uint32_t fault_code)
{
    memset(&action->terminal, 0, sizeof(action->terminal));
    action->terminal.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    action->terminal.size = (uint16_t)sizeof(action->terminal);
    action->terminal.token = action->token;
    action->terminal.state = state;
    action->terminal.terminal_kind = terminal_kind;
    action->terminal.effect = effect;
    action->terminal.units_completed = units;
    action->terminal.fault_domain = fault_domain;
    action->terminal.fault_code = fault_code;
    if (terminal_kind == FWLAB_HOST_ACTION_V0_SUCCEEDED) {
        action->terminal.produced_witness_mask =
            witness_for_kind(action->token.kind);
    }
}

static uint32_t normalized_failure(uint16_t kind)
{
    switch (kind) {
    case FWLAB_HOST_ACTION_V0_DMA_IN:
    case FWLAB_HOST_ACTION_V0_DMA_OUT:
        return FWLAB_SPINE_PROVIDER_V0_TRANSFER_FAILURE;
    case FWLAB_HOST_ACTION_V0_BLOCK_READ:
        return FWLAB_SPINE_PROVIDER_V0_MEDIA_READ;
    case FWLAB_HOST_ACTION_V0_BLOCK_WRITE:
    case FWLAB_HOST_ACTION_V0_BLOCK_FLUSH:
        return FWLAB_SPINE_PROVIDER_V0_MEDIA_WRITE;
    default:
        return FWLAB_SPINE_PROVIDER_V0_RESOURCE_FAILURE;
    }
}

static enum fwlab_spine_result_v0 latch_terminal(
    struct j0_runtime *runtime, struct j0_action_record *action)
{
    struct j0_admission_record *admission =
        admission_for_action(runtime, action);
    uint32_t outcome;
    enum fwlab_spine_result_v0 result;

    if (admission == NULL ||
        !fwlab_host_action_status_v0_valid(&action->terminal)) {
        runtime->poisoned = 1;
        return FWLAB_SPINE_V0_POISONED;
    }
    if (action->terminal.terminal_kind == FWLAB_HOST_ACTION_V0_SUCCEEDED) {
        outcome = FWLAB_SPINE_PROVIDER_V0_SUCCESS;
    } else if (action->terminal.terminal_kind ==
               FWLAB_HOST_ACTION_V0_CANCELLED) {
        outcome = FWLAB_SPINE_PROVIDER_V0_CANCELLED;
    } else {
        outcome = normalized_failure(action->token.kind);
    }
    result = admission->binding.result_latch(
        admission->binding.adapter.context, &action->argument_ref,
        &action->terminal, outcome, 0);
    if (result != FWLAB_SPINE_V0_OK) {
        runtime->poisoned = 1;
        return result;
    }
    action->result_latched = 1;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 payload_submit(
    struct j0_driver_lane *lane, struct j0_action_record *action,
    struct fwlab_host_action_submit_result_v0 *result)
{
    struct j0_admission_record *admission =
        admission_for_action(lane->runtime, action);
    uint8_t payload[J0_MAX_TRANSFER_BYTES];
    uint32_t actual = 0;

    if (admission == NULL || !admission->buffer_held ||
        action->argument.payload_bytes == 0 ||
        action->argument.payload_bytes != admission->transfer_bytes ||
        action->argument.payload_bytes > sizeof(payload) ||
        admission->binding.payload_read(
            admission->binding.adapter.context, &action->argument_ref,
            payload, action->argument.payload_bytes, &actual) !=
            FWLAB_SPINE_V0_OK ||
        actual != action->argument.payload_bytes ||
        lane->runtime->buffer.port.ops->write(
            lane->runtime->buffer.port.context, &admission->buffer,
            &admission->span, payload, actual) !=
            FWLAB_CONTROLLER_BUFFER_V0_OK) {
        lane->runtime->poisoned = 1;
        return FWLAB_SPINE_V0_POISONED;
    }
    terminal_make(action, FWLAB_HOST_ACTION_V0_STATE_TERMINAL,
                  FWLAB_HOST_ACTION_V0_SUCCEEDED,
                  FWLAB_HOST_ACTION_V0_EFFECT_FULL, 1, 0, 0);
    if (latch_terminal(lane->runtime, action) != FWLAB_SPINE_V0_OK) {
        return FWLAB_SPINE_V0_POISONED;
    }
    action->state = J0_DRIVER_TERMINAL_LATCHED;
    submit_result_make(result, &action->token,
                       FWLAB_HOST_ACTION_V0_ACCEPTED, 0, 0);
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 dma_submit_action(
    struct j0_driver_lane *lane, struct j0_action_record *action,
    struct fwlab_host_action_submit_result_v0 *result)
{
    struct j0_runtime *runtime = lane->runtime;
    struct j0_admission_record *admission =
        admission_for_action(runtime, action);
    struct fwlab_dma_submit_result_v0 submitted;
    enum fwlab_spine_result_v0 lower;

    if (admission == NULL || !admission->buffer_held ||
        !admission->authority_held || action->argument.exact_bytes == 0 ||
        action->argument.exact_bytes != admission->transfer_bytes) {
        runtime->poisoned = 1;
        return FWLAB_SPINE_V0_POISONED;
    }
    if (!action->lower_token_valid) {
        lower = runtime->host_binding.data.ops->token_reserve(
            runtime->host_binding.data.context, &action->token, &action->dma_token);
        if (lower != FWLAB_SPINE_V0_OK) {
            return lower;
        }
        memset(&action->dma_request, 0, sizeof(action->dma_request));
        action->dma_request.version = FWLAB_HOST_DATA_V0_VERSION;
        action->dma_request.size = (uint16_t)sizeof(action->dma_request);
        action->dma_request.operation = action->dma_token;
        action->dma_request.authority = admission->authority;
        action->dma_request.buffer = admission->buffer;
        action->dma_request.span = admission->span;
        action->dma_request.execution_epoch = runtime->config.execution_epoch;
        action->dma_request.exact_bytes = action->argument.exact_bytes;
        action->dma_request.direction =
            action->token.kind == FWLAB_HOST_ACTION_V0_DMA_IN
                ? FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER
                : FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST;
        if (!fwlab_dma_request_v0_valid(&action->dma_request)) {
            runtime->poisoned = 1;
            return FWLAB_SPINE_V0_POISONED;
        }
        action->lower_token_valid = 1;
    }
    memset(&submitted, 0, sizeof(submitted));
    action->state = J0_DRIVER_SUBMIT_UNKNOWN;
    lower = runtime->host_binding.data.ops->submit(
        runtime->host_binding.data.context, &action->dma_request, &submitted);
    if (lower == FWLAB_SPINE_V0_IN_PROGRESS) {
        return lower;
    }
    if (lower != FWLAB_SPINE_V0_OK ||
        !fwlab_dma_submit_result_v0_matches_request(
            &submitted, &action->dma_request)) {
        runtime->poisoned = 1;
        return FWLAB_SPINE_V0_POISONED;
    }
    if (submitted.disposition == FWLAB_HOST_ACTION_V0_BACKPRESSURE) {
        action->state = J0_DRIVER_PREPARED;
        submit_result_make(result, &action->token,
                           FWLAB_HOST_ACTION_V0_BACKPRESSURE, 0, 0);
    } else if (submitted.disposition == FWLAB_HOST_ACTION_V0_REJECTED) {
        action->state = J0_DRIVER_REJECTED_CLEAN;
        submit_result_make(result, &action->token,
                           FWLAB_HOST_ACTION_V0_REJECTED,
                           submitted.fault_domain, submitted.fault_code);
    } else {
        action->state = J0_DRIVER_ACCEPTED;
        submit_result_make(result, &action->token,
                           FWLAB_HOST_ACTION_V0_ACCEPTED, 0, 0);
    }
    return FWLAB_SPINE_V0_OK;
}

static uint32_t block_operation(uint16_t kind)
{
    switch (kind) {
    case FWLAB_HOST_ACTION_V0_BLOCK_READ:
        return FWLAB_BLOCK_V0_READ;
    case FWLAB_HOST_ACTION_V0_BLOCK_WRITE:
        return FWLAB_BLOCK_V0_WRITE;
    case FWLAB_HOST_ACTION_V0_BLOCK_FLUSH:
        return FWLAB_BLOCK_V0_FLUSH;
    case FWLAB_HOST_ACTION_V0_BLOCK_TRIM:
        return FWLAB_BLOCK_V0_TRIM;
    default:
        return 0;
    }
}

static enum fwlab_spine_result_v0 block_submit_action(
    struct j0_driver_lane *lane, struct j0_action_record *action,
    struct fwlab_host_action_submit_result_v0 *result)
{
    struct j0_runtime *runtime = lane->runtime;
    struct j0_admission_record *admission =
        admission_for_action(runtime, action);
    struct fwlab_block_submit_result_v0 submitted;
    enum fwlab_spine_result_v0 lower;

    if (admission == NULL) {
        return FWLAB_SPINE_V0_POISONED;
    }
    if (!action->lower_token_valid) {
        memset(&action->block_request, 0, sizeof(action->block_request));
        action->block_request.version = FWLAB_BLOCK_SERVICE_V0_VERSION;
        action->block_request.size =
            (uint16_t)sizeof(action->block_request);
        action->block_request.operation_token.version =
            FWLAB_BLOCK_SERVICE_V0_VERSION;
        action->block_request.operation_token.size =
            (uint16_t)sizeof(action->block_request.operation_token);
        action->block_request.operation_token.type_tag =
            FWLAB_BLOCK_OP_TOKEN_V0_TAG;
        action->block_request.operation_token.action = action->token;
        action->block_request.operation_token.provider_nonce =
            runtime->block.provider_nonce;
        action->block_request.operation_token.generation =
            runtime->block.generation;
        action->block_request.namespace_ref = runtime->namespace_ref;
        action->block_request.lba = action->argument.lba;
        action->block_request.lba_count = action->argument.lba_count;
        action->block_request.operation = block_operation(action->token.kind);
        action->block_request.durability = action->argument.durability;
        if (action->token.kind == FWLAB_HOST_ACTION_V0_BLOCK_READ ||
            action->token.kind == FWLAB_HOST_ACTION_V0_BLOCK_WRITE) {
            if (!admission->buffer_held || action->argument.exact_bytes == 0 ||
                action->argument.exact_bytes != admission->transfer_bytes) {
                runtime->poisoned = 1;
                return FWLAB_SPINE_V0_POISONED;
            }
            action->block_request.buffer_present = 1;
            action->block_request.buffer = admission->buffer;
            action->block_request.buffer_span = admission->span;
        }
        if (!fwlab_block_request_v0_valid(&action->block_request)) {
            runtime->poisoned = 1;
            return FWLAB_SPINE_V0_POISONED;
        }
        action->lower_token_valid = 1;
    }
    memset(&submitted, 0, sizeof(submitted));
    action->state = J0_DRIVER_SUBMIT_UNKNOWN;
    lower = runtime->block.ops->submit(runtime->block.context,
                                       &action->block_request, &submitted);
    if (lower == FWLAB_SPINE_V0_IN_PROGRESS) {
        return lower;
    }
    if (lower != FWLAB_SPINE_V0_OK ||
        !fwlab_block_submit_result_v0_matches_request(
            &submitted, &action->block_request)) {
        runtime->poisoned = 1;
        return FWLAB_SPINE_V0_POISONED;
    }
    if (submitted.disposition == FWLAB_HOST_ACTION_V0_BACKPRESSURE) {
        action->state = J0_DRIVER_PREPARED;
        submit_result_make(result, &action->token,
                           FWLAB_HOST_ACTION_V0_BACKPRESSURE, 0, 0);
    } else if (submitted.disposition == FWLAB_HOST_ACTION_V0_REJECTED) {
        action->state = J0_DRIVER_REJECTED_CLEAN;
        submit_result_make(result, &action->token,
                           FWLAB_HOST_ACTION_V0_REJECTED,
                           submitted.fault_domain, submitted.fault_code);
    } else {
        action->state = J0_DRIVER_ACCEPTED;
        submit_result_make(result, &action->token,
                           FWLAB_HOST_ACTION_V0_ACCEPTED, 0, 0);
    }
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 driver_submit(
    void *context, const struct fwlab_host_action_token_v0 *token,
    const struct fwlab_host_action_argument_ref_v0 *argument,
    struct fwlab_host_action_submit_result_v0 *result)
{
    struct j0_driver_lane *lane = context;
    struct j0_action_record *action;

    if (lane == NULL || lane->runtime == NULL || argument == NULL ||
        result == NULL || lane->runtime->poisoned) {
        return FWLAB_SPINE_V0_INVALID;
    }
    action = action_for(lane, token, argument, 1);
    if (action == NULL || action->state != J0_DRIVER_PREPARED) {
        lane->runtime->poisoned = 1;
        return FWLAB_SPINE_V0_POISONED;
    }
    switch (lane->kind) {
    case FWLAB_HOST_ACTION_V0_PAYLOAD_FILL:
        return payload_submit(lane, action, result);
    case FWLAB_HOST_ACTION_V0_DMA_IN:
    case FWLAB_HOST_ACTION_V0_DMA_OUT:
        return dma_submit_action(lane, action, result);
    case FWLAB_HOST_ACTION_V0_BLOCK_READ:
    case FWLAB_HOST_ACTION_V0_BLOCK_WRITE:
    case FWLAB_HOST_ACTION_V0_BLOCK_FLUSH:
        return block_submit_action(lane, action, result);
    case FWLAB_HOST_ACTION_V0_QUEUE_EFFECT:
    case FWLAB_HOST_ACTION_V0_TARGET_RESOLVE:
    case FWLAB_HOST_ACTION_V0_BLOCK_TRIM:
        action->state = J0_DRIVER_REJECTED_CLEAN;
        submit_result_make(result, token, FWLAB_HOST_ACTION_V0_REJECTED,
                           1, 1);
        return FWLAB_SPINE_V0_OK;
    default:
        return FWLAB_SPINE_V0_INVALID;
    }
}

static enum fwlab_spine_result_v0 translate_dma_status(
    struct j0_runtime *runtime, struct j0_action_record *action,
    const struct fwlab_dma_status_v0 *lower)
{
    uint32_t state;
    uint32_t terminal;

    if (!fwlab_dma_status_v0_matches_request(lower, &action->dma_request)) {
        runtime->poisoned = 1;
        return FWLAB_SPINE_V0_POISONED;
    }
    if (lower->state == FWLAB_DMA_V0_STATE_ACCEPTED) {
        terminal_make(action, FWLAB_HOST_ACTION_V0_STATE_ACCEPTED, 0, 0, 0,
                      0, 0);
        return FWLAB_SPINE_V0_OK;
    }
    state = lower->state == FWLAB_DMA_V0_STATE_TERMINAL
                ? FWLAB_HOST_ACTION_V0_STATE_TERMINAL
                : lower->state == FWLAB_DMA_V0_STATE_DRAINING
                      ? FWLAB_HOST_ACTION_V0_STATE_DRAINING
                      : lower->state == FWLAB_DMA_V0_STATE_QUARANTINED
                            ? FWLAB_HOST_ACTION_V0_STATE_QUARANTINED
                            : FWLAB_HOST_ACTION_V0_STATE_DRAINED;
    terminal = lower->terminal_kind == FWLAB_DMA_V0_SUCCEEDED
                   ? FWLAB_HOST_ACTION_V0_SUCCEEDED
                   : lower->terminal_kind == FWLAB_DMA_V0_CANCELLED
                         ? FWLAB_HOST_ACTION_V0_CANCELLED
                         : lower->terminal_kind ==
                                   FWLAB_DMA_V0_TERMINAL_QUARANTINED
                               ? FWLAB_HOST_ACTION_V0_TERMINAL_QUARANTINED
                               : FWLAB_HOST_ACTION_V0_FAILED;
    terminal_make(action, state, terminal, lower->effect,
                  lower->bytes_completed, lower->fault_domain,
                  lower->fault_code);
    return latch_terminal(runtime, action);
}

static enum fwlab_spine_result_v0 translate_block_status(
    struct j0_runtime *runtime, struct j0_action_record *action,
    const struct fwlab_block_status_v0 *lower)
{
    uint32_t state;
    uint32_t terminal;
    uint32_t units;

    if (!fwlab_block_status_v0_matches_request(
            lower, &action->block_request)) {
        runtime->poisoned = 1;
        return FWLAB_SPINE_V0_POISONED;
    }
    if (lower->state == FWLAB_BLOCK_V0_STATE_ACCEPTED) {
        terminal_make(action, FWLAB_HOST_ACTION_V0_STATE_ACCEPTED, 0, 0, 0,
                      0, 0);
        return FWLAB_SPINE_V0_OK;
    }
    state = lower->state == FWLAB_BLOCK_V0_STATE_TERMINAL
                ? FWLAB_HOST_ACTION_V0_STATE_TERMINAL
                : lower->state == FWLAB_BLOCK_V0_STATE_DRAINING
                      ? FWLAB_HOST_ACTION_V0_STATE_DRAINING
                      : lower->state == FWLAB_BLOCK_V0_STATE_QUARANTINED
                            ? FWLAB_HOST_ACTION_V0_STATE_QUARANTINED
                            : FWLAB_HOST_ACTION_V0_STATE_DRAINED;
    terminal = lower->outcome == FWLAB_BLOCK_V0_SUCCEEDED
                   ? FWLAB_HOST_ACTION_V0_SUCCEEDED
                   : lower->outcome == FWLAB_BLOCK_V0_CANCELLED
                         ? FWLAB_HOST_ACTION_V0_CANCELLED
                         : lower->outcome ==
                                   FWLAB_BLOCK_V0_OUTCOME_QUARANTINED
                               ? FWLAB_HOST_ACTION_V0_TERMINAL_QUARANTINED
                               : FWLAB_HOST_ACTION_V0_FAILED;
    units = action->token.kind == FWLAB_HOST_ACTION_V0_BLOCK_FLUSH
                ? (uint32_t)(lower->effect != FWLAB_BLOCK_V0_EFFECT_NONE)
                : lower->completed_lbas;
    terminal_make(action, state, terminal, lower->effect, units,
                  lower->fault_domain, lower->fault_code);
    action->block_status = *lower;
    return latch_terminal(runtime, action);
}

static enum fwlab_spine_result_v0 driver_query(
    void *context, const struct fwlab_host_action_token_v0 *token,
    struct fwlab_host_action_status_v0 *status)
{
    struct j0_driver_lane *lane = context;
    struct j0_action_record *action;
    enum fwlab_spine_result_v0 result;

    if (lane == NULL || lane->runtime == NULL || status == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    action = action_for(lane, token, NULL, 0);
    if (action == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (action->state == J0_DRIVER_TERMINAL_LATCHED ||
        action->state == J0_DRIVER_DRAINING ||
        action->state == J0_DRIVER_DRAINED) {
        *status = action->terminal;
        return FWLAB_SPINE_V0_OK;
    }
    if (lane->kind == FWLAB_HOST_ACTION_V0_DMA_IN ||
        lane->kind == FWLAB_HOST_ACTION_V0_DMA_OUT) {
        memset(&action->dma_status, 0, sizeof(action->dma_status));
        result = lane->runtime->host_binding.data.ops->query(
            lane->runtime->host_binding.data.context, &action->dma_token,
            &action->dma_status);
        if (result != FWLAB_SPINE_V0_OK) {
            return result;
        }
        result = translate_dma_status(lane->runtime, action,
                                      &action->dma_status);
    } else if (lane->kind >= FWLAB_HOST_ACTION_V0_BLOCK_READ &&
               lane->kind <= FWLAB_HOST_ACTION_V0_BLOCK_FLUSH) {
        memset(&action->block_status, 0, sizeof(action->block_status));
        result = lane->runtime->block.ops->query(
            lane->runtime->block.context,
            &action->block_request.operation_token,
            &action->block_status);
        if (result != FWLAB_SPINE_V0_OK) {
            return result;
        }
        result = translate_block_status(lane->runtime, action,
                                        &action->block_status);
    } else {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    if (result != FWLAB_SPINE_V0_OK) {
        return result;
    }
    if (action->terminal.state == FWLAB_HOST_ACTION_V0_STATE_ACCEPTED) {
        action->state = J0_DRIVER_ACCEPTED;
    } else {
        action->state = J0_DRIVER_TERMINAL_LATCHED;
    }
    *status = action->terminal;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 driver_cancel(
    void *context, const struct fwlab_host_action_token_v0 *token)
{
    struct j0_driver_lane *lane = context;
    struct j0_action_record *action;

    if (lane == NULL || lane->runtime == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    action = action_for(lane, token, NULL, 0);
    if (action == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (action->state == J0_DRIVER_TERMINAL_LATCHED ||
        action->state == J0_DRIVER_DRAINING ||
        action->state == J0_DRIVER_DRAINED ||
        lane->kind == FWLAB_HOST_ACTION_V0_PAYLOAD_FILL) {
        return FWLAB_SPINE_V0_OK;
    }
    if (lane->kind == FWLAB_HOST_ACTION_V0_DMA_IN ||
        lane->kind == FWLAB_HOST_ACTION_V0_DMA_OUT) {
        struct fwlab_dma_status_v0 lower;

        if (lane->runtime->host_binding.data.ops->query(
                lane->runtime->host_binding.data.context, &action->dma_token,
                &lower) == FWLAB_SPINE_V0_OK &&
            lower.state != FWLAB_DMA_V0_STATE_ACCEPTED) {
            return FWLAB_SPINE_V0_OK;
        }
        return lane->runtime->host_binding.data.ops->cancel(
            lane->runtime->host_binding.data.context, &action->dma_token);
    }
    if (lane->kind >= FWLAB_HOST_ACTION_V0_BLOCK_READ &&
        lane->kind <= FWLAB_HOST_ACTION_V0_BLOCK_FLUSH) {
        struct fwlab_block_status_v0 lower;

        if (lane->runtime->block.ops->query(
                lane->runtime->block.context,
                &action->block_request.operation_token, &lower) ==
                FWLAB_SPINE_V0_OK &&
            lower.state != FWLAB_BLOCK_V0_STATE_ACCEPTED) {
            return FWLAB_SPINE_V0_OK;
        }
        return lane->runtime->block.ops->cancel(
            lane->runtime->block.context,
            &action->block_request.operation_token);
    }
    return FWLAB_SPINE_V0_WRONG_STATE;
}

static enum fwlab_spine_result_v0 driver_retire_start(
    void *context, const struct fwlab_host_action_token_v0 *token)
{
    struct j0_driver_lane *lane = context;
    struct j0_action_record *action;
    enum fwlab_spine_result_v0 result;

    if (lane == NULL || lane->runtime == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    action = action_for(lane, token, NULL, 0);
    if (action == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (action->state == J0_DRIVER_DRAINING ||
        action->state == J0_DRIVER_DRAINED) {
        return FWLAB_SPINE_V0_OK;
    }
    if (action->state != J0_DRIVER_TERMINAL_LATCHED) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    if (lane->kind == FWLAB_HOST_ACTION_V0_PAYLOAD_FILL) {
        action->state = J0_DRIVER_DRAINING;
        action->terminal.state = FWLAB_HOST_ACTION_V0_STATE_DRAINING;
        return FWLAB_SPINE_V0_OK;
    }
    if (lane->kind == FWLAB_HOST_ACTION_V0_DMA_IN ||
        lane->kind == FWLAB_HOST_ACTION_V0_DMA_OUT) {
        result = lane->runtime->host_binding.data.ops->retire_start(
            lane->runtime->host_binding.data.context, &action->dma_token);
    } else if (lane->kind >= FWLAB_HOST_ACTION_V0_BLOCK_READ &&
               lane->kind <= FWLAB_HOST_ACTION_V0_BLOCK_FLUSH) {
        result = lane->runtime->block.ops->retire_start(
            lane->runtime->block.context,
            &action->block_request.operation_token);
    } else {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    if (result == FWLAB_SPINE_V0_OK) {
        action->state = J0_DRIVER_DRAINING;
        action->terminal.state = FWLAB_HOST_ACTION_V0_STATE_DRAINING;
    }
    return result;
}

static enum fwlab_spine_result_v0 driver_retire_query(
    void *context, const struct fwlab_host_action_token_v0 *token,
    struct fwlab_host_action_status_v0 *status)
{
    struct j0_driver_lane *lane = context;
    struct j0_action_record *action;
    struct j0_admission_record *admission;
    enum fwlab_spine_result_v0 result;

    if (lane == NULL || lane->runtime == NULL || status == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    action = action_for(lane, token, NULL, 0);
    if (action == NULL || action->state != J0_DRIVER_DRAINING) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    if (lane->kind == FWLAB_HOST_ACTION_V0_PAYLOAD_FILL) {
        action->terminal.state = FWLAB_HOST_ACTION_V0_STATE_DRAINED;
        result = latch_terminal(lane->runtime, action);
    } else if (lane->kind == FWLAB_HOST_ACTION_V0_DMA_IN ||
               lane->kind == FWLAB_HOST_ACTION_V0_DMA_OUT) {
        memset(&action->dma_status, 0, sizeof(action->dma_status));
        result = lane->runtime->host_binding.data.ops->retire_query(
            lane->runtime->host_binding.data.context, &action->dma_token,
            &action->dma_status);
        if (result == FWLAB_SPINE_V0_IN_PROGRESS) {
            return result;
        }
        if (result == FWLAB_SPINE_V0_OK) {
            result = translate_dma_status(lane->runtime, action,
                                          &action->dma_status);
        }
        admission = admission_for_action(lane->runtime, action);
        if (result == FWLAB_SPINE_V0_OK && admission != NULL &&
            admission->authority_held && !action->authority_released) {
            result = lane->runtime->host_binding.data.ops->authority_release(
                lane->runtime->host_binding.data.context, &admission->authority);
            if (result == FWLAB_SPINE_V0_OK) {
                action->authority_released = 1;
                admission->authority_held = 0;
            }
        }
    } else if (lane->kind >= FWLAB_HOST_ACTION_V0_BLOCK_READ &&
               lane->kind <= FWLAB_HOST_ACTION_V0_BLOCK_FLUSH) {
        memset(&action->block_status, 0, sizeof(action->block_status));
        result = lane->runtime->block.ops->retire_query(
            lane->runtime->block.context,
            &action->block_request.operation_token,
            &action->block_status);
        if (result == FWLAB_SPINE_V0_IN_PROGRESS) {
            return result;
        }
        if (result == FWLAB_SPINE_V0_OK) {
            result = translate_block_status(lane->runtime, action,
                                            &action->block_status);
        }
    } else {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    if (result != FWLAB_SPINE_V0_OK) {
        return result;
    }
    action->state = J0_DRIVER_DRAINED;
    action->terminal.state = FWLAB_HOST_ACTION_V0_STATE_DRAINED;
    *status = action->terminal;
    return FWLAB_SPINE_V0_OK;
}

static int lane_actions_quiescent(
    const struct j0_runtime *runtime, uint16_t kind)
{
    uint32_t record_index;

    for (record_index = 0; record_index < J0_MAX_COMMANDS; ++record_index) {
        const struct j0_admission_record *record =
            &runtime->admission[record_index];
        uint32_t action_index;

        if (!record->occupied) {
            continue;
        }
        for (action_index = 0; action_index < record->program.action_count;
             ++action_index) {
            const struct j0_action_record *action =
                &record->action[action_index];

            if (action->token_valid && action->token.kind == kind &&
                action->state != J0_DRIVER_DRAINED &&
                action->state != J0_DRIVER_REJECTED_CLEAN) {
                return 0;
            }
        }
    }
    return 1;
}

static int local_close_lane(uint16_t kind)
{
    return kind == FWLAB_HOST_ACTION_V0_PAYLOAD_FILL ||
           kind == FWLAB_HOST_ACTION_V0_QUEUE_EFFECT ||
           kind == FWLAB_HOST_ACTION_V0_TARGET_RESOLVE ||
           kind == FWLAB_HOST_ACTION_V0_BLOCK_TRIM;
}

static enum fwlab_spine_result_v0 driver_epoch_close(
    void *context, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch)
{
    struct j0_driver_lane *lane = context;
    struct j0_lower_close_record *shared;
    enum fwlab_spine_result_v0 result;

    if (lane == NULL || lane->runtime == NULL ||
        lifecycle_instance_nonce == 0 || old_execution_epoch == 0) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (lifecycle_instance_nonce != lane->runtime->lifecycle_instance_nonce ||
        old_execution_epoch != lane->runtime->config.execution_epoch) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (local_close_lane(lane->kind)) {
        shared = &lane->local_close;
        if (!shared->started) {
            shared->started = 1;
            shared->close_acked = 1;
            shared->lifecycle_instance_nonce = lifecycle_instance_nonce;
            shared->execution_epoch = old_execution_epoch;
        }
        return shared->lifecycle_instance_nonce == lifecycle_instance_nonce &&
                       shared->execution_epoch == old_execution_epoch
                   ? FWLAB_SPINE_V0_OK
                   : FWLAB_SPINE_V0_STALE;
    }
    if (lane->kind == FWLAB_HOST_ACTION_V0_DMA_IN ||
        lane->kind == FWLAB_HOST_ACTION_V0_DMA_OUT) {
        shared = &lane->runtime->host_close;
        if (!shared->started) {
            result = lane->runtime->host_binding.data.ops->epoch_close(
                lane->runtime->host_binding.data.context, lifecycle_instance_nonce,
                old_execution_epoch);
            if (result != FWLAB_SPINE_V0_OK) {
                return result;
            }
            shared->started = 1;
            shared->close_acked = 1;
            shared->lifecycle_instance_nonce = lifecycle_instance_nonce;
            shared->execution_epoch = old_execution_epoch;
        }
        return shared->lifecycle_instance_nonce == lifecycle_instance_nonce &&
                       shared->execution_epoch == old_execution_epoch
                   ? FWLAB_SPINE_V0_OK
                   : FWLAB_SPINE_V0_STALE;
    }
    if (lane->kind >= FWLAB_HOST_ACTION_V0_BLOCK_READ &&
        lane->kind <= FWLAB_HOST_ACTION_V0_BLOCK_FLUSH) {
        shared = &lane->runtime->block_close;
        if (!shared->started) {
            result = lane->runtime->block.ops->epoch_close(
                lane->runtime->block.context, lifecycle_instance_nonce,
                old_execution_epoch);
            if (result != FWLAB_SPINE_V0_OK) {
                return result;
            }
            shared->started = 1;
            shared->close_acked = 1;
            shared->lifecycle_instance_nonce = lifecycle_instance_nonce;
            shared->execution_epoch = old_execution_epoch;
        }
        return shared->lifecycle_instance_nonce == lifecycle_instance_nonce &&
                       shared->execution_epoch == old_execution_epoch
                   ? FWLAB_SPINE_V0_OK
                   : FWLAB_SPINE_V0_STALE;
    }
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 driver_epoch_quiescent(
    void *context, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch, uint8_t *quiescent)
{
    struct j0_driver_lane *lane = context;

    if (lane == NULL || lane->runtime == NULL || quiescent == NULL ||
        lifecycle_instance_nonce == 0 || old_execution_epoch == 0) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (lifecycle_instance_nonce != lane->runtime->lifecycle_instance_nonce ||
        old_execution_epoch != lane->runtime->config.execution_epoch) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (local_close_lane(lane->kind)) {
        const struct j0_lower_close_record *closed = &lane->local_close;

        if (!closed->started) {
            return FWLAB_SPINE_V0_WRONG_STATE;
        }
        if (closed->lifecycle_instance_nonce != lifecycle_instance_nonce ||
            closed->execution_epoch != old_execution_epoch) {
            return FWLAB_SPINE_V0_STALE;
        }
        *quiescent = (uint8_t)lane_actions_quiescent(lane->runtime, lane->kind);
        return FWLAB_SPINE_V0_OK;
    }
    if (!lane->runtime->close_started) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    *quiescent = (uint8_t)lane_actions_quiescent(lane->runtime, lane->kind);
    if (!*quiescent) {
        return FWLAB_SPINE_V0_OK;
    }
    if (lane->kind == FWLAB_HOST_ACTION_V0_DMA_IN ||
        lane->kind == FWLAB_HOST_ACTION_V0_DMA_OUT) {
        struct fwlab_host_data_epoch_status_v0 lower;
        enum fwlab_spine_result_v0 result =
            lane->runtime->host_binding.data.ops->epoch_quiescent(
                lane->runtime->host_binding.data.context, lifecycle_instance_nonce,
                old_execution_epoch, &lower);

        if (result != FWLAB_SPINE_V0_OK) {
            return result;
        }
        lane->runtime->host_close.quiescent = lower.quiescent;
        *quiescent = lower.quiescent;
    } else if (lane->kind >= FWLAB_HOST_ACTION_V0_BLOCK_READ &&
               lane->kind <= FWLAB_HOST_ACTION_V0_BLOCK_FLUSH) {
        struct fwlab_block_epoch_status_v0 lower;
        enum fwlab_spine_result_v0 result =
            lane->runtime->block.ops->epoch_quiescent(
                lane->runtime->block.context, lifecycle_instance_nonce,
                old_execution_epoch, &lower);

        if (result != FWLAB_SPINE_V0_OK) {
            return result;
        }
        lane->runtime->block_close.quiescent = lower.quiescent;
        *quiescent = lower.quiescent;
    }
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_host_action_driver_ops_v0 driver_ops = {
    .version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION,
    .size = sizeof(struct fwlab_host_action_driver_ops_v0),
    .submit = driver_submit,
    .query = driver_query,
    .cancel = driver_cancel,
    .retire_start = driver_retire_start,
    .retire_query = driver_retire_query,
    .epoch_close = driver_epoch_close,
    .epoch_quiescent = driver_epoch_quiescent,
};

void j0_action_drivers_init(
    struct j0_runtime *runtime,
    struct fwlab_host_action_driver_table_v0 *drivers)
{
    uint32_t index;

    if (runtime == NULL || drivers == NULL) {
        return;
    }
    memset(drivers, 0, sizeof(*drivers));
    drivers->version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    drivers->size = (uint16_t)sizeof(*drivers);
    drivers->entry_count = FWLAB_HOST_ACTION_V0_KIND_COUNT;
    for (index = 0; index < FWLAB_HOST_ACTION_V0_KIND_COUNT; ++index) {
        runtime->lane[index].runtime = runtime;
        runtime->lane[index].kind = (uint16_t)(index + 1u);
        drivers->entry[index].version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
        drivers->entry[index].size = (uint16_t)sizeof(drivers->entry[index]);
        drivers->entry[index].kind = (uint16_t)(index + 1u);
        drivers->entry[index].generation = runtime->config.generation;
        drivers->entry[index].ops = &driver_ops;
        drivers->entry[index].context = &runtime->lane[index];
    }
}

void j0_action_close_unaccepted(struct j0_admission_record *record)
{
    uint32_t index;

    /* Called only after close and a lifecycle intent. PREPARED then denotes
     * a known no-effect submit; lifecycle cancelled it without lower ownership.
     * An ambiguous or accepted submit must still take its actual drain path. */
    for (index = 0; index < record->program.action_count; ++index) {
        struct j0_action_record *action = &record->action[index];

        if (action->token_valid && action->state == J0_DRIVER_PREPARED) {
            action->state = J0_DRIVER_REJECTED_CLEAN;
        }
    }
}

int j0_action_all_drained(const struct j0_admission_record *record)
{
    uint32_t index;

    if (record == NULL) {
        return 0;
    }
    for (index = 0; index < record->program.action_count; ++index) {
        if (!record->action[index].token_valid ||
            record->action[index].state != J0_DRIVER_DRAINED) {
            return 0;
        }
    }
    return 1;
}
