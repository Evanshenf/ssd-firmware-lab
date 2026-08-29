/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_headless.h"

#include <string.h>

static int binding_valid(const struct c35_binding *binding)
{
    return binding != NULL && binding->ops != NULL &&
           binding->context != NULL &&
           binding->ops->version == C35_BINDING_VERSION &&
           binding->ops->size == sizeof(*binding->ops) &&
           binding->ops->reserved == 0 &&
           binding->ops->register_after_submit != NULL &&
           binding->ops->result_copy_before_consume != NULL &&
           binding->ops->result_ack_after_consume != NULL &&
           binding->ops->post_reset_recover != NULL &&
           binding->ops->semantic_snapshot != NULL &&
           binding->ops->quiescent != NULL;
}

static int bytes_zero(const uint8_t *bytes, size_t length)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int request_valid(const struct c35_request *request)
{
    unsigned int atom;

    if (request == NULL || request->version != C35_BINDING_VERSION ||
        request->size != sizeof(*request) || request->kind > C35_FENCE ||
        request->reserved[0] != 0 || request->reserved[1] != 0) {
        return 0;
    }
    if (request->kind == C35_READ) {
        return request->durability_kind == 0 && request->atom_mask == 0 &&
               request->atom < C35_ATOMS && request->sequence == 0 &&
               request->frontier == 0 &&
               bytes_zero(&request->payload[0][0], sizeof(request->payload));
    }
    if (request->kind == C35_FENCE) {
        return request->durability_kind == 2 && request->atom_mask == 0 &&
               request->sequence == 0 && request->frontier != 0 &&
               bytes_zero(&request->payload[0][0], sizeof(request->payload));
    }
    if ((request->durability_kind != 0 &&
         request->durability_kind != 1) || request->atom_mask == 0 ||
        (request->atom_mask & ~UINT8_C(0x03)) != 0 ||
        request->sequence == 0 || request->frontier != 0) {
        return 0;
    }
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        if ((request->atom_mask & (uint8_t)(1u << atom)) == 0 &&
            !bytes_zero(request->payload[atom], C35_ATOM_BYTES)) {
            return 0;
        }
        if (request->kind == C35_TRIM &&
            !bytes_zero(request->payload[atom], C35_ATOM_BYTES)) {
            return 0;
        }
    }
    return 1;
}

enum c35_result c35_headless_init(
    struct c35_headless *headless,
    struct fwlab_c31 *lifecycle,
    const struct c35_binding *binding,
    struct c35_trace *trace,
    uint64_t instance_nonce,
    uint32_t owner_epoch,
    uint8_t actor
)
{
    if (headless == NULL || lifecycle == NULL || !binding_valid(binding) ||
        trace == NULL || instance_nonce == 0 || owner_epoch == 0) {
        return C35_INVALID;
    }
    memset(headless, 0, sizeof(*headless));
    headless->lifecycle = lifecycle;
    headless->binding = *binding;
    headless->trace = trace;
    headless->instance_nonce = instance_nonce;
    headless->owner_epoch = owner_epoch;
    headless->next_request = 1;
    headless->actor = actor;
    headless->admission_open = 1;
    return C35_OK;
}

static void descriptor_make(
    struct c35_headless *headless,
    struct fwlab_c31_command_descriptor *descriptor,
    struct fwlab_c31_request_token *token
)
{
    uint64_t identity = headless->next_request++;

    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->version = FWLAB_C31_CONTRACT_VERSION;
    descriptor->size = sizeof(*descriptor);
    descriptor->origin.word[0] = headless->instance_nonce ^
                                 UINT64_C(0x3511000000000000) ^ identity;
    descriptor->origin.word[1] = ((uint64_t)headless->owner_epoch << 32) |
                                 identity;
    descriptor->trace_cookie = UINT64_C(0x3500000000000000) | identity;
    token->word[0] = UINT64_C(0xc350000000000000) | identity;
    token->word[1] = headless->instance_nonce ^ identity;
    descriptor->provider_request = *token;
    descriptor->provider_kind = FWLAB_C31_PROVIDER_NFC;
    descriptor->dma_direction = FWLAB_C31_DMA_NONE;
}

static void rollback_accepted(
    struct c35_headless *headless,
    const struct fwlab_c31_command_handle *command
)
{
    struct fwlab_c31_abort_ticket ticket;
    enum fwlab_c31_abort_outcome outcome;
    unsigned int iteration;

    if (fwlab_c31_abort_request(
            headless->lifecycle, command, &ticket, &outcome) !=
        FWLAB_C31_API_OK) {
        return;
    }
    for (iteration = 0; iteration < 64; ++iteration) {
        enum fwlab_c31_lifecycle_state state;
        struct fwlab_c31_step_result step;

        if (fwlab_c31_command_state(
                headless->lifecycle, command, &state) != FWLAB_C31_API_OK) {
            break;
        }
        if (state == FWLAB_C31_CMD_COMPLETION_READY) {
            struct fwlab_c31_completion_lease lease;
            struct fwlab_c31_completion_intent intent;

            if (fwlab_c31_completion_acquire(
                    headless->lifecycle, command, &lease, &intent) ==
                FWLAB_C31_API_OK) {
                (void)fwlab_c31_completion_consume(
                    headless->lifecycle, &lease);
            }
            break;
        }
        if (fwlab_c31_step(headless->lifecycle, 1, &step) !=
            FWLAB_C31_API_OK) {
            break;
        }
    }
    (void)fwlab_c31_abort_ack(headless->lifecycle, &ticket);
}

enum c35_result c35_headless_submit(
    struct c35_headless *headless,
    const struct c35_request *request,
    struct fwlab_c31_command_handle *command
)
{
    struct c35_submission submission;
    enum c35_result result;

    if (command == NULL) {
        return C35_INVALID;
    }
    result = c35_headless_submit_observed(headless, request, &submission);
    if (result == C35_OK) {
        *command = submission.command;
    }
    return result;
}

enum c35_result c35_headless_submit_observed(
    struct c35_headless *headless,
    const struct c35_request *request,
    struct c35_submission *submission
)
{
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_request_token token;
    enum c35_result result;

    if (headless == NULL || submission == NULL ||
        !headless->admission_open || !request_valid(request)) {
        return C35_INVALID;
    }
    descriptor_make(headless, &descriptor, &token);
    memset(submission, 0, sizeof(*submission));
    if (fwlab_c31_submit(
            headless->lifecycle, &descriptor, &submission->command) !=
        FWLAB_C31_API_OK) {
        return C35_NO_CAPACITY;
    }
    result = headless->binding.ops->register_after_submit(
        headless->binding.context, &token, &submission->command,
        headless->owner_epoch, request);
    if (result != C35_OK) {
        rollback_accepted(headless, &submission->command);
        return result;
    }
    submission->request = token;
    submission->owner_epoch = headless->owner_epoch;
    return C35_OK;
}

enum c35_result c35_headless_complete(
    struct c35_headless *headless,
    const struct fwlab_c31_command_handle *command,
    struct c35_semantic_result *semantic,
    struct fwlab_c31_completion_intent *intent
)
{
    unsigned int iteration;

    if (headless == NULL || command == NULL || semantic == NULL ||
        intent == NULL) {
        return C35_INVALID;
    }
    for (iteration = 0; iteration < 8192; ++iteration) {
        enum fwlab_c31_lifecycle_state state;
        struct fwlab_c31_step_result step;

        if (fwlab_c31_command_state(
                headless->lifecycle, command, &state) != FWLAB_C31_API_OK) {
            return C35_STALE;
        }
        if (state == FWLAB_C31_CMD_COMPLETION_READY) {
            struct fwlab_c31_completion_lease lease;
            struct c35_trace_event event;
            enum c35_result result;

            if (fwlab_c31_completion_acquire(
                    headless->lifecycle, command, &lease, intent) !=
                FWLAB_C31_API_OK) {
                return C35_INVARIANT;
            }
            result = headless->binding.ops->result_copy_before_consume(
                headless->binding.context, command, intent, semantic);
            if (result != C35_OK) {
                (void)fwlab_c31_completion_release(
                    headless->lifecycle, &lease);
                return result;
            }
            memset(&event, 0, sizeof(event));
            event.kind = C35_TRACE_COMMAND;
            event.actor = headless->actor;
            event.request_kind = semantic->request_kind;
            event.terminal = intent->result == FWLAB_C31_COMPLETION_SUCCESS ?
                FWLAB_C31_PROVIDER_SUCCESS : FWLAB_C31_PROVIDER_FAILED;
            event.completion_result = (uint8_t)intent->result;
            event.effect_class = intent->fault.effect_class;
            event.witness_class = semantic->witness_class;
            event.witness_reason = semantic->witness_reason;
            event.status = semantic->status;
            event.atom_mask = semantic->atom_mask;
            event.present_mask = semantic->present_mask;
            event.epoch = headless->owner_epoch;
            event.ordinal = headless->trace->events;
            event.semantic = *semantic;
            if (c35_trace_append(headless->trace, &event) != C35_OK ||
                fwlab_c31_completion_consume(
                    headless->lifecycle, &lease) != FWLAB_C31_API_OK ||
                headless->binding.ops->result_ack_after_consume(
                    headless->binding.context, command) != C35_OK) {
                return C35_INVARIANT;
            }
            return C35_OK;
        }
        if (fwlab_c31_step(headless->lifecycle, 1, &step) !=
            FWLAB_C31_API_OK) {
            return C35_PROVIDER_FAILURE;
        }
    }
    return C35_LIMIT;
}

enum c35_result c35_headless_pump_quiescent(
    struct c35_headless *headless,
    uint32_t limit
)
{
    uint32_t iteration;

    for (iteration = 0; iteration < limit; ++iteration) {
        bool quiescent;
        struct fwlab_c31_step_result step;

        if (headless->binding.ops->quiescent(
                headless->binding.context, &quiescent) != C35_OK) {
            return C35_INVARIANT;
        }
        if (quiescent) {
            return C35_OK;
        }
        if (fwlab_c31_step(headless->lifecycle, 1, &step) !=
            FWLAB_C31_API_OK) {
            return C35_PROVIDER_FAILURE;
        }
    }
    return C35_LIMIT;
}

enum c35_result c35_headless_reset(struct c35_headless *headless, uint32_t limit)
{
    uint32_t iteration;
    bool quiescent;
    struct c35_trace_event event;

    if (headless == NULL || !headless->admission_open) {
        return C35_WRONG_STATE;
    }
    headless->admission_open = 0;
    if (fwlab_c31_reset_begin(headless->lifecycle) != FWLAB_C31_API_OK) {
        return C35_INVARIANT;
    }
    for (iteration = 0; iteration < limit; ++iteration) {
        struct fwlab_c31_step_result step;

        if (fwlab_c31_phase(headless->lifecycle) ==
            FWLAB_C31_INSTANCE_RESET_ACK) {
            break;
        }
        if (fwlab_c31_step(headless->lifecycle, 1, &step) !=
            FWLAB_C31_API_OK) {
            return C35_PROVIDER_FAILURE;
        }
    }
    if (iteration == limit ||
        headless->binding.ops->post_reset_recover(
            headless->binding.context) != C35_OK ||
        headless->binding.ops->quiescent(
            headless->binding.context, &quiescent) != C35_OK ||
        !quiescent ||
        fwlab_c31_reset_ack(headless->lifecycle) != FWLAB_C31_API_OK) {
        return C35_INVARIANT;
    }
    ++headless->owner_epoch;
    headless->admission_open = 1;
    memset(&event, 0, sizeof(event));
    event.kind = C35_TRACE_RESET;
    event.actor = headless->actor;
    event.epoch = headless->owner_epoch;
    event.ordinal = headless->trace->events;
    return c35_trace_append(headless->trace, &event);
}

enum c35_result c35_headless_teardown(
    struct c35_headless *headless,
    uint32_t limit
)
{
    uint32_t iteration;
    bool quiescent;
    struct c35_trace_event event;

    if (headless == NULL || !headless->admission_open) {
        return C35_WRONG_STATE;
    }
    headless->admission_open = 0;
    if (fwlab_c31_teardown_begin(headless->lifecycle) != FWLAB_C31_API_OK) {
        return C35_INVARIANT;
    }
    for (iteration = 0; iteration < limit; ++iteration) {
        struct fwlab_c31_step_result step;

        if (fwlab_c31_phase(headless->lifecycle) ==
            FWLAB_C31_INSTANCE_TEARDOWN_ACK) {
            break;
        }
        if (fwlab_c31_step(headless->lifecycle, 1, &step) !=
            FWLAB_C31_API_OK) {
            return C35_PROVIDER_FAILURE;
        }
    }
    if (iteration == limit ||
        headless->binding.ops->quiescent(
            headless->binding.context, &quiescent) != C35_OK ||
        !quiescent ||
        fwlab_c31_teardown_ack(headless->lifecycle) != FWLAB_C31_API_OK) {
        return C35_INVARIANT;
    }
    headless->teardown_complete = 1;
    memset(&event, 0, sizeof(event));
    event.kind = C35_TRACE_TEARDOWN;
    event.actor = headless->actor;
    event.epoch = headless->owner_epoch;
    event.ordinal = headless->trace->events;
    return c35_trace_append(headless->trace, &event);
}
