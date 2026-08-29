/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_scripted.h"

#include <string.h>

static int command_equal(
    const struct fwlab_c31_command_handle *left,
    const struct fwlab_c31_command_handle *right
)
{
    return left->instance_nonce == right->instance_nonce &&
           left->command_uid == right->command_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->slot == right->slot &&
           left->slot_generation == right->slot_generation;
}

static enum c35_result scripted_register(
    void *context,
    const struct fwlab_c31_request_token *token,
    const struct fwlab_c31_command_handle *command,
    uint32_t owner_epoch,
    const struct c35_request *request
)
{
    struct c35_scripted_binding *binding = context;
    struct c31_fake_scenario scenario;
    unsigned int index = C35_SCRIPTED_RESULTS;

    if (command->instance_nonce != binding->instance_nonce ||
        command->controller_epoch != binding->owner_epoch ||
        owner_epoch != binding->owner_epoch) {
        return C35_INVALID;
    }
    for (index = 0; index < C35_SCRIPTED_RESULTS; ++index) {
        if (!binding->result[index].used) {
            break;
        }
    }
    if (index == C35_SCRIPTED_RESULTS) {
        return C35_NO_CAPACITY;
    }
    memset(&scenario, 0, sizeof(scenario));
    scenario.request = *token;
    scenario.submit_disposition = FWLAB_C31_PROVIDER_ACCEPTED;
    scenario.terminal = FWLAB_C31_PROVIDER_SUCCESS;
    scenario.delay_polls = 1;
    if (!c31_fake_provider_add(binding->provider, &scenario)) {
        return C35_NO_CAPACITY;
    }
    binding->result[index].used = 1;
    binding->result[index].request_kind = request->kind;
    binding->result[index].atom_mask = request->kind == C35_READ ?
        (uint8_t)(1u << request->atom) : request->atom_mask;
    binding->result[index].atom = request->atom;
    binding->result[index].command = *command;
    return C35_OK;
}

static enum c35_result scripted_result(
    void *context,
    const struct fwlab_c31_command_handle *command,
    const struct fwlab_c31_completion_intent *intent,
    struct c35_semantic_result *result
)
{
    struct c35_scripted_binding *binding = context;
    unsigned int index;

    memset(result, 0, sizeof(*result));
    for (index = 0; index < C35_SCRIPTED_RESULTS; ++index) {
        if (binding->result[index].used &&
            command_equal(&binding->result[index].command, command)) {
            result->request_kind = binding->result[index].request_kind;
            result->atom_mask = binding->result[index].atom_mask;
            result->status = intent->result == FWLAB_C31_COMPLETION_SUCCESS ?
                0 : 3;
            return C35_OK;
        }
    }
    return C35_STALE;
}

static enum c35_result scripted_ack(
    void *context,
    const struct fwlab_c31_command_handle *command
)
{
    struct c35_scripted_binding *binding = context;
    unsigned int index;

    for (index = 0; index < C35_SCRIPTED_RESULTS; ++index) {
        if (binding->result[index].used &&
            command_equal(&binding->result[index].command, command)) {
            memset(&binding->result[index], 0,
                   sizeof(binding->result[index]));
            return C35_OK;
        }
    }
    return C35_STALE;
}

static enum c35_result scripted_reset(void *context)
{
    struct c35_scripted_binding *binding = context;

    if (binding->owner_epoch == UINT32_MAX) {
        return C35_LIMIT;
    }
    memset(binding->result, 0, sizeof(binding->result));
    ++binding->owner_epoch;
    return C35_OK;
}

static enum c35_result scripted_snapshot(
    void *context,
    struct c35_semantic_result *result
)
{
    (void)context;
    memset(result, 0, sizeof(*result));
    return C35_OK;
}

static enum c35_result scripted_quiescent(void *context, bool *quiescent)
{
    struct c35_scripted_binding *binding = context;

    *quiescent = c31_fake_provider_active(binding->provider) == 0;
    return C35_OK;
}

static const struct c35_binding_ops scripted_ops = {
    .version = C35_BINDING_VERSION,
    .size = sizeof(struct c35_binding_ops),
    .reserved = 0,
    .register_after_submit = scripted_register,
    .result_copy_before_consume = scripted_result,
    .result_ack_after_consume = scripted_ack,
    .post_reset_recover = scripted_reset,
    .semantic_snapshot = scripted_snapshot,
    .quiescent = scripted_quiescent,
};

enum c35_result c35_scripted_binding_init(
    struct c35_scripted_binding *binding,
    struct c31_fake_provider_context *provider,
    uint64_t instance_nonce,
    uint32_t owner_epoch
)
{
    if (binding == NULL || provider == NULL || instance_nonce == 0 ||
        owner_epoch == 0) {
        return C35_INVALID;
    }
    memset(binding, 0, sizeof(*binding));
    binding->provider = provider;
    binding->instance_nonce = instance_nonce;
    binding->owner_epoch = owner_epoch;
    return C35_OK;
}

struct c35_binding c35_scripted_binding_provider(
    struct c35_scripted_binding *binding
)
{
    struct c35_binding provider;

    provider.ops = binding != NULL ? &scripted_ops : NULL;
    provider.context = binding;
    return provider;
}
