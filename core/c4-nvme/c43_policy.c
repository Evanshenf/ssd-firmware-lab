/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_internal.h"

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
           request->reserved_command_flags == 0 &&
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
