/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_c34.h"

#include <string.h>

static enum c35_result register_request(
    void *context,
    const struct fwlab_c31_request_token *token,
    const struct fwlab_c31_command_handle *command,
    uint32_t owner_epoch,
    const struct c35_request *request
)
{
    struct c35_c34_binding *binding = context;
    struct c34_request inner;
    enum c34_result result;

    if (binding == NULL || token == NULL || command == NULL ||
        request == NULL || command->instance_nonce != binding->instance_nonce ||
        command->controller_epoch != binding->owner_epoch ||
        owner_epoch != binding->owner_epoch) {
        return C35_INVALID;
    }
    memset(&inner, 0, sizeof(inner));
    inner.version = C34_CONTRACT_VERSION;
    inner.size = sizeof(inner);
    inner.kind = request->kind;
    inner.durability_kind = request->durability_kind;
    inner.atom_mask = request->atom_mask;
    inner.atom = request->atom;
    inner.owner_epoch = owner_epoch;
    inner.scope = request->scope;
    inner.sequence = request->sequence;
    inner.frontier = request->frontier;
    memcpy(inner.payload, request->payload, sizeof(inner.payload));
    result = c34_request_register(binding->firmware, token, &inner);
    return result == C34_OK ? C35_OK :
           result == C34_NO_CAPACITY ? C35_NO_CAPACITY :
           result == C34_WRONG_STATE ? C35_WRONG_STATE : C35_PROVIDER_FAILURE;
}

static enum c35_result result_copy(
    void *context,
    const struct fwlab_c31_command_handle *command,
    const struct fwlab_c31_completion_intent *intent,
    struct c35_semantic_result *result
)
{
    struct c35_c34_binding *binding = context;
    struct c34_command_result inner;
    struct c34_logical_entry logical;
    unsigned int atom;

    memset(result, 0, sizeof(*result));
    if (c34_result_read(binding->firmware, command, &inner) != C34_OK) {
        result->status = intent->result == FWLAB_C31_COMPLETION_SUCCESS ? 0 : 3;
        return C35_STALE;
    }
    result->status = inner.status;
    result->request_kind = inner.request_kind;
    result->atom_mask = inner.atom_mask;
    result->present_mask = inner.present_mask;
    result->witness_class = inner.witness.witness_class;
    result->witness_reason = (uint8_t)inner.witness.reason;
    memcpy(result->payload, inner.payload, sizeof(result->payload));
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        if (c34_logical_state(
                binding->firmware, (uint8_t)atom, &logical) != C34_OK) {
            return C35_INVARIANT;
        }
        result->logical_kind[atom] = logical.kind;
        result->logical_version[atom] = logical.version;
        result->logical_copy[atom] = logical.copy_sequence;
        result->value_crc[atom] = logical.value_crc32c;
    }
    return C35_OK;
}

static enum c35_result result_ack(
    void *context,
    const struct fwlab_c31_command_handle *command
)
{
    struct c35_c34_binding *binding = context;

    return c34_result_ack(binding->firmware, command) == C34_OK ?
        C35_OK : C35_STALE;
}

static enum c35_result post_reset(void *context)
{
    struct c35_c34_binding *binding = context;

    if (c34_recover(binding->firmware) != C34_OK ||
        binding->owner_epoch == UINT32_MAX) {
        return C35_PROVIDER_FAILURE;
    }
    ++binding->owner_epoch;
    return C35_OK;
}

static enum c35_result snapshot(
    void *context,
    struct c35_semantic_result *result
)
{
    struct c35_c34_binding *binding = context;
    struct c34_logical_entry logical;
    unsigned int atom;

    memset(result, 0, sizeof(*result));
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        if (c34_logical_state(
                binding->firmware, (uint8_t)atom, &logical) != C34_OK) {
            return C35_INVARIANT;
        }
        result->logical_kind[atom] = logical.kind;
        result->logical_version[atom] = logical.version;
        result->logical_copy[atom] = logical.copy_sequence;
        result->value_crc[atom] = logical.value_crc32c;
    }
    return C35_OK;
}

static enum c35_result quiescent(void *context, bool *is_quiescent)
{
    struct c35_c34_binding *binding = context;

    return c34_maintenance_quiescent(
               binding->firmware, is_quiescent) == C34_OK ?
        C35_OK : C35_INVARIANT;
}

static const struct c35_binding_ops binding_ops = {
    .version = C35_BINDING_VERSION,
    .size = sizeof(struct c35_binding_ops),
    .reserved = 0,
    .register_after_submit = register_request,
    .result_copy_before_consume = result_copy,
    .result_ack_after_consume = result_ack,
    .post_reset_recover = post_reset,
    .semantic_snapshot = snapshot,
    .quiescent = quiescent,
};

enum c35_result c35_c34_binding_init(
    struct c35_c34_binding *binding,
    struct c34 *firmware,
    uint64_t instance_nonce,
    uint32_t owner_epoch
)
{
    if (binding == NULL || firmware == NULL || instance_nonce == 0 ||
        owner_epoch == 0) {
        return C35_INVALID;
    }
    binding->firmware = firmware;
    binding->instance_nonce = instance_nonce;
    binding->owner_epoch = owner_epoch;
    return C35_OK;
}

struct c35_binding c35_c34_binding_provider(
    struct c35_c34_binding *binding
)
{
    struct c35_binding provider;

    provider.ops = binding != NULL ? &binding_ops : NULL;
    provider.context = binding;
    return provider;
}
