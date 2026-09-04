// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: BSD-3-Clause

#include "fwlab/contracts/block_service_v0.h"
#include "fwlab/contracts/controller_buffer_v0.h"
#include "fwlab/contracts/host_data_v0.h"
#include "fwlab/contracts/owner_control_v0.h"
#include "fwlab/portable/host_action_program_v0.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                       \
    do {                                                                        \
        if (!(expression)) {                                                    \
            fprintf(stderr, "S0-A contract check failed: %s:%d: %s\n",       \
                    __FILE__, __LINE__, #expression);                           \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static struct fwlab_nvme_command_handle command_handle(void)
{
    struct fwlab_nvme_command_handle value = {
        .instance_nonce = 0x1001,
        .command_uid = 0x1002,
        .controller_epoch = 1,
        .generation = 1,
    };

    return value;
}

static struct fwlab_nvme_origin_token origin_token(void)
{
    struct fwlab_nvme_origin_token value = {{0x1003, 0x1004}};

    return value;
}

static struct fwlab_host_action_argument_ref_v0 argument_ref(
    uint16_t ordinal, uint16_t kind)
{
    struct fwlab_host_action_argument_ref_v0 value;

    memset(&value, 0, sizeof(value));
    value.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    value.size = sizeof(value);
    value.adapter_instance_nonce = 0x1101;
    value.argument_uid = (uint64_t)ordinal + 1;
    value.generation = 1;
    value.ordinal = ordinal;
    value.kind = kind;
    return value;
}

static struct fwlab_host_completion_recipe_ref_v0 completion_recipe(void)
{
    struct fwlab_host_completion_recipe_ref_v0 value;

    memset(&value, 0, sizeof(value));
    value.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    value.size = sizeof(value);
    value.adapter_instance_nonce = 0x1101;
    value.recipe_uid = 0x1102;
    value.generation = 1;
    return value;
}

static struct fwlab_host_action_desc_v0 action_desc(
    uint16_t ordinal, uint16_t kind, uint32_t dependencies,
    uint32_t required, uint32_t produced)
{
    struct fwlab_host_action_desc_v0 value;

    memset(&value, 0, sizeof(value));
    value.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    value.size = sizeof(value);
    value.ordinal = ordinal;
    value.kind = kind;
    value.dependency_mask = dependencies;
    value.required_witness_mask = required;
    value.produced_witness_mask = produced;
    value.argument = argument_ref(ordinal, kind);
    return value;
}

static struct fwlab_host_action_program_v0 write_program(void)
{
    struct fwlab_host_action_program_v0 value;

    memset(&value, 0, sizeof(value));
    value.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    value.size = sizeof(value);
    value.command = command_handle();
    value.origin = origin_token();
    value.program_uid = 0x1201;
    value.program_generation = 1;
    value.action_count = 2;
    value.completion_required_witness_mask =
        FWLAB_HOST_WITNESS_V0_BLOCK_WRITE_COMPLETE;
    value.completion_recipe = completion_recipe();
    value.action[0] = action_desc(
        0, FWLAB_HOST_ACTION_V0_DMA_IN, 0, 0,
        FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE);
    value.action[1] = action_desc(
        1, FWLAB_HOST_ACTION_V0_BLOCK_WRITE, UINT32_C(1),
        FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE,
        FWLAB_HOST_WITNESS_V0_BLOCK_WRITE_COMPLETE);
    return value;
}

static struct fwlab_host_action_token_v0 action_token(uint16_t kind)
{
    struct fwlab_host_action_token_v0 value;

    memset(&value, 0, sizeof(value));
    value.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    value.size = sizeof(value);
    value.type_tag = FWLAB_HOST_ACTION_TOKEN_V0_TAG;
    value.command = command_handle();
    value.origin = origin_token();
    value.action_uid = 0x1301;
    value.generation = 1;
    value.ordinal = 0;
    value.kind = kind;
    return value;
}

static int test_action_program(void)
{
    struct fwlab_host_action_program_v0 program = write_program();
    struct fwlab_host_action_status_v0 action_status[2];
    struct fwlab_host_lifecycle_config_v0 config;
    struct fwlab_completion_lease_v0 lease;
    struct fwlab_nvme_completion_intent intent;

    memset(&config, 0, sizeof(config));
    config.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    config.size = sizeof(config);
    config.lifecycle_instance_nonce = 0x1401;
    config.execution_epoch = 1;
    config.generation = 1;
    config.command_capacity = 4;
    config.actions_per_command = FWLAB_HOST_ACTION_V0_MAX_ACTIONS;
    config.command_uid = (struct fwlab_uid_range_v0){1, 4};
    config.action_uid = (struct fwlab_uid_range_v0){10, 41};
    config.abort_uid = (struct fwlab_uid_range_v0){50, 53};
    config.completion_lease_uid = (struct fwlab_uid_range_v0){60, 63};

    CHECK(fwlab_host_lifecycle_config_v0_valid(&config));
    config.size--;
    CHECK(!fwlab_host_lifecycle_config_v0_valid(&config));
    config.size = sizeof(config);
    CHECK(fwlab_host_action_program_v0_valid(&program));

    program.action[1].dependency_mask = UINT32_C(2);
    CHECK(!fwlab_host_action_program_v0_valid(&program));
    program = write_program();
    program.action[1].required_witness_mask =
        FWLAB_HOST_WITNESS_V0_DMA_OUT_COMPLETE;
    CHECK(!fwlab_host_action_program_v0_valid(&program));
    program = write_program();
    program.action[2].reserved1[0] = 1;
    CHECK(!fwlab_host_action_program_v0_valid(&program));
    program = write_program();
    program.reserved3[0] = 1;
    CHECK(!fwlab_host_action_program_v0_valid(&program));
    program = write_program();
    program.action[1] = action_desc(
        1, FWLAB_HOST_ACTION_V0_DMA_IN, UINT32_C(1),
        FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE,
        FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE);
    program.completion_required_witness_mask =
        FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE;
    CHECK(!fwlab_host_action_program_v0_valid(&program));

    program = write_program();
    memset(action_status, 0, sizeof(action_status));
    action_status[0].version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    action_status[0].size = sizeof(action_status[0]);
    action_status[0].token = action_token(FWLAB_HOST_ACTION_V0_DMA_IN);
    action_status[0].state = FWLAB_HOST_ACTION_V0_STATE_DRAINED;
    action_status[0].terminal_kind = FWLAB_HOST_ACTION_V0_FAILED;
    action_status[0].effect = FWLAB_HOST_ACTION_V0_EFFECT_NONE;
    action_status[0].fault_domain = 1;
    action_status[0].fault_code = 1;
    action_status[1].version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    action_status[1].size = sizeof(action_status[1]);
    action_status[1].token =
        action_token(FWLAB_HOST_ACTION_V0_BLOCK_WRITE);
    action_status[1].token.action_uid++;
    action_status[1].token.ordinal = 1;
    action_status[1].state = FWLAB_HOST_ACTION_V0_STATE_DRAINED;
    action_status[1].terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
    action_status[1].produced_witness_mask =
        FWLAB_HOST_WITNESS_V0_BLOCK_WRITE_COMPLETE;
    action_status[1].effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL;
    action_status[1].units_completed = 1;
    memset(&intent, 0, sizeof(intent));
    intent.version = FWLAB_NVME_COMPLETION_VERSION;
    intent.size = sizeof(intent);
    intent.handle = program.command;
    intent.origin = program.origin;
    intent.effect_class = FWLAB_NVME_EFFECT_FULL;
    CHECK(fwlab_host_action_status_v0_valid(&action_status[0]));
    CHECK(fwlab_host_action_status_v0_valid(&action_status[1]));
    CHECK(!fwlab_host_completion_intent_v0_valid_for_program(
        &program, action_status, 2, &intent));
    action_status[0].terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
    action_status[0].produced_witness_mask =
        FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE;
    action_status[0].effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL;
    action_status[0].units_completed = 1;
    action_status[0].fault_domain = 0;
    action_status[0].fault_code = 0;
    CHECK(fwlab_host_completion_intent_v0_valid_for_program(
        &program, action_status, 2, &intent));

    program = write_program();
    memset(program.action, 0, sizeof(program.action));
    program.action_count = 0;
    program.completion_required_witness_mask = 0;
    memset(&intent, 0, sizeof(intent));
    intent.version = FWLAB_NVME_COMPLETION_VERSION;
    intent.size = sizeof(intent);
    intent.handle = program.command;
    intent.origin = program.origin;
    intent.effect_class = FWLAB_NVME_EFFECT_NONE;
    CHECK(fwlab_host_completion_intent_v0_valid_for_program(
        &program, NULL, 0, &intent));
    intent.effect_class = FWLAB_NVME_EFFECT_FULL;
    CHECK(!fwlab_host_completion_intent_v0_valid_for_program(
        &program, NULL, 0, &intent));

    memset(&lease, 0, sizeof(lease));
    lease.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    lease.size = sizeof(lease);
    lease.type_tag = FWLAB_COMPLETION_LEASE_V0_TAG;
    lease.command = command_handle();
    lease.origin = origin_token();
    lease.issuer_nonce = 0x1501;
    lease.lease_uid = 0x1502;
    lease.intent_generation = 1;
    lease.lease_generation = 1;
    CHECK(fwlab_completion_lease_v0_valid(&lease));
    lease.type_tag = FWLAB_HOST_DMA_AUTHORITY_V0_TAG;
    CHECK(!fwlab_completion_lease_v0_valid(&lease));
    return 0;
}

static struct fwlab_controller_buffer_lease_v0 buffer_lease(void)
{
    struct fwlab_controller_buffer_lease_v0 value;

    memset(&value, 0, sizeof(value));
    value.version = FWLAB_CONTROLLER_BUFFER_V0_VERSION;
    value.size = sizeof(value);
    value.type_tag = FWLAB_CONTROLLER_BUFFER_LEASE_V0_TAG;
    value.issuer_nonce = 0x2001;
    value.buffer_uid = 0x2002;
    value.lease_uid = 0x2003;
    value.generation = 1;
    value.capacity_bytes = 4096;
    value.rights = FWLAB_CONTROLLER_BUFFER_V0_READ |
                   FWLAB_CONTROLLER_BUFFER_V0_WRITE;
    return value;
}

static struct fwlab_controller_buffer_span_v0 buffer_span(void)
{
    struct fwlab_controller_buffer_span_v0 value;

    memset(&value, 0, sizeof(value));
    value.version = FWLAB_CONTROLLER_BUFFER_V0_VERSION;
    value.size = sizeof(value);
    value.offset = 512;
    value.length = 1024;
    return value;
}

static struct fwlab_host_dma_authority_ref_v0 dma_authority(void)
{
    struct fwlab_host_dma_authority_ref_v0 value;

    memset(&value, 0, sizeof(value));
    value.version = FWLAB_HOST_DATA_V0_VERSION;
    value.size = sizeof(value);
    value.type_tag = FWLAB_HOST_DMA_AUTHORITY_V0_TAG;
    value.issuer_nonce = 0x2101;
    value.authority_uid = 0x2102;
    value.generation = 1;
    value.exact_bytes = 1024;
    value.direction = FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER;
    return value;
}

static struct fwlab_dma_op_token_v0 dma_token(void)
{
    struct fwlab_dma_op_token_v0 value;

    memset(&value, 0, sizeof(value));
    value.version = FWLAB_HOST_DATA_V0_VERSION;
    value.size = sizeof(value);
    value.type_tag = FWLAB_DMA_OP_TOKEN_V0_TAG;
    value.action = action_token(FWLAB_HOST_ACTION_V0_DMA_IN);
    value.issuer_nonce = 0x2201;
    value.operation_uid = 0x2202;
    value.generation = 1;
    return value;
}

static int test_buffer_and_dma(void)
{
    struct fwlab_controller_buffer_lease_v0 buffer = buffer_lease();
    struct fwlab_controller_buffer_span_v0 span = buffer_span();
    struct fwlab_dma_request_v0 request;
    struct fwlab_dma_submit_result_v0 result;
    struct fwlab_dma_status_v0 status;

    CHECK(fwlab_controller_buffer_lease_v0_valid(&buffer));
    buffer.reserved1[0] = 1;
    CHECK(!fwlab_controller_buffer_lease_v0_valid(&buffer));
    buffer = buffer_lease();
    CHECK(fwlab_controller_buffer_span_v0_valid_for_lease(
        &span, &buffer, FWLAB_CONTROLLER_BUFFER_V0_WRITE));
    span.length = 4096;
    CHECK(!fwlab_controller_buffer_span_v0_valid_for_lease(
        &span, &buffer, FWLAB_CONTROLLER_BUFFER_V0_WRITE));

    memset(&request, 0, sizeof(request));
    request.version = FWLAB_HOST_DATA_V0_VERSION;
    request.size = sizeof(request);
    request.operation = dma_token();
    request.authority = dma_authority();
    request.buffer = buffer_lease();
    request.span = buffer_span();
    request.execution_epoch = 1;
    request.exact_bytes = request.span.length;
    request.direction = FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER;
    CHECK(fwlab_dma_request_v0_valid(&request));
    request.reserved2[0] = 1;
    CHECK(!fwlab_dma_request_v0_valid(&request));
    request.reserved2[0] = 0;

    memset(&result, 0, sizeof(result));
    result.version = FWLAB_HOST_DATA_V0_VERSION;
    result.size = sizeof(result);
    result.operation = request.operation;
    result.disposition = FWLAB_HOST_ACTION_V0_ACCEPTED;
    CHECK(fwlab_dma_submit_result_v0_matches_request(&result, &request));
    result.operation.operation_uid++;
    CHECK(!fwlab_dma_submit_result_v0_matches_request(&result, &request));

    memset(&status, 0, sizeof(status));
    status.version = FWLAB_HOST_DATA_V0_VERSION;
    status.size = sizeof(status);
    status.operation = request.operation;
    status.state = FWLAB_DMA_V0_STATE_TERMINAL;
    status.terminal_kind = FWLAB_DMA_V0_SUCCEEDED;
    status.effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL;
    status.bytes_completed = request.exact_bytes;
    CHECK(fwlab_dma_status_v0_valid(&status));
    CHECK(fwlab_dma_status_v0_matches_request(&status, &request));
    status.bytes_completed = request.exact_bytes + 1;
    CHECK(fwlab_dma_status_v0_valid(&status));
    CHECK(!fwlab_dma_status_v0_matches_request(&status, &request));
    status.terminal_kind = FWLAB_DMA_V0_FAILED;
    status.effect = FWLAB_HOST_ACTION_V0_EFFECT_EXACT_PREFIX;
    status.fault_domain = 1;
    status.fault_code = 1;
    CHECK(fwlab_dma_status_v0_valid(&status));
    CHECK(!fwlab_dma_status_v0_matches_request(&status, &request));
    status.bytes_completed = request.exact_bytes - 1;
    CHECK(fwlab_dma_status_v0_matches_request(&status, &request));
    status.operation.type_tag = FWLAB_CONTROLLER_BUFFER_LEASE_V0_TAG;
    CHECK(!fwlab_dma_status_v0_valid(&status));
    return 0;
}

static struct fwlab_block_request_v0 block_write_request(void)
{
    struct fwlab_block_request_v0 value;

    memset(&value, 0, sizeof(value));
    value.version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    value.size = sizeof(value);
    value.operation_token.version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    value.operation_token.size = sizeof(value.operation_token);
    value.operation_token.type_tag = FWLAB_BLOCK_OP_TOKEN_V0_TAG;
    value.operation_token.action =
        action_token(FWLAB_HOST_ACTION_V0_BLOCK_WRITE);
    value.operation_token.provider_nonce = 0x3001;
    value.operation_token.generation = 1;
    value.namespace_ref.word[0] = 1;
    value.lba = 8;
    value.lba_count = 2;
    value.operation = FWLAB_BLOCK_V0_WRITE;
    value.durability = FWLAB_BLOCK_V0_DURABILITY_SELF;
    value.buffer_present = 1;
    value.buffer = buffer_lease();
    value.buffer_span = buffer_span();
    return value;
}

static int test_block_service(void)
{
    struct fwlab_block_request_v0 request = block_write_request();
    struct fwlab_block_submit_result_v0 submitted;
    struct fwlab_block_status_v0 status;

    CHECK(fwlab_block_request_v0_valid(&request));
    request.reserved2[0] = 1;
    CHECK(!fwlab_block_request_v0_valid(&request));
    request = block_write_request();
    request.operation_token.action.kind = FWLAB_HOST_ACTION_V0_BLOCK_READ;
    CHECK(!fwlab_block_request_v0_valid(&request));
    request = block_write_request();
    request.buffer_present = 0;
    CHECK(!fwlab_block_request_v0_valid(&request));

    memset(&submitted, 0, sizeof(submitted));
    submitted.version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    submitted.size = sizeof(submitted);
    submitted.operation_token = request.operation_token;
    submitted.disposition = FWLAB_HOST_ACTION_V0_ACCEPTED;
    CHECK(!fwlab_block_submit_result_v0_matches_request(
        &submitted, &request));
    request = block_write_request();
    submitted.operation_token = request.operation_token;
    CHECK(fwlab_block_submit_result_v0_matches_request(
        &submitted, &request));

    memset(&status, 0, sizeof(status));
    status.version = FWLAB_BLOCK_SERVICE_V0_VERSION;
    status.size = sizeof(status);
    status.operation_token = request.operation_token;
    status.state = FWLAB_BLOCK_V0_STATE_TERMINAL;
    status.outcome = FWLAB_BLOCK_V0_SUCCEEDED;
    status.effect = FWLAB_BLOCK_V0_EFFECT_FULL;
    status.completed_lbas = request.lba_count;
    status.data_bytes = request.buffer_span.length;
    status.durability_witness = FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE;
    status.frontier.word[0] = 1;
    CHECK(fwlab_block_status_v0_matches_request(&status, &request));
    status.completed_lbas--;
    CHECK(!fwlab_block_status_v0_matches_request(&status, &request));
    status.outcome = FWLAB_BLOCK_V0_FAILED;
    status.effect = FWLAB_BLOCK_V0_EFFECT_EXACT_PREFIX;
    status.completed_lbas = request.lba_count + 1;
    status.data_bytes = request.buffer_span.length + 1;
    status.durability_witness = FWLAB_BLOCK_V0_WITNESS_NONE;
    memset(&status.frontier, 0, sizeof(status.frontier));
    status.fault_domain = 1;
    status.fault_code = 1;
    CHECK(fwlab_block_status_v0_valid(&status));
    CHECK(!fwlab_block_status_v0_matches_request(&status, &request));
    status.completed_lbas = 1;
    status.data_bytes = 512;
    status.durability_witness =
        FWLAB_BLOCK_V0_WITNESS_FRONTIER_DURABLE;
    status.frontier.word[0] = 1;
    CHECK(fwlab_block_status_v0_valid(&status));
    CHECK(!fwlab_block_status_v0_matches_request(&status, &request));
    return 0;
}

static struct fwlab_owner_stable_identity_v0 stable_identity(void)
{
    struct fwlab_owner_stable_identity_v0 value;

    memset(&value, 0, sizeof(value));
    value.version = FWLAB_OWNER_CONTROL_V0_VERSION;
    value.size = sizeof(value);
    value.function_instance_nonce = 0x4001;
    value.media_uuid[0] = 1;
    value.media_format_version = 1;
    value.binding_manifest_sha256[0] = 1;
    return value;
}

static struct fwlab_owner_transition_token_v0 owner_transition(void)
{
    struct fwlab_owner_transition_token_v0 value;

    memset(&value, 0, sizeof(value));
    value.version = FWLAB_OWNER_CONTROL_V0_VERSION;
    value.size = sizeof(value);
    value.type_tag = FWLAB_OWNER_TRANSITION_V0_TAG;
    value.function_instance_nonce = 0x4001;
    value.transition_uid = 0x4002;
    value.old_owner_epoch = 7;
    value.no_owner_epoch = 8;
    value.old_controller_epoch = 3;
    value.old_execution_epoch = 5;
    value.generation = 1;
    value.old_owner_kind = FWLAB_OWNER_V0_HOST_NATIVE;
    return value;
}

static struct fwlab_owner_zero_certificate_v0 zero_certificate(void)
{
    struct fwlab_owner_zero_certificate_v0 value;

    memset(&value, 0, sizeof(value));
    value.version = FWLAB_OWNER_CONTROL_V0_VERSION;
    value.size = sizeof(value);
    value.type_tag = FWLAB_OWNER_ZERO_CERTIFICATE_V0_TAG;
    value.transition = owner_transition();
    value.certificate_uid = 0x4003;
    value.generation = 1;
    value.sq_capture_closed = 1;
    value.capability_mint_closed = 1;
    value.ftl_epoch_quiescent = 1;
    value.nfc_epoch_quiescent = 1;
    value.routes_cleared = 1;
    value.bar_volatile_cleared = 1;
    value.ftl_epoch_proof[0] = 1;
    value.nfc_epoch_proof[0] = 1;
    value.binding_manifest_sha256[0] = 1;
    return value;
}

static enum fwlab_spine_result_v0 owner_revoke_stub(
    void *context,
    const struct fwlab_owner_revoke_key_v0 *key,
    struct fwlab_owner_revoke_status_v0 *status)
{
    (void)context;
    (void)key;
    (void)status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 owner_drain_stub(
    void *context,
    const struct fwlab_owner_transition_token_v0 *transition,
    uint32_t budget,
    struct fwlab_owner_step_result_v0 *result)
{
    (void)context;
    (void)transition;
    (void)budget;
    (void)result;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 owner_grant_stub(
    void *context,
    const struct fwlab_owner_grant_key_v0 *key,
    struct fwlab_owner_grant_status_v0 *status)
{
    (void)context;
    (void)key;
    (void)status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 owner_observe_stub(
    void *context,
    struct fwlab_owner_epoch_state_v0 *state)
{
    (void)context;
    (void)state;
    return FWLAB_SPINE_V0_OK;
}

static int test_owner_control(void)
{
    const struct fwlab_owner_control_ops_v0 ops = {
        .version = FWLAB_OWNER_CONTROL_V0_VERSION,
        .size = sizeof(ops),
        .revoke_start = owner_revoke_stub,
        .revoke_query = owner_revoke_stub,
        .drain_step = owner_drain_stub,
        .grant_start = owner_grant_stub,
        .grant_query = owner_grant_stub,
        .observe = owner_observe_stub,
    };
    struct fwlab_owner_grant_key_v0 key;
    struct fwlab_owner_grant_key_v0 alternate_key;
    struct fwlab_owner_grant_status_v0 status;
    struct fwlab_owner_grant_status_v0 alternate_status;
    struct fwlab_owner_control_port_v0 port;
    struct fwlab_owner_stable_identity_v0 stable = stable_identity();
    struct fwlab_owner_transition_token_v0 transition = owner_transition();
    struct fwlab_owner_zero_certificate_v0 certificate = zero_certificate();

    CHECK(fwlab_owner_stable_identity_v0_valid(&stable));
    stable.reserved2[0] = 1;
    CHECK(!fwlab_owner_stable_identity_v0_valid(&stable));
    stable = stable_identity();
    CHECK(fwlab_owner_transition_token_v0_valid(&transition));
    transition.type_tag = FWLAB_COMPLETION_LEASE_V0_TAG;
    CHECK(!fwlab_owner_transition_token_v0_valid(&transition));
    transition = owner_transition();
    CHECK(fwlab_owner_zero_certificate_v0_valid(&certificate));

    memset(&key, 0, sizeof(key));
    key.version = FWLAB_OWNER_CONTROL_V0_VERSION;
    key.size = sizeof(key);
    key.transition = transition;
    key.certificate = certificate;
    key.client_uid = 0x4004;
    key.target_owner = FWLAB_OWNER_V0_VFIO;
    CHECK(fwlab_owner_grant_key_v0_valid(&key));

    memset(&port, 0, sizeof(port));
    port.ops = &ops;
    port.context = &port;
    port.stable = stable;
    port.generation = 1;
    CHECK(fwlab_owner_control_port_v0_valid(&port));
    CHECK(fwlab_owner_grant_key_v0_valid_for_port(&key, &port));

    memset(&status, 0, sizeof(status));
    status.version = FWLAB_OWNER_CONTROL_V0_VERSION;
    status.size = sizeof(status);
    status.current.version = FWLAB_OWNER_CONTROL_V0_VERSION;
    status.current.size = sizeof(status.current);
    status.current.function_instance_nonce = transition.function_instance_nonce;
    status.current.owner_epoch = transition.no_owner_epoch;
    status.current.controller_epoch = transition.old_controller_epoch + 1;
    status.current.execution_epoch = transition.old_execution_epoch + 1;
    status.current.owner_kind = key.target_owner;
    status.current.phase = FWLAB_OWNER_V0_OWNED;
    memcpy(status.current.binding_manifest_sha256,
           certificate.binding_manifest_sha256,
           sizeof(status.current.binding_manifest_sha256));
    CHECK(fwlab_owner_grant_status_v0_matches_key(&status, &key));
    CHECK(fwlab_owner_grant_status_v0_matches_key_for_port(
        &status, &key, &port));
    status.current.controller_epoch = 1;
    status.current.execution_epoch = 1;
    CHECK(!fwlab_owner_grant_status_v0_matches_key(&status, &key));
    status.current.controller_epoch = transition.old_controller_epoch + 1;
    status.current.execution_epoch = transition.old_execution_epoch + 1;
    status.current.owner_epoch++;
    CHECK(!fwlab_owner_grant_status_v0_matches_key(&status, &key));
    status.current.owner_epoch = transition.no_owner_epoch;

    alternate_key = key;
    alternate_key.transition.old_controller_epoch = UINT32_MAX;
    alternate_key.certificate.transition.old_controller_epoch = UINT32_MAX;
    CHECK(fwlab_owner_transition_token_v0_valid(
        &alternate_key.transition));
    CHECK(!fwlab_owner_grant_key_v0_valid(&alternate_key));
    alternate_key = key;
    alternate_key.transition.old_execution_epoch = UINT32_MAX;
    alternate_key.certificate.transition.old_execution_epoch = UINT32_MAX;
    CHECK(!fwlab_owner_grant_key_v0_valid(&alternate_key));

    alternate_key = key;
    alternate_status = status;
    alternate_key.certificate.binding_manifest_sha256[0] = 2;
    alternate_status.current.binding_manifest_sha256[0] = 2;
    CHECK(fwlab_owner_grant_key_v0_valid(&alternate_key));
    CHECK(fwlab_owner_grant_status_v0_matches_key(
        &alternate_status, &alternate_key));
    CHECK(!fwlab_owner_grant_key_v0_valid_for_port(
        &alternate_key, &port));
    CHECK(!fwlab_owner_grant_status_v0_matches_key_for_port(
        &alternate_status, &alternate_key, &port));

    alternate_key = key;
    alternate_status = status;
    alternate_key.transition.function_instance_nonce++;
    alternate_key.certificate.transition.function_instance_nonce++;
    alternate_status.current.function_instance_nonce++;
    CHECK(fwlab_owner_grant_key_v0_valid(&alternate_key));
    CHECK(fwlab_owner_grant_status_v0_matches_key(
        &alternate_status, &alternate_key));
    CHECK(!fwlab_owner_grant_key_v0_valid_for_port(
        &alternate_key, &port));
    CHECK(!fwlab_owner_grant_status_v0_matches_key_for_port(
        &alternate_status, &alternate_key, &port));
    return 0;
}

int main(void)
{
    CHECK(test_action_program() == 0);
    CHECK(test_buffer_and_dma() == 0);
    CHECK(test_block_service() == 0);
    CHECK(test_owner_control() == 0);
    puts("S0-A public contracts: PASS (families=5 actions=9 nominal_tokens=4)");
    return 0;
}
