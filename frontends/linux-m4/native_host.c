/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "native_internal.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/ioctl.h>

void native_message_init(struct native_context *context,
                        const struct native_slot *slot, uint32_t operation,
                        struct fwlab_m4_native_message *message)
{
    memset(message, 0, sizeof(*message));
    message->version = FWLAB_M4_NATIVE_VERSION;
    message->size = (uint32_t)sizeof(*message);
    message->operation = operation;
    message->result = INT32_MIN;
    message->function_nonce = context->function_nonce;
    message->controller_epoch = slot ? slot->capture.controller_epoch
                                      : context->epoch;
    if (slot) {
        message->origin_uid = slot->capture.origin_uid;
        message->authority_uid = slot->authority_uid;
        message->dma_uid = slot->dma_uid;
        message->direction = slot->direction;
        message->bytes = slot->bytes;
        message->data_pointer = (uintptr_t)slot->bounce;
    }
}

int native_exchange(struct native_context *context,
                    struct fwlab_m4_native_message *message)
{
    if (ioctl(context->descriptor, FWLAB_M4_NATIVE_EXCHANGE, message) < 0) {
        message->result = INT32_MIN;
        return -errno;
    }
    return message->result;
}

static struct native_slot *find_command(
    struct native_context *context, const struct fwlab_nvme_command_handle *command,
    const struct fwlab_nvme_origin_token *origin)
{
    uint32_t index;

    for (index = 0; index < NATIVE_COMMANDS; ++index) {
        struct native_slot *slot = &context->slot[index];
        if (slot->occupied && j0_handle_equal(&slot->command.handle, command) &&
            j0_origin_equal(&slot->command.origin, origin))
            return slot;
    }
    return NULL;
}

static struct native_slot *find_authority(
    struct native_context *context, const struct fwlab_host_dma_authority_ref_v0 *authority)
{
    uint32_t index;

    if (!authority)
        return NULL;
    for (index = 0; index < NATIVE_COMMANDS; ++index) {
        struct native_slot *slot = &context->slot[index];
        if (slot->occupied && slot->authority_uid &&
            memcmp(&slot->authority, authority, sizeof(*authority)) == 0)
            return slot;
    }
    return NULL;
}

static struct native_slot *find_dma(
    struct native_context *context, const struct fwlab_dma_op_token_v0 *token)
{
    struct native_slot *slot = find_command(context, &token->action.command,
                                           &token->action.origin);

    return slot && slot->token_reserved &&
                   memcmp(&slot->dma_token, token, sizeof(*token)) == 0
               ? slot : NULL;
}

static enum fwlab_spine_result_v0 endpoint_prepare(
    void *opaque, const struct fwlab_nvme_command_handle *command,
    const struct fwlab_nvme_origin_token *origin, uint32_t direction,
    uint32_t bytes, const uint8_t *input)
{
    struct native_context *context = opaque;
    struct native_slot *slot = find_command(context, command, origin);

    if (!slot || input || context->closing || !bytes ||
        bytes > FWLAB_M4_NATIVE_MAX_BYTES || (direction != 1 && direction != 2))
        return FWLAB_SPINE_V0_INVALID;
    if (slot->bytes && (slot->bytes != bytes || slot->direction != direction))
        return FWLAB_SPINE_V0_POISONED;
    slot->bytes = bytes;
    slot->direction = direction;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 endpoint_release(
    void *opaque, const struct fwlab_nvme_command_handle *command,
    const struct fwlab_nvme_origin_token *origin)
{
    return find_command(opaque, command, origin) ? FWLAB_SPINE_V0_OK
                                               : FWLAB_SPINE_V0_STALE;
}

static enum fwlab_spine_result_v0 authority_mint(
    void *opaque, const struct fwlab_host_dma_mint_request_v0 *request,
    struct fwlab_host_dma_authority_ref_v0 *authority)
{
    struct native_context *context = opaque;
    struct native_slot *slot;
    struct fwlab_m4_native_message message;

    if (!fwlab_host_dma_mint_request_v0_valid(request) || !authority ||
        context->closing || request->execution_epoch != context->epoch)
        return FWLAB_SPINE_V0_INVALID;
    slot = find_command(context, &request->command, &request->origin);
    if (!slot || slot->bytes != request->exact_bytes ||
        slot->direction != request->direction)
        return FWLAB_SPINE_V0_STALE;
    if (slot->authority_uid) {
        if (!slot->authority_live)
            return FWLAB_SPINE_V0_STALE;
        *authority = slot->authority;
        return FWLAB_SPINE_V0_OK;
    }
    native_message_init(context, slot, FWLAB_M4_NATIVE_SHAPE, &message);
    if (native_exchange(context, &message) || !message.authority_uid || !message.dma_uid)
        return FWLAB_SPINE_V0_INVALID;
    slot->authority_uid = message.authority_uid;
    slot->dma_uid = message.dma_uid;
    memset(&slot->authority, 0, sizeof(slot->authority));
    slot->authority.version = FWLAB_HOST_DATA_V0_VERSION;
    slot->authority.size = (uint16_t)sizeof(slot->authority);
    slot->authority.type_tag = FWLAB_HOST_DMA_AUTHORITY_V0_TAG;
    slot->authority.issuer_nonce = context->authority_issuer;
    slot->authority.authority_uid = message.authority_uid;
    slot->authority.generation = context->generation;
    slot->authority.exact_bytes = slot->bytes;
    slot->authority.direction = (uint8_t)slot->direction;
    slot->authority_live = 1;
    *authority = slot->authority;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 authority_release(
    void *opaque, const struct fwlab_host_dma_authority_ref_v0 *authority)
{
    struct native_context *context = opaque;
    struct native_slot *slot = find_authority(context, authority);
    struct fwlab_m4_native_message message;

    if (!slot)
        return FWLAB_SPINE_V0_STALE;
    if (!slot->authority_live)
        return FWLAB_SPINE_V0_OK;
    native_message_init(context, slot, FWLAB_M4_NATIVE_AUTHORITY_RELEASE, &message);
    if (native_exchange(context, &message))
        return FWLAB_SPINE_V0_IN_PROGRESS;
    slot->authority_live = 0;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 token_reserve(
    void *opaque, const struct fwlab_host_action_token_v0 *action,
    struct fwlab_dma_op_token_v0 *token)
{
    struct native_context *context = opaque;
    struct native_slot *slot;

    if (!fwlab_host_action_token_v0_valid(action) || !token || context->closing ||
        (action->kind != FWLAB_HOST_ACTION_V0_DMA_IN &&
         action->kind != FWLAB_HOST_ACTION_V0_DMA_OUT))
        return FWLAB_SPINE_V0_INVALID;
    slot = find_command(context, &action->command, &action->origin);
    if (!slot || !slot->authority_live || !slot->dma_uid)
        return FWLAB_SPINE_V0_STALE;
    if (slot->token_reserved) {
        if (!j0_action_token_equal(action, &slot->dma_token.action))
            return FWLAB_SPINE_V0_POISONED;
    } else {
        memset(&slot->dma_token, 0, sizeof(slot->dma_token));
        slot->dma_token.version = FWLAB_HOST_DATA_V0_VERSION;
        slot->dma_token.size = (uint16_t)sizeof(slot->dma_token);
        slot->dma_token.type_tag = FWLAB_DMA_OP_TOKEN_V0_TAG;
        slot->dma_token.action = *action;
        slot->dma_token.issuer_nonce = context->dma_issuer;
        slot->dma_token.operation_uid = slot->dma_uid;
        slot->dma_token.generation = context->generation;
        slot->token_reserved = 1;
    }
    *token = slot->dma_token;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 dma_observe(
    struct native_context *context, struct native_slot *slot, uint32_t operation)
{
    struct fwlab_m4_native_message message;
    int result;

    if (slot->dma_terminal)
        return FWLAB_SPINE_V0_OK;
    native_message_init(context, slot, operation, &message);
    result = native_exchange(context, &message);
    if (result && message.result == INT32_MIN)
        return FWLAB_SPINE_V0_IN_PROGRESS;
    if (result && (!message.dma_state ||
                   message.dma_state == FWLAB_M4_NATIVE_DMA_RESERVED)) {
        int original = result;
        native_message_init(context, slot, FWLAB_M4_NATIVE_DMA_CANCEL, &message);
        if (native_exchange(context, &message))
            return FWLAB_SPINE_V0_IN_PROGRESS;
        result = original;
    }
    if (!result && message.dma_state == FWLAB_M4_NATIVE_DMA_RESERVED) {
        native_message_init(context, slot, FWLAB_M4_NATIVE_DMA_CANCEL, &message);
        result = native_exchange(context, &message);
        if (result)
            return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    memset(&slot->dma_status, 0, sizeof(slot->dma_status));
    slot->dma_status.version = FWLAB_HOST_DATA_V0_VERSION;
    slot->dma_status.size = (uint16_t)sizeof(slot->dma_status);
    slot->dma_status.operation = slot->dma_token;
    slot->dma_status.state = FWLAB_DMA_V0_STATE_TERMINAL;
    slot->dma_status.bytes_completed = result ? 0 : message.bytes_done;
    if (slot->dma_status.bytes_completed > slot->bytes)
        return FWLAB_SPINE_V0_POISONED;
    slot->dma_status.effect = slot->dma_status.bytes_completed == 0
        ? FWLAB_HOST_ACTION_V0_EFFECT_NONE
        : slot->dma_status.bytes_completed == slot->bytes
            ? FWLAB_HOST_ACTION_V0_EFFECT_FULL : FWLAB_HOST_ACTION_V0_EFFECT_EXACT_PREFIX;
    slot->dma_status.terminal_kind =
        !result && message.dma_state == FWLAB_M4_NATIVE_DMA_DONE
            ? FWLAB_DMA_V0_SUCCEEDED
            : !result && message.dma_state == FWLAB_M4_NATIVE_DMA_CANCELLED
                ? FWLAB_DMA_V0_CANCELLED : FWLAB_DMA_V0_FAILED;
    if (slot->direction == FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER &&
        slot->dma_status.terminal_kind == FWLAB_DMA_V0_SUCCEEDED &&
        context->buffer.ops->write(context->buffer.context,
            &slot->dma_request.buffer, &slot->dma_request.span,
            slot->bounce, slot->bytes) != FWLAB_CONTROLLER_BUFFER_V0_OK)
        slot->dma_status.terminal_kind = FWLAB_DMA_V0_FAILED;
    if (slot->dma_status.terminal_kind == FWLAB_DMA_V0_FAILED) {
        slot->dma_status.fault_domain = 1;
        slot->dma_status.fault_code = (uint32_t)(result < 0 ? -result : EIO);
    }
    slot->dma_terminal = 1;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 dma_submit(
    void *opaque, const struct fwlab_dma_request_v0 *request,
    struct fwlab_dma_submit_result_v0 *result)
{
    struct native_context *context = opaque;
    struct native_slot *slot;
    enum fwlab_spine_result_v0 observed;

    if (!fwlab_dma_request_v0_valid(request) || !result || context->closing)
        return FWLAB_SPINE_V0_INVALID;
    slot = find_dma(context, &request->operation);
    if (!slot || !slot->authority_live ||
        memcmp(&slot->authority, &request->authority, sizeof(slot->authority)) ||
        request->direction != slot->direction || request->exact_bytes != slot->bytes ||
        request->execution_epoch != slot->capture.controller_epoch)
        return FWLAB_SPINE_V0_STALE;
    if (slot->dma_submitted) {
        if (memcmp(&slot->dma_request, request, sizeof(*request)))
            return FWLAB_SPINE_V0_POISONED;
    } else {
        slot->dma_request = *request;
        slot->dma_submitted = 1;
        if (slot->direction == FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST &&
            context->buffer.ops->read(context->buffer.context, &request->buffer,
                &request->span, slot->bounce, slot->bytes) != FWLAB_CONTROLLER_BUFFER_V0_OK)
            return FWLAB_SPINE_V0_POISONED;
    }
    observed = dma_observe(context, slot, FWLAB_M4_NATIVE_DMA);
    if (observed != FWLAB_SPINE_V0_OK)
        return observed;
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_HOST_DATA_V0_VERSION;
    result->size = (uint16_t)sizeof(*result);
    result->operation = slot->dma_token;
    result->disposition = FWLAB_HOST_ACTION_V0_ACCEPTED;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 dma_query(
    void *opaque, const struct fwlab_dma_op_token_v0 *token,
    struct fwlab_dma_status_v0 *status)
{
    struct native_context *context = opaque;
    struct native_slot *slot;
    enum fwlab_spine_result_v0 result;

    if (!token || !status || !(slot = find_dma(context, token)) || !slot->dma_submitted)
        return FWLAB_SPINE_V0_STALE;
    result = dma_observe(context, slot, FWLAB_M4_NATIVE_DMA_QUERY);
    if (result == FWLAB_SPINE_V0_OK)
        *status = slot->dma_status;
    return result;
}

static enum fwlab_spine_result_v0 dma_cancel(
    void *opaque, const struct fwlab_dma_op_token_v0 *token)
{
    struct native_context *context = opaque;
    struct native_slot *slot;
    struct fwlab_m4_native_message message;

    if (!token || !(slot = find_dma(context, token)))
        return FWLAB_SPINE_V0_STALE;
    if (slot->dma_terminal)
        return FWLAB_SPINE_V0_OK;
    native_message_init(context, slot, FWLAB_M4_NATIVE_DMA_CANCEL, &message);
    return native_exchange(context, &message) ? FWLAB_SPINE_V0_IN_PROGRESS
                                             : FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 dma_retire_start(
    void *opaque, const struct fwlab_dma_op_token_v0 *token)
{
    struct native_context *context = opaque;
    struct native_slot *slot;
    struct fwlab_m4_native_message message;

    if (!token || !(slot = find_dma(context, token)) || !slot->dma_terminal)
        return FWLAB_SPINE_V0_WRONG_STATE;
    if (slot->dma_retire_started)
        return FWLAB_SPINE_V0_OK;
    native_message_init(context, slot, FWLAB_M4_NATIVE_DMA_RETIRE, &message);
    if (native_exchange(context, &message))
        return FWLAB_SPINE_V0_IN_PROGRESS;
    slot->dma_retire_started = 1;
    slot->dma_status.state = FWLAB_DMA_V0_STATE_DRAINING;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 dma_retire_query(
    void *opaque, const struct fwlab_dma_op_token_v0 *token,
    struct fwlab_dma_status_v0 *status)
{
    struct native_context *context = opaque;
    struct native_slot *slot;

    if (!token || !status || !(slot = find_dma(context, token)) ||
        !slot->dma_retire_started)
        return FWLAB_SPINE_V0_WRONG_STATE;
    slot->dma_drained = 1;
    slot->dma_status.state = FWLAB_DMA_V0_STATE_DRAINED;
    *status = slot->dma_status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 epoch_close(void *opaque, uint64_t nonce,
                                            uint32_t epoch)
{
    struct native_context *context = opaque;

    if (!nonce || epoch != context->epoch)
        return FWLAB_SPINE_V0_STALE;
    if (context->closing)
        return context->close_nonce == nonce && context->close_epoch == epoch
                   ? FWLAB_SPINE_V0_OK : FWLAB_SPINE_V0_STALE;
    context->closing = 1;
    context->close_nonce = nonce;
    context->close_epoch = epoch;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 epoch_quiescent(
    void *opaque, uint64_t nonce, uint32_t epoch,
    struct fwlab_host_data_epoch_status_v0 *status)
{
    struct native_context *context = opaque;
    uint32_t index, buffers = 0;
    uint8_t quiescent = 0;

    if (!status || !context->closing || nonce != context->close_nonce ||
        epoch != context->close_epoch)
        return FWLAB_SPINE_V0_STALE;
    if (context->buffer.ops->epoch_quiescent(context->buffer.context, nonce, epoch,
            &buffers, &quiescent) != FWLAB_CONTROLLER_BUFFER_V0_OK)
        return FWLAB_SPINE_V0_POISONED;
    memset(status, 0, sizeof(*status));
    status->version = FWLAB_HOST_DATA_V0_VERSION;
    status->size = (uint16_t)sizeof(*status);
    status->lifecycle_instance_nonce = nonce;
    status->execution_epoch = epoch;
    status->buffer_leases = buffers;
    status->admission_closed = 1;
    for (index = 0; index < NATIVE_COMMANDS; ++index) {
        const struct native_slot *slot = &context->slot[index];
        status->authority_refs += slot->occupied && slot->authority_live;
        status->dma_operations += slot->occupied && slot->dma_submitted && !slot->dma_drained;
    }
    status->quiescent = quiescent && !status->authority_refs && !status->dma_operations;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 queue_observe(
    struct native_context *context, struct native_slot *slot)
{
    struct fwlab_m4_native_message message;
    int result;

    if (slot->queue_terminal)
        return FWLAB_SPINE_V0_OK;
    native_message_init(context, slot, FWLAB_M4_NATIVE_QUEUE, &message);
    message.queue_effect = slot->queue_argument.semantic;
    message.queue_id = slot->queue_argument.queue_id;
    message.queue_entries = slot->queue_argument.queue_entries;
    message.associated_queue = slot->queue_argument.associated_queue_id;
    message.interrupt_vector = slot->queue_argument.interrupt_vector;
    result = native_exchange(context, &message);
    if (result && message.result == INT32_MIN)
        return FWLAB_SPINE_V0_IN_PROGRESS;
    memset(&slot->queue_status, 0, sizeof(slot->queue_status));
    slot->queue_status.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    slot->queue_status.size = (uint16_t)sizeof(slot->queue_status);
    slot->queue_status.token = slot->queue_token;
    slot->queue_status.state = FWLAB_HOST_ACTION_V0_STATE_TERMINAL;
    slot->queue_status.terminal_kind = !result ? FWLAB_HOST_ACTION_V0_SUCCEEDED :
        context->closing ? FWLAB_HOST_ACTION_V0_CANCELLED : FWLAB_HOST_ACTION_V0_FAILED;
    slot->queue_status.effect = result ? FWLAB_HOST_ACTION_V0_EFFECT_NONE
                                      : FWLAB_HOST_ACTION_V0_EFFECT_FULL;
    slot->queue_status.units_completed = result ? 0 : 1;
    if (!result)
        slot->queue_status.produced_witness_mask = FWLAB_HOST_WITNESS_V0_QUEUE_EFFECT;
    else if (!context->closing) {
        slot->queue_status.fault_domain = 1;
        slot->queue_status.fault_code = (uint32_t)-result;
    }
    if (j0_runtime_action_result(context->runtime, &slot->queue_token,
                                &slot->queue_status) != FWLAB_SPINE_V0_OK)
        return FWLAB_SPINE_V0_POISONED;
    slot->queue_terminal = 1;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 queue_submit(
    void *opaque, const struct fwlab_host_action_token_v0 *token,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    struct fwlab_host_action_submit_result_v0 *result)
{
    struct native_context *context = opaque;
    struct native_slot *slot;
    enum fwlab_spine_result_v0 observed;

    if (!token || !reference || !result || context->closing ||
        !(slot = find_command(context, &token->command, &token->origin)))
        return FWLAB_SPINE_V0_INVALID;
    if (slot->queue_valid && !j0_action_token_equal(&slot->queue_token, token))
        return FWLAB_SPINE_V0_POISONED;
    if (j0_runtime_action_argument(context->runtime, token, reference,
                                   &slot->queue_argument) != FWLAB_SPINE_V0_OK)
        return FWLAB_SPINE_V0_POISONED;
    slot->queue_token = *token;
    slot->queue_reference = *reference;
    slot->queue_valid = 1;
    observed = queue_observe(context, slot);
    if (observed != FWLAB_SPINE_V0_OK)
        return observed;
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    result->size = (uint16_t)sizeof(*result);
    result->token = *token;
    result->disposition = FWLAB_HOST_ACTION_V0_ACCEPTED;
    return FWLAB_SPINE_V0_OK;
}

static struct native_slot *find_queue(struct native_context *context,
                                      const struct fwlab_host_action_token_v0 *token)
{
    struct native_slot *slot = token ? find_command(context, &token->command, &token->origin) : NULL;
    return slot && slot->queue_valid && j0_action_token_equal(&slot->queue_token, token)
               ? slot : NULL;
}

static enum fwlab_spine_result_v0 queue_query(
    void *opaque, const struct fwlab_host_action_token_v0 *token,
    struct fwlab_host_action_status_v0 *status)
{
    struct native_context *context = opaque;
    struct native_slot *slot = find_queue(context, token);
    enum fwlab_spine_result_v0 result;

    if (!slot || !status)
        return FWLAB_SPINE_V0_STALE;
    result = queue_observe(context, slot);
    if (result == FWLAB_SPINE_V0_OK)
        *status = slot->queue_status;
    return result;
}

static enum fwlab_spine_result_v0 queue_cancel(
    void *opaque, const struct fwlab_host_action_token_v0 *token)
{
    return find_queue(opaque, token) ? FWLAB_SPINE_V0_OK : FWLAB_SPINE_V0_STALE;
}

static enum fwlab_spine_result_v0 queue_retire_start(
    void *opaque, const struct fwlab_host_action_token_v0 *token)
{
    struct native_slot *slot = find_queue(opaque, token);

    if (!slot || !slot->queue_terminal)
        return FWLAB_SPINE_V0_WRONG_STATE;
    slot->queue_retire_started = 1;
    slot->queue_status.state = FWLAB_HOST_ACTION_V0_STATE_DRAINING;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 queue_retire_query(
    void *opaque, const struct fwlab_host_action_token_v0 *token,
    struct fwlab_host_action_status_v0 *status)
{
    struct native_context *context = opaque;
    struct native_slot *slot = find_queue(context, token);

    if (!slot || !status || !slot->queue_retire_started)
        return FWLAB_SPINE_V0_WRONG_STATE;
    slot->queue_status.state = FWLAB_HOST_ACTION_V0_STATE_DRAINED;
    if (j0_runtime_action_result(context->runtime, token, &slot->queue_status) != FWLAB_SPINE_V0_OK)
        return FWLAB_SPINE_V0_POISONED;
    slot->queue_drained = 1;
    *status = slot->queue_status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 queue_epoch_quiescent(
    void *opaque, uint64_t nonce, uint32_t epoch, uint8_t *quiescent)
{
    struct native_context *context = opaque;
    uint32_t index;

    if (!quiescent || !context->closing || context->close_nonce != nonce || context->close_epoch != epoch)
        return FWLAB_SPINE_V0_STALE;
    *quiescent = 1;
    for (index = 0; index < NATIVE_COMMANDS; ++index)
        if (context->slot[index].occupied && context->slot[index].queue_valid &&
            !context->slot[index].queue_drained)
            *quiescent = 0;
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_host_data_ops_v0 host_ops = {
    .version = FWLAB_HOST_DATA_V0_VERSION, .size = sizeof(struct fwlab_host_data_ops_v0),
    .authority_mint = authority_mint, .authority_release = authority_release,
    .token_reserve = token_reserve, .submit = dma_submit, .query = dma_query,
    .cancel = dma_cancel, .retire_start = dma_retire_start, .retire_query = dma_retire_query,
    .epoch_close = epoch_close, .epoch_quiescent = epoch_quiescent,
};

static const struct fwlab_host_action_driver_ops_v0 queue_ops = {
    .version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION, .size = sizeof(struct fwlab_host_action_driver_ops_v0),
    .submit = queue_submit, .query = queue_query, .cancel = queue_cancel,
    .retire_start = queue_retire_start, .retire_query = queue_retire_query,
    .epoch_close = epoch_close, .epoch_quiescent = queue_epoch_quiescent,
};

enum fwlab_spine_result_v0 native_host_bind(
    void *opaque, const struct fwlab_controller_buffer_port_v0 *buffer,
    uint32_t generation, struct j0_host_binding *binding)
{
    struct native_context *context = opaque;

    context->buffer = *buffer;
    context->generation = generation;
    context->closing = 0;
    context->close_nonce = 0;
    context->close_epoch = 0;
    context->authority_issuer = context->function_nonce ^ UINT64_C(0x4844415200000000);
    context->dma_issuer = context->function_nonce ^ UINT64_C(0x444d414f00000000);
    if (!context->authority_issuer || !context->dma_issuer ||
        context->authority_issuer == buffer->issuer_nonce || context->dma_issuer == buffer->issuer_nonce)
        return FWLAB_SPINE_V0_INVALID;
    memset(binding, 0, sizeof(*binding));
    binding->context = context;
    binding->endpoint_prepare = endpoint_prepare;
    binding->endpoint_release = endpoint_release;
    binding->data.ops = &host_ops;
    binding->data.context = context;
    binding->data.buffer = *buffer;
    binding->data.generation = generation;
    binding->data.authority_issuer_nonce = context->authority_issuer;
    binding->data.dma_issuer_nonce = context->dma_issuer;
    binding->queue_driver.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    binding->queue_driver.size = (uint16_t)sizeof(binding->queue_driver);
    binding->queue_driver.kind = FWLAB_HOST_ACTION_V0_QUEUE_EFFECT;
    binding->queue_driver.generation = generation;
    binding->queue_driver.context = context;
    binding->queue_driver.ops = &queue_ops;
    return FWLAB_SPINE_V0_OK;
}
