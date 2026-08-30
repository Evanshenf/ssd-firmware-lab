/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_fault_lifecycle.h"

#include <string.h>

static int cut_now(
    struct c35_fault_lifecycle *fault,
    enum c35_lifecycle_cut call
)
{
    uint32_t count = ++fault->calls[call];

    if (fault->injected || fault->target != (uint8_t)call ||
        count != fault->occurrence) return 0;
    fault->injected = 1;
    return 1;
}

static enum fwlab_c31_instance_phase fault_phase(const void *context)
{
    const struct c35_fault_lifecycle *fault = context;

    return fault->inner.ops->phase(fault->inner.context);
}

static enum fwlab_c31_api_result fault_submit(
    void *context,
    const struct fwlab_c31_command_descriptor *descriptor,
    struct fwlab_c31_command_handle *command
)
{
    struct c35_fault_lifecycle *fault = context;
    int inject = cut_now(fault, C35_LIFECYCLE_CUT_SUBMIT);
    enum fwlab_c31_api_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->submit(
        fault->inner.context, descriptor, command);
    return inject && result == FWLAB_C31_API_OK ?
        fault->injected_result : result;
}

static enum fwlab_c31_api_result fault_step(
    void *context,
    uint32_t budget,
    struct fwlab_c31_step_result *result_out
)
{
    struct c35_fault_lifecycle *fault = context;
    int inject = cut_now(fault, C35_LIFECYCLE_CUT_STEP);
    enum fwlab_c31_api_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->step(fault->inner.context, budget, result_out);
    return inject && result == FWLAB_C31_API_OK ?
        fault->injected_result : result;
}

static enum fwlab_c31_api_result fault_command_state(
    const void *context,
    const struct fwlab_c31_command_handle *command,
    enum fwlab_c31_lifecycle_state *state
)
{
    struct c35_fault_lifecycle *fault = (struct c35_fault_lifecycle *)context;
    int inject = cut_now(fault, C35_LIFECYCLE_CUT_COMMAND_STATE);
    enum fwlab_c31_api_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->command_state(
        fault->inner.context, command, state);
    return inject && result == FWLAB_C31_API_OK ?
        fault->injected_result : result;
}

static enum fwlab_c31_api_result fault_completion_acquire(
    void *context,
    const struct fwlab_c31_command_handle *command,
    struct fwlab_c31_completion_lease *lease,
    struct fwlab_c31_completion_intent *intent
)
{
    struct c35_fault_lifecycle *fault = context;
    int inject = cut_now(fault, C35_LIFECYCLE_CUT_COMPLETION_ACQUIRE);
    enum fwlab_c31_api_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->completion_acquire(
        fault->inner.context, command, lease, intent);
    return inject && result == FWLAB_C31_API_OK ?
        fault->injected_result : result;
}

static enum fwlab_c31_api_result fault_completion_release(
    void *context,
    const struct fwlab_c31_completion_lease *lease
)
{
    struct c35_fault_lifecycle *fault = context;
    int inject = cut_now(fault, C35_LIFECYCLE_CUT_COMPLETION_RELEASE);
    enum fwlab_c31_api_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->completion_release(
        fault->inner.context, lease);
    return inject && result == FWLAB_C31_API_OK ?
        fault->injected_result : result;
}

static enum fwlab_c31_api_result fault_completion_consume(
    void *context,
    const struct fwlab_c31_completion_lease *lease
)
{
    struct c35_fault_lifecycle *fault = context;
    int inject = cut_now(fault, C35_LIFECYCLE_CUT_COMPLETION_CONSUME);
    enum fwlab_c31_api_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->completion_consume(
        fault->inner.context, lease);
    return inject && result == FWLAB_C31_API_OK ?
        fault->injected_result : result;
}

static enum fwlab_c31_api_result fault_abort_request(
    void *context,
    const struct fwlab_c31_command_handle *command,
    struct fwlab_c31_abort_ticket *ticket,
    enum fwlab_c31_abort_outcome *outcome
)
{
    struct c35_fault_lifecycle *fault = context;
    int inject = cut_now(fault, C35_LIFECYCLE_CUT_ABORT_REQUEST);
    enum fwlab_c31_api_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->abort_request(
        fault->inner.context, command, ticket, outcome);
    return inject && result == FWLAB_C31_API_OK ?
        fault->injected_result : result;
}

static enum fwlab_c31_api_result fault_abort_query(
    const void *context,
    const struct fwlab_c31_abort_ticket *ticket,
    enum fwlab_c31_abort_outcome *outcome
)
{
    struct c35_fault_lifecycle *fault = (struct c35_fault_lifecycle *)context;
    int inject = cut_now(fault, C35_LIFECYCLE_CUT_ABORT_QUERY);
    enum fwlab_c31_api_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->abort_query(
        fault->inner.context, ticket, outcome);
    return inject && result == FWLAB_C31_API_OK ?
        fault->injected_result : result;
}

static enum fwlab_c31_api_result fault_abort_ack(
    void *context,
    const struct fwlab_c31_abort_ticket *ticket
)
{
    struct c35_fault_lifecycle *fault = context;
    int inject = cut_now(fault, C35_LIFECYCLE_CUT_ABORT_ACK);
    enum fwlab_c31_api_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->abort_ack(fault->inner.context, ticket);
    return inject && result == FWLAB_C31_API_OK ?
        fault->injected_result : result;
}

static enum fwlab_c31_api_result fault_reset_begin(void *context)
{
    struct c35_fault_lifecycle *fault = context;
    int inject = cut_now(fault, C35_LIFECYCLE_CUT_RESET_BEGIN);
    enum fwlab_c31_api_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->reset_begin(fault->inner.context);
    return inject && result == FWLAB_C31_API_OK ?
        fault->injected_result : result;
}

static enum fwlab_c31_api_result fault_reset_ack(void *context)
{
    struct c35_fault_lifecycle *fault = context;
    int inject = cut_now(fault, C35_LIFECYCLE_CUT_RESET_ACK);
    enum fwlab_c31_api_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->reset_ack(fault->inner.context);
    return inject && result == FWLAB_C31_API_OK ?
        fault->injected_result : result;
}

static enum fwlab_c31_api_result fault_teardown_begin(void *context)
{
    struct c35_fault_lifecycle *fault = context;
    int inject = cut_now(fault, C35_LIFECYCLE_CUT_TEARDOWN_BEGIN);
    enum fwlab_c31_api_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->teardown_begin(fault->inner.context);
    return inject && result == FWLAB_C31_API_OK ?
        fault->injected_result : result;
}

static enum fwlab_c31_api_result fault_teardown_ack(void *context)
{
    struct c35_fault_lifecycle *fault = context;
    int inject = cut_now(fault, C35_LIFECYCLE_CUT_TEARDOWN_ACK);
    enum fwlab_c31_api_result result;

    if (inject && fault->effect == C35_FAULT_BEFORE_EFFECT)
        return fault->injected_result;
    result = fault->inner.ops->teardown_ack(fault->inner.context);
    return inject && result == FWLAB_C31_API_OK ?
        fault->injected_result : result;
}

static const struct c35_lifecycle_ops fault_ops = {
    .version = C35_LIFECYCLE_PORT_VERSION,
    .size = sizeof(struct c35_lifecycle_ops),
    .reserved = 0,
    .phase = fault_phase,
    .submit = fault_submit,
    .step = fault_step,
    .command_state = fault_command_state,
    .completion_acquire = fault_completion_acquire,
    .completion_release = fault_completion_release,
    .completion_consume = fault_completion_consume,
    .abort_request = fault_abort_request,
    .abort_query = fault_abort_query,
    .abort_ack = fault_abort_ack,
    .reset_begin = fault_reset_begin,
    .reset_ack = fault_reset_ack,
    .teardown_begin = fault_teardown_begin,
    .teardown_ack = fault_teardown_ack,
};

int c35_fault_lifecycle_init(
    struct c35_fault_lifecycle *fault,
    const struct c35_lifecycle_port *inner,
    enum c35_lifecycle_cut target,
    enum c35_fault_effect effect,
    uint32_t occurrence,
    enum fwlab_c31_api_result injected_result
)
{
    if (fault == NULL || !c35_lifecycle_port_valid(inner) ||
        target <= C35_LIFECYCLE_CUT_NONE ||
        target >= C35_LIFECYCLE_CUT_COUNT ||
        effect > C35_FAULT_AFTER_EFFECT || occurrence == 0 ||
        injected_result == FWLAB_C31_API_OK) return 0;
    memset(fault, 0, sizeof(*fault));
    fault->inner = *inner;
    fault->target = (uint8_t)target;
    fault->effect = (uint8_t)effect;
    fault->occurrence = occurrence;
    fault->injected_result = injected_result;
    return 1;
}

struct c35_lifecycle_port c35_fault_lifecycle_port(
    struct c35_fault_lifecycle *fault
)
{
    struct c35_lifecycle_port port;

    port.ops = fault != NULL ? &fault_ops : NULL;
    port.context = fault;
    return port;
}
