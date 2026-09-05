/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _GNU_SOURCE

#include "../j0_internal.h"
#include "m3p_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "J0-B check failed at %s:%d: %s\n", __FILE__, __LINE__, \
            #expression); return 0; } } while (0)
#define CHECK_MAIN(expression) do { if (!(expression)) { \
    fprintf(stderr, "J0-B check failed at %s:%d: %s\n", __FILE__, __LINE__, \
            #expression); return 1; } } while (0)

struct digest_arguments {
    const char *lifecycle;
    const char *host;
    const char *m3p;
    const char *nfc;
    const char *file;
    const char *elf;
};

struct profile_receipt {
    uint64_t adapter;
    uint64_t lifecycle_instance;
    uint64_t m3p_instance;
    uint64_t block_provider;
    uint64_t nfc_instance;
    uint64_t ticket;
    uint64_t authority;
    uint64_t dma;
    uint64_t buffer;
    uint64_t block;
    uint64_t nfc_operations;
    uint64_t ppas;
    uint64_t frontier;
    uint64_t intent;
    uint64_t readback;
};

struct image_context {
    char directory[128];
    const char *name;
    int directory_fd;
    void *arena;
    struct fwlab_file_nand_v0 *file;
    struct fwlab_file_nand_holder_v0 holder;
    uint8_t uuid[16];
};

struct test_fake_block {
    struct fwlab_block_service_v0 service;
    struct fwlab_controller_buffer_port_v0 buffer;
    struct fwlab_block_request_v0 request;
    struct fwlab_block_status_v0 status;
    uint64_t data_digest;
    uint64_t lifecycle_nonce;
    uint32_t execution_epoch;
    uint32_t close_calls;
    uint32_t quiescent_calls;
    uint8_t request_valid;
    uint8_t retire_started;
    uint8_t drained;
    uint8_t close_started;
};

struct test_fake_data {
    struct fwlab_host_data_port_v0 port;
    struct fwlab_nvme_command_handle command;
    struct fwlab_nvme_origin_token origin;
    struct fwlab_host_dma_authority_ref_v0 authority;
    struct fwlab_dma_op_token_v0 operation;
    struct fwlab_dma_request_v0 request;
    struct fwlab_dma_status_v0 status;
    uint8_t source[J0_MAX_TRANSFER_BYTES];
    uint32_t exact_bytes;
    uint64_t lifecycle_nonce;
    uint32_t execution_epoch;
    uint8_t authority_live;
    uint8_t token_reserved;
    uint8_t request_valid;
    uint8_t retire_started;
    uint8_t drained;
    uint8_t close_started;
};

static int lowercase_sha(const char *value)
{
    size_t index;

    if (value == NULL || strlen(value) != 64) {
        return 0;
    }
    for (index = 0; index < 64; ++index) {
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f'))) {
            return 0;
        }
    }
    return 1;
}

static uint64_t digest_bytes(uint64_t digest, const void *value, size_t size)
{
    const uint8_t *bytes = value;
    size_t index;

    for (index = 0; index < size; ++index) {
        digest ^= bytes[index];
        digest *= UINT64_C(1099511628211);
    }
    return digest;
}

static uint64_t bytes_digest(const void *value, size_t size)
{
    return digest_bytes(UINT64_C(1469598103934665603), value, size);
}

static int block_token_equal(
    const struct fwlab_block_op_token_v0 *left,
    const struct fwlab_block_op_token_v0 *right)
{
    return left != NULL && right != NULL &&
           left->version == right->version && left->size == right->size &&
           left->type_tag == right->type_tag &&
           j0_action_token_equal(&left->action, &right->action) &&
           left->provider_nonce == right->provider_nonce &&
           left->generation == right->generation &&
           j0_bytes_zero(left->reserved, sizeof(left->reserved)) &&
           j0_bytes_zero(right->reserved, sizeof(right->reserved));
}

static enum fwlab_spine_result_v0 fake_block_submit(
    void *context, const struct fwlab_block_request_v0 *request,
    struct fwlab_block_submit_result_v0 *result)
{
    struct test_fake_block *fake = context;
    uint8_t bytes[J0_MAX_TRANSFER_BYTES];

    if (fake == NULL || request == NULL || result == NULL ||
        !fwlab_block_request_v0_valid(request) ||
        request->operation != FWLAB_BLOCK_V0_WRITE ||
        request->operation_token.provider_nonce !=
            fake->service.provider_nonce ||
        request->operation_token.generation != fake->service.generation ||
        request->buffer_span.length > sizeof(bytes) || fake->close_started) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (fake->request_valid) {
        if (memcmp(&fake->request, request, sizeof(*request)) != 0) {
            return FWLAB_SPINE_V0_POISONED;
        }
    } else {
        memset(bytes, 0, sizeof(bytes));
        if (fake->buffer.ops->read(
                fake->buffer.context, &request->buffer,
                &request->buffer_span, bytes,
                request->buffer_span.length) !=
            FWLAB_CONTROLLER_BUFFER_V0_OK) {
            return FWLAB_SPINE_V0_POISONED;
        }
        fake->data_digest =
            bytes_digest(bytes, request->buffer_span.length);
        fake->request = *request;
        fake->request_valid = 1;
        memset(&fake->status, 0, sizeof(fake->status));
        fake->status.version = FWLAB_BLOCK_SERVICE_V0_VERSION;
        fake->status.size = (uint16_t)sizeof(fake->status);
        fake->status.operation_token = request->operation_token;
        fake->status.state = FWLAB_BLOCK_V0_STATE_TERMINAL;
        fake->status.outcome = FWLAB_BLOCK_V0_SUCCEEDED;
        fake->status.effect = FWLAB_BLOCK_V0_EFFECT_FULL;
        fake->status.completed_lbas = request->lba_count;
        fake->status.data_bytes = request->buffer_span.length;
        fake->status.durability_witness =
            request->durability == FWLAB_BLOCK_V0_DURABILITY_SELF
                ? FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE
                : FWLAB_BLOCK_V0_WITNESS_VOLATILE;
        if (fake->status.durability_witness ==
            FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE) {
            fake->status.frontier.word[0] = fake->service.provider_nonce;
            fake->status.frontier.word[1] = 1;
        }
    }
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    result->size = (uint16_t)sizeof(*result);
    result->operation_token = request->operation_token;
    result->disposition = FWLAB_HOST_ACTION_V0_ACCEPTED;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_block_query(
    void *context, const struct fwlab_block_op_token_v0 *operation,
    struct fwlab_block_status_v0 *status)
{
    struct test_fake_block *fake = context;

    if (fake == NULL || operation == NULL || status == NULL ||
        !fake->request_valid ||
        !block_token_equal(operation, &fake->request.operation_token)) {
        return FWLAB_SPINE_V0_STALE;
    }
    *status = fake->status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_block_cancel(
    void *context, const struct fwlab_block_op_token_v0 *operation)
{
    struct test_fake_block *fake = context;

    if (fake == NULL || operation == NULL || !fake->request_valid ||
        !block_token_equal(operation, &fake->request.operation_token)) {
        return FWLAB_SPINE_V0_STALE;
    }
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_block_retire_start(
    void *context, const struct fwlab_block_op_token_v0 *operation)
{
    struct test_fake_block *fake = context;

    if (fake == NULL || operation == NULL || !fake->request_valid ||
        !block_token_equal(operation, &fake->request.operation_token)) {
        return FWLAB_SPINE_V0_STALE;
    }
    fake->retire_started = 1;
    fake->status.state = FWLAB_BLOCK_V0_STATE_DRAINING;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_block_retire_query(
    void *context, const struct fwlab_block_op_token_v0 *operation,
    struct fwlab_block_status_v0 *status)
{
    struct test_fake_block *fake = context;

    if (fake == NULL || operation == NULL || status == NULL ||
        !fake->retire_started ||
        !block_token_equal(operation, &fake->request.operation_token)) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    fake->drained = 1;
    fake->status.state = FWLAB_BLOCK_V0_STATE_RETIRED;
    *status = fake->status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_block_epoch_close(
    void *context, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch)
{
    struct test_fake_block *fake = context;

    if (fake != NULL) {
        ++fake->close_calls;
    }
    if (fake == NULL || lifecycle_instance_nonce == 0 ||
        old_execution_epoch == 0) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (fake->close_started) {
        return fake->lifecycle_nonce == lifecycle_instance_nonce &&
                       fake->execution_epoch == old_execution_epoch
                   ? FWLAB_SPINE_V0_OK
                   : FWLAB_SPINE_V0_STALE;
    }
    fake->close_started = 1;
    fake->lifecycle_nonce = lifecycle_instance_nonce;
    fake->execution_epoch = old_execution_epoch;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_block_epoch_quiescent(
    void *context, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch,
    struct fwlab_block_epoch_status_v0 *status)
{
    struct test_fake_block *fake = context;

    if (fake != NULL) {
        ++fake->quiescent_calls;
    }
    if (fake == NULL || status == NULL || !fake->close_started ||
        fake->lifecycle_nonce != lifecycle_instance_nonce ||
        fake->execution_epoch != old_execution_epoch) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(status, 0, sizeof(*status));
    status->version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    status->size = (uint16_t)sizeof(*status);
    status->lifecycle_instance_nonce = lifecycle_instance_nonce;
    status->execution_epoch = old_execution_epoch;
    status->aggregate_operations =
        (uint32_t)(fake->request_valid && !fake->drained);
    status->admission_closed = 1;
    status->quiescent = (uint8_t)(status->aggregate_operations == 0);
    if (status->quiescent) {
        status->aggregate_proof[0] = fake->service.provider_nonce;
        status->aggregate_proof[1] = old_execution_epoch;
    }
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_block_service_ops_v0 fake_block_ops = {
    .version = FWLAB_BLOCK_SERVICE_V0_VERSION,
    .size = sizeof(struct fwlab_block_service_ops_v0),
    .submit = fake_block_submit,
    .query = fake_block_query,
    .cancel = fake_block_cancel,
    .retire_start = fake_block_retire_start,
    .retire_query = fake_block_retire_query,
    .epoch_close = fake_block_epoch_close,
    .epoch_quiescent = fake_block_epoch_quiescent,
};

static void fake_block_init(
    struct test_fake_block *fake,
    const struct fwlab_controller_buffer_port_v0 *buffer)
{
    memset(fake, 0, sizeof(*fake));
    fake->buffer = *buffer;
    fake->service.ops = &fake_block_ops;
    fake->service.context = fake;
    fake->service.provider_nonce = UINT64_C(0x4a3046414b45424c);
    fake->service.generation = 1;
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

static enum fwlab_spine_result_v0 fake_authority_mint(
    void *context, const struct fwlab_host_dma_mint_request_v0 *request,
    struct fwlab_host_dma_authority_ref_v0 *authority)
{
    struct test_fake_data *fake = context;

    if (fake == NULL || request == NULL || authority == NULL ||
        !fwlab_host_dma_mint_request_v0_valid(request) ||
        !j0_handle_equal(&fake->command, &request->command) ||
        !j0_origin_equal(&fake->origin, &request->origin) ||
        request->direction != FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER ||
        request->exact_bytes != fake->exact_bytes || fake->close_started) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (!fake->authority_live) {
        memset(&fake->authority, 0, sizeof(fake->authority));
        fake->authority.version = FWLAB_HOST_DATA_V0_VERSION;
        fake->authority.size = (uint16_t)sizeof(fake->authority);
        fake->authority.type_tag = FWLAB_HOST_DMA_AUTHORITY_V0_TAG;
        fake->authority.issuer_nonce =
            fake->port.authority_issuer_nonce;
        fake->authority.authority_uid = UINT64_C(51001);
        fake->authority.generation = fake->port.generation;
        fake->authority.exact_bytes = request->exact_bytes;
        fake->authority.direction = request->direction;
        fake->authority_live = 1;
    }
    *authority = fake->authority;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_authority_release(
    void *context,
    const struct fwlab_host_dma_authority_ref_v0 *authority)
{
    struct test_fake_data *fake = context;

    if (fake == NULL || authority == NULL ||
        memcmp(authority, &fake->authority, sizeof(*authority)) != 0) {
        return FWLAB_SPINE_V0_STALE;
    }
    fake->authority_live = 0;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_dma_token_reserve(
    void *context, const struct fwlab_host_action_token_v0 *action,
    struct fwlab_dma_op_token_v0 *operation)
{
    struct test_fake_data *fake = context;

    if (fake == NULL || action == NULL || operation == NULL ||
        !fwlab_host_action_token_v0_valid(action) ||
        action->kind != FWLAB_HOST_ACTION_V0_DMA_IN ||
        fake->close_started) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (!fake->token_reserved) {
        memset(&fake->operation, 0, sizeof(fake->operation));
        fake->operation.version = FWLAB_HOST_DATA_V0_VERSION;
        fake->operation.size = (uint16_t)sizeof(fake->operation);
        fake->operation.type_tag = FWLAB_DMA_OP_TOKEN_V0_TAG;
        fake->operation.action = *action;
        fake->operation.issuer_nonce = fake->port.dma_issuer_nonce;
        fake->operation.operation_uid = UINT64_C(52001);
        fake->operation.generation = fake->port.generation;
        fake->token_reserved = 1;
    } else if (!j0_action_token_equal(&fake->operation.action, action)) {
        return FWLAB_SPINE_V0_POISONED;
    }
    *operation = fake->operation;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_dma_submit(
    void *context, const struct fwlab_dma_request_v0 *request,
    struct fwlab_dma_submit_result_v0 *result)
{
    struct test_fake_data *fake = context;

    if (fake == NULL || request == NULL || result == NULL ||
        !fwlab_dma_request_v0_valid(request) || !fake->authority_live ||
        !dma_token_equal(&request->operation, &fake->operation) ||
        memcmp(&request->authority, &fake->authority,
               sizeof(request->authority)) != 0 ||
        request->exact_bytes != fake->exact_bytes ||
        fake->port.buffer.ops->write(
            fake->port.buffer.context, &request->buffer, &request->span,
            fake->source, fake->exact_bytes) !=
            FWLAB_CONTROLLER_BUFFER_V0_OK) {
        return FWLAB_SPINE_V0_POISONED;
    }
    if (fake->request_valid &&
        memcmp(&fake->request, request, sizeof(*request)) != 0) {
        return FWLAB_SPINE_V0_POISONED;
    }
    fake->request = *request;
    fake->request_valid = 1;
    memset(&fake->status, 0, sizeof(fake->status));
    fake->status.version = FWLAB_HOST_DATA_V0_VERSION;
    fake->status.size = (uint16_t)sizeof(fake->status);
    fake->status.operation = fake->operation;
    fake->status.state = FWLAB_DMA_V0_STATE_TERMINAL;
    fake->status.terminal_kind = FWLAB_DMA_V0_SUCCEEDED;
    fake->status.effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL;
    fake->status.bytes_completed = fake->exact_bytes;
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_HOST_DATA_V0_VERSION;
    result->size = (uint16_t)sizeof(*result);
    result->operation = fake->operation;
    result->disposition = FWLAB_HOST_ACTION_V0_ACCEPTED;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_dma_query(
    void *context, const struct fwlab_dma_op_token_v0 *operation,
    struct fwlab_dma_status_v0 *status)
{
    struct test_fake_data *fake = context;

    if (fake == NULL || operation == NULL || status == NULL ||
        !fake->request_valid ||
        !dma_token_equal(operation, &fake->operation)) {
        return FWLAB_SPINE_V0_STALE;
    }
    *status = fake->status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_dma_cancel(
    void *context, const struct fwlab_dma_op_token_v0 *operation)
{
    struct test_fake_data *fake = context;

    return fake != NULL && fake->request_valid &&
                   dma_token_equal(operation, &fake->operation)
               ? FWLAB_SPINE_V0_OK
               : FWLAB_SPINE_V0_STALE;
}

static enum fwlab_spine_result_v0 fake_dma_retire_start(
    void *context, const struct fwlab_dma_op_token_v0 *operation)
{
    struct test_fake_data *fake = context;

    if (fake == NULL || !fake->request_valid ||
        !dma_token_equal(operation, &fake->operation)) {
        return FWLAB_SPINE_V0_STALE;
    }
    fake->retire_started = 1;
    fake->status.state = FWLAB_DMA_V0_STATE_DRAINING;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_dma_retire_query(
    void *context, const struct fwlab_dma_op_token_v0 *operation,
    struct fwlab_dma_status_v0 *status)
{
    struct test_fake_data *fake = context;

    if (fake == NULL || status == NULL || !fake->retire_started ||
        !dma_token_equal(operation, &fake->operation)) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    fake->drained = 1;
    fake->status.state = FWLAB_DMA_V0_STATE_DRAINED;
    *status = fake->status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_data_epoch_close(
    void *context, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch)
{
    struct test_fake_data *fake = context;

    if (fake == NULL || lifecycle_instance_nonce == 0 ||
        old_execution_epoch == 0) {
        return FWLAB_SPINE_V0_INVALID;
    }
    fake->close_started = 1;
    fake->lifecycle_nonce = lifecycle_instance_nonce;
    fake->execution_epoch = old_execution_epoch;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_data_epoch_quiescent(
    void *context, uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch,
    struct fwlab_host_data_epoch_status_v0 *status)
{
    struct test_fake_data *fake = context;

    if (fake == NULL || status == NULL || !fake->close_started ||
        fake->lifecycle_nonce != lifecycle_instance_nonce ||
        fake->execution_epoch != old_execution_epoch) {
        return FWLAB_SPINE_V0_INVALID;
    }
    memset(status, 0, sizeof(*status));
    status->version = FWLAB_HOST_DATA_V0_VERSION;
    status->size = (uint16_t)sizeof(*status);
    status->lifecycle_instance_nonce = lifecycle_instance_nonce;
    status->execution_epoch = old_execution_epoch;
    status->authority_refs = fake->authority_live;
    status->dma_operations =
        (uint32_t)(fake->request_valid && !fake->drained);
    status->admission_closed = 1;
    status->quiescent =
        (uint8_t)(status->authority_refs == 0 &&
                  status->dma_operations == 0);
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_host_data_ops_v0 fake_data_ops = {
    .version = FWLAB_HOST_DATA_V0_VERSION,
    .size = sizeof(struct fwlab_host_data_ops_v0),
    .authority_mint = fake_authority_mint,
    .authority_release = fake_authority_release,
    .token_reserve = fake_dma_token_reserve,
    .submit = fake_dma_submit,
    .query = fake_dma_query,
    .cancel = fake_dma_cancel,
    .retire_start = fake_dma_retire_start,
    .retire_query = fake_dma_retire_query,
    .epoch_close = fake_data_epoch_close,
    .epoch_quiescent = fake_data_epoch_quiescent,
};

static void fake_data_init(
    struct test_fake_data *fake, struct j0_runtime *runtime,
    const struct fwlab_nvme_command *command,
    const uint8_t *input, uint32_t exact_bytes)
{
    memset(fake, 0, sizeof(*fake));
    fake->command = command->handle;
    fake->origin = command->origin;
    fake->exact_bytes = exact_bytes;
    memcpy(fake->source, input, exact_bytes);
    fake->port.ops = &fake_data_ops;
    fake->port.context = fake;
    fake->port.buffer = runtime->buffer.port;
    fake->port.authority_issuer_nonce = UINT64_C(0x4a3046414b454844);
    fake->port.dma_issuer_nonce = UINT64_C(0x4a3046414b45444d);
    fake->port.generation = runtime->config.generation;
}

static void pattern_fill(uint8_t *bytes, size_t size, uint8_t seed)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = (uint8_t)(seed + index * 17u + (index >> 3));
    }
}

static int uuid_encode(const uint8_t uuid[16], char output[33])
{
    static const char hex[] = "0123456789abcdef";
    uint32_t index;

    for (index = 0; index < 16; ++index) {
        output[index * 2] = hex[uuid[index] >> 4];
        output[index * 2 + 1] = hex[uuid[index] & 0x0f];
    }
    output[32] = '\0';
    return 1;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

static int uuid_decode(const char *text, uint8_t uuid[16])
{
    uint32_t index;

    if (text == NULL || strlen(text) != 32) {
        return 0;
    }
    for (index = 0; index < 16; ++index) {
        int high = hex_value(text[index * 2]);
        int low = hex_value(text[index * 2 + 1]);

        if (high < 0 || low < 0) {
            return 0;
        }
        uuid[index] = (uint8_t)((high << 4) | low);
    }
    return 1;
}

static int parse_u64(const char *text, uint64_t *value)
{
    char *end = NULL;
    unsigned long long parsed;

    if (text == NULL || value == NULL || text[0] == '\0') {
        return 0;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }
    *value = (uint64_t)parsed;
    return 1;
}

static void *arena_allocate(size_t alignment, size_t size)
{
    size_t rounded;

    if (alignment == 0 || size == 0 || size > SIZE_MAX - (alignment - 1u)) {
        return NULL;
    }
    rounded = (size + alignment - 1u) & ~(alignment - 1u);
    return aligned_alloc(alignment, rounded);
}

static struct fwlab_nvme_command command_make(
    uint64_t instance_nonce, uint64_t command_uid, uint32_t profile,
    uint8_t opcode, uint64_t lba, uint32_t lba_count)
{
    struct fwlab_nvme_command command;

    memset(&command, 0, sizeof(command));
    command.version = FWLAB_NVME_COMMAND_VERSION;
    command.size = (uint16_t)sizeof(command);
    command.handle.instance_nonce = instance_nonce;
    command.handle.command_uid = command_uid;
    command.handle.controller_epoch = 1;
    command.handle.generation = 1;
    command.origin.word[0] = instance_nonce ^ UINT64_C(0x4f52494700000000);
    command.origin.word[1] = command_uid;
    command.trace_cookie = instance_nonce + command_uid;
    command.safety_generation = 1;
    command.namespace_id = 1;
    command.opcode = opcode;
    command.queue_class = FWLAB_NVME_QUEUE_IO;
    command.fuse = FWLAB_NVME_FUSE_NONE;
    command.data_pointer_format = FWLAB_NVME_DATA_POINTER_PRP;
    command.data_address_present = (uint8_t)(opcode != 0);
    if (opcode == 0) {
        return command;
    }
    command.command_dword10_15[0] = (uint32_t)lba;
    command.command_dword10_15[1] = (uint32_t)(lba >> 32);
    command.command_dword10_15[2] = lba_count - 1u;
    if (profile == J0_PROFILE_C43_P1 && opcode == 0x01) {
        command.command_dword10_15[2] &= UINT32_C(0x0000ffff);
    }
    return command;
}

static struct j0_host_transfer transfer_make(
    uint32_t direction, uint32_t exact_bytes, const uint8_t *input)
{
    struct j0_host_transfer transfer;

    memset(&transfer, 0, sizeof(transfer));
    transfer.version = J0_RUNTIME_VERSION;
    transfer.size = (uint16_t)sizeof(transfer);
    transfer.direction = direction;
    transfer.exact_bytes = exact_bytes;
    transfer.input = input;
    return transfer;
}

static int runtime_ready(struct j0_runtime *runtime)
{
    uint32_t iteration;

    for (iteration = 0; iteration < 400000; ++iteration) {
        uint32_t units = 0;

        CHECK(j0_runtime_step(runtime, 3, &units) == FWLAB_SPINE_V0_OK);
        CHECK(units == 3);
        if (runtime->ready) {
            return 1;
        }
    }
    return 0;
}

static int command_run(
    struct j0_runtime *runtime, uint32_t profile,
    const struct fwlab_nvme_command *command,
    const struct j0_host_transfer *transfer,
    struct fwlab_spine_command_ticket_v0 *ticket,
    struct fwlab_nvme_completion_intent *intent)
{
    struct fwlab_spine_command_ticket_v0 repeated;
    uint32_t iteration;

    CHECK(j0_runtime_admit_start(runtime, profile, command, transfer,
                                 ticket) == FWLAB_SPINE_V0_OK);
    memset(&repeated, 0, sizeof(repeated));
    CHECK(j0_runtime_admit_start(runtime, profile, command, transfer,
                                 &repeated) == FWLAB_SPINE_V0_OK);
    CHECK(fwlab_spine_command_ticket_v0_equal(ticket, &repeated));
    for (iteration = 0; iteration < 400000; ++iteration) {
        uint32_t units = 0;
        enum fwlab_spine_result_v0 result =
            j0_runtime_intent_read(runtime, ticket, intent);

        if (result == FWLAB_SPINE_V0_OK) {
            CHECK(intent->status_code == 0);
            CHECK(intent->status_code_type == 0);
            return 1;
        }
        CHECK(result == FWLAB_SPINE_V0_IN_PROGRESS);
        CHECK(j0_runtime_step(runtime, 3, &units) == FWLAB_SPINE_V0_OK);
        CHECK(units == 3);
    }
    return 0;
}

static struct j0_admission_record *record_for_ticket(
    struct j0_runtime *runtime,
    const struct fwlab_spine_command_ticket_v0 *ticket)
{
    uint32_t index;

    for (index = 0; index < J0_MAX_COMMANDS; ++index) {
        if (runtime->admission[index].occupied &&
            runtime->admission[index].lifecycle_owned &&
            fwlab_spine_command_ticket_v0_equal(
                &runtime->admission[index].ticket, ticket)) {
            return &runtime->admission[index];
        }
    }
    return NULL;
}

static uint64_t trace_digest(
    struct fwlab_nfc_model *model, uint32_t first, uint32_t last,
    int ppas_only)
{
    uint64_t digest = UINT64_C(1469598103934665603);
    uint32_t index;

    for (index = first; index < last; ++index) {
        struct fwlab_nfc_trace_entry entry;

        if (fwlab_nfc_model_trace_read(model, index, &entry) !=
            FWLAB_NFC_API_OK) {
            return 0;
        }
        if (ppas_only) {
            digest = digest_bytes(digest, &entry.ppa, sizeof(entry.ppa));
        } else {
            digest = digest_bytes(digest, &entry.kind, sizeof(entry.kind));
            digest = digest_bytes(digest, &entry.sequence,
                                  sizeof(entry.sequence));
            digest = digest_bytes(digest, &entry.operation,
                                  sizeof(entry.operation));
        }
    }
    return first == last ? 0 : digest;
}

static uint64_t trace_instance(
    struct fwlab_nfc_model *model, uint32_t first, uint32_t last)
{
    uint64_t instance = 0;
    uint32_t index;

    for (index = first; index < last; ++index) {
        struct fwlab_nfc_trace_entry entry;

        CHECK(fwlab_nfc_model_trace_read(model, index, &entry) ==
              FWLAB_NFC_API_OK);
        if (entry.operation.operation_uid == 0) {
            continue;
        }
        CHECK(entry.operation.instance_nonce != 0);
        CHECK(instance == 0 || instance == entry.operation.instance_nonce);
        instance = entry.operation.instance_nonce;
    }
    return instance;
}

static int changed_retry_rejected(
    struct j0_runtime *runtime, uint32_t profile,
    const struct fwlab_nvme_command *original,
    const struct j0_host_transfer *transfer,
    const struct fwlab_spine_command_ticket_v0 *ticket, uint32_t variant)
{
    struct fwlab_nvme_command changed = *original;
    struct j0_host_transfer changed_transfer = *transfer;
    struct j0_admission_record *record = record_for_ticket(runtime, ticket);
    struct fwlab_spine_command_ticket_v0 repeated;
    struct fwlab_spine_command_ticket_v0 sentinel;
    uint8_t input[J0_MAX_TRANSFER_BYTES];

    CHECK(record != NULL && record->program.action_count == 2);
    memset(&sentinel, 0xa5, sizeof(sentinel));
    repeated = sentinel;
    if (variant == 0) {
        ++changed.command_dword10_15[0];
    } else if (variant == 1) {
        memcpy(input, transfer->input, transfer->exact_bytes);
        input[0] ^= 1u;
        changed_transfer.input = input;
    } else if (variant == 2) {
        struct fwlab_spine_profile_binding_v0 *binding =
            profile == J0_PROFILE_C43_P1 ? &runtime->c43_binding
                                         : &runtime->linux_binding;

        ++binding->generation;
    } else {
        /* The fresh adapter read must detect a differing retained snapshot. */
        ++record->argument[1].lba;
    }
    CHECK(j0_runtime_admit_start(runtime, profile, &changed,
                                 &changed_transfer, &repeated) ==
          FWLAB_SPINE_V0_POISONED);
    CHECK(runtime->poisoned && runtime->admission_closed);
    CHECK(memcmp(&repeated, &sentinel, sizeof(repeated)) == 0);
    CHECK(fwlab_spine_command_ticket_v0_equal(&record->ticket, ticket));
    changed = *original;
    changed.handle.command_uid += 10000u;
    changed.origin.word[1] += 10000u;
    CHECK(j0_runtime_admit_start(runtime, profile, &changed, transfer,
                                 &repeated) == FWLAB_SPINE_V0_POISONED);
    CHECK(memcmp(&repeated, &sentinel, sizeof(repeated)) == 0);
    return 1;
}

static int admission_retry_checks(
    struct j0_runtime *runtime, uint32_t profile,
    const struct fwlab_nvme_command *command,
    const struct j0_host_transfer *transfer,
    const struct fwlab_spine_command_ticket_v0 *ticket)
{
    uint32_t variant;

    /* Private process copies test absorbing admission failures. They perform
     * no media operation and cannot change the continuing real journey. */
    for (variant = 0; variant < 4; ++variant) {
        pid_t child = fork();
        int status;

        CHECK(child >= 0);
        if (child == 0) {
            _exit(changed_retry_rejected(runtime, profile, command, transfer,
                                         ticket, variant) ? 0 : 1);
        }
        CHECK(waitpid(child, &status, 0) == child);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    CHECK(!runtime->poisoned && !runtime->admission_closed);
    return 1;
}

static int runtime_close(struct j0_runtime *runtime,
                         struct j0_close_status *final_status)
{
    uint32_t iteration;
    int quiescent = 0;

    CHECK(j0_runtime_close_start(runtime) == FWLAB_SPINE_V0_OK);
    CHECK(j0_runtime_close_start(runtime) == FWLAB_SPINE_V0_OK);
    for (iteration = 0; iteration < 400000; ++iteration) {
        struct j0_close_status status;
        uint32_t units = 0;

        CHECK(j0_runtime_close_query(runtime, &status) == FWLAB_SPINE_V0_OK);
        if (status.quiescent) {
            quiescent = 1;
            break;
        }
        CHECK(j0_runtime_step(runtime, 3, &units) == FWLAB_SPINE_V0_OK);
    }
    CHECK(quiescent);
    for (iteration = 0; iteration < 256; ++iteration) {
        enum fwlab_spine_result_v0 result = j0_runtime_fini(runtime);

        if (result == FWLAB_SPINE_V0_OK) {
            CHECK(j0_runtime_close_query(runtime, final_status) ==
                  FWLAB_SPINE_V0_OK);
            CHECK(final_status->quiescent);
            CHECK(final_status->profiles_retired);
            return 1;
        }
        CHECK(result == FWLAB_SPINE_V0_IN_PROGRESS);
    }
    return 0;
}

static int profile_journey(
    struct j0_runtime *runtime, uint32_t profile, uint64_t instance_nonce,
    uint64_t first_uid, uint64_t lba, uint32_t lba_count, uint8_t seed,
    struct profile_receipt *receipt)
{
    uint8_t input[J0_MAX_TRANSFER_BYTES];
    uint8_t output[J0_MAX_TRANSFER_BYTES];
    const uint32_t bytes = lba_count * FWLAB_M3P_LBA_BYTES;
    struct fwlab_nvme_command write = command_make(
        instance_nonce, first_uid, profile, 0x01, lba, lba_count);
    struct fwlab_nvme_command flush = command_make(
        instance_nonce, first_uid + 1u, profile, 0x00, 0, 0);
    struct fwlab_nvme_command read = command_make(
        instance_nonce, first_uid + 2u, profile, 0x02, lba, lba_count);
    struct j0_host_transfer write_transfer;
    struct j0_host_transfer no_transfer;
    struct j0_host_transfer read_transfer;
    struct fwlab_spine_command_ticket_v0 write_ticket;
    struct fwlab_spine_command_ticket_v0 flush_ticket;
    struct fwlab_spine_command_ticket_v0 read_ticket;
    struct fwlab_nvme_completion_intent intent[3];
    struct j0_admission_record *write_record;
    struct j0_admission_record *flush_record;
    uint32_t trace_first = fwlab_nfc_model_trace_count(runtime->nfc_model);
    uint32_t trace_last;

    CHECK(bytes <= sizeof(input));
    pattern_fill(input, bytes, seed);
    memset(output, 0, bytes);
    memset(intent, 0, sizeof(intent));
    write_transfer = transfer_make(
        FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER, bytes, input);
    no_transfer = transfer_make(0, 0, NULL);
    read_transfer = transfer_make(
        FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST, bytes, NULL);
    CHECK(command_run(runtime, profile, &write, &write_transfer,
                      &write_ticket, &intent[0]));
    CHECK(admission_retry_checks(runtime, profile, &write, &write_transfer,
                                 &write_ticket));
    CHECK(command_run(runtime, profile, &flush, &no_transfer,
                      &flush_ticket, &intent[1]));
    CHECK(command_run(runtime, profile, &read, &read_transfer,
                      &read_ticket, &intent[2]));
    CHECK(j0_runtime_host_read(runtime, &read_ticket, output, bytes) ==
          FWLAB_SPINE_V0_OK);
    CHECK(memcmp(input, output, bytes) == 0);
    write_record = record_for_ticket(runtime, &write_ticket);
    flush_record = record_for_ticket(runtime, &flush_ticket);
    CHECK(write_record != NULL && flush_record != NULL);
    CHECK(write_record->program.action_count == 2);
    CHECK(write_record->action[0].token.kind == FWLAB_HOST_ACTION_V0_DMA_IN);
    CHECK(write_record->action[1].token.kind ==
          FWLAB_HOST_ACTION_V0_BLOCK_WRITE);
    CHECK(flush_record->program.action_count == 1);
    CHECK(flush_record->action[0].block_status.durability_witness ==
          FWLAB_BLOCK_V0_WITNESS_FRONTIER_DURABLE);
    CHECK(flush_record->action[0].block_status.frontier.word[1] != 0);
    trace_last = fwlab_nfc_model_trace_count(runtime->nfc_model);
    receipt->adapter = write_record->binding.adapter_instance_nonce;
    receipt->lifecycle_instance = write_record->ticket.lifecycle_instance_nonce;
    receipt->m3p_instance = runtime->m3p->config.instance_nonce;
    receipt->block_provider =
        write_record->action[1].block_status.operation_token.provider_nonce;
    receipt->nfc_instance = trace_instance(runtime->nfc_model,
                                         trace_first, trace_last);
    CHECK(receipt->lifecycle_instance == runtime->lifecycle_instance_nonce);
    CHECK(flush_ticket.lifecycle_instance_nonce == receipt->lifecycle_instance);
    CHECK(read_ticket.lifecycle_instance_nonce == receipt->lifecycle_instance);
    CHECK(runtime->block.context == runtime->m3p);
    CHECK(runtime->block.ops == fwlab_m3p_block_service(runtime->m3p).ops);
    CHECK(receipt->m3p_instance == runtime->m3p_instance_nonce);
    CHECK(receipt->block_provider == runtime->block.provider_nonce);
    CHECK(receipt->block_provider == runtime->m3p->config.provider_nonce);
    CHECK(flush_record->action[0].block_status.frontier.word[0] ==
          receipt->block_provider);
    CHECK(receipt->nfc_instance == runtime->nfc_instance_nonce);
    CHECK(receipt->nfc_instance == runtime->m3p->config.nfc_instance_nonce);
    CHECK(runtime->m3p->nfc.context == runtime->nfc_provider.context);
    CHECK(runtime->m3p->nfc.ops == runtime->nfc_provider.ops);
    receipt->ticket = write_record->ticket.ticket_uid;
    receipt->authority = write_record->authority.authority_uid;
    receipt->dma = write_record->action[0].dma_token.operation_uid;
    receipt->buffer = write_record->buffer.lease_uid;
    receipt->block = write_record->action[1].token.action_uid;
    receipt->nfc_operations = trace_digest(
        runtime->nfc_model, trace_first, trace_last, 0);
    receipt->ppas = trace_digest(
        runtime->nfc_model, trace_first, trace_last, 1);
    receipt->frontier = flush_record->action[0].block_status.frontier.word[1];
    receipt->intent = bytes_digest(intent, sizeof(intent));
    receipt->readback = bytes_digest(output, bytes);
    CHECK(receipt->adapter != 0 && receipt->ticket != 0 &&
          receipt->authority != 0 && receipt->dma != 0 &&
          receipt->buffer != 0 && receipt->block != 0 &&
          receipt->nfc_operations != 0 && receipt->ppas != 0 &&
          receipt->frontier != 0 && receipt->intent != 0 &&
          receipt->readback != 0);
    return 1;
}

static int recovery_read(
    struct j0_runtime *runtime, uint32_t profile, uint64_t instance_nonce,
    uint64_t uid, uint64_t lba, uint32_t lba_count, uint8_t seed)
{
    uint8_t expected[J0_MAX_TRANSFER_BYTES];
    uint8_t output[J0_MAX_TRANSFER_BYTES];
    uint32_t bytes = lba_count * FWLAB_M3P_LBA_BYTES;
    struct fwlab_nvme_command read = command_make(
        instance_nonce, uid, profile, 0x02, lba, lba_count);
    struct j0_host_transfer transfer = transfer_make(
        FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST, bytes, NULL);
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_nvme_completion_intent intent;

    pattern_fill(expected, bytes, seed);
    memset(output, 0, bytes);
    CHECK(command_run(runtime, profile, &read, &transfer, &ticket, &intent));
    CHECK(j0_runtime_host_read(runtime, &ticket, output, bytes) ==
          FWLAB_SPINE_V0_OK);
    return memcmp(expected, output, bytes) == 0;
}

static int command_close_cut(
    struct j0_runtime *runtime,
    struct fwlab_spine_command_ticket_v0 *backpressure_ticket)
{
    uint8_t input[8192];
    struct fwlab_nvme_command command = command_make(
        UINT64_C(0x4a30434c4f534500), UINT64_C(0x7001),
        J0_PROFILE_LINUX_V1, 0x01, 129, 16);
    struct j0_host_transfer transfer;
    struct fwlab_spine_command_ticket_v0 ticket;
    struct j0_admission_record *record;
    struct j0_admission_record *waiting;
    uint32_t iteration;

    pattern_fill(input, sizeof(input), 0xc3);
    transfer = transfer_make(
        FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER, sizeof(input), input);
    CHECK(j0_runtime_admit_start(runtime, J0_PROFILE_LINUX_V1, &command,
                                 &transfer, &ticket) == FWLAB_SPINE_V0_OK);
    record = record_for_ticket(runtime, &ticket);
    CHECK(record != NULL && record->program.action_count == 2);
    command = command_make(
        UINT64_C(0x4a30434c4f534500), UINT64_C(0x7002),
        J0_PROFILE_LINUX_V1, 0x01, 161, 16);
    CHECK(j0_runtime_admit_start(runtime, J0_PROFILE_LINUX_V1, &command,
                                 &transfer, backpressure_ticket) ==
          FWLAB_SPINE_V0_OK);
    waiting = record_for_ticket(runtime, backpressure_ticket);
    CHECK(waiting != NULL && waiting->program.action_count == 2);
    for (iteration = 0; iteration < 10000; ++iteration) {
        uint32_t units = 0;

        if (record->action[0].lower_token_valid &&
            record->action[1].lower_token_valid &&
            waiting->action[1].lower_token_valid &&
            waiting->action[1].state == J0_DRIVER_PREPARED) {
            struct fwlab_block_status_v0 status;

            CHECK(record->action[1].state == J0_DRIVER_ACCEPTED);
            CHECK(record->action[0].dma_token.operation_uid != 0);
            CHECK(record->action[1].block_request.operation_token.
                  action.action_uid != 0);
            CHECK(runtime->block.ops->query(
                      runtime->block.context,
                      &waiting->action[1].block_request.operation_token,
                      &status) == FWLAB_SPINE_V0_STALE);
            return 1;
        }
        CHECK(j0_runtime_step(runtime, 1, &units) == FWLAB_SPINE_V0_OK);
        CHECK(units == 1);
    }
    CHECK(0);
    return 1;
}

static int runtime_open(
    struct j0_runtime **runtime_out, struct fwlab_file_nand_v0 *file,
    const uint8_t uuid[16], uint32_t mode, uint64_t salt)
{
    struct j0_runtime *runtime = calloc(1, sizeof(*runtime));
    struct j0_runtime_config config;

    CHECK(runtime != NULL);
    memset(&config, 0, sizeof(config));
    config.version = J0_RUNTIME_VERSION;
    config.size = (uint16_t)sizeof(config);
    memcpy(config.media_uuid, uuid, sizeof(config.media_uuid));
    config.file = file;
    config.media_mode = mode;
    config.generation = 1;
    config.execution_epoch = 1;
    config.volatile_nonce_seed = salt;
    CHECK(j0_runtime_init(runtime, &config) == FWLAB_SPINE_V0_OK);
    CHECK(runtime_ready(runtime));
    *runtime_out = runtime;
    return 1;
}

static int closed_lane_checks(struct j0_runtime *runtime)
{
    static const uint16_t kinds[] = {
        FWLAB_HOST_ACTION_V0_QUEUE_EFFECT,
        FWLAB_HOST_ACTION_V0_TARGET_RESOLVE,
        FWLAB_HOST_ACTION_V0_BLOCK_TRIM,
    };
    struct test_fake_block fake;
    struct fwlab_block_service_v0 real = runtime->block;
    uint32_t index;

    fake_block_init(&fake, &runtime->buffer.port);
    runtime->block = fake.service;
    for (index = 0; index < sizeof(kinds) / sizeof(kinds[0]); ++index) {
        const struct fwlab_host_action_driver_binding_v0 *lane =
            &runtime->drivers.entry[kinds[index] - 1u];
        uint64_t nonce = runtime->lifecycle_instance_nonce;
        uint32_t epoch = runtime->config.execution_epoch;
        uint8_t quiescent = 0xa5;

        CHECK(lane->ops->epoch_quiescent(lane->context, nonce, epoch,
                                         &quiescent) ==
              FWLAB_SPINE_V0_WRONG_STATE);
        CHECK(quiescent == 0xa5);
        CHECK(lane->ops->epoch_close(lane->context, nonce + 1u, epoch) ==
              FWLAB_SPINE_V0_STALE);
        CHECK(lane->ops->epoch_close(lane->context, nonce, epoch) ==
              FWLAB_SPINE_V0_OK);
        CHECK(lane->ops->epoch_close(lane->context, nonce, epoch) ==
              FWLAB_SPINE_V0_OK);
        CHECK(lane->ops->epoch_close(lane->context, nonce, epoch + 1u) ==
              FWLAB_SPINE_V0_STALE);
        CHECK(lane->ops->epoch_quiescent(lane->context, nonce, epoch,
                                         &quiescent) == FWLAB_SPINE_V0_OK);
        CHECK(quiescent == 1);
        quiescent = 0xa5;
        CHECK(lane->ops->epoch_quiescent(lane->context, nonce + 1u, epoch,
                                         &quiescent) == FWLAB_SPINE_V0_STALE);
        CHECK(quiescent == 0xa5);
        CHECK(!runtime->host_close.started && !runtime->block_close.started);
        CHECK(fake.close_calls == 0 && fake.quiescent_calls == 0);
    }
    runtime->block = real;
    return 1;
}

static int adjacent_replacement_rows(struct image_context *image)
{
    struct j0_runtime *runtime;
    struct test_fake_block fake_block;
    struct test_fake_data fake_data;
    struct fwlab_block_service_v0 real_block;
    struct fwlab_host_data_port_v0 real_host;
    struct fwlab_nvme_command block_command;
    struct fwlab_nvme_command data_command;
    struct j0_host_transfer transfer;
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_nvme_completion_intent intent;
    struct j0_close_status close_status;
    uint8_t block_bytes[512];
    uint8_t data_bytes[512];

    image->arena = arena_allocate(
        fwlab_file_nand_v0_arena_alignment(),
        fwlab_file_nand_v0_arena_size());
    CHECK(image->arena != NULL);
    CHECK(fwlab_file_nand_v0_posix_restart(
              image->arena, fwlab_file_nand_v0_arena_size(),
              image->directory_fd, image->name, &image->holder,
              &image->file) == FWLAB_NFC_API_OK);
    CHECK(runtime_open(&runtime, image->file, image->uuid,
                       J0_MEDIA_RECOVER, 3));
    CHECK(closed_lane_checks(runtime));

    pattern_fill(block_bytes, sizeof(block_bytes), 0x44);
    block_command = command_make(
        UINT64_C(0x4a304146424c4b00), UINT64_C(0x8001),
        J0_PROFILE_LINUX_V1, 0x01, 401, 1);
    transfer = transfer_make(
        FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER,
        sizeof(block_bytes), block_bytes);
    fake_block_init(&fake_block, &runtime->buffer.port);
    CHECK(fwlab_block_service_v0_valid(&fake_block.service));
    real_block = runtime->block;
    runtime->block = fake_block.service;
    CHECK(command_run(runtime, J0_PROFILE_LINUX_V1, &block_command,
                      &transfer, &ticket, &intent));
    CHECK(fake_block.data_digest ==
          bytes_digest(block_bytes, sizeof(block_bytes)));
    CHECK(fake_block.drained);
    runtime->block = real_block;

    pattern_fill(data_bytes, sizeof(data_bytes), 0x55);
    data_command = command_make(
        UINT64_C(0x4a30414644415400), UINT64_C(0x8101),
        J0_PROFILE_LINUX_V1, 0x01, 409, 1);
    transfer = transfer_make(
        FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER,
        sizeof(data_bytes), data_bytes);
    fake_data_init(&fake_data, runtime, &data_command,
                   data_bytes, sizeof(data_bytes));
    CHECK(fwlab_host_data_port_v0_valid(&fake_data.port));
    real_host = runtime->host_binding.data;
    runtime->host_binding.data = fake_data.port;
    CHECK(command_run(runtime, J0_PROFILE_LINUX_V1, &data_command,
                      &transfer, &ticket, &intent));
    CHECK(fake_data.request_valid && fake_data.drained &&
          !fake_data.authority_live);
    runtime->host_binding.data = real_host;

    CHECK(runtime_close(runtime, &close_status));
    CHECK(close_status.quiescent && close_status.profiles_retired);
    free(runtime);
    CHECK(fwlab_file_nand_v0_close(image->file) == FWLAB_NFC_API_OK);
    free(image->arena);
    image->arena = NULL;
    image->file = NULL;
    return 1;
}

static int child_image_open(
    struct image_context *image, const char *directory, const char *name,
    const char *holder_device, const char *holder_inode,
    const char *uuid_text)
{
    uint64_t device;
    uint64_t inode;

    memset(image, 0, sizeof(*image));
    CHECK(strlen(directory) < sizeof(image->directory));
    (void)strcpy(image->directory, directory);
    image->name = name;
    CHECK(uuid_decode(uuid_text, image->uuid));
    CHECK(parse_u64(holder_device, &device));
    CHECK(parse_u64(holder_inode, &inode));
    image->holder.device = device;
    image->holder.inode = inode;
    memcpy(image->holder.media_uuid, image->uuid,
           sizeof(image->holder.media_uuid));
    image->directory_fd = open(
        image->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(image->directory_fd >= 0);
    image->arena = arena_allocate(
        fwlab_file_nand_v0_arena_alignment(),
        fwlab_file_nand_v0_arena_size());
    CHECK(image->arena != NULL);
    CHECK(fwlab_file_nand_v0_posix_restart(
              image->arena, fwlab_file_nand_v0_arena_size(),
              image->directory_fd, image->name, &image->holder,
              &image->file) == FWLAB_NFC_API_OK);
    return 1;
}

static int child_image_close(struct image_context *image)
{
    CHECK(fwlab_file_nand_v0_close(image->file) == FWLAB_NFC_API_OK);
    CHECK(close(image->directory_fd) == 0);
    free(image->arena);
    image->arena = NULL;
    image->file = NULL;
    return 1;
}

static void print_profile_receipt(
    const char *name, const struct profile_receipt *receipt,
    const struct digest_arguments *digest, const uint8_t uuid[16])
{
    char uuid_text[33];

    (void)uuid_encode(uuid, uuid_text);
    printf("J0B_PROFILE|name=%s|adapter=%" PRIu64
           "|lifecycle=%s|host=%s|m3p=%s|nfc=%s|file=%s|uuid=%s"
           "|life_instance=%" PRIu64 "|m3p_instance=%" PRIu64
           "|block_provider=%" PRIu64 "|nfc_instance=%" PRIu64
           "|ticket=%" PRIu64 "|hdar=%" PRIu64 "|dmop=%" PRIu64
           "|cbls=%" PRIu64 "|bopt=%" PRIu64
           "|nfc_ops=%016" PRIx64 "|ppas=%016" PRIx64
           "|frontier=%" PRIu64 "|intent=%016" PRIx64
           "|readback=%016" PRIx64 "\n",
           name, receipt->adapter, digest->lifecycle, digest->host,
           digest->m3p, digest->nfc, digest->file, uuid_text,
           receipt->lifecycle_instance, receipt->m3p_instance,
           receipt->block_provider, receipt->nfc_instance,
           receipt->ticket, receipt->authority, receipt->dma,
           receipt->buffer, receipt->block, receipt->nfc_operations,
           receipt->ppas, receipt->frontier, receipt->intent,
           receipt->readback);
}

static int child_write(
    struct image_context *image, const struct digest_arguments *digest)
{
    struct j0_runtime *runtime;
    struct profile_receipt c43;
    struct profile_receipt linux_profile;
    struct j0_close_status close_status;

    memset(&c43, 0, sizeof(c43));
    memset(&linux_profile, 0, sizeof(linux_profile));
    CHECK(runtime_open(&runtime, image->file, image->uuid,
                       J0_MEDIA_FORMAT, 1));
    CHECK(profile_journey(
        runtime, J0_PROFILE_C43_P1, UINT64_C(0x4a30433433010000),
        1, 1, 3, 0x31, &c43));
    CHECK(profile_journey(
        runtime, J0_PROFILE_LINUX_V1, UINT64_C(0x4a304c4e58010000),
        101, 17, 16, 0x71, &linux_profile));
    CHECK(c43.adapter != linux_profile.adapter);
    CHECK(c43.ticket != linux_profile.ticket);
    CHECK(c43.authority != linux_profile.authority);
    CHECK(c43.dma != linux_profile.dma);
    CHECK(c43.buffer != linux_profile.buffer);
    CHECK(c43.block != linux_profile.block);
    CHECK(c43.lifecycle_instance == linux_profile.lifecycle_instance);
    CHECK(c43.m3p_instance == linux_profile.m3p_instance);
    CHECK(c43.block_provider == linux_profile.block_provider);
    CHECK(c43.nfc_instance == linux_profile.nfc_instance);
    CHECK(runtime_close(runtime, &close_status));
    print_profile_receipt("C43-P1", &c43, digest, image->uuid);
    print_profile_receipt("Linux-profile-v1", &linux_profile, digest,
                          image->uuid);
    free(runtime);
    return 1;
}

static int child_recover(
    struct image_context *image, const struct digest_arguments *digest)
{
    struct j0_runtime *runtime;
    struct j0_close_status close_status;
    struct fwlab_spine_command_ticket_v0 backpressure_ticket;
    struct j0_admission_record *waiting;
    char uuid_text[33];

    CHECK(runtime_open(&runtime, image->file, image->uuid,
                       J0_MEDIA_RECOVER, 2));
    CHECK(runtime->lifecycle_instance_nonce != J0_LIFECYCLE_NONCE + 1u);
    CHECK(runtime->m3p_instance_nonce != J0_M3P_INSTANCE_NONCE + 1u);
    CHECK(runtime->nfc_instance_nonce != J0_NFC_INSTANCE_NONCE + 1u);
    CHECK(recovery_read(
        runtime, J0_PROFILE_C43_P1, UINT64_C(0x4a30433433020000),
        201, 1, 3, 0x31));
    CHECK(recovery_read(
        runtime, J0_PROFILE_LINUX_V1, UINT64_C(0x4a304c4e58020000),
        301, 17, 16, 0x71));
    CHECK(command_close_cut(runtime, &backpressure_ticket));
    CHECK(runtime_close(runtime, &close_status));
    waiting = record_for_ticket(runtime, &backpressure_ticket);
    CHECK(waiting != NULL && waiting->close_reaped);
    CHECK(waiting->action[1].state == J0_DRIVER_REJECTED_CLEAN);
    CHECK(!waiting->action[1].result_latched);
    CHECK(close_status.host_authorities == 0);
    CHECK(close_status.dma_operations == 0);
    CHECK(close_status.buffers == 0);
    CHECK(close_status.block_operations == 0);
    CHECK(close_status.pending == 0);
    CHECK(close_status.pinned == 0);
    CHECK(close_status.nfc_operations == 0);
    (void)uuid_encode(image->uuid, uuid_text);
    printf("J0B_RESTART|same_elf=%s|same_uuid=%s|fresh_lifecycle=1"
           "|fresh_m3p=1|fresh_nfc=1|c43_readback=1|linux_readback=1\n",
           digest->elf, uuid_text);
    printf("J0B_CLOSE|host_authorities=0|dma_ops=0|buffers=0|block_ops=0"
           "|pending=0|pinned=0|nfc_ops=0|profiles_retired=1\n");
    free(runtime);
    return 1;
}

static int child_identity_check(
    const char *expected_device, const char *expected_inode)
{
    struct stat status;
    uint64_t device;
    uint64_t inode;
    int descriptor;

    CHECK(parse_u64(expected_device, &device));
    CHECK(parse_u64(expected_inode, &inode));
    descriptor = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    CHECK(fstat(descriptor, &status) == 0);
    CHECK((uint64_t)status.st_dev == device);
    CHECK((uint64_t)status.st_ino == inode);
    CHECK(close(descriptor) == 0);
    return 1;
}

static int child_main(int argc, char **argv)
{
    struct image_context image;
    struct digest_arguments digest;
    int success;

    if (argc != 16 ||
        (strcmp(argv[1], "--child-write") != 0 &&
         strcmp(argv[1], "--child-recover") != 0) ||
        !child_identity_check(argv[4], argv[5])) {
        return 1;
    }
    digest.lifecycle = argv[10];
    digest.host = argv[11];
    digest.m3p = argv[12];
    digest.nfc = argv[13];
    digest.file = argv[14];
    digest.elf = argv[15];
    if (!lowercase_sha(digest.lifecycle) || !lowercase_sha(digest.host) ||
        !lowercase_sha(digest.m3p) || !lowercase_sha(digest.nfc) ||
        !lowercase_sha(digest.file) || !lowercase_sha(digest.elf) ||
        strcmp(argv[9], "J0B-V1") != 0 ||
        !child_image_open(&image, argv[2], argv[3], argv[6], argv[7],
                          argv[8])) {
        return 1;
    }
    success = strcmp(argv[1], "--child-write") == 0
                  ? child_write(&image, &digest)
                  : child_recover(&image, &digest);
    if (!success || !child_image_close(&image)) {
        return 1;
    }
    return 0;
}

static int child_spawn(
    int executable_fd, const struct stat *executable_status,
    const struct image_context *image, const char *mode,
    const struct digest_arguments *digest)
{
    char executable_device[32];
    char executable_inode[32];
    char holder_device[32];
    char holder_inode[32];
    char uuid_text[33];
    char *child_argv[17];
    pid_t child;
    int status;

    (void)snprintf(executable_device, sizeof(executable_device), "%ju",
                   (uintmax_t)executable_status->st_dev);
    (void)snprintf(executable_inode, sizeof(executable_inode), "%ju",
                   (uintmax_t)executable_status->st_ino);
    (void)snprintf(holder_device, sizeof(holder_device), "%" PRIu64,
                   image->holder.device);
    (void)snprintf(holder_inode, sizeof(holder_inode), "%" PRIu64,
                   image->holder.inode);
    (void)uuid_encode(image->uuid, uuid_text);
    child_argv[0] = (char *)"j0b_profile_matrix";
    child_argv[1] = (char *)mode;
    child_argv[2] = (char *)image->directory;
    child_argv[3] = (char *)image->name;
    child_argv[4] = executable_device;
    child_argv[5] = executable_inode;
    child_argv[6] = holder_device;
    child_argv[7] = holder_inode;
    child_argv[8] = uuid_text;
    child_argv[9] = (char *)"J0B-V1";
    child_argv[10] = (char *)digest->lifecycle;
    child_argv[11] = (char *)digest->host;
    child_argv[12] = (char *)digest->m3p;
    child_argv[13] = (char *)digest->nfc;
    child_argv[14] = (char *)digest->file;
    child_argv[15] = (char *)digest->elf;
    child_argv[16] = NULL;
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        (void)execveat(executable_fd, "", child_argv, environ, AT_EMPTY_PATH);
        _exit(127);
    }
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
    return 1;
}

static int child_failure_probe(int executable_fd)
{
    char *child_argv[] = {
        (char *)"j0b_profile_matrix", (char *)"--child-recover", NULL,
    };
    pid_t child = fork();
    int status;

    CHECK(child >= 0);
    if (child == 0) {
        (void)execveat(executable_fd, "", child_argv, environ, AT_EMPTY_PATH);
        _exit(127);
    }
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) != 0);
    return 1;
}

static int image_create(struct image_context *image)
{
    uint32_t index;

    memset(image, 0, sizeof(*image));
    (void)strcpy(image->directory, "/tmp/fwlab-j0b-XXXXXX");
    CHECK(mkdtemp(image->directory) != NULL);
    CHECK(chmod(image->directory, 0700) == 0);
    image->directory_fd = open(
        image->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(image->directory_fd >= 0);
    image->name = "j0b-media.bin";
    for (index = 0; index < sizeof(image->uuid); ++index) {
        image->uuid[index] = (uint8_t)(0x90u + index);
    }
    image->arena = arena_allocate(
        fwlab_file_nand_v0_arena_alignment(),
        fwlab_file_nand_v0_arena_size());
    CHECK(image->arena != NULL);
    CHECK(fwlab_file_nand_v0_posix_format(
              image->arena, fwlab_file_nand_v0_arena_size(),
              image->directory_fd, image->name, image->uuid, &image->file,
              &image->holder) == FWLAB_NFC_API_OK);
    CHECK(fwlab_file_nand_v0_close(image->file) == FWLAB_NFC_API_OK);
    free(image->arena);
    image->arena = NULL;
    image->file = NULL;
    return 1;
}

static int image_destroy(struct image_context *image)
{
    CHECK(unlinkat(image->directory_fd, image->name, 0) == 0);
    CHECK(fsync(image->directory_fd) == 0);
    CHECK(close(image->directory_fd) == 0);
    CHECK(rmdir(image->directory) == 0);
    return 1;
}

static int parse_parent_arguments(
    int argc, char **argv, struct digest_arguments *digest)
{
    if (argc != 13 || strcmp(argv[1], "--lifecycle-sha") != 0 ||
        strcmp(argv[3], "--host-sha") != 0 ||
        strcmp(argv[5], "--m3p-sha") != 0 ||
        strcmp(argv[7], "--nfc-sha") != 0 ||
        strcmp(argv[9], "--file-sha") != 0 ||
        strcmp(argv[11], "--elf-sha") != 0) {
        return 0;
    }
    digest->lifecycle = argv[2];
    digest->host = argv[4];
    digest->m3p = argv[6];
    digest->nfc = argv[8];
    digest->file = argv[10];
    digest->elf = argv[12];
    return lowercase_sha(digest->lifecycle) && lowercase_sha(digest->host) &&
           lowercase_sha(digest->m3p) && lowercase_sha(digest->nfc) &&
           lowercase_sha(digest->file) && lowercase_sha(digest->elf);
}

int main(int argc, char **argv)
{
    struct digest_arguments digest;
    struct image_context image;
    struct stat executable_status;
    int executable_fd;

    if (argc > 1 &&
        (strcmp(argv[1], "--child-write") == 0 ||
         strcmp(argv[1], "--child-recover") == 0)) {
        return child_main(argc, argv);
    }
    CHECK_MAIN(parse_parent_arguments(argc, argv, &digest));
    CHECK_MAIN(image_create(&image));
    executable_fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    CHECK_MAIN(executable_fd >= 0);
    CHECK_MAIN(fstat(executable_fd, &executable_status) == 0);
    CHECK_MAIN(child_failure_probe(executable_fd));
    CHECK_MAIN(child_spawn(executable_fd, &executable_status, &image,
                           "--child-write", &digest));
    CHECK_MAIN(child_spawn(executable_fd, &executable_status, &image,
                           "--child-recover", &digest));
    CHECK_MAIN(adjacent_replacement_rows(&image));
    CHECK_MAIN(close(executable_fd) == 0);
    CHECK_MAIN(image_destroy(&image));
    puts("J0-B profile matrix: PASS (profiles=2 artifacts=1)");
    return 0;
}
