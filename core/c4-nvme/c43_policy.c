/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_internal.h"

#include <string.h>

static int boolean_valid(uint8_t value)
{
    return value <= 1;
}

static int request_branch_valid(
    const struct fwlab_c43_policy_request *request)
{
    const int block_fields_zero = request->slba == 0 &&
                                  request->zero_based_nlb == 0;
    const int feature_fields_zero = request->feature_selector == 0 &&
                                    request->requested_cq_count == 0 &&
                                    request->requested_sq_count == 0;

    switch (request->kind) {
    case FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES:
        return block_fields_zero && request->queue_entries == 0;
    case FWLAB_C43_REQUEST_CREATE_IO_CQ:
    case FWLAB_C43_REQUEST_CREATE_IO_SQ:
        return block_fields_zero && feature_fields_zero;
    case FWLAB_C43_REQUEST_READ:
    case FWLAB_C43_REQUEST_WRITE:
        return feature_fields_zero && request->queue_entries == 0;
    case FWLAB_C43_REQUEST_IDENTIFY_CONTROLLER:
    case FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE:
    case FWLAB_C43_REQUEST_IDENTIFY_ACTIVE_NAMESPACE_LIST:
    case FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST:
    case FWLAB_C43_REQUEST_DELETE_IO_CQ:
    case FWLAB_C43_REQUEST_DELETE_IO_SQ:
    case FWLAB_C43_REQUEST_ABORT:
    case FWLAB_C43_REQUEST_FLUSH:
    case FWLAB_C43_REQUEST_UNSUPPORTED:
        return block_fields_zero && feature_fields_zero &&
               request->queue_entries == 0;
    default:
        return 0;
    }
}

int fwlab_c43_policy_request_valid(
    const struct fwlab_c43_policy_request *request)
{
    return request != NULL && request->version == FWLAB_C43_POLICY_VERSION &&
           request->size == sizeof(*request) && request->reserved0 == 0 &&
           c43_handle_valid(&request->handle) &&
           c43_origin_valid(&request->origin) && request->transaction_uid != 0 &&
           request->kind >= FWLAB_C43_REQUEST_IDENTIFY_CONTROLLER &&
           request->kind <= FWLAB_C43_REQUEST_UNSUPPORTED &&
           (request->queue_class == FWLAB_NVME_QUEUE_ADMIN ||
            request->queue_class == FWLAB_NVME_QUEUE_IO) &&
           request->fuse <= FWLAB_NVME_FUSE_RESERVED &&
           request->pointer_format <= FWLAB_NVME_DATA_POINTER_RESERVED &&
           boolean_valid(request->data_present) &&
           boolean_valid(request->metadata_present) &&
           boolean_valid(request->fua) && boolean_valid(request->save) &&
           request->transport_fault <=
               FWLAB_NVME_TRANSPORT_STALE_GENERATION &&
           boolean_valid(request->reserved_bits_present) &&
           c43_bytes_zero(request->reserved_flag_padding,
                           sizeof(request->reserved_flag_padding)) &&
           c43_bytes_zero(request->reserved1, sizeof(request->reserved1)) &&
           request->reserved2 == 0 && request_branch_valid(request);
}

int fwlab_c43_transfer_shape_valid(
    const struct fwlab_c43_transfer_shape *shape)
{
    struct fwlab_nvme_profile profile;
    uint64_t expected_bytes;

    if (shape == NULL || shape->version != FWLAB_C43_POLICY_VERSION ||
        shape->size != sizeof(*shape) || shape->reserved0 != 0 ||
        shape->direction > FWLAB_C43_TRANSFER_CONTROLLER_TO_HOST ||
        shape->metadata_bytes != 0 || shape->metadata_pointer_required != 0 ||
        shape->data_pointer_required > 1 || shape->reserved1[0] != 0 ||
        shape->reserved1[1] != 0 ||
        !c43_bytes_zero(shape->reserved2, sizeof(shape->reserved2))) {
        return 0;
    }
    c43_profile_fixed(&profile);
    if (shape->direction == FWLAB_C43_TRANSFER_NONE) {
        return shape->data_bytes == 0 && shape->lba_bytes == 0 &&
               shape->lba_count == 0 && shape->data_pointer_required == 0;
    }
    if (shape->data_pointer_required != 1 || shape->data_bytes == 0 ||
        shape->data_bytes > profile.maximum_transfer_bytes) {
        return 0;
    }
    if (shape->lba_count == 0) {
        return shape->direction == FWLAB_C43_TRANSFER_CONTROLLER_TO_HOST &&
               shape->lba_bytes == 0 &&
               shape->data_bytes == profile.maximum_transfer_bytes;
    }
    expected_bytes = (uint64_t)shape->lba_count * profile.lba_bytes;
    return shape->lba_bytes == profile.lba_bytes &&
           shape->lba_count <= profile.lba_count &&
           expected_bytes == shape->data_bytes;
}

int fwlab_c43_block_intent_valid(
    const struct fwlab_c43_block_intent *intent)
{
    struct fwlab_nvme_profile profile;
    uint64_t expected_bytes;

    if (intent == NULL || intent->version != FWLAB_C43_POLICY_VERSION ||
        intent->size != sizeof(*intent) || intent->reserved0 != 0 ||
        !c43_handle_valid(&intent->command) ||
        !c43_origin_valid(&intent->origin) ||
        intent->namespace_id != 1 ||
        intent->operation < FWLAB_C43_BLOCK_READ ||
        intent->operation > FWLAB_C43_BLOCK_FLUSH ||
        !c43_ref_zero(&intent->frontier) ||
        !c43_bytes_zero(intent->reserved1, sizeof(intent->reserved1))) {
        return 0;
    }
    c43_profile_fixed(&profile);
    if (intent->operation == FWLAB_C43_BLOCK_FLUSH) {
        return intent->slba == 0 && intent->lba_count == 0 &&
               intent->data_bytes == 0 &&
               intent->durability == FWLAB_C43_DURABILITY_FIXED_FRONTIER;
    }
    if (intent->lba_count == 0 || intent->lba_count > profile.lba_count ||
        intent->slba >= profile.lba_count ||
        intent->slba > profile.lba_count - intent->lba_count) {
        return 0;
    }
    expected_bytes = (uint64_t)intent->lba_count * profile.lba_bytes;
    if (expected_bytes != intent->data_bytes) {
        return 0;
    }
    if (intent->operation == FWLAB_C43_BLOCK_READ) {
        return intent->durability == FWLAB_C43_DURABILITY_DEFAULT;
    }
    return intent->durability == FWLAB_C43_DURABILITY_DEFAULT ||
           intent->durability == FWLAB_C43_DURABILITY_REQUIRE_SELF;
}

static void policy_plan_base(
    struct fwlab_c43_policy_plan *plan,
    const struct fwlab_c43_policy_request *request,
    uint32_t kind,
    uint32_t status,
    uint8_t dnr)
{
    memset(plan, 0, sizeof(*plan));
    plan->version = FWLAB_C43_POLICY_VERSION;
    plan->size = sizeof(*plan);
    plan->command = request->handle;
    plan->origin = request->origin;
    plan->transaction_uid = request->transaction_uid;
    plan->kind = kind;
    plan->semantic_status = status;
    plan->dnr = dnr;
}

static enum fwlab_c43_api_result policy_plan_emit(
    const struct fwlab_c43_policy_plan *local,
    struct fwlab_c43_policy_plan *plan)
{
    if (!fwlab_c43_policy_plan_valid(local)) {
        return FWLAB_C43_API_POISONED;
    }
    memcpy(plan, local, sizeof(*local));
    return FWLAB_C43_API_OK;
}

static enum fwlab_c43_api_result policy_error(
    const struct fwlab_c43_policy_request *request,
    uint32_t status,
    uint8_t dnr,
    struct fwlab_c43_policy_plan *plan)
{
    struct fwlab_c43_policy_plan local;

    policy_plan_base(&local, request, FWLAB_C43_PLAN_IMMEDIATE, status, dnr);
    return policy_plan_emit(&local, plan);
}

static uint8_t request_expected_queue(uint8_t kind)
{
    switch (kind) {
    case FWLAB_C43_REQUEST_READ:
    case FWLAB_C43_REQUEST_WRITE:
    case FWLAB_C43_REQUEST_FLUSH:
        return FWLAB_NVME_QUEUE_IO;
    default:
        return FWLAB_NVME_QUEUE_ADMIN;
    }
}

static int request_namespace_valid(
    const struct fwlab_c43_policy_request *request)
{
    switch (request->kind) {
    case FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE:
    case FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST:
    case FWLAB_C43_REQUEST_READ:
    case FWLAB_C43_REQUEST_WRITE:
    case FWLAB_C43_REQUEST_FLUSH:
        return request->namespace_id == 1;
    case FWLAB_C43_REQUEST_IDENTIFY_ACTIVE_NAMESPACE_LIST:
        return 1;
    default:
        return request->namespace_id == 0;
    }
}

static int request_data_required(uint8_t kind)
{
    return kind == FWLAB_C43_REQUEST_IDENTIFY_CONTROLLER ||
           kind == FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE ||
           kind == FWLAB_C43_REQUEST_IDENTIFY_ACTIVE_NAMESPACE_LIST ||
           kind == FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST ||
           kind == FWLAB_C43_REQUEST_READ ||
           kind == FWLAB_C43_REQUEST_WRITE;
}

static int request_common_error(
    const struct fwlab_c43_policy_request *request,
    uint32_t *status,
    uint8_t *dnr)
{
    if (request->transport_fault != FWLAB_NVME_TRANSPORT_NONE) {
        *dnr = request->transport_fault ==
                       FWLAB_NVME_TRANSPORT_QUEUE_MEMORY
                   ? 0
                   : 1;
        if (request->transport_fault ==
            FWLAB_NVME_TRANSPORT_UNSUPPORTED_FORMAT) {
            *status = FWLAB_C43_STATUS_INVALID_FIELD;
        } else if (request->transport_fault ==
                   FWLAB_NVME_TRANSPORT_STALE_GENERATION) {
            *status = FWLAB_C43_STATUS_COMMAND_SEQUENCE;
        } else {
            *status = FWLAB_C43_STATUS_TRANSFER_FAILURE;
        }
        return 1;
    }
    if (request->kind == FWLAB_C43_REQUEST_UNSUPPORTED ||
        request->queue_class != request_expected_queue(request->kind)) {
        *status = FWLAB_C43_STATUS_UNSUPPORTED_COMMAND;
        *dnr = 1;
        return 1;
    }
    if (request->reserved_bits_present ||
        request->fuse != FWLAB_NVME_FUSE_NONE ||
        request->pointer_format != FWLAB_NVME_DATA_POINTER_PRP ||
        request->metadata_present || request->save ||
        (request->fua && request->kind != FWLAB_C43_REQUEST_WRITE) ||
        (request_data_required(request->kind) && !request->data_present) ||
        (!request_data_required(request->kind) && request->data_present)) {
        *status = FWLAB_C43_STATUS_INVALID_FIELD;
        *dnr = 1;
        return 1;
    }
    if (!request_namespace_valid(request)) {
        *status = FWLAB_C43_STATUS_INVALID_NAMESPACE;
        *dnr = 1;
        return 1;
    }
    return 0;
}

static void identify_plan(
    struct fwlab_c43_policy_plan *plan,
    const struct fwlab_c43_policy_request *request)
{
    policy_plan_base(plan, request, FWLAB_C43_PLAN_PAYLOAD,
                     FWLAB_C43_STATUS_SUCCESS, 0);
    plan->actual_length = FWLAB_C43_IDENTIFY_BYTES;
    plan->required_witness_mask = FWLAB_C43_WITNESS_PAYLOAD_READY |
                                  FWLAB_C43_WITNESS_DMA_OUT_COMPLETE;
    plan->shape.version = FWLAB_C43_POLICY_VERSION;
    plan->shape.size = sizeof(plan->shape);
    plan->shape.direction = FWLAB_C43_TRANSFER_CONTROLLER_TO_HOST;
    plan->shape.data_bytes = FWLAB_C43_IDENTIFY_BYTES;
    plan->shape.data_pointer_required = 1;
    plan->identify.version = FWLAB_C43_POLICY_VERSION;
    plan->identify.size = sizeof(plan->identify);
    plan->identify.namespace_id = request->namespace_id;
    plan->identify.payload_bytes = FWLAB_C43_IDENTIFY_BYTES;
    plan->identify.identity_version = 1;
    switch (request->kind) {
    case FWLAB_C43_REQUEST_IDENTIFY_CONTROLLER:
        plan->identify.kind = FWLAB_C43_IDENTIFY_CONTROLLER;
        break;
    case FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE:
        plan->identify.kind = FWLAB_C43_IDENTIFY_NAMESPACE;
        break;
    case FWLAB_C43_REQUEST_IDENTIFY_ACTIVE_NAMESPACE_LIST:
        plan->identify.kind = FWLAB_C43_IDENTIFY_ACTIVE_NAMESPACE_LIST;
        break;
    default:
        plan->identify.kind =
            FWLAB_C43_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST;
        break;
    }
}

static int checked_transfer(
    const struct fwlab_nvme_profile *profile,
    const struct fwlab_c43_policy_request *request,
    uint32_t *lba_count,
    uint32_t *data_bytes)
{
    const uint64_t count = (uint64_t)request->zero_based_nlb + 1;
    uint64_t end;
    uint64_t bytes;

    if (count > profile->lba_count || request->slba >= profile->lba_count ||
        request->slba > UINT64_MAX - count) {
        return 0;
    }
    end = request->slba + count;
    if (end > profile->lba_count ||
        count > UINT64_MAX / profile->lba_bytes) {
        return 0;
    }
    bytes = count * profile->lba_bytes;
    if (bytes > profile->maximum_transfer_bytes || bytes > UINT32_MAX) {
        return 0;
    }
    *lba_count = (uint32_t)count;
    *data_bytes = (uint32_t)bytes;
    return 1;
}

static void block_plan(
    struct fwlab_c43_policy_plan *plan,
    const struct fwlab_nvme_profile *profile,
    const struct fwlab_c43_policy_request *request,
    uint32_t operation,
    uint32_t lba_count,
    uint32_t data_bytes)
{
    policy_plan_base(plan, request, FWLAB_C43_PLAN_BLOCK,
                     FWLAB_C43_STATUS_SUCCESS, 0);
    plan->actual_length = data_bytes;
    plan->shape.version = FWLAB_C43_POLICY_VERSION;
    plan->shape.size = sizeof(plan->shape);
    plan->block.version = FWLAB_C43_POLICY_VERSION;
    plan->block.size = sizeof(plan->block);
    plan->block.command = request->handle;
    plan->block.origin = request->origin;
    plan->block.slba = request->slba;
    plan->block.namespace_id = 1;
    plan->block.operation = operation;
    plan->block.lba_count = lba_count;
    plan->block.data_bytes = data_bytes;
    if (operation == FWLAB_C43_BLOCK_READ) {
        plan->shape.direction = FWLAB_C43_TRANSFER_CONTROLLER_TO_HOST;
        plan->block.durability = FWLAB_C43_DURABILITY_DEFAULT;
        plan->required_witness_mask =
            FWLAB_C43_WITNESS_BLOCK_READ_READY |
            FWLAB_C43_WITNESS_DMA_OUT_COMPLETE;
    } else if (operation == FWLAB_C43_BLOCK_WRITE) {
        plan->shape.direction = FWLAB_C43_TRANSFER_HOST_TO_CONTROLLER;
        plan->block.durability = request->fua
                                     ? FWLAB_C43_DURABILITY_REQUIRE_SELF
                                     : FWLAB_C43_DURABILITY_DEFAULT;
        plan->required_witness_mask =
            FWLAB_C43_WITNESS_DMA_IN_COMPLETE |
            FWLAB_C43_WITNESS_BLOCK_WRITE_COMPLETE;
        if (request->fua) {
            plan->required_witness_mask |=
                FWLAB_C43_WITNESS_DURABILITY_COMPLETE;
        }
    } else {
        plan->block.durability = FWLAB_C43_DURABILITY_FIXED_FRONTIER;
        plan->required_witness_mask =
            FWLAB_C43_WITNESS_DURABILITY_COMPLETE;
    }
    if (operation != FWLAB_C43_BLOCK_FLUSH) {
        plan->shape.data_bytes = data_bytes;
        plan->shape.lba_bytes = profile->lba_bytes;
        plan->shape.lba_count = lba_count;
        plan->shape.data_pointer_required = 1;
    }
}

enum fwlab_c43_api_result fwlab_c43_policy_begin(
    const struct fwlab_nvme_profile *profile,
    const struct fwlab_c43_policy_request *request,
    struct fwlab_c43_policy_plan *plan)
{
    struct fwlab_c43_policy_plan local;
    uint32_t status = FWLAB_C43_STATUS_SUCCESS;
    uint32_t lba_count;
    uint32_t data_bytes;
    uint8_t dnr = 0;

    if (!c43_profile_is_fixed(profile) ||
        !fwlab_c43_policy_request_valid(request) || plan == NULL ||
        c43_ranges_overlap(profile, sizeof(*profile), plan, sizeof(*plan)) ||
        c43_ranges_overlap(request, sizeof(*request), plan, sizeof(*plan))) {
        return FWLAB_C43_API_INVALID;
    }
    if (request_common_error(request, &status, &dnr)) {
        return policy_error(request, status, dnr, plan);
    }
    switch (request->kind) {
    case FWLAB_C43_REQUEST_IDENTIFY_CONTROLLER:
    case FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE:
    case FWLAB_C43_REQUEST_IDENTIFY_ACTIVE_NAMESPACE_LIST:
    case FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST:
        identify_plan(&local, request);
        break;
    case FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES:
        if (request->feature_selector !=
                FWLAB_C43_FEATURE_NUMBER_OF_QUEUES ||
            request->requested_cq_count > UINT16_MAX ||
            request->requested_sq_count > UINT16_MAX) {
            return policy_error(request, FWLAB_C43_STATUS_INVALID_FIELD, 1,
                                plan);
        }
        policy_plan_base(&local, request, FWLAB_C43_PLAN_IMMEDIATE,
                         FWLAB_C43_STATUS_SUCCESS, 0);
        break;
    case FWLAB_C43_REQUEST_CREATE_IO_CQ:
    case FWLAB_C43_REQUEST_CREATE_IO_SQ:
        if (request->queue_entries != profile->integration_queue_depth) {
            return policy_error(request,
                                FWLAB_C43_STATUS_INVALID_QUEUE_SIZE, 1,
                                plan);
        }
        policy_plan_base(&local, request, FWLAB_C43_PLAN_QUEUE_EFFECT,
                         FWLAB_C43_STATUS_SUCCESS, 0);
        local.required_witness_mask =
            FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED;
        break;
    case FWLAB_C43_REQUEST_DELETE_IO_CQ:
    case FWLAB_C43_REQUEST_DELETE_IO_SQ:
        policy_plan_base(&local, request, FWLAB_C43_PLAN_QUEUE_EFFECT,
                         FWLAB_C43_STATUS_SUCCESS, 0);
        local.required_witness_mask =
            FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED;
        break;
    case FWLAB_C43_REQUEST_ABORT:
        policy_plan_base(&local, request, FWLAB_C43_PLAN_ABORT_RESOLVE,
                         FWLAB_C43_STATUS_SUCCESS, 0);
        break;
    case FWLAB_C43_REQUEST_READ:
    case FWLAB_C43_REQUEST_WRITE:
        if (!checked_transfer(profile, request, &lba_count, &data_bytes)) {
            return policy_error(request, FWLAB_C43_STATUS_LBA_RANGE, 1,
                                plan);
        }
        block_plan(&local, profile, request,
                   request->kind == FWLAB_C43_REQUEST_READ
                       ? FWLAB_C43_BLOCK_READ
                       : FWLAB_C43_BLOCK_WRITE,
                   lba_count, data_bytes);
        break;
    case FWLAB_C43_REQUEST_FLUSH:
        block_plan(&local, profile, request, FWLAB_C43_BLOCK_FLUSH, 0, 0);
        break;
    default:
        return FWLAB_C43_API_POISONED;
    }
    return policy_plan_emit(&local, plan);
}
