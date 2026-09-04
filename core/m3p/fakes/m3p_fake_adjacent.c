/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "m3p_fake_adjacent.h"

#include "../m3p_internal.h"
#include "../../../media/file-nand-v0/file_nand_internal.h"

#include <string.h>

static enum fwlab_controller_buffer_result_v0 fake_buffer_acquire(
    void *opaque, const struct fwlab_controller_buffer_acquire_v0 *request,
    struct fwlab_controller_buffer_lease_v0 *lease)
{
    struct m3p_fake_controller_buffer *buffer = opaque;

    if (buffer == NULL || request == NULL || lease == NULL || buffer->closed ||
        buffer->acquired || request->capacity_bytes > M3P_FAKE_BUFFER_BYTES ||
        request->rights == 0) {
        return FWLAB_CONTROLLER_BUFFER_V0_NO_CAPACITY;
    }
    buffer->lease.version = FWLAB_CONTROLLER_BUFFER_V0_VERSION;
    buffer->lease.size = (uint16_t)sizeof(buffer->lease);
    buffer->lease.type_tag = FWLAB_CONTROLLER_BUFFER_LEASE_V0_TAG;
    buffer->lease.issuer_nonce = buffer->issuer_nonce;
    buffer->lease.buffer_uid = 1;
    buffer->lease.lease_uid = 1;
    buffer->lease.generation = buffer->generation;
    buffer->lease.capacity_bytes = request->capacity_bytes;
    buffer->lease.rights = request->rights;
    buffer->acquired = 1;
    *lease = buffer->lease;
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static int fake_lease_matches(
    const struct m3p_fake_controller_buffer *buffer,
    const struct fwlab_controller_buffer_lease_v0 *lease,
    const struct fwlab_controller_buffer_span_v0 *span, size_t size,
    uint32_t right)
{
    return buffer != NULL && lease != NULL && span != NULL &&
           buffer->acquired && !buffer->closed &&
           memcmp(lease, &buffer->lease, sizeof(*lease)) == 0 &&
           span->version == FWLAB_CONTROLLER_BUFFER_V0_VERSION &&
           span->size == sizeof(*span) && span->reserved0 == 0 &&
           span->length == size &&
           (uint64_t)span->offset + span->length <= lease->capacity_bytes &&
           (lease->rights & right) == right &&
           m3p_bytes_zero(span->reserved1, sizeof(span->reserved1));
}

static enum fwlab_controller_buffer_result_v0 fake_buffer_read(
    void *opaque, const struct fwlab_controller_buffer_lease_v0 *lease,
    const struct fwlab_controller_buffer_span_v0 *span, void *output,
    size_t output_size)
{
    struct m3p_fake_controller_buffer *buffer = opaque;

    if (output == NULL || !fake_lease_matches(buffer, lease, span,
            output_size, FWLAB_CONTROLLER_BUFFER_V0_READ)) {
        return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    }
    memcpy(output, &buffer->bytes[span->offset], output_size);
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 fake_buffer_write(
    void *opaque, const struct fwlab_controller_buffer_lease_v0 *lease,
    const struct fwlab_controller_buffer_span_v0 *span, const void *input,
    size_t input_size)
{
    struct m3p_fake_controller_buffer *buffer = opaque;

    if (input == NULL || !fake_lease_matches(buffer, lease, span, input_size,
            FWLAB_CONTROLLER_BUFFER_V0_WRITE)) {
        return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    }
    memcpy(&buffer->bytes[span->offset], input, input_size);
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 fake_buffer_copy(
    void *opaque, const struct fwlab_controller_buffer_lease_v0 *destination,
    const struct fwlab_controller_buffer_span_v0 *destination_span,
    const struct fwlab_controller_buffer_lease_v0 *source,
    const struct fwlab_controller_buffer_span_v0 *source_span)
{
    struct m3p_fake_controller_buffer *buffer = opaque;

    if (!fake_lease_matches(buffer, destination, destination_span,
            destination_span == NULL ? 0 : destination_span->length,
            FWLAB_CONTROLLER_BUFFER_V0_WRITE) ||
        !fake_lease_matches(buffer, source, source_span,
            source_span == NULL ? 0 : source_span->length,
            FWLAB_CONTROLLER_BUFFER_V0_READ) ||
        destination_span->length != source_span->length) {
        return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    }
    memmove(&buffer->bytes[destination_span->offset],
            &buffer->bytes[source_span->offset], source_span->length);
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 fake_buffer_release(
    void *opaque, const struct fwlab_controller_buffer_lease_v0 *lease)
{
    struct m3p_fake_controller_buffer *buffer = opaque;

    if (buffer == NULL || lease == NULL || !buffer->acquired ||
        memcmp(lease, &buffer->lease, sizeof(*lease)) != 0) {
        return FWLAB_CONTROLLER_BUFFER_V0_STALE;
    }
    buffer->acquired = 0;
    memset(&buffer->lease, 0, sizeof(buffer->lease));
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 fake_buffer_epoch_close(
    void *opaque, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch)
{
    struct m3p_fake_controller_buffer *buffer = opaque;

    if (buffer == NULL || lifecycle_instance_nonce == 0 ||
        old_execution_epoch == 0) {
        return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    }
    buffer->closed = 1;
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 fake_buffer_quiescent(
    void *opaque, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch, uint32_t *active_leases,
    uint8_t *quiescent)
{
    struct m3p_fake_controller_buffer *buffer = opaque;

    if (buffer == NULL || lifecycle_instance_nonce == 0 ||
        old_execution_epoch == 0 || active_leases == NULL ||
        quiescent == NULL) {
        return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    }
    *active_leases = buffer->acquired ? 1 : 0;
    *quiescent = (uint8_t)(buffer->closed && !buffer->acquired);
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static const struct fwlab_controller_buffer_ops_v0 fake_buffer_ops = {
    .version = FWLAB_CONTROLLER_BUFFER_V0_VERSION,
    .size = sizeof(struct fwlab_controller_buffer_ops_v0),
    .reserved0 = 0,
    .acquire = fake_buffer_acquire,
    .read = fake_buffer_read,
    .write = fake_buffer_write,
    .copy = fake_buffer_copy,
    .release = fake_buffer_release,
    .epoch_close = fake_buffer_epoch_close,
    .epoch_quiescent = fake_buffer_quiescent,
    .reserved1 = {0, 0, 0, 0},
};

void m3p_fake_controller_buffer_init(
    struct m3p_fake_controller_buffer *buffer, uint64_t issuer_nonce,
    uint32_t generation)
{
    memset(buffer, 0, sizeof(*buffer));
    buffer->issuer_nonce = issuer_nonce;
    buffer->generation = generation;
}

struct fwlab_controller_buffer_port_v0 m3p_fake_controller_buffer_port(
    struct m3p_fake_controller_buffer *buffer)
{
    struct fwlab_controller_buffer_port_v0 port;

    memset(&port, 0, sizeof(port));
    port.ops = &fake_buffer_ops;
    port.context = buffer;
    port.issuer_nonce = buffer->issuer_nonce;
    port.generation = buffer->generation;
    return port;
}

static enum fwlab_nfc_api_result fake_size(void *opaque, uint64_t *size)
{
    struct m3p_fake_file_substrate *substrate = opaque;

    if (substrate == NULL || size == NULL || substrate->closed) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *size = substrate->size;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result fake_resize(void *opaque, uint64_t size)
{
    struct m3p_fake_file_substrate *substrate = opaque;

    if (substrate == NULL || substrate->closed || size > substrate->capacity) {
        return FWLAB_NFC_API_NO_CAPACITY;
    }
    substrate->size = size;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result fake_read(void *opaque, uint64_t offset,
                                            void *buffer, size_t size)
{
    struct m3p_fake_file_substrate *substrate = opaque;

    if (substrate == NULL || substrate->closed || buffer == NULL ||
        offset > substrate->size || size > substrate->size - offset) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memcpy(buffer, &substrate->bytes[offset], size);
    return FWLAB_NFC_API_OK;
}

static void record_write(struct m3p_fake_file_substrate *substrate,
                         const uint8_t *bytes, size_t size)
{
    if (size < 9) {
        return;
    }
    if (m3p_get_le32(bytes) == FILE_NAND_PAGE_MAGIC) {
        ++substrate->page_candidates;
    } else if (m3p_get_le32(bytes) == FILE_NAND_HEALTH_MAGIC) {
        ++substrate->health_candidates;
    } else if (m3p_get_le32(bytes) == FILE_NAND_WAL_RECORD_MAGIC) {
        if (bytes[8] == FILE_NAND_WAL_BEGIN) {
            ++substrate->wal_begin;
        } else if (bytes[8] == FILE_NAND_WAL_APPLIED ||
                   bytes[8] == FILE_NAND_WAL_NO_EFFECT) {
            ++substrate->wal_applied;
        } else if (bytes[8] == FILE_NAND_WAL_COMMIT) {
            ++substrate->wal_commit;
        } else if (bytes[8] == FILE_NAND_WAL_ROLLBACK) {
            ++substrate->wal_rollback;
        }
    }
}

static enum fwlab_nfc_api_result fake_write(void *opaque, uint64_t offset,
                                             const void *buffer, size_t size)
{
    struct m3p_fake_file_substrate *substrate = opaque;

    if (substrate == NULL || substrate->closed || buffer == NULL ||
        offset > substrate->size || size > substrate->size - offset) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    ++substrate->writes;
    if (substrate->fail_write != 0 &&
        substrate->writes == substrate->fail_write) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    record_write(substrate, buffer, size);
    memcpy(&substrate->bytes[offset], buffer, size);
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result fake_barrier(void *opaque)
{
    struct m3p_fake_file_substrate *substrate = opaque;

    if (substrate == NULL || substrate->closed) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    ++substrate->barriers;
    if (substrate->fail_barrier != 0 &&
        substrate->barriers == substrate->fail_barrier) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result fake_identity(
    void *opaque, struct file_nand_identity *identity)
{
    struct m3p_fake_file_substrate *substrate = opaque;

    if (substrate == NULL || substrate->closed || identity == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    identity->device = substrate->device;
    identity->inode = substrate->inode;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result fake_close(void *opaque)
{
    struct m3p_fake_file_substrate *substrate = opaque;

    if (substrate == NULL || substrate->closed) {
        return FWLAB_NFC_API_WRONG_STATE;
    }
    substrate->closed = 1;
    return FWLAB_NFC_API_OK;
}

static const struct file_nand_substrate_ops fake_file_ops = {
    .version = FWLAB_FILE_NAND_V0_VERSION,
    .size = sizeof(struct file_nand_substrate_ops),
    .reserved = 0,
    .size_get = fake_size,
    .resize = fake_resize,
    .read = fake_read,
    .write = fake_write,
    .barrier = fake_barrier,
    .identity = fake_identity,
    .close = fake_close,
};

void m3p_fake_file_substrate_init(
    struct m3p_fake_file_substrate *substrate, uint8_t *bytes,
    size_t capacity, uint64_t device, uint64_t inode)
{
    memset(substrate, 0, sizeof(*substrate));
    substrate->bytes = bytes;
    substrate->capacity = capacity;
    substrate->device = device;
    substrate->inode = inode;
}

enum fwlab_nfc_api_result m3p_fake_file_format(
    void *arena, size_t arena_size, struct m3p_fake_file_substrate *substrate,
    const uint8_t media_uuid[16], struct fwlab_file_nand_v0 **media,
    struct fwlab_file_nand_holder_v0 *holder)
{
    struct file_nand_substrate binding = {&fake_file_ops, substrate};
    enum fwlab_nfc_api_result result = file_nand_engine_format(
        arena, arena_size, &binding, media_uuid, media);

    if (result == FWLAB_NFC_API_OK) {
        memset(holder, 0, sizeof(*holder));
        holder->device = substrate->device;
        holder->inode = substrate->inode;
        memcpy(holder->media_uuid, media_uuid, 16);
    }
    return result;
}

enum fwlab_nfc_api_result m3p_fake_file_restart(
    void *arena, size_t arena_size, struct m3p_fake_file_substrate *substrate,
    const struct fwlab_file_nand_holder_v0 *holder,
    struct fwlab_file_nand_v0 **media)
{
    struct file_nand_substrate binding = {&fake_file_ops, substrate};

    return file_nand_engine_restart(arena, arena_size, &binding, holder,
                                    media);
}

int m3p_fake_file_corrupt(struct m3p_fake_file_substrate *substrate,
                          uint64_t offset, uint8_t mask)
{
    if (substrate == NULL || offset >= substrate->size || mask == 0) {
        return 0;
    }
    substrate->bytes[offset] ^= mask;
    return 1;
}

static struct fwlab_nfc_submit_result fake_nfc_submit(
    void *opaque, const struct fwlab_nfc_request *request)
{
    struct m3p_fake_nfc *fake = opaque;
    struct fwlab_nfc_submit_result result;

    memset(&result, 0, sizeof(result));
    if (fake == NULL || request == NULL || fake->pending ||
        fake->event_ready || request->version != FWLAB_NFC_CONTRACT_VERSION ||
        request->size != sizeof(*request) ||
        request->operation.instance_nonce != fake->instance_nonce ||
        request->operation.controller_epoch != fake->epoch ||
        request->operation.operation_uid == 0 ||
        request->operation.generation == 0 || request->kind > FWLAB_NFC_STATUS) {
        result.disposition = FWLAB_NFC_REJECTED;
        result.reason = FWLAB_NFC_REASON_RANGE;
        return result;
    }
    ++fake->submissions;
    fake->request = *request;
    fake->pending = 1;
    result.disposition = FWLAB_NFC_ACCEPTED;
    return result;
}

static enum fwlab_nfc_api_result fake_nfc_cancel(
    void *opaque, const struct fwlab_nfc_operation_token *operation)
{
    struct m3p_fake_nfc *fake = opaque;

    if (fake == NULL || operation == NULL || !fake->pending ||
        memcmp(operation, &fake->request.operation, sizeof(*operation)) != 0) {
        return FWLAB_NFC_API_NOT_FOUND;
    }
    fake->completion.terminal = FWLAB_NFC_TERMINAL_CANCELLED;
    fake->completion.reason = FWLAB_NFC_REASON_CANCELLED;
    fake->pending = 0;
    fake->event_ready = 1;
    return FWLAB_NFC_API_OK;
}

static void fake_completion_init(struct m3p_fake_nfc *fake)
{
    memset(&fake->completion, 0, sizeof(fake->completion));
    fake->completion.version = FWLAB_NFC_CONTRACT_VERSION;
    fake->completion.size = (uint16_t)sizeof(fake->completion);
    fake->completion.operation = fake->request.operation;
    fake->completion.ppa = fake->request.ppa;
    fake->completion.cache = fake->request.cache;
    fake->completion.cookie = fake->request.cookie;
    fake->completion.operation_kind = fake->request.kind;
    fake->completion.requested_region_mask = fake->request.region_mask;
    fake->completion.terminal = FWLAB_NFC_TERMINAL_SUCCESS;
    fake->completion.reason = FWLAB_NFC_REASON_NONE;
}

static enum fwlab_nfc_api_result fake_process_read_trigger(
    struct m3p_fake_nfc *fake)
{
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
    enum fwlab_nfc_api_result result = fake->media.ops->read_page(
        fake->media.context, &fake->request.ppa, fake->main, sizeof(fake->main),
        fake->oob, sizeof(fake->oob), &page, &block);

    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    fake->completion.base_erase_generation = block.erase_generation;
    fake->completion.final_erase_generation = block.erase_generation;
    fake->completion.block_health = block.health;
    fake->completion.valid_region_mask = fake->request.region_mask;
    if (block.health != FWLAB_NFC_BLOCK_GOOD ||
        page.state == FWLAB_NAND_PAGE_TORN ||
        block.erase_state == FWLAB_NAND_ERASE_TORN) {
        fake->completion.terminal = FWLAB_NFC_TERMINAL_FAILED;
        fake->completion.reason = block.health != FWLAB_NFC_BLOCK_GOOD ?
            FWLAB_NFC_REASON_BAD_BLOCK : FWLAB_NFC_REASON_ECC_UNCORRECTABLE;
        return FWLAB_NFC_API_OK;
    }
    ++fake->cache_generation;
    fake->completion.cache.instance_nonce = fake->instance_nonce;
    fake->completion.cache.controller_epoch = fake->epoch;
    fake->completion.cache.generation = fake->cache_generation;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result fake_process_read_transfer(
    struct m3p_fake_nfc *fake)
{
    if (fake->request.cache.instance_nonce != fake->instance_nonce ||
        fake->request.cache.controller_epoch != fake->epoch ||
        fake->request.cache.generation != fake->cache_generation) {
        fake->completion.terminal = FWLAB_NFC_TERMINAL_FAILED;
        fake->completion.reason = FWLAB_NFC_REASON_STALE;
        return FWLAB_NFC_API_OK;
    }
    if ((fake->request.region_mask & FWLAB_NFC_REGION_MAIN) != 0 &&
        fake->buffers.ops->write(fake->buffers.context, &fake->request.main,
            fake->main, sizeof(fake->main)) != FWLAB_NFC_API_OK) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    if ((fake->request.region_mask & FWLAB_NFC_REGION_OOB) != 0 &&
        fake->buffers.ops->write(fake->buffers.context, &fake->request.oob,
            fake->oob, sizeof(fake->oob)) != FWLAB_NFC_API_OK) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    fake->completion.valid_region_mask = fake->request.region_mask;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result fake_process_program_transfer(
    struct m3p_fake_nfc *fake)
{
    if (fake->buffers.ops->read(fake->buffers.context, &fake->request.main,
            fake->main, sizeof(fake->main)) != FWLAB_NFC_API_OK ||
        ((fake->request.region_mask & FWLAB_NFC_REGION_OOB) != 0 &&
         fake->buffers.ops->read(fake->buffers.context, &fake->request.oob,
            fake->oob, sizeof(fake->oob)) != FWLAB_NFC_API_OK)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    if ((fake->request.region_mask & FWLAB_NFC_REGION_OOB) == 0) {
        memset(fake->oob, 0xff, sizeof(fake->oob));
    }
    ++fake->cache_generation;
    fake->completion.cache.instance_nonce = fake->instance_nonce;
    fake->completion.cache.controller_epoch = fake->epoch;
    fake->completion.cache.generation = fake->cache_generation;
    fake->completion.valid_region_mask = fake->request.region_mask;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result fake_apply_media_result(
    struct m3p_fake_nfc *fake, enum fwlab_nfc_api_result call_result,
    const struct fwlab_nand_media_result *media_result)
{
    if (call_result != FWLAB_NFC_API_OK) {
        return call_result;
    }
    fake->completion.physical_outcome = media_result->physical_outcome;
    fake->completion.integrity = media_result->integrity;
    fake->completion.reason = media_result->reason;
    fake->completion.block_health = media_result->block_health;
    fake->completion.applied_region_mask = media_result->applied_region_mask;
    fake->completion.base_erase_generation =
        media_result->base_erase_generation;
    fake->completion.final_erase_generation =
        media_result->final_erase_generation;
    if (media_result->physical_outcome != FWLAB_NFC_PHYS_APPLIED ||
        media_result->reason != FWLAB_NFC_REASON_NONE) {
        fake->completion.terminal = FWLAB_NFC_TERMINAL_FAILED;
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result fake_process_request(
    struct m3p_fake_nfc *fake)
{
    struct fwlab_nand_media_result media_result;

    fake_completion_init(fake);
    switch ((enum fwlab_nfc_operation_kind)fake->request.kind) {
    case FWLAB_NFC_READ_TRIGGER:
        return fake_process_read_trigger(fake);
    case FWLAB_NFC_READ_TRANSFER:
        return fake_process_read_transfer(fake);
    case FWLAB_NFC_PROGRAM_TRANSFER:
        return fake_process_program_transfer(fake);
    case FWLAB_NFC_PROGRAM_EXECUTE:
        if (fake->request.cache.instance_nonce != fake->instance_nonce ||
            fake->request.cache.controller_epoch != fake->epoch ||
            fake->request.cache.generation != fake->cache_generation) {
            fake->completion.terminal = FWLAB_NFC_TERMINAL_FAILED;
            fake->completion.reason = FWLAB_NFC_REASON_STALE;
            return FWLAB_NFC_API_OK;
        }
        return fake_apply_media_result(fake, fake->media.ops->program(
            fake->media.context, &fake->request.ppa, fake->main,
            sizeof(fake->main), fake->oob, sizeof(fake->oob),
            sizeof(fake->main), sizeof(fake->oob),
            FWLAB_NFC_INTEGRITY_COMPLETE, &media_result), &media_result);
    case FWLAB_NFC_ERASE:
        return fake_apply_media_result(fake, fake->media.ops->erase(
            fake->media.context, &fake->request.ppa, M3P_PAGES_PER_BLOCK,
            FWLAB_NFC_INTEGRITY_COMPLETE, &media_result), &media_result);
    case FWLAB_NFC_STATUS:
        return FWLAB_NFC_API_OK;
    default:
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
}

static enum fwlab_nfc_api_result fake_nfc_step(
    void *opaque, uint32_t budget, struct fwlab_nfc_step_result *result)
{
    struct m3p_fake_nfc *fake = opaque;
    enum fwlab_nfc_api_result call_result;

    if (fake == NULL || result == NULL || budget == 0) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    ++fake->steps;
    memset(result, 0, sizeof(*result));
    if (!fake->pending) {
        result->phase = FWLAB_NFC_MODEL_READY;
        return FWLAB_NFC_API_OK;
    }
    call_result = fake_process_request(fake);
    if (call_result != FWLAB_NFC_API_OK) {
        return call_result;
    }
    fake->pending = 0;
    fake->event_ready = 1;
    result->units_used = 1;
    result->transitions = 1;
    result->events_pending = 1;
    result->phase = FWLAB_NFC_MODEL_READY;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result fake_nfc_poll(
    void *opaque, uint32_t budget, struct fwlab_nfc_completion *events,
    uint32_t event_capacity, uint32_t *event_count)
{
    struct m3p_fake_nfc *fake = opaque;

    if (fake == NULL || budget == 0 || events == NULL ||
        event_capacity == 0 || event_count == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    ++fake->polls;
    *event_count = 0;
    if (fake->event_ready) {
        events[0] = fake->completion;
        fake->event_ready = 0;
        *event_count = 1;
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result fake_nfc_reset(
    void *opaque, uint64_t instance_nonce, uint32_t old_epoch)
{
    struct m3p_fake_nfc *fake = opaque;

    if (fake == NULL || instance_nonce != fake->instance_nonce ||
        old_epoch != fake->epoch || fake->pending || fake->event_ready) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    ++fake->resets;
    ++fake->epoch;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result fake_nfc_quiescent(
    void *opaque, uint64_t instance_nonce, uint32_t old_epoch,
    bool *quiescent)
{
    struct m3p_fake_nfc *fake = opaque;

    if (fake == NULL || quiescent == NULL ||
        instance_nonce != fake->instance_nonce || old_epoch + 1u != fake->epoch) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *quiescent = !fake->pending && !fake->event_ready;
    return FWLAB_NFC_API_OK;
}

static const struct fwlab_nfc_provider_ops fake_nfc_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_nfc_provider_ops),
    .reserved = 0,
    .try_submit = fake_nfc_submit,
    .cancel = fake_nfc_cancel,
    .step = fake_nfc_step,
    .poll = fake_nfc_poll,
    .reset_begin = fake_nfc_reset,
    .quiescent = fake_nfc_quiescent,
};

void m3p_fake_nfc_init(
    struct m3p_fake_nfc *fake, struct fwlab_nfc_buffer_provider buffers,
    struct fwlab_nand_media media, uint64_t instance_nonce, uint32_t epoch)
{
    memset(fake, 0, sizeof(*fake));
    fake->buffers = buffers;
    fake->media = media;
    fake->instance_nonce = instance_nonce;
    fake->epoch = epoch;
}

struct fwlab_nfc_provider m3p_fake_nfc_provider(struct m3p_fake_nfc *fake)
{
    struct fwlab_nfc_provider provider = {&fake_nfc_ops, fake};

    return provider;
}
