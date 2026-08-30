/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_c34.h"

#include "c34_internal.h"

#include <string.h>

struct credit_cost {
    uint16_t inner;
    uint16_t physical;
    uint16_t cache;
    uint16_t record;
};

static int persistent_valid(const struct c35_persistent_credits *credits)
{
    return credits != NULL && credits->version == C35_CREDIT_VERSION &&
           credits->size == sizeof(*credits) && credits->reserved == 0 &&
           credits->reserved1[0] == 0 && credits->reserved1[1] == 0 &&
           credits->reserved1[2] == 0 && credits->known <= 1 &&
           credits->record_used <= C35_C34_RECORD_CREDIT_LIMIT;
}

static int request_cost(
    const struct c35_request *request,
    struct credit_cost *cost
)
{
    unsigned int atoms = 0;
    unsigned int index;

    memset(cost, 0, sizeof(*cost));
    if (request->kind == C35_READ) {
        cost->inner = 2;
        cost->cache = 1;
        return request->atom < C35_ATOMS;
    }
    if (request->kind == C35_FENCE) return 1;
    if (request->kind != C35_WRITE && request->kind != C35_TRIM) return 0;
    for (index = 0; index < C35_ATOMS; ++index) {
        if ((request->atom_mask & (uint8_t)(1u << index)) != 0) ++atoms;
    }
    if (atoms == 0) return 0;
    if (request->kind == C35_WRITE) {
        cost->inner = (uint16_t)(4u * atoms);
        cost->physical = (uint16_t)(2u * atoms);
        cost->cache = (uint16_t)(2u * atoms);
        cost->record = (uint16_t)(2u * atoms);
    } else {
        cost->inner = (uint16_t)(2u * atoms);
        cost->physical = (uint16_t)atoms;
        cost->cache = (uint16_t)atoms;
        cost->record = (uint16_t)atoms;
    }
    return 1;
}

static enum c35_result credits_reserve(
    struct c35_c34_binding *binding,
    struct c35_c34_registration *entry,
    const struct c35_request *request
)
{
    struct credit_cost cost;

    if (!request_cost(request, &cost)) return C35_INVALID;
    if (!binding->persistent->known &&
        (request->kind == C35_WRITE || request->kind == C35_TRIM)) {
        c35_cause_record(&binding->cause, C35_CAUSE_C35,
                         C35_MEDIA_CAPACITY, C35_RETRY_REPAIR_REQUIRED);
        return C35_MEDIA_CAPACITY;
    }
    if ((uint64_t)binding->persistent->record_used + cost.record >
        C35_C34_RECORD_CREDIT_LIMIT) {
        c35_cause_record(&binding->cause, C35_CAUSE_MEDIA,
                         C35_MEDIA_CAPACITY, C35_RETRY_REPAIR_REQUIRED);
        return C35_MEDIA_CAPACITY;
    }
    if ((uint64_t)binding->inner_used + cost.inner >
            C35_C34_INNER_CREDIT_LIMIT ||
        (uint64_t)binding->physical_used + cost.physical >
            C35_C34_PHYSICAL_CREDIT_LIMIT ||
        (uint64_t)binding->physical_sequence_used + cost.physical >
            C35_C34_PHYSICAL_CREDIT_LIMIT ||
        (uint64_t)binding->cache_used + cost.cache >
            C35_C34_CACHE_CREDIT_LIMIT ||
        (uint64_t)binding->nfc_uid_used + cost.inner > 32u ||
        (uint64_t)binding->nfc_generation_used + cost.inner > 32u ||
        (uint64_t)binding->nfc_submit_used + cost.inner > 32u ||
        (uint64_t)binding->nfc_trace_used + 9u * cost.inner > 288u ||
        (uint64_t)binding->nfc_tick_used + 6u * cost.inner > 192u)
    {
        c35_cause_record(&binding->cause, C35_CAUSE_C35,
                         C35_COUNTER_EXHAUSTED,
                         C35_RETRY_REPAIR_REQUIRED);
        return C35_COUNTER_EXHAUSTED;
    }
    binding->inner_used += cost.inner;
    binding->physical_used += cost.physical;
    binding->physical_sequence_used += cost.physical;
    binding->cache_used += cost.cache;
    binding->nfc_uid_used += cost.inner;
    binding->nfc_generation_used += cost.inner;
    binding->nfc_submit_used += cost.inner;
    binding->nfc_trace_used += 9u * cost.inner;
    binding->nfc_tick_used += 6u * cost.inner;
    binding->persistent->record_used += cost.record;
    entry->inner_credits = cost.inner;
    entry->physical_credits = cost.physical;
    entry->cache_credits = cost.cache;
    entry->record_credits = cost.record;
    entry->credits_reserved = 1;
    return C35_OK;
}

static enum c35_result credits_refund(
    struct c35_c34_binding *binding,
    struct c35_c34_registration *entry
)
{
    if (!entry->credits_reserved) return C35_OK;
    if (binding->inner_used < entry->inner_credits ||
        binding->physical_used < entry->physical_credits ||
        binding->physical_sequence_used < entry->physical_credits ||
        binding->cache_used < entry->cache_credits ||
        binding->nfc_uid_used < entry->inner_credits ||
        binding->nfc_generation_used < entry->inner_credits ||
        binding->nfc_submit_used < entry->inner_credits ||
        binding->nfc_trace_used < 9u * entry->inner_credits ||
        binding->nfc_tick_used < 6u * entry->inner_credits ||
        binding->persistent->record_used < entry->record_credits)
        return C35_INVARIANT;
    binding->inner_used -= entry->inner_credits;
    binding->physical_used -= entry->physical_credits;
    binding->physical_sequence_used -= entry->physical_credits;
    binding->cache_used -= entry->cache_credits;
    binding->nfc_uid_used -= entry->inner_credits;
    binding->nfc_generation_used -= entry->inner_credits;
    binding->nfc_submit_used -= entry->inner_credits;
    binding->nfc_trace_used -= 9u * entry->inner_credits;
    binding->nfc_tick_used -= 6u * entry->inner_credits;
    binding->persistent->record_used -= entry->record_credits;
    entry->credits_reserved = 0;
    return C35_OK;
}

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

static enum c35_result map_c34(
    struct c35_c34_binding *binding,
    enum c34_result result
)
{
    if (binding != NULL) {
        if (result == C34_OK) c35_cause_clear(&binding->cause);
        else c35_cause_record(
            &binding->cause, C35_CAUSE_C34, (uint32_t)result,
            result == C34_NO_CAPACITY ? C35_RETRY_SAME_TOKEN :
            result == C34_WRONG_STATE || result == C34_STALE_TOKEN ||
            result == C34_NOT_FOUND ? C35_RETRY_NONE :
                                      C35_RETRY_REPAIR_REQUIRED);
    }
    switch (result) {
    case C34_OK: return C35_OK;
    case C34_INVALID_CONTRACT: return C35_INVALID;
    case C34_UNSUPPORTED_VERSION: return C35_UNSUPPORTED_VERSION;
    case C34_NO_CAPACITY: return C35_NO_CAPACITY;
    case C34_WRONG_STATE: return C35_WRONG_STATE;
    case C34_STALE_TOKEN: return C35_STALE;
    case C34_NOT_FOUND: return C35_NOT_FOUND;
    case C34_MEDIA_FAILURE: return C35_PROVIDER_FAILURE;
    case C34_CORRUPT: return C35_CORRUPT;
    case C34_COUNTER_EXHAUSTED: return C35_COUNTER_EXHAUSTED;
    case C34_INVARIANT_FAILURE: return C35_INVARIANT;
    default: return C35_INVARIANT;
    }
}

static struct c35_c34_registration *registration_find(
    struct c35_c34_binding *binding,
    const struct c35_txid *txid
)
{
    unsigned int index;

    for (index = 0; index < C35_BINDING_SLOTS; ++index) {
        if (binding->registration[index].used &&
            c35_txid_equal(&binding->registration[index].txid, txid)) {
            return &binding->registration[index];
        }
    }
    return NULL;
}

static struct c35_c34_registration *registration_for_command(
    struct c35_c34_binding *binding,
    const struct fwlab_c31_command_handle *command,
    unsigned int *index_out
)
{
    unsigned int index;

    for (index = 0; index < C35_BINDING_SLOTS; ++index) {
        struct c35_c34_registration *entry = &binding->registration[index];

        if (entry->used && entry->state == C35_REG_COMMITTED &&
            command_equal(&entry->command, command)) {
            if (index_out != NULL) {
                *index_out = index;
            }
            return entry;
        }
    }
    return NULL;
}

static struct c35_c34_result_entry *result_find(
    struct c35_c34_binding *binding,
    const struct c35_txid *txid
)
{
    unsigned int index;

    for (index = 0; index < C35_BINDING_SLOTS; ++index) {
        if (binding->result[index].used &&
            c35_txid_equal(&binding->result[index].txid, txid)) {
            return &binding->result[index];
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
    struct c35_c34_binding *binding = context;
    struct c35_c34_registration *entry;
    unsigned int index;

    if (binding != NULL) c35_cause_clear(&binding->cause);

    if (binding == NULL || txid == NULL || token == NULL || request == NULL ||
        txid->instance_nonce != binding->instance_nonce ||
        txid->owner_epoch != binding->owner_epoch ||
        owner_epoch != binding->owner_epoch) {
        return C35_INVALID;
    }
    entry = registration_find(binding, txid);
    if (entry != NULL) {
        return entry->state == C35_REG_PREPARED ? C35_OK : C35_WRONG_STATE;
    }
    for (index = 0; index < C35_BINDING_SLOTS; ++index) {
        if (!binding->registration[index].used) {
            entry = &binding->registration[index];
            break;
        }
    }
    if (entry == NULL) {
        return C35_NO_CAPACITY;
    }
    memset(entry, 0, sizeof(*entry));
    entry->used = 1;
    entry->state = C35_REG_PREPARED;
    entry->txid = *txid;
    entry->token = *token;
    entry->request.version = C34_CONTRACT_VERSION;
    entry->request.size = sizeof(entry->request);
    entry->request.kind = request->kind;
    entry->request.durability_kind = request->durability_kind;
    entry->request.atom_mask = request->atom_mask;
    entry->request.atom = request->atom;
    entry->request.owner_epoch = owner_epoch;
    entry->request.scope = request->scope;
    entry->request.sequence = request->sequence;
    entry->request.frontier = request->frontier;
    memcpy(entry->request.payload, request->payload,
           sizeof(entry->request.payload));
    {
        enum c35_result credit_result = credits_reserve(
            binding, entry, request);

        if (credit_result != C35_OK) {
            memset(entry, 0, sizeof(*entry));
            return credit_result;
        }
    }
    return C35_OK;
}

static enum c35_result registration_commit(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command
)
{
    struct c35_c34_binding *binding = context;
    struct c35_c34_registration *entry = registration_find(binding, txid);
    enum c34_result result;

    if (binding != NULL) c35_cause_clear(&binding->cause);

    if (entry == NULL || command == NULL ||
        command->instance_nonce != binding->instance_nonce ||
        command->controller_epoch != binding->owner_epoch) {
        return C35_INVALID;
    }
    if (entry->state == C35_REG_COMMITTED) {
        return command_equal(&entry->command, command) ?
            C35_OK : C35_STALE;
    }
    if (entry->state != C35_REG_PREPARED) {
        return C35_WRONG_STATE;
    }
    result = c34_request_register(
        binding->firmware, &entry->token, &entry->request);
    if (result != C34_OK) {
        return map_c34(binding, result);
    }
    entry->command = *command;
    entry->state = C35_REG_COMMITTED;
    return C35_OK;
}

static enum c35_result registration_query(
    void *context,
    const struct c35_txid *txid,
    enum c35_registration_state *state
)
{
    struct c35_c34_binding *binding = context;
    struct c35_c34_registration *entry;

    if (binding != NULL) c35_cause_clear(&binding->cause);

    if (binding == NULL || txid == NULL || state == NULL) {
        return C35_INVALID;
    }
    entry = registration_find(binding, txid);
    *state = entry != NULL ?
        (enum c35_registration_state)entry->state : C35_REG_ABSENT;
    return C35_OK;
}

static enum c35_result registration_abort(
    void *context,
    const struct c35_txid *txid
)
{
    struct c35_c34_binding *binding = context;
    struct c35_c34_registration *entry = registration_find(binding, txid);

    if (binding != NULL) c35_cause_clear(&binding->cause);

    if (entry == NULL) {
        return C35_STALE;
    }
    if (entry->state == C35_REG_ABORTED) {
        return C35_OK;
    }
    if (entry->state == C35_REG_COMMITTED) {
        return C35_WRONG_STATE;
    }
    if (entry->state != C35_REG_PREPARED &&
        entry->state != C35_REG_PARTIAL) {
        return C35_WRONG_STATE;
    }
    if (credits_refund(binding, entry) != C35_OK) {
        entry->state = C35_REG_POISONED;
        return C35_INVARIANT;
    }
    entry->state = C35_REG_ABORTED;
    return C35_OK;
}

static enum c35_result semantic_copy(
    struct c35_c34_binding *binding,
    const struct fwlab_c31_command_handle *command,
    const struct fwlab_c31_completion_intent *intent,
    struct c35_semantic_result *semantic
)
{
    struct c34_command_result inner;
    struct c34_logical_entry logical;
    enum c34_result lower;
    unsigned int atom;

    memset(semantic, 0, sizeof(*semantic));
    lower = c34_result_read(binding->firmware, command, &inner);
    if (lower != C34_OK) {
        semantic->status = intent->result == FWLAB_C31_COMPLETION_SUCCESS ?
            0 : 3;
        return map_c34(binding, lower);
    }
    semantic->status = inner.status;
    semantic->request_kind = inner.request_kind;
    semantic->atom_mask = inner.atom_mask;
    semantic->present_mask = inner.present_mask;
    semantic->witness_class = inner.witness.witness_class;
    semantic->witness_reason = (uint8_t)inner.witness.reason;
    memcpy(semantic->payload, inner.payload, sizeof(semantic->payload));
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        lower = c34_logical_state(
            binding->firmware, (uint8_t)atom, &logical);
        if (lower != C34_OK) return map_c34(binding, lower);
        semantic->logical_kind[atom] = logical.kind;
        semantic->logical_version[atom] = logical.version;
        semantic->logical_copy[atom] = logical.copy_sequence;
        semantic->value_crc[atom] = logical.value_crc32c;
    }
    c35_cause_clear(&binding->cause);
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
    struct c35_c34_binding *binding = context;
    struct c35_c34_result_entry *entry;
    struct c35_semantic_result local;
    unsigned int registration_index;
    unsigned int index;
    enum c35_result result;

    if (binding != NULL) c35_cause_clear(&binding->cause);

    if (binding == NULL || txid == NULL || command == NULL || intent == NULL ||
        candidate == NULL || registration_for_command(
            binding, command, &registration_index) == NULL) {
        return C35_INVALID;
    }
    entry = result_find(binding, txid);
    if (entry != NULL) {
        if (entry->state != C35_RESULT_PRESENT &&
            entry->state != C35_RESULT_ACKED) {
            return C35_WRONG_STATE;
        }
        *candidate = entry->semantic;
        return C35_OK;
    }
    for (index = 0; index < C35_BINDING_SLOTS; ++index) {
        if (!binding->result[index].used) {
            entry = &binding->result[index];
            break;
        }
    }
    if (entry == NULL) {
        return C35_NO_CAPACITY;
    }
    result = semantic_copy(binding, command, intent, &local);
    if (result != C35_OK) {
        return result;
    }
    memset(entry, 0, sizeof(*entry));
    entry->used = 1;
    entry->state = C35_RESULT_PRESENT;
    entry->registration_index = (uint16_t)registration_index;
    entry->txid = *txid;
    entry->command = *command;
    entry->semantic = local;
    *candidate = local;
    return C35_OK;
}

static enum c35_result result_query(
    void *context,
    const struct c35_txid *txid,
    enum c35_result_state *state
)
{
    struct c35_c34_binding *binding = context;
    struct c35_c34_result_entry *entry;

    if (binding != NULL) c35_cause_clear(&binding->cause);

    if (binding == NULL || txid == NULL || state == NULL) {
        return C35_INVALID;
    }
    entry = result_find(binding, txid);
    *state = entry != NULL ?
        (enum c35_result_state)entry->state : C35_RESULT_ABSENT;
    return C35_OK;
}

static enum c35_result result_abort(
    void *context,
    const struct c35_txid *txid
)
{
    struct c35_c34_binding *binding = context;
    struct c35_c34_result_entry *entry = result_find(binding, txid);

    if (binding != NULL) c35_cause_clear(&binding->cause);

    if (entry == NULL) {
        return C35_STALE;
    }
    if (entry->state == C35_RESULT_ABORTED) {
        return C35_OK;
    }
    if (entry->state != C35_RESULT_PRESENT &&
        entry->state != C35_RESULT_PREPARED) {
        return C35_WRONG_STATE;
    }
    entry->state = C35_RESULT_ABORTED;
    return C35_OK;
}

static enum c35_result result_ack(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command
)
{
    struct c35_c34_binding *binding = context;
    struct c35_c34_result_entry *entry = result_find(binding, txid);
    enum c34_result result;

    if (binding != NULL) c35_cause_clear(&binding->cause);

    if (entry == NULL || command == NULL ||
        !command_equal(&entry->command, command)) {
        return C35_STALE;
    }
    if (entry->state == C35_RESULT_ACKED ||
        entry->state == C35_RESULT_CLEARED_BY_RESET) {
        return C35_OK;
    }
    if (entry->state != C35_RESULT_PRESENT) {
        return C35_WRONG_STATE;
    }
    result = c34_result_ack(binding->firmware, command);
    if (result != C34_OK) {
        return map_c34(binding, result);
    }
    entry->state = C35_RESULT_ACKED;
    return C35_OK;
}

static enum c35_result reset_recover(
    void *context,
    const struct c35_txid *txid,
    uint32_t old_epoch,
    uint32_t new_epoch
)
{
    struct c35_c34_binding *binding = context;
    enum c34_result recover_result;
    unsigned int index;

    if (binding != NULL) c35_cause_clear(&binding->cause);

    if (binding == NULL || txid == NULL || old_epoch != binding->owner_epoch ||
        new_epoch != old_epoch + 1u) {
        return C35_INVALID;
    }
    if (binding->reset_state == C35_RESET_RECOVERED &&
        c35_txid_equal(&binding->reset_txid, txid)) {
        return C35_OK;
    }
    if (binding->reset_state != C35_RESET_ABSENT) {
        return C35_WRONG_STATE;
    }
    recover_result = binding->firmware->phase == 2 ?
        C34_WRONG_STATE : c34_recover(binding->firmware);
    if (recover_result != C34_OK) {
        (void)map_c34(binding, recover_result);
        binding->reset_txid = *txid;
        binding->reset_state = C35_RESET_POISONED;
        return C35_PROVIDER_FAILURE;
    }
    for (index = 0; index < C35_BINDING_SLOTS; ++index) {
        int result_retains_registration = 0;
        unsigned int result_index;

        if (binding->result[index].used &&
            binding->result[index].state != C35_RESULT_ACKED) {
            binding->result[index].state = C35_RESULT_CLEARED_BY_RESET;
        }
        for (result_index = 0; result_index < C35_BINDING_SLOTS;
             ++result_index) {
            if (binding->result[result_index].used &&
                binding->result[result_index].registration_index == index)
                result_retains_registration = 1;
        }
        if (binding->registration[index].used) {
            if ((binding->registration[index].state == C35_REG_PREPARED ||
                 binding->registration[index].state == C35_REG_PARTIAL) &&
                credits_refund(binding, &binding->registration[index]) !=
                    C35_OK) {
                binding->reset_txid = *txid;
                binding->reset_state = C35_RESET_POISONED;
                return C35_INVARIANT;
            }
            if (result_retains_registration)
                binding->registration[index].state = C35_REG_ABORTED;
            else
                memset(&binding->registration[index], 0,
                       sizeof(binding->registration[index]));
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
    struct c35_c34_binding *binding = context;

    if (binding != NULL) c35_cause_clear(&binding->cause);

    if (binding == NULL || txid == NULL || state == NULL) {
        return C35_INVALID;
    }
    *state = c35_txid_equal(&binding->reset_txid, txid) ?
        (enum c35_reset_state)binding->reset_state : C35_RESET_ABSENT;
    return C35_OK;
}

static enum c35_result transaction_retire(
    void *context,
    const struct c35_txid *txid
)
{
    struct c35_c34_binding *binding = context;
    struct c35_c34_result_entry *result = result_find(binding, txid);
    struct c35_c34_registration *registration = registration_find(binding, txid);

    if (binding != NULL) c35_cause_clear(&binding->cause);

    if (binding->reset_state != C35_RESET_ABSENT &&
        c35_txid_equal(&binding->reset_txid, txid)) {
        memset(&binding->reset_txid, 0, sizeof(binding->reset_txid));
        binding->reset_state = C35_RESET_ABSENT;
        return C35_OK;
    }

    if (result != NULL) {
        uint16_t registration_index = result->registration_index;
        uint8_t result_state = result->state;

        if (result->state != C35_RESULT_ACKED &&
            result->state != C35_RESULT_CLEARED_BY_RESET &&
            result->state != C35_RESULT_ABORTED) {
            return C35_WRONG_STATE;
        }
        memset(result, 0, sizeof(*result));
        if (result_state != C35_RESULT_ABORTED &&
            registration_index < C35_BINDING_SLOTS) {
            memset(&binding->registration[registration_index], 0,
                   sizeof(binding->registration[registration_index]));
        }
        return C35_OK;
    }
    if (registration != NULL && registration->state == C35_REG_ABORTED) {
        memset(registration, 0, sizeof(*registration));
        return C35_OK;
    }
    return C35_STALE;
}

static enum c35_result teardown_finalize(void *context)
{
    struct c35_c34_binding *binding = context;

    if (binding == NULL || binding->firmware == NULL) return C35_INVALID;
    c35_cause_clear(&binding->cause);
    if (binding->firmware->phase == 2 ||
        binding->reset_state == C35_RESET_POISONED) {
        (void)map_c34(binding, C34_WRONG_STATE);
        return C35_POISONED;
    }
    {
        unsigned int index;

        for (index = 0; index < C35_BINDING_SLOTS; ++index) {
            if (binding->registration[index].used &&
                (binding->registration[index].state == C35_REG_PREPARED ||
                 binding->registration[index].state == C35_REG_PARTIAL) &&
                credits_refund(binding, &binding->registration[index]) !=
                    C35_OK) return C35_POISONED;
        }
    }
    memset(binding->registration, 0, sizeof(binding->registration));
    memset(binding->result, 0, sizeof(binding->result));
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

    if (binding == NULL || binding->firmware == NULL || result == NULL)
        return C35_INVALID;
    c35_cause_clear(&binding->cause);

    memset(result, 0, sizeof(*result));
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        enum c34_result lower = c34_logical_state(
            binding->firmware, (uint8_t)atom, &logical);

        if (lower != C34_OK) return map_c34(binding, lower);
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

    if (binding == NULL || binding->firmware == NULL || is_quiescent == NULL)
        return C35_INVALID;
    c35_cause_clear(&binding->cause);
    if (binding->firmware->phase == 2 ||
        binding->reset_state == C35_RESET_POISONED) {
        (void)map_c34(binding, C34_WRONG_STATE);
        return C35_POISONED;
    }
    return map_c34(binding, c34_maintenance_quiescent(
        binding->firmware, is_quiescent));
}

static enum c35_result cause_query(
    void *context,
    struct c35_cause_detail *cause
)
{
    struct c35_c34_binding *binding = context;

    if (binding == NULL || cause == NULL ||
        !c35_cause_valid(&binding->cause)) return C35_INVALID;
    *cause = binding->cause;
    return C35_OK;
}

static const struct c35_binding_ops binding_ops = {
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

enum c35_result c35_c34_binding_init(
    struct c35_c34_binding *binding,
    struct c34 *firmware,
    struct c35_persistent_credits *persistent,
    uint64_t instance_nonce,
    uint32_t owner_epoch
)
{
    if (binding == NULL || firmware == NULL ||
        !persistent_valid(persistent) || instance_nonce == 0 ||
        owner_epoch == 0) {
        return C35_INVALID;
    }
    memset(binding, 0, sizeof(*binding));
    binding->firmware = firmware;
    binding->persistent = persistent;
    binding->instance_nonce = instance_nonce;
    binding->owner_epoch = owner_epoch;
    c35_cause_clear(&binding->cause);
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
