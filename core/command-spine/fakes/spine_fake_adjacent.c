/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fakes/spine_fake_adjacent.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int handle_equal(
    const struct fwlab_nvme_command_handle *left,
    const struct fwlab_nvme_command_handle *right)
{
    return left->instance_nonce == right->instance_nonce &&
           left->command_uid == right->command_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->generation == right->generation;
}

static int origin_equal(
    const struct fwlab_nvme_origin_token *left,
    const struct fwlab_nvme_origin_token *right)
{
    return left->word[0] == right->word[0] &&
           left->word[1] == right->word[1];
}

static int token_equal(
    const struct fwlab_host_action_token_v0 *left,
    const struct fwlab_host_action_token_v0 *right)
{
    return left->version == right->version && left->size == right->size &&
           left->type_tag == right->type_tag &&
           handle_equal(&left->command, &right->command) &&
           origin_equal(&left->origin, &right->origin) &&
           left->action_uid == right->action_uid &&
           left->generation == right->generation &&
           left->ordinal == right->ordinal && left->kind == right->kind;
}

static uint32_t action_witness(uint16_t kind)
{
    switch (kind) {
    case FWLAB_HOST_ACTION_V0_PAYLOAD_FILL:
        return FWLAB_HOST_WITNESS_V0_PAYLOAD_READY;
    case FWLAB_HOST_ACTION_V0_QUEUE_EFFECT:
        return FWLAB_HOST_WITNESS_V0_QUEUE_EFFECT;
    case FWLAB_HOST_ACTION_V0_TARGET_RESOLVE:
        return FWLAB_HOST_WITNESS_V0_TARGET_RESOLVED;
    case FWLAB_HOST_ACTION_V0_DMA_IN:
        return FWLAB_HOST_WITNESS_V0_DMA_IN_COMPLETE;
    case FWLAB_HOST_ACTION_V0_DMA_OUT:
        return FWLAB_HOST_WITNESS_V0_DMA_OUT_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_READ:
        return FWLAB_HOST_WITNESS_V0_BLOCK_READ_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_WRITE:
        return FWLAB_HOST_WITNESS_V0_BLOCK_WRITE_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_FLUSH:
        return FWLAB_HOST_WITNESS_V0_BLOCK_FLUSH_COMPLETE;
    case FWLAB_HOST_ACTION_V0_BLOCK_TRIM:
        return FWLAB_HOST_WITNESS_V0_BLOCK_TRIM_COMPLETE;
    default:
        return 0;
    }
}

static struct fwlab_spine_fake_action_v0 *find_action(
    struct fwlab_spine_fake_v0 *fake,
    const struct fwlab_host_action_token_v0 *token)
{
    uint32_t index;

    for (index = 0; index < FWLAB_SPINE_FAKE_V0_MAX_ACTIONS; ++index) {
        if (fake->action[index].occupied &&
            token_equal(&fake->action[index].token, token)) {
            return &fake->action[index];
        }
    }
    return NULL;
}

static struct fwlab_spine_fake_action_v0 *free_action(
    struct fwlab_spine_fake_v0 *fake)
{
    uint32_t index;

    for (index = 0; index < FWLAB_SPINE_FAKE_V0_MAX_ACTIONS; ++index) {
        if (!fake->action[index].occupied) {
            return &fake->action[index];
        }
    }
    return NULL;
}

static struct fwlab_spine_fake_expected_v0 *expected_for_token(
    struct fwlab_spine_fake_v0 *fake,
    const struct fwlab_host_action_token_v0 *token,
    const struct fwlab_host_action_argument_ref_v0 *argument,
    struct fwlab_spine_profile_argument_v0 *value)
{
    uint32_t index;

    for (index = 0; index < fake->expected_count; ++index) {
        struct fwlab_spine_fake_expected_v0 *expected =
            &fake->expected[index];

        if (expected->occupied &&
            handle_equal(&expected->command, &token->command) &&
            origin_equal(&expected->origin, &token->origin) &&
            token->ordinal < expected->action_count) {
            struct fwlab_spine_profile_argument_v0 current;

            if (memcmp(&expected->argument[token->ordinal], argument,
                       sizeof(*argument)) != 0) {
                return NULL;
            }
            memset(&current, 0, sizeof(current));
            if (expected->binding.argument_read(
                    expected->binding.adapter.context, argument,
                    &current) != FWLAB_SPINE_V0_OK ||
                memcmp(&current,
                       &expected->argument_value[token->ordinal],
                       sizeof(current)) != 0) {
                return NULL;
            }
            *value = current;
            return expected;
        }
    }
    return NULL;
}

static struct fwlab_spine_fake_behavior_v0 behavior_default(uint16_t kind)
{
    struct fwlab_spine_fake_behavior_v0 behavior;

    memset(&behavior, 0, sizeof(behavior));
    behavior.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
    behavior.normalized_outcome = FWLAB_SPINE_PROVIDER_V0_SUCCESS;
    if (kind == FWLAB_HOST_ACTION_V0_TARGET_RESOLVE) {
        behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_NONE;
        behavior.units_completed = 0;
    } else {
        behavior.effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL;
        behavior.units_completed = 1;
    }
    return behavior;
}

static enum fwlab_spine_result_v0 fake_submit(
    void *context,
    const struct fwlab_host_action_token_v0 *token,
    const struct fwlab_host_action_argument_ref_v0 *argument,
    struct fwlab_host_action_submit_result_v0 *result)
{
    struct fwlab_spine_fake_lane_v0 *lane = context;
    struct fwlab_spine_fake_v0 *fake;
    struct fwlab_spine_fake_behavior_v0 behavior;
    struct fwlab_spine_fake_action_v0 *record;
    struct fwlab_spine_fake_expected_v0 *expected;
    struct fwlab_spine_profile_argument_v0 argument_value;

    if (lane == NULL || lane->owner == NULL || token == NULL ||
        argument == NULL || result == NULL ||
        !fwlab_host_action_token_v0_valid(token) ||
        !fwlab_host_action_argument_ref_v0_valid(argument) ||
        token->kind != lane->kind || argument->kind != lane->kind ||
        token->ordinal != argument->ordinal || lane->close_acked) {
        return FWLAB_SPINE_V0_INVALID;
    }
    fake = lane->owner;
    memset(&argument_value, 0, sizeof(argument_value));
    expected = expected_for_token(fake, token, argument, &argument_value);
    if (expected == NULL) {
        return FWLAB_SPINE_V0_POISONED;
    }
    record = find_action(fake, token);
    if (record != NULL) {
        return FWLAB_SPINE_V0_POISONED;
    }
    behavior = fake->next[lane->kind - 1];
    if (behavior.terminal_kind == 0) {
        behavior = behavior_default(lane->kind);
    }
    if (behavior.backpressure_count != 0) {
        --fake->next[lane->kind - 1].backpressure_count;
        memset(result, 0, sizeof(*result));
        result->version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
        result->size = sizeof(*result);
        result->token = *token;
        result->disposition = FWLAB_HOST_ACTION_V0_BACKPRESSURE;
        return FWLAB_SPINE_V0_OK;
    }
    record = free_action(fake);
    if (record == NULL ||
        fake->observed_count >= FWLAB_SPINE_FAKE_V0_MAX_ACTIONS) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    memset(record, 0, sizeof(*record));
    record->occupied = 1;
    record->token = *token;
    record->argument = *argument;
    record->argument_value = argument_value;
    record->behavior = behavior;
    if (token->kind == FWLAB_HOST_ACTION_V0_PAYLOAD_FILL) {
        uint32_t actual_bytes = 0;
        enum fwlab_spine_result_v0 payload_result;

        if (argument_value.payload_bytes == 0 ||
            argument_value.payload_bytes > sizeof(record->payload)) {
            return FWLAB_SPINE_V0_POISONED;
        }
        payload_result = expected->binding.payload_read(
            expected->binding.adapter.context, argument, record->payload,
            argument_value.payload_bytes, &actual_bytes);
        if (payload_result != FWLAB_SPINE_V0_OK ||
            actual_bytes != argument_value.payload_bytes) {
            return FWLAB_SPINE_V0_POISONED;
        }
        record->payload_bytes = actual_bytes;
    }
    fake->observed[fake->observed_count++] = *token;
    ++fake->active_actions;
    memset(&fake->next[lane->kind - 1], 0,
           sizeof(fake->next[lane->kind - 1]));

    if (behavior.abort_candidate_present) {
        struct fwlab_spine_abort_candidate_v0 candidate;
        enum fwlab_spine_result_v0 appended;

        memset(&candidate, 0, sizeof(candidate));
        candidate.version = FWLAB_SPINE_LIFECYCLE_V0_VERSION;
        candidate.size = sizeof(candidate);
        candidate.abort_uid = behavior.abort_uid;
        candidate.resolver = *token;
        candidate.report = behavior.abort_report;
        candidate.target_present = behavior.target_present;
        if (behavior.target_present) {
            candidate.target = behavior.target;
        }
        appended = fwlab_spine_fake_v0_abort_candidate_append(
            fake, &candidate);
        if (appended != FWLAB_SPINE_V0_OK) {
            return appended;
        }
    }

    memset(result, 0, sizeof(*result));
    result->version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    result->size = sizeof(*result);
    result->token = *token;
    result->disposition = FWLAB_HOST_ACTION_V0_ACCEPTED;
    return behavior.lose_submit_response ? FWLAB_SPINE_V0_IN_PROGRESS
                                         : FWLAB_SPINE_V0_OK;
}

static void terminal_status(
    const struct fwlab_spine_fake_action_v0 *record,
    uint32_t state,
    struct fwlab_host_action_status_v0 *status)
{
    memset(status, 0, sizeof(*status));
    status->version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    status->size = sizeof(*status);
    status->token = record->token;
    status->state = state;
    if (record->cancelled) {
        status->terminal_kind = FWLAB_HOST_ACTION_V0_CANCELLED;
        status->effect = FWLAB_HOST_ACTION_V0_EFFECT_NONE;
        return;
    }
    status->terminal_kind = record->behavior.terminal_kind;
    status->effect = record->behavior.effect;
    status->units_completed = record->behavior.units_completed;
    status->fault_domain = record->behavior.fault_domain;
    status->fault_code = record->behavior.fault_code;
    if (status->terminal_kind == FWLAB_HOST_ACTION_V0_SUCCEEDED) {
        status->produced_witness_mask = action_witness(record->token.kind);
        status->fault_domain = 0;
        status->fault_code = 0;
    }
}

static uint32_t default_failure_outcome(uint16_t kind)
{
    switch (kind) {
    case FWLAB_HOST_ACTION_V0_DMA_IN:
    case FWLAB_HOST_ACTION_V0_DMA_OUT:
        return FWLAB_SPINE_PROVIDER_V0_TRANSFER_FAILURE;
    case FWLAB_HOST_ACTION_V0_BLOCK_READ:
        return FWLAB_SPINE_PROVIDER_V0_MEDIA_READ;
    case FWLAB_HOST_ACTION_V0_BLOCK_WRITE:
    case FWLAB_HOST_ACTION_V0_BLOCK_FLUSH:
        return FWLAB_SPINE_PROVIDER_V0_MEDIA_WRITE;
    default:
        return FWLAB_SPINE_PROVIDER_V0_RESOURCE_FAILURE;
    }
}

static enum fwlab_spine_result_v0 latch_result(
    struct fwlab_spine_fake_v0 *fake,
    struct fwlab_spine_fake_action_v0 *record,
    const struct fwlab_host_action_status_v0 *status)
{
    struct fwlab_spine_profile_argument_v0 argument_value;
    struct fwlab_spine_fake_expected_v0 *expected;
    uint32_t outcome;
    enum fwlab_spine_result_v0 result;

    memset(&argument_value, 0, sizeof(argument_value));
    expected = expected_for_token(fake, &record->token, &record->argument,
                                  &argument_value);
    if (expected == NULL) {
        return FWLAB_SPINE_V0_POISONED;
    }
    if (status->terminal_kind == FWLAB_HOST_ACTION_V0_SUCCEEDED) {
        outcome = FWLAB_SPINE_PROVIDER_V0_SUCCESS;
    } else if (status->terminal_kind == FWLAB_HOST_ACTION_V0_CANCELLED) {
        outcome = FWLAB_SPINE_PROVIDER_V0_CANCELLED;
    } else {
        outcome = record->behavior.normalized_outcome;
        if (outcome == 0 || outcome == FWLAB_SPINE_PROVIDER_V0_SUCCESS ||
            outcome == FWLAB_SPINE_PROVIDER_V0_CANCELLED) {
            outcome = default_failure_outcome(record->token.kind);
        }
    }
    result = expected->binding.result_latch(
        expected->binding.adapter.context, &record->argument, status,
        outcome, record->behavior.result_dword0);
    if (result == FWLAB_SPINE_V0_OK) {
        record->result_latched = 1;
    }
    return result;
}

static enum fwlab_spine_result_v0 fake_query(
    void *context,
    const struct fwlab_host_action_token_v0 *token,
    struct fwlab_host_action_status_v0 *status)
{
    struct fwlab_spine_fake_lane_v0 *lane = context;
    struct fwlab_spine_fake_action_v0 *record;

    if (lane == NULL || lane->owner == NULL || token == NULL ||
        status == NULL || token->kind != lane->kind) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = find_action(lane->owner, token);
    if (record == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (record->behavior.lose_submit_response && record->query_count == 0) {
        memset(status, 0, sizeof(*status));
        status->version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
        status->size = sizeof(*status);
        status->token = record->token;
        status->state = FWLAB_HOST_ACTION_V0_STATE_ACCEPTED;
        ++record->query_count;
        return FWLAB_SPINE_V0_OK;
    }
    if (record->behavior.wait_for_epoch_close && !lane->close_acked) {
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    if (record->query_count < record->behavior.terminal_delay) {
        ++record->query_count;
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    terminal_status(record, FWLAB_HOST_ACTION_V0_STATE_TERMINAL, status);
    return latch_result(lane->owner, record, status);
}

static enum fwlab_spine_result_v0 fake_cancel(
    void *context,
    const struct fwlab_host_action_token_v0 *token)
{
    struct fwlab_spine_fake_lane_v0 *lane = context;
    struct fwlab_spine_fake_action_v0 *record;

    if (lane == NULL || lane->owner == NULL || token == NULL ||
        token->kind != lane->kind) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = find_action(lane->owner, token);
    if (record == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    ++record->cancel_calls;
    if (record->behavior.wait_for_epoch_close && !lane->close_acked) {
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    if (record->cancel_calls <= record->behavior.cancel_delay) {
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    record->cancelled = 1;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_retire_start(
    void *context,
    const struct fwlab_host_action_token_v0 *token)
{
    struct fwlab_spine_fake_lane_v0 *lane = context;
    struct fwlab_spine_fake_action_v0 *record;

    if (lane == NULL || lane->owner == NULL || token == NULL ||
        token->kind != lane->kind) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = find_action(lane->owner, token);
    if (record == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    ++record->retire_start_calls;
    if (record->retire_start_calls <=
        record->behavior.retire_start_delay) {
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    record->retire_started = 1;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_retire_query(
    void *context,
    const struct fwlab_host_action_token_v0 *token,
    struct fwlab_host_action_status_v0 *status)
{
    struct fwlab_spine_fake_lane_v0 *lane = context;
    struct fwlab_spine_fake_action_v0 *record;

    if (lane == NULL || lane->owner == NULL || token == NULL ||
        status == NULL || token->kind != lane->kind) {
        return FWLAB_SPINE_V0_INVALID;
    }
    record = find_action(lane->owner, token);
    if (record == NULL || !record->retire_started) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    if (record->retire_query_count < record->behavior.retire_delay) {
        ++record->retire_query_count;
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    terminal_status(record, FWLAB_HOST_ACTION_V0_STATE_DRAINED, status);
    {
        enum fwlab_spine_result_v0 result =
            latch_result(lane->owner, record, status);

        if (result != FWLAB_SPINE_V0_OK) {
            return result;
        }
    }
    if (!record->drained) {
        record->drained = 1;
        if (lane->owner->active_actions != 0) {
            --lane->owner->active_actions;
        }
    }
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_epoch_close(
    void *context,
    uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch)
{
    struct fwlab_spine_fake_lane_v0 *lane = context;

    if (lane == NULL || lane->owner == NULL ||
        lifecycle_instance_nonce == 0 || old_execution_epoch == 0) {
        return FWLAB_SPINE_V0_INVALID;
    }
    ++lane->close_calls;
    if (lane->close_calls <= lane->close_in_progress) {
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    lane->close_acked = 1;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_epoch_quiescent(
    void *context,
    uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch,
    uint8_t *quiescent)
{
    struct fwlab_spine_fake_lane_v0 *lane = context;
    uint32_t index;

    if (lane == NULL || lane->owner == NULL || quiescent == NULL ||
        lifecycle_instance_nonce == 0 || old_execution_epoch == 0 ||
        !lane->close_acked) {
        return FWLAB_SPINE_V0_INVALID;
    }
    ++lane->quiescent_calls;
    *quiescent = 1;
    for (index = 0; index < FWLAB_SPINE_FAKE_V0_MAX_ACTIONS; ++index) {
        const struct fwlab_spine_fake_action_v0 *record =
            &lane->owner->action[index];

        if (record->occupied && record->token.kind == lane->kind &&
            !record->drained) {
            *quiescent = 0;
            break;
        }
    }
    return FWLAB_SPINE_V0_OK;
}

static const struct fwlab_host_action_driver_ops_v0 fake_ops = {
    .version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION,
    .size = sizeof(struct fwlab_host_action_driver_ops_v0),
    .submit = fake_submit,
    .query = fake_query,
    .cancel = fake_cancel,
    .retire_start = fake_retire_start,
    .retire_query = fake_retire_query,
    .epoch_close = fake_epoch_close,
    .epoch_quiescent = fake_epoch_quiescent,
};

void fwlab_spine_fake_v0_init(
    struct fwlab_spine_fake_v0 *fake,
    struct fwlab_host_action_driver_table_v0 *drivers)
{
    uint32_t index;

    if (fake == NULL || drivers == NULL) {
        return;
    }
    memset(fake, 0, sizeof(*fake));
    memset(drivers, 0, sizeof(*drivers));
    drivers->version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    drivers->size = sizeof(*drivers);
    drivers->entry_count = FWLAB_HOST_ACTION_V0_KIND_COUNT;
    for (index = 0; index < FWLAB_HOST_ACTION_V0_KIND_COUNT; ++index) {
        fake->lane[index].owner = fake;
        fake->lane[index].kind = (uint16_t)(index + 1);
        drivers->entry[index].version =
            FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
        drivers->entry[index].size = sizeof(drivers->entry[index]);
        drivers->entry[index].kind = (uint16_t)(index + 1);
        drivers->entry[index].generation = UINT64_C(1);
        drivers->entry[index].ops = &fake_ops;
        drivers->entry[index].context = &fake->lane[index];
    }
}

void fwlab_spine_fake_v0_set_next(
    struct fwlab_spine_fake_v0 *fake,
    uint16_t kind,
    const struct fwlab_spine_fake_behavior_v0 *behavior)
{
    if (fake == NULL || behavior == NULL || kind == 0 ||
        kind > FWLAB_HOST_ACTION_V0_KIND_COUNT) {
        return;
    }
    fake->next[kind - 1] = *behavior;
}

void fwlab_spine_fake_v0_set_close_delay(
    struct fwlab_spine_fake_v0 *fake,
    uint16_t kind,
    uint32_t attempts)
{
    if (fake == NULL || kind == 0 ||
        kind > FWLAB_HOST_ACTION_V0_KIND_COUNT) {
        return;
    }
    fake->lane[kind - 1].close_in_progress = attempts;
}

enum fwlab_spine_result_v0 fwlab_spine_fake_v0_expect_program(
    struct fwlab_spine_fake_v0 *fake,
    const struct fwlab_spine_profile_binding_v0 *binding,
    const struct fwlab_host_action_program_v0 *program)
{
    struct fwlab_spine_fake_expected_v0 *expected;
    uint32_t index;
    uint32_t action_index;

    if (fake == NULL || binding == NULL || program == NULL ||
        !fwlab_spine_profile_binding_v0_valid(binding) ||
        !fwlab_host_action_program_v0_valid(program)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    for (index = 0; index < fake->expected_count; ++index) {
        expected = &fake->expected[index];
        if (handle_equal(&expected->command, &program->command) ||
            origin_equal(&expected->origin, &program->origin)) {
            if (!handle_equal(&expected->command, &program->command) ||
                !origin_equal(&expected->origin, &program->origin) ||
                expected->action_count != program->action_count ||
                memcmp(&expected->binding, binding, sizeof(*binding)) != 0 ||
                memcmp(&expected->program, program, sizeof(*program)) != 0) {
                return FWLAB_SPINE_V0_POISONED;
            }
            for (action_index = 0; action_index < program->action_count;
                 ++action_index) {
                if (memcmp(&expected->argument[action_index],
                           &program->action[action_index].argument,
                           sizeof(expected->argument[action_index])) != 0) {
                    return FWLAB_SPINE_V0_POISONED;
                }
            }
            return FWLAB_SPINE_V0_OK;
        }
    }
    if (fake->expected_count >= FWLAB_SPINE_FAKE_V0_MAX_ACTIONS) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    expected = &fake->expected[fake->expected_count++];
    memset(expected, 0, sizeof(*expected));
    expected->occupied = 1;
    expected->command = program->command;
    expected->origin = program->origin;
    expected->binding = *binding;
    expected->program = *program;
    expected->action_count = program->action_count;
    for (action_index = 0; action_index < program->action_count;
         ++action_index) {
        expected->argument[action_index] =
            program->action[action_index].argument;
        if (binding->argument_read(
                binding->adapter.context,
                &program->action[action_index].argument,
                &expected->argument_value[action_index]) !=
            FWLAB_SPINE_V0_OK) {
            memset(expected, 0, sizeof(*expected));
            --fake->expected_count;
            return FWLAB_SPINE_V0_POISONED;
        }
    }
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 fake_relation_source(
    void *context,
    uint64_t abort_uid,
    const struct fwlab_host_action_token_v0 *resolver,
    struct fwlab_spine_abort_candidate_v0 *candidate,
    uint8_t *present)
{
    struct fwlab_spine_fake_v0 *fake = context;
    uint32_t index;

    if (fake == NULL || abort_uid == 0 || resolver == NULL ||
        candidate == NULL || present == NULL ||
        !fwlab_host_action_token_v0_valid(resolver)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    *present = 0;
    memset(candidate, 0, sizeof(*candidate));
    for (index = 0; index < FWLAB_SPINE_LIFECYCLE_V0_MAX_ABORTS; ++index) {
        struct fwlab_spine_fake_mailbox_v0 *mailbox = &fake->mailbox[index];

        if (!mailbox->occupied || mailbox->candidate.abort_uid != abort_uid) {
            continue;
        }
        if (mailbox->poisoned ||
            !token_equal(&mailbox->candidate.resolver, resolver)) {
            return FWLAB_SPINE_V0_POISONED;
        }
        *candidate = mailbox->candidate;
        *present = 1;
        return FWLAB_SPINE_V0_OK;
    }
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_spine_fake_v0_attach_relation_source(
    struct fwlab_spine_fake_v0 *fake,
    struct fwlab_spine_profile_binding_v0 *binding)
{
    if (fake == NULL || binding == NULL || binding->relation_sink == NULL ||
        binding->relation_context == NULL ||
        binding->relation_source != NULL ||
        binding->relation_source_context != NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    binding->relation_source = fake_relation_source;
    binding->relation_source_context = fake;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_spine_fake_v0_abort_candidate_append(
    struct fwlab_spine_fake_v0 *fake,
    const struct fwlab_spine_abort_candidate_v0 *candidate)
{
    struct fwlab_spine_fake_mailbox_v0 *free_mailbox = NULL;
    uint32_t index;

    if (fake == NULL || !fwlab_spine_abort_candidate_v0_valid(candidate)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    for (index = 0; index < FWLAB_SPINE_LIFECYCLE_V0_MAX_ABORTS; ++index) {
        struct fwlab_spine_fake_mailbox_v0 *mailbox = &fake->mailbox[index];

        if (!mailbox->occupied) {
            if (free_mailbox == NULL) {
                free_mailbox = mailbox;
            }
            continue;
        }
        if (mailbox->candidate.abort_uid != candidate->abort_uid) {
            continue;
        }
        if (memcmp(&mailbox->candidate, candidate, sizeof(*candidate)) == 0) {
            return mailbox->poisoned ? FWLAB_SPINE_V0_POISONED
                                     : FWLAB_SPINE_V0_OK;
        }
        mailbox->poisoned = 1;
        return FWLAB_SPINE_V0_POISONED;
    }
    if (free_mailbox == NULL) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    memset(free_mailbox, 0, sizeof(*free_mailbox));
    free_mailbox->occupied = 1;
    free_mailbox->candidate = *candidate;
    return FWLAB_SPINE_V0_OK;
}

uint32_t fwlab_spine_fake_v0_observed_count(
    const struct fwlab_spine_fake_v0 *fake)
{
    return fake == NULL ? 0 : fake->observed_count;
}

const struct fwlab_host_action_token_v0 *fwlab_spine_fake_v0_observed(
    const struct fwlab_spine_fake_v0 *fake,
    uint32_t index)
{
    if (fake == NULL || index >= fake->observed_count) {
        return NULL;
    }
    return &fake->observed[index];
}
