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

static void prepare_result_fill(
    const struct c42_fake_command_record *record,
    uint32_t disposition,
    struct fwlab_hif_prepare_result *result)
{
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_HIF_COMMAND_PORT_VERSION;
    result->size = sizeof(*result);
    result->disposition = disposition;
    if (record != NULL && disposition == FWLAB_HIF_PREPARE_RESERVED) {
        result->prepared = record->prepared;
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

enum c42_result c42_fake_command_injection_push(
    struct c42_fake_command *command,
    const struct c42_fake_command_injection *injection)
{
    if (command == NULL || injection == NULL ||
        command->injection_count >= C42_FAKE_COMMAND_INJECTIONS ||
        injection->operation < C42_FAKE_COMMAND_PREPARE ||
        injection->operation > C42_FAKE_COMMAND_TEARDOWN_QUIESCENT ||
        injection->result > FWLAB_HIF_PORT_COUNTER_EXHAUSTED + 2u ||
        injection->omit_outputs > 1 || injection->reserved[0] != 0 ||
        injection->reserved[1] != 0 || injection->reserved[2] != 0) {
        return C42_INVALID;
    }
    command->injections[command->injection_count] = *injection;
    command->injection_count++;
    return C42_OK;
}

static int injection_take(
    struct c42_fake_command *command,
    uint32_t operation,
    enum fwlab_hif_command_port_result *result,
    uint32_t *value,
    int *omit_outputs)
{
    if (command->injection_index < command->injection_count) {
        const struct c42_fake_command_injection *injection =
            &command->injections[command->injection_index];

        if (injection->operation != operation) {
            return 0;
        }
        command->injection_index++;
        *result = (enum fwlab_hif_command_port_result)injection->result;
        *value = injection->value;
        *omit_outputs = injection->omit_outputs != 0;
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
        if (omit == 0) {
            prepare_result_fill(NULL, value, result);
        }
        return injected;
    }
    record = find_prepare_key(command, key);
    if (record != NULL && record->prepared.reservation_uid != 0) {
        prepare_result_fill(record, FWLAB_HIF_PREPARE_RESERVED, result);
        return FWLAB_HIF_PORT_OK;
    }
    command->prepare_attempts++;
    if (command->prepare_attempts <= command->script.prepare_backpressure ||
        active_count(command) >= command->active_limit) {
        prepare_result_fill(NULL, FWLAB_HIF_PREPARE_BACKPRESSURE, result);
        return FWLAB_HIF_PORT_OK;
    }
    if (record == NULL) {
        record = allocate_record(command);
        if (record == NULL) {
            prepare_result_fill(NULL, FWLAB_HIF_PREPARE_BACKPRESSURE, result);
            return FWLAB_HIF_PORT_OK;
        }
        record->prepare_key = *key;
    }
    if (command->script.prepare_delay != 0) {
        return FWLAB_HIF_PORT_IN_PROGRESS;
    }
    mint_prepared(command, record);
    prepare_result_fill(record, FWLAB_HIF_PREPARE_RESERVED, result);
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
        if (omit == 0) {
            prepare_result_fill(record, value, result);
        }
        return injected;
    }
    record->prepare_queries++;
    if (record->prepare_queries < command->script.prepare_delay) {
        return FWLAB_HIF_PORT_IN_PROGRESS;
    }
    mint_prepared(command, record);
    prepare_result_fill(record, FWLAB_HIF_PREPARE_RESERVED, result);
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
        if (omit == 0) {
            *aborted = value != 0;
        }
        return injected;
    }
    if (record->admitted != 0) {
        return FWLAB_HIF_PORT_WRONG_STATE;
    }
    record->retired = 1;
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
        if (omit == 0) {
            *state = (enum fwlab_hif_admission_state)value;
            memset(ticket, 0, sizeof(*ticket));
        }
        return injected;
    }
    if (record->admitted != 0) {
        *state = FWLAB_HIF_ADMISSION_COMMITTED;
        *ticket = record->ticket;
        return FWLAB_HIF_PORT_OK;
    }
    record->admit_queries++;
    if (record->admit_queries <= command->script.admit_delay) {
        *state = FWLAB_HIF_ADMISSION_NOT_STARTED;
        return FWLAB_HIF_PORT_IN_PROGRESS;
    }
    record->command = *canonical;
    record->ticket.handle = canonical->handle;
    record->ticket.origin = canonical->origin;
    record->ticket.ticket_uid = command->next_ticket_uid++;
    record->ticket.generation = command->next_generation++;
    record->admitted = 1;
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
    uint32_t value;
    uint32_t index;
    int omit;

    if (command == NULL || count == NULL ||
        (capacity != 0 && events == NULL)) {
        return FWLAB_HIF_PORT_INVALID;
    }
    if (injection_take(
            command, C42_FAKE_COMMAND_POLL, &injected, &value, &omit)) {
        if (omit == 0) {
            *count = value;
            if (capacity != 0) {
                memset(events, 0, sizeof(*events));
            }
        }
        return injected;
    }
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
    memset(&events[0], 0, sizeof(events[0]));
    events[0].version = FWLAB_HIF_COMMAND_PORT_VERSION;
    events[0].size = sizeof(events[0]);
    events[0].ticket = selected->ticket;
    events[0].sequence = command->next_ready_sequence++;
    selected->ready_sent = 1;
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
        if (omit == 0) {
            memset(intent, 0, sizeof(*intent));
            memset(lease, 0, sizeof(*lease));
        }
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
        if (omit == 0) {
            *released = value != 0;
        }
        return injected;
    }
    record->released = 1;
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
        if (omit == 0) {
            memset(token, 0, sizeof(*token));
            *state = (enum fwlab_hif_consume_state)value;
        }
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
    *token = record->consume;
    *state = FWLAB_HIF_CONSUME_PREPARED;
    return FWLAB_HIF_PORT_OK;
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
        if (omit == 0) {
            *state = (enum fwlab_hif_consume_state)value;
        }
        return injected;
    }
    record->retired = 1;
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
        if (omit == 0) {
            *state = (enum fwlab_hif_consume_state)value;
        }
        return injected;
    }
    record->consume_queries++;
    if (record->consume_queries <= command->script.consume_commit_delay) {
        *state = FWLAB_HIF_CONSUME_PREPARED;
        return FWLAB_HIF_PORT_IN_PROGRESS;
    }
    record->consume_committed = 1;
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
        if (omit == 0) {
            *state = (enum fwlab_hif_consume_state)value;
        }
        return injected;
    }
    if (record->consume_committed == 0) {
        return fake_consume_commit(context, token, state);
    }
    if (command->script.cleanup_pending != 0 &&
        record->cleanup_queries < command->script.cleanup_delay) {
        record->cleanup_queries++;
        *state = FWLAB_HIF_CONSUME_CLEANUP_PENDING;
    } else {
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
        if (omit == 0) {
            *state = (enum fwlab_hif_consume_state)value;
        }
        return injected;
    }
    record->retired = 1;
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
        if (omit == 0) {
            *quiescent = value != 0;
        }
        return injected;
    }
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
        if (omit == 0) {
            *quiescent = value != 0;
        }
        return injected;
    }
    *quiescent = true;
    return FWLAB_HIF_PORT_OK;
}

static const struct fwlab_hif_command_port_ops C42_FAKE_COMMAND_OPS = {
    .version = FWLAB_HIF_COMMAND_PORT_VERSION,
    .size = sizeof(struct fwlab_hif_command_port_ops),
    .prepare_start = fake_prepare_start,
    .prepare_query = fake_prepare_query,
    .prepare_abort = fake_prepare_abort,
    .prepare_abort_query = fake_prepare_abort_query,
    .admit_start = fake_admit_start,
    .admit_query = fake_admit_query,
    .poll = fake_poll,
    .completion_acquire = fake_completion_acquire,
    .completion_release_start = fake_completion_release_start,
    .completion_release_query = fake_completion_release_query,
    .consume_prepare = fake_consume_prepare,
    .consume_abort = fake_consume_abort,
    .consume_abort_query = fake_consume_abort_query,
    .consume_commit = fake_consume_commit,
    .consume_query = fake_consume_query,
    .consume_retire = fake_consume_retire,
    .reset_begin = fake_reset_begin,
    .reset_quiescent = fake_reset_quiescent,
    .teardown_begin = fake_teardown_begin,
    .teardown_quiescent = fake_teardown_quiescent,
};

struct fwlab_hif_command_port c42_fake_command_port(
    struct c42_fake_command *command)
{
    struct fwlab_hif_command_port port = {0};

    port.ops = &C42_FAKE_COMMAND_OPS;
    port.context = command;
    return port;
}
