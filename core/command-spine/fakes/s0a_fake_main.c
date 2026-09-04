// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: BSD-3-Clause

#include "spine_anchor_internal.h"

#include "fwlab/contracts/block_service_v0.h"
#include "fwlab/contracts/controller_buffer_v0.h"
#include "fwlab/contracts/host_data_v0.h"
#include "fwlab/contracts/owner_control_v0.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                       \
    do {                                                                        \
        if (!(expression)) {                                                    \
            fprintf(stderr, "S0-A fake check failed: %s:%d: %s\n",           \
                    __FILE__, __LINE__, #expression);                           \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static enum fwlab_spine_result_v0 fake_action_submit(
    void *context, const struct fwlab_host_action_token_v0 *token,
    const struct fwlab_host_action_argument_ref_v0 *argument,
    struct fwlab_host_action_submit_result_v0 *result)
{
    (void)context;
    (void)token;
    (void)argument;
    (void)result;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_action_query(
    void *context, const struct fwlab_host_action_token_v0 *token,
    struct fwlab_host_action_status_v0 *status)
{
    (void)context;
    (void)token;
    (void)status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_action_control(
    void *context, const struct fwlab_host_action_token_v0 *token)
{
    (void)context;
    (void)token;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_action_epoch_close(
    void *context, uint64_t instance, uint32_t epoch)
{
    (void)context;
    (void)instance;
    (void)epoch;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_action_epoch_quiescent(
    void *context, uint64_t instance, uint32_t epoch, uint8_t *quiescent)
{
    (void)context;
    (void)instance;
    (void)epoch;
    if (quiescent != NULL)
        *quiescent = 1;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_profile_plan(
    void *context, const struct fwlab_nvme_command *command,
    struct fwlab_host_action_program_v0 *program)
{
    (void)context;
    (void)command;
    (void)program;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_profile_complete(
    void *context, const struct fwlab_host_action_program_v0 *program,
    const struct fwlab_host_action_status_v0 *status, uint16_t status_count,
    struct fwlab_nvme_completion_intent *intent)
{
    (void)context;
    (void)program;
    (void)status;
    (void)status_count;
    (void)intent;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_profile_retire(
    void *context, const struct fwlab_host_action_program_v0 *program)
{
    (void)context;
    (void)program;
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_host_action_driver_ops_v0 action_ops = {
    .version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION,
    .size = sizeof(action_ops),
    .submit = fake_action_submit,
    .query = fake_action_query,
    .cancel = fake_action_control,
    .retire_start = fake_action_control,
    .retire_query = fake_action_query,
    .epoch_close = fake_action_epoch_close,
    .epoch_quiescent = fake_action_epoch_quiescent,
};

static const struct fwlab_host_profile_ops_v0 profile_ops = {
    .version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION,
    .size = sizeof(profile_ops),
    .plan = fake_profile_plan,
    .complete = fake_profile_complete,
    .retire = fake_profile_retire,
};

static enum fwlab_controller_buffer_result_v0 fake_buffer_acquire(
    void *context, const struct fwlab_controller_buffer_acquire_v0 *request,
    struct fwlab_controller_buffer_lease_v0 *lease)
{
    (void)context;
    (void)request;
    (void)lease;
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 fake_buffer_read(
    void *context, const struct fwlab_controller_buffer_lease_v0 *lease,
    const struct fwlab_controller_buffer_span_v0 *span, void *output,
    size_t output_size)
{
    (void)context;
    (void)lease;
    (void)span;
    (void)output;
    (void)output_size;
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 fake_buffer_write(
    void *context, const struct fwlab_controller_buffer_lease_v0 *lease,
    const struct fwlab_controller_buffer_span_v0 *span, const void *input,
    size_t input_size)
{
    (void)context;
    (void)lease;
    (void)span;
    (void)input;
    (void)input_size;
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 fake_buffer_copy(
    void *context,
    const struct fwlab_controller_buffer_lease_v0 *destination,
    const struct fwlab_controller_buffer_span_v0 *destination_span,
    const struct fwlab_controller_buffer_lease_v0 *source,
    const struct fwlab_controller_buffer_span_v0 *source_span)
{
    (void)context;
    (void)destination;
    (void)destination_span;
    (void)source;
    (void)source_span;
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 fake_buffer_release(
    void *context, const struct fwlab_controller_buffer_lease_v0 *lease)
{
    (void)context;
    (void)lease;
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 fake_buffer_epoch(
    void *context, uint64_t instance, uint32_t epoch)
{
    (void)context;
    (void)instance;
    (void)epoch;
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static enum fwlab_controller_buffer_result_v0 fake_buffer_quiescent(
    void *context, uint64_t instance, uint32_t epoch, uint32_t *active,
    uint8_t *quiescent)
{
    (void)context;
    (void)instance;
    (void)epoch;
    if (active != NULL)
        *active = 0;
    if (quiescent != NULL)
        *quiescent = 1;
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}

static const struct fwlab_controller_buffer_ops_v0 buffer_ops = {
    .version = FWLAB_CONTROLLER_BUFFER_V0_VERSION,
    .size = sizeof(buffer_ops),
    .acquire = fake_buffer_acquire,
    .read = fake_buffer_read,
    .write = fake_buffer_write,
    .copy = fake_buffer_copy,
    .release = fake_buffer_release,
    .epoch_close = fake_buffer_epoch,
    .epoch_quiescent = fake_buffer_quiescent,
};

static enum fwlab_spine_result_v0 fake_authority_mint(
    void *context, const struct fwlab_host_dma_mint_request_v0 *request,
    struct fwlab_host_dma_authority_ref_v0 *authority)
{
    (void)context;
    (void)request;
    (void)authority;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_authority_release(
    void *context, const struct fwlab_host_dma_authority_ref_v0 *authority)
{
    (void)context;
    (void)authority;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_dma_reserve(
    void *context, const struct fwlab_host_action_token_v0 *action,
    struct fwlab_dma_op_token_v0 *operation)
{
    (void)context;
    (void)action;
    (void)operation;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_dma_submit(
    void *context, const struct fwlab_dma_request_v0 *request,
    struct fwlab_dma_submit_result_v0 *result)
{
    (void)context;
    (void)request;
    (void)result;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_dma_query(
    void *context, const struct fwlab_dma_op_token_v0 *operation,
    struct fwlab_dma_status_v0 *status)
{
    (void)context;
    (void)operation;
    (void)status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_dma_control(
    void *context, const struct fwlab_dma_op_token_v0 *operation)
{
    (void)context;
    (void)operation;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_host_epoch_close(
    void *context, uint64_t instance, uint32_t epoch)
{
    (void)context;
    (void)instance;
    (void)epoch;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_host_epoch_quiescent(
    void *context, uint64_t instance, uint32_t epoch,
    struct fwlab_host_data_epoch_status_v0 *status)
{
    (void)context;
    (void)instance;
    (void)epoch;
    (void)status;
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_host_data_ops_v0 host_data_ops = {
    .version = FWLAB_HOST_DATA_V0_VERSION,
    .size = sizeof(host_data_ops),
    .authority_mint = fake_authority_mint,
    .authority_release = fake_authority_release,
    .token_reserve = fake_dma_reserve,
    .submit = fake_dma_submit,
    .query = fake_dma_query,
    .cancel = fake_dma_control,
    .retire_start = fake_dma_control,
    .retire_query = fake_dma_query,
    .epoch_close = fake_host_epoch_close,
    .epoch_quiescent = fake_host_epoch_quiescent,
};

static const struct fwlab_host_data_reconcile_ops_v0 reconcile_ops = {
    .version = FWLAB_HOST_DATA_V0_VERSION,
    .size = sizeof(reconcile_ops),
    .epoch_close = fake_host_epoch_close,
    .dma_query = fake_dma_query,
    .dma_cancel = fake_dma_control,
    .dma_retire_start = fake_dma_control,
    .dma_retire_query = fake_dma_query,
    .buffer_read = fake_buffer_read,
    .buffer_write = fake_buffer_write,
    .buffer_copy = fake_buffer_copy,
    .buffer_release = fake_buffer_release,
    .authority_release = fake_authority_release,
    .epoch_quiescent = fake_host_epoch_quiescent,
};

static enum fwlab_spine_result_v0 fake_block_submit(
    void *context, const struct fwlab_block_request_v0 *request,
    struct fwlab_block_submit_result_v0 *result)
{
    (void)context;
    (void)request;
    (void)result;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_block_query(
    void *context, const struct fwlab_block_op_token_v0 *operation,
    struct fwlab_block_status_v0 *status)
{
    (void)context;
    (void)operation;
    (void)status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_block_control(
    void *context, const struct fwlab_block_op_token_v0 *operation)
{
    (void)context;
    (void)operation;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_block_epoch_close(
    void *context, uint64_t instance, uint32_t epoch)
{
    (void)context;
    (void)instance;
    (void)epoch;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_block_epoch_quiescent(
    void *context, uint64_t instance, uint32_t epoch,
    struct fwlab_block_epoch_status_v0 *status)
{
    (void)context;
    (void)instance;
    (void)epoch;
    (void)status;
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_block_service_ops_v0 block_ops = {
    .version = FWLAB_BLOCK_SERVICE_V0_VERSION,
    .size = sizeof(block_ops),
    .submit = fake_block_submit,
    .query = fake_block_query,
    .cancel = fake_block_control,
    .retire_start = fake_block_control,
    .retire_query = fake_block_query,
    .epoch_close = fake_block_epoch_close,
    .epoch_quiescent = fake_block_epoch_quiescent,
};

static enum fwlab_spine_result_v0 fake_owner_revoke(
    void *context, const struct fwlab_owner_revoke_key_v0 *key,
    struct fwlab_owner_revoke_status_v0 *status)
{
    (void)context;
    (void)key;
    (void)status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_owner_drain(
    void *context, const struct fwlab_owner_transition_token_v0 *transition,
    uint32_t budget, struct fwlab_owner_step_result_v0 *result)
{
    (void)context;
    (void)transition;
    (void)budget;
    (void)result;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_owner_grant(
    void *context, const struct fwlab_owner_grant_key_v0 *key,
    struct fwlab_owner_grant_status_v0 *status)
{
    (void)context;
    (void)key;
    (void)status;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_owner_observe(
    void *context, struct fwlab_owner_epoch_state_v0 *state)
{
    (void)context;
    (void)state;
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_owner_control_ops_v0 owner_ops = {
    .version = FWLAB_OWNER_CONTROL_V0_VERSION,
    .size = sizeof(owner_ops),
    .revoke_start = fake_owner_revoke,
    .revoke_query = fake_owner_revoke,
    .drain_step = fake_owner_drain,
    .grant_start = fake_owner_grant,
    .grant_query = fake_owner_grant,
    .observe = fake_owner_observe,
};

const struct fwlab_sq_consumer_anchor_v0
    fwlab_authoritative_sq_consumer_v0 = {
        .type_tag = FWLAB_SQ_CONSUMER_ANCHOR_V0_TAG,
    };

const struct fwlab_cqe_publisher_anchor_v0
    fwlab_authoritative_cqe_publisher_v0 = {
        .type_tag = FWLAB_CQE_PUBLISHER_ANCHOR_V0_TAG,
    };

int main(void)
{
    int context = 1;
    struct fwlab_host_action_driver_table_v0 drivers;
    struct fwlab_host_profile_adapter_v0 profile;
    struct fwlab_controller_buffer_port_v0 buffer;
    struct fwlab_host_data_port_v0 host_data;
    struct fwlab_host_data_reconcile_port_v0 reconcile;
    struct fwlab_block_service_v0 block;
    struct fwlab_owner_control_port_v0 owner;
    uint32_t index;

    memset(&drivers, 0, sizeof(drivers));
    drivers.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    drivers.size = sizeof(drivers);
    drivers.entry_count = FWLAB_HOST_ACTION_V0_KIND_COUNT;
    for (index = 0; index < FWLAB_HOST_ACTION_V0_KIND_COUNT; ++index) {
        drivers.entry[index].version =
            FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
        drivers.entry[index].size = sizeof(drivers.entry[index]);
        drivers.entry[index].kind = (uint16_t)(index + 1);
        drivers.entry[index].generation = index + 1;
        drivers.entry[index].ops = &action_ops;
        drivers.entry[index].context = &context;
    }

    memset(&profile, 0, sizeof(profile));
    profile.ops = &profile_ops;
    profile.context = &context;
    profile.generation = 1;

    memset(&buffer, 0, sizeof(buffer));
    buffer.ops = &buffer_ops;
    buffer.context = &context;
    buffer.issuer_nonce = 0x2001;
    buffer.generation = 1;

    memset(&host_data, 0, sizeof(host_data));
    host_data.ops = &host_data_ops;
    host_data.context = &context;
    host_data.buffer = buffer;
    host_data.authority_issuer_nonce = 0x2002;
    host_data.dma_issuer_nonce = 0x2003;
    host_data.generation = 1;

    memset(&reconcile, 0, sizeof(reconcile));
    reconcile.ops = &reconcile_ops;
    reconcile.context = &context;
    reconcile.authority_issuer_nonce = 0x2002;
    reconcile.buffer_issuer_nonce = 0x2001;
    reconcile.dma_issuer_nonce = 0x2003;
    reconcile.generation = 1;

    memset(&block, 0, sizeof(block));
    block.ops = &block_ops;
    block.context = &context;
    block.provider_nonce = 0x3001;
    block.generation = 1;

    memset(&owner, 0, sizeof(owner));
    owner.ops = &owner_ops;
    owner.context = &context;
    owner.generation = 1;
    owner.stable.version = FWLAB_OWNER_CONTROL_V0_VERSION;
    owner.stable.size = sizeof(owner.stable);
    owner.stable.function_instance_nonce = 0x4001;
    owner.stable.media_uuid[0] = 1;
    owner.stable.media_format_version = 1;
    owner.stable.binding_manifest_sha256[0] = 1;

    CHECK(fwlab_spine_construction_valid());
    CHECK(fwlab_host_action_driver_table_v0_valid(&drivers));
    CHECK(fwlab_host_profile_adapter_v0_valid(&profile));
    CHECK(fwlab_controller_buffer_port_v0_valid(&buffer));
    CHECK(fwlab_host_data_port_v0_valid(&host_data));
    CHECK(fwlab_host_data_reconcile_port_v0_valid(&reconcile));
    CHECK(fwlab_block_service_v0_valid(&block));
    CHECK(fwlab_owner_control_port_v0_valid(&owner));

    puts("S0-A fake adjacency: PASS (profile/action/data/block/owner markers=2)");
    return 0;
}
