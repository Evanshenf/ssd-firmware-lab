/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_lifecycle_port.h"

static enum fwlab_c31_instance_phase native_phase(const void *context)
{
    return fwlab_c31_phase(context);
}

static enum fwlab_c31_api_result native_submit(
    void *context,
    const struct fwlab_c31_command_descriptor *descriptor,
    struct fwlab_c31_command_handle *command
)
{
    return fwlab_c31_submit(context, descriptor, command);
}

static enum fwlab_c31_api_result native_step(
    void *context,
    uint32_t budget,
    struct fwlab_c31_step_result *result
)
{
    return fwlab_c31_step(context, budget, result);
}

static enum fwlab_c31_api_result native_command_state(
    const void *context,
    const struct fwlab_c31_command_handle *command,
    enum fwlab_c31_lifecycle_state *state
)
{
    return fwlab_c31_command_state(context, command, state);
}

static enum fwlab_c31_api_result native_completion_acquire(
    void *context,
    const struct fwlab_c31_command_handle *command,
    struct fwlab_c31_completion_lease *lease,
    struct fwlab_c31_completion_intent *intent
)
{
    return fwlab_c31_completion_acquire(context, command, lease, intent);
}

static enum fwlab_c31_api_result native_completion_release(
    void *context,
    const struct fwlab_c31_completion_lease *lease
)
{
    return fwlab_c31_completion_release(context, lease);
}

static enum fwlab_c31_api_result native_completion_consume(
    void *context,
    const struct fwlab_c31_completion_lease *lease
)
{
    return fwlab_c31_completion_consume(context, lease);
}

static enum fwlab_c31_api_result native_abort_request(
    void *context,
    const struct fwlab_c31_command_handle *command,
    struct fwlab_c31_abort_ticket *ticket,
    enum fwlab_c31_abort_outcome *outcome
)
{
    return fwlab_c31_abort_request(context, command, ticket, outcome);
}

static enum fwlab_c31_api_result native_abort_query(
    const void *context,
    const struct fwlab_c31_abort_ticket *ticket,
    enum fwlab_c31_abort_outcome *outcome
)
{
    return fwlab_c31_abort_query(context, ticket, outcome);
}

static enum fwlab_c31_api_result native_abort_ack(
    void *context,
    const struct fwlab_c31_abort_ticket *ticket
)
{
    return fwlab_c31_abort_ack(context, ticket);
}

static enum fwlab_c31_api_result native_reset_begin(void *context)
{
    return fwlab_c31_reset_begin(context);
}

static enum fwlab_c31_api_result native_reset_ack(void *context)
{
    return fwlab_c31_reset_ack(context);
}

static enum fwlab_c31_api_result native_teardown_begin(void *context)
{
    return fwlab_c31_teardown_begin(context);
}

static enum fwlab_c31_api_result native_teardown_ack(void *context)
{
    return fwlab_c31_teardown_ack(context);
}

static const struct c35_lifecycle_ops native_ops = {
    .version = C35_LIFECYCLE_PORT_VERSION,
    .size = sizeof(struct c35_lifecycle_ops),
    .reserved = 0,
    .phase = native_phase,
    .submit = native_submit,
    .step = native_step,
    .command_state = native_command_state,
    .completion_acquire = native_completion_acquire,
    .completion_release = native_completion_release,
    .completion_consume = native_completion_consume,
    .abort_request = native_abort_request,
    .abort_query = native_abort_query,
    .abort_ack = native_abort_ack,
    .reset_begin = native_reset_begin,
    .reset_ack = native_reset_ack,
    .teardown_begin = native_teardown_begin,
    .teardown_ack = native_teardown_ack,
};

int c35_lifecycle_port_valid(const struct c35_lifecycle_port *port)
{
    return port != NULL && port->ops != NULL && port->context != NULL &&
           port->ops->version == C35_LIFECYCLE_PORT_VERSION &&
           port->ops->size == sizeof(*port->ops) && port->ops->reserved == 0 &&
           port->ops->phase != NULL && port->ops->submit != NULL &&
           port->ops->step != NULL && port->ops->command_state != NULL &&
           port->ops->completion_acquire != NULL &&
           port->ops->completion_release != NULL &&
           port->ops->completion_consume != NULL &&
           port->ops->abort_request != NULL &&
           port->ops->abort_query != NULL && port->ops->abort_ack != NULL &&
           port->ops->reset_begin != NULL && port->ops->reset_ack != NULL &&
           port->ops->teardown_begin != NULL &&
           port->ops->teardown_ack != NULL;
}

struct c35_lifecycle_port c35_lifecycle_port_native(
    struct fwlab_c31 *lifecycle
)
{
    struct c35_lifecycle_port port;

    port.ops = lifecycle != NULL ? &native_ops : NULL;
    port.context = lifecycle;
    return port;
}
