/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_internal.h"

#include <string.h>

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
