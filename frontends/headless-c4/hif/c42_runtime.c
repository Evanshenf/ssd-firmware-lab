/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_internal.h"

#include <string.h>

_Static_assert(sizeof(struct c42_observer_slot_v2) == 64,
               "observer slot v2 ABI");
_Static_assert(sizeof(struct c42_observer_queue_v2) == 2104,
               "observer queue v2 ABI");
_Static_assert(sizeof(struct c42_observer_command_v2) == 88,
               "observer command v2 ABI");
_Static_assert(sizeof(struct c42_observer_publication_v2) == 40,
               "observer publication v2 ABI");
_Static_assert(sizeof(struct c42_observer_reconcile_v2) == 32,
               "observer reconcile v2 ABI");
_Static_assert(sizeof(struct c42_observer_notification_v2) == 56,
               "observer notification v2 ABI");
_Static_assert(sizeof(struct c42_observer_candidate_v2) == 64,
               "observer candidate v2 ABI");
_Static_assert(sizeof(struct c42_observer_control_v2) == 56,
               "observer control v2 ABI");
_Static_assert(sizeof(struct c42_observer_target_v2) == 64,
               "observer target v2 ABI");
_Static_assert(sizeof(struct c42_observer_v2) == 26888,
               "observer v2 ABI");
_Static_assert(offsetof(struct c42_observer_v2, sq) == 40,
               "observer v2 header ABI");
_Static_assert(C42_OK == 0 && C42_INVALID == 1 &&
               C42_WRONG_STATE == 2 && C42_STALE == 3 &&
               C42_NO_EFFECT == 4 && C42_BACKPRESSURE == 5 &&
               C42_NO_CAPACITY == 6 && C42_IN_PROGRESS == 7 &&
               C42_TOO_LATE == 8 && C42_FAULTED == 9 &&
               C42_COUNTER_EXHAUSTED == 10 && C42_NOT_FOUND == 11 &&
               C42_POISONED == 12 && C42_SUPERSEDED == 13,
               "component result numeric ABI");
_Static_assert(C42_CANDIDATE_PREPARED == 1 &&
               C42_CANDIDATE_SCRUB_UNKNOWN == 2 &&
               C42_CANDIDATE_READY == 3 &&
               C42_CANDIDATE_ABORTING == 4 &&
               C42_CANDIDATE_COMMITTED == 5 &&
               C42_CANDIDATE_ABORTED == 6 &&
               C42_CANDIDATE_POISONED == 7 &&
               C42_CANDIDATE_SUPERSEDED == 8 &&
               C42_CANDIDATE_COMMITTED_AWAIT_RETIRE == 9 &&
               C42_CANDIDATE_RETIRE_UNKNOWN == 10 &&
               C42_CANDIDATE_RETIRE_READY == 11 &&
               C42_CANDIDATE_RETIRED == 12,
               "candidate state numeric ABI");
_Static_assert(C42_CONTROL_STARTED == 1 && C42_CONTROL_WAITING == 2 &&
               C42_CONTROL_COMMITTED == 3 &&
               C42_CONTROL_CLEANUP_PENDING == 4 &&
               C42_CONTROL_RETIRED == 5 && C42_CONTROL_POISONED == 6 &&
               C42_CONTROL_SUPERSEDED == 7,
               "control state numeric ABI");
_Static_assert(C42_MEMORY_OK == 0 && C42_MEMORY_INVALID == 1 &&
               C42_MEMORY_STALE == 2 && C42_MEMORY_NO_EFFECT == 3 &&
               C42_MEMORY_EXACT_PREFIX == 4 && C42_MEMORY_FULL == 5 &&
               C42_MEMORY_UNKNOWN == 6 && C42_MEMORY_POISONED == 7 &&
               C42_MEMORY_IN_PROGRESS == 8 && C42_MEMORY_RETIRED == 9,
               "memory result numeric ABI");
_Static_assert(C42_OBSERVER_SLOT_FREE == 0 &&
               C42_OBSERVER_SLOT_RESERVED == 1 &&
               C42_OBSERVER_SLOT_CQE_COMMITTED == 2 &&
               C42_OBSERVER_SLOT_BODY_STAGED == 10 &&
               C42_OBSERVER_SLOT_MARKER_RECONCILE == 11 &&
               C42_OBSERVER_SLOT_INVALID == 255,
               "observer slot numeric ABI");
_Static_assert(C42_OBSERVER_COMMAND_FREE == 0 &&
               C42_OBSERVER_COMMAND_CAPTURED == 1 &&
               C42_OBSERVER_COMMAND_PREPARE_QUERY == 2 &&
               C42_OBSERVER_COMMAND_PORT_RESERVED == 3 &&
               C42_OBSERVER_COMMAND_ADMIT_QUERY == 4 &&
               C42_OBSERVER_COMMAND_PORT_COMMITTED == 5 &&
               C42_OBSERVER_COMMAND_HIF_COMMITTED == 6 &&
               C42_OBSERVER_COMMAND_READY == 7 &&
               C42_OBSERVER_COMMAND_LEASED == 8 &&
               C42_OBSERVER_COMMAND_CONSUME_PREPARE == 9 &&
               C42_OBSERVER_COMMAND_PUB_RESERVED == 10 &&
               C42_OBSERVER_COMMAND_MARKER_RECONCILE == 11 &&
               C42_OBSERVER_COMMAND_RELEASE_RECONCILE == 12 &&
               C42_OBSERVER_COMMAND_ABORT_RECONCILE == 13 &&
               C42_OBSERVER_COMMAND_ADMIT_POISON_HOLD == 14 &&
               C42_OBSERVER_COMMAND_CONSUME_POISON_HOLD == 15 &&
               C42_OBSERVER_COMMAND_INVALID == 255,
               "observer command numeric ABI");
_Static_assert(C42_OBSERVER_RECONCILE_RESERVED == 0 &&
               C42_OBSERVER_RECONCILE_PREPARED == 1 &&
               C42_OBSERVER_RECONCILE_COMMIT_UNKNOWN == 2 &&
               C42_OBSERVER_RECONCILE_CLEANUP_PENDING == 3 &&
               C42_OBSERVER_RECONCILE_RETIRE_READY == 4 &&
               C42_OBSERVER_RECONCILE_INVALID == 255,
               "observer reconcile numeric ABI");
_Static_assert(C42_OBSERVER_NOTIFY_RESERVED == 0 &&
               C42_OBSERVER_NOTIFY_READY == 1 &&
               C42_OBSERVER_NOTIFY_ACQUIRED == 2 &&
               C42_OBSERVER_NOTIFY_CONSUMED == 3 &&
               C42_OBSERVER_NOTIFY_SUPPRESSED == 4 &&
               C42_OBSERVER_NOTIFY_INVALID == 255,
               "observer notification numeric ABI");

static int seed_valid(const struct c42_counter_seed *seed)
{
    return seed->next != 0 && seed->maximum != 0 &&
           seed->next <= seed->maximum;
}

static int config_valid(const struct c42_config *config)
{
    return config != NULL && config->version == C42_COMPONENT_VERSION &&
           config->size == sizeof(*config) &&
           config->maximum_queue_depth >= 2 &&
           config->maximum_queue_depth <= C42_MAX_QUEUE_DEPTH &&
           config->command_capacity != 0 &&
           config->command_capacity <= C42_MAX_COMMANDS &&
           config->target_capacity != 0 &&
           config->target_capacity <= C42_MAX_TARGET_REFS &&
           config->worst_case_actions != 0 &&
           config->instance_nonce != 0 && config->owner_epoch != 0 &&
           config->origin_domain_nonce != 0 &&
           config->safety_generation != 0 &&
           config->initial_controller_epoch != 0 &&
           config->initial_active_generation != 0 &&
           config->initial_active_generation != UINT32_MAX &&
           seed_valid(&config->origin_uid) &&
           seed_valid(&config->client_uid) &&
           seed_valid(&config->release_uid) &&
           seed_valid(&config->trace_uid) &&
           seed_valid(&config->publication_uid) &&
           seed_valid(&config->notification_uid) &&
           seed_valid(&config->candidate_uid) &&
           seed_valid(&config->target_uid) &&
           seed_valid(&config->control_uid) &&
           seed_valid(&config->reset_uid) &&
           seed_valid(&config->teardown_uid) &&
           c42_bytes_zero(config->reserved, sizeof(config->reserved));
}

static void counter_init(
    struct c42_counter *counter,
    const struct c42_counter_seed *seed)
{
    counter->next = seed->next;
    counter->maximum = seed->maximum;
}

size_t c42_arena_size(const struct c42_config *config)
{
    if (!config_valid(config)) {
        return 0;
    }
    return sizeof(struct c42_controller);
}

enum c42_result c42_init(
    void *arena,
    size_t arena_size,
    const struct c42_config *config,
    const struct c42_providers *providers,
    struct c42_controller **controller)
{
    struct c42_controller *local;
    uint16_t index;

    if (arena == NULL || controller == NULL || !config_valid(config) ||
        providers == NULL || !c42_memory_port_valid(&providers->memory) ||
        !fwlab_hif_command_port_valid(&providers->command) ||
        arena_size < sizeof(struct c42_controller) ||
        ((uintptr_t)arena % _Alignof(struct c42_controller)) != 0) {
        return C42_INVALID;
    }
    memset(arena, 0, sizeof(struct c42_controller));
    local = arena;
    local->magic = C42_INTERNAL_MAGIC;
    local->config = *config;
    local->providers = *providers;
    local->controller_epoch = config->initial_controller_epoch;
    local->next_active_generation = config->initial_active_generation;
    local->next_candidate_generation = 1;
    local->next_target_generation = 1;
    local->next_control_generation = 1;
    local->next_notification_generation = 1;
    local->next_reset_generation = 1;
    local->next_teardown_generation = 1;
    local->phase = C42_CONTROLLER_COLD_NO_QUEUES;
    counter_init(&local->origin_uid, &config->origin_uid);
    counter_init(&local->client_uid, &config->client_uid);
    counter_init(&local->release_uid, &config->release_uid);
    counter_init(&local->trace_uid, &config->trace_uid);
    counter_init(&local->publication_uid, &config->publication_uid);
    counter_init(&local->notification_uid, &config->notification_uid);
    counter_init(&local->candidate_uid, &config->candidate_uid);
    counter_init(&local->target_uid, &config->target_uid);
    counter_init(&local->control_uid, &config->control_uid);
    counter_init(&local->reset_uid, &config->reset_uid);
    counter_init(&local->teardown_uid, &config->teardown_uid);
    for (index = 0; index < C42_QUEUE_SLOTS; ++index) {
        local->sq[index].queue_id = index;
        local->sq[index].life = C42_QUEUE_ABSENT;
        local->cq[index].queue_id = index;
        local->cq[index].life = C42_QUEUE_ABSENT;
        local->cq[index].next_slot_generation = 1;
    }
    *controller = local;
    return C42_OK;
}

enum c42_result c42_step(
    struct c42_controller *controller,
    uint32_t budget,
    struct c42_step_result *result)
{
    struct c42_step_result local = {0};
    uint32_t unit;

    if (!c42_controller_valid(controller) || result == NULL) {
        return C42_INVALID;
    }
    local.requested_budget = budget;
    if (budget == 0) {
        *result = local;
        return C42_OK;
    }
    for (unit = 0; unit < budget; ++unit) {
        int progressed = 0;
        uint8_t offset;

        if (c42_progress_queue_controls(controller) != 0) {
            progressed = 1;
        } else if (controller->phase == C42_CONTROLLER_RESETTING ||
                   controller->phase == C42_CONTROLLER_TEARING_DOWN) {
            break;
        } else {
            for (offset = 0; offset < 4; ++offset) {
                uint8_t lane = (uint8_t)(
                    (controller->scheduler_cursor + offset) % 4u
                );

                if ((lane == 0 && c42_progress_admission(controller) != 0) ||
                    (lane == 1 && c42_poll_ready(controller) != 0) ||
                    (lane == 2 &&
                     c42_progress_publication(controller) != 0) ||
                    (lane == 3 && c42_progress_reconcile(controller) != 0)) {
                    controller->scheduler_cursor = (uint8_t)((lane + 1u) % 4u);
                    progressed = 1;
                    break;
                }
            }
        }
        if (progressed == 0) {
            break;
        }
        local.units_executed++;
        local.transitions++;
    }
    local.notifications_ready = c42_count_notifications(controller);
    *result = local;
    return controller->phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED ?
           C42_FAULTED : C42_OK;
}

enum c42_result c42_raw_snapshot_copy(
    const struct c42_controller *controller,
    const struct fwlab_nvme_command_handle *handle,
    const struct fwlab_nvme_origin_token *origin,
    uint8_t output[C42_SQE_BYTES])
{
    uint32_t index;

    if (!c42_controller_valid(controller) || handle == NULL ||
        origin == NULL || output == NULL) {
        return C42_INVALID;
    }
    if (controller->phase != C42_CONTROLLER_READY) {
        return C42_SUPERSEDED;
    }
    for (index = 0; index < controller->config.command_capacity; ++index) {
        const struct c42_command_record *command = &controller->commands[index];

        if (command->state >= C42_COMMAND_HIF_COMMITTED &&
            command->state <= C42_COMMAND_RELEASE_RECONCILE &&
            c42_handle_equal(&command->command.handle, handle) &&
            c42_origin_equal(&command->origin, origin)) {
            memcpy(output, command->raw_bytes, C42_SQE_BYTES);
            return C42_OK;
        }
    }
    return C42_NOT_FOUND;
}

static void snapshot_sq(
    const struct c42_sq_record *source,
    struct c42_queue_snapshot *target)
{
    target->ring_generation = source->ring_generation;
    target->mapping_generation = source->mapping_generation;
    target->queue_id = source->queue_id;
    target->depth = source->depth;
    target->host_index = source->host_tail;
    target->device_index = source->device_head;
    target->pending_or_unacked = source->pending;
    target->kind = C42_QUEUE_SQ;
    target->life = source->life;
}

static void snapshot_cq(
    const struct c42_cq_record *source,
    struct c42_queue_snapshot *target)
{
    target->ring_generation = source->ring_generation;
    target->mapping_generation = source->mapping_generation;
    target->queue_id = source->queue_id;
    target->depth = source->depth;
    target->host_index = source->host_head;
    target->device_index = source->device_tail;
    target->pending_or_unacked = source->unacked_count;
    target->kind = C42_QUEUE_CQ;
    target->life = source->life;
    target->phase = source->device_phase;
}

enum c42_result c42_snapshot_read(
    const struct c42_controller *controller,
    struct c42_snapshot *snapshot)
{
    struct c42_snapshot local = {0};
    uint16_t index;

    if (!c42_controller_valid(controller) || snapshot == NULL) {
        return C42_INVALID;
    }
    local.version = C42_COMPONENT_VERSION;
    local.size = sizeof(local);
    local.controller_epoch = controller->controller_epoch;
    local.instance_nonce = controller->config.instance_nonce;
    local.owner_epoch = controller->config.owner_epoch;
    local.phase = controller->phase;
    local.fault_cause = controller->fault_cause;
    local.active_commands = c42_count_active(controller);
    local.target_refs = c42_count_targets(controller);
    local.pending_publications = c42_count_publications(controller);
    local.pending_notifications = c42_count_notifications(controller);
    for (index = 0; index < C42_QUEUE_SLOTS; ++index) {
        snapshot_sq(&controller->sq[index], &local.sq[index]);
        snapshot_cq(&controller->cq[index], &local.cq[index]);
    }
    *snapshot = local;
    return C42_OK;
}

static int observer_ticket_equal(
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

static uint8_t observer_slot_state(uint8_t state)
{
    switch (state) {
    case C42_SLOT_FREE: return C42_OBSERVER_SLOT_FREE;
    case C42_SLOT_RESERVED: return C42_OBSERVER_SLOT_RESERVED;
    case C42_SLOT_BODY_STAGED: return C42_OBSERVER_SLOT_BODY_STAGED;
    case C42_SLOT_MARKER_VISIBLE_RECONCILE:
        return C42_OBSERVER_SLOT_MARKER_RECONCILE;
    case C42_SLOT_CQE_COMMITTED:
        return C42_OBSERVER_SLOT_CQE_COMMITTED;
    default: return C42_OBSERVER_SLOT_INVALID;
    }
}

static uint8_t observer_command_state(uint8_t state)
{
    switch (state) {
    case C42_COMMAND_FREE: return C42_OBSERVER_COMMAND_FREE;
    case C42_COMMAND_CAPTURED: return C42_OBSERVER_COMMAND_CAPTURED;
    case C42_COMMAND_PREPARE_QUERY:
        return C42_OBSERVER_COMMAND_PREPARE_QUERY;
    case C42_COMMAND_PORT_RESERVED:
        return C42_OBSERVER_COMMAND_PORT_RESERVED;
    case C42_COMMAND_ADMIT_QUERY: return C42_OBSERVER_COMMAND_ADMIT_QUERY;
    case C42_COMMAND_PORT_COMMITTED:
        return C42_OBSERVER_COMMAND_PORT_COMMITTED;
    case C42_COMMAND_HIF_COMMITTED:
        return C42_OBSERVER_COMMAND_HIF_COMMITTED;
    case C42_COMMAND_READY: return C42_OBSERVER_COMMAND_READY;
    case C42_COMMAND_LEASED: return C42_OBSERVER_COMMAND_LEASED;
    case C42_COMMAND_CONSUME_PREPARE:
        return C42_OBSERVER_COMMAND_CONSUME_PREPARE;
    case C42_COMMAND_PUB_RESERVED:
        return C42_OBSERVER_COMMAND_PUB_RESERVED;
    case C42_COMMAND_MARKER_RECONCILE:
        return C42_OBSERVER_COMMAND_MARKER_RECONCILE;
    case C42_COMMAND_RELEASE_RECONCILE:
        return C42_OBSERVER_COMMAND_RELEASE_RECONCILE;
    case C42_COMMAND_ABORT_RECONCILE:
        return C42_OBSERVER_COMMAND_ABORT_RECONCILE;
    case C42_COMMAND_ADMIT_POISON_HOLD:
        return C42_OBSERVER_COMMAND_ADMIT_POISON_HOLD;
    case C42_COMMAND_CONSUME_POISON_HOLD:
        return C42_OBSERVER_COMMAND_CONSUME_POISON_HOLD;
    default: return C42_OBSERVER_COMMAND_INVALID;
    }
}

static uint8_t observer_reconcile_state(uint8_t state)
{
    switch (state) {
    case C42_RECONCILE_RESERVED:
        return C42_OBSERVER_RECONCILE_RESERVED;
    case C42_RECONCILE_PREPARED:
        return C42_OBSERVER_RECONCILE_PREPARED;
    case C42_RECONCILE_COMMIT_UNKNOWN:
        return C42_OBSERVER_RECONCILE_COMMIT_UNKNOWN;
    case C42_RECONCILE_CLEANUP_PENDING:
        return C42_OBSERVER_RECONCILE_CLEANUP_PENDING;
    case C42_RECONCILE_RETIRE_READY:
        return C42_OBSERVER_RECONCILE_RETIRE_READY;
    default: return C42_OBSERVER_RECONCILE_INVALID;
    }
}

static uint8_t observer_notification_state(uint8_t state)
{
    switch (state) {
    case C42_NOTIFY_RESERVED: return C42_OBSERVER_NOTIFY_RESERVED;
    case C42_NOTIFY_READY: return C42_OBSERVER_NOTIFY_READY;
    case C42_NOTIFY_ACQUIRED: return C42_OBSERVER_NOTIFY_ACQUIRED;
    case C42_NOTIFY_CONSUMED: return C42_OBSERVER_NOTIFY_CONSUMED;
    case C42_NOTIFY_SUPPRESSED: return C42_OBSERVER_NOTIFY_SUPPRESSED;
    default: return C42_OBSERVER_NOTIFY_INVALID;
    }
}

static void observer_slot_fill(
    const struct c42_controller *controller,
    const struct c42_cq_slot *source,
    struct c42_observer_slot_v2 *target)
{
    uint16_t index;

    target->publication_uid = source->publication_uid;
    target->notification_uid = source->notification_uid;
    target->cq_ring_generation = source->cq_ring_generation;
    target->source_sq_generation = source->source_sq_generation;
    target->slot_generation = source->slot_generation;
    target->source_sq_id = source->source_sq_id;
    target->command_id = source->command_id;
    target->submission_queue_head = source->submission_queue_head;
    target->ordinal = source->ordinal;
    target->phase = source->phase;
    target->state = observer_slot_state(source->state);
    for (index = 0; index < controller->config.command_capacity; ++index) {
        const struct c42_command_record *command =
            &controller->commands[index];

        if (source->publication_uid != 0 &&
            command->publication_uid == source->publication_uid) {
            target->owner_present = 1;
            target->origin_matches_owner = (uint8_t)c42_origin_equal(
                &source->origin, &command->origin
            );
            break;
        }
    }
    memcpy(target->wire, source->wire, sizeof(target->wire));
}

static void observer_sq_fill(
    const struct c42_sq_record *source,
    struct c42_observer_queue_v2 *target)
{
    target->ring_generation = source->ring_generation;
    target->mapping_generation = source->mapping_generation;
    target->last_ring_generation = source->last_ring_generation;
    target->last_mapping_generation = source->last_mapping_generation;
    target->queue_id = source->queue_id;
    target->associated_cq_id = source->associated_cq_id;
    target->depth = source->depth;
    target->host_index = source->host_tail;
    target->device_index = source->device_head;
    target->pending = source->pending;
    target->frozen_tail = source->frozen_tail;
    target->kind = C42_QUEUE_SQ;
    target->queue_class = source->queue_class;
    target->life = source->life;
}

static void observer_cq_fill(
    const struct c42_controller *controller,
    const struct c42_cq_record *source,
    struct c42_observer_queue_v2 *target)
{
    uint16_t slot;

    target->ring_generation = source->ring_generation;
    target->mapping_generation = source->mapping_generation;
    target->last_ring_generation = source->last_ring_generation;
    target->last_mapping_generation = source->last_mapping_generation;
    target->next_slot_generation = source->next_slot_generation;
    target->queue_id = source->queue_id;
    target->depth = source->depth;
    target->host_index = source->host_head;
    target->device_index = source->device_tail;
    target->unacked_count = source->unacked_count;
    target->reserved_count = source->reserved_count;
    target->pending_ack_head = source->pending_ack.new_head;
    target->pending_ack_delta = source->pending_ack.delta;
    target->kind = C42_QUEUE_CQ;
    target->queue_class = source->queue_class;
    target->life = source->life;
    target->phase = source->device_phase;
    target->create_scrub_retired = source->create_scrub_retired;
    target->pending_ack_valid = source->pending_ack.valid;
    for (slot = 0; slot < C42_MAX_QUEUE_DEPTH; ++slot) {
        observer_slot_fill(
            controller, &source->slots[slot], &target->slots[slot]
        );
    }
}

static void observer_command_fill(
    const struct c42_controller *controller,
    uint16_t index,
    struct c42_observer_command_v2 *target)
{
    const struct c42_command_record *source = &controller->commands[index];
    const struct c42_reconcile_record *reconcile =
        &controller->reconciles[index];

    target->handle = source->command.handle;
    target->client_uid = source->client_uid;
    target->publication_uid = source->publication_uid;
    target->notification_uid = source->notification_uid;
    target->sq_ring_generation = source->sq_ring_generation;
    target->active_generation = source->active_generation;
    target->command_id = source->command_id;
    target->sq_index = source->sq_index;
    target->cq_index = source->cq_index;
    target->cq_slot = source->cq_slot;
    target->sqhd_snapshot = source->sqhd_snapshot;
    target->state = observer_command_state(source->state);
    target->queue_class = source->queue_class;
    target->prepared_origin_matches = (uint8_t)(
        fwlab_hif_prepared_token_valid(&source->prepared) &&
        c42_origin_equal(&source->prepared.origin, &source->origin)
    );
    target->ticket_identity_matches = (uint8_t)(
        fwlab_hif_command_ticket_valid(&source->ticket) &&
        c42_handle_equal(&source->ticket.handle, &source->command.handle) &&
        c42_origin_equal(&source->ticket.origin, &source->origin)
    );
    target->ready_ticket_matches = (uint8_t)observer_ticket_equal(
        &source->ready.ticket, &source->ticket
    );
    target->lease_ticket_matches = (uint8_t)(
        fwlab_hif_completion_lease_valid(&source->lease) &&
        observer_ticket_equal(&source->lease.ticket, &source->ticket)
    );
    target->consume_known = reconcile->consume_known;
}

static void observer_publication_fill(
    const struct c42_publication_record *source,
    struct c42_observer_publication_v2 *target)
{
    target->publication_uid = source->publication_uid;
    target->body_token_uid = source->body_token.uid;
    target->marker_token_uid = source->marker_token.uid;
    target->command_index = source->command_index;
    target->body_prefix = source->body_prefix;
    target->in_use = source->in_use;
    target->body_started = source->body_started;
    target->marker_started = source->marker_started;
    target->marker_visible = source->marker_visible;
}

static void observer_reconcile_fill(
    const struct c42_controller *controller,
    uint16_t index,
    struct c42_observer_reconcile_v2 *target)
{
    const struct c42_reconcile_record *source =
        &controller->reconciles[index];
    const struct c42_command_record *command = &controller->commands[index];

    target->publication_uid = source->publication_uid;
    target->consume_uid = source->consume.consume_uid;
    target->command_index = source->command_index;
    target->in_use = source->in_use;
    target->state = observer_reconcile_state(source->state);
    target->consume_known = source->consume_known;
    target->lease_matches_command = (uint8_t)(
        fwlab_hif_completion_lease_valid(&source->lease) &&
        observer_ticket_equal(&source->lease.ticket, &command->ticket)
    );
}

static void observer_notification_fill(
    const struct c42_controller *controller,
    const struct c42_notification_record *source,
    struct c42_observer_notification_v2 *target)
{
    target->token = source->token;
    target->publication_uid = source->publication_uid;
    target->cq_ring_generation = source->cq_ring_generation;
    target->controller_epoch = source->controller_epoch;
    target->completion_queue_id = source->completion_queue_id;
    target->slot_ordinal = source->slot_ordinal;
    target->in_use = source->in_use;
    target->state = observer_notification_state(source->state);
    target->current_epoch = (uint8_t)(
        source->in_use != 0 &&
        source->controller_epoch == controller->controller_epoch
    );
}

static void observer_candidate_fill(
    const struct c42_candidate_record *source,
    struct c42_observer_candidate_v2 *target)
{
    target->token = source->token;
    target->controller_epoch = source->controller_epoch;
    target->state = source->state;
    target->associated_cq_ring_generation =
        source->associated_cq_ring_generation;
    target->associated_cq_mapping_generation =
        source->associated_cq_mapping_generation;
    target->queue_id = source->descriptor.queue_id;
    target->associated_cq_id = source->descriptor.associated_cq_id;
    target->in_use = source->in_use;
    target->kind = source->descriptor.kind;
    target->scrub_started = source->scrub_started;
    target->retire_started = source->retire_started;
    target->provider_retired = source->provider_retired;
}

static void observer_control_fill(
    const struct c42_control_record *source,
    struct c42_observer_control_v2 *target)
{
    target->token = source->token;
    target->controller_epoch = source->controller_epoch;
    target->old_epoch = source->old_epoch;
    target->state = source->state;
    target->queue_id = source->queue_id;
    target->in_use = source->in_use;
    target->kind = source->kind;
    target->port_started = source->port_started;
    target->memory_started = source->memory_started;
}

static void observer_target_fill(
    const struct c42_controller *controller,
    const struct c42_target_record *source,
    struct c42_observer_target_v2 *target)
{
    const struct c42_command_record *active = NULL;

    target->token = source->value.token;
    target->handle = source->value.handle;
    target->sq_ring_generation = source->sq_ring_generation;
    target->sq_index = source->sq_index;
    target->in_use = source->in_use;
    if (source->in_use != 0) {
        uint16_t index;

        for (index = 0; index < controller->config.command_capacity;
             ++index) {
            const struct c42_command_record *candidate =
                &controller->commands[index];

            if (c42_command_record_active(candidate) &&
                candidate->sq_index == source->sq_index &&
                candidate->sq_ring_generation ==
                    source->sq_ring_generation &&
                c42_handle_equal(
                    &candidate->command.handle,
                    &source->value.handle) &&
                c42_origin_equal(
                    &candidate->origin, &source->value.origin) &&
                observer_ticket_equal(
                    &candidate->ticket, &source->value.ticket)) {
                active = candidate;
                break;
            }
        }
    }
    target->identity_matches_active = (uint8_t)(active != NULL);
}

enum c42_result c42_observer_read_v2(
    const struct c42_controller *controller,
    struct c42_observer_v2 *observer)
{
    struct c42_observer_v2 local;
    uint16_t index;

    if (!c42_controller_valid(controller) || observer == NULL ||
        sizeof(local) > UINT16_MAX) {
        return C42_INVALID;
    }
    memset(&local, 0, sizeof(local));
    local.version = C42_OBSERVER_VERSION;
    local.size = (uint16_t)sizeof(local);
    local.controller_epoch = controller->controller_epoch;
    local.instance_nonce = controller->config.instance_nonce;
    local.phase = controller->phase;
    local.fault_cause = controller->fault_cause;
    local.command_capacity = controller->config.command_capacity;
    local.target_capacity = controller->config.target_capacity;
    local.admission_paused = controller->admission_paused;
    local.scheduler_cursor = controller->scheduler_cursor;
    local.admission_cursor = controller->admission_cursor;
    local.publication_cursor = controller->publication_cursor;
    local.reconcile_cursor = controller->reconcile_cursor;
    local.sq_cursor = controller->sq_cursor;
    local.ready_cursor = controller->ready_cursor;
    local.ready_poll_pending = controller->ready_poll_pending;
    for (index = 0; index < C42_QUEUE_SLOTS; ++index) {
        observer_sq_fill(&controller->sq[index], &local.sq[index]);
        observer_cq_fill(
            controller, &controller->cq[index], &local.cq[index]
        );
    }
    for (index = 0; index < C42_CANDIDATE_SLOTS; ++index) {
        observer_candidate_fill(
            &controller->candidates[index], &local.candidates[index]
        );
    }
    for (index = 0; index < controller->config.command_capacity; ++index) {
        observer_command_fill(controller, index, &local.commands[index]);
        observer_publication_fill(
            &controller->publications[index], &local.publications[index]
        );
        observer_reconcile_fill(
            controller, index, &local.reconciles[index]
        );
        observer_notification_fill(
            controller, &controller->notifications[index],
            &local.notifications[index]
        );
    }
    for (index = 0; index < controller->config.target_capacity; ++index) {
        observer_target_fill(
            controller, &controller->targets[index], &local.targets[index]
        );
    }
    for (index = 0; index < C42_BUSINESS_CONTROL_SLOTS; ++index) {
        observer_control_fill(
            &controller->business_controls[index], &local.controls[index]
        );
    }
    observer_control_fill(
        &controller->reset_control,
        &local.controls[C42_BUSINESS_CONTROL_SLOTS]
    );
    observer_control_fill(
        &controller->teardown_control,
        &local.controls[C42_BUSINESS_CONTROL_SLOTS + 1u]
    );
    memcpy(observer, &local, sizeof(local));
    return C42_OK;
}
