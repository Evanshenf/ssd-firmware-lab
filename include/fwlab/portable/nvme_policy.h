/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PORTABLE_NVME_POLICY_H
#define FWLAB_PORTABLE_NVME_POLICY_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/portable/nvme_types.h"

#define FWLAB_C43_POLICY_VERSION 1u
#define FWLAB_C43_IDENTIFY_BYTES 4096u
#define FWLAB_C43_FEATURE_NUMBER_OF_QUEUES 7u

enum fwlab_c43_api_result {
    FWLAB_C43_API_OK = 0,
    FWLAB_C43_API_INVALID = 1,
    FWLAB_C43_API_WRONG_STATE = 2,
    FWLAB_C43_API_STALE = 3,
    FWLAB_C43_API_NO_CAPACITY = 4,
    FWLAB_C43_API_IN_PROGRESS = 5,
    FWLAB_C43_API_SUPERSEDED = 6,
    FWLAB_C43_API_POISONED = 7,
    FWLAB_C43_API_COUNTER_EXHAUSTED = 8,
    FWLAB_C43_API_NOT_IMPLEMENTED = 9
};

enum fwlab_c43_request_kind {
    FWLAB_C43_REQUEST_IDENTIFY_CONTROLLER = 1,
    FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE = 2,
    FWLAB_C43_REQUEST_IDENTIFY_ACTIVE_NAMESPACE_LIST = 3,
    FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST = 4,
    FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES = 5,
    FWLAB_C43_REQUEST_CREATE_IO_CQ = 6,
    FWLAB_C43_REQUEST_CREATE_IO_SQ = 7,
    FWLAB_C43_REQUEST_DELETE_IO_CQ = 8,
    FWLAB_C43_REQUEST_DELETE_IO_SQ = 9,
    FWLAB_C43_REQUEST_ABORT = 10,
    FWLAB_C43_REQUEST_READ = 11,
    FWLAB_C43_REQUEST_WRITE = 12,
    FWLAB_C43_REQUEST_FLUSH = 13,
    FWLAB_C43_REQUEST_UNSUPPORTED = 14
};

enum fwlab_c43_plan_kind {
    FWLAB_C43_PLAN_IMMEDIATE = 1,
    FWLAB_C43_PLAN_QUEUE_EFFECT = 2,
    FWLAB_C43_PLAN_ABORT_RESOLVE = 3,
    FWLAB_C43_PLAN_PAYLOAD = 4,
    FWLAB_C43_PLAN_BLOCK = 5
};

enum fwlab_c43_semantic_status {
    FWLAB_C43_STATUS_SUCCESS = 0,
    FWLAB_C43_STATUS_UNSUPPORTED_COMMAND = 1,
    FWLAB_C43_STATUS_INVALID_FIELD = 2,
    FWLAB_C43_STATUS_INVALID_NAMESPACE = 3,
    FWLAB_C43_STATUS_LBA_RANGE = 4,
    FWLAB_C43_STATUS_COMMAND_SEQUENCE = 5,
    FWLAB_C43_STATUS_INVALID_QUEUE = 6,
    FWLAB_C43_STATUS_INVALID_QUEUE_SIZE = 7,
    FWLAB_C43_STATUS_INVALID_QUEUE_DELETE = 8,
    FWLAB_C43_STATUS_ABORTED = 9,
    FWLAB_C43_STATUS_TRANSFER_FAILURE = 10,
    FWLAB_C43_STATUS_MEDIA_FAILURE = 11,
    FWLAB_C43_STATUS_RESOURCE_FAILURE = 12,
    FWLAB_C43_STATUS_INTERNAL_FAILURE = 13
};

enum fwlab_c43_transfer_direction {
    FWLAB_C43_TRANSFER_NONE = 0,
    FWLAB_C43_TRANSFER_HOST_TO_CONTROLLER = 1,
    FWLAB_C43_TRANSFER_CONTROLLER_TO_HOST = 2
};

enum fwlab_c43_durability_request {
    FWLAB_C43_DURABILITY_NONE = 0,
    FWLAB_C43_DURABILITY_DEFAULT = 1,
    FWLAB_C43_DURABILITY_REQUIRE_SELF = 2,
    FWLAB_C43_DURABILITY_FIXED_FRONTIER = 3
};

enum fwlab_c43_block_operation {
    FWLAB_C43_BLOCK_READ = 1,
    FWLAB_C43_BLOCK_WRITE = 2,
    FWLAB_C43_BLOCK_FLUSH = 3
};

enum fwlab_c43_identify_kind {
    FWLAB_C43_IDENTIFY_CONTROLLER = 1,
    FWLAB_C43_IDENTIFY_NAMESPACE = 2,
    FWLAB_C43_IDENTIFY_ACTIVE_NAMESPACE_LIST = 3,
    FWLAB_C43_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST = 4
};

enum fwlab_c43_witness_bit {
    FWLAB_C43_WITNESS_VALIDATED_ONLY = 1u << 0,
    FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED = 1u << 1,
    FWLAB_C43_WITNESS_PAYLOAD_READY = 1u << 2,
    FWLAB_C43_WITNESS_DMA_IN_COMPLETE = 1u << 3,
    FWLAB_C43_WITNESS_BLOCK_READ_READY = 1u << 4,
    FWLAB_C43_WITNESS_BLOCK_WRITE_COMPLETE = 1u << 5,
    FWLAB_C43_WITNESS_DMA_OUT_COMPLETE = 1u << 6,
    FWLAB_C43_WITNESS_DURABILITY_COMPLETE = 1u << 7
};

#define FWLAB_C43_WITNESS_ALL ((uint32_t)0xffu)

struct fwlab_c43_opaque_ref {
    uint64_t word[2];
};

struct fwlab_c43_policy_request {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nvme_command_handle handle;
    struct fwlab_nvme_origin_token origin;
    uint64_t transaction_uid;
    uint64_t slba;
    uint32_t namespace_id;
    uint32_t zero_based_nlb;
    uint32_t feature_selector;
    uint32_t requested_cq_count;
    uint32_t requested_sq_count;
    uint32_t queue_entries;
    uint32_t transport_fault;
    uint8_t kind;
    uint8_t queue_class;
    uint8_t fuse;
    uint8_t pointer_format;
    uint8_t data_present;
    uint8_t metadata_present;
    uint8_t fua;
    uint8_t save;
    uint8_t reserved_bits_present;
    uint8_t reserved_flag_padding[3];
    uint32_t reserved1[5];
    uint32_t reserved2;
};

struct fwlab_c43_transfer_shape {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint32_t direction;
    uint32_t data_bytes;
    uint32_t metadata_bytes;
    uint32_t lba_bytes;
    uint32_t lba_count;
    uint8_t data_pointer_required;
    uint8_t metadata_pointer_required;
    uint8_t reserved1[2];
    uint32_t reserved2[4];
};

struct fwlab_c43_identify_recipe {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint32_t kind;
    uint32_t namespace_id;
    uint32_t payload_bytes;
    uint32_t identity_version;
    uint32_t reserved1[6];
};

struct fwlab_c43_block_intent {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nvme_command_handle command;
    struct fwlab_nvme_origin_token origin;
    struct fwlab_c43_opaque_ref frontier;
    uint64_t slba;
    uint32_t namespace_id;
    uint32_t operation;
    uint32_t durability;
    uint32_t lba_count;
    uint32_t data_bytes;
    uint32_t reserved1[3];
};

struct fwlab_c43_policy_plan {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nvme_command_handle command;
    struct fwlab_nvme_origin_token origin;
    uint64_t transaction_uid;
    uint32_t kind;
    uint32_t semantic_status;
    uint32_t result_dword0;
    uint32_t actual_length;
    uint32_t required_witness_mask;
    uint32_t satisfied_witness_mask;
    uint8_t dnr;
    uint8_t more;
    uint8_t crd;
    uint8_t effect_class;
    struct fwlab_c43_transfer_shape shape;
    struct fwlab_c43_identify_recipe identify;
    uint32_t reserved_branch_padding;
    struct fwlab_c43_block_intent block;
    uint32_t reserved1[4];
};

struct fwlab_c43_completion_witness {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nvme_command_handle command;
    struct fwlab_nvme_origin_token origin;
    struct fwlab_c43_opaque_ref predecessor;
    uint64_t provider_generation;
    uint32_t witness_mask;
    uint32_t units_completed;
    uint8_t effect_class;
    uint8_t terminal_kind;
    uint8_t reserved1[2];
    uint32_t reserved2[4];
    uint32_t reserved3;
};

int fwlab_c43_policy_request_valid(
    const struct fwlab_c43_policy_request *request
);
int fwlab_c43_transfer_shape_valid(
    const struct fwlab_c43_transfer_shape *shape
);
int fwlab_c43_identify_recipe_valid(
    const struct fwlab_c43_identify_recipe *recipe
);
int fwlab_c43_block_intent_valid(
    const struct fwlab_c43_block_intent *intent
);
int fwlab_c43_policy_plan_valid(const struct fwlab_c43_policy_plan *plan);
int fwlab_c43_completion_witness_valid(
    const struct fwlab_c43_completion_witness *witness
);

enum fwlab_c43_api_result fwlab_c43_policy_begin(
    const struct fwlab_nvme_profile *profile,
    const struct fwlab_c43_policy_request *request,
    struct fwlab_c43_policy_plan *plan
);

enum fwlab_c43_api_result fwlab_c43_identify_encode(
    const struct fwlab_c43_identify_recipe *recipe,
    uint8_t *output,
    size_t output_size
);

#endif /* FWLAB_PORTABLE_NVME_POLICY_H */
