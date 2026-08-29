/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c31_internal.h"

#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#include "fwlab/portable/c31_codec.h"

static size_t align_up(size_t value, size_t alignment)
{
    size_t mask;

    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return 0;
    }
    mask = alignment - 1;
    if (value > SIZE_MAX - mask) {
        return 0;
    }
    return (value + mask) & ~mask;
}

static int add_array(
    size_t *offset,
    size_t count,
    size_t element_size,
    size_t alignment
)
{
    size_t aligned = align_up(*offset, alignment);
    size_t bytes;

    if (aligned == 0 && *offset != 0) {
        return 0;
    }
    if (count != 0 && element_size > SIZE_MAX / count) {
        return 0;
    }
    bytes = count * element_size;
    if (aligned > SIZE_MAX - bytes) {
        return 0;
    }
    *offset = aligned + bytes;
    return 1;
}

static int capacity_valid(const struct fwlab_c31_capacity *capacity)
{
    return capacity != NULL &&
           capacity->version == FWLAB_C31_CONTRACT_VERSION &&
           capacity->size == sizeof(*capacity) &&
           capacity->reserved0 == 0 && capacity->reserved1 == 0 &&
           capacity->commands > 0 &&
           capacity->commands <= FWLAB_C31_HARD_MAX_COMMANDS &&
           capacity->abort_tickets > 0 &&
           capacity->abort_tickets <= FWLAB_C31_HARD_MAX_ABORT_TICKETS &&
           capacity->event_batch > 0 &&
           capacity->event_batch <= FWLAB_C31_HARD_MAX_EVENT_BATCH &&
           capacity->trace_entries > 0 &&
           capacity->trace_entries <= FWLAB_C31_HARD_MAX_TRACE_ENTRIES &&
           capacity->scratch_bytes >= FWLAB_C31_DESCRIPTOR_WIRE_SIZE &&
           capacity->scratch_bytes <= FWLAB_C31_HARD_MAX_SCRATCH_BYTES &&
           capacity->slot_generation_limit > 0 &&
           capacity->slot_generation_limit <= UINT16_MAX &&
           capacity->operation_generation_limit > 0 &&
           capacity->lease_generation_limit > 0 &&
           capacity->ticket_generation_limit > 0 &&
           capacity->controller_epoch_limit > 0 &&
           capacity->command_uid_limit > 0;
}

static int provider_valid(const struct fwlab_c31_provider *provider)
{
    const struct fwlab_c31_provider_ops *ops;

    if (provider->ops == NULL) {
        return provider->context == NULL;
    }
    ops = provider->ops;
    return ops->version == FWLAB_C31_PROVIDER_CONTRACT_VERSION &&
           ops->size == sizeof(*ops) && ops->reserved == 0 &&
           ops->try_submit != NULL && ops->cancel != NULL &&
           ops->poll != NULL && ops->reset_begin != NULL &&
           ops->quiescent != NULL;
}

static int instance_valid(const struct fwlab_c31 *instance)
{
    return instance != NULL && instance->magic == FWLAB_C31_MAGIC;
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

static int operation_equal(
    const struct fwlab_c31_operation_token *left,
    const struct fwlab_c31_operation_token *right
)
{
    return command_equal(&left->command, &right->command) &&
           left->cookie == right->cookie &&
           left->operation_generation == right->operation_generation &&
           left->reserved == right->reserved;
}

static void zero_command(struct fwlab_c31_command_handle *command)
{
    memset(command, 0, sizeof(*command));
}

static void trace_push(
    struct fwlab_c31 *instance,
    uint32_t kind,
    const struct fwlab_c31_command_handle *command,
    uint32_t from_state,
    uint32_t to_state,
    uint32_t detail
)
{
    struct fwlab_c31_trace_entry *entry;
    uint32_t index;

    if (instance->trace_sequence == UINT64_MAX) {
        instance->phase = FWLAB_C31_INSTANCE_FAULTED;
        return;
    }
    if (instance->trace_count < instance->capacity.trace_entries) {
        index = (instance->trace_head + instance->trace_count) %
                instance->capacity.trace_entries;
        ++instance->trace_count;
    } else {
        index = instance->trace_head;
        instance->trace_head = (instance->trace_head + 1) %
                               instance->capacity.trace_entries;
    }
    entry = &instance->trace[index];
    memset(entry, 0, sizeof(*entry));
    entry->sequence = ++instance->trace_sequence;
    if (command != NULL) {
        entry->command = *command;
    } else {
        zero_command(&entry->command);
    }
    entry->kind = kind;
    entry->from_state = from_state;
    entry->to_state = to_state;
    entry->detail = detail;
}

static enum fwlab_c31_api_result instance_fault(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_command_handle *command,
    uint32_t detail
)
{
    uint32_t old_phase = instance->phase;

    instance->phase = FWLAB_C31_INSTANCE_FAULTED;
    trace_push(instance, FWLAB_C31_TRACE_FAULT, command, old_phase,
               FWLAB_C31_INSTANCE_FAULTED, detail);
    return FWLAB_C31_API_INVARIANT_FAILURE;
}

static enum fwlab_c31_api_result descriptor_valid(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_command_descriptor *descriptor
)
{
    return fwlab_c31_descriptor_encode(
        descriptor, instance->scratch, FWLAB_C31_DESCRIPTOR_WIRE_SIZE);
}

static int fault_valid(const struct fwlab_c31_fault *fault, uint32_t length)
{
    if (fault->reserved != 0 ||
        fault->domain > FWLAB_C31_FAULT_PROVIDER ||
        fault->retry_class > FWLAB_C31_RETRY_NEVER ||
        fault->effect_class > FWLAB_C31_EFFECT_UNKNOWN_PREFIX) {
        return 0;
    }
    if ((fault->effect_class == FWLAB_C31_EFFECT_EXACT_PREFIX ||
         fault->effect_class == FWLAB_C31_EFFECT_UNKNOWN_PREFIX) &&
        fault->prefix_length > length) {
        return 0;
    }
    if ((fault->effect_class == FWLAB_C31_EFFECT_NONE ||
         fault->effect_class == FWLAB_C31_EFFECT_FULL) &&
        fault->prefix_length != 0) {
        return 0;
    }
    if (fault->domain == FWLAB_C31_FAULT_NONE &&
        (fault->retry_class != FWLAB_C31_RETRY_NONE ||
         fault->reason != FWLAB_C31_REASON_NONE)) {
        return 0;
    }
    return 1;
}

static int fault_is_zero(const struct fwlab_c31_fault *fault)
{
    static const struct fwlab_c31_fault zero;

    return memcmp(fault, &zero, sizeof(*fault)) == 0;
}

static struct fwlab_c31_provider *provider_for(
    struct fwlab_c31 *instance,
    uint8_t provider_kind
)
{
    if (provider_kind == FWLAB_C31_PROVIDER_DMA) {
        return &instance->providers.dma;
    }
    if (provider_kind == FWLAB_C31_PROVIDER_NFC) {
        return &instance->providers.nfc;
    }
    return NULL;
}

static const struct fwlab_c31_provider *provider_for_const(
    const struct fwlab_c31 *instance,
    uint8_t provider_kind
)
{
    if (provider_kind == FWLAB_C31_PROVIDER_DMA) {
        return &instance->providers.dma;
    }
    if (provider_kind == FWLAB_C31_PROVIDER_NFC) {
        return &instance->providers.nfc;
    }
    return NULL;
}

static struct fwlab_c31_command_slot *find_command(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_command_handle *command,
    enum fwlab_c31_api_result *result
)
{
    struct fwlab_c31_command_slot *slot;

    if (command == NULL || command->instance_nonce != instance->instance_nonce ||
        command->slot >= instance->capacity.commands) {
        *result = FWLAB_C31_API_STALE_TOKEN;
        return NULL;
    }
    slot = &instance->commands[command->slot];
    if (!slot->in_use || !command_equal(&slot->handle, command)) {
        *result = FWLAB_C31_API_STALE_TOKEN;
        return NULL;
    }
    *result = FWLAB_C31_API_OK;
    return slot;
}

static const struct fwlab_c31_command_slot *find_command_const(
    const struct fwlab_c31 *instance,
    const struct fwlab_c31_command_handle *command,
    enum fwlab_c31_api_result *result
)
{
    const struct fwlab_c31_command_slot *slot;

    if (command == NULL || command->instance_nonce != instance->instance_nonce ||
        command->slot >= instance->capacity.commands) {
        *result = FWLAB_C31_API_STALE_TOKEN;
        return NULL;
    }
    slot = &instance->commands[command->slot];
    if (!slot->in_use || !command_equal(&slot->handle, command)) {
        *result = FWLAB_C31_API_STALE_TOKEN;
        return NULL;
    }
    *result = FWLAB_C31_API_OK;
    return slot;
}

static void set_state(
    struct fwlab_c31 *instance,
    struct fwlab_c31_command_slot *slot,
    enum fwlab_c31_lifecycle_state state,
    uint32_t detail
)
{
    uint32_t old_state = slot->state;

    slot->state = (uint8_t)state;
    trace_push(instance, FWLAB_C31_TRACE_STATE, &slot->handle, old_state,
               state, detail);
}

static struct fwlab_c31_abort_slot *abort_for_command(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_command_handle *command
)
{
    uint32_t index;

    for (index = 0; index < instance->capacity.abort_tickets; ++index) {
        struct fwlab_c31_abort_slot *abort = &instance->aborts[index];

        if (abort->used && command_equal(&abort->ticket.command, command)) {
            return abort;
        }
    }
    return NULL;
}

static void update_abort(
    struct fwlab_c31 *instance,
    struct fwlab_c31_command_slot *slot,
    enum fwlab_c31_abort_outcome outcome
)
{
    struct fwlab_c31_abort_slot *abort =
        abort_for_command(instance, &slot->handle);

    if (abort != NULL && abort->outcome == FWLAB_C31_ABORT_PENDING) {
        abort->outcome = (uint8_t)outcome;
        trace_push(instance, FWLAB_C31_TRACE_ABORT, &slot->handle,
                   FWLAB_C31_ABORT_PENDING, outcome, 0);
    }
}

static void retire_command(
    struct fwlab_c31 *instance,
    struct fwlab_c31_command_slot *slot
)
{
    set_state(instance, slot, FWLAB_C31_CMD_RETIRED, 0);
    slot->provider_owned = false;
    slot->cancel_sent = false;
    slot->completion_valid = false;
    slot->lease_active = false;
    slot->abort_index = FWLAB_C31_NO_ABORT;
    slot->in_use = false;
    set_state(instance, slot, FWLAB_C31_CMD_FREE, 0);
}

static void make_completion(
    struct fwlab_c31 *instance,
    struct fwlab_c31_command_slot *slot,
    enum fwlab_c31_completion_result result,
    const struct fwlab_c31_fault *fault
)
{
    memset(&slot->completion, 0, sizeof(slot->completion));
    slot->completion.command = slot->handle;
    slot->completion.origin = slot->descriptor.origin;
    slot->completion.trace_cookie = slot->descriptor.trace_cookie;
    slot->completion.result = (uint32_t)result;
    if (fault != NULL) {
        slot->completion.fault = *fault;
    }
    slot->completion_valid = true;
    slot->lease_active = false;
    set_state(instance, slot, FWLAB_C31_CMD_COMPLETION_READY, result);
    trace_push(instance, FWLAB_C31_TRACE_COMPLETION, &slot->handle,
               result, result, slot->completion.fault.reason);
}

static enum fwlab_c31_completion_result provider_failure_result(
    uint8_t provider_kind
)
{
    if (provider_kind == FWLAB_C31_PROVIDER_DMA) {
        return FWLAB_C31_COMPLETION_TRANSFER_FAILURE;
    }
    if (provider_kind == FWLAB_C31_PROVIDER_NFC) {
        return FWLAB_C31_COMPLETION_MEDIA_FAILURE;
    }
    return FWLAB_C31_COMPLETION_RESOURCE_FAILURE;
}

static void prepare_provider_request(
    const struct fwlab_c31_command_slot *slot,
    struct fwlab_c31_provider_request *request
)
{
    memset(request, 0, sizeof(*request));
    request->version = FWLAB_C31_PROVIDER_CONTRACT_VERSION;
    request->size = (uint16_t)sizeof(*request);
    request->operation = slot->operation;
    request->origin = slot->descriptor.origin;
    request->request = slot->descriptor.provider_request;
    request->capability = slot->descriptor.capability;
    request->capability_offset = slot->descriptor.capability_offset;
    request->controller_region = slot->descriptor.controller_region;
    request->controller_offset = slot->descriptor.controller_offset;
    request->length = slot->descriptor.length;
    request->provider_kind = slot->descriptor.provider_kind;
    request->dma_direction = slot->descriptor.dma_direction;
    request->ordering_flags = slot->descriptor.ordering_flags;
}

static enum fwlab_c31_api_result submit_provider_operation(
    struct fwlab_c31 *instance,
    struct fwlab_c31_command_slot *slot
)
{
    struct fwlab_c31_provider *provider =
        provider_for(instance, slot->descriptor.provider_kind);
    struct fwlab_c31_provider_request request;
    struct fwlab_c31_provider_submit_result submit;

    if (provider == NULL || provider->ops == NULL) {
        make_completion(instance, slot,
                        FWLAB_C31_COMPLETION_UNSUPPORTED_COMMAND, NULL);
        return FWLAB_C31_API_OK;
    }
    prepare_provider_request(slot, &request);
    submit = provider->ops->try_submit(provider->context, &request);
    if (submit.disposition > FWLAB_C31_PROVIDER_REJECTED ||
        !fault_valid(&submit.fault, slot->descriptor.length) ||
        ((submit.disposition == FWLAB_C31_PROVIDER_ACCEPTED ||
          submit.disposition == FWLAB_C31_PROVIDER_BACKPRESSURE) &&
         !fault_is_zero(&submit.fault)) ||
        (submit.disposition == FWLAB_C31_PROVIDER_REJECTED &&
         (submit.fault.domain == FWLAB_C31_FAULT_NONE ||
          submit.fault.reason == FWLAB_C31_REASON_NONE))) {
        return instance_fault(instance, &slot->handle,
                              FWLAB_C31_REASON_INVARIANT);
    }
    if (submit.disposition == FWLAB_C31_PROVIDER_ACCEPTED) {
        slot->provider_owned = true;
        set_state(instance, slot, FWLAB_C31_CMD_RUNNING, 0);
        trace_push(instance, FWLAB_C31_TRACE_PROVIDER_ACCEPT,
                   &slot->handle, 0, 0, slot->descriptor.provider_kind);
    } else if (submit.disposition == FWLAB_C31_PROVIDER_BACKPRESSURE) {
        set_state(instance, slot, FWLAB_C31_CMD_HELD, 0);
    } else {
        make_completion(instance, slot,
                        provider_failure_result(slot->descriptor.provider_kind),
                        &submit.fault);
    }
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result process_provider_event(
    struct fwlab_c31 *instance,
    uint8_t provider_kind,
    const struct fwlab_c31_provider_event *event
)
{
    struct fwlab_c31_command_slot *slot;
    const struct fwlab_c31_command_handle *command = &event->operation.command;
    enum fwlab_c31_completion_result result;

    if (event->version != FWLAB_C31_PROVIDER_CONTRACT_VERSION ||
        event->size != sizeof(*event) || event->reserved0 != 0 ||
        event->reserved1 != 0 ||
        event->terminal > FWLAB_C31_PROVIDER_FAILED ||
        !fault_valid(&event->fault, UINT32_MAX)) {
        return instance_fault(instance, NULL, FWLAB_C31_REASON_INVARIANT);
    }
    if (command->instance_nonce != instance->instance_nonce ||
        command->slot >= instance->capacity.commands) {
        return FWLAB_C31_API_STALE_TOKEN;
    }
    slot = &instance->commands[command->slot];
    if (slot->last_terminal_valid &&
        operation_equal(&slot->last_terminal, &event->operation)) {
        return instance_fault(instance, command,
                              FWLAB_C31_REASON_DUPLICATE_EVENT);
    }
    if (!command_equal(&slot->handle, command) ||
        !operation_equal(&slot->operation, &event->operation)) {
        return FWLAB_C31_API_STALE_TOKEN;
    }
    if (slot->descriptor.provider_kind != provider_kind ||
        !slot->provider_owned ||
        (slot->state != FWLAB_C31_CMD_RUNNING &&
         slot->state != FWLAB_C31_CMD_CANCEL_PENDING &&
         slot->state != FWLAB_C31_CMD_RESET_DRAIN)) {
        return instance_fault(instance, command,
                              FWLAB_C31_REASON_INVARIANT);
    }
    if (!fault_valid(&event->fault, slot->descriptor.length) ||
        (event->terminal == FWLAB_C31_PROVIDER_SUCCESS &&
         event->fault.domain != FWLAB_C31_FAULT_NONE) ||
        (event->terminal != FWLAB_C31_PROVIDER_SUCCESS &&
         (event->fault.domain == FWLAB_C31_FAULT_NONE ||
          event->fault.reason == FWLAB_C31_REASON_NONE))) {
        return instance_fault(instance, command,
                              FWLAB_C31_REASON_INVARIANT);
    }

    slot->provider_owned = false;
    slot->last_terminal = event->operation;
    slot->last_terminal_valid = true;
    trace_push(instance, FWLAB_C31_TRACE_PROVIDER_EVENT, command,
               slot->state, slot->state, event->terminal);

    if (instance->phase == FWLAB_C31_INSTANCE_RESET_DRAIN ||
        instance->phase == FWLAB_C31_INSTANCE_TEARDOWN_DRAIN ||
        slot->state == FWLAB_C31_CMD_RESET_DRAIN) {
        retire_command(instance, slot);
        return FWLAB_C31_API_OK;
    }

    if (event->terminal == FWLAB_C31_PROVIDER_SUCCESS) {
        result = FWLAB_C31_COMPLETION_SUCCESS;
        update_abort(instance, slot, FWLAB_C31_ABORT_TOO_LATE);
    } else if (event->terminal == FWLAB_C31_PROVIDER_CANCELLED) {
        result = FWLAB_C31_COMPLETION_ABORTED;
        update_abort(instance, slot, FWLAB_C31_ABORT_TERMINAL);
    } else {
        result = provider_failure_result(provider_kind);
        update_abort(instance, slot, FWLAB_C31_ABORT_TERMINAL);
    }
    make_completion(instance, slot, result, &event->fault);
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result poll_provider(
    struct fwlab_c31 *instance,
    uint8_t provider_kind,
    uint32_t *event_count
)
{
    struct fwlab_c31_provider *provider =
        provider_for(instance, provider_kind);
    enum fwlab_c31_api_result result;
    uint32_t count = 0;

    *event_count = 0;
    if (provider == NULL || provider->ops == NULL) {
        return FWLAB_C31_API_OK;
    }
    memset(instance->events, 0,
           sizeof(*instance->events) * instance->capacity.event_batch);
    result = provider->ops->poll(provider->context, 1, instance->events,
                                 instance->capacity.event_batch, &count);
    if (result != FWLAB_C31_API_OK || count > 1 ||
        count > instance->capacity.event_batch) {
        return instance_fault(instance, NULL, FWLAB_C31_REASON_INVARIANT);
    }
    if (count == 1) {
        result = process_provider_event(instance, provider_kind,
                                        &instance->events[0]);
        if (result == FWLAB_C31_API_INVARIANT_FAILURE) {
            return result;
        }
        *event_count = 1;
    }
    return FWLAB_C31_API_OK;
}

static int progress_command(struct fwlab_c31 *instance)
{
    uint32_t scanned;

    for (scanned = 0; scanned < instance->capacity.commands; ++scanned) {
        uint32_t index = (instance->command_cursor + scanned) %
                         instance->capacity.commands;
        struct fwlab_c31_command_slot *slot = &instance->commands[index];
        struct fwlab_c31_provider *provider;

        if (!slot->in_use) {
            continue;
        }
        instance->command_cursor = (index + 1) % instance->capacity.commands;
        switch (slot->state) {
        case FWLAB_C31_CMD_ACCEPTED:
            if (slot->descriptor.provider_kind == FWLAB_C31_PROVIDER_NONE) {
                make_completion(instance, slot,
                                FWLAB_C31_COMPLETION_SUCCESS, NULL);
            } else {
                set_state(instance, slot, FWLAB_C31_CMD_DISPATCHED, 0);
            }
            return 1;
        case FWLAB_C31_CMD_DISPATCHED:
        case FWLAB_C31_CMD_HELD:
            (void)submit_provider_operation(instance, slot);
            return 1;
        case FWLAB_C31_CMD_CANCEL_PENDING:
        case FWLAB_C31_CMD_RESET_DRAIN:
            if (!slot->provider_owned) {
                if (slot->state == FWLAB_C31_CMD_RESET_DRAIN) {
                    retire_command(instance, slot);
                    return 1;
                }
                continue;
            }
            if (slot->cancel_sent) {
                continue;
            }
            provider = provider_for(instance, slot->descriptor.provider_kind);
            if (provider == NULL || provider->ops == NULL ||
                provider->ops->cancel(provider->context, &slot->operation) !=
                    FWLAB_C31_API_OK) {
                (void)instance_fault(instance, &slot->handle,
                                     FWLAB_C31_REASON_INVARIANT);
                return 1;
            }
            slot->cancel_sent = true;
            return 1;
        default:
            break;
        }
    }
    return 0;
}

static int any_commands(const struct fwlab_c31 *instance)
{
    uint32_t index;

    for (index = 0; index < instance->capacity.commands; ++index) {
        if (instance->commands[index].in_use) {
            return 1;
        }
    }
    return 0;
}

static int any_abort_tickets(const struct fwlab_c31 *instance)
{
    uint32_t index;

    for (index = 0; index < instance->capacity.abort_tickets; ++index) {
        if (instance->aborts[index].used) {
            return 1;
        }
    }
    return 0;
}

static enum fwlab_c31_api_result providers_quiescent(
    struct fwlab_c31 *instance,
    bool *quiescent
)
{
    uint8_t kind;

    *quiescent = true;
    for (kind = FWLAB_C31_PROVIDER_DMA;
         kind <= FWLAB_C31_PROVIDER_NFC; ++kind) {
        const struct fwlab_c31_provider *provider =
            provider_for_const(instance, kind);
        bool one_quiescent = true;

        if (provider != NULL && provider->ops != NULL &&
            (provider->ops->quiescent(provider->context,
                                      instance->instance_nonce,
                                      instance->drain_epoch,
                                      &one_quiescent) != FWLAB_C31_API_OK)) {
            return instance_fault(instance, NULL,
                                  FWLAB_C31_REASON_INVARIANT);
        }
        if (!one_quiescent) {
            *quiescent = false;
        }
    }
    return FWLAB_C31_API_OK;
}

static void check_drain_complete(struct fwlab_c31 *instance)
{
    bool quiescent = false;
    uint8_t next_phase;

    if (instance->phase != FWLAB_C31_INSTANCE_RESET_DRAIN &&
        instance->phase != FWLAB_C31_INSTANCE_TEARDOWN_DRAIN) {
        return;
    }
    if (any_commands(instance) || any_abort_tickets(instance)) {
        return;
    }
    if (providers_quiescent(instance, &quiescent) != FWLAB_C31_API_OK ||
        !quiescent) {
        return;
    }
    next_phase = instance->phase == FWLAB_C31_INSTANCE_RESET_DRAIN ?
                 FWLAB_C31_INSTANCE_RESET_ACK :
                 FWLAB_C31_INSTANCE_TEARDOWN_ACK;
    trace_push(instance,
               next_phase == FWLAB_C31_INSTANCE_RESET_ACK ?
                   FWLAB_C31_TRACE_RESET : FWLAB_C31_TRACE_TEARDOWN,
               NULL, instance->phase, next_phase, 0);
    instance->phase = next_phase;
}

size_t fwlab_c31_arena_alignment(void)
{
    return alignof(max_align_t);
}

size_t fwlab_c31_arena_size(const struct fwlab_c31_capacity *capacity)
{
    size_t offset = 0;

    if (!capacity_valid(capacity)) {
        return 0;
    }
    if (!add_array(&offset, 1, sizeof(struct fwlab_c31),
                   alignof(struct fwlab_c31)) ||
        !add_array(&offset, capacity->commands,
                   sizeof(struct fwlab_c31_command_slot),
                   alignof(struct fwlab_c31_command_slot)) ||
        !add_array(&offset, capacity->abort_tickets,
                   sizeof(struct fwlab_c31_abort_slot),
                   alignof(struct fwlab_c31_abort_slot)) ||
        !add_array(&offset, capacity->event_batch,
                   sizeof(struct fwlab_c31_provider_event),
                   alignof(struct fwlab_c31_provider_event)) ||
        !add_array(&offset, capacity->trace_entries,
                   sizeof(struct fwlab_c31_trace_entry),
                   alignof(struct fwlab_c31_trace_entry)) ||
        !add_array(&offset, capacity->scratch_bytes, sizeof(uint8_t),
                   alignof(max_align_t))) {
        return 0;
    }
    return align_up(offset, alignof(max_align_t));
}

static void *layout_array(
    uint8_t *base,
    size_t *offset,
    size_t count,
    size_t element_size,
    size_t alignment
)
{
    void *result;

    *offset = align_up(*offset, alignment);
    result = base + *offset;
    *offset += count * element_size;
    return result;
}

enum fwlab_c31_api_result fwlab_c31_init(
    void *arena,
    size_t arena_size,
    const struct fwlab_c31_capacity *capacity,
    uint64_t instance_nonce,
    const struct fwlab_c31_provider_set *providers,
    struct fwlab_c31 **instance_out
)
{
    struct fwlab_c31 *instance;
    uint8_t *base = arena;
    size_t required = fwlab_c31_arena_size(capacity);
    size_t offset = 0;
    uint32_t index;

    if (required == 0 || arena == NULL || providers == NULL ||
        instance_out == NULL || instance_nonce == 0 || arena_size < required ||
        ((uintptr_t)arena % fwlab_c31_arena_alignment()) != 0 ||
        !provider_valid(&providers->dma) || !provider_valid(&providers->nfc)) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    memset(arena, 0, required);
    instance = layout_array(base, &offset, 1, sizeof(*instance),
                            alignof(struct fwlab_c31));
    instance->commands = layout_array(
        base, &offset, capacity->commands,
        sizeof(*instance->commands), alignof(struct fwlab_c31_command_slot));
    instance->aborts = layout_array(
        base, &offset, capacity->abort_tickets,
        sizeof(*instance->aborts), alignof(struct fwlab_c31_abort_slot));
    instance->events = layout_array(
        base, &offset, capacity->event_batch,
        sizeof(*instance->events), alignof(struct fwlab_c31_provider_event));
    instance->trace = layout_array(
        base, &offset, capacity->trace_entries,
        sizeof(*instance->trace), alignof(struct fwlab_c31_trace_entry));
    instance->scratch = layout_array(
        base, &offset, capacity->scratch_bytes, sizeof(uint8_t),
        alignof(max_align_t));
    instance->magic = FWLAB_C31_MAGIC;
    instance->capacity = *capacity;
    instance->providers = *providers;
    instance->instance_nonce = instance_nonce;
    instance->controller_epoch = 1;
    instance->phase = FWLAB_C31_INSTANCE_READY;
    for (index = 0; index < capacity->commands; ++index) {
        instance->commands[index].state = FWLAB_C31_CMD_FREE;
        instance->commands[index].abort_index = FWLAB_C31_NO_ABORT;
    }
    trace_push(instance, FWLAB_C31_TRACE_INIT, NULL, 0,
               FWLAB_C31_INSTANCE_READY, 0);
    *instance_out = instance;
    return FWLAB_C31_API_OK;
}

enum fwlab_c31_instance_phase fwlab_c31_phase(
    const struct fwlab_c31 *instance
)
{
    if (!instance_valid(instance)) {
        return FWLAB_C31_INSTANCE_DEAD;
    }
    return (enum fwlab_c31_instance_phase)instance->phase;
}

enum fwlab_c31_api_result fwlab_c31_submit(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_command_descriptor *descriptor,
    struct fwlab_c31_command_handle *command
)
{
    struct fwlab_c31_command_slot *slot = NULL;
    enum fwlab_c31_api_result result;
    uint32_t index;
    int exhausted = 0;

    if (!instance_valid(instance) || command == NULL) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    result = descriptor_valid(instance, descriptor);
    if (result != FWLAB_C31_API_OK) {
        return result;
    }
    if (instance->phase != FWLAB_C31_INSTANCE_READY) {
        return FWLAB_C31_API_WRONG_STATE;
    }
    if (instance->next_command_uid >= instance->capacity.command_uid_limit) {
        (void)instance_fault(instance, NULL,
                             FWLAB_C31_API_COUNTER_EXHAUSTED);
        return FWLAB_C31_API_COUNTER_EXHAUSTED;
    }
    for (index = 0; index < instance->capacity.commands; ++index) {
        struct fwlab_c31_command_slot *candidate = &instance->commands[index];

        if (candidate->in_use) {
            continue;
        }
        if (candidate->slot_generation >=
                instance->capacity.slot_generation_limit ||
            (descriptor->provider_kind != FWLAB_C31_PROVIDER_NONE &&
             candidate->operation_generation >=
                 instance->capacity.operation_generation_limit)) {
            exhausted = 1;
            continue;
        }
        slot = candidate;
        break;
    }
    if (slot == NULL) {
        if (exhausted) {
            (void)instance_fault(instance, NULL,
                                 FWLAB_C31_API_COUNTER_EXHAUSTED);
            return FWLAB_C31_API_COUNTER_EXHAUSTED;
        }
        return FWLAB_C31_API_NO_CAPACITY;
    }

    ++slot->slot_generation;
    if (descriptor->provider_kind != FWLAB_C31_PROVIDER_NONE) {
        ++slot->operation_generation;
    }
    slot->in_use = true;
    slot->provider_owned = false;
    slot->cancel_sent = false;
    slot->completion_valid = false;
    slot->lease_active = false;
    slot->last_terminal_valid = false;
    slot->abort_index = FWLAB_C31_NO_ABORT;
    slot->state = FWLAB_C31_CMD_ACCEPTED;
    slot->descriptor = *descriptor;
    memset(&slot->completion, 0, sizeof(slot->completion));
    slot->handle.instance_nonce = instance->instance_nonce;
    slot->handle.command_uid = ++instance->next_command_uid;
    slot->handle.controller_epoch = instance->controller_epoch;
    slot->handle.slot = (uint16_t)(slot - instance->commands);
    slot->handle.slot_generation = (uint16_t)slot->slot_generation;
    memset(&slot->operation, 0, sizeof(slot->operation));
    slot->operation.command = slot->handle;
    slot->operation.cookie = descriptor->provider_request.word[0] ^
                             descriptor->provider_request.word[1] ^
                             descriptor->trace_cookie;
    slot->operation.operation_generation = slot->operation_generation;
    *command = slot->handle;
    trace_push(instance, FWLAB_C31_TRACE_SUBMIT, &slot->handle,
               FWLAB_C31_CMD_FREE, FWLAB_C31_CMD_ACCEPTED,
               descriptor->provider_kind);
    return FWLAB_C31_API_OK;
}

enum fwlab_c31_api_result fwlab_c31_step(
    struct fwlab_c31 *instance,
    uint32_t budget,
    struct fwlab_c31_step_result *step_result
)
{
    struct fwlab_c31_step_result local;

    if (!instance_valid(instance) || step_result == NULL || budget == 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (instance->phase == FWLAB_C31_INSTANCE_DEAD ||
        instance->phase == FWLAB_C31_INSTANCE_RESET_ACK ||
        instance->phase == FWLAB_C31_INSTANCE_TEARDOWN_ACK ||
        instance->phase == FWLAB_C31_INSTANCE_FAULTED) {
        return FWLAB_C31_API_WRONG_STATE;
    }
    memset(&local, 0, sizeof(local));
    while (local.units_used < budget) {
        uint8_t provider_kind = instance->provider_cursor == 0 ?
                                FWLAB_C31_PROVIDER_DMA :
                                FWLAB_C31_PROVIDER_NFC;
        uint32_t events = 0;
        enum fwlab_c31_api_result result;

        instance->provider_cursor ^= 1u;
        result = poll_provider(instance, provider_kind, &events);
        ++local.units_used;
        local.provider_events += events;
        if (result == FWLAB_C31_API_INVARIANT_FAILURE ||
            instance->phase == FWLAB_C31_INSTANCE_FAULTED) {
            local.phase = instance->phase;
            *step_result = local;
            return FWLAB_C31_API_INVARIANT_FAILURE;
        }
        if (events == 0 && progress_command(instance)) {
            ++local.transitions;
        }
        if (instance->phase == FWLAB_C31_INSTANCE_FAULTED) {
            local.phase = instance->phase;
            *step_result = local;
            return FWLAB_C31_API_INVARIANT_FAILURE;
        }
        check_drain_complete(instance);
        if (instance->phase == FWLAB_C31_INSTANCE_FAULTED) {
            local.phase = instance->phase;
            *step_result = local;
            return FWLAB_C31_API_INVARIANT_FAILURE;
        }
        if (instance->phase == FWLAB_C31_INSTANCE_RESET_ACK ||
            instance->phase == FWLAB_C31_INSTANCE_TEARDOWN_ACK) {
            break;
        }
    }
    local.phase = instance->phase;
    *step_result = local;
    return FWLAB_C31_API_OK;
}

enum fwlab_c31_api_result fwlab_c31_command_state(
    const struct fwlab_c31 *instance,
    const struct fwlab_c31_command_handle *command,
    enum fwlab_c31_lifecycle_state *state
)
{
    const struct fwlab_c31_command_slot *slot;
    enum fwlab_c31_api_result result;

    if (!instance_valid(instance) || state == NULL) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    slot = find_command_const(instance, command, &result);
    if (slot == NULL) {
        return result;
    }
    *state = (enum fwlab_c31_lifecycle_state)slot->state;
    return FWLAB_C31_API_OK;
}

enum fwlab_c31_api_result fwlab_c31_completion_acquire(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_command_handle *command,
    struct fwlab_c31_completion_lease *lease,
    struct fwlab_c31_completion_intent *intent
)
{
    struct fwlab_c31_command_slot *slot;
    enum fwlab_c31_api_result result;

    if (!instance_valid(instance) || lease == NULL || intent == NULL) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (instance->phase != FWLAB_C31_INSTANCE_READY) {
        return FWLAB_C31_API_WRONG_STATE;
    }
    slot = find_command(instance, command, &result);
    if (slot == NULL) {
        return result;
    }
    if (slot->state != FWLAB_C31_CMD_COMPLETION_READY ||
        !slot->completion_valid || slot->lease_active) {
        return FWLAB_C31_API_WRONG_STATE;
    }
    if (slot->lease_generation >=
        instance->capacity.lease_generation_limit) {
        (void)instance_fault(instance, &slot->handle,
                             FWLAB_C31_API_COUNTER_EXHAUSTED);
        return FWLAB_C31_API_COUNTER_EXHAUSTED;
    }
    ++slot->lease_generation;
    memset(lease, 0, sizeof(*lease));
    lease->command = slot->handle;
    lease->lease_generation = slot->lease_generation;
    *intent = slot->completion;
    slot->lease_active = true;
    set_state(instance, slot, FWLAB_C31_CMD_COMPLETION_LEASED, 0);
    trace_push(instance, FWLAB_C31_TRACE_LEASE, &slot->handle,
               FWLAB_C31_CMD_COMPLETION_READY,
               FWLAB_C31_CMD_COMPLETION_LEASED,
               slot->lease_generation);
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result validate_lease(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_completion_lease *lease,
    struct fwlab_c31_command_slot **slot_out
)
{
    struct fwlab_c31_command_slot *slot;
    enum fwlab_c31_api_result result;

    if (lease == NULL || lease->reserved != 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    slot = find_command(instance, &lease->command, &result);
    if (slot == NULL) {
        return result;
    }
    if (slot->handle.controller_epoch != instance->controller_epoch) {
        return FWLAB_C31_API_STALE_TOKEN;
    }
    if (slot->state != FWLAB_C31_CMD_COMPLETION_LEASED ||
        !slot->lease_active ||
        lease->lease_generation != slot->lease_generation) {
        return FWLAB_C31_API_STALE_TOKEN;
    }
    *slot_out = slot;
    return FWLAB_C31_API_OK;
}

enum fwlab_c31_api_result fwlab_c31_completion_release(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_completion_lease *lease
)
{
    struct fwlab_c31_command_slot *slot;
    enum fwlab_c31_api_result result;

    if (!instance_valid(instance)) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (instance->phase != FWLAB_C31_INSTANCE_READY) {
        return FWLAB_C31_API_WRONG_STATE;
    }
    result = validate_lease(instance, lease, &slot);
    if (result != FWLAB_C31_API_OK) {
        return result;
    }
    slot->lease_active = false;
    set_state(instance, slot, FWLAB_C31_CMD_COMPLETION_READY, 0);
    return FWLAB_C31_API_OK;
}

enum fwlab_c31_api_result fwlab_c31_completion_consume(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_completion_lease *lease
)
{
    struct fwlab_c31_command_slot *slot;
    enum fwlab_c31_api_result result;

    if (!instance_valid(instance)) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (instance->phase != FWLAB_C31_INSTANCE_READY) {
        return FWLAB_C31_API_WRONG_STATE;
    }
    result = validate_lease(instance, lease, &slot);
    if (result != FWLAB_C31_API_OK) {
        return result;
    }
    slot->lease_active = false;
    retire_command(instance, slot);
    return FWLAB_C31_API_OK;
}

static struct fwlab_c31_abort_slot *allocate_abort(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_command_handle *command,
    enum fwlab_c31_api_result *result
)
{
    uint32_t index;
    int exhausted = 0;

    for (index = 0; index < instance->capacity.abort_tickets; ++index) {
        struct fwlab_c31_abort_slot *abort = &instance->aborts[index];

        if (abort->used) {
            continue;
        }
        if (abort->generation >=
            instance->capacity.ticket_generation_limit) {
            exhausted = 1;
            continue;
        }
        ++abort->generation;
        abort->used = true;
        abort->outcome = FWLAB_C31_ABORT_PENDING;
        memset(&abort->ticket, 0, sizeof(abort->ticket));
        abort->ticket.command = *command;
        abort->ticket.ticket_generation = abort->generation;
        *result = FWLAB_C31_API_OK;
        return abort;
    }
    *result = exhausted ? FWLAB_C31_API_COUNTER_EXHAUSTED :
                          FWLAB_C31_API_NO_CAPACITY;
    return NULL;
}

enum fwlab_c31_api_result fwlab_c31_abort_request(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_command_handle *command,
    struct fwlab_c31_abort_ticket *ticket,
    enum fwlab_c31_abort_outcome *outcome
)
{
    struct fwlab_c31_command_slot *slot;
    struct fwlab_c31_abort_slot *abort;
    enum fwlab_c31_api_result result;
    struct fwlab_c31_fault fault;

    if (!instance_valid(instance) || ticket == NULL || outcome == NULL) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (instance->phase != FWLAB_C31_INSTANCE_READY) {
        return FWLAB_C31_API_WRONG_STATE;
    }
    slot = find_command(instance, command, &result);
    if (slot == NULL) {
        return result;
    }
    abort = abort_for_command(instance, command);
    if (abort != NULL) {
        *ticket = abort->ticket;
        *outcome = (enum fwlab_c31_abort_outcome)abort->outcome;
        return FWLAB_C31_API_OK;
    }
    abort = allocate_abort(instance, command, &result);
    if (abort == NULL) {
        if (result == FWLAB_C31_API_COUNTER_EXHAUSTED) {
            (void)instance_fault(instance, command,
                                 FWLAB_C31_API_COUNTER_EXHAUSTED);
        }
        return result;
    }
    slot->abort_index = (uint16_t)(abort - instance->aborts);
    memset(&fault, 0, sizeof(fault));
    fault.domain = FWLAB_C31_FAULT_CORE;
    fault.retry_class = FWLAB_C31_RETRY_NEVER;
    fault.effect_class = FWLAB_C31_EFFECT_NONE;
    fault.reason = FWLAB_C31_REASON_CANCELLED;
    switch (slot->state) {
    case FWLAB_C31_CMD_ACCEPTED:
    case FWLAB_C31_CMD_DISPATCHED:
    case FWLAB_C31_CMD_HELD:
        abort->outcome = FWLAB_C31_ABORT_TERMINAL;
        make_completion(instance, slot, FWLAB_C31_COMPLETION_ABORTED, &fault);
        break;
    case FWLAB_C31_CMD_RUNNING:
        set_state(instance, slot, FWLAB_C31_CMD_CANCEL_PENDING, 0);
        break;
    case FWLAB_C31_CMD_COMPLETION_READY:
    case FWLAB_C31_CMD_COMPLETION_LEASED:
        abort->outcome = FWLAB_C31_ABORT_TOO_LATE;
        break;
    default:
        abort->used = false;
        slot->abort_index = FWLAB_C31_NO_ABORT;
        return FWLAB_C31_API_WRONG_STATE;
    }
    trace_push(instance, FWLAB_C31_TRACE_ABORT, command,
               FWLAB_C31_ABORT_PENDING, abort->outcome, 0);
    *ticket = abort->ticket;
    *outcome = (enum fwlab_c31_abort_outcome)abort->outcome;
    return FWLAB_C31_API_OK;
}

static const struct fwlab_c31_abort_slot *find_abort_const(
    const struct fwlab_c31 *instance,
    const struct fwlab_c31_abort_ticket *ticket
)
{
    uint32_t index;

    if (ticket == NULL || ticket->reserved != 0) {
        return NULL;
    }
    for (index = 0; index < instance->capacity.abort_tickets; ++index) {
        const struct fwlab_c31_abort_slot *abort = &instance->aborts[index];

        if (abort->used && abort->generation == ticket->ticket_generation &&
            command_equal(&abort->ticket.command, &ticket->command)) {
            return abort;
        }
    }
    return NULL;
}

enum fwlab_c31_api_result fwlab_c31_abort_query(
    const struct fwlab_c31 *instance,
    const struct fwlab_c31_abort_ticket *ticket,
    enum fwlab_c31_abort_outcome *outcome
)
{
    const struct fwlab_c31_abort_slot *abort;

    if (!instance_valid(instance) || outcome == NULL) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    abort = find_abort_const(instance, ticket);
    if (abort == NULL) {
        return FWLAB_C31_API_STALE_TOKEN;
    }
    *outcome = (enum fwlab_c31_abort_outcome)abort->outcome;
    return FWLAB_C31_API_OK;
}

enum fwlab_c31_api_result fwlab_c31_abort_ack(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_abort_ticket *ticket
)
{
    uint32_t index;

    if (!instance_valid(instance) || ticket == NULL || ticket->reserved != 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (instance->phase == FWLAB_C31_INSTANCE_DEAD) {
        return FWLAB_C31_API_WRONG_STATE;
    }
    for (index = 0; index < instance->capacity.abort_tickets; ++index) {
        struct fwlab_c31_abort_slot *abort = &instance->aborts[index];

        if (!abort->used ||
            abort->generation != ticket->ticket_generation ||
            !command_equal(&abort->ticket.command, &ticket->command)) {
            continue;
        }
        if (abort->outcome == FWLAB_C31_ABORT_PENDING) {
            return FWLAB_C31_API_WRONG_STATE;
        }
        abort->used = false;
        return FWLAB_C31_API_OK;
    }
    return FWLAB_C31_API_STALE_TOKEN;
}

static enum fwlab_c31_api_result provider_reset_begin(
    struct fwlab_c31 *instance,
    uint32_t old_epoch
)
{
    uint8_t kind;

    for (kind = FWLAB_C31_PROVIDER_DMA;
         kind <= FWLAB_C31_PROVIDER_NFC; ++kind) {
        struct fwlab_c31_provider *provider = provider_for(instance, kind);

        if (provider != NULL && provider->ops != NULL &&
            provider->ops->reset_begin(provider->context,
                                       instance->instance_nonce,
                                       old_epoch) != FWLAB_C31_API_OK) {
            return instance_fault(instance, NULL,
                                  FWLAB_C31_REASON_INVARIANT);
        }
    }
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result begin_drain(
    struct fwlab_c31 *instance,
    int teardown
)
{
    uint32_t index;
    uint32_t old_epoch = instance->controller_epoch;
    uint32_t old_phase = instance->phase;
    uint8_t target_phase = teardown ?
                           FWLAB_C31_INSTANCE_TEARDOWN_DRAIN :
                           FWLAB_C31_INSTANCE_RESET_DRAIN;

    if ((!teardown && instance->phase != FWLAB_C31_INSTANCE_READY &&
         instance->phase != FWLAB_C31_INSTANCE_FAULTED) ||
        (teardown && instance->phase != FWLAB_C31_INSTANCE_READY &&
         instance->phase != FWLAB_C31_INSTANCE_FAULTED)) {
        return FWLAB_C31_API_WRONG_STATE;
    }
    if (!teardown) {
        if (instance->controller_epoch >=
            instance->capacity.controller_epoch_limit) {
            (void)instance_fault(instance, NULL,
                                 FWLAB_C31_API_COUNTER_EXHAUSTED);
            return FWLAB_C31_API_COUNTER_EXHAUSTED;
        }
        ++instance->controller_epoch;
    }
    instance->drain_epoch = old_epoch;
    instance->phase = target_phase;
    trace_push(instance,
               teardown ? FWLAB_C31_TRACE_TEARDOWN : FWLAB_C31_TRACE_RESET,
               NULL, old_phase, target_phase, old_epoch);
    if (provider_reset_begin(instance, old_epoch) != FWLAB_C31_API_OK) {
        return FWLAB_C31_API_INVARIANT_FAILURE;
    }
    for (index = 0; index < instance->capacity.abort_tickets; ++index) {
        struct fwlab_c31_abort_slot *abort = &instance->aborts[index];

        if (abort->used && abort->outcome == FWLAB_C31_ABORT_PENDING) {
            abort->outcome = FWLAB_C31_ABORT_RESET_SUPERSEDED;
        }
    }
    for (index = 0; index < instance->capacity.commands; ++index) {
        struct fwlab_c31_command_slot *slot = &instance->commands[index];

        if (!slot->in_use) {
            continue;
        }
        slot->lease_active = false;
        slot->completion_valid = false;
        if (slot->provider_owned) {
            slot->cancel_sent = false;
            set_state(instance, slot, FWLAB_C31_CMD_RESET_DRAIN, 0);
        } else {
            retire_command(instance, slot);
        }
    }
    check_drain_complete(instance);
    return FWLAB_C31_API_OK;
}

enum fwlab_c31_api_result fwlab_c31_reset_begin(struct fwlab_c31 *instance)
{
    if (!instance_valid(instance)) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    return begin_drain(instance, 0);
}

enum fwlab_c31_api_result fwlab_c31_reset_ack(struct fwlab_c31 *instance)
{
    if (!instance_valid(instance)) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (instance->phase != FWLAB_C31_INSTANCE_RESET_ACK) {
        return FWLAB_C31_API_WRONG_STATE;
    }
    trace_push(instance, FWLAB_C31_TRACE_RESET, NULL,
               FWLAB_C31_INSTANCE_RESET_ACK,
               FWLAB_C31_INSTANCE_READY, instance->controller_epoch);
    instance->phase = FWLAB_C31_INSTANCE_READY;
    return FWLAB_C31_API_OK;
}

enum fwlab_c31_api_result fwlab_c31_teardown_begin(
    struct fwlab_c31 *instance
)
{
    if (!instance_valid(instance)) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    return begin_drain(instance, 1);
}

enum fwlab_c31_api_result fwlab_c31_teardown_ack(struct fwlab_c31 *instance)
{
    if (!instance_valid(instance)) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (instance->phase != FWLAB_C31_INSTANCE_TEARDOWN_ACK) {
        return FWLAB_C31_API_WRONG_STATE;
    }
    trace_push(instance, FWLAB_C31_TRACE_TEARDOWN, NULL,
               FWLAB_C31_INSTANCE_TEARDOWN_ACK,
               FWLAB_C31_INSTANCE_DEAD, 0);
    instance->phase = FWLAB_C31_INSTANCE_DEAD;
    return FWLAB_C31_API_OK;
}

uint32_t fwlab_c31_trace_count(const struct fwlab_c31 *instance)
{
    if (!instance_valid(instance)) {
        return 0;
    }
    return instance->trace_count;
}

enum fwlab_c31_api_result fwlab_c31_trace_read(
    const struct fwlab_c31 *instance,
    uint32_t ordinal,
    struct fwlab_c31_trace_entry *entry
)
{
    uint32_t index;

    if (!instance_valid(instance) || entry == NULL) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    if (ordinal >= instance->trace_count) {
        return FWLAB_C31_API_NOT_FOUND;
    }
    index = (instance->trace_head + ordinal) %
            instance->capacity.trace_entries;
    *entry = instance->trace[index];
    return FWLAB_C31_API_OK;
}
