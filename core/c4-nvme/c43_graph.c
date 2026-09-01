/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_internal.h"

#include <string.h>

_Static_assert(FWLAB_C43_PHASE_RETIRED_TOMBSTONE == 13,
               "C4.3 observer must retain exactly 14 phases");
_Static_assert(FWLAB_C43_MAX_COMMANDS * FWLAB_C43_ACTIONS_PER_COMMAND ==
                   FWLAB_C43_MAX_ACTIONS,
               "C4.3 action capacity mismatch");

static int prepare_key_structural_valid(
    const struct fwlab_hif_prepare_key *key)
{
    return key != NULL && key->version == FWLAB_HIF_COMMAND_PORT_VERSION &&
           key->size == sizeof(*key) && key->reserved0 == 0 &&
           c43_origin_valid(&key->origin) && key->client_uid != 0 &&
           key->instance_nonce != 0 && key->controller_epoch != 0 &&
           key->client_generation != 0 &&
           (key->queue_class == FWLAB_NVME_QUEUE_ADMIN ||
            key->queue_class == FWLAB_NVME_QUEUE_IO) &&
           key->worst_case_actions == FWLAB_C43_ACTIONS_PER_COMMAND &&
           c43_bytes_zero(key->reserved1, sizeof(key->reserved1));
}

int c43_prepare_key_valid(
    const struct fwlab_c43_graph_config *config,
    const struct fwlab_hif_prepare_key *key)
{
    return config != NULL && prepare_key_structural_valid(key) &&
           key->instance_nonce == config->instance_nonce &&
           key->controller_epoch == config->controller_epoch;
}

static int prepare_key_identity_equal(
    const struct fwlab_hif_prepare_key *left,
    const struct fwlab_hif_prepare_key *right)
{
    return left->instance_nonce == right->instance_nonce &&
           left->controller_epoch == right->controller_epoch &&
           left->client_uid == right->client_uid &&
           left->client_generation == right->client_generation;
}

static int prepare_key_equal(
    const struct fwlab_hif_prepare_key *left,
    const struct fwlab_hif_prepare_key *right)
{
    return prepare_key_identity_equal(left, right) &&
           c43_origin_equal(&left->origin, &right->origin) &&
           left->queue_class == right->queue_class &&
           left->worst_case_actions == right->worst_case_actions;
}

static void prepare_key_copy(
    struct fwlab_hif_prepare_key *output,
    const struct fwlab_hif_prepare_key *input)
{
    memset(output, 0, sizeof(*output));
    output->version = input->version;
    output->size = input->size;
    output->origin = input->origin;
    output->client_uid = input->client_uid;
    output->instance_nonce = input->instance_nonce;
    output->controller_epoch = input->controller_epoch;
    output->client_generation = input->client_generation;
    output->queue_class = input->queue_class;
    output->worst_case_actions = input->worst_case_actions;
}

static int cursor_valid(
    const struct c43_counter_cursor *cursor,
    const struct fwlab_c43_counter_seed *seed)
{
    if (cursor == NULL || seed == NULL ||
        cursor->maximum != seed->maximum ||
        !c43_bytes_zero(cursor->reserved, sizeof(cursor->reserved))) {
        return 0;
    }
    if (cursor->exhausted) {
        return cursor->exhausted == 1 && cursor->next == 0;
    }
    return cursor->next >= seed->next && cursor->next != 0 &&
           cursor->next <= cursor->maximum;
}

static int cursor_can_take(
    const struct c43_counter_cursor *cursor,
    uint64_t count)
{
    return cursor != NULL && !cursor->exhausted && count != 0 &&
           cursor->next != 0 && cursor->next <= cursor->maximum &&
           count - 1 <= cursor->maximum - cursor->next;
}

static int cursor_consumed(
    const struct c43_counter_cursor *cursor,
    const struct fwlab_c43_counter_seed *seed,
    uint64_t value)
{
    return value >= seed->next && value <= cursor->maximum &&
           (cursor->exhausted || value < cursor->next);
}

static uint64_t cursor_take(
    struct c43_counter_cursor *cursor,
    uint64_t count)
{
    const uint64_t first = cursor->next;

    if (count - 1 == cursor->maximum - cursor->next) {
        cursor->next = 0;
        cursor->exhausted = 1;
    } else {
        cursor->next += count;
    }
    return first;
}

static int prepared_valid(
    const struct fwlab_c43_graph *graph,
    const struct c43_command_record *record)
{
    uint32_t index;

    if (!c43_prepare_key_valid(&graph->config, &record->key) ||
        !c43_handle_valid(&record->prepared.handle) ||
        !c43_origin_valid(&record->prepared.origin) ||
        record->prepared.reservation_uid == 0 ||
        record->prepared.generation != graph->config.safety_generation ||
        record->prepared.reserved != 0 ||
        record->prepared.handle.instance_nonce !=
            graph->config.instance_nonce ||
        record->prepared.handle.controller_epoch !=
            graph->config.controller_epoch ||
        record->prepared.handle.generation !=
            graph->config.safety_generation ||
        !c43_origin_equal(&record->prepared.origin, &record->key.origin) ||
        record->transaction_uid != record->prepared.reservation_uid ||
        record->lease_uid == 0 || record->consume_uid == 0 ||
        record->finalizer_uid == 0 ||
        record->reservation_credit_mask != FWLAB_C43_CREDIT_ALL ||
        !c43_bytes_zero(record->reserved0, sizeof(record->reserved0)) ||
        !c43_bytes_zero(record->reserved1, sizeof(record->reserved1))) {
        return 0;
    }
    for (index = 0; index < FWLAB_C43_ACTIONS_PER_COMMAND; ++index) {
        const struct c43_action_record *action = &record->actions[index];

        if (action->action_uid == 0 ||
            action->action_uid != record->actions[0].action_uid + index ||
            action->generation != graph->config.safety_generation ||
            action->ordinal != index ||
            action->state != C43_ACTION_RECORD_RESERVED ||
            !c43_bytes_zero(action->reserved, sizeof(action->reserved))) {
            return 0;
        }
    }
    return 1;
}

static int observer_matches_record(
    const struct c43_command_record *record,
    const struct fwlab_c43_command_observer *observer)
{
    return observer->in_use == 1 &&
           c43_handle_equal(&observer->handle, &record->prepared.handle) &&
           c43_origin_equal(&observer->origin, &record->prepared.origin) &&
           observer->transaction_uid == record->transaction_uid &&
           observer->phase == FWLAB_C43_PHASE_PREPARED &&
           observer->terminal_winner == FWLAB_C43_WINNER_NONE &&
           observer->publication == FWLAB_C43_PUBLICATION_ELIGIBLE &&
           observer->required_witness_mask == 0 &&
           observer->satisfied_witness_mask == 0 &&
           observer->action_count == FWLAB_C43_ACTIONS_PER_COMMAND &&
           observer->success_eligible == 0 &&
           observer->reservation_credit_mask == FWLAB_C43_CREDIT_ALL &&
           observer->first_action_uid == record->actions[0].action_uid &&
           observer->action_generation == record->actions[0].generation &&
           observer->provider_generation_current == 1;
}

int c43_reservation_state_valid(const struct fwlab_c43_graph *graph)
{
    uint32_t active_commands = 0;
    uint32_t active_actions = 0;
    uint32_t left;

    if (graph == NULL ||
        !cursor_valid(&graph->command_uid, &graph->config.command_uid) ||
        !cursor_valid(&graph->action_uid, &graph->config.action_uid) ||
        !cursor_valid(&graph->transaction_uid,
                      &graph->config.transaction_uid) ||
        !cursor_valid(&graph->lease_uid, &graph->config.lease_uid) ||
        !cursor_valid(&graph->consume_uid, &graph->config.consume_uid) ||
        !cursor_valid(&graph->finalizer_uid, &graph->config.finalizer_uid)) {
        return 0;
    }
    for (left = 0; left < FWLAB_C43_MAX_COMMANDS; ++left) {
        const struct c43_command_record *record = &graph->commands[left];
        uint32_t right;

        if (!record->in_use) {
            if (!c43_bytes_zero(record, sizeof(*record)) ||
                graph->observer.commands[left].in_use != 0) {
                return 0;
            }
            continue;
        }
        if (record->in_use != 1 || !prepared_valid(graph, record) ||
            !observer_matches_record(record,
                                     &graph->observer.commands[left])) {
            return 0;
        }
        for (right = 0; right < left; ++right) {
            const struct c43_command_record *prior = &graph->commands[right];
            uint32_t left_action;
            uint32_t right_action;

            if (prior->in_use &&
                (prepare_key_identity_equal(&record->key, &prior->key) ||
                 c43_origin_equal(&record->key.origin, &prior->key.origin) ||
                 c43_handle_equal(&record->prepared.handle,
                                  &prior->prepared.handle) ||
                 record->transaction_uid == prior->transaction_uid ||
                 record->lease_uid == prior->lease_uid ||
                 record->consume_uid == prior->consume_uid ||
                 record->finalizer_uid == prior->finalizer_uid)) {
                return 0;
            }
            if (prior->in_use) {
                for (left_action = 0;
                     left_action < FWLAB_C43_ACTIONS_PER_COMMAND;
                     ++left_action) {
                    for (right_action = 0;
                         right_action < FWLAB_C43_ACTIONS_PER_COMMAND;
                         ++right_action) {
                        if (record->actions[left_action].action_uid ==
                            prior->actions[right_action].action_uid) {
                            return 0;
                        }
                    }
                }
            }
        }
        if (!cursor_consumed(&graph->command_uid,
                             &graph->config.command_uid,
                             record->prepared.handle.command_uid) ||
            !cursor_consumed(&graph->action_uid,
                             &graph->config.action_uid,
                             record->actions[0].action_uid) ||
            !cursor_consumed(&graph->action_uid,
                             &graph->config.action_uid,
                             record->actions[
                                 FWLAB_C43_ACTIONS_PER_COMMAND - 1].action_uid) ||
            !cursor_consumed(&graph->transaction_uid,
                             &graph->config.transaction_uid,
                             record->transaction_uid) ||
            !cursor_consumed(&graph->lease_uid, &graph->config.lease_uid,
                             record->lease_uid) ||
            !cursor_consumed(&graph->consume_uid,
                             &graph->config.consume_uid,
                             record->consume_uid) ||
            !cursor_consumed(&graph->finalizer_uid,
                             &graph->config.finalizer_uid,
                             record->finalizer_uid)) {
            return 0;
        }
        ++active_commands;
        active_actions += FWLAB_C43_ACTIONS_PER_COMMAND;
    }
    return graph->observer.active_commands == active_commands &&
           graph->observer.active_actions == active_actions;
}

static void prepare_result_set(
    struct fwlab_hif_prepare_result *result,
    uint32_t disposition,
    const struct fwlab_hif_prepared_token *prepared)
{
    struct fwlab_hif_prepare_result local;

    memset(&local, 0, sizeof(local));
    local.version = FWLAB_HIF_COMMAND_PORT_VERSION;
    local.size = sizeof(local);
    local.disposition = disposition;
    if (prepared != NULL) {
        local.prepared = *prepared;
    }
    memcpy(result, &local, sizeof(local));
}

static enum fwlab_c43_graph_result existing_key_result(
    const struct fwlab_c43_graph *graph,
    const struct fwlab_hif_prepare_key *key,
    uint32_t *exact_index)
{
    uint32_t index;

    for (index = 0; index < FWLAB_C43_MAX_COMMANDS; ++index) {
        const struct c43_command_record *record = &graph->commands[index];

        if (!record->in_use) {
            continue;
        }
        if (prepare_key_identity_equal(&record->key, key)) {
            if (!prepare_key_equal(&record->key, key)) {
                return FWLAB_C43_GRAPH_POISONED;
            }
            *exact_index = index;
            return FWLAB_C43_GRAPH_OK;
        }
        if (c43_origin_equal(&record->key.origin, &key->origin)) {
            return FWLAB_C43_GRAPH_POISONED;
        }
    }
    return FWLAB_C43_GRAPH_STALE;
}

enum fwlab_c43_graph_result fwlab_c43_graph_prepare_start(
    struct fwlab_c43_graph *graph,
    const struct fwlab_hif_prepare_key *key,
    struct fwlab_hif_prepare_result *result)
{
    struct c43_command_record record = {0};
    struct fwlab_c43_command_observer observer = {0};
    struct fwlab_hif_prepare_result local_result = {0};
    uint64_t command_uid;
    uint64_t action_uid;
    uint64_t transaction_uid;
    uint32_t free_index = FWLAB_C43_MAX_COMMANDS;
    uint32_t exact_index = FWLAB_C43_MAX_COMMANDS;
    uint32_t index;
    enum fwlab_c43_graph_result existing;

    if (!c43_graph_valid(graph) || result == NULL ||
        !prepare_key_structural_valid(key)) {
        return FWLAB_C43_GRAPH_INVALID;
    }
    if (c43_ranges_overlap(graph, sizeof(*graph), key, sizeof(*key)) ||
        c43_ranges_overlap(graph, sizeof(*graph), result, sizeof(*result)) ||
        c43_ranges_overlap(key, sizeof(*key), result, sizeof(*result))) {
        return FWLAB_C43_GRAPH_INVALID;
    }
    if (key->instance_nonce != graph->config.instance_nonce ||
        key->controller_epoch != graph->config.controller_epoch) {
        return FWLAB_C43_GRAPH_STALE;
    }
    existing = existing_key_result(graph, key, &exact_index);
    if (existing == FWLAB_C43_GRAPH_OK) {
        return FWLAB_C43_GRAPH_IN_PROGRESS;
    }
    if (existing == FWLAB_C43_GRAPH_POISONED) {
        return existing;
    }
    for (index = 0; index < FWLAB_C43_MAX_COMMANDS; ++index) {
        if (!graph->commands[index].in_use) {
            free_index = index;
            break;
        }
    }
    if (free_index == FWLAB_C43_MAX_COMMANDS) {
        prepare_result_set(&local_result, FWLAB_HIF_PREPARE_BACKPRESSURE,
                           NULL);
        memcpy(result, &local_result, sizeof(local_result));
        return FWLAB_C43_GRAPH_OK;
    }
    if (!cursor_can_take(&graph->command_uid, 1) ||
        !cursor_can_take(&graph->action_uid,
                         FWLAB_C43_ACTIONS_PER_COMMAND) ||
        !cursor_can_take(&graph->transaction_uid, 1) ||
        !cursor_can_take(&graph->lease_uid, 1) ||
        !cursor_can_take(&graph->consume_uid, 1) ||
        !cursor_can_take(&graph->finalizer_uid, 1)) {
        return FWLAB_C43_GRAPH_COUNTER_EXHAUSTED;
    }

    command_uid = cursor_take(&graph->command_uid, 1);
    action_uid = cursor_take(&graph->action_uid,
                             FWLAB_C43_ACTIONS_PER_COMMAND);
    transaction_uid = cursor_take(&graph->transaction_uid, 1);
    record.lease_uid = cursor_take(&graph->lease_uid, 1);
    record.consume_uid = cursor_take(&graph->consume_uid, 1);
    record.finalizer_uid = cursor_take(&graph->finalizer_uid, 1);
    prepare_key_copy(&record.key, key);
    record.prepared.handle.instance_nonce = graph->config.instance_nonce;
    record.prepared.handle.command_uid = command_uid;
    record.prepared.handle.controller_epoch = graph->config.controller_epoch;
    record.prepared.handle.generation = graph->config.safety_generation;
    record.prepared.origin = key->origin;
    record.prepared.reservation_uid = transaction_uid;
    record.prepared.generation = graph->config.safety_generation;
    record.transaction_uid = transaction_uid;
    record.reservation_credit_mask = FWLAB_C43_CREDIT_ALL;
    record.in_use = 1;
    for (index = 0; index < FWLAB_C43_ACTIONS_PER_COMMAND; ++index) {
        record.actions[index].action_uid = action_uid + index;
        record.actions[index].generation = graph->config.safety_generation;
        record.actions[index].ordinal = (uint16_t)index;
        record.actions[index].state = C43_ACTION_RECORD_RESERVED;
    }

    observer.handle = record.prepared.handle;
    observer.origin = record.prepared.origin;
    observer.transaction_uid = transaction_uid;
    observer.phase = FWLAB_C43_PHASE_PREPARED;
    observer.publication = FWLAB_C43_PUBLICATION_ELIGIBLE;
    observer.action_count = FWLAB_C43_ACTIONS_PER_COMMAND;
    observer.in_use = 1;
    observer.reservation_credit_mask = FWLAB_C43_CREDIT_ALL;
    observer.first_action_uid = action_uid;
    observer.action_generation = graph->config.safety_generation;
    observer.provider_generation_current = 1;

    memcpy(&graph->commands[free_index], &record, sizeof(record));
    graph->observer.commands[free_index] = observer;
    ++graph->observer.active_commands;
    graph->observer.active_actions += FWLAB_C43_ACTIONS_PER_COMMAND;
    ++graph->observer.reserved_intent_credits;
    ++graph->observer.reserved_ready_credits;
    ++graph->observer.reserved_lease_credits;
    ++graph->observer.reserved_consume_credits;
    ++graph->observer.reserved_finalizer_credits;
    ++graph->observer.reserved_abort_credits;
    ++graph->observer.reserved_target_credits;
    ++graph->observer.reserved_queue_transaction_credits;
    ++graph->observer.reserved_block_intent_credits;
    prepare_result_set(&local_result, FWLAB_HIF_PREPARE_RESERVED,
                       &record.prepared);
    memcpy(result, &local_result, sizeof(local_result));
    return FWLAB_C43_GRAPH_OK;
}

enum fwlab_c43_graph_result fwlab_c43_graph_prepare_query(
    const struct fwlab_c43_graph *graph,
    const struct fwlab_hif_prepare_key *key,
    struct fwlab_hif_prepare_result *result)
{
    struct fwlab_hif_prepare_result local_result = {0};
    uint32_t exact_index = FWLAB_C43_MAX_COMMANDS;
    enum fwlab_c43_graph_result existing;

    if (!c43_graph_valid(graph) || result == NULL ||
        !prepare_key_structural_valid(key)) {
        return FWLAB_C43_GRAPH_INVALID;
    }
    if (c43_ranges_overlap(graph, sizeof(*graph), key, sizeof(*key)) ||
        c43_ranges_overlap(graph, sizeof(*graph), result, sizeof(*result)) ||
        c43_ranges_overlap(key, sizeof(*key), result, sizeof(*result))) {
        return FWLAB_C43_GRAPH_INVALID;
    }
    if (key->instance_nonce != graph->config.instance_nonce ||
        key->controller_epoch != graph->config.controller_epoch) {
        return FWLAB_C43_GRAPH_STALE;
    }
    existing = existing_key_result(graph, key, &exact_index);
    if (existing != FWLAB_C43_GRAPH_OK) {
        return existing;
    }
    prepare_result_set(&local_result, FWLAB_HIF_PREPARE_RESERVED,
                       &graph->commands[exact_index].prepared);
    memcpy(result, &local_result, sizeof(local_result));
    return FWLAB_C43_GRAPH_OK;
}

enum fwlab_c43_graph_result fwlab_c43_graph_step(
    struct fwlab_c43_graph *graph,
    uint32_t budget,
    struct fwlab_c43_step_result *result)
{
    if (!c43_graph_valid(graph) || result == NULL || budget == 0 ||
        budget > graph->config.ordinary_progress_maximum) {
        return FWLAB_C43_GRAPH_INVALID;
    }
    if (c43_ranges_overlap(graph, sizeof(*graph), result, sizeof(*result))) {
        return FWLAB_C43_GRAPH_INVALID;
    }
    /* Phase 2 reserves complete records; policy/action behavior comes later. */
    return FWLAB_C43_GRAPH_NOT_IMPLEMENTED;
}
