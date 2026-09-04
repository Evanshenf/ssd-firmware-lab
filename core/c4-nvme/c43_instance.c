/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_internal.h"

#include <stdint.h>
#include <string.h>

static int counter_seed_valid(const struct fwlab_c43_counter_seed *seed)
{
    return seed->next != 0 && seed->maximum != 0 &&
           seed->next <= seed->maximum;
}

static void counter_cursor_init(
    struct c43_counter_cursor *cursor,
    const struct fwlab_c43_counter_seed *seed)
{
    cursor->next = seed->next;
    cursor->maximum = seed->maximum;
}

int c43_bytes_zero(const void *value, size_t size)
{
    const unsigned char *bytes = value;
    size_t index;

    if (bytes == NULL) {
        return 0;
    }
    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

int c43_handle_valid(const struct fwlab_nvme_command_handle *handle)
{
    return handle != NULL && handle->instance_nonce != 0 &&
           handle->command_uid != 0 && handle->controller_epoch != 0 &&
           handle->generation != 0;
}

int c43_origin_valid(const struct fwlab_nvme_origin_token *origin)
{
    return origin != NULL && (origin->word[0] != 0 || origin->word[1] != 0);
}

int c43_ticket_valid(const struct fwlab_hif_command_ticket *ticket)
{
    return ticket != NULL && c43_handle_valid(&ticket->handle) &&
           c43_origin_valid(&ticket->origin) && ticket->ticket_uid != 0 &&
           ticket->generation != 0 && ticket->reserved == 0;
}

int c43_handle_equal(
    const struct fwlab_nvme_command_handle *left,
    const struct fwlab_nvme_command_handle *right)
{
    return left != NULL && right != NULL &&
           left->instance_nonce == right->instance_nonce &&
           left->command_uid == right->command_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->generation == right->generation;
}

int c43_origin_equal(
    const struct fwlab_nvme_origin_token *left,
    const struct fwlab_nvme_origin_token *right)
{
    return left != NULL && right != NULL &&
           left->word[0] == right->word[0] &&
           left->word[1] == right->word[1];
}

int c43_action_token_equal(
    const struct fwlab_hif_action_token *left,
    const struct fwlab_hif_action_token *right)
{
    return left != NULL && right != NULL &&
           c43_handle_equal(&left->command, &right->command) &&
           c43_origin_equal(&left->origin, &right->origin) &&
           left->action_uid == right->action_uid &&
           left->generation == right->generation &&
           left->kind == right->kind && left->reserved == right->reserved;
}

int c43_ref_zero(const struct fwlab_c43_opaque_ref *reference)
{
    return reference != NULL && reference->word[0] == 0 &&
           reference->word[1] == 0;
}

int c43_ref_equal(
    const struct fwlab_c43_opaque_ref *left,
    const struct fwlab_c43_opaque_ref *right)
{
    return left != NULL && right != NULL &&
           left->word[0] == right->word[0] &&
           left->word[1] == right->word[1];
}

void c43_profile_fixed(struct fwlab_nvme_profile *profile)
{
    fwlab_nvme_profile_fixed(profile);
}

int c43_profile_is_fixed(const struct fwlab_nvme_profile *profile)
{
    struct fwlab_nvme_profile fixed;

    if (!fwlab_nvme_profile_valid(profile)) {
        return 0;
    }
    fwlab_nvme_profile_fixed(&fixed);
    return memcmp(profile, &fixed, sizeof(fixed)) == 0;
}

int fwlab_c43_graph_config_valid(
    const struct fwlab_c43_graph_config *config)
{
    return config != NULL && config->version == FWLAB_C43_GRAPH_VERSION &&
           config->size == sizeof(*config) && config->reserved_header == 0 &&
           c43_profile_is_fixed(&config->profile) &&
           config->command_capacity == FWLAB_C43_MAX_COMMANDS &&
           config->actions_per_command == FWLAB_C43_ACTIONS_PER_COMMAND &&
           config->queue_mailbox_capacity == 8 &&
           config->target_mailbox_capacity == 4 &&
           config->block_mailbox_capacity == 20 &&
           config->dma_mailbox_capacity == 0 &&
           config->service_gap_maximum == FWLAB_C43_SERVICE_GAP_MAXIMUM &&
           config->ordinary_progress_maximum == FWLAB_C43_PROGRESS_MAXIMUM &&
           config->control_progress_maximum ==
               FWLAB_C43_CONTROL_PROGRESS_MAXIMUM &&
           config->safety_generation != 0 &&
           config->reserved_alignment == 0 && config->instance_nonce != 0 &&
           config->controller_epoch != 0 && config->reserved0 == 0 &&
           counter_seed_valid(&config->command_uid) &&
           counter_seed_valid(&config->action_uid) &&
           counter_seed_valid(&config->transaction_uid) &&
           counter_seed_valid(&config->lease_uid) &&
           counter_seed_valid(&config->consume_uid) &&
           counter_seed_valid(&config->finalizer_uid) &&
           c43_bytes_zero(config->reserved1, sizeof(config->reserved1));
}

static int providers_valid(const struct fwlab_c43_graph_providers *providers)
{
    return providers != NULL &&
           providers->version == FWLAB_C43_PROVIDER_BUNDLE_VERSION &&
           providers->size == sizeof(*providers) && providers->reserved0 == 0 &&
           fwlab_c43_queue_effect_port_valid(&providers->queue) &&
           fwlab_c43_target_resolver_port_valid(&providers->target) &&
           fwlab_c43_block_action_port_valid(&providers->block) &&
           providers->block.capability_bits ==
               FWLAB_C43_BLOCK_CAP_VALIDATION_ONLY &&
           providers->dma_generation != 0 && providers->dma_bound == 0 &&
           c43_bytes_zero(providers->reserved, sizeof(providers->reserved));
}

int c43_ranges_overlap(
    const void *left,
    size_t left_size,
    const void *right,
    size_t right_size)
{
    const uintptr_t left_start = (uintptr_t)left;
    const uintptr_t right_start = (uintptr_t)right;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left == NULL || right == NULL || left_size == 0 || right_size == 0) {
        return 0;
    }
    if (left_start > UINTPTR_MAX - (left_size - 1) ||
        right_start > UINTPTR_MAX - (right_size - 1)) {
        return 1;
    }
    left_end = left_start + left_size - 1;
    right_end = right_start + right_size - 1;
    return left_start <= right_end && right_start <= left_end;
}

static int providers_overlap_region(
    const struct fwlab_c43_graph_providers *providers,
    const void *region,
    size_t region_size)
{
    return c43_ranges_overlap(providers->queue.ops,
                              sizeof(*providers->queue.ops), region,
                              region_size) ||
           c43_ranges_overlap(providers->target.ops,
                              sizeof(*providers->target.ops), region,
                              region_size) ||
           c43_ranges_overlap(providers->block.ops,
                              sizeof(*providers->block.ops), region,
                              region_size) ||
           c43_ranges_overlap(providers->queue.context, 1, region,
                              region_size) ||
           c43_ranges_overlap(providers->target.context, 1, region,
                              region_size) ||
           c43_ranges_overlap(providers->block.context, 1, region,
                              region_size);
}

size_t fwlab_c43_graph_arena_size(
    const struct fwlab_c43_graph_config *config)
{
    if (!fwlab_c43_graph_config_valid(config)) {
        return 0;
    }
    return sizeof(struct fwlab_c43_graph);
}

size_t fwlab_c43_graph_arena_alignment(void)
{
    return _Alignof(struct fwlab_c43_graph);
}

enum fwlab_c43_graph_result fwlab_c43_graph_init(
    void *arena,
    size_t arena_size,
    const struct fwlab_c43_graph_config *config,
    const struct fwlab_c43_graph_providers *providers,
    struct fwlab_c43_graph **graph)
{
    struct fwlab_c43_graph *local;
    struct fwlab_c43_graph_config config_copy;
    struct fwlab_c43_graph_providers providers_copy;
    uint32_t index;

    if (arena == NULL || graph == NULL ||
        !fwlab_c43_graph_config_valid(config) || !providers_valid(providers) ||
        arena_size != sizeof(*local) ||
        (uintptr_t)arena % fwlab_c43_graph_arena_alignment() != 0 ||
        c43_ranges_overlap(arena, arena_size, config, sizeof(*config)) ||
        c43_ranges_overlap(arena, arena_size, providers,
                           sizeof(*providers)) ||
        c43_ranges_overlap(arena, arena_size, graph, sizeof(*graph)) ||
        c43_ranges_overlap(graph, sizeof(*graph), config, sizeof(*config)) ||
        c43_ranges_overlap(graph, sizeof(*graph), providers,
                           sizeof(*providers)) ||
        providers_overlap_region(providers, arena, arena_size) ||
        providers_overlap_region(providers, graph, sizeof(*graph))) {
        return FWLAB_C43_GRAPH_INVALID;
    }
    config_copy = *config;
    providers_copy = *providers;
    memset(arena, 0, arena_size);
    local = arena;
    local->magic = FWLAB_C43_INTERNAL_MAGIC;
    local->config = config_copy;
    local->queue_ops = *providers_copy.queue.ops;
    local->target_ops = *providers_copy.target.ops;
    local->block_ops = *providers_copy.block.ops;
    local->providers = providers_copy;
    local->providers.queue.ops = &local->queue_ops;
    local->providers.target.ops = &local->target_ops;
    local->providers.block.ops = &local->block_ops;
    counter_cursor_init(&local->command_uid, &config_copy.command_uid);
    counter_cursor_init(&local->action_uid, &config_copy.action_uid);
    counter_cursor_init(&local->transaction_uid,
                        &config_copy.transaction_uid);
    counter_cursor_init(&local->lease_uid, &config_copy.lease_uid);
    counter_cursor_init(&local->consume_uid, &config_copy.consume_uid);
    counter_cursor_init(&local->finalizer_uid, &config_copy.finalizer_uid);
    local->observer.version = FWLAB_C43_GRAPH_VERSION;
    local->observer.size = sizeof(local->observer);
    local->observer.controller_epoch = config_copy.controller_epoch;
    local->observer.instance_nonce = config_copy.instance_nonce;
    local->observer.provider_generation[0] = providers_copy.queue.generation;
    local->observer.provider_generation[1] = providers_copy.dma_generation;
    local->observer.provider_generation[2] = providers_copy.block.generation;
    local->observer.provider_generation[3] = providers_copy.target.generation;
    for (index = 0; index < FWLAB_C43_MAX_COMMANDS; ++index) {
        local->observer.commands[index].phase = FWLAB_C43_PHASE_FREE;
        local->observer.commands[index].publication =
            FWLAB_C43_PUBLICATION_ELIGIBLE;
    }
    *graph = local;
    return FWLAB_C43_GRAPH_OK;
}

int c43_graph_valid(const struct fwlab_c43_graph *graph)
{
    return graph != NULL && graph->magic == FWLAB_C43_INTERNAL_MAGIC &&
           fwlab_c43_graph_config_valid(&graph->config) &&
           providers_valid(&graph->providers) &&
           c43_reservation_state_valid(graph) &&
           c43_phase4_state_valid(graph) &&
           fwlab_c43_graph_observer_valid(&graph->observer);
}

enum fwlab_c43_graph_result fwlab_c43_graph_observer_read(
    const struct fwlab_c43_graph *graph,
    struct fwlab_c43_graph_observer *observer)
{
    if (!c43_graph_valid(graph) || observer == NULL) {
        return FWLAB_C43_GRAPH_INVALID;
    }
    if (c43_ranges_overlap(graph, sizeof(*graph), observer,
                           sizeof(*observer))) {
        return FWLAB_C43_GRAPH_INVALID;
    }
    *observer = graph->observer;
    return FWLAB_C43_GRAPH_OK;
}
