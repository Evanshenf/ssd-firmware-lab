/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fwlab/contracts/block_service_v0.h"
#include "fwlab/contracts/host_data_v0.h"
#include "fwlab/contracts/owner_control_v0.h"
#include "fwlab/portable/host_action_program_v0.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(FWLAB_HOST_ACTION_V0_MAX_ACTIONS == 8u,
               "S0 v0 has exactly eight finite action slots");
_Static_assert(FWLAB_HOST_ACTION_V0_KIND_COUNT == 9u,
               "S0 v0 has exactly nine closed action kinds");
_Static_assert(FWLAB_HOST_DMA_AUTHORITY_V0_TAG != FWLAB_DMA_OP_TOKEN_V0_TAG &&
                   FWLAB_HOST_DMA_AUTHORITY_V0_TAG !=
                       FWLAB_CONTROLLER_BUFFER_LEASE_V0_TAG &&
                   FWLAB_HOST_DMA_AUTHORITY_V0_TAG !=
                       FWLAB_COMPLETION_LEASE_V0_TAG &&
                   FWLAB_DMA_OP_TOKEN_V0_TAG !=
                       FWLAB_CONTROLLER_BUFFER_LEASE_V0_TAG &&
                   FWLAB_DMA_OP_TOKEN_V0_TAG != FWLAB_COMPLETION_LEASE_V0_TAG &&
                   FWLAB_CONTROLLER_BUFFER_LEASE_V0_TAG !=
                       FWLAB_COMPLETION_LEASE_V0_TAG,
               "authority and lifetime tags are pairwise distinct");
_Static_assert(sizeof(struct fwlab_host_dma_authority_ref_v0) !=
                       sizeof(struct fwlab_dma_op_token_v0) &&
                   sizeof(struct fwlab_host_dma_authority_ref_v0) !=
                       sizeof(struct fwlab_controller_buffer_lease_v0) &&
                   sizeof(struct fwlab_host_dma_authority_ref_v0) !=
                       sizeof(struct fwlab_completion_lease_v0) &&
                   sizeof(struct fwlab_dma_op_token_v0) !=
                       sizeof(struct fwlab_controller_buffer_lease_v0) &&
                   sizeof(struct fwlab_dma_op_token_v0) !=
                       sizeof(struct fwlab_completion_lease_v0) &&
                   sizeof(struct fwlab_controller_buffer_lease_v0) !=
                       sizeof(struct fwlab_completion_lease_v0),
               "authority and lifetime layouts are pairwise distinct");

static int spine_bytes_zero(const void *value, size_t size)
{
    const unsigned char *bytes = value;
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

static int spine_bytes_nonzero(const void *value, size_t size)
{
    return value != NULL && !spine_bytes_zero(value, size);
}

static int spine_handle_valid(
    const struct fwlab_nvme_command_handle *handle)
{
    return handle != NULL && handle->instance_nonce != 0 &&
           handle->command_uid != 0 && handle->controller_epoch != 0 &&
           handle->generation != 0;
}

static int spine_handle_equal(
    const struct fwlab_nvme_command_handle *left,
    const struct fwlab_nvme_command_handle *right)
{
    return left != NULL && right != NULL &&
           left->instance_nonce == right->instance_nonce &&
           left->command_uid == right->command_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->generation == right->generation;
}

static int spine_origin_valid(const struct fwlab_nvme_origin_token *origin)
{
    return origin != NULL &&
           (origin->word[0] != 0 || origin->word[1] != 0);
}

static int spine_origin_equal(
    const struct fwlab_nvme_origin_token *left,
    const struct fwlab_nvme_origin_token *right)
{
    return left != NULL && right != NULL &&
           left->word[0] == right->word[0] &&
           left->word[1] == right->word[1];
}

static int spine_uid_range_valid(const struct fwlab_uid_range_v0 *range)
{
    return range != NULL && range->next != 0 && range->maximum != 0 &&
           range->next <= range->maximum;
}

static int spine_action_kind_valid(uint16_t kind)
{
    return kind >= FWLAB_HOST_ACTION_V0_PAYLOAD_FILL &&
           kind <= FWLAB_HOST_ACTION_V0_BLOCK_TRIM;
}

static uint32_t spine_action_witness(uint16_t kind)
{
    switch (kind) {
    case FWLAB_HOST_ACTION_V0_PAYLOAD_FILL:
        return FWLAB_HOST_WITNESS_V0_PAYLOAD_READY;
    case FWLAB_HOST_ACTION_V0_QUEUE_EFFECT:
        return FWLAB_HOST_WITNESS_V0_QUEUE_EFFECT;
    case FWLAB_HOST_ACTION_V0_TARGET_RESOLVE:
        return FWLAB_HOST_WITNESS_V0_TARGET_RESOLVED;
    case FWLAB_HOST_ACTION_V0_DMA_IN:
        return FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE;
    case FWLAB_HOST_ACTION_V0_DMA_OUT:
        return FWLAB_HOST_WITNESS_V0_DMA_OUT_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_READ:
        return FWLAB_HOST_WITNESS_V0_BLOCK_READ_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_WRITE:
        return FWLAB_HOST_WITNESS_V0_BLOCK_WRITE_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_FLUSH:
        return FWLAB_HOST_WITNESS_V0_BLOCK_FLUSH_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_TRIM:
        return FWLAB_HOST_WITNESS_V0_BLOCK_TRIM_COMPLETE;
    default:
        return 0;
    }
}

static int spine_action_token_equal(
    const struct fwlab_host_action_token_v0 *left,
    const struct fwlab_host_action_token_v0 *right)
{
    return left != NULL && right != NULL &&
           left->version == right->version && left->size == right->size &&
           left->type_tag == right->type_tag &&
           spine_handle_equal(&left->command, &right->command) &&
           spine_origin_equal(&left->origin, &right->origin) &&
           left->action_uid == right->action_uid &&
           left->generation == right->generation &&
           left->ordinal == right->ordinal && left->kind == right->kind;
}

int fwlab_host_lifecycle_config_v0_valid(
    const struct fwlab_host_lifecycle_config_v0 *config)
{
    return config != NULL &&
           config->version == FWLAB_HOST_ACTION_PROGRAM_V0_VERSION &&
           config->size == sizeof(*config) && config->reserved0 == 0 &&
           config->lifecycle_instance_nonce != 0 &&
           config->execution_epoch != 0 && config->generation != 0 &&
           config->command_capacity != 0 &&
           config->actions_per_command == FWLAB_HOST_ACTION_V0_MAX_ACTIONS &&
           config->reserved1 == 0 &&
           spine_uid_range_valid(&config->command_uid) &&
           spine_uid_range_valid(&config->action_uid) &&
           spine_uid_range_valid(&config->abort_uid) &&
           spine_uid_range_valid(&config->completion_lease_uid) &&
           spine_bytes_zero(config->reserved2, sizeof(config->reserved2));
}

int fwlab_host_action_argument_ref_v0_valid(
    const struct fwlab_host_action_argument_ref_v0 *argument)
{
    return argument != NULL &&
           argument->version == FWLAB_HOST_ACTION_PROGRAM_V0_VERSION &&
           argument->size == sizeof(*argument) && argument->reserved0 == 0 &&
           argument->adapter_instance_nonce != 0 &&
           argument->argument_uid != 0 && argument->generation != 0 &&
           argument->ordinal < FWLAB_HOST_ACTION_V0_MAX_ACTIONS &&
           spine_action_kind_valid(argument->kind) &&
           spine_bytes_zero(argument->reserved1,
                            sizeof(argument->reserved1));
}

int fwlab_host_completion_recipe_ref_v0_valid(
    const struct fwlab_host_completion_recipe_ref_v0 *recipe)
{
    return recipe != NULL &&
           recipe->version == FWLAB_HOST_ACTION_PROGRAM_V0_VERSION &&
           recipe->size == sizeof(*recipe) && recipe->reserved0 == 0 &&
           recipe->adapter_instance_nonce != 0 && recipe->recipe_uid != 0 &&
           recipe->generation != 0 &&
           spine_bytes_zero(recipe->reserved1, sizeof(recipe->reserved1));
}

int fwlab_host_action_desc_v0_valid(
    const struct fwlab_host_action_desc_v0 *action)
{
    const uint32_t action_bits =
        (UINT32_C(1) << FWLAB_HOST_ACTION_V0_MAX_ACTIONS) - UINT32_C(1);

    return action != NULL &&
           action->version == FWLAB_HOST_ACTION_PROGRAM_V0_VERSION &&
           action->size == sizeof(*action) &&
           action->ordinal < FWLAB_HOST_ACTION_V0_MAX_ACTIONS &&
           spine_action_kind_valid(action->kind) &&
           (action->dependency_mask & ~action_bits) == 0 &&
           (action->required_witness_mask &
            ~FWLAB_HOST_ACTION_V0_WITNESS_ALL) == 0 &&
           action->produced_witness_mask ==
               spine_action_witness(action->kind) &&
           action->reserved0 == 0 &&
           fwlab_host_action_argument_ref_v0_valid(&action->argument) &&
           action->argument.ordinal == action->ordinal &&
           action->argument.kind == action->kind &&
           spine_bytes_zero(action->reserved1, sizeof(action->reserved1));
}

int fwlab_host_action_program_v0_valid(
    const struct fwlab_host_action_program_v0 *program)
{
    uint32_t dependency_closure[FWLAB_HOST_ACTION_V0_MAX_ACTIONS] = {0};
    uint32_t all_produced = 0;
    uint32_t index;

    if (program == NULL ||
        program->version != FWLAB_HOST_ACTION_PROGRAM_V0_VERSION ||
        program->size != sizeof(*program) || program->reserved0 != 0 ||
        !spine_handle_valid(&program->command) ||
        !spine_origin_valid(&program->origin) || program->program_uid == 0 ||
        program->program_generation == 0 ||
        program->action_count > FWLAB_HOST_ACTION_V0_MAX_ACTIONS ||
        program->reserved1 != 0 ||
        (program->completion_required_witness_mask &
         ~FWLAB_HOST_ACTION_V0_WITNESS_ALL) != 0 ||
        program->reserved2 != 0 ||
        !fwlab_host_completion_recipe_ref_v0_valid(
            &program->completion_recipe) ||
        spine_bytes_zero(program->reserved3, sizeof(program->reserved3)) ==
            0) {
        return 0;
    }

    for (index = 0; index < program->action_count; ++index) {
        const struct fwlab_host_action_desc_v0 *action =
            &program->action[index];
        uint32_t preceding_mask =
            index == 0 ? 0 : (UINT32_C(1) << index) - UINT32_C(1);
        uint32_t available_witnesses = 0;
        uint32_t dependency;

        if (!fwlab_host_action_desc_v0_valid(action) ||
            action->ordinal != index ||
            (action->dependency_mask & ~preceding_mask) != 0 ||
            (all_produced & action->produced_witness_mask) != 0) {
            return 0;
        }
        for (dependency = 0; dependency < index; ++dependency) {
            if (program->action[dependency].kind == action->kind) {
                return 0;
            }
        }
        dependency_closure[index] = action->dependency_mask;
        for (dependency = 0; dependency < index; ++dependency) {
            if ((action->dependency_mask &
                 (UINT32_C(1) << dependency)) != 0) {
                dependency_closure[index] |=
                    dependency_closure[dependency];
            }
        }
        for (dependency = 0; dependency < index; ++dependency) {
            if ((dependency_closure[index] &
                 (UINT32_C(1) << dependency)) != 0) {
                available_witnesses |=
                    program->action[dependency].produced_witness_mask;
            }
        }
        if ((action->required_witness_mask & ~available_witnesses) != 0) {
            return 0;
        }
        all_produced |= action->produced_witness_mask;
    }
    for (; index < FWLAB_HOST_ACTION_V0_MAX_ACTIONS; ++index) {
        if (!spine_bytes_zero(&program->action[index],
                              sizeof(program->action[index]))) {
            return 0;
        }
    }
    return (program->completion_required_witness_mask & ~all_produced) == 0;
}

int fwlab_host_action_token_v0_valid(
    const struct fwlab_host_action_token_v0 *token)
{
    return token != NULL &&
           token->version == FWLAB_HOST_ACTION_PROGRAM_V0_VERSION &&
           token->size == sizeof(*token) &&
           token->type_tag == FWLAB_HOST_ACTION_TOKEN_V0_TAG &&
           spine_handle_valid(&token->command) &&
           spine_origin_valid(&token->origin) && token->action_uid != 0 &&
           token->generation != 0 &&
           token->ordinal < FWLAB_HOST_ACTION_V0_MAX_ACTIONS &&
           spine_action_kind_valid(token->kind) &&
           spine_bytes_zero(token->reserved, sizeof(token->reserved));
}

int fwlab_host_action_submit_result_v0_valid(
    const struct fwlab_host_action_submit_result_v0 *result)
{
    if (result == NULL ||
        result->version != FWLAB_HOST_ACTION_PROGRAM_V0_VERSION ||
        result->size != sizeof(*result) || result->reserved0 != 0 ||
        !fwlab_host_action_token_v0_valid(&result->token) ||
        result->disposition < FWLAB_HOST_ACTION_V0_ACCEPTED ||
        result->disposition > FWLAB_HOST_ACTION_V0_REJECTED ||
        !spine_bytes_zero(result->reserved1, sizeof(result->reserved1))) {
        return 0;
    }
    return result->disposition == FWLAB_HOST_ACTION_V0_REJECTED ||
           (result->fault_domain == 0 && result->fault_code == 0);
}

int fwlab_host_action_status_v0_valid(
    const struct fwlab_host_action_status_v0 *status)
{
    uint32_t expected_witness;

    if (status == NULL ||
        status->version != FWLAB_HOST_ACTION_PROGRAM_V0_VERSION ||
        status->size != sizeof(*status) || status->reserved0 != 0 ||
        !fwlab_host_action_token_v0_valid(&status->token) ||
        status->state < FWLAB_HOST_ACTION_V0_STATE_ACCEPTED ||
        status->state > FWLAB_HOST_ACTION_V0_STATE_QUARANTINED ||
        status->effect > FWLAB_HOST_ACTION_V0_EFFECT_UNKNOWN_PREFIX ||
        !spine_bytes_zero(status->reserved1, sizeof(status->reserved1))) {
        return 0;
    }
    if (status->state == FWLAB_HOST_ACTION_V0_STATE_ACCEPTED) {
        return status->terminal_kind == 0 &&
               status->produced_witness_mask == 0 && status->effect == 0 &&
               status->units_completed == 0 && status->fault_domain == 0 &&
               status->fault_code == 0;
    }
    if (status->terminal_kind < FWLAB_HOST_ACTION_V0_SUCCEEDED ||
        status->terminal_kind >
            FWLAB_HOST_ACTION_V0_TERMINAL_QUARANTINED ||
        ((status->state == FWLAB_HOST_ACTION_V0_STATE_QUARANTINED) !=
         (status->terminal_kind ==
          FWLAB_HOST_ACTION_V0_TERMINAL_QUARANTINED)) ||
        (status->effect == FWLAB_HOST_ACTION_V0_EFFECT_NONE &&
         status->units_completed != 0) ||
        ((status->effect == FWLAB_HOST_ACTION_V0_EFFECT_FULL ||
          status->effect == FWLAB_HOST_ACTION_V0_EFFECT_EXACT_PREFIX) &&
         status->units_completed == 0)) {
        return 0;
    }
    expected_witness = spine_action_witness(status->token.kind);
    if (status->terminal_kind == FWLAB_HOST_ACTION_V0_SUCCEEDED) {
        return status->produced_witness_mask == expected_witness &&
               status->fault_domain == 0 && status->fault_code == 0;
    }
    return status->produced_witness_mask == 0;
}

int fwlab_completion_lease_v0_valid(
    const struct fwlab_completion_lease_v0 *lease)
{
    return lease != NULL &&
           lease->version == FWLAB_HOST_ACTION_PROGRAM_V0_VERSION &&
           lease->size == sizeof(*lease) &&
           lease->type_tag == FWLAB_COMPLETION_LEASE_V0_TAG &&
           spine_handle_valid(&lease->command) &&
           spine_origin_valid(&lease->origin) && lease->issuer_nonce != 0 &&
           lease->lease_uid != 0 && lease->intent_generation != 0 &&
           lease->lease_generation != 0 &&
           spine_bytes_zero(lease->reserved, sizeof(lease->reserved));
}

static int spine_completion_intent_valid(
    const struct fwlab_nvme_completion_intent *intent)
{
    return intent != NULL &&
           intent->version == FWLAB_NVME_COMPLETION_VERSION &&
           intent->size == sizeof(*intent) && intent->reserved0 == 0 &&
           spine_handle_valid(&intent->handle) &&
           spine_origin_valid(&intent->origin) &&
           intent->status_code <= UINT8_MAX &&
           intent->status_code_type <= 7 &&
           intent->command_retry_delay <= 3 && intent->more <= 1 &&
           intent->do_not_retry <= 1 &&
           intent->effect_class <= FWLAB_NVME_EFFECT_UNKNOWN_PREFIX &&
           intent->reserved1 == 0;
}

int fwlab_host_completion_intent_v0_valid_for_program(
    const struct fwlab_host_action_program_v0 *program,
    const struct fwlab_host_action_status_v0 *status,
    uint16_t status_count,
    const struct fwlab_nvme_completion_intent *intent)
{
    uint32_t satisfied = 0;
    uint32_t index;

    if (!fwlab_host_action_program_v0_valid(program) ||
        !spine_completion_intent_valid(intent) ||
        !spine_handle_equal(&program->command, &intent->handle) ||
        !spine_origin_equal(&program->origin, &intent->origin) ||
        status_count != program->action_count ||
        (status_count != 0 && status == NULL) ||
        (status_count == 0 && intent->effect_class != FWLAB_NVME_EFFECT_NONE)) {
        return 0;
    }
    for (index = 0; index < status_count; ++index) {
        uint32_t available_witnesses = 0;
        uint32_t dependency_closure =
            program->action[index].dependency_mask;
        uint32_t prior;

        if (!fwlab_host_action_status_v0_valid(&status[index]) ||
            !spine_handle_equal(&status[index].token.command,
                                &program->command) ||
            !spine_origin_equal(&status[index].token.origin,
                                &program->origin) ||
            status[index].token.ordinal != index ||
            status[index].token.kind != program->action[index].kind ||
            (status[index].state != FWLAB_HOST_ACTION_V0_STATE_DRAINED &&
             status[index].state != FWLAB_HOST_ACTION_V0_STATE_RETIRED)) {
            return 0;
        }
        for (prior = 0; prior < index; ++prior) {
            if (status[index].token.action_uid ==
                status[prior].token.action_uid) {
                return 0;
            }
        }
        if (status[index].terminal_kind ==
            FWLAB_HOST_ACTION_V0_SUCCEEDED) {
            for (prior = index; prior > 0; --prior) {
                const uint32_t predecessor = prior - 1;

                if ((dependency_closure &
                     (UINT32_C(1) << predecessor)) != 0) {
                    dependency_closure |=
                        program->action[predecessor].dependency_mask;
                }
            }
            for (prior = 0; prior < index; ++prior) {
                if ((dependency_closure & (UINT32_C(1) << prior)) != 0 &&
                    status[prior].terminal_kind ==
                        FWLAB_HOST_ACTION_V0_SUCCEEDED) {
                    available_witnesses |=
                        status[prior].produced_witness_mask;
                }
            }
            if ((program->action[index].required_witness_mask &
                 ~available_witnesses) != 0) {
                return 0;
            }
            satisfied |= status[index].produced_witness_mask;
        }
    }
    return intent->status_code != 0 || intent->status_code_type != 0 ||
           (program->completion_required_witness_mask & ~satisfied) == 0;
}

int fwlab_host_action_driver_ops_v0_valid(
    const struct fwlab_host_action_driver_ops_v0 *ops)
{
    return ops != NULL &&
           ops->version == FWLAB_HOST_ACTION_PROGRAM_V0_VERSION &&
           ops->size == sizeof(*ops) && ops->reserved0 == 0 &&
           ops->submit != NULL && ops->query != NULL &&
           ops->cancel != NULL && ops->retire_start != NULL &&
           ops->retire_query != NULL && ops->epoch_close != NULL &&
           ops->epoch_quiescent != NULL &&
           spine_bytes_zero(ops->reserved1, sizeof(ops->reserved1));
}

int fwlab_host_action_driver_binding_v0_valid(
    const struct fwlab_host_action_driver_binding_v0 *binding)
{
    return binding != NULL &&
           binding->version == FWLAB_HOST_ACTION_PROGRAM_V0_VERSION &&
           binding->size == sizeof(*binding) &&
           spine_action_kind_valid(binding->kind) &&
           binding->reserved0 == 0 && binding->generation != 0 &&
           fwlab_host_action_driver_ops_v0_valid(binding->ops) &&
           binding->context != NULL &&
           spine_bytes_zero(binding->reserved1, sizeof(binding->reserved1));
}

int fwlab_host_action_driver_table_v0_valid(
    const struct fwlab_host_action_driver_table_v0 *table)
{
    uint32_t index;

    if (table == NULL ||
        table->version != FWLAB_HOST_ACTION_PROGRAM_V0_VERSION ||
        table->size != sizeof(*table) ||
        table->entry_count != FWLAB_HOST_ACTION_V0_KIND_COUNT ||
        table->reserved0 != 0 ||
        !spine_bytes_zero(table->reserved1, sizeof(table->reserved1))) {
        return 0;
    }
    for (index = 0; index < FWLAB_HOST_ACTION_V0_KIND_COUNT; ++index) {
        if (!fwlab_host_action_driver_binding_v0_valid(
                &table->entry[index]) ||
            table->entry[index].kind != index + 1) {
            return 0;
        }
    }
    return 1;
}

int fwlab_host_profile_ops_v0_valid(
    const struct fwlab_host_profile_ops_v0 *ops)
{
    return ops != NULL &&
           ops->version == FWLAB_HOST_ACTION_PROGRAM_V0_VERSION &&
           ops->size == sizeof(*ops) && ops->reserved0 == 0 &&
           ops->plan != NULL && ops->complete != NULL &&
           ops->retire != NULL &&
           spine_bytes_zero(ops->reserved1, sizeof(ops->reserved1));
}

int fwlab_host_profile_adapter_v0_valid(
    const struct fwlab_host_profile_adapter_v0 *adapter)
{
    return adapter != NULL &&
           fwlab_host_profile_ops_v0_valid(adapter->ops) &&
           adapter->context != NULL && adapter->generation != 0;
}

int fwlab_controller_buffer_lease_v0_valid(
    const struct fwlab_controller_buffer_lease_v0 *lease)
{
    return lease != NULL &&
           lease->version == FWLAB_CONTROLLER_BUFFER_V0_VERSION &&
           lease->size == sizeof(*lease) &&
           lease->type_tag == FWLAB_CONTROLLER_BUFFER_LEASE_V0_TAG &&
           lease->issuer_nonce != 0 && lease->buffer_uid != 0 &&
           lease->lease_uid != 0 && lease->generation != 0 &&
           lease->capacity_bytes != 0 && lease->rights != 0 &&
           (lease->rights & ~FWLAB_CONTROLLER_BUFFER_RIGHT_V0_ALL) == 0 &&
           lease->reserved0 == 0 &&
           spine_bytes_zero(lease->reserved1, sizeof(lease->reserved1));
}

int fwlab_controller_buffer_span_v0_valid(
    const struct fwlab_controller_buffer_span_v0 *span)
{
    return span != NULL &&
           span->version == FWLAB_CONTROLLER_BUFFER_V0_VERSION &&
           span->size == sizeof(*span) && span->reserved0 == 0 &&
           span->length != 0 && span->offset <= UINT32_MAX - span->length &&
           spine_bytes_zero(span->reserved1, sizeof(span->reserved1));
}

int fwlab_controller_buffer_span_v0_valid_for_lease(
    const struct fwlab_controller_buffer_span_v0 *span,
    const struct fwlab_controller_buffer_lease_v0 *lease,
    uint32_t required_rights)
{
    return fwlab_controller_buffer_span_v0_valid(span) &&
           fwlab_controller_buffer_lease_v0_valid(lease) &&
           required_rights != 0 &&
           (required_rights & ~FWLAB_CONTROLLER_BUFFER_RIGHT_V0_ALL) == 0 &&
           (lease->rights & required_rights) == required_rights &&
           span->offset <= lease->capacity_bytes &&
           span->length <= lease->capacity_bytes - span->offset;
}

int fwlab_controller_buffer_acquire_v0_valid(
    const struct fwlab_controller_buffer_acquire_v0 *request)
{
    return request != NULL &&
           request->version == FWLAB_CONTROLLER_BUFFER_V0_VERSION &&
           request->size == sizeof(*request) && request->reserved0 == 0 &&
           spine_handle_valid(&request->command) &&
           spine_origin_valid(&request->origin) && request->client_uid != 0 &&
           request->execution_epoch != 0 && request->capacity_bytes != 0 &&
           request->rights != 0 &&
           (request->rights & ~FWLAB_CONTROLLER_BUFFER_RIGHT_V0_ALL) == 0 &&
           spine_bytes_zero(request->reserved1,
                            sizeof(request->reserved1));
}

int fwlab_controller_buffer_ops_v0_valid(
    const struct fwlab_controller_buffer_ops_v0 *ops)
{
    return ops != NULL &&
           ops->version == FWLAB_CONTROLLER_BUFFER_V0_VERSION &&
           ops->size == sizeof(*ops) && ops->reserved0 == 0 &&
           ops->acquire != NULL && ops->read != NULL &&
           ops->write != NULL && ops->copy != NULL &&
           ops->release != NULL && ops->epoch_close != NULL &&
           ops->epoch_quiescent != NULL &&
           spine_bytes_zero(ops->reserved1, sizeof(ops->reserved1));
}

int fwlab_controller_buffer_port_v0_valid(
    const struct fwlab_controller_buffer_port_v0 *port)
{
    return port != NULL && fwlab_controller_buffer_ops_v0_valid(port->ops) &&
           port->context != NULL && port->issuer_nonce != 0 &&
           port->generation != 0 &&
           spine_bytes_zero(port->reserved, sizeof(port->reserved));
}

static int spine_data_direction_valid(uint8_t direction)
{
    return direction == FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER ||
           direction == FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST;
}

static int spine_dma_token_equal(
    const struct fwlab_dma_op_token_v0 *left,
    const struct fwlab_dma_op_token_v0 *right)
{
    return left != NULL && right != NULL &&
           left->version == right->version && left->size == right->size &&
           left->type_tag == right->type_tag &&
           spine_action_token_equal(&left->action, &right->action) &&
           left->issuer_nonce == right->issuer_nonce &&
           left->operation_uid == right->operation_uid &&
           left->generation == right->generation;
}

int fwlab_host_dma_authority_ref_v0_valid(
    const struct fwlab_host_dma_authority_ref_v0 *authority)
{
    return authority != NULL &&
           authority->version == FWLAB_HOST_DATA_V0_VERSION &&
           authority->size == sizeof(*authority) &&
           authority->type_tag == FWLAB_HOST_DMA_AUTHORITY_V0_TAG &&
           authority->issuer_nonce != 0 && authority->authority_uid != 0 &&
           authority->generation != 0 && authority->exact_bytes != 0 &&
           spine_data_direction_valid(authority->direction) &&
           spine_bytes_zero(authority->reserved0,
                            sizeof(authority->reserved0)) &&
           spine_bytes_zero(authority->reserved1,
                            sizeof(authority->reserved1));
}

int fwlab_host_dma_mint_request_v0_valid(
    const struct fwlab_host_dma_mint_request_v0 *request)
{
    return request != NULL &&
           request->version == FWLAB_HOST_DATA_V0_VERSION &&
           request->size == sizeof(*request) && request->reserved0 == 0 &&
           spine_handle_valid(&request->command) &&
           spine_origin_valid(&request->origin) && request->client_uid != 0 &&
           request->execution_epoch != 0 && request->exact_bytes != 0 &&
           spine_data_direction_valid(request->direction) &&
           spine_bytes_zero(request->reserved1,
                            sizeof(request->reserved1)) &&
           spine_bytes_zero(request->reserved2,
                            sizeof(request->reserved2));
}

int fwlab_dma_op_token_v0_valid(
    const struct fwlab_dma_op_token_v0 *operation)
{
    return operation != NULL &&
           operation->version == FWLAB_HOST_DATA_V0_VERSION &&
           operation->size == sizeof(*operation) &&
           operation->type_tag == FWLAB_DMA_OP_TOKEN_V0_TAG &&
           fwlab_host_action_token_v0_valid(&operation->action) &&
           (operation->action.kind == FWLAB_HOST_ACTION_V0_DMA_IN ||
            operation->action.kind == FWLAB_HOST_ACTION_V0_DMA_OUT) &&
           operation->issuer_nonce != 0 && operation->operation_uid != 0 &&
           operation->generation != 0 &&
           spine_bytes_zero(operation->reserved,
                            sizeof(operation->reserved));
}

int fwlab_dma_request_v0_valid(
    const struct fwlab_dma_request_v0 *request)
{
    uint32_t required_right;
    uint16_t expected_kind;

    if (request == NULL ||
        request->version != FWLAB_HOST_DATA_V0_VERSION ||
        request->size != sizeof(*request) || request->reserved0 != 0 ||
        !fwlab_dma_op_token_v0_valid(&request->operation) ||
        !fwlab_host_dma_authority_ref_v0_valid(&request->authority) ||
        !fwlab_controller_buffer_lease_v0_valid(&request->buffer) ||
        request->execution_epoch == 0 || request->exact_bytes == 0 ||
        !spine_data_direction_valid(request->direction) ||
        request->authority.direction != request->direction ||
        request->authority.exact_bytes != request->exact_bytes ||
        request->span.length != request->exact_bytes ||
        request->operation.issuer_nonce == request->authority.issuer_nonce ||
        request->operation.issuer_nonce == request->buffer.issuer_nonce ||
        request->authority.issuer_nonce == request->buffer.issuer_nonce ||
        !spine_bytes_zero(request->reserved1,
                          sizeof(request->reserved1)) ||
        !spine_bytes_zero(request->reserved2,
                          sizeof(request->reserved2))) {
        return 0;
    }
    if (request->direction == FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER) {
        expected_kind = FWLAB_HOST_ACTION_V0_DMA_IN;
        required_right = FWLAB_CONTROLLER_BUFFER_V0_WRITE;
    } else {
        expected_kind = FWLAB_HOST_ACTION_V0_DMA_OUT;
        required_right = FWLAB_CONTROLLER_BUFFER_V0_READ;
    }
    return request->operation.action.kind == expected_kind &&
           fwlab_controller_buffer_span_v0_valid_for_lease(
               &request->span, &request->buffer, required_right);
}

int fwlab_dma_submit_result_v0_valid(
    const struct fwlab_dma_submit_result_v0 *result)
{
    if (result == NULL || result->version != FWLAB_HOST_DATA_V0_VERSION ||
        result->size != sizeof(*result) || result->reserved0 != 0 ||
        !fwlab_dma_op_token_v0_valid(&result->operation) ||
        result->disposition < FWLAB_HOST_ACTION_V0_ACCEPTED ||
        result->disposition > FWLAB_HOST_ACTION_V0_REJECTED ||
        !spine_bytes_zero(result->reserved1, sizeof(result->reserved1))) {
        return 0;
    }
    return result->disposition == FWLAB_HOST_ACTION_V0_REJECTED ||
           (result->fault_domain == 0 && result->fault_code == 0);
}

int fwlab_dma_submit_result_v0_matches_request(
    const struct fwlab_dma_submit_result_v0 *result,
    const struct fwlab_dma_request_v0 *request)
{
    return fwlab_dma_submit_result_v0_valid(result) &&
           fwlab_dma_request_v0_valid(request) &&
           spine_dma_token_equal(&result->operation, &request->operation);
}

int fwlab_dma_status_v0_valid(const struct fwlab_dma_status_v0 *status)
{
    if (status == NULL || status->version != FWLAB_HOST_DATA_V0_VERSION ||
        status->size != sizeof(*status) || status->reserved0 != 0 ||
        !fwlab_dma_op_token_v0_valid(&status->operation) ||
        status->state < FWLAB_DMA_V0_STATE_ACCEPTED ||
        status->state > FWLAB_DMA_V0_STATE_QUARANTINED ||
        status->effect > FWLAB_HOST_ACTION_V0_EFFECT_UNKNOWN_PREFIX ||
        !spine_bytes_zero(status->reserved1, sizeof(status->reserved1))) {
        return 0;
    }
    if (status->state == FWLAB_DMA_V0_STATE_ACCEPTED) {
        return status->terminal_kind == 0 && status->effect == 0 &&
               status->bytes_completed == 0 && status->fault_domain == 0 &&
               status->fault_code == 0;
    }
    if (status->terminal_kind < FWLAB_DMA_V0_SUCCEEDED ||
        status->terminal_kind > FWLAB_DMA_V0_TERMINAL_QUARANTINED ||
        ((status->state == FWLAB_DMA_V0_STATE_QUARANTINED) !=
         (status->terminal_kind == FWLAB_DMA_V0_TERMINAL_QUARANTINED)) ||
        (status->effect == FWLAB_HOST_ACTION_V0_EFFECT_NONE &&
         status->bytes_completed != 0) ||
        ((status->effect == FWLAB_HOST_ACTION_V0_EFFECT_FULL ||
          status->effect == FWLAB_HOST_ACTION_V0_EFFECT_EXACT_PREFIX) &&
         status->bytes_completed == 0)) {
        return 0;
    }
    return status->terminal_kind != FWLAB_DMA_V0_SUCCEEDED ||
           (status->effect == FWLAB_HOST_ACTION_V0_EFFECT_FULL &&
            status->fault_domain == 0 && status->fault_code == 0);
}

int fwlab_dma_status_v0_matches_request(
    const struct fwlab_dma_status_v0 *status,
    const struct fwlab_dma_request_v0 *request)
{
    if (!fwlab_dma_status_v0_valid(status) ||
        !fwlab_dma_request_v0_valid(request) ||
        !spine_dma_token_equal(&status->operation, &request->operation) ||
        status->bytes_completed > request->exact_bytes) {
        return 0;
    }
    if (status->state == FWLAB_DMA_V0_STATE_ACCEPTED) {
        return 1;
    }
    switch (status->effect) {
    case FWLAB_HOST_ACTION_V0_EFFECT_NONE:
        return status->bytes_completed == 0;
    case FWLAB_HOST_ACTION_V0_EFFECT_FULL:
        return status->bytes_completed == request->exact_bytes;
    case FWLAB_HOST_ACTION_V0_EFFECT_EXACT_PREFIX:
        return status->bytes_completed < request->exact_bytes;
    case FWLAB_HOST_ACTION_V0_EFFECT_UNKNOWN_PREFIX:
        return 1;
    default:
        return 0;
    }
}

int fwlab_host_data_epoch_status_v0_valid(
    const struct fwlab_host_data_epoch_status_v0 *status)
{
    if (status == NULL || status->version != FWLAB_HOST_DATA_V0_VERSION ||
        status->size != sizeof(*status) || status->reserved0 != 0 ||
        status->lifecycle_instance_nonce == 0 || status->execution_epoch == 0 ||
        status->admission_closed > 1 || status->quiescent > 1 ||
        !spine_bytes_zero(status->reserved1, sizeof(status->reserved1)) ||
        !spine_bytes_zero(status->reserved2, sizeof(status->reserved2))) {
        return 0;
    }
    return !status->quiescent ||
           (status->admission_closed && status->authority_refs == 0 &&
            status->buffer_leases == 0 && status->dma_operations == 0);
}

int fwlab_host_data_ops_v0_valid(
    const struct fwlab_host_data_ops_v0 *ops)
{
    return ops != NULL && ops->version == FWLAB_HOST_DATA_V0_VERSION &&
           ops->size == sizeof(*ops) && ops->reserved0 == 0 &&
           ops->authority_mint != NULL && ops->authority_release != NULL &&
           ops->token_reserve != NULL && ops->submit != NULL &&
           ops->query != NULL && ops->cancel != NULL &&
           ops->retire_start != NULL && ops->retire_query != NULL &&
           ops->epoch_close != NULL && ops->epoch_quiescent != NULL &&
           spine_bytes_zero(ops->reserved1, sizeof(ops->reserved1));
}

int fwlab_host_data_port_v0_valid(
    const struct fwlab_host_data_port_v0 *port)
{
    return port != NULL && fwlab_host_data_ops_v0_valid(port->ops) &&
           port->context != NULL &&
           fwlab_controller_buffer_port_v0_valid(&port->buffer) &&
           port->authority_issuer_nonce != 0 &&
           port->dma_issuer_nonce != 0 && port->generation != 0 &&
           port->authority_issuer_nonce != port->dma_issuer_nonce &&
           port->authority_issuer_nonce != port->buffer.issuer_nonce &&
           port->dma_issuer_nonce != port->buffer.issuer_nonce &&
           spine_bytes_zero(port->reserved, sizeof(port->reserved));
}

int fwlab_host_data_reconcile_ops_v0_valid(
    const struct fwlab_host_data_reconcile_ops_v0 *ops)
{
    return ops != NULL && ops->version == FWLAB_HOST_DATA_V0_VERSION &&
           ops->size == sizeof(*ops) && ops->reserved0 == 0 &&
           ops->epoch_close != NULL && ops->dma_query != NULL &&
           ops->dma_cancel != NULL && ops->dma_retire_start != NULL &&
           ops->dma_retire_query != NULL && ops->buffer_read != NULL &&
           ops->buffer_write != NULL && ops->buffer_copy != NULL &&
           ops->buffer_release != NULL && ops->authority_release != NULL &&
           ops->epoch_quiescent != NULL &&
           spine_bytes_zero(ops->reserved1, sizeof(ops->reserved1));
}

int fwlab_host_data_reconcile_port_v0_valid(
    const struct fwlab_host_data_reconcile_port_v0 *port)
{
    return port != NULL &&
           fwlab_host_data_reconcile_ops_v0_valid(port->ops) &&
           port->context != NULL && port->authority_issuer_nonce != 0 &&
           port->buffer_issuer_nonce != 0 && port->dma_issuer_nonce != 0 &&
           port->generation != 0 &&
           port->authority_issuer_nonce != port->buffer_issuer_nonce &&
           port->authority_issuer_nonce != port->dma_issuer_nonce &&
           port->buffer_issuer_nonce != port->dma_issuer_nonce &&
           spine_bytes_zero(port->reserved, sizeof(port->reserved));
}

static int spine_block_namespace_valid(
    const struct fwlab_block_namespace_ref_v0 *reference)
{
    return reference != NULL &&
           (reference->word[0] != 0 || reference->word[1] != 0);
}

static int spine_block_frontier_zero(
    const struct fwlab_block_frontier_ref_v0 *reference)
{
    return reference != NULL && reference->word[0] == 0 &&
           reference->word[1] == 0;
}

static int spine_block_token_equal(
    const struct fwlab_block_op_token_v0 *left,
    const struct fwlab_block_op_token_v0 *right)
{
    return left != NULL && right != NULL &&
           left->version == right->version && left->size == right->size &&
           left->type_tag == right->type_tag &&
           spine_action_token_equal(&left->action, &right->action) &&
           left->provider_nonce == right->provider_nonce &&
           left->generation == right->generation;
}

static uint16_t spine_block_action_kind(uint32_t operation)
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

int fwlab_block_op_token_v0_valid(
    const struct fwlab_block_op_token_v0 *operation)
{
    return operation != NULL &&
           operation->version == FWLAB_BLOCK_SERVICE_V0_VERSION &&
           operation->size == sizeof(*operation) &&
           operation->type_tag == FWLAB_BLOCK_OP_TOKEN_V0_TAG &&
           fwlab_host_action_token_v0_valid(&operation->action) &&
           operation->action.kind >= FWLAB_HOST_ACTION_V0_BLOCK_READ &&
           operation->action.kind <= FWLAB_HOST_ACTION_V0_BLOCK_TRIM &&
           operation->provider_nonce != 0 && operation->generation != 0 &&
           spine_bytes_zero(operation->reserved,
                            sizeof(operation->reserved));
}

int fwlab_block_request_v0_valid(
    const struct fwlab_block_request_v0 *request)
{
    uint32_t required_right;

    if (request == NULL ||
        request->version != FWLAB_BLOCK_SERVICE_V0_VERSION ||
        request->size != sizeof(*request) || request->reserved0 != 0 ||
        !fwlab_block_op_token_v0_valid(&request->operation_token) ||
        !spine_block_namespace_valid(&request->namespace_ref) ||
        request->operation < FWLAB_BLOCK_V0_READ ||
        request->operation > FWLAB_BLOCK_V0_TRIM ||
        request->operation_token.action.kind !=
            spine_block_action_kind(request->operation) ||
        request->buffer_present > 1 ||
        !spine_bytes_zero(request->reserved1,
                          sizeof(request->reserved1)) ||
        !spine_bytes_zero(request->reserved2,
                          sizeof(request->reserved2))) {
        return 0;
    }

    if (request->operation == FWLAB_BLOCK_V0_READ ||
        request->operation == FWLAB_BLOCK_V0_WRITE) {
        if (!request->buffer_present || request->lba_count == 0 ||
            request->operation_token.provider_nonce ==
                request->buffer.issuer_nonce) {
            return 0;
        }
        if (request->operation == FWLAB_BLOCK_V0_READ) {
            if (request->durability != FWLAB_BLOCK_V0_DURABILITY_NONE) {
                return 0;
            }
            required_right = FWLAB_CONTROLLER_BUFFER_V0_WRITE;
        } else {
            if (request->durability !=
                    FWLAB_BLOCK_V0_DURABILITY_VOLATILE_ALLOWED &&
                request->durability != FWLAB_BLOCK_V0_DURABILITY_SELF) {
                return 0;
            }
            required_right = FWLAB_CONTROLLER_BUFFER_V0_READ;
        }
        return fwlab_controller_buffer_span_v0_valid_for_lease(
            &request->buffer_span, &request->buffer, required_right);
    }

    if (request->buffer_present ||
        !spine_bytes_zero(&request->buffer, sizeof(request->buffer)) ||
        !spine_bytes_zero(&request->buffer_span,
                          sizeof(request->buffer_span))) {
        return 0;
    }
    if (request->operation == FWLAB_BLOCK_V0_FLUSH) {
        return request->lba == 0 && request->lba_count == 0 &&
               request->durability == FWLAB_BLOCK_V0_DURABILITY_FRONTIER;
    }
    return request->lba_count != 0 &&
           request->durability == FWLAB_BLOCK_V0_DURABILITY_NONE;
}

int fwlab_block_submit_result_v0_valid(
    const struct fwlab_block_submit_result_v0 *result)
{
    if (result == NULL ||
        result->version != FWLAB_BLOCK_SERVICE_V0_VERSION ||
        result->size != sizeof(*result) || result->reserved0 != 0 ||
        !fwlab_block_op_token_v0_valid(&result->operation_token) ||
        result->disposition < FWLAB_HOST_ACTION_V0_ACCEPTED ||
        result->disposition > FWLAB_HOST_ACTION_V0_REJECTED ||
        !spine_bytes_zero(result->reserved1, sizeof(result->reserved1))) {
        return 0;
    }
    return result->disposition == FWLAB_HOST_ACTION_V0_REJECTED ||
           (result->fault_domain == 0 && result->fault_code == 0);
}

int fwlab_block_submit_result_v0_matches_request(
    const struct fwlab_block_submit_result_v0 *result,
    const struct fwlab_block_request_v0 *request)
{
    return fwlab_block_submit_result_v0_valid(result) &&
           fwlab_block_request_v0_valid(request) &&
           spine_block_token_equal(&result->operation_token,
                                   &request->operation_token);
}

int fwlab_block_status_v0_valid(
    const struct fwlab_block_status_v0 *status)
{
    int frontier_zero;

    if (status == NULL ||
        status->version != FWLAB_BLOCK_SERVICE_V0_VERSION ||
        status->size != sizeof(*status) || status->reserved0 != 0 ||
        !fwlab_block_op_token_v0_valid(&status->operation_token) ||
        status->state < FWLAB_BLOCK_V0_STATE_ACCEPTED ||
        status->state > FWLAB_BLOCK_V0_STATE_QUARANTINED ||
        status->effect > FWLAB_BLOCK_V0_EFFECT_UNKNOWN_PREFIX ||
        status->durability_witness >
            FWLAB_BLOCK_V0_WITNESS_FRONTIER_DURABLE ||
        !spine_bytes_zero(status->reserved1, sizeof(status->reserved1))) {
        return 0;
    }
    frontier_zero = spine_block_frontier_zero(&status->frontier);
    if (status->state == FWLAB_BLOCK_V0_STATE_ACCEPTED) {
        return status->outcome == 0 && status->effect == 0 &&
               status->completed_lbas == 0 && status->data_bytes == 0 &&
               status->durability_witness == 0 && frontier_zero &&
               status->fault_domain == 0 && status->fault_code == 0;
    }
    if (status->outcome < FWLAB_BLOCK_V0_SUCCEEDED ||
        status->outcome > FWLAB_BLOCK_V0_OUTCOME_QUARANTINED ||
        ((status->state == FWLAB_BLOCK_V0_STATE_QUARANTINED) !=
         (status->outcome == FWLAB_BLOCK_V0_OUTCOME_QUARANTINED)) ||
        ((status->durability_witness == FWLAB_BLOCK_V0_WITNESS_NONE ||
          status->durability_witness == FWLAB_BLOCK_V0_WITNESS_VOLATILE) !=
         frontier_zero) ||
        (status->effect == FWLAB_BLOCK_V0_EFFECT_NONE &&
         (status->completed_lbas != 0 || status->data_bytes != 0)) ||
        ((status->effect == FWLAB_BLOCK_V0_EFFECT_FULL ||
          status->effect == FWLAB_BLOCK_V0_EFFECT_EXACT_PREFIX) &&
         status->operation_token.action.kind !=
             FWLAB_HOST_ACTION_V0_BLOCK_FLUSH &&
         status->completed_lbas == 0)) {
        return 0;
    }
    return status->outcome != FWLAB_BLOCK_V0_SUCCEEDED ||
           (status->effect == FWLAB_BLOCK_V0_EFFECT_FULL &&
            status->fault_domain == 0 && status->fault_code == 0);
}

int fwlab_block_status_v0_matches_request(
    const struct fwlab_block_status_v0 *status,
    const struct fwlab_block_request_v0 *request)
{
    if (!fwlab_block_status_v0_valid(status) ||
        !fwlab_block_request_v0_valid(request) ||
        !spine_block_token_equal(&status->operation_token,
                                 &request->operation_token)) {
        return 0;
    }
    if (status->completed_lbas > request->lba_count ||
        ((request->operation == FWLAB_BLOCK_V0_READ ||
          request->operation == FWLAB_BLOCK_V0_WRITE) &&
         status->data_bytes > request->buffer_span.length) ||
        ((request->operation == FWLAB_BLOCK_V0_FLUSH ||
          request->operation == FWLAB_BLOCK_V0_TRIM) &&
         status->data_bytes != 0)) {
        return 0;
    }
    switch (request->operation) {
    case FWLAB_BLOCK_V0_READ:
    case FWLAB_BLOCK_V0_TRIM:
        if (status->durability_witness != FWLAB_BLOCK_V0_WITNESS_NONE ||
            !spine_block_frontier_zero(&status->frontier)) {
            return 0;
        }
        break;
    case FWLAB_BLOCK_V0_WRITE:
        if (request->durability ==
            FWLAB_BLOCK_V0_DURABILITY_VOLATILE_ALLOWED) {
            if ((status->durability_witness != FWLAB_BLOCK_V0_WITNESS_NONE &&
                 status->durability_witness !=
                     FWLAB_BLOCK_V0_WITNESS_VOLATILE) ||
                !spine_block_frontier_zero(&status->frontier)) {
                return 0;
            }
        } else if ((status->durability_witness !=
                        FWLAB_BLOCK_V0_WITNESS_NONE &&
                    status->durability_witness !=
                        FWLAB_BLOCK_V0_WITNESS_VOLATILE &&
                    status->durability_witness !=
                        FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE) ||
                   ((status->durability_witness ==
                         FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE) !=
                    !spine_block_frontier_zero(&status->frontier))) {
            return 0;
        }
        break;
    case FWLAB_BLOCK_V0_FLUSH:
        if ((status->durability_witness != FWLAB_BLOCK_V0_WITNESS_NONE &&
             status->durability_witness !=
                 FWLAB_BLOCK_V0_WITNESS_FRONTIER_DURABLE) ||
            ((status->durability_witness ==
                  FWLAB_BLOCK_V0_WITNESS_FRONTIER_DURABLE) !=
             !spine_block_frontier_zero(&status->frontier))) {
            return 0;
        }
        break;
    default:
        return 0;
    }
    if ((status->effect == FWLAB_BLOCK_V0_EFFECT_FULL &&
         (status->completed_lbas != request->lba_count ||
          ((request->operation == FWLAB_BLOCK_V0_READ ||
            request->operation == FWLAB_BLOCK_V0_WRITE) &&
           status->data_bytes != request->buffer_span.length))) ||
        (status->effect == FWLAB_BLOCK_V0_EFFECT_EXACT_PREFIX &&
         (status->completed_lbas >= request->lba_count ||
          ((request->operation == FWLAB_BLOCK_V0_READ ||
            request->operation == FWLAB_BLOCK_V0_WRITE) &&
           status->data_bytes >= request->buffer_span.length)))) {
        return 0;
    }
    if (status->outcome != FWLAB_BLOCK_V0_SUCCEEDED) {
        return 1;
    }
    switch (request->operation) {
    case FWLAB_BLOCK_V0_READ:
        return status->completed_lbas == request->lba_count &&
               status->data_bytes == request->buffer_span.length &&
               status->durability_witness == FWLAB_BLOCK_V0_WITNESS_NONE &&
               spine_block_frontier_zero(&status->frontier);
    case FWLAB_BLOCK_V0_WRITE:
        return status->completed_lbas == request->lba_count &&
               status->data_bytes == request->buffer_span.length &&
               ((request->durability ==
                     FWLAB_BLOCK_V0_DURABILITY_VOLATILE_ALLOWED &&
                 status->durability_witness ==
                     FWLAB_BLOCK_V0_WITNESS_VOLATILE &&
                 spine_block_frontier_zero(&status->frontier)) ||
                (request->durability == FWLAB_BLOCK_V0_DURABILITY_SELF &&
                 status->durability_witness ==
                     FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE &&
                 !spine_block_frontier_zero(&status->frontier)));
    case FWLAB_BLOCK_V0_FLUSH:
        return status->completed_lbas == 0 && status->data_bytes == 0 &&
               status->durability_witness ==
                   FWLAB_BLOCK_V0_WITNESS_FRONTIER_DURABLE &&
               !spine_block_frontier_zero(&status->frontier);
    case FWLAB_BLOCK_V0_TRIM:
        return status->completed_lbas == request->lba_count &&
               status->data_bytes == 0 &&
               status->durability_witness == FWLAB_BLOCK_V0_WITNESS_NONE &&
               spine_block_frontier_zero(&status->frontier);
    default:
        return 0;
    }
}

int fwlab_block_epoch_status_v0_valid(
    const struct fwlab_block_epoch_status_v0 *status)
{
    if (status == NULL ||
        status->version != FWLAB_BLOCK_SERVICE_V0_VERSION ||
        status->size != sizeof(*status) || status->reserved0 != 0 ||
        status->lifecycle_instance_nonce == 0 || status->execution_epoch == 0 ||
        status->admission_closed > 1 || status->quiescent > 1 ||
        !spine_bytes_zero(status->reserved1, sizeof(status->reserved1)) ||
        !spine_bytes_zero(status->reserved2, sizeof(status->reserved2))) {
        return 0;
    }
    if (status->quiescent) {
        return status->admission_closed &&
               status->aggregate_operations == 0 &&
               spine_bytes_nonzero(status->aggregate_proof,
                                   sizeof(status->aggregate_proof));
    }
    return spine_bytes_zero(status->aggregate_proof,
                            sizeof(status->aggregate_proof));
}

int fwlab_block_service_ops_v0_valid(
    const struct fwlab_block_service_ops_v0 *ops)
{
    return ops != NULL && ops->version == FWLAB_BLOCK_SERVICE_V0_VERSION &&
           ops->size == sizeof(*ops) && ops->reserved0 == 0 &&
           ops->submit != NULL && ops->query != NULL &&
           ops->cancel != NULL && ops->retire_start != NULL &&
           ops->retire_query != NULL && ops->epoch_close != NULL &&
           ops->epoch_quiescent != NULL &&
           spine_bytes_zero(ops->reserved1, sizeof(ops->reserved1));
}

int fwlab_block_service_v0_valid(
    const struct fwlab_block_service_v0 *service)
{
    return service != NULL &&
           fwlab_block_service_ops_v0_valid(service->ops) &&
           service->context != NULL && service->provider_nonce != 0 &&
           service->generation != 0 &&
           spine_bytes_zero(service->reserved, sizeof(service->reserved));
}

static int spine_owner_kind_live(uint8_t owner)
{
    return owner == FWLAB_OWNER_V0_HOST_NATIVE ||
           owner == FWLAB_OWNER_V0_VFIO;
}

static int spine_owner_transition_equal(
    const struct fwlab_owner_transition_token_v0 *left,
    const struct fwlab_owner_transition_token_v0 *right)
{
    return left != NULL && right != NULL &&
           left->version == right->version && left->size == right->size &&
           left->type_tag == right->type_tag &&
           left->function_instance_nonce == right->function_instance_nonce &&
           left->transition_uid == right->transition_uid &&
           left->old_owner_epoch == right->old_owner_epoch &&
           left->no_owner_epoch == right->no_owner_epoch &&
           left->old_controller_epoch == right->old_controller_epoch &&
           left->old_execution_epoch == right->old_execution_epoch &&
           left->generation == right->generation &&
           left->old_owner_kind == right->old_owner_kind;
}

int fwlab_owner_stable_identity_v0_valid(
    const struct fwlab_owner_stable_identity_v0 *identity)
{
    return identity != NULL &&
           identity->version == FWLAB_OWNER_CONTROL_V0_VERSION &&
           identity->size == sizeof(*identity) && identity->reserved0 == 0 &&
           identity->function_instance_nonce != 0 &&
           spine_bytes_nonzero(identity->media_uuid,
                               sizeof(identity->media_uuid)) &&
           identity->media_format_version != 0 && identity->reserved1 == 0 &&
           spine_bytes_nonzero(identity->binding_manifest_sha256,
                               sizeof(identity->binding_manifest_sha256)) &&
           spine_bytes_zero(identity->reserved2,
                            sizeof(identity->reserved2));
}

int fwlab_owner_epoch_state_v0_valid(
    const struct fwlab_owner_epoch_state_v0 *state)
{
    if (state == NULL || state->version != FWLAB_OWNER_CONTROL_V0_VERSION ||
        state->size != sizeof(*state) || state->reserved0 != 0 ||
        state->function_instance_nonce == 0 || state->owner_epoch == 0 ||
        state->phase < FWLAB_OWNER_V0_OWNED ||
        state->phase > FWLAB_OWNER_V0_QUARANTINED ||
        !spine_bytes_zero(state->reserved1, sizeof(state->reserved1)) ||
        !spine_bytes_nonzero(state->binding_manifest_sha256,
                             sizeof(state->binding_manifest_sha256)) ||
        !spine_bytes_zero(state->reserved2, sizeof(state->reserved2))) {
        return 0;
    }
    if (state->phase == FWLAB_OWNER_V0_OWNED) {
        return spine_owner_kind_live(state->owner_kind) &&
               state->controller_epoch != 0 && state->execution_epoch != 0;
    }
    if (state->owner_kind != FWLAB_OWNER_V0_NONE) {
        return 0;
    }
    if (state->phase == FWLAB_OWNER_V0_NO_OWNER) {
        return state->controller_epoch == 0 && state->execution_epoch == 0;
    }
    if (state->phase == FWLAB_OWNER_V0_REVOKING ||
        state->phase == FWLAB_OWNER_V0_DRAINING) {
        return state->controller_epoch != 0 && state->execution_epoch != 0;
    }
    return (state->controller_epoch == 0) == (state->execution_epoch == 0);
}

int fwlab_owner_revoke_key_v0_valid(
    const struct fwlab_owner_revoke_key_v0 *key)
{
    return key != NULL && key->version == FWLAB_OWNER_CONTROL_V0_VERSION &&
           key->size == sizeof(*key) && key->reserved0 == 0 &&
           fwlab_owner_epoch_state_v0_valid(&key->expected_owner) &&
           key->expected_owner.phase == FWLAB_OWNER_V0_OWNED &&
           key->client_uid != 0 &&
           (key->policy == FWLAB_OWNER_V0_DRAIN_ONLY ||
            key->policy == FWLAB_OWNER_V0_REQUIRE_DURABLE_FRONTIER) &&
           spine_bytes_zero(key->reserved1, sizeof(key->reserved1));
}

int fwlab_owner_transition_token_v0_valid(
    const struct fwlab_owner_transition_token_v0 *transition)
{
    return transition != NULL &&
           transition->version == FWLAB_OWNER_CONTROL_V0_VERSION &&
           transition->size == sizeof(*transition) &&
           transition->type_tag == FWLAB_OWNER_TRANSITION_V0_TAG &&
           transition->function_instance_nonce != 0 &&
           transition->transition_uid != 0 &&
           transition->old_owner_epoch != 0 &&
           transition->old_owner_epoch != UINT64_MAX &&
           transition->no_owner_epoch == transition->old_owner_epoch + 1 &&
           transition->old_controller_epoch != 0 &&
           transition->old_execution_epoch != 0 &&
           transition->generation != 0 &&
           spine_owner_kind_live(transition->old_owner_kind) &&
           spine_bytes_zero(transition->reserved0,
                            sizeof(transition->reserved0)) &&
           spine_bytes_zero(transition->reserved1,
                            sizeof(transition->reserved1));
}

int fwlab_owner_zero_certificate_v0_valid(
    const struct fwlab_owner_zero_certificate_v0 *certificate)
{
    return certificate != NULL &&
           certificate->version == FWLAB_OWNER_CONTROL_V0_VERSION &&
           certificate->size == sizeof(*certificate) &&
           certificate->type_tag == FWLAB_OWNER_ZERO_CERTIFICATE_V0_TAG &&
           fwlab_owner_transition_token_v0_valid(&certificate->transition) &&
           certificate->certificate_uid != 0 && certificate->generation != 0 &&
           certificate->host_dma_authorities == 0 &&
           certificate->mapping_refs == 0 && certificate->pin_refs == 0 &&
           certificate->dma_operations == 0 &&
           certificate->controller_buffer_leases == 0 &&
           certificate->lifecycle_commands == 0 &&
           certificate->aggregate_block_operations == 0 &&
           certificate->completion_leases == 0 &&
           certificate->cqe_workers == 0 && certificate->irq_workers == 0 &&
           certificate->pba_pending_vectors == 0 &&
           certificate->sq_capture_closed == 1 &&
           certificate->capability_mint_closed == 1 &&
           certificate->ftl_epoch_quiescent == 1 &&
           certificate->nfc_epoch_quiescent == 1 &&
           certificate->routes_cleared == 1 &&
           certificate->bar_volatile_cleared == 1 &&
           spine_bytes_zero(certificate->reserved0,
                            sizeof(certificate->reserved0)) &&
           spine_bytes_nonzero(certificate->ftl_epoch_proof,
                               sizeof(certificate->ftl_epoch_proof)) &&
           spine_bytes_nonzero(certificate->nfc_epoch_proof,
                               sizeof(certificate->nfc_epoch_proof)) &&
           spine_bytes_nonzero(certificate->binding_manifest_sha256,
                               sizeof(certificate->binding_manifest_sha256)) &&
           spine_bytes_zero(certificate->reserved1,
                            sizeof(certificate->reserved1));
}

int fwlab_owner_revoke_status_v0_valid(
    const struct fwlab_owner_revoke_status_v0 *status)
{
    if (status == NULL ||
        status->version != FWLAB_OWNER_CONTROL_V0_VERSION ||
        status->size != sizeof(*status) || status->reserved0 != 0 ||
        !fwlab_owner_transition_token_v0_valid(&status->transition) ||
        !fwlab_owner_epoch_state_v0_valid(&status->current) ||
        status->certificate_valid > 1 ||
        status->current.function_instance_nonce !=
            status->transition.function_instance_nonce ||
        status->current.owner_epoch != status->transition.no_owner_epoch ||
        status->current.owner_kind != FWLAB_OWNER_V0_NONE ||
        !spine_bytes_zero(status->reserved1, sizeof(status->reserved1)) ||
        !spine_bytes_zero(status->reserved2, sizeof(status->reserved2))) {
        return 0;
    }
    if (status->current.phase == FWLAB_OWNER_V0_REVOKING ||
        status->current.phase == FWLAB_OWNER_V0_DRAINING) {
        if (status->current.controller_epoch !=
                status->transition.old_controller_epoch ||
            status->current.execution_epoch !=
                status->transition.old_execution_epoch) {
            return 0;
        }
    } else if (status->current.phase != FWLAB_OWNER_V0_NO_OWNER &&
               status->current.phase != FWLAB_OWNER_V0_QUARANTINED) {
        return 0;
    }
    if (!status->certificate_valid) {
        return status->current.phase != FWLAB_OWNER_V0_NO_OWNER &&
               spine_bytes_zero(&status->certificate,
                                sizeof(status->certificate));
    }
    return status->current.phase == FWLAB_OWNER_V0_NO_OWNER &&
           status->fault_domain == 0 && status->fault_code == 0 &&
           fwlab_owner_zero_certificate_v0_valid(&status->certificate) &&
           spine_owner_transition_equal(&status->certificate.transition,
                                        &status->transition) &&
           memcmp(status->certificate.binding_manifest_sha256,
                  status->current.binding_manifest_sha256,
                  sizeof(status->current.binding_manifest_sha256)) == 0;
}

int fwlab_owner_grant_key_v0_valid(
    const struct fwlab_owner_grant_key_v0 *key)
{
    return key != NULL && key->version == FWLAB_OWNER_CONTROL_V0_VERSION &&
           key->size == sizeof(*key) && key->reserved0 == 0 &&
           fwlab_owner_transition_token_v0_valid(&key->transition) &&
           fwlab_owner_zero_certificate_v0_valid(&key->certificate) &&
           spine_owner_transition_equal(&key->transition,
                                        &key->certificate.transition) &&
           key->transition.old_controller_epoch != UINT32_MAX &&
           key->transition.old_execution_epoch != UINT32_MAX &&
           key->client_uid != 0 && spine_owner_kind_live(key->target_owner) &&
           spine_bytes_zero(key->reserved1, sizeof(key->reserved1)) &&
           spine_bytes_zero(key->reserved2, sizeof(key->reserved2));
}

int fwlab_owner_grant_key_v0_valid_for_port(
    const struct fwlab_owner_grant_key_v0 *key,
    const struct fwlab_owner_control_port_v0 *port)
{
    return fwlab_owner_grant_key_v0_valid(key) &&
           fwlab_owner_control_port_v0_valid(port) &&
           key->transition.function_instance_nonce ==
               port->stable.function_instance_nonce &&
           memcmp(key->certificate.binding_manifest_sha256,
                  port->stable.binding_manifest_sha256,
                  sizeof(port->stable.binding_manifest_sha256)) == 0;
}

int fwlab_owner_grant_status_v0_valid(
    const struct fwlab_owner_grant_status_v0 *status)
{
    return status != NULL &&
           status->version == FWLAB_OWNER_CONTROL_V0_VERSION &&
           status->size == sizeof(*status) && status->reserved0 == 0 &&
           fwlab_owner_epoch_state_v0_valid(&status->current) &&
           status->current.phase == FWLAB_OWNER_V0_OWNED &&
           status->fault_domain == 0 && status->fault_code == 0 &&
           spine_bytes_zero(status->reserved1, sizeof(status->reserved1));
}

int fwlab_owner_grant_status_v0_matches_key(
    const struct fwlab_owner_grant_status_v0 *status,
    const struct fwlab_owner_grant_key_v0 *key)
{
    return fwlab_owner_grant_status_v0_valid(status) &&
           fwlab_owner_grant_key_v0_valid(key) &&
           status->current.function_instance_nonce ==
               key->transition.function_instance_nonce &&
           status->current.owner_epoch == key->transition.no_owner_epoch &&
           status->current.owner_kind == key->target_owner &&
           status->current.controller_epoch ==
               key->transition.old_controller_epoch + 1 &&
           status->current.execution_epoch ==
               key->transition.old_execution_epoch + 1 &&
           memcmp(status->current.binding_manifest_sha256,
                  key->certificate.binding_manifest_sha256,
                  sizeof(status->current.binding_manifest_sha256)) == 0;
}

int fwlab_owner_grant_status_v0_matches_key_for_port(
    const struct fwlab_owner_grant_status_v0 *status,
    const struct fwlab_owner_grant_key_v0 *key,
    const struct fwlab_owner_control_port_v0 *port)
{
    return fwlab_owner_grant_status_v0_matches_key(status, key) &&
           fwlab_owner_grant_key_v0_valid_for_port(key, port) &&
           status->current.function_instance_nonce ==
               port->stable.function_instance_nonce &&
           memcmp(status->current.binding_manifest_sha256,
                  port->stable.binding_manifest_sha256,
                  sizeof(port->stable.binding_manifest_sha256)) == 0;
}

int fwlab_owner_step_result_v0_valid(
    const struct fwlab_owner_step_result_v0 *result)
{
    return result != NULL &&
           result->version == FWLAB_OWNER_CONTROL_V0_VERSION &&
           result->size == sizeof(*result) && result->reserved0 == 0 &&
           result->units_executed <= result->requested_budget &&
           result->transitions <= result->units_executed &&
           spine_bytes_zero(result->reserved1, sizeof(result->reserved1));
}

int fwlab_owner_control_ops_v0_valid(
    const struct fwlab_owner_control_ops_v0 *ops)
{
    return ops != NULL && ops->version == FWLAB_OWNER_CONTROL_V0_VERSION &&
           ops->size == sizeof(*ops) && ops->reserved0 == 0 &&
           ops->revoke_start != NULL && ops->revoke_query != NULL &&
           ops->drain_step != NULL && ops->grant_start != NULL &&
           ops->grant_query != NULL && ops->observe != NULL &&
           spine_bytes_zero(ops->reserved1, sizeof(ops->reserved1));
}

int fwlab_owner_control_port_v0_valid(
    const struct fwlab_owner_control_port_v0 *port)
{
    return port != NULL && fwlab_owner_control_ops_v0_valid(port->ops) &&
           port->context != NULL &&
           fwlab_owner_stable_identity_v0_valid(&port->stable) &&
           port->generation != 0 &&
           spine_bytes_zero(port->reserved, sizeof(port->reserved));
}
