/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "spine_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SPINE_LIFECYCLE_MAGIC UINT64_C(0x5350494e45304231)

enum spine_action_phase {
    SPINE_ACTION_WAIT_DEP = 1,
    SPINE_ACTION_SUBMIT_READY = 2,
    SPINE_ACTION_SUBMIT_UNKNOWN = 3,
    SPINE_ACTION_ACCEPTED = 4,
    SPINE_ACTION_TERMINAL = 5,
    SPINE_ACTION_RETIRE_ISSUED = 6,
    SPINE_ACTION_DRAINING = 7,
    SPINE_ACTION_DRAINED = 8
};

enum spine_command_phase {
    SPINE_COMMAND_ACTIVE = 1,
    SPINE_COMMAND_INTENT = 2,
    SPINE_COMMAND_QUARANTINED = 3
};

struct spine_action_record {
    struct fwlab_host_action_desc_v0 description;
    struct fwlab_host_action_token_v0 token;
    struct fwlab_host_action_status_v0 terminal;
    uint32_t phase;
    uint8_t ever_accepted;
    uint8_t cancel_requested;
    uint8_t cancel_issued;
    uint8_t cancel_acked;
    uint8_t retire_issued;
    uint8_t retire_acked;
    uint8_t terminal_valid;
    uint8_t reserved0;
};

struct spine_abort_record {
    uint64_t abort_uid;
    struct fwlab_spine_abort_candidate_v0 candidate;
    uint32_t decision;
    uint32_t target_accepted_mask;
    uint8_t candidate_present;
    uint8_t target_bound;
    uint8_t sink_issued;
    uint8_t sink_acked;
    uint8_t abandoned;
    uint8_t superseded;
    uint8_t sink_failed;
    uint8_t reserved0;
    uint16_t target_index;
    uint16_t reserved1;
};

struct spine_command_record {
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_spine_profile_binding_v0 binding;
    struct fwlab_host_action_program_v0 program;
    struct spine_action_record action[FWLAB_HOST_ACTION_V0_MAX_ACTIONS];
    struct spine_abort_record abort;
    struct fwlab_nvme_completion_intent intent;
    uint64_t first_terminal_sequence;
    uint64_t incoming_abort_uid;
    uint32_t role;
    uint32_t phase;
    uint8_t occupied;
    uint8_t cancel_requested;
    uint8_t first_failure;
    uint8_t intent_valid;
    uint8_t profile_retire_issued;
    uint8_t profile_retired;
    uint8_t resolver_succeeded_before_close;
    uint8_t suppress_intent;
};

struct spine_driver_close_record {
    uint8_t close_issued;
    uint8_t close_acked;
    uint8_t quiescent;
    uint8_t reserved0;
};

struct spine_lifecycle {
    uint64_t magic;
    struct fwlab_host_lifecycle_config_v0 config;
    struct fwlab_host_action_driver_table_v0 drivers;
    struct spine_command_record command[FWLAB_SPINE_LIFECYCLE_V0_MAX_COMMANDS];
    struct spine_driver_close_record
        driver_close[FWLAB_HOST_ACTION_V0_KIND_COUNT];
    uint64_t next_command_uid;
    uint64_t next_action_uid;
    uint64_t next_abort_uid;
    uint64_t terminal_sequence;
    uint64_t close_sequence;
    uint32_t service_cursor;
    uint32_t active_commands;
    uint32_t retained_intents;
    uint8_t admission_closed;
    uint8_t close_started;
    uint8_t poisoned;
    uint8_t reserved0;
};

const uint64_t fwlab_spine_lifecycle_v0_symbol_owner =
    UINT64_C(0x4c49464543593030);

static int bytes_zero(const void *value, size_t size)
{
    const unsigned char *bytes = value;
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

static int binding_equal(
    const struct fwlab_spine_profile_binding_v0 *left,
    const struct fwlab_spine_profile_binding_v0 *right)
{
    return left->adapter.ops == right->adapter.ops &&
           left->adapter.context == right->adapter.context &&
           left->adapter.generation == right->adapter.generation &&
           left->adapter_instance_nonce == right->adapter_instance_nonce &&
           left->generation == right->generation &&
           left->argument_read == right->argument_read &&
           left->payload_read == right->payload_read &&
           left->result_latch == right->result_latch &&
           left->relation_source == right->relation_source &&
           left->relation_source_context == right->relation_source_context &&
           left->relation_sink == right->relation_sink &&
           left->relation_context == right->relation_context;
}

static struct spine_lifecycle *lifecycle_from(void *arena)
{
    struct spine_lifecycle *lifecycle = arena;

    if (lifecycle == NULL || lifecycle->magic != SPINE_LIFECYCLE_MAGIC) {
        return NULL;
    }
    return lifecycle;
}

static int uid_available(uint64_t next, uint64_t maximum, uint32_t count)
{
    if (count == 0) {
        return 1;
    }
    return next != 0 && next <= maximum &&
           (uint64_t)(count - 1) <= maximum - next;
}

int fwlab_spine_command_ticket_v0_valid(
    const struct fwlab_spine_command_ticket_v0 *ticket)
{
    return ticket != NULL &&
           ticket->version == FWLAB_SPINE_LIFECYCLE_V0_VERSION &&
           ticket->size == sizeof(*ticket) && ticket->reserved0 == 0 &&
           ticket->command.instance_nonce != 0 &&
           ticket->command.command_uid != 0 &&
           ticket->command.controller_epoch != 0 &&
           ticket->command.generation != 0 &&
           (ticket->origin.word[0] != 0 || ticket->origin.word[1] != 0) &&
           ticket->lifecycle_instance_nonce != 0 && ticket->ticket_uid != 0 &&
           ticket->execution_epoch != 0 && ticket->generation != 0 &&
           bytes_zero(ticket->reserved1, sizeof(ticket->reserved1));
}

int fwlab_spine_command_ticket_v0_equal(
    const struct fwlab_spine_command_ticket_v0 *left,
    const struct fwlab_spine_command_ticket_v0 *right)
{
    return left != NULL && right != NULL &&
           fwlab_spine_command_ticket_v0_valid(left) &&
           fwlab_spine_command_ticket_v0_valid(right) &&
           handle_equal(&left->command, &right->command) &&
           origin_equal(&left->origin, &right->origin) &&
           left->lifecycle_instance_nonce == right->lifecycle_instance_nonce &&
           left->ticket_uid == right->ticket_uid &&
           left->relation_uid == right->relation_uid &&
           left->execution_epoch == right->execution_epoch &&
           left->generation == right->generation;
}

int fwlab_spine_profile_binding_v0_valid(
    const struct fwlab_spine_profile_binding_v0 *binding)
{
    return binding != NULL &&
           binding->version == FWLAB_SPINE_LIFECYCLE_V0_VERSION &&
           binding->size == sizeof(*binding) && binding->reserved0 == 0 &&
           fwlab_host_profile_adapter_v0_valid(&binding->adapter) &&
           binding->adapter_instance_nonce != 0 && binding->generation != 0 &&
           binding->adapter.generation == binding->generation &&
           binding->reserved1 == 0 && binding->argument_read != NULL &&
           binding->payload_read != NULL && binding->result_latch != NULL &&
           ((binding->relation_source == NULL) ==
            (binding->relation_source_context == NULL)) &&
           ((binding->relation_sink == NULL) ==
            (binding->relation_context == NULL)) &&
           bytes_zero(binding->reserved2, sizeof(binding->reserved2));
}

int fwlab_spine_abort_candidate_v0_valid(
    const struct fwlab_spine_abort_candidate_v0 *candidate)
{
    if (candidate == NULL ||
        candidate->version != FWLAB_SPINE_LIFECYCLE_V0_VERSION ||
        candidate->size != sizeof(*candidate) || candidate->reserved0 != 0 ||
        candidate->abort_uid == 0 ||
        !fwlab_host_action_token_v0_valid(&candidate->resolver) ||
        candidate->report < FWLAB_SPINE_ABORT_REPORT_V0_NOT_FOUND ||
        candidate->report > FWLAB_SPINE_ABORT_REPORT_V0_SUPERSEDED ||
        candidate->target_present > 1 ||
        !bytes_zero(candidate->reserved1, sizeof(candidate->reserved1)) ||
        !bytes_zero(candidate->reserved2, sizeof(candidate->reserved2))) {
        return 0;
    }
    if (candidate->report == FWLAB_SPINE_ABORT_REPORT_V0_FOUND) {
        return candidate->target_present == 1 &&
               fwlab_spine_command_ticket_v0_valid(&candidate->target);
    }
    return candidate->target_present == 0 &&
           bytes_zero(&candidate->target, sizeof(candidate->target));
}

size_t fwlab_spine_lifecycle_v0_arena_size(void)
{
    return sizeof(struct spine_lifecycle);
}

size_t fwlab_spine_lifecycle_v0_arena_alignment(void)
{
    return _Alignof(struct spine_lifecycle);
}

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_init(
    void *arena,
    size_t arena_size,
    const struct fwlab_host_lifecycle_config_v0 *config,
    const struct fwlab_host_action_driver_table_v0 *drivers)
{
    struct spine_lifecycle *lifecycle = arena;
    struct fwlab_host_lifecycle_config_v0 config_copy;
    struct fwlab_host_action_driver_table_v0 drivers_copy;

    if (arena == NULL || config == NULL || drivers == NULL ||
        arena_size != sizeof(*lifecycle) ||
        ((uintptr_t)arena % _Alignof(struct spine_lifecycle)) != 0 ||
        !fwlab_host_lifecycle_config_v0_valid(config) ||
        !fwlab_host_action_driver_table_v0_valid(drivers) ||
        config->command_capacity > FWLAB_SPINE_LIFECYCLE_V0_MAX_COMMANDS) {
        return FWLAB_SPINE_V0_INVALID;
    }
    config_copy = *config;
    drivers_copy = *drivers;
    memset(lifecycle, 0, sizeof(*lifecycle));
    lifecycle->magic = SPINE_LIFECYCLE_MAGIC;
    lifecycle->config = config_copy;
    lifecycle->drivers = drivers_copy;
    lifecycle->next_command_uid = config_copy.command_uid.next;
    lifecycle->next_action_uid = config_copy.action_uid.next;
    lifecycle->next_abort_uid = config_copy.abort_uid.next;
    return FWLAB_SPINE_V0_OK;
}

static int program_matches_binding(
    const struct fwlab_host_action_program_v0 *program,
    const struct fwlab_spine_profile_binding_v0 *binding)
{
    uint32_t index;

    if (program->completion_recipe.adapter_instance_nonce !=
            binding->adapter_instance_nonce ||
        program->completion_recipe.generation != binding->generation) {
        return 0;
    }
    for (index = 0; index < program->action_count; ++index) {
        if (program->action[index].argument.adapter_instance_nonce !=
                binding->adapter_instance_nonce ||
            program->action[index].argument.generation !=
                binding->generation) {
            return 0;
        }
    }
    return 1;
}

static int role_valid(
    const struct fwlab_host_action_program_v0 *program,
    const struct fwlab_spine_profile_binding_v0 *binding,
    uint32_t role)
{
    uint32_t index;
    uint32_t targets = 0;

    for (index = 0; index < program->action_count; ++index) {
        if (program->action[index].kind ==
            FWLAB_HOST_ACTION_V0_TARGET_RESOLVE) {
            ++targets;
        }
    }
    if (role == FWLAB_SPINE_ROLE_V0_NORMAL) {
        return targets == 0 && binding->relation_source == NULL &&
               binding->relation_sink == NULL;
    }
    return role == FWLAB_SPINE_ROLE_V0_ABORT && targets == 1 &&
           program->action_count == 1 &&
           binding->relation_source != NULL && binding->relation_sink != NULL;
}

static struct spine_command_record *find_ticket(
    struct spine_lifecycle *lifecycle,
    const struct fwlab_spine_command_ticket_v0 *ticket)
{
    uint32_t index;

    for (index = 0; index < lifecycle->config.command_capacity; ++index) {
        if (lifecycle->command[index].occupied &&
            fwlab_spine_command_ticket_v0_equal(
                &lifecycle->command[index].ticket, ticket)) {
            return &lifecycle->command[index];
        }
    }
    return NULL;
}

static enum fwlab_spine_result_v0 poison_lifecycle(
    struct spine_lifecycle *lifecycle)
{
    uint32_t index;

    lifecycle->poisoned = 1;
    lifecycle->admission_closed = 1;
    for (index = 0; index < lifecycle->config.command_capacity; ++index) {
        if (lifecycle->command[index].occupied &&
            lifecycle->command[index].phase == SPINE_COMMAND_ACTIVE) {
            lifecycle->command[index].cancel_requested = 1;
            lifecycle->command[index].intent_valid = 0;
            lifecycle->command[index].suppress_intent = 1;
        }
    }
    return FWLAB_SPINE_V0_POISONED;
}

static int admission_collision(
    const struct spine_command_record *record,
    const struct fwlab_spine_profile_binding_v0 *binding,
    const struct fwlab_host_action_program_v0 *program,
    uint32_t role)
{
    return record->role == role && binding_equal(&record->binding, binding) &&
           memcmp(&record->program, program, sizeof(*program)) == 0;
}

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_admit_start(
    void *arena,
    const struct fwlab_spine_profile_binding_v0 *binding,
    const struct fwlab_host_action_program_v0 *program,
    uint32_t role,
    struct fwlab_spine_command_ticket_v0 *ticket)
{
    struct spine_lifecycle *lifecycle = lifecycle_from(arena);
    struct spine_command_record local;
    uint32_t free_index = UINT32_MAX;
    uint32_t occupied = 0;
    uint32_t index;

    if (lifecycle == NULL || binding == NULL || program == NULL ||
        ticket == NULL || !fwlab_spine_profile_binding_v0_valid(binding) ||
        !fwlab_host_action_program_v0_valid(program) ||
        !program_matches_binding(program, binding) ||
        !role_valid(program, binding, role)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (lifecycle->admission_closed) {
        return lifecycle->poisoned ? FWLAB_SPINE_V0_POISONED
                                   : FWLAB_SPINE_V0_WRONG_STATE;
    }
    for (index = 0; index < lifecycle->config.command_capacity; ++index) {
        struct spine_command_record *record = &lifecycle->command[index];

        if (!record->occupied) {
            if (free_index == UINT32_MAX) {
                free_index = index;
            }
            continue;
        }
        ++occupied;
        if (handle_equal(&record->program.command, &program->command) ||
            origin_equal(&record->program.origin, &program->origin)) {
            if (!handle_equal(&record->program.command, &program->command) ||
                !origin_equal(&record->program.origin, &program->origin) ||
                !admission_collision(record, binding, program, role)) {
                return poison_lifecycle(lifecycle);
            }
            *ticket = record->ticket;
            return FWLAB_SPINE_V0_OK;
        }
    }
    if (occupied >= lifecycle->config.command_capacity ||
        free_index == UINT32_MAX) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    if (!uid_available(lifecycle->next_command_uid,
                       lifecycle->config.command_uid.maximum, 1) ||
        !uid_available(lifecycle->next_action_uid,
                       lifecycle->config.action_uid.maximum,
                       program->action_count) ||
        (role == FWLAB_SPINE_ROLE_V0_ABORT &&
         !uid_available(lifecycle->next_abort_uid,
                        lifecycle->config.abort_uid.maximum, 1))) {
        return FWLAB_SPINE_V0_COUNTER_EXHAUSTED;
    }

    memset(&local, 0, sizeof(local));
    local.occupied = 1;
    local.role = role;
    local.phase = SPINE_COMMAND_ACTIVE;
    local.binding = *binding;
    local.program = *program;
    local.ticket.version = FWLAB_SPINE_LIFECYCLE_V0_VERSION;
    local.ticket.size = sizeof(local.ticket);
    local.ticket.command = program->command;
    local.ticket.origin = program->origin;
    local.ticket.lifecycle_instance_nonce =
        lifecycle->config.lifecycle_instance_nonce;
    local.ticket.ticket_uid = lifecycle->next_command_uid;
    local.ticket.execution_epoch = lifecycle->config.execution_epoch;
    local.ticket.generation = lifecycle->config.generation;
    for (index = 0; index < program->action_count; ++index) {
        struct spine_action_record *action = &local.action[index];

        action->description = program->action[index];
        action->phase = SPINE_ACTION_WAIT_DEP;
        action->token.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
        action->token.size = sizeof(action->token);
        action->token.type_tag = FWLAB_HOST_ACTION_TOKEN_V0_TAG;
        action->token.command = program->command;
        action->token.origin = program->origin;
        action->token.action_uid = lifecycle->next_action_uid + index;
        action->token.generation = lifecycle->config.generation;
        action->token.ordinal = (uint16_t)index;
        action->token.kind = program->action[index].kind;
    }
    if (role == FWLAB_SPINE_ROLE_V0_ABORT) {
        local.abort.abort_uid = lifecycle->next_abort_uid;
        local.ticket.relation_uid = lifecycle->next_abort_uid;
    }

    lifecycle->command[free_index] = local;
    ++lifecycle->next_command_uid;
    lifecycle->next_action_uid += program->action_count;
    if (role == FWLAB_SPINE_ROLE_V0_ABORT) {
        ++lifecycle->next_abort_uid;
    }
    ++lifecycle->active_commands;
    *ticket = local.ticket;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_admit_query(
    void *arena,
    const struct fwlab_spine_profile_binding_v0 *binding,
    const struct fwlab_host_action_program_v0 *program,
    uint32_t role,
    struct fwlab_spine_command_ticket_v0 *ticket)
{
    struct spine_lifecycle *lifecycle = lifecycle_from(arena);
    uint32_t index;

    if (lifecycle == NULL || binding == NULL || program == NULL ||
        ticket == NULL || !fwlab_spine_profile_binding_v0_valid(binding) ||
        !fwlab_host_action_program_v0_valid(program)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    for (index = 0; index < lifecycle->config.command_capacity; ++index) {
        struct spine_command_record *record = &lifecycle->command[index];

        if (!record->occupied) {
            continue;
        }
        if (handle_equal(&record->program.command, &program->command) ||
            origin_equal(&record->program.origin, &program->origin)) {
            if (!handle_equal(&record->program.command, &program->command) ||
                !origin_equal(&record->program.origin, &program->origin) ||
                !admission_collision(record, binding, program, role)) {
                return poison_lifecycle(lifecycle);
            }
            *ticket = record->ticket;
            return FWLAB_SPINE_V0_OK;
        }
    }
    return FWLAB_SPINE_V0_STALE;
}

static void status_synthetic(
    struct spine_action_record *action,
    uint32_t terminal_kind,
    uint32_t fault_domain,
    uint32_t fault_code)
{
    memset(&action->terminal, 0, sizeof(action->terminal));
    action->terminal.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION;
    action->terminal.size = sizeof(action->terminal);
    action->terminal.token = action->token;
    action->terminal.state = FWLAB_HOST_ACTION_V0_STATE_DRAINED;
    action->terminal.terminal_kind = terminal_kind;
    action->terminal.effect = FWLAB_HOST_ACTION_V0_EFFECT_NONE;
    action->terminal.fault_domain = fault_domain;
    action->terminal.fault_code = fault_code;
    action->terminal_valid = 1;
    action->phase = SPINE_ACTION_DRAINED;
}

static int terminal_facts_equal(
    const struct fwlab_host_action_status_v0 *left,
    const struct fwlab_host_action_status_v0 *right)
{
    return token_equal(&left->token, &right->token) &&
           left->terminal_kind == right->terminal_kind &&
           left->produced_witness_mask == right->produced_witness_mask &&
           left->effect == right->effect &&
           left->units_completed == right->units_completed &&
           left->fault_domain == right->fault_domain &&
           left->fault_code == right->fault_code;
}

static int status_matches_action(
    const struct fwlab_host_action_status_v0 *status,
    const struct spine_action_record *action)
{
    return fwlab_host_action_status_v0_valid(status) &&
           token_equal(&status->token, &action->token);
}

static void note_failure(
    struct spine_lifecycle *lifecycle,
    struct spine_command_record *command,
    struct spine_action_record *action)
{
    uint32_t index;

    if (!action->terminal_valid ||
        action->terminal.terminal_kind == FWLAB_HOST_ACTION_V0_SUCCEEDED ||
        command->first_failure) {
        return;
    }
    command->first_failure = 1;
    command->first_terminal_sequence = ++lifecycle->terminal_sequence;
    command->cancel_requested = 1;
    for (index = 0; index < command->program.action_count; ++index) {
        if (&command->action[index] != action) {
            command->action[index].cancel_requested = 1;
        }
    }
}

static enum fwlab_spine_result_v0 accept_terminal(
    struct spine_lifecycle *lifecycle,
    struct spine_command_record *command,
    struct spine_action_record *action,
    const struct fwlab_host_action_status_v0 *status)
{
    if (!status_matches_action(status, action) ||
        status->state == FWLAB_HOST_ACTION_V0_STATE_ACCEPTED) {
        return poison_lifecycle(lifecycle);
    }
    if (!action->terminal_valid) {
        action->terminal = *status;
        action->terminal_valid = 1;
        if (status->terminal_kind == FWLAB_HOST_ACTION_V0_SUCCEEDED &&
            lifecycle->close_sequence == 0 &&
            action->token.kind == FWLAB_HOST_ACTION_V0_TARGET_RESOLVE) {
            command->resolver_succeeded_before_close = 1;
        }
        note_failure(lifecycle, command, action);
    } else if (!terminal_facts_equal(&action->terminal, status)) {
        return poison_lifecycle(lifecycle);
    } else {
        action->terminal.state = status->state;
    }
    return FWLAB_SPINE_V0_OK;
}

static int dependency_ready(
    const struct spine_command_record *command,
    uint32_t ordinal,
    int *failed)
{
    const uint32_t mask = command->action[ordinal].description.dependency_mask;
    uint32_t index;

    *failed = 0;
    for (index = 0; index < ordinal; ++index) {
        if ((mask & (UINT32_C(1) << index)) == 0) {
            continue;
        }
        if (command->action[index].phase != SPINE_ACTION_DRAINED) {
            return 0;
        }
        if (command->action[index].terminal.terminal_kind !=
            FWLAB_HOST_ACTION_V0_SUCCEEDED) {
            *failed = 1;
        }
    }
    return 1;
}

static enum fwlab_spine_result_v0 action_query(
    struct spine_lifecycle *lifecycle,
    struct spine_command_record *command,
    struct spine_action_record *action,
    uint32_t *external)
{
    const struct fwlab_host_action_driver_binding_v0 *driver =
        &lifecycle->drivers.entry[action->token.kind - 1];
    struct fwlab_host_action_status_v0 status;
    enum fwlab_spine_result_v0 result;

    memset(&status, 0, sizeof(status));
    result = driver->ops->query(driver->context, &action->token, &status);
    *external = 1;
    if (result == FWLAB_SPINE_V0_IN_PROGRESS) {
        return FWLAB_SPINE_V0_OK;
    }
    if (result != FWLAB_SPINE_V0_OK ||
        !status_matches_action(&status, action)) {
        return poison_lifecycle(lifecycle);
    }
    if (status.state == FWLAB_HOST_ACTION_V0_STATE_ACCEPTED) {
        action->ever_accepted = 1;
        action->phase = SPINE_ACTION_ACCEPTED;
        return FWLAB_SPINE_V0_OK;
    }
    if (status.state != FWLAB_HOST_ACTION_V0_STATE_TERMINAL) {
        return poison_lifecycle(lifecycle);
    }
    if (accept_terminal(lifecycle, command, action, &status) !=
        FWLAB_SPINE_V0_OK) {
        return FWLAB_SPINE_V0_POISONED;
    }
    action->ever_accepted = 1;
    action->phase = SPINE_ACTION_TERMINAL;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 process_action(
    struct spine_lifecycle *lifecycle,
    struct spine_command_record *command,
    struct spine_action_record *action,
    uint32_t ordinal,
    uint32_t *external)
{
    const struct fwlab_host_action_driver_binding_v0 *driver =
        &lifecycle->drivers.entry[action->token.kind - 1];
    enum fwlab_spine_result_v0 result;
    int dependency_failed;

    *external = 0;
    switch (action->phase) {
    case SPINE_ACTION_WAIT_DEP:
        if (command->cancel_requested || action->cancel_requested) {
            status_synthetic(action, FWLAB_HOST_ACTION_V0_CANCELLED, 0, 0);
            note_failure(lifecycle, command, action);
            return FWLAB_SPINE_V0_OK;
        }
        if (!dependency_ready(command, ordinal, &dependency_failed)) {
            return FWLAB_SPINE_V0_IN_PROGRESS;
        }
        if (dependency_failed) {
            status_synthetic(action, FWLAB_HOST_ACTION_V0_CANCELLED, 0, 0);
            note_failure(lifecycle, command, action);
            return FWLAB_SPINE_V0_OK;
        }
        action->phase = SPINE_ACTION_SUBMIT_READY;
        return FWLAB_SPINE_V0_OK;
    case SPINE_ACTION_SUBMIT_READY: {
        struct fwlab_host_action_submit_result_v0 submitted;

        if (command->cancel_requested || action->cancel_requested) {
            status_synthetic(action, FWLAB_HOST_ACTION_V0_CANCELLED, 0, 0);
            note_failure(lifecycle, command, action);
            return FWLAB_SPINE_V0_OK;
        }
        memset(&submitted, 0, sizeof(submitted));
        action->phase = SPINE_ACTION_SUBMIT_UNKNOWN;
        result = driver->ops->submit(driver->context, &action->token,
                                     &action->description.argument,
                                     &submitted);
        *external = 1;
        if (result == FWLAB_SPINE_V0_IN_PROGRESS) {
            return FWLAB_SPINE_V0_OK;
        }
        if (result != FWLAB_SPINE_V0_OK ||
            !fwlab_host_action_submit_result_v0_valid(&submitted) ||
            !token_equal(&submitted.token, &action->token)) {
            return poison_lifecycle(lifecycle);
        }
        if (submitted.disposition == FWLAB_HOST_ACTION_V0_BACKPRESSURE) {
            action->phase = SPINE_ACTION_SUBMIT_READY;
        } else if (submitted.disposition == FWLAB_HOST_ACTION_V0_REJECTED) {
            status_synthetic(action, FWLAB_HOST_ACTION_V0_FAILED,
                             submitted.fault_domain, submitted.fault_code);
            note_failure(lifecycle, command, action);
        } else {
            action->ever_accepted = 1;
            action->phase = SPINE_ACTION_ACCEPTED;
        }
        return FWLAB_SPINE_V0_OK;
    }
    case SPINE_ACTION_SUBMIT_UNKNOWN:
        return action_query(lifecycle, command, action, external);
    case SPINE_ACTION_ACCEPTED:
        if ((command->cancel_requested || action->cancel_requested) &&
            !action->cancel_acked) {
            action->cancel_requested = 1;
            action->cancel_issued = 1;
            result = driver->ops->cancel(driver->context, &action->token);
            *external = 1;
            if (result == FWLAB_SPINE_V0_OK) {
                action->cancel_acked = 1;
                return FWLAB_SPINE_V0_OK;
            }
            if (result == FWLAB_SPINE_V0_IN_PROGRESS) {
                return FWLAB_SPINE_V0_OK;
            }
            return poison_lifecycle(lifecycle);
        }
        return action_query(lifecycle, command, action, external);
    case SPINE_ACTION_TERMINAL:
    case SPINE_ACTION_RETIRE_ISSUED:
        action->retire_issued = 1;
        result = driver->ops->retire_start(driver->context, &action->token);
        *external = 1;
        if (result == FWLAB_SPINE_V0_IN_PROGRESS) {
            action->phase = SPINE_ACTION_RETIRE_ISSUED;
            return FWLAB_SPINE_V0_OK;
        }
        if (result != FWLAB_SPINE_V0_OK) {
            return poison_lifecycle(lifecycle);
        }
        action->retire_acked = 1;
        action->phase = SPINE_ACTION_DRAINING;
        return FWLAB_SPINE_V0_OK;
    case SPINE_ACTION_DRAINING: {
        struct fwlab_host_action_status_v0 status;

        memset(&status, 0, sizeof(status));
        result = driver->ops->retire_query(driver->context, &action->token,
                                           &status);
        *external = 1;
        if (result == FWLAB_SPINE_V0_IN_PROGRESS) {
            return FWLAB_SPINE_V0_OK;
        }
        if (result != FWLAB_SPINE_V0_OK ||
            !status_matches_action(&status, action) ||
            (status.state != FWLAB_HOST_ACTION_V0_STATE_DRAINING &&
             status.state != FWLAB_HOST_ACTION_V0_STATE_DRAINED &&
             status.state != FWLAB_HOST_ACTION_V0_STATE_RETIRED) ||
            accept_terminal(lifecycle, command, action, &status) !=
                FWLAB_SPINE_V0_OK) {
            return poison_lifecycle(lifecycle);
        }
        if (status.state == FWLAB_HOST_ACTION_V0_STATE_DRAINED ||
            status.state == FWLAB_HOST_ACTION_V0_STATE_RETIRED) {
            action->phase = SPINE_ACTION_DRAINED;
        }
        return FWLAB_SPINE_V0_OK;
    }
    case SPINE_ACTION_DRAINED:
        return FWLAB_SPINE_V0_IN_PROGRESS;
    default:
        return poison_lifecycle(lifecycle);
    }
}

static int all_actions_drained(const struct spine_command_record *command)
{
    uint32_t index;

    for (index = 0; index < command->program.action_count; ++index) {
        if (command->action[index].phase != SPINE_ACTION_DRAINED) {
            return 0;
        }
    }
    return 1;
}

static int target_abort_won(
    const struct spine_abort_record *abort,
    const struct spine_command_record *target)
{
    uint32_t index;

    if (!all_actions_drained(target)) {
        return 0;
    }
    for (index = 0; index < target->program.action_count; ++index) {
        const struct spine_action_record *action = &target->action[index];

        if ((abort->target_accepted_mask & (UINT32_C(1) << index)) != 0 &&
            action->terminal.terminal_kind !=
                FWLAB_HOST_ACTION_V0_CANCELLED) {
            return 0;
        }
    }
    return 1;
}

static enum fwlab_spine_result_v0 process_abort(
    struct spine_lifecycle *lifecycle,
    struct spine_command_record *command,
    uint32_t *external)
{
    struct spine_abort_record *abort = &command->abort;
    struct spine_action_record *resolver = &command->action[0];

    *external = 0;
    if (abort->abandoned || abort->superseded || abort->sink_failed ||
        abort->sink_acked) {
        return FWLAB_SPINE_V0_OK;
    }
    if (resolver->phase != SPINE_ACTION_DRAINED) {
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    if (resolver->terminal.terminal_kind != FWLAB_HOST_ACTION_V0_SUCCEEDED) {
        memset(&abort->candidate, 0, sizeof(abort->candidate));
        abort->candidate_present = 0;
        abort->abandoned = 1;
        return FWLAB_SPINE_V0_OK;
    }
    if (!command->resolver_succeeded_before_close) {
        abort->superseded = 1;
        command->suppress_intent = 1;
        return FWLAB_SPINE_V0_OK;
    }
    if (!abort->candidate_present) {
        struct fwlab_spine_abort_candidate_v0 candidate;
        enum fwlab_spine_result_v0 result;
        uint8_t present = 0;

        memset(&candidate, 0, sizeof(candidate));
        result = command->binding.relation_source(
            command->binding.relation_source_context, abort->abort_uid,
            &resolver->token, &candidate, &present);
        *external = 1;
        if (result != FWLAB_SPINE_V0_OK || present > 1 ||
            (present &&
             (!fwlab_spine_abort_candidate_v0_valid(&candidate) ||
              candidate.abort_uid != abort->abort_uid ||
              !token_equal(&candidate.resolver, &resolver->token)))) {
            abort->sink_failed = 1;
            command->suppress_intent = 1;
            return poison_lifecycle(lifecycle);
        }
        if (!present) {
            if (lifecycle->close_started) {
                abort->superseded = 1;
                command->suppress_intent = 1;
            }
            return FWLAB_SPINE_V0_OK;
        }
        abort->candidate = candidate;
        abort->candidate_present = 1;
        return FWLAB_SPINE_V0_OK;
    }
    if (abort->decision == 0 && !abort->target_bound) {
        if (abort->candidate.report != FWLAB_SPINE_ABORT_REPORT_V0_FOUND) {
            abort->decision = FWLAB_SPINE_ABORT_DECISION_V0_NOT_ABORTED;
        } else {
            struct spine_command_record *target =
                find_ticket(lifecycle, &abort->candidate.target);

            if (target == NULL || target == command ||
                target->incoming_abort_uid != 0 ||
                target->phase != SPINE_COMMAND_ACTIVE ||
                target->first_failure || target->program.action_count == 0) {
                abort->decision = FWLAB_SPINE_ABORT_DECISION_V0_NOT_ABORTED;
            } else {
                uint32_t index;

                target->incoming_abort_uid = abort->abort_uid;
                target->cancel_requested = 1;
                for (index = 0; index < target->program.action_count; ++index) {
                    if (target->action[index].ever_accepted) {
                        abort->target_accepted_mask |=
                            UINT32_C(1) << index;
                    }
                    target->action[index].cancel_requested = 1;
                }
                abort->target_bound = 1;
                abort->target_index =
                    (uint16_t)(target - lifecycle->command);
            }
        }
    }
    if (abort->target_bound && abort->decision == 0) {
        struct spine_command_record *target =
            &lifecycle->command[abort->target_index];

        if (!all_actions_drained(target)) {
            return FWLAB_SPINE_V0_IN_PROGRESS;
        }
        abort->decision = target_abort_won(abort, target)
                              ? FWLAB_SPINE_ABORT_DECISION_V0_ABORT_WON
                              : FWLAB_SPINE_ABORT_DECISION_V0_NOT_ABORTED;
    }
    if (abort->decision != 0 && !abort->sink_acked) {
        enum fwlab_spine_result_v0 result;

        abort->sink_issued = 1;
        result = command->binding.relation_sink(
            command->binding.relation_context, &command->program,
            abort->abort_uid, abort->decision);
        *external = 1;
        if (result != FWLAB_SPINE_V0_OK) {
            abort->sink_failed = 1;
            command->suppress_intent = 1;
            return poison_lifecycle(lifecycle);
        }
        abort->sink_acked = 1;
    }
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 finalize_command(
    struct spine_lifecycle *lifecycle,
    struct spine_command_record *command,
    uint32_t *external)
{
    struct fwlab_host_action_status_v0
        status[FWLAB_HOST_ACTION_V0_MAX_ACTIONS];
    struct fwlab_nvme_completion_intent intent;
    enum fwlab_spine_result_v0 result;
    uint32_t index;

    *external = 0;
    if (!all_actions_drained(command)) {
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    if (command->role == FWLAB_SPINE_ROLE_V0_ABORT &&
        !command->abort.abandoned && !command->abort.superseded &&
        !command->abort.sink_failed && !command->abort.sink_acked) {
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    if (command->suppress_intent) {
        command->phase = SPINE_COMMAND_QUARANTINED;
        if (lifecycle->active_commands != 0) {
            --lifecycle->active_commands;
        }
        return FWLAB_SPINE_V0_OK;
    }
    memset(status, 0, sizeof(status));
    for (index = 0; index < command->program.action_count; ++index) {
        status[index] = command->action[index].terminal;
    }
    memset(&intent, 0, sizeof(intent));
    result = command->binding.adapter.ops->complete(
        command->binding.adapter.context, &command->program, status,
        command->program.action_count, &intent);
    *external = 1;
    if (result != FWLAB_SPINE_V0_OK ||
        !fwlab_host_completion_intent_v0_valid_for_program(
            &command->program, status, command->program.action_count,
            &intent)) {
        command->phase = SPINE_COMMAND_QUARANTINED;
        if (lifecycle->active_commands != 0) {
            --lifecycle->active_commands;
        }
        return poison_lifecycle(lifecycle);
    }
    command->intent = intent;
    command->intent_valid = 1;
    command->phase = SPINE_COMMAND_INTENT;
    if (lifecycle->active_commands != 0) {
        --lifecycle->active_commands;
    }
    ++lifecycle->retained_intents;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 process_command(
    struct spine_lifecycle *lifecycle,
    struct spine_command_record *command,
    uint32_t *external)
{
    uint32_t index;

    *external = 0;
    if (!command->occupied || command->phase != SPINE_COMMAND_ACTIVE) {
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    if (command->cancel_requested) {
        for (index = 0; index < command->program.action_count; ++index) {
            command->action[index].cancel_requested = 1;
        }
    }
    for (index = 0; index < command->program.action_count; ++index) {
        if (command->action[index].phase != SPINE_ACTION_DRAINED) {
            return process_action(lifecycle, command, &command->action[index],
                                  index, external);
        }
    }
    if (command->role == FWLAB_SPINE_ROLE_V0_ABORT) {
        enum fwlab_spine_result_v0 result =
            process_abort(lifecycle, command, external);

        if (result != FWLAB_SPINE_V0_OK || *external != 0) {
            return result;
        }
    }
    return finalize_command(lifecycle, command, external);
}

static enum fwlab_spine_result_v0 process_driver_close(
    struct spine_lifecycle *lifecycle,
    uint32_t lane,
    uint32_t *external)
{
    struct spine_driver_close_record *state =
        &lifecycle->driver_close[lane];
    const struct fwlab_host_action_driver_binding_v0 *driver =
        &lifecycle->drivers.entry[lane];
    enum fwlab_spine_result_v0 result;

    *external = 1;
    if (!state->close_acked) {
        state->close_issued = 1;
        result = driver->ops->epoch_close(
            driver->context, lifecycle->config.lifecycle_instance_nonce,
            lifecycle->config.execution_epoch);
        if (result == FWLAB_SPINE_V0_OK) {
            state->close_acked = 1;
            return FWLAB_SPINE_V0_OK;
        }
        if (result == FWLAB_SPINE_V0_IN_PROGRESS) {
            return FWLAB_SPINE_V0_OK;
        }
        return poison_lifecycle(lifecycle);
    }
    if (!state->quiescent) {
        uint8_t quiescent = 0;

        result = driver->ops->epoch_quiescent(
            driver->context, lifecycle->config.lifecycle_instance_nonce,
            lifecycle->config.execution_epoch, &quiescent);
        if (result == FWLAB_SPINE_V0_IN_PROGRESS) {
            return FWLAB_SPINE_V0_OK;
        }
        if (result != FWLAB_SPINE_V0_OK || quiescent > 1) {
            return poison_lifecycle(lifecycle);
        }
        state->quiescent = quiescent;
    }
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 step_one(
    struct spine_lifecycle *lifecycle,
    uint32_t *external,
    uint32_t *transitioned)
{
    const uint32_t command_count = lifecycle->config.command_capacity;
    const uint32_t span = command_count +
                          (lifecycle->close_started
                               ? FWLAB_HOST_ACTION_V0_KIND_COUNT
                               : 0);
    uint32_t offset;

    *external = 0;
    *transitioned = 0;
    for (offset = 0; offset < span; ++offset) {
        const uint32_t slot = (lifecycle->service_cursor + offset) % span;
        enum fwlab_spine_result_v0 result;

        if (slot < command_count) {
            struct spine_command_record *command =
                &lifecycle->command[slot];
            unsigned char before[sizeof(*command)];

            if (!command->occupied ||
                command->phase != SPINE_COMMAND_ACTIVE) {
                continue;
            }
            memcpy(before, command, sizeof(before));
            result = process_command(lifecycle, command, external);
            lifecycle->service_cursor = (slot + 1) % span;
            *transitioned = memcmp(before, command, sizeof(before)) != 0;
            if (result == FWLAB_SPINE_V0_OK || *external || *transitioned ||
                result == FWLAB_SPINE_V0_POISONED) {
                return result;
            }
        } else {
            const uint32_t lane = slot - command_count;
            struct spine_driver_close_record before =
                lifecycle->driver_close[lane];

            if (before.close_acked && before.quiescent) {
                continue;
            }
            result = process_driver_close(lifecycle, lane, external);
            lifecycle->service_cursor = (slot + 1) % span;
            *transitioned = memcmp(&before, &lifecycle->driver_close[lane],
                                   sizeof(before)) != 0;
            return result;
        }
    }
    return FWLAB_SPINE_V0_IN_PROGRESS;
}

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_step(
    void *arena,
    uint32_t budget,
    uint32_t *units,
    uint32_t *transitions)
{
    struct spine_lifecycle *lifecycle = lifecycle_from(arena);
    uint32_t local_units = 0;
    uint32_t local_transitions = 0;

    if (lifecycle == NULL || budget == 0 || units == NULL ||
        transitions == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    while (local_units < budget) {
        uint32_t external;
        uint32_t transitioned;
        enum fwlab_spine_result_v0 result =
            step_one(lifecycle, &external, &transitioned);

        if (external || transitioned) {
            ++local_units;
            local_transitions += transitioned;
        } else {
            break;
        }
        if (result == FWLAB_SPINE_V0_POISONED) {
            *units = local_units;
            *transitions = local_transitions;
            return result;
        }
    }
    *units = local_units;
    *transitions = local_transitions;
    return local_units == 0 ? FWLAB_SPINE_V0_IN_PROGRESS
                            : FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_intent_read(
    void *arena,
    const struct fwlab_spine_command_ticket_v0 *ticket,
    struct fwlab_nvme_completion_intent *intent)
{
    struct spine_lifecycle *lifecycle = lifecycle_from(arena);
    struct spine_command_record *command;

    if (lifecycle == NULL || ticket == NULL || intent == NULL ||
        !fwlab_spine_command_ticket_v0_valid(ticket)) {
        return FWLAB_SPINE_V0_INVALID;
    }
    command = find_ticket(lifecycle, ticket);
    if (command == NULL) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (command->phase == SPINE_COMMAND_QUARANTINED) {
        return FWLAB_SPINE_V0_QUARANTINED;
    }
    if (!command->intent_valid) {
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    *intent = command->intent;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_epoch_close_start(
    void *arena,
    uint32_t old_execution_epoch)
{
    struct spine_lifecycle *lifecycle = lifecycle_from(arena);
    uint32_t index;

    if (lifecycle == NULL || old_execution_epoch == 0) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (old_execution_epoch != lifecycle->config.execution_epoch) {
        return FWLAB_SPINE_V0_STALE;
    }
    if (lifecycle->close_started) {
        return FWLAB_SPINE_V0_OK;
    }
    lifecycle->admission_closed = 1;
    lifecycle->close_started = 1;
    lifecycle->close_sequence = ++lifecycle->terminal_sequence;
    for (index = 0; index < lifecycle->config.command_capacity; ++index) {
        struct spine_command_record *command = &lifecycle->command[index];

        if (command->occupied && command->phase == SPINE_COMMAND_ACTIVE) {
            uint32_t action;

            command->cancel_requested = 1;
            for (action = 0; action < command->program.action_count; ++action) {
                command->action[action].cancel_requested = 1;
            }
        }
    }
    return FWLAB_SPINE_V0_OK;
}

static int effectful_quiescent(const struct spine_lifecycle *lifecycle)
{
    uint32_t index;

    if (!lifecycle->close_started || lifecycle->active_commands != 0) {
        return 0;
    }
    for (index = 0; index < FWLAB_HOST_ACTION_V0_KIND_COUNT; ++index) {
        if (!lifecycle->driver_close[index].close_acked ||
            !lifecycle->driver_close[index].quiescent) {
            return 0;
        }
    }
    return 1;
}

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_epoch_query(
    void *arena,
    struct fwlab_spine_epoch_status_v0 *status)
{
    struct spine_lifecycle *lifecycle = lifecycle_from(arena);

    if (lifecycle == NULL || status == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (!lifecycle->close_started) {
        return FWLAB_SPINE_V0_WRONG_STATE;
    }
    memset(status, 0, sizeof(*status));
    status->version = FWLAB_SPINE_LIFECYCLE_V0_VERSION;
    status->size = sizeof(*status);
    status->lifecycle_instance_nonce =
        lifecycle->config.lifecycle_instance_nonce;
    status->execution_epoch = lifecycle->config.execution_epoch;
    status->active_commands = lifecycle->active_commands;
    status->retained_intents = lifecycle->retained_intents;
    status->admission_closed = lifecycle->admission_closed;
    status->effectful_quiescent = (uint8_t)effectful_quiescent(lifecycle);
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 fwlab_spine_lifecycle_v0_fini(void *arena)
{
    struct spine_lifecycle *lifecycle = lifecycle_from(arena);
    uint32_t index;

    if (lifecycle == NULL) {
        return FWLAB_SPINE_V0_INVALID;
    }
    if (!effectful_quiescent(lifecycle)) {
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    for (index = 0; index < lifecycle->config.command_capacity; ++index) {
        struct spine_command_record *command = &lifecycle->command[index];
        enum fwlab_spine_result_v0 result;

        if (!command->occupied || command->profile_retired) {
            continue;
        }
        command->profile_retire_issued = 1;
        result = command->binding.adapter.ops->retire(
            command->binding.adapter.context, &command->program);
        if (result == FWLAB_SPINE_V0_IN_PROGRESS) {
            return result;
        }
        if (result != FWLAB_SPINE_V0_OK) {
            return poison_lifecycle(lifecycle);
        }
        command->profile_retired = 1;
        return FWLAB_SPINE_V0_IN_PROGRESS;
    }
    memset(lifecycle, 0, sizeof(*lifecycle));
    return FWLAB_SPINE_V0_OK;
}
