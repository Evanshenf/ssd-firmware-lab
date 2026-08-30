/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_internal.h"

#include "fwlab/portable/nvme_codec.h"

#include <string.h>

#define C42_FAULT_READY_CONTRACT 20u
#define C42_FAULT_COMPLETION_CONTRACT 21u
#define C42_FAULT_PUBLICATION_MEMORY 22u
#define C42_FAULT_CONSUME_CONTRACT 23u
#define C42_FAULT_COUNTER_EXHAUSTED 24u

static int memory_token_equal(
    const struct c42_memory_token *left,
    const struct c42_memory_token *right)
{
    return left->instance_nonce == right->instance_nonce &&
           left->uid == right->uid && left->generation == right->generation &&
           left->kind == right->kind && left->reserved == right->reserved;
}

static enum c42_memory_result publication_effect(
    enum c42_memory_result call_result,
    const struct c42_memory_status *status,
    const struct c42_memory_token *token,
    int *valid)
{
    enum c42_memory_result effect = call_result;

    *valid = 1;
    if (call_result == C42_MEMORY_IN_PROGRESS) {
        return call_result;
    }
    if (call_result != C42_MEMORY_OK) {
        *valid = 0;
        return call_result;
    }
    if (!memory_token_equal(&status->token, token) ||
        status->result > C42_MEMORY_RETIRED || status->committed > 1 ||
        status->quiescent > 1 ||
        !c42_bytes_zero(status->reserved, sizeof(status->reserved))) {
        *valid = 0;
        return C42_MEMORY_POISONED;
    }
    effect = (enum c42_memory_result)status->result;
    return effect;
}

static int ticket_equal(
    const struct fwlab_hif_command_ticket *left,
    const struct fwlab_hif_command_ticket *right)
{
    return fwlab_hif_command_ticket_valid(left) &&
           fwlab_hif_command_ticket_valid(right) &&
           c42_handle_equal(&left->handle, &right->handle) &&
           c42_origin_equal(&left->origin, &right->origin) &&
           left->ticket_uid == right->ticket_uid &&
           left->generation == right->generation;
}

static int lease_matches(
    const struct fwlab_hif_completion_lease *lease,
    const struct c42_command_record *command)
{
    return fwlab_hif_completion_lease_valid(lease) &&
           ticket_equal(&lease->ticket, &command->ticket);
}

static int consume_matches(
    const struct fwlab_hif_consume_token *consume,
    const struct c42_command_record *command)
{
    return fwlab_hif_consume_token_valid(consume) &&
           lease_matches(&consume->lease, command) &&
           consume->publication_uid == command->publication_uid;
}

static int consume_equal(
    const struct fwlab_hif_consume_token *left,
    const struct fwlab_hif_consume_token *right)
{
    return fwlab_hif_consume_token_valid(left) &&
           fwlab_hif_consume_token_valid(right) &&
           ticket_equal(&left->lease.ticket, &right->lease.ticket) &&
           left->lease.lease_uid == right->lease.lease_uid &&
           left->lease.generation == right->lease.generation &&
           left->publication_uid == right->publication_uid &&
           left->consume_uid == right->consume_uid &&
           left->generation == right->generation;
}

static int intent_matches(
    const struct fwlab_nvme_completion_intent *intent,
    const struct c42_command_record *command)
{
    return fwlab_nvme_completion_valid(intent) &&
           c42_handle_equal(&intent->handle, &command->command.handle) &&
           c42_origin_equal(&intent->origin, &command->origin);
}

static int acquire_completion(
    struct c42_controller *controller,
    struct c42_command_record *command)
{
    struct c42_cq_record *cq = &controller->cq[command->cq_index];
    struct fwlab_nvme_completion_intent intent = {0};
    struct fwlab_hif_completion_lease lease = {0};
    enum fwlab_hif_command_port_result result;

    if (cq->life != C42_QUEUE_LIVE ||
        cq->unacked_count + cq->reserved_count >= cq->depth - 1u) {
        return 0;
    }
    if (cq->next_slot_generation == 0 ||
        cq->next_slot_generation == UINT32_MAX) {
        c42_fault_controller(controller, C42_FAULT_COUNTER_EXHAUSTED);
        return 1;
    }
    result = controller->providers.command.ops->completion_acquire(
        controller->providers.command.context, &command->ticket,
        &intent, &lease
    );
    if (result != FWLAB_HIF_PORT_OK || !lease_matches(&lease, command) ||
        !intent_matches(&intent, command)) {
        c42_fault_controller(controller, C42_FAULT_COMPLETION_CONTRACT);
        return 1;
    }
    command->intent = intent;
    command->lease = lease;
    command->state = C42_COMMAND_LEASED;
    return 1;
}

static int reserve_cq_slot(
    struct c42_controller *controller,
    uint16_t command_index,
    const struct fwlab_hif_consume_token *consume)
{
    struct c42_command_record *command = &controller->commands[command_index];
    struct c42_cq_record *cq = &controller->cq[command->cq_index];
    struct c42_sq_record *sq = &controller->sq[command->sq_index];
    struct c42_cq_slot *slot;
    struct c42_publication_record *publication =
        &controller->publications[command_index];
    struct c42_reconcile_record *reconcile =
        &controller->reconciles[command_index];
    struct c42_notification_record *notification =
        &controller->notifications[command_index];
    struct c41_publication_context context = {0};
    uint8_t wire[C42_CQE_BYTES] = {0};
    uint32_t slot_generation;

    if (cq->life != C42_QUEUE_LIVE ||
        (sq->life != C42_QUEUE_LIVE &&
         sq->life != C42_QUEUE_PREQUIESCE &&
         sq->life != C42_QUEUE_QUIESCING) ||
        sq->ring_generation != command->sq_ring_generation ||
        sq->associated_cq_id != cq->queue_id ||
        cq->device_tail >= cq->depth ||
        publication->in_use == 0 || reconcile->in_use == 0 ||
        notification->in_use == 0) {
        c42_fault_controller(controller, C42_FAULT_COMPLETION_CONTRACT);
        return 1;
    }
    if (cq->unacked_count + cq->reserved_count >= cq->depth - 1u ||
        cq->slots[cq->device_tail].state != C42_SLOT_FREE) {
        command->state = C42_COMMAND_CONSUME_PREPARE;
        return 1;
    }
    if (cq->next_slot_generation == 0 ||
        cq->next_slot_generation == UINT32_MAX) {
        c42_fault_controller(controller, C42_FAULT_COUNTER_EXHAUSTED);
        return 1;
    }
    command->sqhd_snapshot = sq->device_head;
    context.handle = command->command.handle;
    context.origin = command->origin;
    context.submission_queue_head = command->sqhd_snapshot;
    context.submission_queue_id = sq->queue_id;
    context.command_id = command->command_id;
    context.phase = cq->device_phase;
    if (c41_completion_publish(
            &command->intent, &context, wire, sizeof(wire)) != C41_WIRE_OK) {
        c42_fault_controller(controller, C42_FAULT_COMPLETION_CONTRACT);
        return 1;
    }
    slot_generation = cq->next_slot_generation;
    cq->next_slot_generation++;
    slot = &cq->slots[cq->device_tail];
    memset(slot, 0, sizeof(*slot));
    slot->origin = command->origin;
    slot->publication_uid = command->publication_uid;
    slot->notification_uid = command->notification_uid;
    slot->cq_ring_generation = cq->ring_generation;
    slot->source_sq_generation = command->sq_ring_generation;
    slot->slot_generation = slot_generation;
    slot->source_sq_id = controller->sq[command->sq_index].queue_id;
    slot->command_id = command->command_id;
    slot->submission_queue_head = command->sqhd_snapshot;
    slot->ordinal = cq->device_tail;
    slot->phase = cq->device_phase;
    slot->state = C42_SLOT_RESERVED;
    memcpy(command->cqe_bytes, wire, sizeof(command->cqe_bytes));
    memcpy(slot->wire, wire, sizeof(slot->wire));
    command->cq_slot = cq->device_tail;
    cq->reserved_count++;
    publication->body_token.instance_nonce = controller->config.instance_nonce;
    publication->body_token.uid = command->publication_uid;
    publication->body_token.generation = slot_generation;
    publication->body_token.kind = 1;
    publication->marker_token.instance_nonce = controller->config.instance_nonce;
    publication->marker_token.uid = command->publication_uid;
    publication->marker_token.generation = slot_generation;
    publication->marker_token.kind = 2;
    reconcile->lease = command->lease;
    reconcile->consume = *consume;
    reconcile->consume_known = 1;
    reconcile->state = C42_RECONCILE_PREPARED;
    notification->cq_ring_generation = cq->ring_generation;
    notification->slot_ordinal = cq->device_tail;
    command->state = C42_COMMAND_PUB_RESERVED;
    return 1;
}

static int prepare_consume(
    struct c42_controller *controller,
    uint16_t command_index)
{
    struct c42_command_record *command = &controller->commands[command_index];
    struct c42_reconcile_record *reconcile =
        &controller->reconciles[command_index];
    struct fwlab_hif_consume_token consume = {0};
    enum fwlab_hif_consume_state state = (enum fwlab_hif_consume_state)(
        FWLAB_HIF_CONSUME_POISONED + 1u
    );
    enum fwlab_hif_command_port_result result;

    result = controller->providers.command.ops->consume_prepare(
        controller->providers.command.context, &command->lease,
        command->publication_uid, &consume, &state
    );
    command->consume_started = 1;
    if (result == FWLAB_HIF_PORT_IN_PROGRESS ||
        (result == FWLAB_HIF_PORT_OK &&
         state == FWLAB_HIF_CONSUME_NOT_STARTED)) {
        command->state = C42_COMMAND_CONSUME_PREPARE;
        return 1;
    }
    if (result != FWLAB_HIF_PORT_OK ||
        state != FWLAB_HIF_CONSUME_PREPARED ||
        !consume_matches(&consume, command)) {
        if (consume_matches(&consume, command)) {
            reconcile->lease = command->lease;
            reconcile->consume = consume;
            reconcile->consume_known = 1;
        }
        command->state = C42_COMMAND_CONSUME_POISON_HOLD;
        c42_fault_controller(controller, C42_FAULT_CONSUME_CONTRACT);
        return 1;
    }
    if (reconcile->consume_known != 0 &&
        !consume_equal(&reconcile->consume, &consume)) {
        command->state = C42_COMMAND_CONSUME_POISON_HOLD;
        c42_fault_controller(controller, C42_FAULT_CONSUME_CONTRACT);
        return 1;
    }
    reconcile->lease = command->lease;
    reconcile->consume = consume;
    reconcile->consume_known = 1;
    command->state = C42_COMMAND_CONSUME_PREPARE;
    return reserve_cq_slot(
        controller, command_index, &reconcile->consume
    );
}

static int ready_event_valid(const struct fwlab_hif_ready_event *event)
{
    return event->version == FWLAB_HIF_COMMAND_PORT_VERSION &&
           event->size == sizeof(*event) && event->reserved0 == 0 &&
           event->sequence != 0 &&
           fwlab_hif_command_ticket_valid(&event->ticket);
}

static int latch_ready_event(
    struct c42_controller *controller,
    const struct fwlab_hif_ready_event *event)
{
    uint32_t index;

    for (index = 0; index < controller->config.command_capacity; ++index) {
        struct c42_command_record *command = &controller->commands[index];

        if (command->state == C42_COMMAND_HIF_COMMITTED &&
            ticket_equal(&command->ticket, &event->ticket)) {
            command->ready = *event;
            command->state = C42_COMMAND_READY;
            return 1;
        }
    }
    return 0;
}

static int poll_ready_once(struct c42_controller *controller)
{
    struct fwlab_hif_ready_event event = {0};
    enum fwlab_hif_command_port_result result;
    uint32_t count = UINT32_MAX;

    result = controller->providers.command.ops->poll(
        controller->providers.command.context, 1, &event, 1, &count
    );
    if (result != FWLAB_HIF_PORT_OK || count > 1 ||
        (count == 1 && !ready_event_valid(&event))) {
        c42_fault_controller(controller, C42_FAULT_READY_CONTRACT);
        return 1;
    }
    if (count == 1 && latch_ready_event(controller, &event) == 0) {
        c42_fault_controller(controller, C42_FAULT_READY_CONTRACT);
    }
    return 1;
}

static int local_hif_commit_pending(
    const struct c42_controller *controller)
{
    uint32_t index;

    for (index = 0; index < controller->config.command_capacity; ++index) {
        if (controller->commands[index].state ==
            C42_COMMAND_PORT_COMMITTED) {
            return 1;
        }
    }
    return 0;
}

int c42_poll_ready(struct c42_controller *controller)
{
    uint32_t offset;
    int poll_needed = 0;

    if (!c42_controller_valid(controller) ||
        controller->phase != C42_CONTROLLER_READY ||
        controller->admission_paused != 0) {
        return 0;
    }
    if (local_hif_commit_pending(controller)) {
        return 0;
    }
    controller->ready_poll_pending = 0;
    for (offset = 0; offset < controller->config.command_capacity; ++offset) {
        uint16_t selected = (uint16_t)(
            (controller->ready_cursor + offset) %
            controller->config.command_capacity
        );
        struct c42_command_record *command = &controller->commands[selected];

        if (command->state == C42_COMMAND_HIF_COMMITTED) {
            poll_needed = 1;
            continue;
        }
        if (command->state == C42_COMMAND_READY &&
            acquire_completion(controller, command) != 0) {
            controller->ready_cursor = (uint8_t)(
                (selected + 1u) % controller->config.command_capacity
            );
            return 1;
        }
        if (command->state == C42_COMMAND_LEASED ||
            command->state == C42_COMMAND_CONSUME_PREPARE) {
            controller->ready_cursor = (uint8_t)(
                (selected + 1u) % controller->config.command_capacity
            );
            return prepare_consume(controller, selected);
        }
    }
    if (poll_needed != 0) {
        return poll_ready_once(controller);
    }
    return 0;
}

static int progress_release(
    struct c42_controller *controller,
    struct c42_command_record *command)
{
    enum fwlab_hif_command_port_result result;
    bool released = false;

    if (command->release_started == 0) {
        result = controller->providers.command.ops->completion_release_start(
            controller->providers.command.context, &command->lease,
            command->release_uid, &released
        );
        command->release_started = 1;
    } else {
        result = controller->providers.command.ops->completion_release_query(
            controller->providers.command.context, &command->lease,
            command->release_uid, &released
        );
    }
    if (result == FWLAB_HIF_PORT_OK && released) {
        command->state = C42_COMMAND_HIF_COMMITTED;
    } else if (result != FWLAB_HIF_PORT_IN_PROGRESS &&
               result != FWLAB_HIF_PORT_OK) {
        c42_fault_controller(controller, C42_FAULT_CONSUME_CONTRACT);
    }
    return 1;
}

static int progress_body(
    struct c42_controller *controller,
    uint16_t command_index)
{
    struct c42_command_record *command = &controller->commands[command_index];
    struct c42_publication_record *publication =
        &controller->publications[command_index];
    struct c42_cq_record *cq = &controller->cq[command->cq_index];
    struct c42_cq_slot *slot = &cq->slots[command->cq_slot];
    struct c42_memory_status status = {0};
    enum c42_memory_result call_result;
    enum c42_memory_result effect;
    int valid;

    if (publication->body_started == 0) {
        call_result = controller->providers.memory.ops->body_start(
            controller->providers.memory.context, &cq->memory,
            command->cq_slot, command->cqe_bytes,
            &publication->body_token, &status
        );
        publication->body_started = 1;
    } else {
        call_result = controller->providers.memory.ops->body_query(
            controller->providers.memory.context, &cq->memory,
            command->cq_slot, command->cqe_bytes,
            &publication->body_token, &status
        );
    }
    effect = publication_effect(
        call_result, &status, &publication->body_token, &valid
    );
    if (effect == C42_MEMORY_IN_PROGRESS) {
        return 1;
    }
    if (valid == 0 || effect == C42_MEMORY_POISONED ||
        effect == C42_MEMORY_INVALID || effect == C42_MEMORY_STALE ||
        status.prefix < publication->body_prefix || status.prefix > 15 ||
        (effect == C42_MEMORY_FULL && status.prefix != 15)) {
        c42_fault_controller(controller, C42_FAULT_PUBLICATION_MEMORY);
        return 1;
    }
    if (effect == C42_MEMORY_FULL ||
        ((effect == C42_MEMORY_EXACT_PREFIX ||
          effect == C42_MEMORY_NO_EFFECT) && status.prefix == 15)) {
        publication->body_prefix = 15;
        slot->state = C42_SLOT_BODY_STAGED;
    } else if (effect == C42_MEMORY_EXACT_PREFIX ||
               effect == C42_MEMORY_NO_EFFECT) {
        publication->body_prefix = (uint16_t)status.prefix;
    } else if (effect != C42_MEMORY_UNKNOWN &&
               effect != C42_MEMORY_IN_PROGRESS) {
        c42_fault_controller(controller, C42_FAULT_PUBLICATION_MEMORY);
    }
    return 1;
}

static void pause_for_marker(
    struct c42_controller *controller,
    struct c42_command_record *command,
    struct c42_cq_slot *slot)
{
    command->state = C42_COMMAND_MARKER_RECONCILE;
    slot->state = C42_SLOT_MARKER_VISIBLE_RECONCILE;
    controller->admission_paused = 1;
}

static int progress_marker(
    struct c42_controller *controller,
    uint16_t command_index)
{
    struct c42_command_record *command = &controller->commands[command_index];
    struct c42_publication_record *publication =
        &controller->publications[command_index];
    struct c42_cq_record *cq = &controller->cq[command->cq_index];
    struct c42_cq_slot *slot = &cq->slots[command->cq_slot];
    struct c42_memory_status status = {0};
    enum c42_memory_result call_result;
    enum c42_memory_result effect;
    int valid;

    if (publication->marker_started == 0) {
        call_result = controller->providers.memory.ops->marker_start(
            controller->providers.memory.context, &cq->memory,
            command->cq_slot, command->cqe_bytes[14],
            &publication->marker_token, &status
        );
        publication->marker_started = 1;
    } else {
        call_result = controller->providers.memory.ops->marker_query(
            controller->providers.memory.context, &cq->memory,
            command->cq_slot, command->cqe_bytes[14],
            &publication->marker_token, &status
        );
    }
    effect = publication_effect(
        call_result, &status, &publication->marker_token, &valid
    );
    if (valid == 0 || effect == C42_MEMORY_POISONED ||
        effect == C42_MEMORY_INVALID || effect == C42_MEMORY_STALE ||
        effect == C42_MEMORY_EXACT_PREFIX) {
        pause_for_marker(controller, command, slot);
        c42_fault_controller(controller, C42_FAULT_PUBLICATION_MEMORY);
        return 1;
    }
    if (effect == C42_MEMORY_FULL && status.committed != 0) {
        publication->marker_visible = 1;
        pause_for_marker(controller, command, slot);
    } else if (effect == C42_MEMORY_UNKNOWN) {
        publication->marker_visible = 0;
        pause_for_marker(controller, command, slot);
    } else if (effect != C42_MEMORY_NO_EFFECT &&
               effect != C42_MEMORY_IN_PROGRESS) {
        pause_for_marker(controller, command, slot);
        c42_fault_controller(controller, C42_FAULT_PUBLICATION_MEMORY);
    }
    return 1;
}

static int ack_prefix_valid(
    const struct c42_cq_record *cq,
    uint16_t delta)
{
    uint16_t index;
    uint16_t slot = cq->host_head;

    if (delta > cq->unacked_count) {
        return 0;
    }
    for (index = 0; index < delta; ++index) {
        if (cq->slots[slot].state != C42_SLOT_CQE_COMMITTED) {
            return 0;
        }
        slot = (uint16_t)((slot + 1u) % cq->depth);
    }
    return 1;
}

static int apply_ack(
    struct c42_controller *controller,
    struct c42_cq_record *cq,
    uint16_t new_head,
    uint16_t delta)
{
    uint16_t index;
    uint16_t slot = cq->host_head;

    if (!ack_prefix_valid(cq, delta)) {
        return 0;
    }
    for (index = 0; index < delta; ++index) {
        uint16_t next = (uint16_t)((slot + 1u) % cq->depth);

        memset(&cq->slots[slot], 0, sizeof(cq->slots[slot]));
        slot = next;
    }
    cq->host_head = new_head;
    cq->unacked_count = (uint16_t)(cq->unacked_count - delta);
    c42_try_finish_tombstones(controller);
    return 1;
}

static void recompute_admission_pause(struct c42_controller *controller)
{
    uint32_t index;

    controller->admission_paused = 0;
    for (index = 0; index < controller->config.command_capacity; ++index) {
        if (controller->commands[index].state ==
            C42_COMMAND_MARKER_RECONCILE) {
            controller->admission_paused = 1;
            return;
        }
    }
}

static int cross_commit(
    struct c42_controller *controller,
    uint16_t command_index,
    enum fwlab_hif_consume_state consume_state)
{
    struct c42_command_record *command = &controller->commands[command_index];
    struct c42_cq_record *cq = &controller->cq[command->cq_index];
    struct c42_cq_slot *slot = &cq->slots[command->cq_slot];
    struct c42_reconcile_record *reconcile =
        &controller->reconciles[command_index];
    struct c42_notification_record *notification =
        &controller->notifications[command_index];

    if (slot->state != C42_SLOT_MARKER_VISIBLE_RECONCILE ||
        cq->reserved_count == 0 || reconcile->consume_known == 0 ||
        notification->in_use == 0) {
        c42_fault_controller(controller, C42_FAULT_CONSUME_CONTRACT);
        return 1;
    }
    slot->state = C42_SLOT_CQE_COMMITTED;
    cq->reserved_count--;
    cq->unacked_count++;
    cq->device_tail++;
    if (cq->device_tail == cq->depth) {
        cq->device_tail = 0;
        cq->device_phase ^= 1u;
    }
    notification->state = C42_NOTIFY_READY;
    reconcile->state = consume_state == FWLAB_HIF_CONSUME_CLEANUP_PENDING ?
                       C42_RECONCILE_CLEANUP_PENDING :
                       C42_RECONCILE_RETIRE_READY;
    c42_release_command_record(controller, command_index);
    recompute_admission_pause(controller);
    if (cq->pending_ack.valid != 0) {
        uint16_t new_head = cq->pending_ack.new_head;
        uint16_t delta = cq->pending_ack.delta;

        if (apply_ack(controller, cq, new_head, delta) != 0) {
            memset(&cq->pending_ack, 0, sizeof(cq->pending_ack));
        } else {
            c42_fault_controller(controller, C42_FAULT_PUBLICATION_MEMORY);
        }
    }
    return 1;
}

static int progress_cross_commit(
    struct c42_controller *controller,
    uint16_t command_index)
{
    struct c42_publication_record *publication =
        &controller->publications[command_index];
    struct c42_reconcile_record *reconcile =
        &controller->reconciles[command_index];
    enum fwlab_hif_consume_state state = (enum fwlab_hif_consume_state)(
        FWLAB_HIF_CONSUME_POISONED + 1u
    );
    enum fwlab_hif_command_port_result result;

    if (publication->marker_visible == 0) {
        return progress_marker(controller, command_index);
    }
    if (reconcile->state == C42_RECONCILE_PREPARED) {
        result = controller->providers.command.ops->consume_commit(
            controller->providers.command.context, &reconcile->consume, &state
        );
        reconcile->state = C42_RECONCILE_COMMIT_UNKNOWN;
    } else {
        result = controller->providers.command.ops->consume_query(
            controller->providers.command.context, &reconcile->consume, &state
        );
    }
    if (result == FWLAB_HIF_PORT_IN_PROGRESS ||
        (result == FWLAB_HIF_PORT_OK &&
         (state == FWLAB_HIF_CONSUME_NOT_STARTED ||
          state == FWLAB_HIF_CONSUME_PREPARED))) {
        return 1;
    }
    if (result != FWLAB_HIF_PORT_OK ||
        (state != FWLAB_HIF_CONSUME_COMMITTED &&
         state != FWLAB_HIF_CONSUME_CLEANUP_PENDING)) {
        c42_fault_controller(controller, C42_FAULT_CONSUME_CONTRACT);
        return 1;
    }
    return cross_commit(controller, command_index, state);
}

int c42_progress_publication(struct c42_controller *controller)
{
    uint32_t offset;

    if (!c42_controller_valid(controller) ||
        (controller->phase != C42_CONTROLLER_READY &&
         controller->phase != C42_CONTROLLER_FAULTED_RESET_REQUIRED)) {
        return 0;
    }
    for (offset = 0; offset < controller->config.command_capacity; ++offset) {
        uint16_t index = (uint16_t)(
            (controller->publication_cursor + offset) %
            controller->config.command_capacity
        );
        struct c42_command_record *command = &controller->commands[index];

        if (command->state == C42_COMMAND_RELEASE_RECONCILE) {
            controller->publication_cursor = (uint8_t)(
                (index + 1u) % controller->config.command_capacity
            );
            return progress_release(controller, command);
        }
        if (command->state == C42_COMMAND_MARKER_RECONCILE &&
            controller->phase == C42_CONTROLLER_READY) {
            controller->publication_cursor = (uint8_t)(
                (index + 1u) % controller->config.command_capacity
            );
            return progress_cross_commit(controller, index);
        }
        if (command->state == C42_COMMAND_PUB_RESERVED &&
            controller->phase == C42_CONTROLLER_READY) {
            struct c42_cq_record *cq = &controller->cq[command->cq_index];
            struct c42_cq_slot *slot = &cq->slots[command->cq_slot];

            if (slot->state == C42_SLOT_RESERVED) {
                controller->publication_cursor = (uint8_t)(
                    (index + 1u) % controller->config.command_capacity
                );
                return progress_body(controller, index);
            }
            if (slot->state == C42_SLOT_BODY_STAGED) {
                controller->publication_cursor = (uint8_t)(
                    (index + 1u) % controller->config.command_capacity
                );
                return progress_marker(controller, index);
            }
        }
    }
    return 0;
}

int c42_progress_reconcile(struct c42_controller *controller)
{
    uint32_t offset;

    if (!c42_controller_valid(controller) ||
        (controller->phase != C42_CONTROLLER_READY &&
         controller->phase != C42_CONTROLLER_FAULTED_RESET_REQUIRED)) {
        return 0;
    }
    for (offset = 0; offset < controller->config.command_capacity; ++offset) {
        uint16_t index = (uint16_t)(
            (controller->reconcile_cursor + offset) %
            controller->config.command_capacity
        );
        struct c42_reconcile_record *record = &controller->reconciles[index];
        enum fwlab_hif_consume_state state =
            (enum fwlab_hif_consume_state)(
                FWLAB_HIF_CONSUME_POISONED + 1u
            );
        enum fwlab_hif_command_port_result result;

        if (record->in_use == 0 || record->consume_known == 0) {
            continue;
        }
        if (record->state == C42_RECONCILE_CLEANUP_PENDING) {
            controller->reconcile_cursor = (uint8_t)(
                (index + 1u) % controller->config.command_capacity
            );
            result = controller->providers.command.ops->consume_query(
                controller->providers.command.context, &record->consume, &state
            );
            if (result == FWLAB_HIF_PORT_OK &&
                state == FWLAB_HIF_CONSUME_COMMITTED) {
                record->state = C42_RECONCILE_RETIRE_READY;
            } else if (result != FWLAB_HIF_PORT_IN_PROGRESS &&
                       (result != FWLAB_HIF_PORT_OK ||
                        state != FWLAB_HIF_CONSUME_CLEANUP_PENDING)) {
                c42_fault_controller(controller, C42_FAULT_CONSUME_CONTRACT);
            }
            return 1;
        }
        if (record->state == C42_RECONCILE_RETIRE_READY) {
            controller->reconcile_cursor = (uint8_t)(
                (index + 1u) % controller->config.command_capacity
            );
            result = controller->providers.command.ops->consume_retire(
                controller->providers.command.context, &record->consume, &state
            );
            if (result == FWLAB_HIF_PORT_OK &&
                state == FWLAB_HIF_CONSUME_RETIRED) {
                memset(record, 0, sizeof(*record));
            } else if (result != FWLAB_HIF_PORT_IN_PROGRESS) {
                c42_fault_controller(controller, C42_FAULT_CONSUME_CONTRACT);
            }
            return 1;
        }
    }
    return 0;
}

static int marker_pending(const struct c42_cq_record *cq)
{
    uint16_t slot;

    for (slot = 0; slot < cq->depth; ++slot) {
        if (cq->slots[slot].state == C42_SLOT_MARKER_VISIBLE_RECONCILE) {
            return 1;
        }
    }
    return 0;
}

enum c42_result c42_cq_head_event_apply(
    struct c42_controller *controller,
    const struct c42_cq_head_event *event)
{
    struct c42_cq_record *cq;
    uint16_t index;
    uint16_t delta;
    uint16_t potential;

    if (!c42_controller_valid(controller) || event == NULL ||
        event->reserved != 0 ||
        event->instance_nonce != controller->config.instance_nonce ||
        !c42_queue_index(event->queue_id, &index)) {
        return C42_INVALID;
    }
    if (event->controller_epoch != controller->controller_epoch) {
        return C42_STALE;
    }
    if (controller->phase != C42_CONTROLLER_READY) {
        return controller->phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED ?
               C42_FAULTED : C42_WRONG_STATE;
    }
    cq = &controller->cq[index];
    if (event->ring_generation != cq->ring_generation) {
        return C42_STALE;
    }
    if (cq->life != C42_QUEUE_LIVE || event->new_head >= cq->depth) {
        return C42_INVALID;
    }
    if (event->new_head == cq->host_head) {
        return C42_NO_EFFECT;
    }
    delta = (uint16_t)(((uint32_t)event->new_head + cq->depth -
                        cq->host_head) % cq->depth);
    if (marker_pending(cq)) {
        potential = (uint16_t)(cq->unacked_count + 1u);
        if (delta > potential) {
            return C42_INVALID;
        }
        if (cq->pending_ack.valid != 0) {
            if (delta < cq->pending_ack.delta) {
                return C42_NO_EFFECT;
            }
            if (delta == cq->pending_ack.delta &&
                event->new_head == cq->pending_ack.new_head) {
                return C42_NO_EFFECT;
            }
        }
        cq->pending_ack.valid = 1;
        cq->pending_ack.base_head = cq->host_head;
        cq->pending_ack.new_head = event->new_head;
        cq->pending_ack.delta = delta;
        return C42_OK;
    }
    if (!ack_prefix_valid(cq, delta)) {
        return C42_INVALID;
    }
    (void)apply_ack(controller, cq, event->new_head, delta);
    return C42_OK;
}

static void notification_copy(
    const struct c42_notification_record *record,
    struct c42_notification *notification)
{
    memset(notification, 0, sizeof(*notification));
    notification->token = record->token;
    notification->publication_uid = record->publication_uid;
    notification->cq_ring_generation = record->cq_ring_generation;
    notification->completion_queue_id = record->completion_queue_id;
    notification->slot_ordinal = record->slot_ordinal;
    if (record->state == C42_NOTIFY_READY) {
        notification->state = C42_NOTIFICATION_READY;
    } else if (record->state == C42_NOTIFY_ACQUIRED) {
        notification->state = C42_NOTIFICATION_ACQUIRED;
    } else if (record->state == C42_NOTIFY_CONSUMED) {
        notification->state = C42_NOTIFICATION_CONSUMED;
    } else {
        notification->state = C42_NOTIFICATION_SUPPRESSED;
    }
}

enum c42_result c42_notification_acquire(
    struct c42_controller *controller,
    struct c42_notification *notification)
{
    uint32_t index;

    if (!c42_controller_valid(controller) || notification == NULL) {
        return C42_INVALID;
    }
    if (controller->phase != C42_CONTROLLER_READY) {
        return C42_SUPERSEDED;
    }
    for (index = 0; index < controller->config.command_capacity; ++index) {
        struct c42_notification_record *record =
            &controller->notifications[index];

        if (record->in_use != 0 && record->state == C42_NOTIFY_READY) {
            if (record->controller_epoch != controller->controller_epoch) {
                record->state = C42_NOTIFY_SUPPRESSED;
                continue;
            }
            record->state = C42_NOTIFY_ACQUIRED;
            notification_copy(record, notification);
            return C42_OK;
        }
    }
    return C42_NOT_FOUND;
}

static struct c42_notification_record *find_notification(
    struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    uint32_t index;

    for (index = 0; index < controller->config.command_capacity; ++index) {
        struct c42_notification_record *record =
            &controller->notifications[index];

        if (record->in_use != 0 &&
            c42_operation_token_equal(&record->token, token)) {
            return record;
        }
    }
    return NULL;
}

static const struct c42_notification_record *find_notification_const(
    const struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    uint32_t index;

    for (index = 0; index < controller->config.command_capacity; ++index) {
        const struct c42_notification_record *record =
            &controller->notifications[index];

        if (record->in_use != 0 &&
            c42_operation_token_equal(&record->token, token)) {
            return record;
        }
    }
    return NULL;
}

enum c42_result c42_notification_consume(
    struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    struct c42_notification_record *record;

    if (!c42_controller_valid(controller) || token == NULL) {
        return C42_INVALID;
    }
    record = find_notification(controller, token);
    if (record == NULL) {
        return C42_STALE;
    }
    if (record->controller_epoch != controller->controller_epoch ||
        record->state == C42_NOTIFY_SUPPRESSED) {
        return C42_SUPERSEDED;
    }
    if (record->state != C42_NOTIFY_ACQUIRED) {
        return C42_WRONG_STATE;
    }
    record->state = C42_NOTIFY_CONSUMED;
    return C42_OK;
}

enum c42_result c42_notification_query(
    const struct c42_controller *controller,
    const struct c42_operation_token *token,
    struct c42_notification *notification)
{
    const struct c42_notification_record *record;

    if (!c42_controller_valid(controller) || token == NULL ||
        notification == NULL) {
        return C42_INVALID;
    }
    record = find_notification_const(controller, token);
    if (record == NULL) {
        return C42_STALE;
    }
    notification_copy(record, notification);
    return C42_OK;
}

enum c42_result c42_notification_retire(
    struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    struct c42_notification_record *record;

    if (!c42_controller_valid(controller) || token == NULL) {
        return C42_INVALID;
    }
    record = find_notification(controller, token);
    if (record == NULL) {
        return C42_STALE;
    }
    if (record->state != C42_NOTIFY_CONSUMED &&
        record->state != C42_NOTIFY_SUPPRESSED) {
        return C42_WRONG_STATE;
    }
    memset(record, 0, sizeof(*record));
    return C42_OK;
}
