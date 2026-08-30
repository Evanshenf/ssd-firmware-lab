/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_fault_binding.h"

#include <string.h>

static int cut_now(struct c35_fault_binding *fault, enum c35_binding_cut call)
{
    uint32_t count = ++fault->calls[call];

    if (fault->injected || fault->target != (uint8_t)call ||
        count != fault->occurrence) return 0;
    fault->injected = 1;
    return 1;
}

static enum c35_result registration_prepare(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_request_token *token,
    uint32_t owner_epoch,
    const struct c35_request *request
)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_REGISTRATION_PREPARE);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->registration_prepare(
        fault->inner.context, txid, token, owner_epoch, request);
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result registration_commit(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command
)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_REGISTRATION_COMMIT);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->registration_commit(
        fault->inner.context, txid, command);
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result registration_query(
    void *context,
    const struct c35_txid *txid,
    enum c35_registration_state *state
)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_REGISTRATION_QUERY);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->registration_query(
        fault->inner.context, txid, state);
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result registration_abort(
    void *context,
    const struct c35_txid *txid
)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_REGISTRATION_ABORT);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->registration_abort(fault->inner.context, txid);
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result result_prepare(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command,
    const struct fwlab_c31_completion_intent *intent,
    struct c35_semantic_result *candidate
)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_RESULT_PREPARE);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->result_prepare(
        fault->inner.context, txid, command, intent, candidate);
    if (inject && result == C35_OK && fault->corrupt_candidate)
        candidate->reserved0[0] = 1;
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result result_query(
    void *context,
    const struct c35_txid *txid,
    enum c35_result_state *state
)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_RESULT_QUERY);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->result_query(fault->inner.context, txid, state);
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result result_abort(
    void *context,
    const struct c35_txid *txid
)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_RESULT_ABORT);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->result_abort(fault->inner.context, txid);
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result result_ack(
    void *context,
    const struct c35_txid *txid,
    const struct fwlab_c31_command_handle *command
)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_RESULT_ACK);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->result_ack(
        fault->inner.context, txid, command);
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result reset_recover(
    void *context,
    const struct c35_txid *txid,
    uint32_t old_epoch,
    uint32_t new_epoch
)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_RESET_RECOVER);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->reset_recover(
        fault->inner.context, txid, old_epoch, new_epoch);
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result reset_query(
    void *context,
    const struct c35_txid *txid,
    enum c35_reset_state *state
)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_RESET_QUERY);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->reset_query(fault->inner.context, txid, state);
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result transaction_retire(
    void *context,
    const struct c35_txid *txid
)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_TRANSACTION_RETIRE);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->transaction_retire(fault->inner.context, txid);
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result teardown_finalize(void *context)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_TEARDOWN_FINALIZE);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->teardown_finalize(fault->inner.context);
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result semantic_snapshot(
    void *context,
    struct c35_semantic_result *snapshot
)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_SEMANTIC_SNAPSHOT);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->semantic_snapshot(
        fault->inner.context, snapshot);
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result quiescent(void *context, bool *is_quiescent)
{
    struct c35_fault_binding *fault = context;
    int inject = cut_now(fault, C35_BINDING_CUT_QUIESCENT);
    enum c35_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->quiescent(fault->inner.context, is_quiescent);
    return inject && result == C35_OK ? fault->injected_result : result;
}

static enum c35_result cause_query(
    void *context,
    struct c35_cause_detail *cause
)
{
    struct c35_fault_binding *fault = context;

    return fault->inner.ops->cause_query(fault->inner.context, cause);
}

static const struct c35_binding_ops fault_ops = {
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
    .semantic_snapshot = semantic_snapshot,
    .quiescent = quiescent,
    .cause_query = cause_query,
};

int c35_fault_binding_init(
    struct c35_fault_binding *fault,
    const struct c35_binding *inner,
    enum c35_binding_cut target,
    enum c35_fault_effect effect,
    uint32_t occurrence,
    enum c35_result injected_result
)
{
    if (fault == NULL || !c35_binding_valid(inner) ||
        target <= C35_BINDING_CUT_NONE || target >= C35_BINDING_CUT_COUNT ||
        effect > C35_FAULT_AFTER_EFFECT || occurrence == 0 ||
        injected_result == C35_OK || injected_result == C35_IN_PROGRESS)
        return 0;
    memset(fault, 0, sizeof(*fault));
    fault->inner = *inner;
    fault->target = (uint8_t)target;
    fault->effect = (uint8_t)effect;
    fault->occurrence = occurrence;
    fault->injected_result = injected_result;
    return 1;
}

struct c35_binding c35_fault_binding_provider(struct c35_fault_binding *fault)
{
    struct c35_binding binding;

    binding.ops = fault != NULL ? &fault_ops : NULL;
    binding.context = fault;
    return binding;
}

int c35_fault_binding_corrupt_result_init(
    struct c35_fault_binding *fault,
    const struct c35_binding *inner
)
{
    if (fault == NULL || !c35_binding_valid(inner)) return 0;
    memset(fault, 0, sizeof(*fault));
    fault->inner = *inner;
    fault->target = C35_BINDING_CUT_RESULT_PREPARE;
    fault->effect = C35_FAULT_AFTER_EFFECT;
    fault->occurrence = 1;
    fault->injected_result = C35_OK;
    fault->corrupt_candidate = 1;
    return 1;
}
