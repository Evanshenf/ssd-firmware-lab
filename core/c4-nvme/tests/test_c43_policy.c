/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fwlab/portable/nvme_codec.h"
#include "fwlab/portable/nvme_policy.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "C4.3 policy check failed at line %d\n", __LINE__);\
            return 1;                                                           \
        }                                                                       \
    } while (0)

static int bytes_zero(const void *value, size_t size)
{
    const unsigned char *bytes = value;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static struct fwlab_nvme_profile fixed_profile(void)
{
    struct fwlab_nvme_profile profile;

    fwlab_nvme_profile_fixed(&profile);
    return profile;
}

static int kind_has_data(uint8_t kind)
{
    return kind <= FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST ||
           kind == FWLAB_C43_REQUEST_READ ||
           kind == FWLAB_C43_REQUEST_WRITE;
}

static int kind_is_io(uint8_t kind)
{
    return kind == FWLAB_C43_REQUEST_READ ||
           kind == FWLAB_C43_REQUEST_WRITE ||
           kind == FWLAB_C43_REQUEST_FLUSH;
}

static struct fwlab_c43_policy_request request_fixed(uint8_t kind)
{
    struct fwlab_c43_policy_request request = {0};

    request.version = FWLAB_C43_POLICY_VERSION;
    request.size = sizeof(request);
    request.handle.instance_nonce = UINT64_C(0xc430000000000001);
    request.handle.command_uid = kind;
    request.handle.controller_epoch = 1;
    request.handle.generation = 1;
    request.origin.word[0] = UINT64_C(0x1111000000000000) + kind;
    request.origin.word[1] = UINT64_C(0x2222000000000000) + kind;
    request.transaction_uid = kind;
    request.kind = kind;
    request.queue_class = kind_is_io(kind) ? FWLAB_NVME_QUEUE_IO
                                           : FWLAB_NVME_QUEUE_ADMIN;
    request.pointer_format = FWLAB_NVME_DATA_POINTER_PRP;
    request.data_present = (uint8_t)kind_has_data(kind);
    if (kind == FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE ||
        kind == FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST ||
        kind_is_io(kind)) {
        request.namespace_id = 1;
    }
    if (kind == FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES) {
        request.feature_selector = FWLAB_C43_FEATURE_NUMBER_OF_QUEUES;
    }
    if (kind == FWLAB_C43_REQUEST_CREATE_IO_CQ ||
        kind == FWLAB_C43_REQUEST_CREATE_IO_SQ) {
        request.queue_entries = 4;
    }
    return request;
}

static int policy(
    const struct fwlab_nvme_profile *profile,
    const struct fwlab_c43_policy_request *request,
    struct fwlab_c43_policy_plan *plan)
{
    return fwlab_c43_policy_begin(profile, request, plan) ==
               FWLAB_C43_API_OK &&
           fwlab_c43_policy_plan_valid(plan);
}

static int expect_error(
    const struct fwlab_nvme_profile *profile,
    const struct fwlab_c43_policy_request *request,
    uint32_t status,
    uint8_t dnr)
{
    struct fwlab_c43_policy_plan plan;

    return policy(profile, request, &plan) &&
           plan.kind == FWLAB_C43_PLAN_IMMEDIATE &&
           plan.semantic_status == status && plan.dnr == dnr &&
           plan.result_dword0 == 0 && plan.actual_length == 0 &&
           plan.required_witness_mask == 0 &&
           plan.satisfied_witness_mask == 0 &&
           plan.effect_class == FWLAB_NVME_EFFECT_NONE;
}

static void test_put_u32(uint8_t *output, size_t offset, uint32_t value)
{
    output[offset] = (uint8_t)value;
    output[offset + 1] = (uint8_t)(value >> 8);
    output[offset + 2] = (uint8_t)(value >> 16);
    output[offset + 3] = (uint8_t)(value >> 24);
}

static void test_put_u64(uint8_t *output, size_t offset, uint64_t value)
{
    uint32_t index;

    for (index = 0; index < 8; ++index) {
        output[offset + index] = (uint8_t)(value >> (index * 8));
    }
}

static uint32_t test_get_u32(const uint8_t *input, size_t offset)
{
    return (uint32_t)input[offset] |
           ((uint32_t)input[offset + 1] << 8) |
           ((uint32_t)input[offset + 2] << 16) |
           ((uint32_t)input[offset + 3] << 24);
}

static uint64_t test_get_u64(const uint8_t *input, size_t offset)
{
    uint64_t value = 0;
    uint32_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)input[offset + index] << (index * 8);
    }
    return value;
}

static void expected_identify(
    uint32_t kind,
    uint32_t namespace_id,
    uint8_t output[FWLAB_C43_IDENTIFY_BYTES])
{
    static const char expected_serial[] = "FWLABC43P1-000000001";
    static const char expected_model[] = "SSD Firmware Lab C43-P1";
    static const char expected_firmware[] = "C43P1";

    memset(output, 0, FWLAB_C43_IDENTIFY_BYTES);
    if (kind == FWLAB_C43_IDENTIFY_CONTROLLER) {
        memset(output + 4, ' ', 20);
        memcpy(output + 4, expected_serial, sizeof(expected_serial) - 1);
        memset(output + 24, ' ', 40);
        memcpy(output + 24, expected_model, sizeof(expected_model) - 1);
        memset(output + 64, ' ', 8);
        memcpy(output + 64, expected_firmware,
               sizeof(expected_firmware) - 1);
        test_put_u32(output, 516, 1);
        output[525] = 1;
    } else if (kind == FWLAB_C43_IDENTIFY_NAMESPACE) {
        test_put_u64(output, 0, 8);
        test_put_u64(output, 8, 8);
        test_put_u64(output, 16, 8);
        output[130] = 9;
    } else if (kind == FWLAB_C43_IDENTIFY_ACTIVE_NAMESPACE_LIST &&
               namespace_id < 1) {
        test_put_u32(output, 0, 1);
    }
}

static int test_identify(void)
{
    union {
        struct fwlab_c43_identify_recipe recipe;
        uint8_t output[FWLAB_C43_IDENTIFY_BYTES];
    } alias;
    const struct fwlab_nvme_profile profile = fixed_profile();
    uint8_t actual[FWLAB_C43_IDENTIFY_BYTES];
    uint8_t expected[FWLAB_C43_IDENTIFY_BYTES];
    uint8_t before[FWLAB_C43_IDENTIFY_BYTES];
    uint8_t alias_before[sizeof(alias)];
    uint8_t request_kind;

    for (request_kind = FWLAB_C43_REQUEST_IDENTIFY_CONTROLLER;
         request_kind <=
             FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST;
         ++request_kind) {
        struct fwlab_c43_policy_request request =
            request_fixed(request_kind);
        struct fwlab_c43_policy_plan plan;

        CHECK(policy(&profile, &request, &plan));
        CHECK(plan.kind == FWLAB_C43_PLAN_PAYLOAD &&
              plan.semantic_status == FWLAB_C43_STATUS_SUCCESS &&
              plan.actual_length == FWLAB_C43_IDENTIFY_BYTES &&
              plan.required_witness_mask ==
                  (FWLAB_C43_WITNESS_PAYLOAD_READY |
                   FWLAB_C43_WITNESS_DMA_OUT_COMPLETE) &&
              plan.satisfied_witness_mask == 0 &&
              plan.shape.direction ==
                  FWLAB_C43_TRANSFER_CONTROLLER_TO_HOST &&
              plan.shape.data_bytes == FWLAB_C43_IDENTIFY_BYTES &&
              plan.identify.kind == request_kind &&
              plan.identify.namespace_id == request.namespace_id);
        memset(actual, 0xa5, sizeof(actual));
        CHECK(fwlab_c43_identify_encode(
                  &plan.identify, actual, sizeof(actual)) ==
              FWLAB_C43_API_OK);
        expected_identify(plan.identify.kind, plan.identify.namespace_id,
                          expected);
        CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
        if (plan.identify.kind == FWLAB_C43_IDENTIFY_CONTROLLER) {
            CHECK(memcmp(actual + 4, "FWLABC43P1-000000001", 20) == 0 &&
                  memcmp(actual + 24, "SSD Firmware Lab C43-P1", 23) == 0 &&
                  memcmp(actual + 64, "C43P1", 5) == 0 &&
                  test_get_u32(actual, 516) == 1 && actual[525] == 1);
        } else if (plan.identify.kind == FWLAB_C43_IDENTIFY_NAMESPACE) {
            CHECK(test_get_u64(actual, 0) == 8 &&
                  test_get_u64(actual, 8) == 8 &&
                  test_get_u64(actual, 16) == 8 && actual[130] == 9);
        } else if (plan.identify.kind ==
                   FWLAB_C43_IDENTIFY_ACTIVE_NAMESPACE_LIST) {
            CHECK(test_get_u32(actual, 0) == 1);
        }
    }

    {
        struct fwlab_c43_policy_request request = request_fixed(
            FWLAB_C43_REQUEST_IDENTIFY_ACTIVE_NAMESPACE_LIST);
        struct fwlab_c43_policy_plan plan;

        request.namespace_id = 1;
        CHECK(policy(&profile, &request, &plan));
        CHECK(fwlab_c43_identify_encode(
                  &plan.identify, actual, sizeof(actual)) ==
              FWLAB_C43_API_OK);
        CHECK(bytes_zero(actual, sizeof(actual)));
    }

    {
        struct fwlab_c43_identify_recipe recipe = {0};

        recipe.version = FWLAB_C43_POLICY_VERSION;
        recipe.size = sizeof(recipe);
        recipe.kind = FWLAB_C43_IDENTIFY_CONTROLLER;
        recipe.payload_bytes = FWLAB_C43_IDENTIFY_BYTES;
        recipe.identity_version = 1;
        memset(actual, 0xa5, sizeof(actual));
        memcpy(before, actual, sizeof(before));
        CHECK(fwlab_c43_identify_encode(
                  &recipe, actual, sizeof(actual) - 1) ==
              FWLAB_C43_API_INVALID);
        CHECK(memcmp(actual, before, sizeof(actual)) == 0);
        recipe.reserved1[0] = 1;
        CHECK(fwlab_c43_identify_encode(
                  &recipe, actual, sizeof(actual)) ==
              FWLAB_C43_API_INVALID);
        CHECK(memcmp(actual, before, sizeof(actual)) == 0);
    }

    memset(&alias, 0, sizeof(alias));
    alias.recipe.version = FWLAB_C43_POLICY_VERSION;
    alias.recipe.size = sizeof(alias.recipe);
    alias.recipe.kind = FWLAB_C43_IDENTIFY_CONTROLLER;
    alias.recipe.payload_bytes = FWLAB_C43_IDENTIFY_BYTES;
    alias.recipe.identity_version = 1;
    memcpy(alias_before, &alias, sizeof(alias));
    CHECK(fwlab_c43_identify_encode(
              &alias.recipe, alias.output, sizeof(alias.output)) ==
          FWLAB_C43_API_INVALID);
    CHECK(memcmp(alias_before, &alias, sizeof(alias)) == 0);
    return 0;
}

static int test_common_status(void)
{
    const struct fwlab_nvme_profile profile = fixed_profile();
    struct fwlab_c43_policy_request request =
        request_fixed(FWLAB_C43_REQUEST_READ);

    request.queue_class = FWLAB_NVME_QUEUE_ADMIN;
    CHECK(expect_error(&profile, &request,
                       FWLAB_C43_STATUS_UNSUPPORTED_COMMAND, 1));
    request = request_fixed(FWLAB_C43_REQUEST_UNSUPPORTED);
    CHECK(expect_error(&profile, &request,
                       FWLAB_C43_STATUS_UNSUPPORTED_COMMAND, 1));
    request = request_fixed(FWLAB_C43_REQUEST_READ);
    request.fuse = FWLAB_NVME_FUSE_FIRST;
    CHECK(expect_error(&profile, &request, FWLAB_C43_STATUS_INVALID_FIELD, 1));
    request = request_fixed(FWLAB_C43_REQUEST_READ);
    request.pointer_format = FWLAB_NVME_DATA_POINTER_UNSUPPORTED_1;
    CHECK(expect_error(&profile, &request, FWLAB_C43_STATUS_INVALID_FIELD, 1));
    request = request_fixed(FWLAB_C43_REQUEST_READ);
    request.metadata_present = 1;
    CHECK(expect_error(&profile, &request, FWLAB_C43_STATUS_INVALID_FIELD, 1));
    request = request_fixed(FWLAB_C43_REQUEST_READ);
    request.data_present = 0;
    CHECK(expect_error(&profile, &request, FWLAB_C43_STATUS_INVALID_FIELD, 1));
    request = request_fixed(FWLAB_C43_REQUEST_FLUSH);
    request.data_present = 1;
    CHECK(expect_error(&profile, &request, FWLAB_C43_STATUS_INVALID_FIELD, 1));
    request = request_fixed(FWLAB_C43_REQUEST_READ);
    request.fua = 1;
    CHECK(expect_error(&profile, &request, FWLAB_C43_STATUS_INVALID_FIELD, 1));
    request = request_fixed(FWLAB_C43_REQUEST_READ);
    request.reserved_bits_present = 1;
    CHECK(expect_error(&profile, &request, FWLAB_C43_STATUS_INVALID_FIELD, 1));
    request = request_fixed(FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES);
    request.save = 1;
    CHECK(expect_error(&profile, &request, FWLAB_C43_STATUS_INVALID_FIELD, 1));
    request = request_fixed(FWLAB_C43_REQUEST_READ);
    request.namespace_id = 2;
    CHECK(expect_error(&profile, &request,
                       FWLAB_C43_STATUS_INVALID_NAMESPACE, 1));

    request = request_fixed(FWLAB_C43_REQUEST_READ);
    request.transport_fault = FWLAB_NVME_TRANSPORT_UNSAFE_GRAPH;
    CHECK(expect_error(&profile, &request,
                       FWLAB_C43_STATUS_TRANSFER_FAILURE, 1));
    request.transport_fault = FWLAB_NVME_TRANSPORT_UNSUPPORTED_FORMAT;
    CHECK(expect_error(&profile, &request,
                       FWLAB_C43_STATUS_INVALID_FIELD, 1));
    request.transport_fault = FWLAB_NVME_TRANSPORT_QUEUE_MEMORY;
    CHECK(expect_error(&profile, &request,
                       FWLAB_C43_STATUS_TRANSFER_FAILURE, 0));
    request.transport_fault = FWLAB_NVME_TRANSPORT_STALE_GENERATION;
    CHECK(expect_error(&profile, &request,
                       FWLAB_C43_STATUS_COMMAND_SEQUENCE, 1));
    return 0;
}

static int test_control_plans(void)
{
    const struct fwlab_nvme_profile profile = fixed_profile();
    struct fwlab_c43_policy_request request;
    struct fwlab_c43_policy_plan plan;
    uint8_t kind;

    request = request_fixed(FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES);
    request.requested_cq_count = UINT16_MAX;
    request.requested_sq_count = UINT16_MAX;
    CHECK(policy(&profile, &request, &plan));
    CHECK(plan.kind == FWLAB_C43_PLAN_IMMEDIATE &&
          plan.semantic_status == FWLAB_C43_STATUS_SUCCESS &&
          plan.result_dword0 == 0);
    request.feature_selector = FWLAB_C43_FEATURE_NUMBER_OF_QUEUES - 1;
    CHECK(expect_error(&profile, &request, FWLAB_C43_STATUS_INVALID_FIELD, 1));
    request = request_fixed(FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES);
    request.requested_cq_count = UINT32_C(65536);
    CHECK(expect_error(&profile, &request, FWLAB_C43_STATUS_INVALID_FIELD, 1));

    for (kind = FWLAB_C43_REQUEST_CREATE_IO_CQ;
         kind <= FWLAB_C43_REQUEST_DELETE_IO_SQ; ++kind) {
        request = request_fixed(kind);
        CHECK(policy(&profile, &request, &plan));
        CHECK(plan.kind == FWLAB_C43_PLAN_QUEUE_EFFECT &&
              plan.required_witness_mask ==
                  FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED &&
              plan.satisfied_witness_mask == 0);
    }
    request = request_fixed(FWLAB_C43_REQUEST_CREATE_IO_CQ);
    request.queue_entries = 3;
    CHECK(expect_error(&profile, &request,
                       FWLAB_C43_STATUS_INVALID_QUEUE_SIZE, 1));
    request = request_fixed(FWLAB_C43_REQUEST_ABORT);
    CHECK(policy(&profile, &request, &plan));
    CHECK(plan.kind == FWLAB_C43_PLAN_ABORT_RESOLVE &&
          plan.required_witness_mask == 0 && plan.result_dword0 == 0);
    return 0;
}

static int check_block_success(
    const struct fwlab_nvme_profile *profile,
    struct fwlab_c43_policy_request *request,
    uint32_t operation,
    uint32_t count,
    uint32_t bytes,
    uint32_t witness)
{
    struct fwlab_c43_policy_plan plan;
    struct fwlab_c43_policy_plan expected = {0};

    expected.version = FWLAB_C43_POLICY_VERSION;
    expected.size = sizeof(expected);
    expected.command = request->handle;
    expected.origin = request->origin;
    expected.transaction_uid = request->transaction_uid;
    expected.kind = FWLAB_C43_PLAN_BLOCK;
    expected.semantic_status = FWLAB_C43_STATUS_SUCCESS;
    expected.actual_length = bytes;
    expected.required_witness_mask = witness;
    expected.shape.version = FWLAB_C43_POLICY_VERSION;
    expected.shape.size = sizeof(expected.shape);
    expected.block.version = FWLAB_C43_POLICY_VERSION;
    expected.block.size = sizeof(expected.block);
    expected.block.command = request->handle;
    expected.block.origin = request->origin;
    expected.block.slba = request->slba;
    expected.block.namespace_id = 1;
    expected.block.operation = operation;
    expected.block.lba_count = count;
    expected.block.data_bytes = bytes;
    if (operation == FWLAB_C43_BLOCK_READ) {
        expected.shape.direction = FWLAB_C43_TRANSFER_CONTROLLER_TO_HOST;
        expected.block.durability = FWLAB_C43_DURABILITY_DEFAULT;
    } else if (operation == FWLAB_C43_BLOCK_WRITE) {
        expected.shape.direction = FWLAB_C43_TRANSFER_HOST_TO_CONTROLLER;
        expected.block.durability = request->fua
                                        ? FWLAB_C43_DURABILITY_REQUIRE_SELF
                                        : FWLAB_C43_DURABILITY_DEFAULT;
    } else {
        expected.block.durability = FWLAB_C43_DURABILITY_FIXED_FRONTIER;
    }
    if (operation != FWLAB_C43_BLOCK_FLUSH) {
        expected.shape.data_bytes = bytes;
        expected.shape.lba_bytes = profile->lba_bytes;
        expected.shape.lba_count = count;
        expected.shape.data_pointer_required = 1;
    }

    return policy(profile, request, &plan) &&
           memcmp(&plan, &expected, sizeof(plan)) == 0;
}

static int test_block_plans(void)
{
    const struct fwlab_nvme_profile profile = fixed_profile();
    struct fwlab_c43_policy_request request =
        request_fixed(FWLAB_C43_REQUEST_READ);

    request.slba = 7;
    CHECK(check_block_success(
        &profile, &request, FWLAB_C43_BLOCK_READ, 1, 512,
        FWLAB_C43_WITNESS_BLOCK_READ_READY |
            FWLAB_C43_WITNESS_DMA_OUT_COMPLETE));
    request.zero_based_nlb = 1;
    CHECK(expect_error(&profile, &request, FWLAB_C43_STATUS_LBA_RANGE, 1));
    request.slba = 0;
    request.zero_based_nlb = 7;
    CHECK(check_block_success(
        &profile, &request, FWLAB_C43_BLOCK_READ, 8, 4096,
        FWLAB_C43_WITNESS_BLOCK_READ_READY |
            FWLAB_C43_WITNESS_DMA_OUT_COMPLETE));
    request.slba = UINT64_MAX;
    request.zero_based_nlb = 0;
    CHECK(expect_error(&profile, &request, FWLAB_C43_STATUS_LBA_RANGE, 1));
    request.slba = 0;
    request.zero_based_nlb = UINT32_MAX;
    CHECK(expect_error(&profile, &request, FWLAB_C43_STATUS_LBA_RANGE, 1));

    request = request_fixed(FWLAB_C43_REQUEST_WRITE);
    CHECK(check_block_success(
        &profile, &request, FWLAB_C43_BLOCK_WRITE, 1, 512,
        FWLAB_C43_WITNESS_DMA_IN_COMPLETE |
            FWLAB_C43_WITNESS_BLOCK_WRITE_COMPLETE));
    request.fua = 1;
    CHECK(check_block_success(
        &profile, &request, FWLAB_C43_BLOCK_WRITE, 1, 512,
        FWLAB_C43_WITNESS_DMA_IN_COMPLETE |
            FWLAB_C43_WITNESS_BLOCK_WRITE_COMPLETE |
            FWLAB_C43_WITNESS_DURABILITY_COMPLETE));

    request = request_fixed(FWLAB_C43_REQUEST_FLUSH);
    CHECK(check_block_success(
        &profile, &request, FWLAB_C43_BLOCK_FLUSH, 0, 0,
        FWLAB_C43_WITNESS_DURABILITY_COMPLETE));
    return 0;
}

static int test_api_atomicity(void)
{
    union {
        struct fwlab_c43_policy_request request;
        struct fwlab_c43_policy_plan plan;
    } alias;
    struct fwlab_nvme_profile profile = fixed_profile();
    struct fwlab_c43_policy_request request =
        request_fixed(FWLAB_C43_REQUEST_READ);
    struct fwlab_c43_policy_plan plan;
    struct fwlab_c43_policy_plan before;
    struct fwlab_c43_policy_plan repeat;
    unsigned char alias_before[sizeof(alias)];

    memset(&plan, 0xa5, sizeof(plan));
    memcpy(&before, &plan, sizeof(plan));
    request.reserved2 = 1;
    CHECK(fwlab_c43_policy_begin(&profile, &request, &plan) ==
          FWLAB_C43_API_INVALID);
    CHECK(memcmp(&plan, &before, sizeof(plan)) == 0);
    request = request_fixed(FWLAB_C43_REQUEST_READ);
    ++profile.maximum_transfer_bytes;
    CHECK(fwlab_c43_policy_begin(&profile, &request, &plan) ==
          FWLAB_C43_API_INVALID);
    CHECK(memcmp(&plan, &before, sizeof(plan)) == 0);

    memset(&alias, 0, sizeof(alias));
    alias.request = request_fixed(FWLAB_C43_REQUEST_READ);
    memcpy(alias_before, &alias, sizeof(alias));
    profile = fixed_profile();
    CHECK(fwlab_c43_policy_begin(
              &profile, &alias.request, &alias.plan) ==
          FWLAB_C43_API_INVALID);
    CHECK(memcmp(alias_before, &alias, sizeof(alias)) == 0);

    request = request_fixed(FWLAB_C43_REQUEST_WRITE);
    memset(&plan, 0xa5, sizeof(plan));
    memset(&repeat, 0x5a, sizeof(repeat));
    CHECK(policy(&profile, &request, &plan));
    CHECK(policy(&profile, &request, &repeat));
    CHECK(memcmp(&plan, &repeat, sizeof(plan)) == 0);
    return 0;
}

int main(void)
{
    CHECK(test_identify() == 0);
    CHECK(test_common_status() == 0);
    CHECK(test_control_plans() == 0);
    CHECK(test_block_plans() == 0);
    CHECK(test_api_atomicity() == 0);
    puts("C4.3 phase3 policy: PASS identify=4 arithmetic=5 held-data=4");
    return 0;
}
