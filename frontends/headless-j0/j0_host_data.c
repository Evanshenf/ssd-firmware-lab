/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "j0_internal.h"

#include <string.h>

static struct j0_host_endpoint *endpoint_find(
    struct j0_host_data *host,
    const struct fwlab_nvme_command_handle *command,
    const struct fwlab_nvme_origin_token *origin)
{
    uint32_t index;

    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        struct j0_host_endpoint *endpoint = &host->endpoint[index];

        if (endpoint->occupied && j0_handle_equal(&endpoint->command, command) &&
            j0_origin_equal(&endpoint->origin, origin)) {
            return endpoint;
        }
    }
    return NULL;
}

static const struct j0_host_endpoint *endpoint_find_const(
    const struct j0_host_data *host,
    const struct fwlab_nvme_command_handle *command,
    const struct fwlab_nvme_origin_token *origin)
{
    uint32_t index;

    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        const struct j0_host_endpoint *endpoint = &host->endpoint[index];

        if (endpoint->occupied && j0_handle_equal(&endpoint->command, command) &&
            j0_origin_equal(&endpoint->origin, origin)) {
            return endpoint;
        }
    }
    return NULL;
}

static struct j0_host_authority_record *authority_find(
    struct j0_host_data *host,
    const struct fwlab_host_dma_authority_ref_v0 *authority)
{
    uint32_t index;

    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        struct j0_host_authority_record *record = &host->authority[index];

        if (record->occupied &&
            memcmp(&record->authority, authority, sizeof(*authority)) == 0) {
            return record;
        }
    }
    return NULL;
}

static struct j0_host_authority_record *authority_request_find(
    struct j0_host_data *host,
    const struct fwlab_host_dma_mint_request_v0 *request)
{
    uint32_t index;

    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        struct j0_host_authority_record *record = &host->authority[index];

        if (!record->occupied) {
            continue;
        }
        if (j0_handle_equal(&record->request.command, &request->command) ||
            j0_origin_equal(&record->request.origin, &request->origin)) {
            return record;
        }
    }
    return NULL;
}

static int dma_token_equal(
    const struct fwlab_dma_op_token_v0 *left,
    const struct fwlab_dma_op_token_v0 *right)
{
    return left != NULL && right != NULL &&
           left->version == right->version && left->size == right->size &&
           left->type_tag == right->type_tag &&
           j0_action_token_equal(&left->action, &right->action) &&
           left->issuer_nonce == right->issuer_nonce &&
           left->operation_uid == right->operation_uid &&
           left->generation == right->generation &&
           j0_bytes_zero(left->reserved, sizeof(left->reserved)) &&
           j0_bytes_zero(right->reserved, sizeof(right->reserved));
}

static struct j0_dma_record *dma_find(
    struct j0_host_data *host,
    const struct fwlab_dma_op_token_v0 *operation)
{
    uint32_t index;

    for (index = 0; index < J0_MAX_DMA_OPERATIONS; ++index) {
        struct j0_dma_record *record = &host->dma[index];

        if (record->occupied && dma_token_equal(&record->operation, operation)) {
            return record;
        }
    }
    return NULL;
}

static struct j0_dma_record *dma_action_find(
    struct j0_host_data *host,
    const struct fwlab_host_action_token_v0 *action)
{
    uint32_t index;

    for (index = 0; index < J0_MAX_DMA_OPERATIONS; ++index) {
        struct j0_dma_record *record = &host->dma[index];

        if (record->occupied &&
            j0_action_token_equal(&record->operation.action, action)) {
            return record;
        }
    }
    return NULL;
}

enum fwlab_spine_result_v0 j0_host_endpoint_prepare(
    struct j0_host_data *host,
    const struct fwlab_nvme_command_handle *command,
    const struct fwlab_nvme_origin_token *origin, uint32_t direction,
    uint32_t exact_bytes, const uint8_t *input)
{
    struct j0_host_endpoint *endpoint;
    uint32_t index;

    if (host == NULL || command == NULL || origin == NULL ||
        command->instance_nonce == 0 || command->command_uid == 0 ||
        command->controller_epoch == 0 || command->generation == 0 ||
        (!origin->word[0] && !origin->word[1]) ||
        (direction != FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER &&
         direction != FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST) ||
        exact_bytes == 0 || exact_bytes > J0_MAX_TRANSFER_BYTES ||
        (direction == FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER && input == NULL) ||
        host->admission_closed) {
        return FWLAB_SPINE_V0_INVALID;
    }
    endpoint = endpoint_find(host, command, origin);
    if (endpoint != NULL) {
        if (endpoint->direction != direction ||
            endpoint->exact_bytes != exact_bytes ||
            (direction == FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER &&
             memcmp(endpoint->bytes, input, exact_bytes) != 0)) {
            host->poisoned = 1;
            return FWLAB_SPINE_V0_POISONED;
        }
        return FWLAB_SPINE_V0_OK;
    }
    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        if (!host->endpoint[index].occupied) {
            endpoint = &host->endpoint[index];
            break;
        }
    }
    if (endpoint == NULL) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->occupied = 1;
    endpoint->command = *command;
    endpoint->origin = *origin;
    endpoint->direction = (uint8_t)direction;
    endpoint->exact_bytes = exact_bytes;
    if (input != NULL) {
        memcpy(endpoint->bytes, input, exact_bytes);
    }
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 j0_host_endpoint_read(
    const struct j0_host_data *host,
    const struct fwlab_nvme_command_handle *command,
    const struct fwlab_nvme_origin_token *origin, void *output,
    size_t output_size)
{
    const struct j0_host_endpoint *endpoint;

    if (host == NULL || command == NULL || origin == NULL || output == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    endpoint = endpoint_find_const(host, command, origin);
    if (endpoint == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (endpoint->direction != FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST ||
        output_size != endpoint->exact_bytes) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    memcpy(output, endpoint->bytes, output_size);
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 j0_host_endpoint_release(
    struct j0_host_data *host,
    const struct fwlab_nvme_command_handle *command,
    const struct fwlab_nvme_origin_token *origin)
{
    struct j0_host_endpoint *endpoint;

    if (host == NULL || command == NULL || origin == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    endpoint = endpoint_find(host, command, origin);
    if (endpoint == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    memset(endpoint, 0, sizeof(*endpoint));
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 authority_mint(
    void *context, const struct fwlab_host_dma_mint_request_v0 *request,
    struct fwlab_host_dma_authority_ref_v0 *authority)
{
    struct j0_host_data *host = context;
    struct j0_host_authority_record *record;
    struct j0_host_endpoint *endpoint;
    uint32_t endpoint_index;
    uint32_t index;

    if (host == NULL || request == NULL || authority == NULL ||
        !fwlab_host_dma_mint_request_v0_valid(request) || host->poisoned) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (host->admission_closed) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    record = authority_request_find(host, request);
    if (record != NULL) {
        if (memcmp(&record->request, request, sizeof(*request)) != 0 ||
            record->released) {
            host->poisoned = 1;
            return FWLAB_SPINE_V0_POISONED;
        }
        *authority = record->authority;
        return FWLAB_SPINE_V0_OK;
    }
    endpoint = endpoint_find(host, &request->command, &request->origin);
    if (endpoint == NULL || endpoint->direction != request->direction ||
        endpoint->exact_bytes != request->exact_bytes) {
        return FWLAB_SPINE_V0_STALE;
    }
    endpoint_index = (uint32_t)(endpoint - host->endpoint);
    record = NULL;
    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        if (!host->authority[index].occupied) {
            record = &host->authority[index];
            break;
        }
    }
    if (record == NULL) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    if (host->next_authority_uid == 0 ||
        host->next_authority_uid == UINT64_MAX) {
        return FWLAB_SPINE_V0_COUNTER_EXHAUSTED;
    }
    memset(record, 0, sizeof(*record));
    record->occupied = 1;
    record->request = *request;
    record->endpoint_index = (uint16_t)endpoint_index;
    record->authority.version = FWLAB_HOST_DATA_V0_VERSION;
    record->authority.size = (uint16_t)sizeof(record->authority);
    record->authority.type_tag = FWLAB_HOST_DMA_AUTHORITY_V0_TAG;
    record->authority.issuer_nonce = host->port.authority_issuer_nonce;
    record->authority.authority_uid = host->next_authority_uid++;
    record->authority.generation = host->generation;
    record->authority.exact_bytes = request->exact_bytes;
    record->authority.direction = request->direction;
    ++host->active_authorities;
    *authority = record->authority;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 authority_release(
    void *context,
    const struct fwlab_host_dma_authority_ref_v0 *authority)
{
    struct j0_host_data *host = context;
    struct j0_host_authority_record *record;

    if (host == NULL || authority == NULL ||
        !fwlab_host_dma_authority_ref_v0_valid(authority)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = authority_find(host, authority);
    if (record == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (record->released) {
        return FWLAB_SPINE_V0_OK;
    }
    record->released = 1;
    if (host->active_authorities == 0) {
        host->poisoned = 1;
        return FWLAB_SPINE_V0_POISONED;
    }
    --host->active_authorities;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 dma_token_reserve(
    void *context, const struct fwlab_host_action_token_v0 *action,
    struct fwlab_dma_op_token_v0 *operation)
{
    struct j0_host_data *host = context;
    struct j0_dma_record *record;
    uint32_t index;

    if (host == NULL || action == NULL || operation == NULL ||
        !fwlab_host_action_token_v0_valid(action) ||
        (action->kind != FWLAB_HOST_ACTION_V0_DMA_IN &&
         action->kind != FWLAB_HOST_ACTION_V0_DMA_OUT) || host->poisoned) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (host->admission_closed) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    record = dma_action_find(host, action);
    if (record != NULL) {
        *operation = record->operation;
        return FWLAB_SPINE_V0_OK;
    }
    record = NULL;
    for (index = 0; index < J0_MAX_DMA_OPERATIONS; ++index) {
        if (!host->dma[index].occupied) {
            record = &host->dma[index];
            break;
        }
    }
    if (record == NULL) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    if (host->next_dma_uid == 0 || host->next_dma_uid == UINT64_MAX) {
        return FWLAB_SPINE_V0_COUNTER_EXHAUSTED;
    }
    memset(record, 0, sizeof(*record));
    record->occupied = 1;
    record->operation.version = FWLAB_HOST_DATA_V0_VERSION;
    record->operation.size = (uint16_t)sizeof(record->operation);
    record->operation.type_tag = FWLAB_DMA_OP_TOKEN_V0_TAG;
    record->operation.action = *action;
    record->operation.issuer_nonce = host->port.dma_issuer_nonce;
    record->operation.operation_uid = host->next_dma_uid++;
    record->operation.generation = host->generation;
    *operation = record->operation;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 dma_submit(
    void *context, const struct fwlab_dma_request_v0 *request,
    struct fwlab_dma_submit_result_v0 *result)
{
    struct j0_host_data *host = context;
    struct j0_dma_record *record;
    struct j0_host_authority_record *authority;
    struct j0_host_endpoint *endpoint;
    enum fwlab_controller_buffer_result_v0 copy_result;

    if (host == NULL || request == NULL || result == NULL ||
        !fwlab_dma_request_v0_valid(request) || host->poisoned) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = dma_find(host, &request->operation);
    authority = authority_find(host, &request->authority);
    if (record == NULL || authority == NULL || authority->released ||
        authority->endpoint_index >= J0_MAX_COMMANDS ||
        request->execution_epoch != authority->request.execution_epoch ||
        !j0_handle_equal(&request->operation.action.command,
                         &authority->request.command) ||
        !j0_origin_equal(&request->operation.action.origin,
                         &authority->request.origin)) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (record->request_valid) {
        if (memcmp(&record->request, request, sizeof(*request)) != 0) {
            host->poisoned = 1;
            return FWLAB_SPINE_V0_POISONED;
        }
        memset(result, 0, sizeof(*result));
        result->version = FWLAB_HOST_DATA_V0_VERSION;
        result->size = (uint16_t)sizeof(*result);
        result->operation = record->operation;
        result->disposition = FWLAB_HOST_ACTION_V0_ACCEPTED;
        return FWLAB_SPINE_V0_OK;
    }
    endpoint = &host->endpoint[authority->endpoint_index];
    if (!endpoint->occupied || endpoint->exact_bytes != request->exact_bytes ||
        endpoint->direction != request->direction) {
        return FWLAB_SPINE_V0_POISONED;
    }
    if (request->direction == FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER) {
        copy_result = host->port.buffer.ops->write(
            host->port.buffer.context, &request->buffer, &request->span,
            endpoint->bytes, request->exact_bytes);
    } else {
        copy_result = host->port.buffer.ops->read(
            host->port.buffer.context, &request->buffer, &request->span,
            endpoint->bytes, request->exact_bytes);
    }
    if (copy_result != FWLAB_CONTROLLER_BUFFER_V0_OK) {
        return FWLAB_SPINE_V0_POISONED;
    }
    record->request = *request;
    record->request_valid = 1;
    memset(&record->status, 0, sizeof(record->status));
    record->status.version = FWLAB_HOST_DATA_V0_VERSION;
    record->status.size = (uint16_t)sizeof(record->status);
    record->status.operation = record->operation;
    record->status.state = FWLAB_DMA_V0_STATE_TERMINAL;
    record->status.terminal_kind = FWLAB_DMA_V0_SUCCEEDED;
    record->status.effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL;
    record->status.bytes_completed = request->exact_bytes;
    ++host->active_dma;
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_HOST_DATA_V0_VERSION;
    result->size = (uint16_t)sizeof(*result);
    result->operation = record->operation;
    result->disposition = FWLAB_HOST_ACTION_V0_ACCEPTED;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 dma_query(
    void *context, const struct fwlab_dma_op_token_v0 *operation,
    struct fwlab_dma_status_v0 *status)
{
    struct j0_host_data *host = context;
    struct j0_dma_record *record;

    if (host == NULL || operation == NULL || status == NULL ||
        !fwlab_dma_op_token_v0_valid(operation)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = dma_find(host, operation);
    if (record == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (!record->request_valid) {
        return FWLAB_SPINE_V0_STALE;
    }
    *status = record->status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 dma_cancel(
    void *context, const struct fwlab_dma_op_token_v0 *operation)
{
    struct j0_host_data *host = context;
    struct j0_dma_record *record;

    if (host == NULL || operation == NULL ||
        !fwlab_dma_op_token_v0_valid(operation)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = dma_find(host, operation);
    if (record == NULL || !record->request_valid) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (record->status.state >= FWLAB_DMA_V0_STATE_TERMINAL) {
        return FWLAB_SPINE_V0_OK;
    }
    record->status.state = FWLAB_DMA_V0_STATE_TERMINAL;
    record->status.terminal_kind = FWLAB_DMA_V0_CANCELLED;
    record->status.effect = FWLAB_HOST_ACTION_V0_EFFECT_NONE;
    record->status.bytes_completed = 0;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 dma_retire_start(
    void *context, const struct fwlab_dma_op_token_v0 *operation)
{
    struct j0_host_data *host = context;
    struct j0_dma_record *record;

    if (host == NULL || operation == NULL ||
        !fwlab_dma_op_token_v0_valid(operation)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = dma_find(host, operation);
    if (record == NULL || !record->request_valid) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (record->status.state != FWLAB_DMA_V0_STATE_TERMINAL &&
        record->status.state != FWLAB_DMA_V0_STATE_DRAINING &&
        record->status.state != FWLAB_DMA_V0_STATE_DRAINED) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    record->retire_started = 1;
    if (!record->drained) {
        record->status.state = FWLAB_DMA_V0_STATE_DRAINING;
    }
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 dma_retire_query(
    void *context, const struct fwlab_dma_op_token_v0 *operation,
    struct fwlab_dma_status_v0 *status)
{
    struct j0_host_data *host = context;
    struct j0_dma_record *record;

    if (host == NULL || operation == NULL || status == NULL ||
        !fwlab_dma_op_token_v0_valid(operation)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = dma_find(host, operation);
    if (record == NULL || !record->request_valid) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (!record->retire_started) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    if (!record->drained) {
        record->drained = 1;
        record->status.state = FWLAB_DMA_V0_STATE_DRAINED;
        if (host->active_dma == 0) {
            host->poisoned = 1;
            return FWLAB_SPINE_V0_POISONED;
        }
        --host->active_dma;
    }
    *status = record->status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 host_epoch_close(
    void *context, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch)
{
    struct j0_host_data *host = context;
    enum fwlab_controller_buffer_result_v0 result;

    if (host == NULL || lifecycle_instance_nonce == 0 ||
        old_execution_epoch == 0) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (host->close_started) {
        return host->close_lifecycle_nonce == lifecycle_instance_nonce &&
                       host->close_execution_epoch == old_execution_epoch
                   ? FWLAB_SPINE_V0_OK
                   : FWLAB_SPINE_V0_STALE;
    }
    host->admission_closed = 1;
    host->close_started = 1;
    host->close_lifecycle_nonce = lifecycle_instance_nonce;
    host->close_execution_epoch = old_execution_epoch;
    result = host->port.buffer.ops->epoch_close(
        host->port.buffer.context, lifecycle_instance_nonce,
        old_execution_epoch);
    return result == FWLAB_CONTROLLER_BUFFER_V0_OK ? FWLAB_SPINE_V0_OK
                                                    : FWLAB_SPINE_V0_POISONED;
}

static enum fwlab_spine_result_v0 host_epoch_quiescent(
    void *context, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch,
    struct fwlab_host_data_epoch_status_v0 *status)
{
    struct j0_host_data *host = context;
    uint32_t active_buffers = 0;
    uint8_t buffer_quiescent = 0;
    enum fwlab_controller_buffer_result_v0 result;

    if (host == NULL || status == NULL || !host->close_started ||
        lifecycle_instance_nonce != host->close_lifecycle_nonce ||
        old_execution_epoch != host->close_execution_epoch) {
        return FWLAB_SPINE_V0_INVALID;
    }
    result = host->port.buffer.ops->epoch_quiescent(
        host->port.buffer.context, lifecycle_instance_nonce,
        old_execution_epoch, &active_buffers, &buffer_quiescent);
    if (result != FWLAB_CONTROLLER_BUFFER_V0_OK) {
        return FWLAB_SPINE_V0_POISONED;
    }
    memset(status, 0, sizeof(*status));
    status->version = FWLAB_HOST_DATA_V0_VERSION;
    status->size = (uint16_t)sizeof(*status);
    status->lifecycle_instance_nonce = lifecycle_instance_nonce;
    status->execution_epoch = old_execution_epoch;
    status->authority_refs = host->active_authorities;
    status->buffer_leases = active_buffers;
    status->dma_operations = host->active_dma;
    status->admission_closed = 1;
    status->quiescent = (uint8_t)(buffer_quiescent &&
        host->active_authorities == 0 && host->active_dma == 0);
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_host_data_ops_v0 host_ops = {
    .version = FWLAB_HOST_DATA_V0_VERSION,
    .size = sizeof(struct fwlab_host_data_ops_v0),
    .authority_mint = authority_mint,
    .authority_release = authority_release,
    .token_reserve = dma_token_reserve,
    .submit = dma_submit,
    .query = dma_query,
    .cancel = dma_cancel,
    .retire_start = dma_retire_start,
    .retire_query = dma_retire_query,
    .epoch_close = host_epoch_close,
    .epoch_quiescent = host_epoch_quiescent,
};

void j0_host_data_init(
    struct j0_host_data *host, struct j0_controller_buffer *buffer,
    uint64_t authority_issuer_nonce, uint64_t dma_issuer_nonce,
    uint32_t generation)
{
    if (host == NULL || buffer == NULL || authority_issuer_nonce == 0 ||
        dma_issuer_nonce == 0 || generation == 0 ||
        authority_issuer_nonce == dma_issuer_nonce ||
        authority_issuer_nonce == buffer->port.issuer_nonce ||
        dma_issuer_nonce == buffer->port.issuer_nonce) {
        return;
    }
    memset(host, 0, sizeof(*host));
    host->buffer = buffer;
    host->generation = generation;
    host->next_authority_uid = UINT64_C(10001);
    host->next_dma_uid = UINT64_C(20001);
    host->port.ops = &host_ops;
    host->port.context = host;
    host->port.buffer = buffer->port;
    host->port.authority_issuer_nonce = authority_issuer_nonce;
    host->port.dma_issuer_nonce = dma_issuer_nonce;
    host->port.generation = generation;
}
