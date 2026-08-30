/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_LIFECYCLE_PORT_H
#define FWLAB_C35_LIFECYCLE_PORT_H

#include "fwlab/portable/c31.h"

#define C35_LIFECYCLE_PORT_VERSION 2u

struct c35_lifecycle_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    enum fwlab_c31_instance_phase (*phase)(const void *context);
    enum fwlab_c31_api_result (*submit)(
        void *context,
        const struct fwlab_c31_command_descriptor *descriptor,
        struct fwlab_c31_command_handle *command
    );
    enum fwlab_c31_api_result (*step)(
        void *context,
        uint32_t budget,
        struct fwlab_c31_step_result *result
    );
    enum fwlab_c31_api_result (*command_state)(
        const void *context,
        const struct fwlab_c31_command_handle *command,
        enum fwlab_c31_lifecycle_state *state
    );
    enum fwlab_c31_api_result (*completion_acquire)(
        void *context,
        const struct fwlab_c31_command_handle *command,
        struct fwlab_c31_completion_lease *lease,
        struct fwlab_c31_completion_intent *intent
    );
    enum fwlab_c31_api_result (*completion_release)(
        void *context,
        const struct fwlab_c31_completion_lease *lease
    );
    enum fwlab_c31_api_result (*completion_consume)(
        void *context,
        const struct fwlab_c31_completion_lease *lease
    );
    enum fwlab_c31_api_result (*abort_request)(
        void *context,
        const struct fwlab_c31_command_handle *command,
        struct fwlab_c31_abort_ticket *ticket,
        enum fwlab_c31_abort_outcome *outcome
    );
    enum fwlab_c31_api_result (*abort_query)(
        const void *context,
        const struct fwlab_c31_abort_ticket *ticket,
        enum fwlab_c31_abort_outcome *outcome
    );
    enum fwlab_c31_api_result (*abort_ack)(
        void *context,
        const struct fwlab_c31_abort_ticket *ticket
    );
    enum fwlab_c31_api_result (*reset_begin)(void *context);
    enum fwlab_c31_api_result (*reset_ack)(void *context);
    enum fwlab_c31_api_result (*teardown_begin)(void *context);
    enum fwlab_c31_api_result (*teardown_ack)(void *context);
};

struct c35_lifecycle_port {
    const struct c35_lifecycle_ops *ops;
    void *context;
};

int c35_lifecycle_port_valid(const struct c35_lifecycle_port *port);
struct c35_lifecycle_port c35_lifecycle_port_native(
    struct fwlab_c31 *lifecycle
);

#endif
