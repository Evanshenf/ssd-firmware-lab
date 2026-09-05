/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "j0_internal.h"

#include <string.h>

int j0_bytes_zero(const void *value, size_t size)
{
    const uint8_t *bytes = value;
    size_t index;

    if (value == NULL) {
        return 0;
    }
    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

int j0_handle_equal(
    const struct fwlab_nvme_command_handle *left,
    const struct fwlab_nvme_command_handle *right)
{
    return left != NULL && right != NULL &&
           left->instance_nonce == right->instance_nonce &&
           left->command_uid == right->command_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->generation == right->generation;
}

int j0_origin_equal(
    const struct fwlab_nvme_origin_token *left,
    const struct fwlab_nvme_origin_token *right)
{
    return left != NULL && right != NULL && left->word[0] == right->word[0] &&
           left->word[1] == right->word[1];
}

int j0_action_token_equal(
    const struct fwlab_host_action_token_v0 *left,
    const struct fwlab_host_action_token_v0 *right)
{
    return left != NULL && right != NULL &&
           left->version == right->version && left->size == right->size &&
           left->type_tag == right->type_tag &&
           j0_handle_equal(&left->command, &right->command) &&
           j0_origin_equal(&left->origin, &right->origin) &&
           left->action_uid == right->action_uid &&
           left->generation == right->generation &&
           left->ordinal == right->ordinal && left->kind == right->kind &&
           j0_bytes_zero(left->reserved, sizeof(left->reserved)) &&
           j0_bytes_zero(right->reserved, sizeof(right->reserved));
}

static struct j0_controller_buffer_record *buffer_record_find(
    struct j0_controller_buffer *buffer,
    const struct fwlab_controller_buffer_lease_v0 *lease)
{
    uint32_t index;

    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        struct j0_controller_buffer_record *record = &buffer->record[index];

        if (record->occupied &&
            memcmp(&record->lease, lease, sizeof(*lease)) == 0) {
            return record;
        }
    }
    return NULL;
}

static struct j0_controller_buffer_record *buffer_request_find(
    struct j0_controller_buffer *buffer,
    const struct fwlab_controller_buffer_acquire_v0 *request)
{
    uint32_t index;

    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        struct j0_controller_buffer_record *record = &buffer->record[index];

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

static enum fwlab_controller_buffer_result_v0 buffer_acquire(
    void *context,
    const struct fwlab_controller_buffer_acquire_v0 *request,
    struct fwlab_controller_buffer_lease_v0 *lease)
{
    struct j0_controller_buffer *buffer = context;
    struct j0_controller_buffer_record *record;
    uint32_t index;

    if (buffer == NULL || request == NULL || lease == NULL ||
        !fwlab_controller_buffer_acquire_v0_valid(request) ||
        request->capacity_bytes > J0_MAX_TRANSFER_BYTES || buffer->poisoned) {
        return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    }
    if (buffer->admission_closed) {
        return FWLAB_CONTROLLER_BUFFER_V0_WRONG_STATE;
    }
    record = buffer_request_find(buffer, request);
    if (record != NULL) {
        if (memcmp(&record->request, request, sizeof(*request)) != 0 ||
            record->released) {
            buffer->poisoned = 1;
            return FWLAB_CONTROLLER_BUFFER_V0_POISONED;
        }
        *lease = record->lease;
        return FWLAB_CONTROLLER_BUFFER_V0_OK;
    }
    record = NULL;
    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        if (!buffer->record[index].occupied) {
            record = &buffer->record[index];
            break;
        }
    }
    if (record == NULL) {
        return FWLAB_CONTROLLER_BUFFER_V0_NO_CAPACITY;
    }
    if (buffer->next_buffer_uid == 0 || buffer->next_lease_uid == 0 ||
        buffer->next_buffer_uid == UINT64_MAX ||
        buffer->next_lease_uid == UINT64_MAX) {
        return FWLAB_CONTROLLER_BUFFER_V0_COUNTER_EXHAUSTED;
    }
    memset(record, 0, sizeof(*record));
    record->occupied = 1;
    record->request = *request;
    record->lease.version = FWLAB_CONTROLLER_BUFFER_V0_VERSION;
    record->lease.size = (uint16_t)sizeof(record->lease);
    record->lease.type_tag = FWLAB_CONTROLLER_BUFFER_LEASE_V0_TAG;
    record->lease.issuer_nonce = buffer->port.issuer_nonce;
    record->lease.buffer_uid = buffer->next_buffer_uid++;
    record->lease.lease_uid = buffer->next_lease_uid++;
    record->lease.generation = buffer->generation;
    record->lease.capacity_bytes = request->capacity_bytes;
    record->lease.rights = request->rights;
    ++buffer->active_leases;
    *lease = record->lease;
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 buffer_read(
    void *context,
    const struct fwlab_controller_buffer_lease_v0 *lease,
    const struct fwlab_controller_buffer_span_v0 *span,
    void *output, size_t output_size)
{
    struct j0_controller_buffer *buffer = context;
    struct j0_controller_buffer_record *record;

    if (buffer == NULL || lease == NULL || span == NULL || output == NULL ||
        output_size != span->length ||
        !fwlab_controller_buffer_span_v0_valid_for_lease(
            span, lease, FWLAB_CONTROLLER_BUFFER_V0_READ)) {
        return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    }
    record = buffer_record_find(buffer, lease);
    if (record == NULL) {
        return FWLAB_CONTROLLER_BUFFER_V0_STALE;
    }
    if (record->released) {
        return FWLAB_CONTROLLER_BUFFER_V0_WRONG_STATE;
    }
    memcpy(output, record->bytes + span->offset, output_size);
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 buffer_write(
    void *context,
    const struct fwlab_controller_buffer_lease_v0 *lease,
    const struct fwlab_controller_buffer_span_v0 *span,
    const void *input, size_t input_size)
{
    struct j0_controller_buffer *buffer = context;
    struct j0_controller_buffer_record *record;

    if (buffer == NULL || lease == NULL || span == NULL || input == NULL ||
        input_size != span->length ||
        !fwlab_controller_buffer_span_v0_valid_for_lease(
            span, lease, FWLAB_CONTROLLER_BUFFER_V0_WRITE)) {
        return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    }
    record = buffer_record_find(buffer, lease);
    if (record == NULL) {
        return FWLAB_CONTROLLER_BUFFER_V0_STALE;
    }
    if (record->released) {
        return FWLAB_CONTROLLER_BUFFER_V0_WRONG_STATE;
    }
    memcpy(record->bytes + span->offset, input, input_size);
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 buffer_copy(
    void *context,
    const struct fwlab_controller_buffer_lease_v0 *destination,
    const struct fwlab_controller_buffer_span_v0 *destination_span,
    const struct fwlab_controller_buffer_lease_v0 *source,
    const struct fwlab_controller_buffer_span_v0 *source_span)
{
    struct j0_controller_buffer *buffer = context;
    struct j0_controller_buffer_record *destination_record;
    struct j0_controller_buffer_record *source_record;

    if (buffer == NULL || destination == NULL || destination_span == NULL ||
        source == NULL || source_span == NULL ||
        destination_span->length != source_span->length ||
        !fwlab_controller_buffer_span_v0_valid_for_lease(
            destination_span, destination, FWLAB_CONTROLLER_BUFFER_V0_WRITE) ||
        !fwlab_controller_buffer_span_v0_valid_for_lease(
            source_span, source, FWLAB_CONTROLLER_BUFFER_V0_READ)) {
        return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    }
    destination_record = buffer_record_find(buffer, destination);
    source_record = buffer_record_find(buffer, source);
    if (destination_record == NULL || source_record == NULL) {
        return FWLAB_CONTROLLER_BUFFER_V0_STALE;
    }
    if (destination_record->released || source_record->released) {
        return FWLAB_CONTROLLER_BUFFER_V0_WRONG_STATE;
    }
    memmove(destination_record->bytes + destination_span->offset,
            source_record->bytes + source_span->offset,
            destination_span->length);
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 buffer_release(
    void *context,
    const struct fwlab_controller_buffer_lease_v0 *lease)
{
    struct j0_controller_buffer *buffer = context;
    struct j0_controller_buffer_record *record;

    if (buffer == NULL || lease == NULL ||
        !fwlab_controller_buffer_lease_v0_valid(lease)) {
        return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    }
    record = buffer_record_find(buffer, lease);
    if (record == NULL) {
        return FWLAB_CONTROLLER_BUFFER_V0_STALE;
    }
    if (record->released) {
        return FWLAB_CONTROLLER_BUFFER_V0_OK;
    }
    memset(record->bytes, 0, sizeof(record->bytes));
    record->released = 1;
    if (buffer->active_leases == 0) {
        buffer->poisoned = 1;
        return FWLAB_CONTROLLER_BUFFER_V0_POISONED;
    }
    --buffer->active_leases;
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 buffer_epoch_close(
    void *context, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch)
{
    struct j0_controller_buffer *buffer = context;

    if (buffer == NULL || lifecycle_instance_nonce == 0 ||
        old_execution_epoch == 0) {
        return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    }
    if (buffer->close_started) {
        return buffer->close_lifecycle_nonce == lifecycle_instance_nonce &&
                       buffer->close_execution_epoch == old_execution_epoch
                   ? FWLAB_CONTROLLER_BUFFER_V0_OK
                   : FWLAB_CONTROLLER_BUFFER_V0_STALE;
    }
    buffer->admission_closed = 1;
    buffer->close_started = 1;
    buffer->close_lifecycle_nonce = lifecycle_instance_nonce;
    buffer->close_execution_epoch = old_execution_epoch;
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 buffer_epoch_quiescent(
    void *context, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch, uint32_t *active_leases,
    uint8_t *quiescent)
{
    struct j0_controller_buffer *buffer = context;

    if (buffer == NULL || active_leases == NULL || quiescent == NULL ||
        !buffer->close_started ||
        lifecycle_instance_nonce != buffer->close_lifecycle_nonce ||
        old_execution_epoch != buffer->close_execution_epoch) {
        return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    }
    *active_leases = buffer->active_leases;
    *quiescent = (uint8_t)(buffer->active_leases == 0);
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static const struct fwlab_controller_buffer_ops_v0 buffer_ops = {
    .version = FWLAB_CONTROLLER_BUFFER_V0_VERSION,
    .size = sizeof(struct fwlab_controller_buffer_ops_v0),
    .acquire = buffer_acquire,
    .read = buffer_read,
    .write = buffer_write,
    .copy = buffer_copy,
    .release = buffer_release,
    .epoch_close = buffer_epoch_close,
    .epoch_quiescent = buffer_epoch_quiescent,
};

void j0_controller_buffer_init(
    struct j0_controller_buffer *buffer, uint64_t issuer_nonce,
    uint32_t generation)
{
    if (buffer == NULL || issuer_nonce == 0 || generation == 0) {
        return;
    }
    memset(buffer, 0, sizeof(*buffer));
    buffer->generation = generation;
    buffer->next_buffer_uid = UINT64_C(30001);
    buffer->next_lease_uid = UINT64_C(31001);
    buffer->port.ops = &buffer_ops;
    buffer->port.context = buffer;
    buffer->port.issuer_nonce = issuer_nonce;
    buffer->port.generation = generation;
}
