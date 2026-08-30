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

static struct c35_scripted_entry *registration_find(
    struct c35_scripted_binding *binding,
    const struct c35_txid *txid
)
{
    unsigned int index;

    for (index = 0; index < C35_BINDING_SLOTS; ++index) {
        if (binding->entry[index].used && c35_txid_equal(
                &binding->entry[index].registration_txid, txid)) {
            return &binding->entry[index];
        }
    }
    return NULL;
}

static struct c35_scripted_entry *result_find(
    struct c35_scripted_binding *binding,
    const struct c35_txid *txid
)
{
    unsigned int index;

    for (index = 0; index < C35_BINDING_SLOTS; ++index) {
        if (binding->entry[index].used && c35_txid_equal(
                &binding->entry[index].result_txid, txid)) {
            return &binding->entry[index];
        }
    }
    return NULL;
}

static struct c35_scripted_entry *command_find(
    struct c35_scripted_binding *binding,
    const struct fwlab_c31_command_handle *command
)
{
    unsigned int index;

    for (index = 0; index < C35_BINDING_SLOTS; ++index) {
        if (binding->entry[index].used &&
            binding->entry[index].registration_state == C35_REG_COMMITTED &&
            command_equal(&binding->entry[index].command, command)) {
            return &binding->entry[index];
        }
    }
    return NULL;
}

static enum c35_result registration_prepare(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_request_token *token,
    uint32_t owner_epoch,
    const struct c35_request *request
)
{
    struct c35_scripted_binding *binding = context;
    struct c35_scripted_entry *entry;
    unsigned int index;

    if (binding == NULL || txid == NULL || token == NULL || request == NULL ||
        txid->instance_nonce != binding->instance_nonce ||
        txid->owner_epoch != binding->owner_epoch ||
        owner_epoch != binding->owner_epoch) {
        return C35_INVALID;
    }
    entry = registration_find(binding, txid);
    if (entry != NULL) {
        return entry->registration_state == C35_REG_PREPARED ?
            C35_OK : C35_WRONG_STATE;
    }
    if (binding->provider->scenario_count >= C31_FAKE_MAX_SCENARIOS) {
        return C35_COUNTER_EXHAUSTED;
    }
    for (index = 0; index < C35_BINDING_SLOTS; ++index) {
        if (!binding->entry[index].used) {
            entry = &binding->entry[index];
            break;
        }
    }
    if (entry == NULL) {
        return C35_NO_CAPACITY;
    }
    memset(entry, 0, sizeof(*entry));
    entry->used = 1;
    entry->registration_state = C35_REG_PREPARED;
    entry->registration_txid = *txid;
    entry->token = *token;
    entry->request = *request;
    entry->request_kind = request->kind;
    entry->atom_mask = request->kind == C35_READ ?
        (uint8_t)(1u << request->atom) : request->atom_mask;
    entry->atom = request->atom;
    return C35_OK;
}

static enum c35_result registration_commit(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command
)
{
    struct c35_scripted_binding *binding = context;
    struct c35_scripted_entry *entry = registration_find(binding, txid);
    struct c31_fake_scenario scenario;

    if (entry == NULL || command == NULL ||
        command->instance_nonce != binding->instance_nonce ||
        command->controller_epoch != binding->owner_epoch) {
        return C35_INVALID;
    }
    if (entry->registration_state == C35_REG_COMMITTED) {
        return command_equal(&entry->command, command) ? C35_OK : C35_STALE;
    }
    if (entry->registration_state != C35_REG_PREPARED) {
        return C35_WRONG_STATE;
    }
    memset(&scenario, 0, sizeof(scenario));
    scenario.request = entry->token;
    scenario.submit_disposition = FWLAB_C31_PROVIDER_ACCEPTED;
    scenario.terminal = FWLAB_C31_PROVIDER_SUCCESS;
    scenario.delay_polls = 1;
    if (!c31_fake_provider_add(binding->provider, &scenario)) {
        return binding->provider->scenario_count >= C31_FAKE_MAX_SCENARIOS ?
            C35_COUNTER_EXHAUSTED : C35_NO_CAPACITY;
    }
    entry->command = *command;
    entry->registration_state = C35_REG_COMMITTED;
    return C35_OK;
}

static enum c35_result registration_query(
    void *context,
    const struct c35_txid *txid,
    enum c35_registration_state *state
)
{
    struct c35_scripted_binding *binding = context;
    struct c35_scripted_entry *entry;

    if (binding == NULL || txid == NULL || state == NULL) {
        return C35_INVALID;
    }
    entry = registration_find(binding, txid);
    *state = entry != NULL ?
        (enum c35_registration_state)entry->registration_state :
        C35_REG_ABSENT;
    return C35_OK;
}

static enum c35_result registration_abort(
    void *context,
    const struct c35_txid *txid
)
{
    struct c35_scripted_binding *binding = context;
    struct c35_scripted_entry *entry = registration_find(binding, txid);

    if (entry == NULL) return C35_STALE;
    if (entry->registration_state == C35_REG_ABORTED) return C35_OK;
    if (entry->registration_state == C35_REG_COMMITTED)
        return C35_WRONG_STATE;
    entry->registration_state = C35_REG_ABORTED;
    return C35_OK;
}

static enum c35_result result_prepare(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command,
    const struct fwlab_c31_completion_intent *intent,
    struct c35_semantic_result *candidate
)
{
    struct c35_scripted_binding *binding = context;
    struct c35_scripted_entry *entry = command_find(binding, command);

    if (entry == NULL || txid == NULL || intent == NULL || candidate == NULL)
        return C35_INVALID;
    if (entry->result_state != C35_RESULT_ABSENT) {
        if (!c35_txid_equal(&entry->result_txid, txid)) return C35_STALE;
        *candidate = entry->semantic;
        return entry->result_state == C35_RESULT_PRESENT ||
               entry->result_state == C35_RESULT_ACKED ?
            C35_OK : C35_WRONG_STATE;
    }
    memset(&entry->semantic, 0, sizeof(entry->semantic));
    entry->semantic.request_kind = entry->request_kind;
    entry->semantic.atom_mask = entry->atom_mask;
    entry->semantic.status = intent->result == FWLAB_C31_COMPLETION_SUCCESS ?
        0 : 3;
    entry->result_txid = *txid;
    entry->result_state = C35_RESULT_PRESENT;
    *candidate = entry->semantic;
    return C35_OK;
}

static enum c35_result result_query(
    void *context,
    const struct c35_txid *txid,
    enum c35_result_state *state
)
{
    struct c35_scripted_binding *binding = context;
    struct c35_scripted_entry *entry;

    if (binding == NULL || txid == NULL || state == NULL) return C35_INVALID;
    entry = result_find(binding, txid);
    *state = entry != NULL ?
        (enum c35_result_state)entry->result_state : C35_RESULT_ABSENT;
    return C35_OK;
}

static enum c35_result result_abort(
    void *context,
    const struct c35_txid *txid
)
{
    struct c35_scripted_binding *binding = context;
    struct c35_scripted_entry *entry = result_find(binding, txid);

    if (entry == NULL) return C35_STALE;
    if (entry->result_state == C35_RESULT_ABORTED) return C35_OK;
    if (entry->result_state != C35_RESULT_PRESENT) return C35_WRONG_STATE;
    entry->result_state = C35_RESULT_ABORTED;
    return C35_OK;
}

static enum c35_result result_ack(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command
)
{
    struct c35_scripted_binding *binding = context;
    struct c35_scripted_entry *entry = result_find(binding, txid);

    if (entry == NULL || command == NULL ||
        !command_equal(&entry->command, command)) return C35_STALE;
    if (entry->result_state == C35_RESULT_ACKED ||
        entry->result_state == C35_RESULT_CLEARED_BY_RESET) return C35_OK;
    if (entry->result_state != C35_RESULT_PRESENT) return C35_WRONG_STATE;
    entry->result_state = C35_RESULT_ACKED;
    return C35_OK;
}

static enum c35_result reset_recover(
    void *context,
    const struct c35_txid *txid,
    uint32_t old_epoch,
    uint32_t new_epoch
)
{
    struct c35_scripted_binding *binding = context;
    unsigned int index;

    if (binding == NULL || txid == NULL || old_epoch != binding->owner_epoch ||
        new_epoch != old_epoch + 1u) return C35_INVALID;
    if (binding->reset_state == C35_RESET_RECOVERED &&
        c35_txid_equal(&binding->reset_txid, txid)) return C35_OK;
    if (binding->reset_state != C35_RESET_ABSENT) return C35_WRONG_STATE;
    for (index = 0; index < C35_BINDING_SLOTS; ++index) {
        if (binding->entry[index].used) {
            if (binding->entry[index].result_state == C35_RESULT_ABSENT) {
                memset(&binding->entry[index], 0,
                       sizeof(binding->entry[index]));
                continue;
            }
            binding->entry[index].registration_state = C35_REG_ABORTED;
            if (binding->entry[index].result_state != C35_RESULT_ACKED)
                binding->entry[index].result_state =
                    C35_RESULT_CLEARED_BY_RESET;
        }
    }
    binding->owner_epoch = new_epoch;
    binding->reset_txid = *txid;
    binding->reset_state = C35_RESET_RECOVERED;
    return C35_OK;
}

static enum c35_result reset_query(
    void *context,
    const struct c35_txid *txid,
    enum c35_reset_state *state
)
{
    struct c35_scripted_binding *binding = context;

    if (binding == NULL || txid == NULL || state == NULL) return C35_INVALID;
    *state = c35_txid_equal(&binding->reset_txid, txid) ?
        (enum c35_reset_state)binding->reset_state : C35_RESET_ABSENT;
    return C35_OK;
}

static enum c35_result transaction_retire(
    void *context,
    const struct c35_txid *txid
)
{
    struct c35_scripted_binding *binding = context;
    struct c35_scripted_entry *entry = result_find(binding, txid);

    if (binding->reset_state != C35_RESET_ABSENT &&
        c35_txid_equal(&binding->reset_txid, txid)) {
        memset(&binding->reset_txid, 0, sizeof(binding->reset_txid));
        binding->reset_state = C35_RESET_ABSENT;
        return C35_OK;
    }

    if (entry != NULL) {
        if (entry->result_state != C35_RESULT_ACKED &&
            entry->result_state != C35_RESULT_CLEARED_BY_RESET &&
            entry->result_state != C35_RESULT_ABORTED)
            return C35_WRONG_STATE;
        if (entry->result_state == C35_RESULT_ABORTED) {
            memset(&entry->result_txid, 0, sizeof(entry->result_txid));
            memset(&entry->semantic, 0, sizeof(entry->semantic));
            entry->result_state = C35_RESULT_ABSENT;
        } else {
            memset(entry, 0, sizeof(*entry));
        }
        return C35_OK;
    }
    entry = registration_find(binding, txid);
    if (entry != NULL && entry->registration_state == C35_REG_ABORTED) {
        memset(entry, 0, sizeof(*entry));
        return C35_OK;
    }
    return C35_STALE;
}

static enum c35_result teardown_finalize(void *context)
{
    struct c35_scripted_binding *binding = context;

    memset(binding->entry, 0, sizeof(binding->entry));
    return C35_OK;
}

static enum c35_result snapshot(
    void *context,
    struct c35_semantic_result *result
)
{
    (void)context;
    memset(result, 0, sizeof(*result));
    return C35_OK;
}

static enum c35_result quiescent(void *context, bool *is_quiescent)
{
    struct c35_scripted_binding *binding = context;

    if (binding == NULL || is_quiescent == NULL) return C35_INVALID;
    *is_quiescent = c31_fake_provider_active(binding->provider) == 0;
    return C35_OK;
}

static enum c35_result cause_query(
    void *context,
    struct c35_cause_detail *cause
)
{
    struct c35_scripted_binding *binding = context;

    if (binding == NULL || cause == NULL ||
        !c35_cause_valid(&binding->cause)) return C35_INVALID;
    *cause = binding->cause;
    return C35_OK;
}

static const struct c35_binding_ops scripted_ops = {
    .version = C35_BINDING_OPS_VERSION,
    .size = sizeof(struct c35_binding_ops),
    .reserved = 0,
    .registration_prepare = registration_prepare,
    .registration_commit = registration_commit,
    .registration_query = registration_query,
    .registration_abort = registration_abort,
    .result_prepare = result_prepare,
    .result_query = result_query,
    .result_abort = result_abort,
    .result_ack = result_ack,
    .reset_recover = reset_recover,
    .reset_query = reset_query,
    .transaction_retire = transaction_retire,
    .teardown_finalize = teardown_finalize,
    .semantic_snapshot = snapshot,
    .quiescent = quiescent,
    .cause_query = cause_query,
};

enum c35_result c35_scripted_binding_init(
    struct c35_scripted_binding *binding,
    struct c31_fake_provider_context *provider,
    uint64_t instance_nonce,
    uint32_t owner_epoch
)
{
    if (binding == NULL || provider == NULL || instance_nonce == 0 ||
        owner_epoch == 0) return C35_INVALID;
    memset(binding, 0, sizeof(*binding));
    binding->provider = provider;
    binding->instance_nonce = instance_nonce;
    binding->owner_epoch = owner_epoch;
    c35_cause_clear(&binding->cause);
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
