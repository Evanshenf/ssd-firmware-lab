/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "m3p_internal.h"

#include <stdalign.h>
#include <string.h>

static int m3p_live(const struct fwlab_m3p *m3p)
{
    return m3p != NULL && m3p->magic == M3P_MAGIC && m3p->initialized &&
           !m3p->quarantined;
}

static int host_action_token_valid(
    const struct fwlab_host_action_token_v0 *token, uint16_t expected_kind)
{
    return token != NULL &&
           token->version == FWLAB_HOST_ACTION_PROGRAM_V0_VERSION &&
           token->size == sizeof(*token) &&
           token->type_tag == FWLAB_HOST_ACTION_TOKEN_V0_TAG &&
           token->command.instance_nonce != 0 &&
           token->command.command_uid != 0 &&
           token->command.controller_epoch != 0 &&
           token->command.generation != 0 &&
           (token->origin.word[0] != 0 || token->origin.word[1] != 0) &&
           token->action_uid != 0 && token->generation != 0 &&
           token->ordinal < FWLAB_HOST_ACTION_V0_MAX_ACTIONS &&
           token->kind == expected_kind &&
           m3p_bytes_zero(token->reserved, sizeof(token->reserved));
}

static uint16_t operation_action_kind(uint32_t operation)
{
    switch (operation) {
    case FWLAB_BLOCK_V0_READ:
        return FWLAB_HOST_ACTION_V0_BLOCK_READ;
    case FWLAB_BLOCK_V0_WRITE:
        return FWLAB_HOST_ACTION_V0_BLOCK_WRITE;
    case FWLAB_BLOCK_V0_FLUSH:
        return FWLAB_HOST_ACTION_V0_BLOCK_FLUSH;
    case FWLAB_BLOCK_V0_TRIM:
        return FWLAB_HOST_ACTION_V0_BLOCK_TRIM;
    default:
        return 0;
    }
}

static int buffer_lease_valid(
    const struct fwlab_m3p *m3p,
    const struct fwlab_controller_buffer_lease_v0 *lease,
    const struct fwlab_controller_buffer_span_v0 *span, uint32_t rights,
    uint32_t exact_bytes)
{
    return lease != NULL && span != NULL &&
           lease->version == FWLAB_CONTROLLER_BUFFER_V0_VERSION &&
           lease->size == sizeof(*lease) &&
           lease->type_tag == FWLAB_CONTROLLER_BUFFER_LEASE_V0_TAG &&
           lease->issuer_nonce == m3p->controller_buffer.issuer_nonce &&
           lease->buffer_uid != 0 && lease->lease_uid != 0 &&
           lease->generation == m3p->controller_buffer.generation &&
           lease->capacity_bytes >= exact_bytes &&
           (lease->rights & rights) == rights && lease->reserved0 == 0 &&
           m3p_bytes_zero(lease->reserved1, sizeof(lease->reserved1)) &&
           span->version == FWLAB_CONTROLLER_BUFFER_V0_VERSION &&
           span->size == sizeof(*span) && span->reserved0 == 0 &&
           span->length == exact_bytes &&
           (uint64_t)span->offset + span->length <= lease->capacity_bytes &&
           m3p_bytes_zero(span->reserved1, sizeof(span->reserved1));
}

static int request_valid(const struct fwlab_m3p *m3p,
                         const struct fwlab_block_request_v0 *request)
{
    uint16_t action_kind;
    uint64_t end;
    uint32_t exact_bytes;

    if (request == NULL ||
        request->version != FWLAB_BLOCK_SERVICE_V0_VERSION ||
        request->size != sizeof(*request) || request->reserved0 != 0 ||
        request->operation < FWLAB_BLOCK_V0_READ ||
        request->operation > FWLAB_BLOCK_V0_TRIM ||
        request->operation_token.version != FWLAB_BLOCK_SERVICE_V0_VERSION ||
        request->operation_token.size != sizeof(request->operation_token) ||
        request->operation_token.type_tag != FWLAB_BLOCK_OP_TOKEN_V0_TAG ||
        request->operation_token.provider_nonce != m3p->config.provider_nonce ||
        request->operation_token.generation != m3p->config.generation ||
        !m3p_bytes_zero(request->operation_token.reserved,
                        sizeof(request->operation_token.reserved)) ||
        !m3p_namespace_equal(&request->namespace_ref,
                             &m3p->config.namespace_ref) ||
        request->buffer_present > 1 ||
        !m3p_bytes_zero(request->reserved1, sizeof(request->reserved1)) ||
        !m3p_bytes_zero(request->reserved2, sizeof(request->reserved2))) {
        return 0;
    }
    action_kind = operation_action_kind(request->operation);
    if (!host_action_token_valid(&request->operation_token.action,
                                 action_kind)) {
        return 0;
    }
    if (request->operation == FWLAB_BLOCK_V0_FLUSH) {
        return request->lba == 0 && request->lba_count == 0 &&
               request->durability == FWLAB_BLOCK_V0_DURABILITY_FRONTIER &&
               !request->buffer_present &&
               m3p_bytes_zero(&request->buffer, sizeof(request->buffer)) &&
               m3p_bytes_zero(&request->buffer_span,
                               sizeof(request->buffer_span));
    }
    if (request->lba_count == 0 || request->lba_count > FWLAB_M3P_MAX_LBAS) {
        return 0;
    }
    end = request->lba + request->lba_count;
    if (end > FWLAB_M3P_NAMESPACE_LBAS || end < request->lba) {
        return 0;
    }
    if (request->operation == FWLAB_BLOCK_V0_TRIM) {
        return request->durability == FWLAB_BLOCK_V0_DURABILITY_NONE &&
               !request->buffer_present &&
               m3p_bytes_zero(&request->buffer, sizeof(request->buffer)) &&
               m3p_bytes_zero(&request->buffer_span,
                               sizeof(request->buffer_span));
    }
    exact_bytes = request->lba_count * FWLAB_M3P_LBA_BYTES;
    if (!request->buffer_present) {
        return 0;
    }
    if (request->operation == FWLAB_BLOCK_V0_READ) {
        return request->durability == FWLAB_BLOCK_V0_DURABILITY_NONE &&
               buffer_lease_valid(m3p, &request->buffer,
                   &request->buffer_span, FWLAB_CONTROLLER_BUFFER_V0_WRITE,
                   exact_bytes);
    }
    return (request->durability ==
                FWLAB_BLOCK_V0_DURABILITY_VOLATILE_ALLOWED ||
            request->durability == FWLAB_BLOCK_V0_DURABILITY_SELF) &&
           buffer_lease_valid(m3p, &request->buffer, &request->buffer_span,
                              FWLAB_CONTROLLER_BUFFER_V0_READ, exact_bytes);
}

static void submit_result_init(
    struct fwlab_block_submit_result_v0 *result,
    const struct fwlab_block_op_token_v0 *token, uint32_t disposition,
    uint32_t fault_code)
{
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    result->size = (uint16_t)sizeof(*result);
    result->operation_token = *token;
    result->disposition = disposition;
    if (disposition == FWLAB_HOST_ACTION_V0_REJECTED) {
        result->fault_domain = 1;
        result->fault_code = fault_code == 0 ? 1 : fault_code;
    }
}

static void submit_resource_failure(
    struct fwlab_block_submit_result_v0 *result,
    const struct fwlab_block_op_token_v0 *token, uint32_t code)
{
    submit_result_init(result, token, FWLAB_HOST_ACTION_V0_REJECTED, code);
    result->fault_domain = FWLAB_BLOCK_V0_FAULT_RESOURCE;
}

static int child_credit_available(const struct fwlab_m3p *m3p,
                                  uint32_t required)
{
    return required != 0 && m3p->next_child_uid != 0 &&
           m3p->next_child_uid <= m3p->config.nfc_operation_uid_limit &&
           required - 1u <= m3p->config.nfc_operation_uid_limit -
                                m3p->next_child_uid;
}

static uint32_t request_child_bound(const struct fwlab_m3p *m3p,
                                    const struct fwlab_block_request_v0 *request)
{
    switch (request->operation) {
    case FWLAB_BLOCK_V0_READ:
        return 6;
    case FWLAB_BLOCK_V0_WRITE:
        return request->durability == FWLAB_BLOCK_V0_DURABILITY_SELF ?
            20 : 10;
    case FWLAB_BLOCK_V0_FLUSH:
        return 8;
    case FWLAB_BLOCK_V0_TRIM:
        return m3p->pending_count < M3P_MAX_PENDING ? 1 : 0;
    default:
        return 0;
    }
}

static uint8_t sector_mask(uint64_t lba, uint32_t count, uint16_t lpn)
{
    uint64_t request_end = lba + count;
    uint64_t page_start = (uint64_t)lpn * M3P_SECTORS_PER_PAGE;
    uint64_t page_end = page_start + M3P_SECTORS_PER_PAGE;
    uint64_t first = lba > page_start ? lba : page_start;
    uint64_t last = request_end < page_end ? request_end : page_end;
    uint8_t mask = 0;
    uint64_t sector;

    for (sector = first; sector < last; ++sector) {
        mask |= (uint8_t)(1u << (sector - page_start));
    }
    return mask;
}

static int reserve_write_pages(struct fwlab_m3p *m3p,
                               struct m3p_operation *operation)
{
    uint8_t saved_next[M3P_BLOCKS];
    uint8_t saved_p2l[M3P_PHYSICAL_PAGES];
    uint32_t saved_record_sequence = m3p->record_sequence;
    uint16_t lpn;

    memcpy(saved_next, m3p->block_next_page, sizeof(saved_next));
    memcpy(saved_p2l, m3p->p2l, sizeof(saved_p2l));
    operation->delta_count = 0;
    for (lpn = operation->first_lpn; lpn <= operation->last_lpn; ++lpn) {
        struct m3p_delta *delta =
            &operation->delta[operation->delta_count];
        struct fwlab_nfc_ppa ppa;
        uint8_t written = sector_mask(operation->request.lba,
                                      operation->request.lba_count, lpn);

        if (!m3p_allocate_data_page(m3p, &ppa) ||
            m3p->record_sequence >= m3p->config.record_sequence_limit) {
            memcpy(m3p->block_next_page, saved_next, sizeof(saved_next));
            memcpy(m3p->p2l, saved_p2l, sizeof(saved_p2l));
            m3p->record_sequence = saved_record_sequence;
            operation->delta_count = 0;
            return 0;
        }
        memset(delta, 0, sizeof(*delta));
        delta->lpn = lpn;
        delta->prior = m3p->visible[lpn];
        delta->target.state = M3P_L2P_VALUE;
        delta->target.valid_mask =
            (uint8_t)((delta->prior.state == M3P_L2P_VALUE ?
                           delta->prior.valid_mask : 0) | written);
        delta->target.block = (uint8_t)ppa.block;
        delta->target.page = (uint8_t)ppa.page;
        delta->target.erase_generation =
            m3p->block_erase_generation[ppa.block];
        delta->target.data_record_sequence = ++m3p->record_sequence;
        delta->target.logical_version = delta->prior.logical_version + 1u;
        ++operation->delta_count;
    }
    return 1;
}

static void prepare_trim(struct fwlab_m3p *m3p,
                         struct m3p_operation *operation)
{
    uint16_t lpn;

    operation->delta_count = 0;
    for (lpn = operation->first_lpn; lpn <= operation->last_lpn; ++lpn) {
        struct m3p_delta *delta =
            &operation->delta[operation->delta_count++];
        uint8_t removed = sector_mask(operation->request.lba,
                                      operation->request.lba_count, lpn);

        memset(delta, 0, sizeof(*delta));
        delta->lpn = lpn;
        delta->prior = m3p->visible[lpn];
        delta->target = delta->prior;
        delta->target.logical_version = delta->prior.logical_version + 1u;
        delta->target.valid_mask = (uint8_t)(delta->target.valid_mask &
                                             (uint8_t)~removed);
        if (delta->target.state != M3P_L2P_VALUE ||
            delta->target.valid_mask == 0) {
            memset(&delta->target, 0, sizeof(delta->target));
            delta->target.state = M3P_L2P_TOMBSTONE;
            delta->target.block = UINT8_MAX;
            delta->target.page = UINT8_MAX;
            delta->target.logical_version =
                delta->prior.logical_version + 1u;
        }
    }
}

static enum fwlab_spine_result_v0 block_submit(
    void *opaque, const struct fwlab_block_request_v0 *request,
    struct fwlab_block_submit_result_v0 *result)
{
    struct fwlab_m3p *m3p = opaque;
    struct m3p_operation candidate;
    uint16_t first_lpn;
    uint16_t last_lpn;
    uint32_t child_bound;

    if (result == NULL || request == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (!m3p_live(m3p) || !request_valid(m3p, request)) {
        submit_result_init(result, &request->operation_token,
                           FWLAB_HOST_ACTION_V0_REJECTED, 1);
        return FWLAB_SPINE_V0_OK;
    }
    if (m3p->admission_closed || !m3p->ready ||
        m3p->work_kind != FWLAB_M3P_MAINTENANCE_NONE ||
        m3p->operation.state != M3P_OPERATION_FREE) {
        submit_result_init(result, &request->operation_token,
                           FWLAB_HOST_ACTION_V0_BACKPRESSURE, 0);
        return FWLAB_SPINE_V0_OK;
    }
    first_lpn = request->operation == FWLAB_BLOCK_V0_FLUSH ? 0 :
        (uint16_t)(request->lba / M3P_SECTORS_PER_PAGE);
    last_lpn = request->operation == FWLAB_BLOCK_V0_FLUSH ? 0 :
        (uint16_t)((request->lba + request->lba_count - 1u) /
                   M3P_SECTORS_PER_PAGE);
    if ((request->operation == FWLAB_BLOCK_V0_WRITE ||
         request->operation == FWLAB_BLOCK_V0_TRIM) &&
        m3p_pending_overlaps(m3p, first_lpn, last_lpn)) {
        submit_result_init(result, &request->operation_token,
                           FWLAB_HOST_ACTION_V0_BACKPRESSURE, 0);
        return FWLAB_SPINE_V0_OK;
    }
    if ((request->operation == FWLAB_BLOCK_V0_TRIM ||
         (request->operation == FWLAB_BLOCK_V0_WRITE &&
          request->durability ==
              FWLAB_BLOCK_V0_DURABILITY_VOLATILE_ALLOWED)) &&
        m3p->pending_count >= M3P_MAX_PENDING) {
        submit_result_init(result, &request->operation_token,
                           FWLAB_HOST_ACTION_V0_BACKPRESSURE, 0);
        return FWLAB_SPINE_V0_OK;
    }
    child_bound = request_child_bound(m3p, request);
    if (child_bound == 0 ||
        !child_credit_available(m3p, child_bound +
            (request->operation == FWLAB_BLOCK_V0_FLUSH ? 0u : 32u)) ||
        m3p->record_sequence >= m3p->config.record_sequence_limit ||
        ((request->operation == FWLAB_BLOCK_V0_WRITE ||
          request->operation == FWLAB_BLOCK_V0_TRIM) &&
         m3p->host_sequence >= m3p->config.host_sequence_limit)) {
        submit_resource_failure(result, &request->operation_token, 3);
        return FWLAB_SPINE_V0_OK;
    }
    if (m3p->journal_page + m3p->pending_count + 1u > M3P_PAGES_PER_BLOCK) {
        submit_resource_failure(result, &request->operation_token, 4);
        return FWLAB_SPINE_V0_OK;
    }
    memset(&candidate, 0, sizeof(candidate));
    candidate.state = M3P_OPERATION_PREPARED;
    candidate.request = *request;
    candidate.first_lpn = first_lpn;
    candidate.last_lpn = last_lpn;
    candidate.current_lpn = first_lpn;
    candidate.captured_frontier = m3p->host_sequence;
    memset(&candidate.status, 0, sizeof(candidate.status));
    candidate.status.version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    candidate.status.size = (uint16_t)sizeof(candidate.status);
    candidate.status.operation_token = request->operation_token;
    candidate.status.state = FWLAB_BLOCK_V0_STATE_ACCEPTED;
    if (request->operation == FWLAB_BLOCK_V0_WRITE) {
        if (m3p->controller_buffer.ops->read(
                m3p->controller_buffer.context, &request->buffer,
                &request->buffer_span, candidate.io_bytes,
                request->buffer_span.length) !=
                FWLAB_CONTROLLER_BUFFER_V0_OK) {
            submit_resource_failure(result, &request->operation_token, 5);
            return FWLAB_SPINE_V0_OK;
        }
        if (!reserve_write_pages(m3p, &candidate)) {
            /* Reservation rolls back fully. Private maintenance is the actual
             * progress owner while this Host operation remains unaccepted. */
            if (fwlab_m3p_force_gc_start(m3p) == FWLAB_SPINE_V0_OK) {
                submit_result_init(result, &request->operation_token,
                                   FWLAB_HOST_ACTION_V0_BACKPRESSURE, 0);
            } else {
                submit_resource_failure(result, &request->operation_token, 6);
            }
            return FWLAB_SPINE_V0_OK;
        }
        candidate.host_sequence = ++m3p->host_sequence;
        candidate.captured_frontier = candidate.host_sequence;
        candidate.state = M3P_OPERATION_PAGE_PREPARE;
    } else if (request->operation == FWLAB_BLOCK_V0_READ) {
        memset(candidate.io_bytes, 0, sizeof(candidate.io_bytes));
        candidate.state = M3P_OPERATION_READ_PAGE;
    } else if (request->operation == FWLAB_BLOCK_V0_TRIM) {
        candidate.host_sequence = ++m3p->host_sequence;
        candidate.captured_frontier = candidate.host_sequence;
        prepare_trim(m3p, &candidate);
        candidate.state = M3P_OPERATION_PENDING_INSTALL;
    } else {
        candidate.state = M3P_OPERATION_COMMIT_PENDING;
    }
    m3p->operation = candidate;
    submit_result_init(result, &request->operation_token,
                       FWLAB_HOST_ACTION_V0_ACCEPTED, 0);
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 block_query(
    void *opaque, const struct fwlab_block_op_token_v0 *operation,
    struct fwlab_block_status_v0 *status)
{
    struct fwlab_m3p *m3p = opaque;

    if (!m3p_live(m3p) || operation == NULL || status == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (m3p->operation.state == M3P_OPERATION_FREE ||
        !m3p_token_equal(operation,
                         &m3p->operation.request.operation_token)) {
        return FWLAB_SPINE_V0_STALE;
    }
    *status = m3p->operation.status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 block_cancel(
    void *opaque, const struct fwlab_block_op_token_v0 *operation)
{
    struct fwlab_m3p *m3p = opaque;

    if (!m3p_live(m3p) || operation == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (m3p->operation.state == M3P_OPERATION_FREE ||
        !m3p_token_equal(operation,
                         &m3p->operation.request.operation_token)) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (m3p->operation.state >= M3P_OPERATION_TERMINAL) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    m3p->operation.cancel_requested = 1;
    ++m3p->cancel_count;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 block_retire_start(
    void *opaque, const struct fwlab_block_op_token_v0 *operation)
{
    struct fwlab_m3p *m3p = opaque;

    if (!m3p_live(m3p) || operation == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (m3p->operation.state == M3P_OPERATION_FREE ||
        !m3p_token_equal(operation,
                         &m3p->operation.request.operation_token)) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (m3p->operation.state == M3P_OPERATION_TERMINAL ||
        m3p->operation.state == M3P_OPERATION_FAILED) {
        m3p->operation.state = M3P_OPERATION_DRAINING;
        m3p->operation.status.state = FWLAB_BLOCK_V0_STATE_DRAINING;
        return FWLAB_SPINE_V0_OK;
    }
    return m3p->operation.state >= M3P_OPERATION_DRAINING ?
        FWLAB_SPINE_V0_OK : FWLAB_SPINE_V0_WRONG_STATE;
}

static enum fwlab_spine_result_v0 block_retire_query(
    void *opaque, const struct fwlab_block_op_token_v0 *operation,
    struct fwlab_block_status_v0 *status)
{
    struct fwlab_m3p *m3p = opaque;

    if (!m3p_live(m3p) || operation == NULL || status == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (m3p->operation.state == M3P_OPERATION_FREE ||
        !m3p_token_equal(operation,
                         &m3p->operation.request.operation_token)) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (m3p->operation.state == M3P_OPERATION_DRAINING) {
        *status = m3p->operation.status;
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    if (m3p->operation.state != M3P_OPERATION_DRAINED) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    m3p->operation.status.state = FWLAB_BLOCK_V0_STATE_RETIRED;
    *status = m3p->operation.status;
    memset(&m3p->operation, 0, sizeof(m3p->operation));
    return FWLAB_SPINE_V0_OK;
}

static void discard_pending(struct fwlab_m3p *m3p)
{
    while (m3p->pending_count != 0) {
        struct m3p_pending *pending = m3p_pending_front(m3p);
        uint8_t index;

        for (index = 0; index < pending->delta_count; ++index) {
            const struct m3p_delta *delta = &pending->delta[index];

            if (delta->target.state == M3P_L2P_VALUE) {
                m3p->p2l[m3p_physical_index(delta->target.block,
                                            delta->target.page)] =
                    M3P_P2L_ORPHAN;
            }
            if (delta->prior.state == M3P_L2P_VALUE) {
                m3p->p2l[m3p_physical_index(delta->prior.block,
                                            delta->prior.page)] =
                    M3P_P2L_LIVE;
            }
            m3p->visible[delta->lpn] = m3p->durable[delta->lpn];
        }
        m3p_pending_pop(m3p);
    }
}

static enum fwlab_spine_result_v0 block_epoch_close(
    void *opaque, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch)
{
    struct fwlab_m3p *m3p = opaque;

    if (!m3p_live(m3p) || lifecycle_instance_nonce == 0 ||
        old_execution_epoch == 0) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (m3p->admission_closed) {
        return m3p->close_lifecycle_nonce == lifecycle_instance_nonce &&
                       m3p->close_execution_epoch == old_execution_epoch ?
                   FWLAB_SPINE_V0_OK : FWLAB_SPINE_V0_STALE;
    }
    m3p->admission_closed = 1;
    m3p->close_lifecycle_nonce = lifecycle_instance_nonce;
    m3p->close_execution_epoch = old_execution_epoch;
    if (m3p->operation.state != M3P_OPERATION_FREE &&
        m3p->operation.state < M3P_OPERATION_TERMINAL) {
        m3p->operation.cancel_requested = 1;
    }
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 block_epoch_quiescent(
    void *opaque, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch, struct fwlab_block_epoch_status_v0 *status)
{
    struct fwlab_m3p *m3p = opaque;
    uint8_t quiescent;

    if (!m3p_live(m3p) || status == NULL || !m3p->admission_closed ||
        lifecycle_instance_nonce != m3p->close_lifecycle_nonce ||
        old_execution_epoch != m3p->close_execution_epoch) {
        return FWLAB_SPINE_V0_INVALID;
    }
    quiescent = m3p->operation.state == M3P_OPERATION_FREE &&
                m3p->pending_count == 0 && m3p->child.state == M3P_CHILD_IDLE &&
                m3p->nfc_quiescent;
    memset(status, 0, sizeof(*status));
    status->version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    status->size = (uint16_t)sizeof(*status);
    status->lifecycle_instance_nonce = lifecycle_instance_nonce;
    status->execution_epoch = old_execution_epoch;
    status->aggregate_operations =
        m3p->operation.state == M3P_OPERATION_FREE ? 0 : 1;
    status->admission_closed = 1;
    status->quiescent = quiescent;
    if (quiescent) {
        status->aggregate_proof[0] = m3p->config.provider_nonce;
        status->aggregate_proof[1] =
            ((uint64_t)m3p->config.generation << 32) | old_execution_epoch;
    }
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_block_service_ops_v0 block_ops = {
    .version = FWLAB_BLOCK_SERVICE_V0_VERSION,
    .size = sizeof(struct fwlab_block_service_ops_v0),
    .reserved0 = 0,
    .submit = block_submit,
    .query = block_query,
    .cancel = block_cancel,
    .retire_start = block_retire_start,
    .retire_query = block_retire_query,
    .epoch_close = block_epoch_close,
    .epoch_quiescent = block_epoch_quiescent,
    .reserved1 = {0, 0, 0, 0},
};

int fwlab_m3p_config_valid(const struct fwlab_m3p_config *config)
{
    return config != NULL && config->version == FWLAB_M3P_VERSION &&
           config->size == sizeof(*config) && config->reserved0 == 0 &&
           !m3p_bytes_zero(config->media_uuid, 16) &&
           (config->namespace_ref.word[0] != 0 ||
            config->namespace_ref.word[1] != 0) &&
           config->instance_nonce != 0 && config->provider_nonce != 0 &&
           config->nfc_instance_nonce != 0 &&
           config->instance_nonce != config->provider_nonce &&
           config->instance_nonce != config->nfc_instance_nonce &&
           config->provider_nonce != config->nfc_instance_nonce &&
           config->next_nfc_operation_uid != 0 && config->generation != 0 &&
           config->execution_epoch != 0 && config->nfc_epoch != 0 &&
           ((config->nfc_operation_uid_limit == 2048 &&
             config->host_sequence_limit == 512 &&
             config->record_sequence_limit == 2048) ||
            (config->nfc_operation_uid_limit == 65536 &&
             config->host_sequence_limit == 4096 &&
             config->record_sequence_limit == 65536)) &&
           config->next_nfc_operation_uid <=
               config->nfc_operation_uid_limit &&
           m3p_bytes_zero(config->reserved1, sizeof(config->reserved1));
}

size_t fwlab_m3p_arena_alignment(void)
{
    return alignof(max_align_t);
}

size_t fwlab_m3p_arena_size(const struct fwlab_m3p_config *config)
{
    size_t alignment = alignof(max_align_t);

    if (!fwlab_m3p_config_valid(config)) {
        return 0;
    }
    return (sizeof(struct fwlab_m3p) + alignment - 1u) & ~(alignment - 1u);
}

static int controller_buffer_port_valid(
    const struct fwlab_controller_buffer_port_v0 *port)
{
    return port != NULL && port->ops != NULL &&
           port->ops->version == FWLAB_CONTROLLER_BUFFER_V0_VERSION &&
           port->ops->size == sizeof(*port->ops) &&
           port->ops->reserved0 == 0 && port->ops->acquire != NULL &&
           port->ops->read != NULL && port->ops->write != NULL &&
           port->ops->copy != NULL && port->ops->release != NULL &&
           port->ops->epoch_close != NULL &&
           port->ops->epoch_quiescent != NULL &&
           m3p_bytes_zero(port->ops->reserved1,
                          sizeof(port->ops->reserved1)) &&
           port->context != NULL && port->issuer_nonce != 0 &&
           port->generation != 0 &&
           m3p_bytes_zero(port->reserved, sizeof(port->reserved));
}

static int nfc_provider_valid(const struct fwlab_nfc_provider *provider)
{
    return provider != NULL && provider->ops != NULL &&
           provider->ops->version == FWLAB_NFC_CONTRACT_VERSION &&
           provider->ops->size == sizeof(*provider->ops) &&
           provider->ops->reserved == 0 && provider->ops->try_submit != NULL &&
           provider->ops->cancel != NULL && provider->ops->step != NULL &&
           provider->ops->poll != NULL && provider->ops->reset_begin != NULL &&
           provider->ops->quiescent != NULL && provider->context != NULL;
}

enum fwlab_spine_result_v0 fwlab_m3p_init(
    void *arena, size_t arena_size, const struct fwlab_m3p_config *config,
    const struct fwlab_controller_buffer_port_v0 *controller_buffer,
    const struct fwlab_nfc_provider *nfc, struct fwlab_m3p **m3p_out)
{
    struct fwlab_m3p *m3p;

    if (arena == NULL || !fwlab_m3p_config_valid(config) ||
        arena_size < fwlab_m3p_arena_size(config) ||
        (uintptr_t)arena % fwlab_m3p_arena_alignment() != 0 ||
        !controller_buffer_port_valid(controller_buffer) ||
        !nfc_provider_valid(nfc) || m3p_out == NULL ||
        config->provider_nonce == controller_buffer->issuer_nonce) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(arena, 0, fwlab_m3p_arena_size(config));
    m3p = arena;
    m3p->magic = M3P_MAGIC;
    m3p->config = *config;
    m3p->controller_buffer = *controller_buffer;
    m3p->nfc = *nfc;
    m3p->service.ops = &block_ops;
    m3p->service.context = m3p;
    m3p->service.provider_nonce = config->provider_nonce;
    m3p->service.generation = config->generation;
    m3p->next_child_uid = config->next_nfc_operation_uid;
    m3p_mapping_reset(m3p);
    m3p->initialized = 1;
    *m3p_out = m3p;
    return FWLAB_SPINE_V0_OK;
}

struct fwlab_block_service_v0 fwlab_m3p_block_service(struct fwlab_m3p *m3p)
{
    struct fwlab_block_service_v0 empty;

    memset(&empty, 0, sizeof(empty));
    return m3p_live(m3p) ? m3p->service : empty;
}

static void prepare_metadata_oob(struct fwlab_m3p *m3p, uint8_t page_type,
                                 uint32_t record_sequence,
                                 uint32_t transaction_sequence,
                                 uint32_t predecessor,
                                 uint32_t resulting,
                                 uint32_t checkpoint_generation,
                                 uint32_t checkpoint_covered,
                                 uint32_t frontier)
{
    struct m3p_oob oob;

    memset(&oob, 0, sizeof(oob));
    oob.page_type = page_type;
    oob.flags = 1;
    oob.namespace_id = UINT32_MAX;
    oob.lpn = UINT32_MAX;
    oob.erase_generation =
        m3p->block_erase_generation[m3p->child.ppa.block];
    oob.record_sequence = record_sequence;
    oob.transaction_sequence = transaction_sequence;
    oob.predecessor_map_sequence = predecessor;
    oob.resulting_map_sequence = resulting;
    oob.main_length = M3P_PAGE_BYTES;
    oob.main_crc = m3p_crc32c(m3p->frame_main[0], M3P_PAGE_BYTES);
    oob.checkpoint_generation = checkpoint_generation;
    oob.checkpoint_covered_sequence = checkpoint_covered;
    oob.durable_frontier = frontier;
    memcpy(oob.media_uuid, m3p->config.media_uuid, 16);
    oob.source_block = UINT8_MAX;
    oob.source_page = UINT8_MAX;
    m3p_encode_oob(m3p->frame_oob[0], &oob);
}

static int start_format_body(struct fwlab_m3p *m3p)
{
    struct fwlab_nfc_ppa ppa = {0, 0, 0, 12, 0, 0};

    if (m3p->record_sequence >= m3p->config.record_sequence_limit) {
        return 0;
    }
    m3p_encode_checkpoint_body(m3p->frame_main[0], m3p->durable);
    m3p->child.ppa = ppa;
    prepare_metadata_oob(m3p, M3P_PAGE_CHECKPOINT_BODY,
                         ++m3p->record_sequence, 0, 0, 0, 1, 0, 0);
    m3p->checkpoint_body_main_crc =
        m3p_crc32c(m3p->frame_main[0], M3P_PAGE_BYTES);
    m3p->checkpoint_body_oob_crc =
        m3p_crc32c(m3p->frame_oob[0], M3P_OOB_BYTES);
    return m3p_child_program_start(m3p, 0, ppa) == FWLAB_SPINE_V0_OK;
}

static int start_format_commit(struct fwlab_m3p *m3p)
{
    struct m3p_checkpoint_commit commit;
    struct fwlab_nfc_ppa ppa = {0, 0, 0, 12, 1, 0};

    if (m3p->record_sequence >= m3p->config.record_sequence_limit) {
        return 0;
    }
    memset(&commit, 0, sizeof(commit));
    commit.generation = 1;
    commit.body_block = 12;
    commit.body_page = 0;
    commit.body_main_crc = m3p->checkpoint_body_main_crc;
    commit.body_oob_crc = m3p->checkpoint_body_oob_crc;
    commit.journal_generation = 1;
    commit.commit_record_sequence = m3p->record_sequence + 1u;
    memcpy(commit.media_uuid, m3p->config.media_uuid, 16);
    m3p_encode_checkpoint_commit(m3p->frame_main[0], &commit);
    m3p->child.ppa = ppa;
    prepare_metadata_oob(m3p, M3P_PAGE_CHECKPOINT_COMMIT,
                         ++m3p->record_sequence, 0, 0, 0, 1, 0, 0);
    return m3p_child_program_start(m3p, 0, ppa) == FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_m3p_format_start(struct fwlab_m3p *m3p)
{
    if (!m3p_live(m3p) || m3p->ready || m3p->admission_closed ||
        m3p->work_kind != FWLAB_M3P_MAINTENANCE_NONE ||
        m3p->operation.state != M3P_OPERATION_FREE ||
        !child_credit_available(m3p, 4)) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    m3p_mapping_reset(m3p);
    m3p->work_kind = FWLAB_M3P_MAINTENANCE_FORMAT;
    m3p->work_state = M3P_FORMAT_BODY;
    return FWLAB_SPINE_V0_OK;
}

static int format_drive(struct fwlab_m3p *m3p)
{
    if (m3p->child.state == M3P_CHILD_FAILED) {
        m3p->work_state = M3P_WORK_FAILED;
        m3p->quarantined = 1;
        return 1;
    }
    if (m3p->work_state == M3P_FORMAT_BODY) {
        if (m3p->child.state == M3P_CHILD_IDLE) {
            return start_format_body(m3p);
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            m3p_child_consume(m3p);
            m3p->block_next_page[12] = 1;
            m3p->work_state = M3P_FORMAT_COMMIT;
            return 1;
        }
    } else if (m3p->work_state == M3P_FORMAT_COMMIT) {
        if (m3p->child.state == M3P_CHILD_IDLE) {
            return start_format_commit(m3p);
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            m3p_child_consume(m3p);
            m3p->block_next_page[12] = 2;
            m3p->checkpoint_page = 2;
            m3p->checkpoint_generation = 1;
            m3p->checkpoint_covered_sequence = 0;
            m3p->ready = 1;
            m3p->work_state = M3P_FORMAT_DONE;
            m3p->work_kind = FWLAB_M3P_MAINTENANCE_NONE;
            return 1;
        }
    }
    return 0;
}

static int frame_matches_entry(struct fwlab_m3p *m3p, uint8_t frame,
                               uint16_t lpn,
                               const struct m3p_map_entry *entry)
{
    struct m3p_oob oob;
    int decoded = m3p_decode_oob(m3p->frame_oob[frame], &oob);

    return decoded &&
           oob.page_type == M3P_PAGE_DATA && oob.namespace_id == 1 &&
           oob.lpn == lpn &&
           (oob.valid_mask & entry->valid_mask) == entry->valid_mask &&
           oob.erase_generation == entry->erase_generation &&
           oob.record_sequence == entry->data_record_sequence &&
           oob.main_crc == entry->main_crc &&
           memcmp(oob.media_uuid, m3p->config.media_uuid, 16) == 0 &&
           m3p_crc32c(m3p->frame_main[frame], M3P_PAGE_BYTES) ==
               oob.main_crc;
}

static void overlay_request_page(struct fwlab_m3p *m3p,
                                 struct m3p_operation *operation,
                                 uint16_t lpn)
{
    uint64_t request_first = operation->request.lba;
    uint64_t request_last = request_first + operation->request.lba_count;
    uint64_t page_first = (uint64_t)lpn * M3P_SECTORS_PER_PAGE;
    uint64_t page_last = page_first + M3P_SECTORS_PER_PAGE;
    uint64_t first = request_first > page_first ? request_first : page_first;
    uint64_t last = request_last < page_last ? request_last : page_last;
    uint64_t sector;

    for (sector = first; sector < last; ++sector) {
        size_t source = (size_t)(sector - request_first) * FWLAB_M3P_LBA_BYTES;
        size_t destination =
            (size_t)(sector - page_first) * FWLAB_M3P_LBA_BYTES;

        memcpy(&m3p->frame_main[0][destination],
               &operation->io_bytes[source], FWLAB_M3P_LBA_BYTES);
    }
}

static void zero_invalid_sectors(uint8_t page[4096], uint8_t valid_mask)
{
    uint8_t sector;

    for (sector = 0; sector < M3P_SECTORS_PER_PAGE; ++sector) {
        if ((valid_mask & (uint8_t)(1u << sector)) == 0) {
            memset(&page[(size_t)sector * FWLAB_M3P_LBA_BYTES], 0,
                   FWLAB_M3P_LBA_BYTES);
        }
    }
}

static int start_current_data_program(struct fwlab_m3p *m3p)
{
    struct m3p_operation *operation = &m3p->operation;
    uint8_t index = (uint8_t)(operation->current_lpn - operation->first_lpn);
    struct m3p_delta *delta = &operation->delta[index];
    struct m3p_oob oob;
    struct fwlab_nfc_ppa ppa = {0, 0, 0, delta->target.block,
                                delta->target.page, 0};

    overlay_request_page(m3p, operation, operation->current_lpn);
    delta->target.main_crc = m3p_crc32c(m3p->frame_main[0], M3P_PAGE_BYTES);
    memset(&oob, 0, sizeof(oob));
    oob.page_type = M3P_PAGE_DATA;
    oob.flags = 1;
    oob.copy_kind = M3P_COPY_HOST;
    oob.valid_mask = delta->target.valid_mask;
    oob.namespace_id = 1;
    oob.lpn = operation->current_lpn;
    oob.erase_generation = delta->target.erase_generation;
    oob.record_sequence = delta->target.data_record_sequence;
    oob.transaction_sequence = operation->host_sequence;
    oob.referenced_data_sequence = delta->target.data_record_sequence;
    oob.main_length = M3P_PAGE_BYTES;
    oob.main_crc = delta->target.main_crc;
    memcpy(oob.media_uuid, m3p->config.media_uuid, 16);
    oob.source_block = UINT8_MAX;
    oob.source_page = UINT8_MAX;
    m3p_encode_oob(m3p->frame_oob[0], &oob);
    return m3p_child_program_start(m3p, 0, ppa) == FWLAB_SPINE_V0_OK;
}

void m3p_operation_fail(struct fwlab_m3p *m3p, uint32_t fault_code,
                        int effect_seen)
{
    struct m3p_operation *operation = &m3p->operation;
    uint8_t index;

    if (m3p->child.state == M3P_CHILD_FAILED ||
        m3p->child.state == M3P_CHILD_DONE) {
        m3p_child_consume(m3p);
    }
    if (operation->request.operation == FWLAB_BLOCK_V0_WRITE &&
        !operation->commit_slot) {
        for (index = 0; index < operation->delta_count; ++index) {
            const struct m3p_delta *delta = &operation->delta[index];

            if (delta->target.state == M3P_L2P_VALUE) {
                m3p->p2l[m3p_physical_index(delta->target.block,
                                            delta->target.page)] =
                    M3P_P2L_ORPHAN;
            }
        }
    }

    operation->fault_code = fault_code == 0 ? 1 : fault_code;
    operation->state = M3P_OPERATION_FAILED;
    operation->status.state = FWLAB_BLOCK_V0_STATE_TERMINAL;
    operation->status.outcome = operation->cancel_requested ?
        FWLAB_BLOCK_V0_CANCELLED : FWLAB_BLOCK_V0_FAILED;
    operation->status.effect = effect_seen ?
        FWLAB_BLOCK_V0_EFFECT_UNKNOWN_PREFIX : FWLAB_BLOCK_V0_EFFECT_NONE;
    operation->status.fault_domain = 1;
    operation->status.fault_code = operation->fault_code;
    operation->status.completed_lbas = 0;
    operation->status.data_bytes = 0;
    operation->status.durability_witness = FWLAB_BLOCK_V0_WITNESS_NONE;
    memset(&operation->status.frontier, 0,
           sizeof(operation->status.frontier));
}

void m3p_operation_succeed(struct fwlab_m3p *m3p,
                           uint32_t durability_witness,
                           uint32_t frontier_sequence)
{
    struct m3p_operation *operation = &m3p->operation;

    operation->state = M3P_OPERATION_TERMINAL;
    operation->status.state = FWLAB_BLOCK_V0_STATE_TERMINAL;
    operation->status.outcome = FWLAB_BLOCK_V0_SUCCEEDED;
    operation->status.effect = FWLAB_BLOCK_V0_EFFECT_FULL;
    operation->status.completed_lbas = operation->request.lba_count;
    operation->status.data_bytes =
        operation->request.operation == FWLAB_BLOCK_V0_READ ||
                operation->request.operation == FWLAB_BLOCK_V0_WRITE ?
            operation->request.buffer_span.length : 0;
    operation->status.durability_witness = durability_witness;
    /* A durable frontier at sequence zero is valid for an empty namespace. */
    if (durability_witness == FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE ||
        durability_witness == FWLAB_BLOCK_V0_WITNESS_FRONTIER_DURABLE) {
        operation->status.frontier.word[0] = m3p->config.provider_nonce;
        operation->status.frontier.word[1] = frontier_sequence;
    }
}

static int prepare_map_program(struct fwlab_m3p *m3p,
                               const struct m3p_pending *pending)
{
    struct m3p_map_record record;
    struct m3p_oob oob;
    struct fwlab_nfc_ppa ppa = {0, 0, 0, m3p->active_journal_block,
                                m3p->journal_page, 0};
    uint32_t next_record = m3p->record_sequence + 1u;
    uint32_t next_map = m3p->map_sequence + 1u;

    if (m3p->journal_page >= M3P_PAGES_PER_BLOCK ||
        next_record == 0 || next_record > m3p->config.record_sequence_limit ||
        next_map == 0 || next_map > m3p->config.record_sequence_limit) {
        return 0;
    }
    memset(&record, 0, sizeof(record));
    record.subtype = pending->subtype;
    record.delta_count = pending->delta_count;
    record.flags = 1;
    record.transaction_sequence = pending->transaction_sequence;
    record.predecessor_map_sequence = m3p->map_sequence;
    record.resulting_map_sequence = next_map;
    record.host_sequence = pending->host_sequence;
    record.captured_frontier = pending->host_sequence;
    record.gc_source_block = UINT8_MAX;
    record.gc_destination_block = UINT8_MAX;
    memcpy(record.delta, pending->delta,
           (size_t)pending->delta_count * sizeof(record.delta[0]));
    m3p_encode_map(m3p->frame_main[0], &record);
    memset(&oob, 0, sizeof(oob));
    oob.page_type = M3P_PAGE_MAP_TXN;
    oob.flags = 1;
    oob.copy_kind = M3P_COPY_HOST;
    oob.namespace_id = 1;
    oob.lpn = UINT32_MAX;
    oob.erase_generation = m3p->block_erase_generation[ppa.block];
    oob.record_sequence = next_record;
    oob.transaction_sequence = pending->transaction_sequence;
    oob.predecessor_map_sequence = m3p->map_sequence;
    oob.resulting_map_sequence = next_map;
    oob.main_length = M3P_PAGE_BYTES;
    oob.main_crc = m3p_crc32c(m3p->frame_main[0], M3P_PAGE_BYTES);
    oob.durable_frontier = pending->host_sequence;
    memcpy(oob.media_uuid, m3p->config.media_uuid, 16);
    oob.source_block = UINT8_MAX;
    oob.source_page = UINT8_MAX;
    m3p_encode_oob(m3p->frame_oob[0], &oob);
    return m3p_child_program_start(m3p, 0, ppa) == FWLAB_SPINE_V0_OK;
}

static int checkpoint_prepare_body(struct fwlab_m3p *m3p)
{
    struct m3p_oob oob;
    struct fwlab_nfc_ppa ppa = {0, 0, 0, m3p->checkpoint_target_block,
                                m3p->checkpoint_target_page, 0};
    uint32_t record = m3p->record_sequence + 1u;
    uint32_t generation = m3p->checkpoint_generation + 1u;

    if (record == 0 || record > m3p->config.record_sequence_limit ||
        generation == 0) {
        return 0;
    }
    m3p_encode_checkpoint_body(m3p->frame_main[0], m3p->durable);
    memset(&oob, 0, sizeof(oob));
    oob.page_type = M3P_PAGE_CHECKPOINT_BODY;
    oob.flags = 1;
    oob.namespace_id = UINT32_MAX;
    oob.lpn = UINT32_MAX;
    oob.erase_generation = m3p->block_erase_generation[ppa.block];
    oob.record_sequence = record;
    oob.main_length = M3P_PAGE_BYTES;
    oob.main_crc = m3p_crc32c(m3p->frame_main[0], M3P_PAGE_BYTES);
    oob.checkpoint_generation = generation;
    oob.checkpoint_covered_sequence = m3p->map_sequence;
    oob.durable_frontier = m3p->durable_frontier;
    memcpy(oob.media_uuid, m3p->config.media_uuid, 16);
    oob.source_block = UINT8_MAX;
    oob.source_page = UINT8_MAX;
    m3p_encode_oob(m3p->frame_oob[0], &oob);
    m3p->checkpoint_body_main_crc = oob.main_crc;
    m3p->checkpoint_body_oob_crc =
        m3p_crc32c(m3p->frame_oob[0], M3P_OOB_BYTES);
    return m3p_child_program_start(m3p, 0, ppa) == FWLAB_SPINE_V0_OK;
}

static int checkpoint_prepare_commit(struct fwlab_m3p *m3p)
{
    struct m3p_checkpoint_commit commit;
    struct m3p_oob oob;
    struct fwlab_nfc_ppa ppa = {0, 0, 0, m3p->checkpoint_target_block,
                                (uint16_t)(m3p->checkpoint_target_page + 1u),
                                0};
    uint32_t record = m3p->record_sequence + 1u;
    uint32_t generation = m3p->checkpoint_generation + 1u;

    if (record == 0 || record > m3p->config.record_sequence_limit ||
        m3p->journal_generation == UINT32_MAX) {
        return 0;
    }
    memset(&commit, 0, sizeof(commit));
    commit.generation = generation;
    commit.body_block = m3p->checkpoint_target_block;
    commit.body_page = m3p->checkpoint_target_page;
    commit.body_erase_generation =
        m3p->block_erase_generation[m3p->checkpoint_target_block];
    commit.body_main_crc = m3p->checkpoint_body_main_crc;
    commit.body_oob_crc = m3p->checkpoint_body_oob_crc;
    commit.covered_map_sequence = m3p->map_sequence;
    commit.durable_frontier = m3p->durable_frontier;
    commit.journal_generation = m3p->journal_generation + 1u;
    commit.commit_record_sequence = record;
    memcpy(commit.media_uuid, m3p->config.media_uuid, 16);
    m3p_encode_checkpoint_commit(m3p->frame_main[0], &commit);
    memset(&oob, 0, sizeof(oob));
    oob.page_type = M3P_PAGE_CHECKPOINT_COMMIT;
    oob.flags = 1;
    oob.namespace_id = UINT32_MAX;
    oob.lpn = UINT32_MAX;
    oob.erase_generation = commit.body_erase_generation;
    oob.record_sequence = record;
    oob.main_length = M3P_PAGE_BYTES;
    oob.main_crc = m3p_crc32c(m3p->frame_main[0], M3P_PAGE_BYTES);
    oob.checkpoint_generation = generation;
    oob.checkpoint_covered_sequence = m3p->map_sequence;
    oob.durable_frontier = m3p->durable_frontier;
    memcpy(oob.media_uuid, m3p->config.media_uuid, 16);
    oob.source_block = UINT8_MAX;
    oob.source_page = UINT8_MAX;
    m3p_encode_oob(m3p->frame_oob[0], &oob);
    return m3p_child_program_start(m3p, 0, ppa) == FWLAB_SPINE_V0_OK;
}

int m3p_start_checkpoint(struct fwlab_m3p *m3p, uint8_t return_state)
{
    if (m3p->pending_count != 0 || m3p->checkpoint_flow != 0) {
        return 0;
    }
    m3p->checkpoint_flow = 1;
    m3p->checkpoint_return_state = return_state;
    m3p->checkpoint_rotating = 0;
    m3p->checkpoint_old_block = m3p->active_checkpoint_block;
    if (m3p->checkpoint_page + 2u <= M3P_PAGES_PER_BLOCK) {
        m3p->checkpoint_target_block = m3p->active_checkpoint_block;
        m3p->checkpoint_target_page = m3p->checkpoint_page;
    } else {
        if (m3p->block_next_page[m3p->inactive_checkpoint_block] != 0) {
            m3p->checkpoint_flow = 0;
            return 0;
        }
        m3p->checkpoint_rotating = 1;
        m3p->checkpoint_target_block = m3p->inactive_checkpoint_block;
        m3p->checkpoint_target_page = 0;
    }
    m3p->checkpoint_old_journal = m3p->active_journal_block;
    m3p->operation.state = M3P_OPERATION_CHECKPOINT;
    return 1;
}

int m3p_checkpoint_drive(struct fwlab_m3p *m3p)
{
    if (m3p->child.state == M3P_CHILD_FAILED) {
        m3p_operation_fail(m3p, m3p->child.fault_code, 1);
        m3p->quarantined = 1;
        return 1;
    }
    if (m3p->checkpoint_flow == 1) {
        if (m3p->child.state == M3P_CHILD_IDLE) {
            return checkpoint_prepare_body(m3p);
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            m3p_child_consume(m3p);
            ++m3p->record_sequence;
            m3p->checkpoint_flow = 2;
            return 1;
        }
    } else if (m3p->checkpoint_flow == 2) {
        if (m3p->child.state == M3P_CHILD_IDLE) {
            return checkpoint_prepare_commit(m3p);
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            m3p_child_consume(m3p);
            ++m3p->record_sequence;
            ++m3p->checkpoint_generation;
            m3p->checkpoint_covered_sequence = m3p->map_sequence;
            if (m3p->checkpoint_rotating) {
                m3p->inactive_checkpoint_block =
                    m3p->active_checkpoint_block;
                m3p->active_checkpoint_block =
                    m3p->checkpoint_target_block;
            }
            m3p->checkpoint_page = (uint8_t)(
                m3p->checkpoint_target_page + 2u);
            m3p->block_next_page[m3p->active_checkpoint_block] =
                m3p->checkpoint_page;
            m3p->active_journal_block = m3p->inactive_journal_block;
            m3p->inactive_journal_block = m3p->checkpoint_old_journal;
            m3p->journal_page = 0;
            ++m3p->journal_generation;
            m3p->checkpoint_flow = 3;
            return 1;
        }
    } else if (m3p->checkpoint_flow == 3) {
        if (m3p->child.state == M3P_CHILD_IDLE) {
            struct fwlab_nfc_ppa ppa = {0, 0, 0,
                                        m3p->checkpoint_old_journal, 0, 0};

            return m3p_child_erase_start(m3p, ppa) == FWLAB_SPINE_V0_OK;
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            uint8_t block = m3p->checkpoint_old_journal;

            m3p->block_erase_generation[block] =
                m3p->child.completion.final_erase_generation;
            ++m3p->block_erase_count[block];
            m3p->block_next_page[block] = 0;
            memset(&m3p->p2l[m3p_physical_index(block, 0)], M3P_P2L_FREE,
                   M3P_PAGES_PER_BLOCK);
            m3p_child_consume(m3p);
            if (m3p->checkpoint_rotating) {
                m3p->checkpoint_flow = 4;
            } else {
                m3p->checkpoint_flow = 0;
                m3p->operation.state = m3p->checkpoint_return_state;
            }
            return 1;
        }
    } else if (m3p->checkpoint_flow == 4) {
        if (m3p->child.state == M3P_CHILD_IDLE) {
            struct fwlab_nfc_ppa ppa = {0, 0, 0,
                                        m3p->checkpoint_old_block, 0, 0};

            return m3p_child_erase_start(m3p, ppa) == FWLAB_SPINE_V0_OK;
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            uint8_t block = m3p->checkpoint_old_block;

            m3p->block_erase_generation[block] =
                m3p->child.completion.final_erase_generation;
            ++m3p->block_erase_count[block];
            m3p->block_next_page[block] = 0;
            memset(&m3p->p2l[m3p_physical_index(block, 0)], M3P_P2L_FREE,
                   M3P_PAGES_PER_BLOCK);
            m3p_child_consume(m3p);
            m3p->checkpoint_rotating = 0;
            m3p->checkpoint_flow = 0;
            m3p->operation.state = m3p->checkpoint_return_state;
            return 1;
        }
    }
    return 0;
}

static int write_drive(struct fwlab_m3p *m3p)
{
    struct m3p_operation *operation = &m3p->operation;
    uint8_t index = (uint8_t)(operation->current_lpn - operation->first_lpn);
    struct m3p_delta *delta = &operation->delta[index];

    if (operation->state == M3P_OPERATION_PAGE_PREPARE) {
        uint8_t written = sector_mask(operation->request.lba,
                                      operation->request.lba_count,
                                      operation->current_lpn);

        if (written != UINT8_MAX && delta->prior.state == M3P_L2P_VALUE) {
            struct fwlab_nfc_ppa ppa = {0, 0, 0, delta->prior.block,
                                        delta->prior.page, 0};

            if (m3p_child_read_start(m3p, 0, ppa) != FWLAB_SPINE_V0_OK) {
                return 0;
            }
            operation->state = M3P_OPERATION_PAGE_READ;
            return 1;
        }
        memset(m3p->frame_main[0], 0, M3P_PAGE_BYTES);
        if (!start_current_data_program(m3p)) {
            m3p_operation_fail(m3p, FWLAB_NFC_REASON_INTERNAL, 0);
            return 1;
        }
        operation->state = M3P_OPERATION_PAGE_PROGRAM;
        return 1;
    }
    if (operation->state == M3P_OPERATION_PAGE_READ) {
        if (m3p->child.state == M3P_CHILD_FAILED) {
            m3p_operation_fail(m3p, m3p->child.fault_code,
                               operation->effect_seen);
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            if (!frame_matches_entry(m3p, 0, operation->current_lpn,
                                     &delta->prior)) {
                m3p_operation_fail(m3p, FWLAB_NFC_REASON_INTERNAL,
                                   operation->effect_seen);
                return 1;
            }
            zero_invalid_sectors(m3p->frame_main[0],
                                 delta->prior.valid_mask);
            m3p_child_consume(m3p);
            if (!start_current_data_program(m3p)) {
                m3p_operation_fail(m3p, FWLAB_NFC_REASON_INTERNAL,
                                   operation->effect_seen);
                return 1;
            }
            operation->state = M3P_OPERATION_PAGE_PROGRAM;
            return 1;
        }
    }
    if (operation->state == M3P_OPERATION_PAGE_PROGRAM) {
        if (m3p->child.state == M3P_CHILD_FAILED) {
            m3p_operation_fail(m3p, m3p->child.fault_code, 1);
            return 1;
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            operation->effect_seen = 1;
            m3p_child_consume(m3p);
            if (operation->current_lpn < operation->last_lpn) {
                ++operation->current_lpn;
                operation->state = M3P_OPERATION_PAGE_PREPARE;
            } else {
                operation->state = M3P_OPERATION_PENDING_INSTALL;
            }
            return 1;
        }
    }
    return 0;
}

static int read_drive(struct fwlab_m3p *m3p)
{
    struct m3p_operation *operation = &m3p->operation;
    const struct m3p_map_entry *entry =
        &m3p->visible[operation->current_lpn];
    uint64_t request_first = operation->request.lba;
    uint64_t request_last = request_first + operation->request.lba_count;
    uint64_t page_first =
        (uint64_t)operation->current_lpn * M3P_SECTORS_PER_PAGE;
    uint64_t page_last = page_first + M3P_SECTORS_PER_PAGE;
    uint64_t first = request_first > page_first ? request_first : page_first;
    uint64_t last = request_last < page_last ? request_last : page_last;
    uint64_t sector;

    if (operation->state == M3P_OPERATION_READ_PAGE &&
        m3p->child.state == M3P_CHILD_IDLE &&
        entry->state == M3P_L2P_VALUE) {
        struct fwlab_nfc_ppa ppa = {0, 0, 0, entry->block, entry->page, 0};

        return m3p_child_read_start(m3p, 0, ppa) == FWLAB_SPINE_V0_OK;
    }
    if (operation->state == M3P_OPERATION_READ_PAGE &&
        m3p->child.state == M3P_CHILD_FAILED) {
        m3p_operation_fail(m3p, m3p->child.fault_code, 0);
        return 1;
    }
    if (operation->state == M3P_OPERATION_READ_PAGE &&
        (entry->state != M3P_L2P_VALUE ||
         m3p->child.state == M3P_CHILD_DONE)) {
        if (entry->state == M3P_L2P_VALUE &&
            !frame_matches_entry(m3p, 0, operation->current_lpn, entry)) {
            m3p_operation_fail(m3p, FWLAB_NFC_REASON_INTERNAL, 0);
            return 1;
        }
        for (sector = first; sector < last; ++sector) {
            size_t destination =
                (size_t)(sector - request_first) * FWLAB_M3P_LBA_BYTES;
            uint8_t sector_bit =
                (uint8_t)(1u << (sector - page_first));

            if (entry->state == M3P_L2P_VALUE &&
                (entry->valid_mask & sector_bit) != 0) {
                size_t source =
                    (size_t)(sector - page_first) * FWLAB_M3P_LBA_BYTES;

                memcpy(&operation->io_bytes[destination],
                       &m3p->frame_main[0][source], FWLAB_M3P_LBA_BYTES);
            } else {
                memset(&operation->io_bytes[destination], 0,
                       FWLAB_M3P_LBA_BYTES);
            }
        }
        if (m3p->child.state == M3P_CHILD_DONE) {
            m3p_child_consume(m3p);
        }
        if (operation->current_lpn < operation->last_lpn) {
            ++operation->current_lpn;
        } else {
            operation->state = M3P_OPERATION_READ_PUBLISH;
        }
        return 1;
    }
    if (operation->state == M3P_OPERATION_READ_PUBLISH) {
        if (m3p->controller_buffer.ops->write(
                m3p->controller_buffer.context, &operation->request.buffer,
                &operation->request.buffer_span, operation->io_bytes,
                operation->request.buffer_span.length) !=
            FWLAB_CONTROLLER_BUFFER_V0_OK) {
            m3p_operation_fail(m3p, 2, 0);
        } else {
            m3p_operation_succeed(m3p, FWLAB_BLOCK_V0_WITNESS_NONE, 0);
        }
        return 1;
    }
    return 0;
}

static int pending_install_drive(struct fwlab_m3p *m3p)
{
    struct m3p_operation *operation = &m3p->operation;
    uint8_t subtype = operation->request.operation == FWLAB_BLOCK_V0_TRIM ?
        M3P_MAP_TRIM : M3P_MAP_WRITE;
    uint8_t index;

    if (operation->request.operation == FWLAB_BLOCK_V0_WRITE &&
        operation->request.durability == FWLAB_BLOCK_V0_DURABILITY_SELF &&
        m3p->pending_count >= M3P_MAX_PENDING) {
        operation->state = M3P_OPERATION_COMMIT_PENDING;
        return 1;
    }
    if (!m3p_pending_append(m3p, subtype, operation->host_sequence,
                            operation->delta, operation->delta_count)) {
        m3p_operation_fail(m3p, 3, operation->effect_seen);
        return 1;
    }
    operation->commit_slot = 1;
    for (index = 0; index < operation->delta_count; ++index) {
        m3p_publish_visible_delta(m3p, &operation->delta[index]);
    }
    if (operation->request.operation == FWLAB_BLOCK_V0_TRIM) {
        m3p_operation_succeed(m3p, FWLAB_BLOCK_V0_WITNESS_NONE, 0);
    } else if (operation->request.durability ==
               FWLAB_BLOCK_V0_DURABILITY_VOLATILE_ALLOWED) {
        m3p_operation_succeed(m3p, FWLAB_BLOCK_V0_WITNESS_VOLATILE, 0);
    } else {
        operation->state = M3P_OPERATION_COMMIT_PENDING;
    }
    return 1;
}

static int commit_drive(struct fwlab_m3p *m3p)
{
    struct m3p_operation *operation = &m3p->operation;
    struct m3p_pending *pending;
    uint8_t index;

    if (operation->request.operation == FWLAB_BLOCK_V0_WRITE &&
        operation->request.durability == FWLAB_BLOCK_V0_DURABILITY_SELF &&
        !operation->commit_slot && m3p->pending_count < M3P_MAX_PENDING) {
        if (!m3p_pending_append(m3p, M3P_MAP_WRITE,
                                operation->host_sequence, operation->delta,
                                operation->delta_count)) {
            m3p_operation_fail(m3p, 4, operation->effect_seen);
            return 1;
        }
        operation->commit_slot = 1;
        for (index = 0; index < operation->delta_count; ++index) {
            m3p_publish_visible_delta(m3p, &operation->delta[index]);
        }
    }
    pending = m3p_pending_front(m3p);
    if (pending != NULL) {
        if (!prepare_map_program(m3p, pending)) {
            m3p_operation_fail(m3p, 5, operation->effect_seen);
            return 1;
        }
        operation->state = M3P_OPERATION_COMMIT_PROGRAM;
        return 1;
    }
    if (m3p->journal_page >= 24) {
        if (!m3p_start_checkpoint(m3p, M3P_OPERATION_COMMIT_PENDING)) {
            m3p_operation_fail(m3p, 6, operation->effect_seen);
        }
        return 1;
    }
    if (operation->request.operation == FWLAB_BLOCK_V0_WRITE) {
        m3p_operation_succeed(m3p, FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE,
                              operation->host_sequence);
    } else {
        m3p_operation_succeed(m3p, FWLAB_BLOCK_V0_WITNESS_FRONTIER_DURABLE,
                              operation->captured_frontier);
    }
    return 1;
}

static int commit_program_drive(struct fwlab_m3p *m3p)
{
    struct m3p_operation *operation = &m3p->operation;
    struct m3p_pending *pending = m3p_pending_front(m3p);
    uint8_t index;

    if (m3p->child.state == M3P_CHILD_FAILED || pending == NULL) {
        m3p_operation_fail(m3p, m3p->child.fault_code, 1);
        return 1;
    }
    if (m3p->child.state != M3P_CHILD_DONE) {
        return 0;
    }
    m3p_child_consume(m3p);
    ++m3p->record_sequence;
    ++m3p->map_sequence;
    ++m3p->journal_page;
    m3p->block_next_page[m3p->active_journal_block] = m3p->journal_page;
    for (index = 0; index < pending->delta_count; ++index) {
        m3p_publish_durable_delta(m3p, &pending->delta[index],
                                  m3p->map_sequence);
    }
    if (pending->host_sequence > m3p->durable_frontier) {
        m3p->durable_frontier = pending->host_sequence;
    }
    m3p_pending_pop(m3p);
    operation->state = M3P_OPERATION_COMMIT_PENDING;
    return 1;
}

static int operation_drive(struct fwlab_m3p *m3p)
{
    struct m3p_operation *operation = &m3p->operation;

    if (operation->state == M3P_OPERATION_FREE) {
        return 0;
    }
    if (operation->state == M3P_OPERATION_DRAINING) {
        operation->state = M3P_OPERATION_DRAINED;
        operation->status.state = FWLAB_BLOCK_V0_STATE_DRAINED;
        return 1;
    }
    if (operation->state >= M3P_OPERATION_TERMINAL) {
        return 0;
    }
    if (operation->cancel_requested && operation->state !=
            M3P_OPERATION_COMMIT_PROGRAM && m3p->child.state == M3P_CHILD_IDLE) {
        m3p_operation_fail(m3p, FWLAB_NFC_REASON_CANCELLED,
                           operation->effect_seen);
        return 1;
    }
    if (operation->state == M3P_OPERATION_PAGE_PREPARE ||
        operation->state == M3P_OPERATION_PAGE_READ ||
        operation->state == M3P_OPERATION_PAGE_PROGRAM) {
        return write_drive(m3p);
    }
    if (operation->state == M3P_OPERATION_READ_PAGE ||
        operation->state == M3P_OPERATION_READ_PUBLISH) {
        return read_drive(m3p);
    }
    if (operation->state == M3P_OPERATION_PENDING_INSTALL) {
        return pending_install_drive(m3p);
    }
    if (operation->state == M3P_OPERATION_COMMIT_PENDING) {
        return commit_drive(m3p);
    }
    if (operation->state == M3P_OPERATION_COMMIT_PROGRAM) {
        return commit_program_drive(m3p);
    }
    if (operation->state == M3P_OPERATION_CHECKPOINT) {
        return m3p_checkpoint_drive(m3p);
    }
    return 0;
}

static int close_drive(struct fwlab_m3p *m3p)
{
    bool quiescent = false;
    enum fwlab_nfc_api_result result;

    if (!m3p->admission_closed || m3p->operation.state != M3P_OPERATION_FREE ||
        m3p->work_kind != FWLAB_M3P_MAINTENANCE_NONE ||
        m3p->child.state != M3P_CHILD_IDLE) {
        return 0;
    }
    if (m3p->pending_count != 0) {
        discard_pending(m3p);
        return 1;
    }
    if (!m3p->nfc_close_started) {
        result = m3p->nfc.ops->reset_begin(
            m3p->nfc.context, m3p->config.nfc_instance_nonce,
            m3p->config.nfc_epoch);
        if (result != FWLAB_NFC_API_OK) {
            m3p->quarantined = 1;
            return 1;
        }
        m3p->nfc_close_started = 1;
        return 1;
    }
    if (!m3p->nfc_quiescent) {
        result = m3p->nfc.ops->quiescent(
            m3p->nfc.context, m3p->config.nfc_instance_nonce,
            m3p->config.nfc_epoch, &quiescent);
        if (result != FWLAB_NFC_API_OK) {
            m3p->quarantined = 1;
            return 1;
        }
        if (quiescent) {
            m3p->nfc_quiescent = 1;
        }
        return 1;
    }
    return 0;
}

int m3p_runtime_drive(struct fwlab_m3p *m3p)
{
    if (m3p->work_kind == FWLAB_M3P_MAINTENANCE_FORMAT) {
        return format_drive(m3p);
    }
    if (m3p->work_kind == FWLAB_M3P_MAINTENANCE_RECOVERY) {
        return m3p_recovery_drive(m3p);
    }
    if (m3p->work_kind == FWLAB_M3P_MAINTENANCE_GC) {
        return m3p_gc_drive(m3p);
    }
    if (operation_drive(m3p)) {
        return 1;
    }
    return close_drive(m3p);
}

enum fwlab_spine_result_v0 fwlab_m3p_step(
    struct fwlab_m3p *m3p, uint32_t budget,
    struct fwlab_m3p_step_result *result)
{
    uint32_t used = 0;
    uint32_t idle = 0;

    if (!m3p_live(m3p) || budget == 0 || result == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_M3P_VERSION;
    result->size = (uint16_t)sizeof(*result);
    while (used < budget && idle < 3) {
        int progressed = 0;

        if (m3p->fair_cursor == 0) {
            progressed = m3p_runtime_drive(m3p);
            result->ftl_transitions += (uint32_t)(progressed != 0);
        } else if (m3p->fair_cursor == 1) {
            progressed = m3p_nfc_submit_or_step(
                m3p, &result->nfc_transitions);
        } else {
            progressed = m3p_nfc_poll(m3p, &result->nfc_events);
        }
        m3p->fair_cursor = (uint8_t)((m3p->fair_cursor + 1u) % 3u);
        ++used;
        if (progressed) {
            idle = 0;
        } else {
            ++idle;
        }
    }
    result->units_used = used;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_m3p_epoch_query(
    const struct fwlab_m3p *m3p, struct fwlab_block_epoch_status_v0 *status)
{
    if (!m3p_live(m3p) || status == NULL || !m3p->admission_closed) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    return block_epoch_quiescent((void *)m3p, m3p->close_lifecycle_nonce,
                                 m3p->close_execution_epoch, status);
}

enum fwlab_spine_result_v0 fwlab_m3p_fini(struct fwlab_m3p *m3p)
{
    if (!m3p_live(m3p) || !m3p->admission_closed || !m3p->nfc_quiescent ||
        m3p->operation.state != M3P_OPERATION_FREE ||
        m3p->pending_count != 0 || m3p->child.state != M3P_CHILD_IDLE) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    m3p->initialized = 0;
    m3p->magic = 0;
    return FWLAB_SPINE_V0_OK;
}
