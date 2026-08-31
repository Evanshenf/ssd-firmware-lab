/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_command.h"

#include <stdbool.h>
#include <string.h>

#include "fwlab/portable/nvme_codec.h"

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

static int prepared_equal(
    const struct fwlab_hif_prepared_token *left,
    const struct fwlab_hif_prepared_token *right)
{
    return handle_equal(&left->handle, &right->handle) &&
           origin_equal(&left->origin, &right->origin) &&
           left->reservation_uid == right->reservation_uid &&
           left->generation == right->generation;
}

static int ticket_equal(
    const struct fwlab_hif_command_ticket *left,
    const struct fwlab_hif_command_ticket *right)
{
    return handle_equal(&left->handle, &right->handle) &&
           origin_equal(&left->origin, &right->origin) &&
           left->ticket_uid == right->ticket_uid &&
           left->generation == right->generation;
}

static int lease_equal(
    const struct fwlab_hif_completion_lease *left,
    const struct fwlab_hif_completion_lease *right)
{
    return ticket_equal(&left->ticket, &right->ticket) &&
           left->lease_uid == right->lease_uid &&
           left->generation == right->generation;
}

static int consume_equal(
    const struct fwlab_hif_consume_token *left,
    const struct fwlab_hif_consume_token *right)
{
    return lease_equal(&left->lease, &right->lease) &&
           left->publication_uid == right->publication_uid &&
           left->consume_uid == right->consume_uid &&
           left->generation == right->generation;
}

static int prepare_key_equal(
    const struct fwlab_hif_prepare_key *left,
    const struct fwlab_hif_prepare_key *right)
{
    return origin_equal(&left->origin, &right->origin) &&
           left->client_uid == right->client_uid &&
           left->instance_nonce == right->instance_nonce &&
           left->controller_epoch == right->controller_epoch &&
           left->client_generation == right->client_generation &&
           left->queue_class == right->queue_class &&
           left->worst_case_actions == right->worst_case_actions;
}

static uint32_t active_count(const struct c42_fake_command *command)
{
    uint32_t count = 0;
    uint32_t index;

    for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
        if (command->records[index].in_use != 0 &&
            command->records[index].retired == 0) {
            count++;
        }
    }
    return count;
}

static struct c42_fake_command_record *find_prepare_key(
    struct c42_fake_command *command,
    const struct fwlab_hif_prepare_key *key)
{
    uint32_t index;

    for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
        if (command->records[index].in_use != 0 &&
            prepare_key_equal(&command->records[index].prepare_key, key)) {
            return &command->records[index];
        }
    }
    return NULL;
}

static struct c42_fake_command_record *find_prepared(
    struct c42_fake_command *command,
    const struct fwlab_hif_prepared_token *prepared)
{
    uint32_t index;

    for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
        if (command->records[index].in_use != 0 &&
            prepared_equal(&command->records[index].prepared, prepared)) {
            return &command->records[index];
        }
    }
    return NULL;
}

static struct c42_fake_command_record *find_ticket(
    struct c42_fake_command *command,
    const struct fwlab_hif_command_ticket *ticket)
{
    uint32_t index;

    for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
        if (command->records[index].in_use != 0 &&
            ticket_equal(&command->records[index].ticket, ticket)) {
            return &command->records[index];
        }
    }
    return NULL;
}

static struct c42_fake_command_record *find_lease(
    struct c42_fake_command *command,
    const struct fwlab_hif_completion_lease *lease)
{
    uint32_t index;

    for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
        if (command->records[index].in_use != 0 &&
            lease_equal(&command->records[index].lease, lease)) {
            return &command->records[index];
        }
    }
    return NULL;
}

static struct c42_fake_command_record *find_consume(
    struct c42_fake_command *command,
    const struct fwlab_hif_consume_token *consume)
{
    uint32_t index;

    for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
        if (command->records[index].in_use != 0 &&
            consume_equal(&command->records[index].consume, consume)) {
            return &command->records[index];
        }
    }
    return NULL;
}

static struct c42_fake_command_record *allocate_record(
    struct c42_fake_command *command)
{
    uint32_t index;

    for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
        if (command->records[index].in_use == 0 ||
            command->records[index].retired != 0) {
            memset(&command->records[index], 0,
                   sizeof(command->records[index]));
            command->records[index].in_use = 1;
            return &command->records[index];
        }
    }
    return NULL;
}

static void provider_output_begin(struct c42_fake_command *command)
{
    if (command != NULL) {
        command->provider_write_mask = 0;
    }
}

static void provider_output_mark(
    struct c42_fake_command *command,
    uint8_t mask)
{
    if (command != NULL) {
        command->provider_write_mask |= mask;
    }
}

static void prepare_result_fill(
    struct c42_fake_command *command,
    const struct c42_fake_command_record *record,
    uint32_t disposition,
    struct fwlab_hif_prepare_result *result)
{
    provider_output_mark(
        command, C42_FAKE_WRITE_VALUE | C42_FAKE_WRITE_OBJECT
    );
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_HIF_COMMAND_PORT_VERSION;
    result->size = sizeof(*result);
    result->disposition = disposition;
    if (record != NULL && disposition == FWLAB_HIF_PREPARE_RESERVED) {
        result->prepared = record->prepared;
    }
}

static void prepare_result_injection_fill(
    struct c42_fake_command *command,
    const struct c42_fake_command_record *record,
    uint32_t disposition,
    uint8_t write_mask,
    uint8_t object_variant,
    struct fwlab_hif_prepare_result *result)
{
    if ((write_mask & C42_FAKE_WRITE_VALUE) != 0) {
        provider_output_mark(command, C42_FAKE_WRITE_VALUE);
        result->version = FWLAB_HIF_COMMAND_PORT_VERSION;
        result->size = sizeof(*result);
        result->disposition = disposition;
    }
    if ((write_mask & C42_FAKE_WRITE_OBJECT) != 0) {
        provider_output_mark(command, C42_FAKE_WRITE_OBJECT);
        memset(&result->prepared, 0, sizeof(result->prepared));
        if (object_variant != C42_FAKE_OBJECT_ZERO && record != NULL) {
            result->prepared = record->prepared;
            if (object_variant == C42_FAKE_OBJECT_MISMATCH) {
                result->prepared.reservation_uid++;
            }
        }
    }
}

void c42_fake_command_init(
    struct c42_fake_command *command,
    uint64_t instance_nonce,
    uint32_t controller_epoch,
    uint32_t active_limit)
{
    if (command == NULL) {
        return;
    }
    memset(command, 0, sizeof(*command));
    command->instance_nonce = instance_nonce;
    command->controller_epoch = controller_epoch;
    command->active_limit = active_limit;
    command->next_command_uid = 1001;
    command->next_reservation_uid = 2001;
    command->next_ticket_uid = 3001;
    command->next_lease_uid = 4001;
    command->next_consume_uid = 5001;
    command->next_ready_sequence = 6001;
    command->next_generation = 101;
}

void c42_fake_command_set_script(
    struct c42_fake_command *command,
    const struct c42_fake_command_script *script)
{
    if (command == NULL || script == NULL) {
        return;
    }
    command->script = *script;
}

void c42_fake_command_bind_event_log(
    struct c42_fake_command *command,
    struct c42_fake_event_log *log)
{
    if (command != NULL) {
        command->event_log = log;
    }
}

static uint8_t default_write_mask(uint32_t operation);

static uint32_t expected_injection_effect(uint32_t operation)
{
    switch (operation) {
    case C42_FAKE_COMMAND_PREPARE:
        return C42_FAKE_COMMAND_EFFECT_PREPARED;
    case C42_FAKE_COMMAND_PREPARE_ABORT:
        return C42_FAKE_COMMAND_EFFECT_PREPARE_ABORTED;
    case C42_FAKE_COMMAND_ADMIT:
        return C42_FAKE_COMMAND_EFFECT_ADMITTED;
    case C42_FAKE_COMMAND_POLL:
        return C42_FAKE_COMMAND_EFFECT_READY;
    case C42_FAKE_COMMAND_COMPLETION_ACQUIRE:
        return C42_FAKE_COMMAND_EFFECT_LEASED;
    case C42_FAKE_COMMAND_COMPLETION_RELEASE:
        return C42_FAKE_COMMAND_EFFECT_RELEASED;
    case C42_FAKE_COMMAND_CONSUME_PREPARE:
        return C42_FAKE_COMMAND_EFFECT_CONSUME_PREPARED;
    case C42_FAKE_COMMAND_CONSUME_ABORT:
        return C42_FAKE_COMMAND_EFFECT_CONSUME_ABORTED;
    case C42_FAKE_COMMAND_CONSUME_COMMIT:
    case C42_FAKE_COMMAND_CONSUME_QUERY:
        return C42_FAKE_COMMAND_EFFECT_CONSUME_COMMITTED;
    case C42_FAKE_COMMAND_CONSUME_RETIRE:
        return C42_FAKE_COMMAND_EFFECT_CONSUME_RETIRED;
    case C42_FAKE_COMMAND_RESET_BEGIN:
        return C42_FAKE_COMMAND_EFFECT_RESET_BEGUN;
    case C42_FAKE_COMMAND_TEARDOWN_BEGIN:
        return C42_FAKE_COMMAND_EFFECT_TEARDOWN_BEGUN;
    default:
        return C42_FAKE_COMMAND_EFFECT_NONE;
    }
}

enum c42_result c42_fake_command_injection_push(
    struct c42_fake_command *command,
    const struct c42_fake_command_injection *injection)
{
    if (command == NULL || injection == NULL ||
        command->injection_count >= C42_FAKE_COMMAND_INJECTIONS ||
        injection->operation < C42_FAKE_COMMAND_PREPARE ||
        injection->operation > C42_FAKE_COMMAND_TEARDOWN_QUIESCENT ||
        injection->result > FWLAB_HIF_PORT_COUNTER_EXHAUSTED + 2u ||
        injection->omit_outputs > 1 || injection->write_mask > 3 ||
        (injection->write_mask &
         (uint8_t)~default_write_mask(injection->operation)) != 0 ||
        injection->flags > C42_FAKE_APPLY_EFFECT ||
        ((injection->flags & C42_FAKE_APPLY_EFFECT) != 0 &&
         injection->requested_effect !=
             expected_injection_effect(injection->operation)) ||
        ((injection->flags & C42_FAKE_APPLY_EFFECT) == 0 &&
         injection->requested_effect != C42_FAKE_COMMAND_EFFECT_NONE) ||
        injection->object_variant > C42_FAKE_OBJECT_MISMATCH) {
        return C42_INVALID;
    }
    command->injections[command->injection_count] = *injection;
    command->injection_count++;
    return C42_OK;
}

static uint8_t default_write_mask(uint32_t operation)
{
    switch (operation) {
    case C42_FAKE_COMMAND_PREPARE:
    case C42_FAKE_COMMAND_ADMIT:
    case C42_FAKE_COMMAND_POLL:
    case C42_FAKE_COMMAND_CONSUME_PREPARE:
        return C42_FAKE_WRITE_VALUE | C42_FAKE_WRITE_OBJECT;
    case C42_FAKE_COMMAND_COMPLETION_ACQUIRE:
        return C42_FAKE_WRITE_OBJECT;
    case C42_FAKE_COMMAND_RESET_BEGIN:
    case C42_FAKE_COMMAND_TEARDOWN_BEGIN:
        return 0;
    default:
        return C42_FAKE_WRITE_VALUE;
    }
}

static int injection_take(
    struct c42_fake_command *command,
    uint32_t operation,
    enum fwlab_hif_command_port_result *result,
    uint32_t *value,
    int *omit_outputs)
{
    command->injection_active = 0;
    command->injection_write_mask = 0;
    command->injection_flags = 0;
    command->injection_object_variant = C42_FAKE_OBJECT_ZERO;
    command->injection_event_value = 0;
    command->injection_requested_effect = C42_FAKE_COMMAND_EFFECT_NONE;
    command->injection_applied_effect = C42_FAKE_COMMAND_EFFECT_NONE;
    if (command->injection_index < command->injection_count) {
        const struct c42_fake_command_injection *injection =
            &command->injections[command->injection_index];

        if (injection->operation != operation) {
            return 0;
        }
        command->injection_index++;
        *result = (enum fwlab_hif_command_port_result)injection->result;
        *value = injection->value;
        command->injection_active = 1;
        command->injection_write_mask = injection->write_mask;
        if (command->injection_write_mask == 0 &&
            injection->omit_outputs == 0) {
            command->injection_write_mask = default_write_mask(operation);
        }
        command->injection_flags = injection->flags;
        command->injection_object_variant = injection->object_variant;
        command->injection_event_value = injection->value;
        command->injection_requested_effect = injection->requested_effect;
        *omit_outputs = command->injection_write_mask == 0;
        return 1;
    }
    if (command->script.inject_operation != operation ||
        command->script.inject_count == 0) {
        return 0;
    }
    command->script.inject_count--;
    *result = (enum fwlab_hif_command_port_result)
        command->script.inject_result;
    *value = command->script.inject_value;
    *omit_outputs = command->script.inject_omit_outputs != 0;
    command->injection_active = 1;
    command->injection_write_mask = *omit_outputs != 0 ? 0 :
        default_write_mask(operation);
    command->injection_event_value = *value;
    return 1;
}

static int prepare_key_valid(
    const struct c42_fake_command *command,
    const struct fwlab_hif_prepare_key *key)
{
    return key != NULL && key->version == FWLAB_HIF_COMMAND_PORT_VERSION &&
           key->size == sizeof(*key) && key->reserved0 == 0 &&
           key->origin.word[0] != 0 && key->origin.word[1] != 0 &&
           key->client_uid != 0 &&
           key->instance_nonce == command->instance_nonce &&
           key->controller_epoch == command->controller_epoch &&
           key->client_generation != 0 && key->worst_case_actions != 0 &&
           (key->queue_class == FWLAB_NVME_QUEUE_ADMIN ||
            key->queue_class == FWLAB_NVME_QUEUE_IO) &&
           key->reserved1[0] == 0 && key->reserved1[1] == 0;
}

static void mint_prepared(
    struct c42_fake_command *command,
    struct c42_fake_command_record *record)
{
    if (record->prepared.reservation_uid != 0) {
        return;
    }
    record->prepared.handle.instance_nonce = command->instance_nonce;
    record->prepared.handle.command_uid = command->next_command_uid++;
    record->prepared.handle.controller_epoch = command->controller_epoch;
    record->prepared.handle.generation = command->next_generation++;
    record->prepared.origin = record->prepare_key.origin;
    record->prepared.reservation_uid = command->next_reservation_uid++;
    record->prepared.generation = command->next_generation++;
}

static void mark_injected_effect(
    struct c42_fake_command *command,
    uint32_t effect)
{
    if (command != NULL && command->injection_active != 0) {
        command->injection_applied_effect = effect;
    }
}

static enum fwlab_hif_command_port_result fake_prepare_start(
    void *context,
    const struct fwlab_hif_prepare_key *key,
    struct fwlab_hif_prepare_result *result)
{
    struct c42_fake_command *command = context;
    struct c42_fake_command_record *record;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    int omit;

    if (command == NULL || result == NULL || !prepare_key_valid(command, key)) {
        return FWLAB_HIF_PORT_INVALID;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_PREPARE, &injected, &value, &omit)) {
        record = find_prepare_key(command, key);
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0) {
            if (record == NULL) {
                record = allocate_record(command);
                if (record != NULL) {
                    record->prepare_key = *key;
                }
            }
            if (record != NULL) {
                mint_prepared(command, record);
                mark_injected_effect(
                    command, C42_FAKE_COMMAND_EFFECT_PREPARED
                );
            }
        }
        prepare_result_injection_fill(
            command, record, value, command->injection_write_mask,
            command->injection_object_variant, result
        );
        (void)omit;
        return injected;
    }
    record = find_prepare_key(command, key);
    if (record != NULL && record->prepared.reservation_uid != 0) {
        prepare_result_fill(
            command, record, FWLAB_HIF_PREPARE_RESERVED, result
        );
        return FWLAB_HIF_PORT_OK;
    }
    command->prepare_attempts++;
    if (command->prepare_attempts <= command->script.prepare_backpressure ||
        active_count(command) >= command->active_limit) {
        prepare_result_fill(
            command, NULL, FWLAB_HIF_PREPARE_BACKPRESSURE, result
        );
        return FWLAB_HIF_PORT_OK;
    }
    if (record == NULL) {
        record = allocate_record(command);
        if (record == NULL) {
            prepare_result_fill(
                command, NULL, FWLAB_HIF_PREPARE_BACKPRESSURE, result
            );
            return FWLAB_HIF_PORT_OK;
        }
        record->prepare_key = *key;
    }
    if (command->script.prepare_delay != 0) {
        return FWLAB_HIF_PORT_IN_PROGRESS;
    }
    mint_prepared(command, record);
    prepare_result_fill(
        command, record, FWLAB_HIF_PREPARE_RESERVED, result
    );
    return FWLAB_HIF_PORT_OK;
}

static enum fwlab_hif_command_port_result fake_prepare_query(
    void *context,
    const struct fwlab_hif_prepare_key *key,
    struct fwlab_hif_prepare_result *result)
{
    struct c42_fake_command *command = context;
    struct c42_fake_command_record *record;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    int omit;

    if (command == NULL || result == NULL || !prepare_key_valid(command, key)) {
        return FWLAB_HIF_PORT_INVALID;
    }
    record = find_prepare_key(command, key);
    if (record == NULL) {
        return FWLAB_HIF_PORT_STALE;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_PREPARE, &injected, &value, &omit)) {
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0) {
            mint_prepared(command, record);
            mark_injected_effect(
                command, C42_FAKE_COMMAND_EFFECT_PREPARED
            );
        }
        prepare_result_injection_fill(
            command, record, value, command->injection_write_mask,
            command->injection_object_variant, result
        );
        (void)omit;
        return injected;
    }
    record->prepare_queries++;
    if (record->prepare_queries < command->script.prepare_delay) {
        return FWLAB_HIF_PORT_IN_PROGRESS;
    }
    mint_prepared(command, record);
    prepare_result_fill(
        command, record, FWLAB_HIF_PREPARE_RESERVED, result
    );
    return FWLAB_HIF_PORT_OK;
}

static enum fwlab_hif_command_port_result prepare_abort_common(
    void *context,
    const struct fwlab_hif_prepared_token *prepared,
    bool *aborted)
{
    struct c42_fake_command *command = context;
    struct c42_fake_command_record *record;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    int omit;

    if (command == NULL || prepared == NULL || aborted == NULL) {
        return FWLAB_HIF_PORT_INVALID;
    }
    command->prepare_abort_call_count++;
    record = find_prepared(command, prepared);
    if (record == NULL) {
        return FWLAB_HIF_PORT_STALE;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_PREPARE_ABORT,
            &injected, &value, &omit)) {
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0) {
            record->retired = 1;
            mark_injected_effect(
                command, C42_FAKE_COMMAND_EFFECT_PREPARE_ABORTED
            );
        }
        if ((command->injection_write_mask & C42_FAKE_WRITE_VALUE) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_VALUE);
            *aborted = value != 0;
        }
        (void)omit;
        return injected;
    }
    if (record->admitted != 0) {
        return FWLAB_HIF_PORT_WRONG_STATE;
    }
    record->retired = 1;
    provider_output_mark(command, C42_FAKE_WRITE_VALUE);
    *aborted = true;
    return FWLAB_HIF_PORT_OK;
}

static enum fwlab_hif_command_port_result fake_prepare_abort(
    void *context,
    const struct fwlab_hif_prepared_token *prepared,
    bool *aborted)
{
    return prepare_abort_common(context, prepared, aborted);
}

static enum fwlab_hif_command_port_result fake_prepare_abort_query(
    void *context,
    const struct fwlab_hif_prepared_token *prepared,
    bool *aborted)
{
    return prepare_abort_common(context, prepared, aborted);
}

static enum fwlab_hif_command_port_result admit_common(
    void *context,
    const struct fwlab_hif_admission_key *key,
    const struct fwlab_nvme_command *canonical,
    enum fwlab_hif_admission_state *state,
    struct fwlab_hif_command_ticket *ticket)
{
    struct c42_fake_command *command = context;
    struct c42_fake_command_record *record;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    int omit;

    if (command == NULL || key == NULL || canonical == NULL || state == NULL ||
        ticket == NULL || key->client_uid == 0 || key->generation == 0 ||
        key->reserved != 0 || !fwlab_nvme_command_valid(canonical)) {
        return FWLAB_HIF_PORT_INVALID;
    }
    record = find_prepared(command, &key->prepared);
    if (record == NULL || record->prepare_key.client_uid != key->client_uid ||
        !handle_equal(&canonical->handle, &record->prepared.handle) ||
        !origin_equal(&canonical->origin, &record->prepared.origin)) {
        return FWLAB_HIF_PORT_STALE;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_ADMIT, &injected, &value, &omit)) {
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0 &&
            record->admitted == 0) {
            record->command = *canonical;
            record->ticket.handle = canonical->handle;
            record->ticket.origin = canonical->origin;
            record->ticket.ticket_uid = command->next_ticket_uid++;
            record->ticket.generation = command->next_generation++;
            record->admitted = 1;
            mark_injected_effect(
                command, C42_FAKE_COMMAND_EFFECT_ADMITTED
            );
        }
        if ((command->injection_write_mask & C42_FAKE_WRITE_VALUE) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_VALUE);
            *state = (enum fwlab_hif_admission_state)value;
        }
        if ((command->injection_write_mask & C42_FAKE_WRITE_OBJECT) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_OBJECT);
            memset(ticket, 0, sizeof(*ticket));
            if (command->injection_object_variant != C42_FAKE_OBJECT_ZERO) {
                ticket->handle = canonical->handle;
                ticket->origin = canonical->origin;
                ticket->ticket_uid = record->admitted != 0 ?
                                     record->ticket.ticket_uid :
                                     UINT64_C(0xf000000000000001);
                ticket->generation = record->admitted != 0 ?
                                     record->ticket.generation :
                                     key->generation;
                if (command->injection_object_variant ==
                    C42_FAKE_OBJECT_MISMATCH) {
                    ticket->ticket_uid++;
                }
            }
        }
        (void)omit;
        return injected;
    }
    if (record->admitted != 0) {
        provider_output_mark(
            command, C42_FAKE_WRITE_VALUE | C42_FAKE_WRITE_OBJECT
        );
        *state = FWLAB_HIF_ADMISSION_COMMITTED;
        *ticket = record->ticket;
        return FWLAB_HIF_PORT_OK;
    }
    record->admit_queries++;
    if (record->admit_queries <= command->script.admit_delay) {
        provider_output_mark(command, C42_FAKE_WRITE_VALUE);
        *state = FWLAB_HIF_ADMISSION_NOT_STARTED;
        return FWLAB_HIF_PORT_IN_PROGRESS;
    }
    record->command = *canonical;
    record->ticket.handle = canonical->handle;
    record->ticket.origin = canonical->origin;
    record->ticket.ticket_uid = command->next_ticket_uid++;
    record->ticket.generation = command->next_generation++;
    record->admitted = 1;
    provider_output_mark(
        command, C42_FAKE_WRITE_VALUE | C42_FAKE_WRITE_OBJECT
    );
    *state = FWLAB_HIF_ADMISSION_COMMITTED;
    *ticket = record->ticket;
    return FWLAB_HIF_PORT_OK;
}

static enum fwlab_hif_command_port_result fake_admit_start(
    void *context,
    const struct fwlab_hif_admission_key *key,
    const struct fwlab_nvme_command *command,
    enum fwlab_hif_admission_state *state,
    struct fwlab_hif_command_ticket *ticket)
{
    return admit_common(context, key, command, state, ticket);
}

static enum fwlab_hif_command_port_result fake_admit_query(
    void *context,
    const struct fwlab_hif_admission_key *key,
    const struct fwlab_nvme_command *command,
    enum fwlab_hif_admission_state *state,
    struct fwlab_hif_command_ticket *ticket)
{
    return admit_common(context, key, command, state, ticket);
}

static int admission_request_valid(
    const struct fwlab_hif_admission_key *key,
    const struct fwlab_nvme_command *command)
{
    return key != NULL && command != NULL && key->client_uid != 0 &&
           key->generation != 0 && key->reserved == 0 &&
           fwlab_hif_prepared_token_valid(&key->prepared) &&
           fwlab_nvme_command_valid(command);
}

static int admission_request_matches(
    struct c42_fake_command *provider,
    const struct fwlab_hif_admission_key *key,
    const struct fwlab_nvme_command *command)
{
    struct c42_fake_command_record *record;

    if (provider == NULL || !admission_request_valid(key, command)) {
        return 0;
    }
    record = find_prepared(provider, &key->prepared);
    return record != NULL &&
           record->prepare_key.client_uid == key->client_uid &&
           handle_equal(&command->handle, &record->prepared.handle) &&
           origin_equal(&command->origin, &record->prepared.origin);
}

static enum fwlab_hif_command_port_result fake_poll(
    void *context,
    uint32_t budget,
    struct fwlab_hif_ready_event *events,
    uint32_t capacity,
    uint32_t *count)
{
    struct c42_fake_command *command = context;
    struct c42_fake_command_record *selected = NULL;
    enum fwlab_hif_command_port_result injected;
    uint64_t injected_sequence = 0;
    uint32_t value;
    uint32_t index;
    int omit;

    if (command == NULL || count == NULL ||
        (capacity != 0 && events == NULL)) {
        return FWLAB_HIF_PORT_INVALID;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_POLL, &injected, &value, &omit)) {
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0) {
            for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
                struct c42_fake_command_record *record =
                    &command->records[index];

                if (record->in_use != 0 && record->admitted != 0 &&
                    record->ready_sent == 0 && record->retired == 0) {
                    selected = record;
                    record->ready_sent = 1;
                    injected_sequence = command->next_ready_sequence++;
                    mark_injected_effect(
                        command, C42_FAKE_COMMAND_EFFECT_READY
                    );
                    break;
                }
            }
        }
        if ((command->injection_write_mask & C42_FAKE_WRITE_VALUE) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_VALUE);
            *count = value;
        }
        if ((command->injection_write_mask & C42_FAKE_WRITE_OBJECT) != 0 &&
            capacity != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_OBJECT);
            memset(events, 0, sizeof(*events));
            if (selected != NULL &&
                command->injection_object_variant != C42_FAKE_OBJECT_ZERO) {
                events[0].version = FWLAB_HIF_COMMAND_PORT_VERSION;
                events[0].size = sizeof(events[0]);
                events[0].ticket = selected->ticket;
                events[0].sequence = injected_sequence;
                if (command->injection_object_variant ==
                    C42_FAKE_OBJECT_MISMATCH) {
                    events[0].ticket.ticket_uid++;
                }
            }
        }
        (void)omit;
        return injected;
    }
    provider_output_mark(command, C42_FAKE_WRITE_VALUE);
    *count = 0;
    if (budget == 0 || capacity == 0) {
        return FWLAB_HIF_PORT_OK;
    }
    for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
        struct c42_fake_command_record *record = &command->records[index];

        if (record->in_use == 0 || record->admitted == 0 ||
            record->ready_sent != 0 || record->retired != 0) {
            continue;
        }
        record->poll_queries++;
        if (record->poll_queries <= command->script.poll_delay) {
            continue;
        }
        selected = record;
        if (command->script.reverse_ready == 0) {
            break;
        }
    }
    if (selected == NULL) {
        return FWLAB_HIF_PORT_OK;
    }
    provider_output_mark(command, C42_FAKE_WRITE_OBJECT);
    memset(&events[0], 0, sizeof(events[0]));
    events[0].version = FWLAB_HIF_COMMAND_PORT_VERSION;
    events[0].size = sizeof(events[0]);
    events[0].ticket = selected->ticket;
    events[0].sequence = command->next_ready_sequence++;
    selected->ready_sent = 1;
    provider_output_mark(command, C42_FAKE_WRITE_VALUE);
    *count = 1;
    return FWLAB_HIF_PORT_OK;
}

static void intent_fill(
    const struct c42_fake_command *command,
    struct c42_fake_command_record *record)
{
    memset(&record->intent, 0, sizeof(record->intent));
    record->intent.version = FWLAB_NVME_COMPLETION_VERSION;
    record->intent.size = sizeof(record->intent);
    record->intent.handle = record->ticket.handle;
    record->intent.origin = record->ticket.origin;
    record->intent.result_dword0 = command->script.completion_result;
    record->intent.status_code = command->script.completion_status_code;
    record->intent.status_code_type = command->script.completion_status_type;
    record->intent.command_retry_delay =
        command->script.completion_retry_delay;
    record->intent.more = command->script.completion_more;
    record->intent.do_not_retry =
        command->script.completion_do_not_retry;
    record->intent.effect_class = FWLAB_NVME_EFFECT_NONE;
}

static enum fwlab_hif_command_port_result fake_completion_acquire(
    void *context,
    const struct fwlab_hif_command_ticket *ticket,
    struct fwlab_nvme_completion_intent *intent,
    struct fwlab_hif_completion_lease *lease)
{
    struct c42_fake_command *command = context;
    struct c42_fake_command_record *record;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    int omit;

    if (command == NULL || ticket == NULL || intent == NULL || lease == NULL) {
        return FWLAB_HIF_PORT_INVALID;
    }
    record = find_ticket(command, ticket);
    if (record == NULL || record->ready_sent == 0 || record->released != 0) {
        return FWLAB_HIF_PORT_STALE;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_COMPLETION_ACQUIRE,
            &injected, &value, &omit)) {
        (void)value;
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0 &&
            record->leased == 0) {
            intent_fill(command, record);
            record->lease.ticket = record->ticket;
            record->lease.lease_uid = command->next_lease_uid++;
            record->lease.generation = command->next_generation++;
            record->leased = 1;
            command->acquire_count++;
            mark_injected_effect(
                command, C42_FAKE_COMMAND_EFFECT_LEASED
            );
        }
        if ((command->injection_write_mask & C42_FAKE_WRITE_OBJECT) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_OBJECT);
            memset(intent, 0, sizeof(*intent));
            memset(lease, 0, sizeof(*lease));
            if (command->injection_object_variant != C42_FAKE_OBJECT_ZERO) {
                if (record->leased == 0) {
                    intent_fill(command, record);
                    record->lease.ticket = record->ticket;
                    record->lease.lease_uid = UINT64_C(0xf200000000000001);
                    record->lease.generation = 1;
                }
                *intent = record->intent;
                *lease = record->lease;
                if (command->injection_object_variant ==
                    C42_FAKE_OBJECT_MISMATCH) {
                    lease->lease_uid++;
                }
            }
        }
        (void)omit;
        return injected;
    }
    if (command->script.acquire_in_progress != 0) {
        command->script.acquire_in_progress--;
        return FWLAB_HIF_PORT_IN_PROGRESS;
    }
    if (record->leased == 0) {
        intent_fill(command, record);
        record->lease.ticket = record->ticket;
        record->lease.lease_uid = command->next_lease_uid++;
        record->lease.generation = command->next_generation++;
        record->leased = 1;
        command->acquire_count++;
    }
    provider_output_mark(command, C42_FAKE_WRITE_OBJECT);
    *intent = record->intent;
    *lease = record->lease;
    return FWLAB_HIF_PORT_OK;
}

static enum fwlab_hif_command_port_result completion_release_common(
    void *context,
    const struct fwlab_hif_completion_lease *lease,
    uint64_t client_uid,
    bool *released)
{
    struct c42_fake_command *command = context;
    struct c42_fake_command_record *record;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    int omit;

    if (command == NULL || lease == NULL || released == NULL ||
        client_uid == 0) {
        return FWLAB_HIF_PORT_INVALID;
    }
    record = find_lease(command, lease);
    if (record == NULL || record->consume_prepared != 0) {
        return FWLAB_HIF_PORT_STALE;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_COMPLETION_RELEASE,
            &injected, &value, &omit)) {
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0) {
            record->released = 1;
            mark_injected_effect(
                command, C42_FAKE_COMMAND_EFFECT_RELEASED
            );
        }
        if ((command->injection_write_mask & C42_FAKE_WRITE_VALUE) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_VALUE);
            *released = value != 0;
        }
        (void)omit;
        return injected;
    }
    record->released = 1;
    provider_output_mark(command, C42_FAKE_WRITE_VALUE);
    *released = true;
    return FWLAB_HIF_PORT_OK;
}

static enum fwlab_hif_command_port_result fake_completion_release_start(
    void *context,
    const struct fwlab_hif_completion_lease *lease,
    uint64_t client_uid,
    bool *released)
{
    return completion_release_common(context, lease, client_uid, released);
}

static enum fwlab_hif_command_port_result fake_completion_release_query(
    void *context,
    const struct fwlab_hif_completion_lease *lease,
    uint64_t client_uid,
    bool *released)
{
    return completion_release_common(context, lease, client_uid, released);
}

static enum fwlab_hif_command_port_result fake_consume_prepare(
    void *context,
    const struct fwlab_hif_completion_lease *lease,
    uint64_t publication_uid,
    struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state)
{
    struct c42_fake_command *command = context;
    struct c42_fake_command_record *record;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    int omit;

    if (command == NULL || lease == NULL || token == NULL || state == NULL ||
        publication_uid == 0) {
        return FWLAB_HIF_PORT_INVALID;
    }
    record = find_lease(command, lease);
    if (record == NULL || record->released != 0) {
        return FWLAB_HIF_PORT_STALE;
    }
    record->consume_prepare_queries++;
    if (injection_take(
            command, C42_FAKE_COMMAND_CONSUME_PREPARE,
            &injected, &value, &omit)) {
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0 &&
            record->consume_prepared == 0) {
            record->publication_uid = publication_uid;
            record->consume.lease = *lease;
            record->consume.publication_uid = publication_uid;
            record->consume.consume_uid = command->next_consume_uid++;
            record->consume.generation = command->next_generation++;
            record->consume_prepared = 1;
            mark_injected_effect(
                command, C42_FAKE_COMMAND_EFFECT_CONSUME_PREPARED
            );
        }
        if ((command->injection_write_mask & C42_FAKE_WRITE_VALUE) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_VALUE);
            *state = (enum fwlab_hif_consume_state)value;
        }
        if ((command->injection_write_mask & C42_FAKE_WRITE_OBJECT) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_OBJECT);
            memset(token, 0, sizeof(*token));
            if (command->injection_object_variant != C42_FAKE_OBJECT_ZERO) {
                token->lease = *lease;
                token->publication_uid = publication_uid;
                token->consume_uid = record->consume_prepared != 0 ?
                                     record->consume.consume_uid :
                                     UINT64_C(0xf100000000000001);
                token->generation = record->consume_prepared != 0 ?
                                    record->consume.generation : 1;
                if (command->injection_object_variant ==
                    C42_FAKE_OBJECT_MISMATCH) {
                    token->consume_uid++;
                }
            }
        }
        (void)omit;
        return injected;
    }
    if (record->consume_prepared == 0) {
        record->publication_uid = publication_uid;
        record->consume.lease = *lease;
        record->consume.publication_uid = publication_uid;
        record->consume.consume_uid = command->next_consume_uid++;
        record->consume.generation = command->next_generation++;
        record->consume_prepared = 1;
    } else if (record->publication_uid != publication_uid) {
        return FWLAB_HIF_PORT_STALE;
    }
    provider_output_mark(
        command, C42_FAKE_WRITE_VALUE | C42_FAKE_WRITE_OBJECT
    );
    *token = record->consume;
    *state = FWLAB_HIF_CONSUME_PREPARED;
    return FWLAB_HIF_PORT_OK;
}

static int consume_prepare_request_matches(
    struct c42_fake_command *command,
    const struct fwlab_hif_completion_lease *lease,
    uint64_t publication_uid)
{
    struct c42_fake_command_record *record;

    if (command == NULL || lease == NULL || publication_uid == 0) {
        return 0;
    }
    record = find_lease(command, lease);
    return record != NULL && record->released == 0 &&
           (record->consume_prepared == 0 ||
            record->publication_uid == publication_uid);
}

static enum fwlab_hif_command_port_result consume_abort_common(
    void *context,
    const struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state)
{
    struct c42_fake_command *command = context;
    struct c42_fake_command_record *record;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    int omit;

    if (command == NULL || token == NULL || state == NULL) {
        return FWLAB_HIF_PORT_INVALID;
    }
    record = find_consume(command, token);
    if (record == NULL || record->consume_committed != 0) {
        return FWLAB_HIF_PORT_STALE;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_CONSUME_ABORT,
            &injected, &value, &omit)) {
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0) {
            record->retired = 1;
            mark_injected_effect(
                command, C42_FAKE_COMMAND_EFFECT_CONSUME_ABORTED
            );
        }
        if ((command->injection_write_mask & C42_FAKE_WRITE_VALUE) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_VALUE);
            *state = (enum fwlab_hif_consume_state)value;
        }
        (void)omit;
        return injected;
    }
    record->retired = 1;
    provider_output_mark(command, C42_FAKE_WRITE_VALUE);
    *state = FWLAB_HIF_CONSUME_ABORTED;
    return FWLAB_HIF_PORT_OK;
}

static enum fwlab_hif_command_port_result fake_consume_abort(
    void *context,
    const struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state)
{
    return consume_abort_common(context, token, state);
}

static enum fwlab_hif_command_port_result fake_consume_abort_query(
    void *context,
    const struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state)
{
    return consume_abort_common(context, token, state);
}

static enum fwlab_hif_command_port_result fake_consume_commit(
    void *context,
    const struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state)
{
    struct c42_fake_command *command = context;
    struct c42_fake_command_record *record;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    int omit;

    if (command == NULL || token == NULL || state == NULL) {
        return FWLAB_HIF_PORT_INVALID;
    }
    record = find_consume(command, token);
    if (record == NULL) {
        return FWLAB_HIF_PORT_STALE;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_CONSUME_COMMIT,
            &injected, &value, &omit)) {
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0) {
            record->consume_committed = 1;
            mark_injected_effect(
                command, C42_FAKE_COMMAND_EFFECT_CONSUME_COMMITTED
            );
        }
        if ((command->injection_write_mask & C42_FAKE_WRITE_VALUE) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_VALUE);
            *state = (enum fwlab_hif_consume_state)value;
        }
        (void)omit;
        return injected;
    }
    record->consume_queries++;
    if (record->consume_queries <= command->script.consume_commit_delay) {
        provider_output_mark(command, C42_FAKE_WRITE_VALUE);
        *state = FWLAB_HIF_CONSUME_PREPARED;
        return FWLAB_HIF_PORT_IN_PROGRESS;
    }
    record->consume_committed = 1;
    provider_output_mark(command, C42_FAKE_WRITE_VALUE);
    *state = command->script.cleanup_pending != 0 ?
             FWLAB_HIF_CONSUME_CLEANUP_PENDING :
             FWLAB_HIF_CONSUME_COMMITTED;
    return FWLAB_HIF_PORT_OK;
}

static enum fwlab_hif_command_port_result fake_consume_query(
    void *context,
    const struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state)
{
    struct c42_fake_command *command = context;
    struct c42_fake_command_record *record;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    int omit;

    if (command == NULL || token == NULL || state == NULL) {
        return FWLAB_HIF_PORT_INVALID;
    }
    record = find_consume(command, token);
    if (record == NULL) {
        return FWLAB_HIF_PORT_STALE;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_CONSUME_QUERY,
            &injected, &value, &omit)) {
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0) {
            record->consume_committed = 1;
            mark_injected_effect(
                command, C42_FAKE_COMMAND_EFFECT_CONSUME_COMMITTED
            );
        }
        if ((command->injection_write_mask & C42_FAKE_WRITE_VALUE) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_VALUE);
            *state = (enum fwlab_hif_consume_state)value;
        }
        (void)omit;
        return injected;
    }
    if (record->consume_committed == 0) {
        return fake_consume_commit(context, token, state);
    }
    if (command->script.cleanup_pending != 0 &&
        record->cleanup_queries < command->script.cleanup_delay) {
        record->cleanup_queries++;
        provider_output_mark(command, C42_FAKE_WRITE_VALUE);
        *state = FWLAB_HIF_CONSUME_CLEANUP_PENDING;
    } else {
        provider_output_mark(command, C42_FAKE_WRITE_VALUE);
        *state = FWLAB_HIF_CONSUME_COMMITTED;
    }
    return FWLAB_HIF_PORT_OK;
}

static enum fwlab_hif_command_port_result fake_consume_retire(
    void *context,
    const struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state)
{
    struct c42_fake_command *command = context;
    struct c42_fake_command_record *record;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    int omit;

    if (command == NULL || token == NULL || state == NULL) {
        return FWLAB_HIF_PORT_INVALID;
    }
    record = find_consume(command, token);
    if (record == NULL || record->consume_committed == 0) {
        return FWLAB_HIF_PORT_STALE;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_CONSUME_RETIRE,
            &injected, &value, &omit)) {
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0) {
            record->retired = 1;
            mark_injected_effect(
                command, C42_FAKE_COMMAND_EFFECT_CONSUME_RETIRED
            );
        }
        if ((command->injection_write_mask & C42_FAKE_WRITE_VALUE) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_VALUE);
            *state = (enum fwlab_hif_consume_state)value;
        }
        (void)omit;
        return injected;
    }
    record->retired = 1;
    provider_output_mark(command, C42_FAKE_WRITE_VALUE);
    *state = FWLAB_HIF_CONSUME_RETIRED;
    return FWLAB_HIF_PORT_OK;
}

static enum fwlab_hif_command_port_result fake_reset_begin(
    void *context,
    uint64_t instance_nonce,
    uint32_t old_epoch)
{
    struct c42_fake_command *command = context;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    uint32_t index;
    int omit;

    if (command == NULL || instance_nonce != command->instance_nonce ||
        old_epoch != command->controller_epoch || old_epoch == UINT32_MAX) {
        return FWLAB_HIF_PORT_INVALID;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_RESET_BEGIN,
            &injected, &value, &omit)) {
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0) {
            command->reset_active = 1;
            command->reset_old_epoch = old_epoch;
            command->controller_epoch = old_epoch + 1u;
            for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
                command->records[index].retired = 1;
            }
            mark_injected_effect(
                command, C42_FAKE_COMMAND_EFFECT_RESET_BEGUN
            );
        }
        (void)value;
        (void)omit;
        return injected;
    }
    command->reset_active = 1;
    command->reset_old_epoch = old_epoch;
    command->controller_epoch = old_epoch + 1u;
    for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
        command->records[index].retired = 1;
    }
    return FWLAB_HIF_PORT_OK;
}

static enum fwlab_hif_command_port_result fake_reset_quiescent(
    void *context,
    uint64_t instance_nonce,
    uint32_t epoch,
    bool *quiescent)
{
    struct c42_fake_command *command = context;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    int omit;

    if (command == NULL || quiescent == NULL ||
        instance_nonce != command->instance_nonce ||
        command->reset_active == 0 || epoch != command->reset_old_epoch) {
        return FWLAB_HIF_PORT_INVALID;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_RESET_QUIESCENT,
            &injected, &value, &omit)) {
        if ((command->injection_write_mask & C42_FAKE_WRITE_VALUE) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_VALUE);
            *quiescent = value != 0;
        }
        (void)omit;
        return injected;
    }
    provider_output_mark(command, C42_FAKE_WRITE_VALUE);
    *quiescent = true;
    return FWLAB_HIF_PORT_OK;
}

static enum fwlab_hif_command_port_result fake_teardown_begin(
    void *context,
    uint64_t instance_nonce,
    uint32_t old_epoch)
{
    struct c42_fake_command *command = context;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    uint32_t index;
    int omit;

    if (command == NULL || instance_nonce != command->instance_nonce ||
        old_epoch != command->controller_epoch) {
        return FWLAB_HIF_PORT_INVALID;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_TEARDOWN_BEGIN,
            &injected, &value, &omit)) {
        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0) {
            command->teardown_active = 1;
            command->teardown_old_epoch = old_epoch;
            if (old_epoch != UINT32_MAX) {
                command->controller_epoch = old_epoch + 1u;
            }
            for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
                command->records[index].retired = 1;
            }
            mark_injected_effect(
                command, C42_FAKE_COMMAND_EFFECT_TEARDOWN_BEGUN
            );
        }
        (void)value;
        (void)omit;
        return injected;
    }
    command->teardown_active = 1;
    command->teardown_old_epoch = old_epoch;
    if (old_epoch != UINT32_MAX) {
        command->controller_epoch = old_epoch + 1u;
    }
    for (index = 0; index < C42_FAKE_COMMAND_RECORDS; ++index) {
        command->records[index].retired = 1;
    }
    return FWLAB_HIF_PORT_OK;
}

static enum fwlab_hif_command_port_result fake_teardown_quiescent(
    void *context,
    uint64_t instance_nonce,
    uint32_t epoch,
    bool *quiescent)
{
    struct c42_fake_command *command = context;
    enum fwlab_hif_command_port_result injected;
    uint32_t value;
    int omit;

    if (command == NULL || quiescent == NULL ||
        instance_nonce != command->instance_nonce ||
        command->teardown_active == 0 || epoch != command->teardown_old_epoch) {
        return FWLAB_HIF_PORT_INVALID;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_TEARDOWN_QUIESCENT,
            &injected, &value, &omit)) {
        if ((command->injection_write_mask & C42_FAKE_WRITE_VALUE) != 0) {
            provider_output_mark(command, C42_FAKE_WRITE_VALUE);
            *quiescent = value != 0;
        }
        (void)omit;
        return injected;
    }
    provider_output_mark(command, C42_FAKE_WRITE_VALUE);
    *quiescent = true;
    return FWLAB_HIF_PORT_OK;
}

static void log_command_event(
    struct c42_fake_command *command,
    uint32_t operation,
    uint8_t call_kind,
    enum fwlab_hif_command_port_result result,
    uint32_t value,
    uint8_t write_mask,
    int input_structural_valid,
    int input_record_match,
    int output_structural_valid,
    int output_record_match,
    uint64_t token_uid,
    uint64_t object_uid,
    uint32_t parameter0,
    uint32_t parameter1)
{
    struct c42_fake_event event = {0};

    event.token_uid = token_uid;
    event.object_uid = object_uid;
    event.operation = operation;
    event.direct_result = (uint32_t)result;
    event.output_value = value;
    event.parameter0 = parameter0;
    event.parameter1 = parameter1;
    event.provider = C42_FAKE_EVENT_COMMAND;
    event.call_kind = call_kind;
    event.input_structural_valid =
        (uint8_t)(input_structural_valid != 0);
    event.input_record_match = (uint8_t)(input_record_match != 0);
    event.output_structural_valid =
        (uint8_t)(output_structural_valid != 0);
    event.output_record_match = (uint8_t)(output_record_match != 0);
    event.reported_effect = value;
    event.output_write_mask = write_mask;
    if (command != NULL && command->injection_active != 0) {
        event.requested_effect = command->injection_requested_effect;
        event.applied_effect = command->injection_applied_effect;
        if (command->injection_applied_effect !=
            C42_FAKE_COMMAND_EFFECT_NONE) {
            event.flags |= C42_FAKE_EVENT_EFFECT_APPLIED;
            if (event.output_write_mask == 0 &&
                result == FWLAB_HIF_PORT_IN_PROGRESS) {
                event.flags |= C42_FAKE_EVENT_RESPONSE_LOST;
            }
        }
    }
    if (event.output_write_mask == 0) {
        event.output_structural_valid = 0;
        event.output_record_match = 0;
        event.output_value = 0;
        event.object_uid = 0;
    }
    if ((event.output_write_mask & C42_FAKE_EVENT_WRITE_VALUE) == 0) {
        event.output_value = 0;
        event.reported_effect = C42_FAKE_COMMAND_EFFECT_NONE;
    }
    c42_fake_event_append(
        command == NULL ? NULL : command->event_log, &event
    );
    if (command != NULL) {
        command->injection_active = 0;
        command->injection_write_mask = 0;
        command->injection_flags = 0;
        command->injection_object_variant = C42_FAKE_OBJECT_ZERO;
        command->injection_event_value = 0;
        command->injection_requested_effect =
            C42_FAKE_COMMAND_EFFECT_NONE;
        command->injection_applied_effect =
            C42_FAKE_COMMAND_EFFECT_NONE;
    }
}

static int prepare_value_valid(const struct fwlab_hif_prepare_result *result)
{
    return result != NULL &&
           result->version == FWLAB_HIF_COMMAND_PORT_VERSION &&
           result->size == sizeof(*result) &&
           result->disposition <= FWLAB_HIF_PREPARE_REJECTED;
}

static enum fwlab_hif_command_port_result logged_prepare_start(
    void *context,
    const struct fwlab_hif_prepare_key *key,
    struct fwlab_hif_prepare_result *result)
{
    int input_match = context != NULL && key != NULL &&
        find_prepare_key(context, key) != NULL;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_prepare_start(context, key, result);
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_PREPARE, C42_FAKE_CALL_START, call,
        result == NULL ? UINT32_MAX : result->disposition,
        write_mask,
        context != NULL && prepare_key_valid(context, key),
        input_match,
        (write_mask & C42_FAKE_EVENT_WRITE_VALUE) != 0 ?
            prepare_value_valid(result) :
            ((write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 &&
             fwlab_hif_prepared_token_valid(
                 result == NULL ? NULL : &result->prepared)),
        (write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 && result != NULL &&
            context != NULL &&
            find_prepared(context, &result->prepared) != NULL,
        key == NULL ? 0 : key->client_uid,
        result == NULL ? 0 : result->prepared.reservation_uid,
        key == NULL ? 0 : key->controller_epoch,
        key == NULL ? 0 : key->client_generation
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_prepare_query(
    void *context,
    const struct fwlab_hif_prepare_key *key,
    struct fwlab_hif_prepare_result *result)
{
    int input_match = context != NULL && key != NULL &&
        find_prepare_key(context, key) != NULL;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_prepare_query(context, key, result);
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_PREPARE, C42_FAKE_CALL_QUERY, call,
        result == NULL ? UINT32_MAX : result->disposition,
        write_mask,
        context != NULL && prepare_key_valid(context, key),
        input_match,
        (write_mask & C42_FAKE_EVENT_WRITE_VALUE) != 0 ?
            prepare_value_valid(result) :
            ((write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 &&
             fwlab_hif_prepared_token_valid(
                 result == NULL ? NULL : &result->prepared)),
        (write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 && result != NULL &&
            context != NULL &&
            find_prepared(context, &result->prepared) != NULL,
        key == NULL ? 0 : key->client_uid,
        result == NULL ? 0 : result->prepared.reservation_uid,
        key == NULL ? 0 : key->controller_epoch,
        key == NULL ? 0 : key->client_generation
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_prepare_abort(
    void *context,
    const struct fwlab_hif_prepared_token *prepared,
    bool *aborted)
{
    int input_match = context != NULL && prepared != NULL &&
        find_prepared(context, prepared) != NULL;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_prepare_abort(context, prepared, aborted);
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_PREPARE_ABORT, C42_FAKE_CALL_START, call,
        aborted != NULL && *aborted ? 1u : 0u,
        write_mask,
        fwlab_hif_prepared_token_valid(prepared),
        input_match,
        write_mask != 0,
        context != NULL && prepared != NULL && aborted != NULL &&
            find_prepared(context, prepared) != NULL &&
            find_prepared(context, prepared)->retired ==
                (uint8_t)(*aborted != 0),
        prepared == NULL ? 0 : prepared->reservation_uid, 0,
        prepared == NULL ? 0 : prepared->generation, 0
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_prepare_abort_query(
    void *context,
    const struct fwlab_hif_prepared_token *prepared,
    bool *aborted)
{
    int input_match = context != NULL && prepared != NULL &&
        find_prepared(context, prepared) != NULL;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_prepare_abort_query(context, prepared, aborted);
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_PREPARE_ABORT, C42_FAKE_CALL_QUERY, call,
        aborted != NULL && *aborted ? 1u : 0u,
        write_mask,
        fwlab_hif_prepared_token_valid(prepared),
        input_match,
        write_mask != 0,
        context != NULL && prepared != NULL && aborted != NULL &&
            find_prepared(context, prepared) != NULL &&
            find_prepared(context, prepared)->retired ==
                (uint8_t)(*aborted != 0),
        prepared == NULL ? 0 : prepared->reservation_uid, 0,
        prepared == NULL ? 0 : prepared->generation, 0
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_admit_start(
    void *context,
    const struct fwlab_hif_admission_key *key,
    const struct fwlab_nvme_command *command,
    enum fwlab_hif_admission_state *state,
    struct fwlab_hif_command_ticket *ticket)
{
    int input_match = admission_request_matches(context, key, command);
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_admit_start(context, key, command, state, ticket);
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_ADMIT, C42_FAKE_CALL_START, call,
        state == NULL ? UINT32_MAX : (uint32_t)*state, write_mask,
        admission_request_valid(key, command),
        input_match,
        write_mask != 0 &&
            (((write_mask & C42_FAKE_EVENT_WRITE_VALUE) == 0) ||
             (state != NULL && *state <= FWLAB_HIF_ADMISSION_POISONED)) &&
            (((write_mask & C42_FAKE_EVENT_WRITE_OBJECT) == 0) ||
             fwlab_hif_command_ticket_valid(ticket)),
        (write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 &&
            context != NULL && ticket != NULL &&
            find_ticket(context, ticket) != NULL,
        key == NULL ? 0 : key->client_uid,
        ticket == NULL ? 0 : ticket->ticket_uid,
        key == NULL ? 0 : key->generation,
        command == NULL ? 0 : command->handle.generation
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_admit_query(
    void *context,
    const struct fwlab_hif_admission_key *key,
    const struct fwlab_nvme_command *command,
    enum fwlab_hif_admission_state *state,
    struct fwlab_hif_command_ticket *ticket)
{
    int input_match = admission_request_matches(context, key, command);
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_admit_query(context, key, command, state, ticket);
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_ADMIT, C42_FAKE_CALL_QUERY, call,
        state == NULL ? UINT32_MAX : (uint32_t)*state, write_mask,
        admission_request_valid(key, command),
        input_match,
        write_mask != 0 &&
            (((write_mask & C42_FAKE_EVENT_WRITE_VALUE) == 0) ||
             (state != NULL && *state <= FWLAB_HIF_ADMISSION_POISONED)) &&
            (((write_mask & C42_FAKE_EVENT_WRITE_OBJECT) == 0) ||
             fwlab_hif_command_ticket_valid(ticket)),
        (write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 &&
            context != NULL && ticket != NULL &&
            find_ticket(context, ticket) != NULL,
        key == NULL ? 0 : key->client_uid,
        ticket == NULL ? 0 : ticket->ticket_uid,
        key == NULL ? 0 : key->generation,
        command == NULL ? 0 : command->handle.generation
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_poll(
    void *context,
    uint32_t budget,
    struct fwlab_hif_ready_event *events,
    uint32_t capacity,
    uint32_t *count)
{
    enum fwlab_hif_command_port_result call;
    int written;
    uint8_t write_mask;
    int valid;

    provider_output_begin(context);
    call = fake_poll(context, budget, events, capacity, count);
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;
    written = count != NULL && *count <= capacity;
    valid = written && (*count == 0 ||
                (events != NULL && events[0].sequence != 0));

    log_command_event(
        context, C42_FAKE_COMMAND_POLL, C42_FAKE_CALL_ACTION, call,
        count == NULL ? UINT32_MAX : *count, write_mask,
        count != NULL && (capacity == 0 || events != NULL),
        1,
        valid && (*count == 0 ||
                  fwlab_hif_command_ticket_valid(&events[0].ticket)),
        (write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 &&
            written && *count == 1 && context != NULL && events != NULL &&
            find_ticket(context, &events[0].ticket) != NULL,
        written && *count == 1 && events != NULL ?
            events[0].ticket.ticket_uid : 0,
        written && *count == 1 && events != NULL ?
            events[0].sequence : 0,
        budget, capacity
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_completion_acquire(
    void *context,
    const struct fwlab_hif_command_ticket *ticket,
    struct fwlab_nvme_completion_intent *intent,
    struct fwlab_hif_completion_lease *lease)
{
    int input_match = context != NULL && ticket != NULL &&
        find_ticket(context, ticket) != NULL;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;
    struct c42_fake_command_record *output_record;

    provider_output_begin(context);
    call = fake_completion_acquire(context, ticket, intent, lease);
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;
    output_record = context == NULL || lease == NULL ? NULL :
        find_lease(context, lease);

    log_command_event(
        context, C42_FAKE_COMMAND_COMPLETION_ACQUIRE,
        C42_FAKE_CALL_ACTION, call,
        lease == NULL ? 0 : (uint32_t)lease->lease_uid, write_mask,
        fwlab_hif_command_ticket_valid(ticket),
        input_match,
        write_mask != 0 && fwlab_hif_completion_lease_valid(lease) &&
            fwlab_nvme_completion_valid(intent),
        (write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 &&
            output_record != NULL && intent != NULL &&
            memcmp(&output_record->intent, intent, sizeof(*intent)) == 0,
        ticket == NULL ? 0 : ticket->ticket_uid,
        lease == NULL ? 0 : lease->lease_uid,
        lease == NULL ? 0 : lease->generation,
        intent == NULL ? 0 : intent->result_dword0
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_completion_release_start(
    void *context,
    const struct fwlab_hif_completion_lease *lease,
    uint64_t client_uid,
    bool *released)
{
    int input_match = context != NULL && lease != NULL && client_uid != 0 &&
        find_lease(context, lease) != NULL;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_completion_release_start(
        context, lease, client_uid, released
    );
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_COMPLETION_RELEASE,
        C42_FAKE_CALL_START, call,
        released != NULL && *released ? 1u : 0u,
        write_mask,
        fwlab_hif_completion_lease_valid(lease) && client_uid != 0,
        input_match,
        write_mask != 0,
        context != NULL && lease != NULL && released != NULL &&
            find_lease(context, lease) != NULL &&
            find_lease(context, lease)->released ==
                (uint8_t)(*released != 0),
        lease == NULL ? 0 : lease->lease_uid, client_uid,
        lease == NULL ? 0 : lease->generation, 0
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_completion_release_query(
    void *context,
    const struct fwlab_hif_completion_lease *lease,
    uint64_t client_uid,
    bool *released)
{
    int input_match = context != NULL && lease != NULL && client_uid != 0 &&
        find_lease(context, lease) != NULL;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_completion_release_query(
        context, lease, client_uid, released
    );
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_COMPLETION_RELEASE,
        C42_FAKE_CALL_QUERY, call,
        released != NULL && *released ? 1u : 0u,
        write_mask,
        fwlab_hif_completion_lease_valid(lease) && client_uid != 0,
        input_match,
        write_mask != 0,
        context != NULL && lease != NULL && released != NULL &&
            find_lease(context, lease) != NULL &&
            find_lease(context, lease)->released ==
                (uint8_t)(*released != 0),
        lease == NULL ? 0 : lease->lease_uid, client_uid,
        lease == NULL ? 0 : lease->generation, 0
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_consume_prepare(
    void *context,
    const struct fwlab_hif_completion_lease *lease,
    uint64_t publication_uid,
    struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state)
{
    int input_match = consume_prepare_request_matches(
        context, lease, publication_uid
    );
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_consume_prepare(
        context, lease, publication_uid, token, state
    );
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_CONSUME_PREPARE,
        C42_FAKE_CALL_START, call,
        state == NULL ? UINT32_MAX : (uint32_t)*state, write_mask,
        fwlab_hif_completion_lease_valid(lease) && publication_uid != 0,
        input_match,
        write_mask != 0 &&
            (((write_mask & C42_FAKE_EVENT_WRITE_VALUE) == 0) ||
             (state != NULL && *state <= FWLAB_HIF_CONSUME_POISONED)) &&
            (((write_mask & C42_FAKE_EVENT_WRITE_OBJECT) == 0) ||
             fwlab_hif_consume_token_valid(token)),
        (write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 &&
            context != NULL && token != NULL &&
            find_consume(context, token) != NULL,
        lease == NULL ? 0 : lease->lease_uid,
        token != NULL && fwlab_hif_consume_token_valid(token) ?
            token->consume_uid : 0,
        (uint32_t)publication_uid,
        lease == NULL ? 0 : lease->generation
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_consume_abort(
    void *context,
    const struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state)
{
    int input_match = context != NULL && token != NULL &&
        find_consume(context, token) != NULL;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_consume_abort(context, token, state);
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_CONSUME_ABORT, C42_FAKE_CALL_START, call,
        state == NULL ? UINT32_MAX : (uint32_t)*state,
        write_mask,
        fwlab_hif_consume_token_valid(token),
        input_match,
        write_mask != 0 && state != NULL &&
            *state <= FWLAB_HIF_CONSUME_POISONED,
        context != NULL && token != NULL && state != NULL &&
            find_consume(context, token) != NULL &&
            find_consume(context, token)->retired ==
                (uint8_t)(*state == FWLAB_HIF_CONSUME_ABORTED),
        token == NULL ? 0 : token->consume_uid, 0,
        token == NULL ? 0 : token->generation, 0
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_consume_abort_query(
    void *context,
    const struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state)
{
    int input_match = context != NULL && token != NULL &&
        find_consume(context, token) != NULL;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_consume_abort_query(context, token, state);
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_CONSUME_ABORT, C42_FAKE_CALL_QUERY, call,
        state == NULL ? UINT32_MAX : (uint32_t)*state,
        write_mask,
        fwlab_hif_consume_token_valid(token),
        input_match,
        write_mask != 0 && state != NULL &&
            *state <= FWLAB_HIF_CONSUME_POISONED,
        context != NULL && token != NULL && state != NULL &&
            find_consume(context, token) != NULL &&
            find_consume(context, token)->retired ==
                (uint8_t)(*state == FWLAB_HIF_CONSUME_ABORTED),
        token == NULL ? 0 : token->consume_uid, 0,
        token == NULL ? 0 : token->generation, 0
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_consume_commit(
    void *context,
    const struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state)
{
    int input_match = context != NULL && token != NULL &&
        find_consume(context, token) != NULL;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_consume_commit(context, token, state);
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_CONSUME_COMMIT,
        C42_FAKE_CALL_START, call,
        state == NULL ? UINT32_MAX : (uint32_t)*state,
        write_mask,
        fwlab_hif_consume_token_valid(token),
        input_match,
        write_mask != 0 && state != NULL &&
            *state <= FWLAB_HIF_CONSUME_POISONED,
        context != NULL && token != NULL && state != NULL &&
            find_consume(context, token) != NULL &&
            find_consume(context, token)->consume_committed ==
                (uint8_t)(*state == FWLAB_HIF_CONSUME_COMMITTED ||
                          *state == FWLAB_HIF_CONSUME_CLEANUP_PENDING),
        token == NULL ? 0 : token->consume_uid, 0,
        token == NULL ? 0 : token->generation, 0
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_consume_query(
    void *context,
    const struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state)
{
    int input_match = context != NULL && token != NULL &&
        find_consume(context, token) != NULL;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_consume_query(context, token, state);
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_CONSUME_QUERY, C42_FAKE_CALL_QUERY, call,
        state == NULL ? UINT32_MAX : (uint32_t)*state,
        write_mask,
        fwlab_hif_consume_token_valid(token),
        input_match,
        write_mask != 0 && state != NULL &&
            *state <= FWLAB_HIF_CONSUME_POISONED,
        context != NULL && token != NULL && state != NULL &&
            find_consume(context, token) != NULL &&
            find_consume(context, token)->consume_committed ==
                (uint8_t)(*state == FWLAB_HIF_CONSUME_COMMITTED ||
                          *state == FWLAB_HIF_CONSUME_CLEANUP_PENDING),
        token == NULL ? 0 : token->consume_uid, 0,
        token == NULL ? 0 : token->generation, 0
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_consume_retire(
    void *context,
    const struct fwlab_hif_consume_token *token,
    enum fwlab_hif_consume_state *state)
{
    int input_match = context != NULL && token != NULL &&
        find_consume(context, token) != NULL;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_consume_retire(context, token, state);
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_CONSUME_RETIRE,
        C42_FAKE_CALL_ACTION, call,
        state == NULL ? UINT32_MAX : (uint32_t)*state,
        write_mask,
        fwlab_hif_consume_token_valid(token),
        input_match,
        write_mask != 0 && state != NULL &&
            *state <= FWLAB_HIF_CONSUME_POISONED,
        context != NULL && token != NULL && state != NULL &&
            find_consume(context, token) != NULL &&
            find_consume(context, token)->retired ==
                (uint8_t)(*state == FWLAB_HIF_CONSUME_RETIRED),
        token == NULL ? 0 : token->consume_uid, 0,
        token == NULL ? 0 : token->generation, 0
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_reset_begin(
    void *context, uint64_t instance_nonce, uint32_t old_epoch)
{
    int input_match = context != NULL &&
        instance_nonce == ((struct c42_fake_command *)context)->instance_nonce &&
        old_epoch == ((struct c42_fake_command *)context)->controller_epoch &&
        old_epoch != UINT32_MAX;
    enum fwlab_hif_command_port_result call;

    provider_output_begin(context);
    call = fake_reset_begin(context, instance_nonce, old_epoch);

    log_command_event(
        context, C42_FAKE_COMMAND_RESET_BEGIN, C42_FAKE_CALL_START,
        call, old_epoch, 0,
        instance_nonce != 0,
        input_match,
        1, 1, instance_nonce, 0, old_epoch, 0
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_reset_quiescent(
    void *context, uint64_t instance_nonce, uint32_t epoch, bool *quiescent)
{
    int input_match = context != NULL &&
        instance_nonce == ((struct c42_fake_command *)context)->instance_nonce &&
        ((struct c42_fake_command *)context)->reset_active != 0 &&
        ((struct c42_fake_command *)context)->reset_old_epoch == epoch;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_reset_quiescent(
        context, instance_nonce, epoch, quiescent
    );
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_RESET_QUIESCENT, C42_FAKE_CALL_QUERY, call,
        quiescent != NULL && *quiescent ? 1u : 0u,
        write_mask,
        instance_nonce != 0,
        input_match,
        write_mask != 0,
        call == FWLAB_HIF_PORT_OK && quiescent != NULL && *quiescent,
        instance_nonce, 0, epoch, 0
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_teardown_begin(
    void *context, uint64_t instance_nonce, uint32_t old_epoch)
{
    int input_match = context != NULL &&
        instance_nonce == ((struct c42_fake_command *)context)->instance_nonce &&
        old_epoch == ((struct c42_fake_command *)context)->controller_epoch;
    enum fwlab_hif_command_port_result call;

    provider_output_begin(context);
    call = fake_teardown_begin(context, instance_nonce, old_epoch);

    log_command_event(
        context, C42_FAKE_COMMAND_TEARDOWN_BEGIN, C42_FAKE_CALL_START,
        call, old_epoch, 0,
        instance_nonce != 0,
        input_match,
        1, 1, instance_nonce, 0, old_epoch, 0
    );
    return call;
}

static enum fwlab_hif_command_port_result logged_teardown_quiescent(
    void *context, uint64_t instance_nonce, uint32_t epoch, bool *quiescent)
{
    int input_match = context != NULL &&
        instance_nonce == ((struct c42_fake_command *)context)->instance_nonce &&
        ((struct c42_fake_command *)context)->teardown_active != 0 &&
        ((struct c42_fake_command *)context)->teardown_old_epoch == epoch;
    enum fwlab_hif_command_port_result call;
    uint8_t write_mask;

    provider_output_begin(context);
    call = fake_teardown_quiescent(
        context, instance_nonce, epoch, quiescent
    );
    write_mask = context == NULL ? 0 :
        ((struct c42_fake_command *)context)->provider_write_mask;

    log_command_event(
        context, C42_FAKE_COMMAND_TEARDOWN_QUIESCENT,
        C42_FAKE_CALL_QUERY, call,
        quiescent != NULL && *quiescent ? 1u : 0u,
        write_mask,
        instance_nonce != 0,
        input_match,
        write_mask != 0,
        call == FWLAB_HIF_PORT_OK && quiescent != NULL && *quiescent,
        instance_nonce, 0, epoch, 0
    );
    return call;
}

static const struct fwlab_hif_command_port_ops C42_FAKE_COMMAND_OPS = {
    .version = FWLAB_HIF_COMMAND_PORT_VERSION,
    .size = sizeof(struct fwlab_hif_command_port_ops),
    .prepare_start = logged_prepare_start,
    .prepare_query = logged_prepare_query,
    .prepare_abort = logged_prepare_abort,
    .prepare_abort_query = logged_prepare_abort_query,
    .admit_start = logged_admit_start,
    .admit_query = logged_admit_query,
    .poll = logged_poll,
    .completion_acquire = logged_completion_acquire,
    .completion_release_start = logged_completion_release_start,
    .completion_release_query = logged_completion_release_query,
    .consume_prepare = logged_consume_prepare,
    .consume_abort = logged_consume_abort,
    .consume_abort_query = logged_consume_abort_query,
    .consume_commit = logged_consume_commit,
    .consume_query = logged_consume_query,
    .consume_retire = logged_consume_retire,
    .reset_begin = logged_reset_begin,
    .reset_quiescent = logged_reset_quiescent,
    .teardown_begin = logged_teardown_begin,
    .teardown_quiescent = logged_teardown_quiescent,
};

struct fwlab_hif_command_port c42_fake_command_port(
    struct c42_fake_command *command)
{
    struct fwlab_hif_command_port port = {0};

    port.ops = &C42_FAKE_COMMAND_OPS;
    port.context = command;
    return port;
}
