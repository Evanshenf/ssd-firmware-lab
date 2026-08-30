/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_internal.h"

#include <string.h>

static int handle_valid(const struct fwlab_nvme_command_handle *handle)
{
    return handle != NULL && handle->instance_nonce != 0 &&
           handle->command_uid != 0 && handle->controller_epoch != 0 &&
           handle->generation != 0;
}

static int origin_valid(const struct fwlab_nvme_origin_token *origin)
{
    return origin != NULL && origin->word[0] != 0 && origin->word[1] != 0;
}

int c42_bytes_zero(const void *value, size_t size)
{
    const uint8_t *bytes = value;
    size_t index;

    if (value == NULL) {
        return 0;
    }
    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

int c42_handle_equal(
    const struct fwlab_nvme_command_handle *left,
    const struct fwlab_nvme_command_handle *right)
{
    return left != NULL && right != NULL &&
           left->instance_nonce == right->instance_nonce &&
           left->command_uid == right->command_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->generation == right->generation;
}

int c42_origin_equal(
    const struct fwlab_nvme_origin_token *left,
    const struct fwlab_nvme_origin_token *right)
{
    return left != NULL && right != NULL &&
           left->word[0] == right->word[0] &&
           left->word[1] == right->word[1];
}

int c42_operation_token_equal(
    const struct c42_operation_token *left,
    const struct c42_operation_token *right)
{
    return left != NULL && right != NULL &&
           left->instance_nonce == right->instance_nonce &&
           left->uid == right->uid && left->generation == right->generation &&
           left->kind == right->kind && left->reserved == right->reserved;
}

int fwlab_hif_prepared_token_valid(
    const struct fwlab_hif_prepared_token *token)
{
    return token != NULL && handle_valid(&token->handle) &&
           origin_valid(&token->origin) && token->reservation_uid != 0 &&
           token->generation != 0 && token->reserved == 0;
}

int fwlab_hif_command_ticket_valid(
    const struct fwlab_hif_command_ticket *ticket)
{
    return ticket != NULL && handle_valid(&ticket->handle) &&
           origin_valid(&ticket->origin) && ticket->ticket_uid != 0 &&
           ticket->generation != 0 && ticket->reserved == 0;
}

int fwlab_hif_completion_lease_valid(
    const struct fwlab_hif_completion_lease *lease)
{
    return lease != NULL && fwlab_hif_command_ticket_valid(&lease->ticket) &&
           lease->lease_uid != 0 && lease->generation != 0 &&
           lease->reserved == 0;
}

int fwlab_hif_consume_token_valid(
    const struct fwlab_hif_consume_token *token)
{
    return token != NULL &&
           fwlab_hif_completion_lease_valid(&token->lease) &&
           token->publication_uid != 0 && token->consume_uid != 0 &&
           token->generation != 0 && token->reserved == 0;
}

int fwlab_hif_command_port_valid(const struct fwlab_hif_command_port *port)
{
    const struct fwlab_hif_command_port_ops *ops;

    if (port == NULL || port->ops == NULL || port->context == NULL) {
        return 0;
    }
    ops = port->ops;
    return ops->version == FWLAB_HIF_COMMAND_PORT_VERSION &&
           ops->size == sizeof(*ops) && ops->reserved == 0 &&
           ops->prepare_start != NULL && ops->prepare_query != NULL &&
           ops->prepare_abort != NULL && ops->prepare_abort_query != NULL &&
           ops->admit_start != NULL && ops->admit_query != NULL &&
           ops->poll != NULL && ops->completion_acquire != NULL &&
           ops->completion_release_start != NULL &&
           ops->completion_release_query != NULL &&
           ops->consume_prepare != NULL && ops->consume_abort != NULL &&
           ops->consume_abort_query != NULL && ops->consume_commit != NULL &&
           ops->consume_query != NULL && ops->consume_retire != NULL &&
           ops->reset_begin != NULL && ops->reset_quiescent != NULL &&
           ops->teardown_begin != NULL && ops->teardown_quiescent != NULL;
}

int c42_queue_memory_cap_valid(
    const struct c42_queue_memory_cap *capability)
{
    return capability != NULL && capability->instance_nonce != 0 &&
           capability->owner_epoch != 0 && capability->memory_uid != 0 &&
           capability->controller_epoch != 0 &&
           capability->ring_generation != 0 &&
           capability->mapping_generation != 0 &&
           capability->exact_bytes != 0 &&
           (capability->role == C42_MEMORY_SQ_READ ||
            capability->role == C42_MEMORY_CQ_PUBLISH) &&
           capability->reserved == 0;
}

int c42_memory_token_valid(const struct c42_memory_token *token)
{
    return token != NULL && token->instance_nonce != 0 && token->uid != 0 &&
           token->generation != 0 && token->kind != 0 &&
           token->reserved == 0;
}

int c42_memory_port_valid(const struct c42_memory_port *port)
{
    const struct c42_memory_ops *ops;

    if (port == NULL || port->ops == NULL || port->context == NULL) {
        return 0;
    }
    ops = port->ops;
    return ops->version == C42_MEMORY_PORT_VERSION &&
           ops->size == sizeof(*ops) && ops->reserved == 0 &&
           ops->validate != NULL && ops->capture != NULL &&
           ops->scrub_start != NULL && ops->scrub_query != NULL &&
           ops->scrub_abort != NULL && ops->scrub_retire_start != NULL &&
           ops->scrub_retire_query != NULL && ops->body_start != NULL &&
           ops->body_query != NULL && ops->marker_start != NULL &&
           ops->marker_query != NULL && ops->reset_begin != NULL &&
           ops->reset_quiescent != NULL && ops->teardown_begin != NULL &&
           ops->teardown_quiescent != NULL;
}

int c42_controller_valid(const struct c42_controller *controller)
{
    return controller != NULL && controller->magic == C42_INTERNAL_MAGIC &&
           controller->config.instance_nonce != 0 &&
           controller->phase <= C42_CONTROLLER_DEAD;
}

enum c42_result c42_counter_take(struct c42_counter *counter, uint64_t *value)
{
    if (counter == NULL || value == NULL || counter->next == 0 ||
        counter->next > counter->maximum) {
        return C42_COUNTER_EXHAUSTED;
    }
    *value = counter->next;
    if (counter->next == UINT64_MAX) {
        counter->next = 0;
    } else {
        counter->next++;
    }
    return C42_OK;
}

enum c42_result c42_generation_take(uint32_t *counter, uint32_t *value)
{
    if (counter == NULL || value == NULL || *counter == 0 ||
        *counter == UINT32_MAX) {
        return C42_COUNTER_EXHAUSTED;
    }
    *value = *counter;
    (*counter)++;
    return C42_OK;
}

int c42_queue_index(uint16_t queue_id, uint16_t *index)
{
    if (index == NULL || queue_id >= C42_MAX_QUEUE_PAIRS) {
        return 0;
    }
    *index = queue_id;
    return 1;
}

void c42_fault_controller(struct c42_controller *controller, uint32_t cause)
{
    if (!c42_controller_valid(controller) ||
        controller->phase == C42_CONTROLLER_TEARING_DOWN ||
        controller->phase == C42_CONTROLLER_DEAD) {
        return;
    }
    controller->fault_cause = cause;
    controller->phase = C42_CONTROLLER_FAULTED_RESET_REQUIRED;
    controller->admission_paused = 1;
}

void c42_fault_sq(
    struct c42_controller *controller,
    uint16_t sq_index,
    uint32_t cause)
{
    if (!c42_controller_valid(controller) || sq_index >= C42_QUEUE_SLOTS) {
        return;
    }
    controller->sq[sq_index].life = C42_QUEUE_FAULTED_RESET_REQUIRED;
    c42_fault_controller(controller, cause);
}

static int command_is_active(const struct c42_command_record *command)
{
    return (command->state >= C42_COMMAND_HIF_COMMITTED &&
            command->state <= C42_COMMAND_RELEASE_RECONCILE) ||
           command->state == C42_COMMAND_CONSUME_POISON_HOLD;
}

struct c42_command_record *c42_find_active(
    struct c42_controller *controller,
    uint16_t sq_index,
    uint32_t sq_generation,
    uint16_t command_id)
{
    uint32_t index;

    if (!c42_controller_valid(controller)) {
        return NULL;
    }
    for (index = 0; index < controller->config.command_capacity; ++index) {
        struct c42_command_record *command = &controller->commands[index];

        if (command_is_active(command) && command->sq_index == sq_index &&
            command->sq_ring_generation == sq_generation &&
            command->command_id == command_id) {
            return command;
        }
    }
    return NULL;
}

const struct c42_command_record *c42_find_active_const(
    const struct c42_controller *controller,
    uint16_t sq_index,
    uint32_t sq_generation,
    uint16_t command_id)
{
    uint32_t index;

    if (!c42_controller_valid(controller)) {
        return NULL;
    }
    for (index = 0; index < controller->config.command_capacity; ++index) {
        const struct c42_command_record *command = &controller->commands[index];

        if (command_is_active(command) && command->sq_index == sq_index &&
            command->sq_ring_generation == sq_generation &&
            command->command_id == command_id) {
            return command;
        }
    }
    return NULL;
}

uint32_t c42_count_active(const struct c42_controller *controller)
{
    uint32_t count = 0;
    uint32_t index;

    if (!c42_controller_valid(controller)) {
        return 0;
    }
    for (index = 0; index < controller->config.command_capacity; ++index) {
        if (command_is_active(&controller->commands[index])) {
            count++;
        }
    }
    return count;
}

uint32_t c42_count_targets(const struct c42_controller *controller)
{
    uint32_t count = 0;
    uint32_t index;

    if (!c42_controller_valid(controller)) {
        return 0;
    }
    for (index = 0; index < controller->config.target_capacity; ++index) {
        if (controller->targets[index].in_use != 0) {
            count++;
        }
    }
    return count;
}

uint32_t c42_count_publications(const struct c42_controller *controller)
{
    uint32_t count = 0;
    uint32_t index;

    if (!c42_controller_valid(controller)) {
        return 0;
    }
    for (index = 0; index < controller->config.command_capacity; ++index) {
        if (controller->publications[index].in_use != 0 &&
            controller->commands[index].state >= C42_COMMAND_LEASED) {
            count++;
        }
    }
    return count;
}

uint32_t c42_count_notifications(const struct c42_controller *controller)
{
    uint32_t count = 0;
    uint32_t index;

    if (!c42_controller_valid(controller)) {
        return 0;
    }
    for (index = 0; index < controller->config.command_capacity; ++index) {
        if (controller->notifications[index].in_use != 0 &&
            controller->notifications[index].state == C42_NOTIFY_READY) {
            count++;
        }
    }
    return count;
}

enum c42_result c42_target_prepare(
    struct c42_controller *controller,
    uint16_t submission_queue_id,
    uint32_t sq_ring_generation,
    uint16_t command_id,
    struct c42_target_ref *target)
{
    struct c42_command_record *command;
    struct c42_target_record *record = NULL;
    uint16_t sq_index;
    uint32_t index;
    uint64_t uid;
    uint32_t generation;

    if (!c42_controller_valid(controller) || target == NULL ||
        !c42_queue_index(submission_queue_id, &sq_index) ||
        controller->phase != C42_CONTROLLER_READY) {
        return C42_INVALID;
    }
    command = c42_find_active(
        controller, sq_index, sq_ring_generation, command_id
    );
    if (command == NULL) {
        return C42_NOT_FOUND;
    }
    if (command->state >= C42_COMMAND_MARKER_RECONCILE) {
        return C42_TOO_LATE;
    }
    for (index = 0; index < controller->config.target_capacity; ++index) {
        if (controller->targets[index].in_use == 0) {
            record = &controller->targets[index];
            break;
        }
    }
    if (record == NULL) {
        return C42_NO_CAPACITY;
    }
    if (controller->target_uid.next == 0 ||
        controller->target_uid.next > controller->target_uid.maximum ||
        controller->next_target_generation == 0 ||
        controller->next_target_generation == UINT32_MAX) {
        return C42_COUNTER_EXHAUSTED;
    }
    (void)c42_counter_take(&controller->target_uid, &uid);
    (void)c42_generation_take(&controller->next_target_generation, &generation);
    memset(record, 0, sizeof(*record));
    record->in_use = 1;
    record->value.token.instance_nonce = controller->config.instance_nonce;
    record->value.token.uid = uid;
    record->value.token.generation = generation;
    record->value.token.kind = 1;
    record->value.handle = command->command.handle;
    record->value.origin = command->origin;
    record->value.ticket = command->ticket;
    record->sq_index = sq_index;
    record->sq_ring_generation = sq_ring_generation;
    *target = record->value;
    return C42_OK;
}

enum c42_result c42_target_release(
    struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    uint32_t index;

    if (!c42_controller_valid(controller) || token == NULL ||
        token->reserved != 0 ||
        token->instance_nonce != controller->config.instance_nonce) {
        return C42_INVALID;
    }
    for (index = 0; index < controller->config.target_capacity; ++index) {
        struct c42_target_record *record = &controller->targets[index];

        if (record->in_use != 0 &&
            c42_operation_token_equal(&record->value.token, token)) {
            memset(record, 0, sizeof(*record));
            return C42_OK;
        }
    }
    return C42_STALE;
}

void c42_release_command_record(
    struct c42_controller *controller,
    uint16_t command_index)
{
    if (!c42_controller_valid(controller) ||
        command_index >= controller->config.command_capacity) {
        return;
    }
    memset(&controller->commands[command_index], 0,
           sizeof(controller->commands[command_index]));
    memset(&controller->publications[command_index], 0,
           sizeof(controller->publications[command_index]));
}
