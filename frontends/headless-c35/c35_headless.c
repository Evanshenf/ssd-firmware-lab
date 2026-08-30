/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_headless.h"

#include <string.h>

#define C35_NO_ACTIVE_SLOT UINT8_C(0xff)

static int bytes_zero(const uint8_t *bytes, size_t length)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        if (bytes[index] != 0) return 0;
    }
    return 1;
}

static int request_valid(const struct c35_request *request)
{
    unsigned int atom;

    if (request == NULL || request->version != C35_REQUEST_VERSION ||
        request->size != sizeof(*request) || request->kind > C35_FENCE ||
        request->reserved[0] != 0 || request->reserved[1] != 0)
        return 0;
    if (request->kind == C35_READ) {
        return request->durability_kind == 0 && request->atom_mask == 0 &&
               request->atom < C35_ATOMS && request->sequence == 0 &&
               request->frontier == 0 &&
               bytes_zero(&request->payload[0][0], sizeof(request->payload));
    }
    if (request->kind == C35_FENCE) {
        return request->durability_kind == 2 && request->atom_mask == 0 &&
               request->sequence == 0 && request->frontier != 0 &&
               bytes_zero(&request->payload[0][0], sizeof(request->payload));
    }
    if ((request->durability_kind != 0 && request->durability_kind != 1) ||
        request->atom_mask == 0 ||
        (request->atom_mask & ~UINT8_C(0x03)) != 0 ||
        request->sequence == 0 || request->frontier != 0)
        return 0;
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        if ((request->atom_mask & (uint8_t)(1u << atom)) == 0 &&
            !bytes_zero(request->payload[atom], C35_ATOM_BYTES)) return 0;
        if (request->kind == C35_TRIM &&
            !bytes_zero(request->payload[atom], C35_ATOM_BYTES)) return 0;
    }
    return 1;
}

static int request_equal(
    const struct c35_request *left,
    const struct c35_request *right
)
{
    return request_valid(left) && request_valid(right) &&
           memcmp(left, right, sizeof(*left)) == 0;
}

static int token_equal(
    const struct c35_operation_token *left,
    const struct c35_operation_token *right
)
{
    return left != NULL && right != NULL &&
           left->instance_nonce == right->instance_nonce &&
           left->uid == right->uid &&
           left->generation == right->generation && left->kind == right->kind &&
           left->reserved[0] == 0 && left->reserved[1] == 0 &&
           left->reserved[2] == 0;
}

static int command_value_valid(
    const struct c35_headless *headless,
    const struct fwlab_c31_command_handle *command
)
{
    return command->instance_nonce == headless->instance_nonce &&
           command->command_uid != 0 && command->controller_epoch != 0 &&
           command->slot_generation != 0;
}

static int command_equal(
    const struct fwlab_c31_command_handle *left,
    const struct fwlab_c31_command_handle *right
)
{
    return left != NULL && right != NULL &&
           left->instance_nonce == right->instance_nonce &&
           left->command_uid == right->command_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->slot == right->slot &&
           left->slot_generation == right->slot_generation;
}

static int lease_value_valid(
    const struct c35_headless *headless,
    const struct fwlab_c31_completion_lease *lease,
    const struct fwlab_c31_command_handle *command
)
{
    return command_value_valid(headless, &lease->command) &&
           lease->command.instance_nonce == command->instance_nonce &&
           lease->command.command_uid == command->command_uid &&
           lease->command.controller_epoch == command->controller_epoch &&
           lease->command.slot == command->slot &&
           lease->command.slot_generation == command->slot_generation &&
           lease->lease_generation != 0 && lease->reserved == 0;
}

static int ticket_value_valid(
    const struct c35_headless *headless,
    const struct fwlab_c31_abort_ticket *ticket,
    const struct fwlab_c31_command_handle *command
)
{
    return command_value_valid(headless, &ticket->command) &&
           ticket->command.instance_nonce == command->instance_nonce &&
           ticket->command.command_uid == command->command_uid &&
           ticket->command.controller_epoch == command->controller_epoch &&
           ticket->command.slot == command->slot &&
           ticket->command.slot_generation == command->slot_generation &&
           ticket->ticket_generation != 0 && ticket->reserved == 0;
}

static enum c35_result map_c31(enum fwlab_c31_api_result result)
{
    switch (result) {
    case FWLAB_C31_API_OK: return C35_OK;
    case FWLAB_C31_API_NO_CAPACITY: return C35_NO_CAPACITY;
    case FWLAB_C31_API_INVALID_CONTRACT: return C35_INVALID;
    case FWLAB_C31_API_WRONG_STATE: return C35_WRONG_STATE;
    case FWLAB_C31_API_STALE_TOKEN: return C35_STALE;
    case FWLAB_C31_API_UNSUPPORTED_VERSION: return C35_UNSUPPORTED_VERSION;
    case FWLAB_C31_API_COUNTER_EXHAUSTED: return C35_COUNTER_EXHAUSTED;
    case FWLAB_C31_API_INVARIANT_FAILURE: return C35_INVARIANT;
    case FWLAB_C31_API_NOT_FOUND: return C35_NOT_FOUND;
    default: return C35_INVARIANT;
    }
}

static int semantic_valid(const struct c35_semantic_result *semantic)
{
    unsigned int atom;

    if (semantic == NULL || semantic->status > 5 ||
        semantic->request_kind > C35_FENCE ||
        (semantic->atom_mask & ~UINT8_C(0x03)) != 0 ||
        (semantic->present_mask & ~UINT8_C(0x03)) != 0 ||
        semantic->reserved0[0] != 0 || semantic->reserved0[1] != 0)
        return 0;
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        if (semantic->logical_kind[atom] > 2) return 0;
    }
    return 1;
}

static void descriptor_make(
    struct c35_headless *headless,
    uint64_t identity,
    struct fwlab_c31_command_descriptor *descriptor,
    struct fwlab_c31_request_token *token
)
{
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->version = FWLAB_C31_CONTRACT_VERSION;
    descriptor->size = sizeof(*descriptor);
    descriptor->origin.word[0] = headless->instance_nonce ^
                                 UINT64_C(0x3511000000000000) ^ identity;
    descriptor->origin.word[1] = ((uint64_t)headless->owner_epoch << 32) |
                                 identity;
    descriptor->trace_cookie = UINT64_C(0x3500000000000000) | identity;
    token->word[0] = UINT64_C(0xc350000000000000) | identity;
    token->word[1] = headless->instance_nonce ^ identity;
    descriptor->provider_request = *token;
    descriptor->provider_kind = FWLAB_C31_PROVIDER_NFC;
    descriptor->dma_direction = FWLAB_C31_DMA_NONE;
}

static struct c35_txid txid_make(
    const struct c35_headless *headless,
    const struct c35_operation_token *token
)
{
    struct c35_txid txid;

    txid.instance_nonce = token->instance_nonce;
    txid.uid = token->uid;
    txid.owner_epoch = headless->owner_epoch;
    txid.generation = token->generation;
    return txid;
}

static struct c35_operation_record *record_find(
    struct c35_headless *headless,
    const struct c35_operation_token *token
)
{
    unsigned int index;

    if (headless->control_active && token_equal(&headless->control.token, token))
        return &headless->control;
    if (headless->previous_control_used &&
        token_equal(&headless->previous_control.token, token))
        return &headless->previous_control;
    for (index = 0; index < C35_OPERATION_SLOTS; ++index) {
        if (headless->operation[index].used &&
            token_equal(&headless->operation[index].token, token))
            return &headless->operation[index];
    }
    return NULL;
}

static const struct c35_operation_record *record_find_const(
    const struct c35_headless *headless,
    const struct c35_operation_token *token
)
{
    unsigned int index;

    if (headless->control_active && token_equal(&headless->control.token, token))
        return &headless->control;
    if (headless->previous_control_used &&
        token_equal(&headless->previous_control.token, token))
        return &headless->previous_control;
    for (index = 0; index < C35_OPERATION_SLOTS; ++index) {
        if (headless->operation[index].used &&
            token_equal(&headless->operation[index].token, token))
            return &headless->operation[index];
    }
    return NULL;
}

static int token_available(const struct c35_headless *headless)
{
    return headless->next_operation_uid != 0 &&
           headless->next_operation_uid <= headless->operation_uid_limit &&
           headless->next_operation_uid <= UINT32_MAX;
}

static int control_token_available(const struct c35_headless *headless)
{
    return headless->next_control_uid != 0 &&
           headless->next_control_uid <= headless->control_uid_limit &&
           headless->next_control_uid <= UINT32_MAX;
}

static int teardown_token_available(const struct c35_headless *headless)
{
    return headless->next_teardown_uid != 0 &&
           headless->next_teardown_uid <= headless->teardown_uid_limit &&
           headless->next_teardown_uid <= UINT32_MAX;
}

static enum c35_result token_allocate(
    struct c35_headless *headless,
    uint8_t kind,
    struct c35_operation_token *token
)
{
    uint64_t uid;

    if (!token_available(headless)) return C35_COUNTER_EXHAUSTED;
    uid = headless->next_operation_uid++;
    memset(token, 0, sizeof(*token));
    token->instance_nonce = headless->instance_nonce;
    token->uid = uid;
    token->generation = (uint32_t)uid;
    token->kind = kind;
    return C35_OK;
}

static enum c35_result control_token_allocate(
    struct c35_headless *headless,
    uint8_t kind,
    struct c35_operation_token *token
)
{
    uint64_t uid;

    if (kind != C35_OPERATION_RESET) return C35_INVALID;
    if (!control_token_available(headless)) return C35_COUNTER_EXHAUSTED;
    uid = headless->next_control_uid++;
    memset(token, 0, sizeof(*token));
    token->instance_nonce = headless->instance_nonce;
    token->uid = uid;
    token->generation = (uint32_t)uid;
    token->kind = kind;
    return C35_OK;
}

static enum c35_result teardown_token_prepare(
    const struct c35_headless *headless,
    struct c35_operation_token *token
)
{
    uint64_t uid;

    if (!teardown_token_available(headless)) return C35_COUNTER_EXHAUSTED;
    uid = headless->next_teardown_uid;
    memset(token, 0, sizeof(*token));
    token->instance_nonce = headless->instance_nonce;
    token->uid = uid;
    token->generation = (uint32_t)uid;
    token->kind = C35_OPERATION_TEARDOWN;
    return C35_OK;
}

static struct c35_operation_record *operation_allocate(
    struct c35_headless *headless,
    uint8_t kind,
    struct c35_operation_token *token
)
{
    unsigned int index;

    for (index = 0; index < C35_OPERATION_SLOTS; ++index) {
        if (!headless->operation[index].used) {
            struct c35_operation_record *record = &headless->operation[index];

            memset(record, 0, sizeof(*record));
            if (token_allocate(headless, kind, token) != C35_OK) return NULL;
            record->used = 1;
            record->kind = kind;
            record->token = *token;
            record->outcome = C35_IN_PROGRESS;
            record->commit_state = C35_COMMIT_NOT_STARTED;
            record->cleanup_state = C35_CLEANUP_NONE;
            headless->active_slot = (uint8_t)index;
            return record;
        }
    }
    return NULL;
}

static enum c35_result control_allocate(
    struct c35_headless *headless,
    uint8_t kind,
    struct c35_operation_token *token
)
{
    if (kind != C35_OPERATION_RESET) return C35_INVALID;
    if (headless->control_active) return C35_WRONG_STATE;
    memset(&headless->control, 0, sizeof(headless->control));
    if (control_token_allocate(headless, kind, token) != C35_OK)
        return C35_COUNTER_EXHAUSTED;
    headless->control.used = 1;
    headless->control.kind = kind;
    headless->control.token = *token;
    headless->control.outcome = C35_IN_PROGRESS;
    headless->control.commit_state = C35_COMMIT_NOT_STARTED;
    headless->control.cleanup_state = C35_CLEANUP_NONE;
    headless->control_active = 1;
    return C35_OK;
}

static void publication_base(
    const struct c35_headless *headless,
    struct c35_operation_record *record,
    uint8_t kind,
    uint32_t epoch
)
{
    struct c35_publication *publication = &record->publication;

    memset(publication, 0, sizeof(*publication));
    publication->version = C35_PUBLICATION_VERSION;
    publication->size = sizeof(*publication);
    publication->kind = kind;
    publication->actor = headless->actor;
    publication->epoch = epoch;
    publication->commit_state = C35_COMMIT_COMMITTED;
    publication->publication_uid =
        UINT64_C(0x35a0000000000000) ^ record->token.uid ^
        ((uint64_t)kind << 48);
}

static void completion_publication(
    const struct c35_headless *headless,
    struct c35_operation_record *record
)
{
    struct c35_publication *publication;

    publication_base(
        headless, record, C35_PUBLICATION_COMMAND, headless->owner_epoch);
    publication = &record->publication;
    publication->request_kind = record->semantic.request_kind;
    publication->terminal =
        record->intent.result == FWLAB_C31_COMPLETION_SUCCESS ?
            FWLAB_C31_PROVIDER_SUCCESS : FWLAB_C31_PROVIDER_FAILED;
    publication->completion_result = (uint8_t)record->intent.result;
    publication->effect_class = record->intent.fault.effect_class;
    publication->witness_class = record->semantic.witness_class;
    publication->witness_reason = record->semantic.witness_reason;
    publication->status = record->semantic.status;
    publication->atom_mask = record->semantic.atom_mask;
    publication->present_mask = record->semantic.present_mask;
    publication->semantic = record->semantic;
}

static void record_finish(
    struct c35_headless *headless,
    struct c35_operation_record *record,
    enum c35_result outcome,
    uint32_t commit_state,
    uint32_t cleanup_state
)
{
    unsigned int index;

    record->outcome = (uint32_t)outcome;
    record->commit_state = commit_state;
    record->cleanup_state = cleanup_state;
    record->phase = C35_INTERNAL_DONE;
    record->finished = 1;
    for (index = 0; index < C35_OPERATION_SLOTS; ++index) {
        if (&headless->operation[index] == record) {
            if (headless->active_slot == index)
                headless->active_slot = C35_NO_ACTIVE_SLOT;
            if (headless->service_phase != C35_SERVICE_FAULTED_CLEANUP &&
                headless->service_phase != C35_SERVICE_POISONED &&
                headless->service_phase != C35_SERVICE_TEARING_DOWN)
                headless->service_phase = C35_SERVICE_READY;
            return;
        }
    }
}

static void cause_set(
    struct c35_operation_record *record,
    uint32_t domain,
    uint32_t code
)
{
    record->cause_domain = domain;
    record->cause_code = code;
}

static uint8_t retry_for_result(enum c35_result result)
{
    return result == C35_NO_CAPACITY || result == C35_PROVIDER_FAILURE ||
           result == C35_IN_PROGRESS ? C35_RETRY_SAME_TOKEN :
           result == C35_POISONED || result == C35_INVARIANT ||
           result == C35_CORRUPT || result == C35_COUNTER_EXHAUSTED ?
               C35_RETRY_REPAIR_REQUIRED : C35_RETRY_NONE;
}

static void binding_cause_set(
    struct c35_headless *headless,
    struct c35_operation_record *record,
    enum c35_result fallback
)
{
    struct c35_cause_detail cause;

    c35_cause_clear(&cause);
    if (headless->binding.ops->cause_query(
            headless->binding.context, &cause) == C35_OK &&
        c35_cause_valid(&cause) && cause.domain != C35_CAUSE_NONE) {
        cause_set(record, cause.domain, cause.code);
        record->retry_class = cause.retry_class;
    } else {
        cause_set(record, C35_CAUSE_BINDING, (uint32_t)fallback);
        record->retry_class = retry_for_result(fallback);
    }
}

static void status_fill(
    const struct c35_headless *headless,
    const struct c35_operation_record *record,
    struct c35_operation_status *status
)
{
    memset(status, 0, sizeof(*status));
    status->version = C35_OPERATION_VERSION;
    status->size = sizeof(*status);
    status->token = record->token;
    status->call_state = record->finished ?
        C35_CALL_DONE : C35_CALL_IN_PROGRESS;
    status->operation_kind = record->kind;
    status->commit_state = (uint8_t)record->commit_state;
    status->cleanup_state = (uint8_t)record->cleanup_state;
    status->outcome = record->outcome;
    status->service_phase = headless->service_phase;
    status->internal_phase = record->phase;
    status->units_used = record->units_used;
    status->cause_domain = record->cause_domain;
    status->cause_code = record->cause_code;
    status->retry_class = record->retry_class != C35_RETRY_NONE ?
        record->retry_class : !record->finished ? C35_RETRY_SAME_TOKEN :
        (record->cleanup_state == C35_CLEANUP_POISONED ||
         record->cleanup_state == C35_CLEANUP_PENDING) ?
            C35_RETRY_REPAIR_REQUIRED : C35_RETRY_NONE;
    status->publication_valid =
        record->publication.version == C35_PUBLICATION_VERSION;
    if (status->publication_valid) status->publication = record->publication;
}

enum c35_result c35_headless_init(
    struct c35_headless *headless,
    const struct c35_lifecycle_port *lifecycle,
    const struct c35_binding *binding,
    uint64_t instance_nonce,
    uint32_t owner_epoch,
    uint32_t controller_epoch_limit,
    uint64_t request_uid_limit,
    uint64_t operation_uid_limit,
    uint8_t actor
)
{
    if (headless == NULL || !c35_lifecycle_port_valid(lifecycle) ||
        !c35_binding_valid(binding) || instance_nonce == 0 || owner_epoch == 0 ||
        controller_epoch_limit < owner_epoch || request_uid_limit == 0 ||
        operation_uid_limit == 0) return C35_INVALID;
    memset(headless, 0, sizeof(*headless));
    headless->lifecycle = *lifecycle;
    headless->binding = *binding;
    headless->instance_nonce = instance_nonce;
    headless->next_request = 1;
    headless->next_operation_uid = 1;
    headless->next_control_uid = 1;
    headless->next_teardown_uid = 1;
    headless->request_uid_limit = request_uid_limit;
    headless->operation_uid_limit = operation_uid_limit;
    headless->control_uid_limit = operation_uid_limit;
    headless->teardown_uid_limit = 1;
    headless->owner_epoch = owner_epoch;
    headless->controller_epoch_limit = controller_epoch_limit;
    headless->actor = actor;
    headless->service_phase = C35_SERVICE_READY;
    headless->active_slot = C35_NO_ACTIVE_SLOT;
    return C35_OK;
}

enum c35_result c35_submit_start(
    struct c35_headless *headless,
    const struct c35_request *request,
    struct c35_operation_token *token
)
{
    struct c35_operation_record *record;
    uint64_t identity;

    if (headless == NULL || token == NULL || !request_valid(request))
        return C35_INVALID;
    if (headless->service_phase != C35_SERVICE_READY ||
        headless->active_slot != C35_NO_ACTIVE_SLOT) return C35_WRONG_STATE;
    if (headless->next_request == 0 ||
        headless->next_request > headless->request_uid_limit)
        return C35_COUNTER_EXHAUSTED;
    if (!token_available(headless)) return C35_COUNTER_EXHAUSTED;
    record = operation_allocate(headless, C35_OPERATION_SUBMIT, token);
    if (record == NULL) return C35_NO_CAPACITY;
    identity = headless->next_request++;
    record->request = *request;
    descriptor_make(
        headless, identity, &record->descriptor, &record->request_token);
    record->registration_txid = txid_make(headless, token);
    record->phase = C35_SUBMIT_PREPARE;
    record->commit_state = C35_COMMIT_IN_PROGRESS;
    headless->service_phase = C35_SERVICE_SUBMIT_RECONCILE;
    return C35_OK;
}

enum c35_result c35_completion_start(
    struct c35_headless *headless,
    const struct fwlab_c31_command_handle *command,
    struct c35_operation_token *token
)
{
    struct c35_operation_record *record;

    if (headless == NULL || command == NULL || token == NULL ||
        headless->service_phase != C35_SERVICE_READY ||
        headless->active_slot != C35_NO_ACTIVE_SLOT) return C35_WRONG_STATE;
    if (!token_available(headless)) return C35_COUNTER_EXHAUSTED;
    record = operation_allocate(headless, C35_OPERATION_COMPLETION, token);
    if (record == NULL) return C35_NO_CAPACITY;
    record->command = *command;
    record->command_valid = 1;
    record->result_txid = txid_make(headless, token);
    record->phase = C35_COMPLETE_WAIT_READY;
    record->commit_state = C35_COMMIT_IN_PROGRESS;
    headless->service_phase = C35_SERVICE_COMPLETION_RECONCILE;
    return C35_OK;
}

enum c35_result c35_reset_start(
    struct c35_headless *headless,
    struct c35_operation_token *token
)
{
    enum c35_result result;

    if (headless == NULL || token == NULL || headless->control_active ||
        headless->service_phase == C35_SERVICE_TEARING_DOWN ||
        headless->service_phase == C35_SERVICE_DEAD ||
        headless->service_phase == C35_SERVICE_POISONED) return C35_WRONG_STATE;
    result = control_allocate(headless, C35_OPERATION_RESET, token);
    if (result != C35_OK) return result;
    headless->control.old_epoch = headless->owner_epoch;
    headless->control.new_epoch = headless->owner_epoch + 1u;
    headless->control.registration_txid = txid_make(headless, token);
    if (headless->owner_epoch >= headless->controller_epoch_limit) {
        record_finish(headless, &headless->control, C35_COUNTER_EXHAUSTED,
                      C35_COMMIT_NOT_STARTED, C35_CLEANUP_NONE);
        headless->service_phase = C35_SERVICE_READY;
        return C35_OK;
    }
    if (headless->active_slot != C35_NO_ACTIVE_SLOT)
        headless->operation[headless->active_slot].superseded = 1;
    headless->control.phase = C35_RESET_BEGIN;
    headless->control.commit_state = C35_COMMIT_IN_PROGRESS;
    headless->service_phase = C35_SERVICE_RESETTING;
    return C35_OK;
}

enum c35_result c35_teardown_start(
    struct c35_headless *headless,
    struct c35_operation_token *token
)
{
    struct c35_operation_token prepared;
    enum c35_result result;

    if (headless == NULL || token == NULL ||
        headless->service_phase == C35_SERVICE_DEAD) return C35_WRONG_STATE;
    if (headless->control_active) {
        if (headless->control.kind == C35_OPERATION_TEARDOWN)
            return C35_WRONG_STATE;
        if (headless->previous_control_used) return C35_NO_CAPACITY;
    }
    result = teardown_token_prepare(headless, &prepared);
    if (result != C35_OK) return result;
    if (headless->control_active) {
        headless->previous_control = headless->control;
        headless->previous_control_used = 1;
        if (!headless->previous_control.finished) {
            headless->previous_control.finished = 1;
            headless->previous_control.outcome = C35_OK;
            if (headless->previous_control.commit_state ==
                    C35_COMMIT_COMMITTED ||
                headless->previous_control.publication.version ==
                    C35_PUBLICATION_VERSION) {
                headless->previous_control.commit_state = C35_COMMIT_COMMITTED;
                headless->previous_control.cleanup_state = C35_CLEANUP_PENDING;
            } else {
                headless->previous_control.commit_state =
                    C35_COMMIT_SUPERSEDED;
                headless->previous_control.cleanup_state =
                    C35_CLEANUP_COMPLETE;
            }
            headless->previous_control.phase = C35_INTERNAL_DONE;
        }
        memset(&headless->control, 0, sizeof(headless->control));
        headless->control_active = 0;
    }
    memset(&headless->control, 0, sizeof(headless->control));
    headless->control.used = 1;
    headless->control.kind = C35_OPERATION_TEARDOWN;
    headless->control.token = prepared;
    headless->control.outcome = C35_IN_PROGRESS;
    headless->control.commit_state = C35_COMMIT_NOT_STARTED;
    headless->control.cleanup_state = C35_CLEANUP_NONE;
    headless->control_active = 1;
    ++headless->next_teardown_uid;
    *token = prepared;
    if (headless->active_slot != C35_NO_ACTIVE_SLOT)
        headless->operation[headless->active_slot].superseded = 1;
    headless->control.phase = C35_TEARDOWN_ALIGN;
    headless->control.commit_state = C35_COMMIT_IN_PROGRESS;
    headless->service_phase = C35_SERVICE_TEARING_DOWN;
    return C35_OK;
}

static void fault_if_needed(
    struct c35_headless *headless,
    struct c35_operation_record *record,
    enum fwlab_c31_api_result result
)
{
    cause_set(record, C35_CAUSE_C31, (uint32_t)result);
    record->retry_class = result == FWLAB_C31_API_NO_CAPACITY ?
        C35_RETRY_SAME_TOKEN :
        result == FWLAB_C31_API_COUNTER_EXHAUSTED ||
        result == FWLAB_C31_API_INVARIANT_FAILURE ?
            C35_RETRY_REPAIR_REQUIRED : C35_RETRY_NONE;
    if (result == FWLAB_C31_API_COUNTER_EXHAUSTED ||
        headless->lifecycle.ops->phase(headless->lifecycle.context) ==
            FWLAB_C31_INSTANCE_FAULTED) {
        headless->service_phase = C35_SERVICE_FAULTED_CLEANUP;
    }
}

static void submit_progress(
    struct c35_headless *headless,
    struct c35_operation_record *record
)
{
    enum c35_result result;
    enum c35_registration_state state;
    enum fwlab_c31_api_result lower;

    switch (record->phase) {
    case C35_SUBMIT_PREPARE:
        result = headless->binding.ops->registration_prepare(
            headless->binding.context, &record->registration_txid,
            &record->request_token, headless->owner_epoch, &record->request);
        if (result == C35_OK) record->phase = C35_SUBMIT_C31;
        else {
            binding_cause_set(headless, record, result);
            record->outcome = result;
            record->phase = C35_SUBMIT_QUERY;
        }
        break;
    case C35_SUBMIT_C31:
        lower = headless->lifecycle.ops->submit(
            headless->lifecycle.context, &record->descriptor,
            &record->command);
        if (lower == FWLAB_C31_API_OK) {
            record->command_valid = 1;
            record->phase = C35_SUBMIT_COMMIT;
        } else {
            fault_if_needed(headless, record, lower);
            record->outcome = map_c31(lower);
            cause_set(record, C35_CAUSE_C31, (uint32_t)lower);
            if (command_value_valid(headless, &record->command)) {
                record->command_valid = 1;
                record->phase = C35_SUBMIT_C31_QUERY;
            } else {
                record->phase = C35_SUBMIT_BINDING_ABORT;
            }
        }
        break;
    case C35_SUBMIT_C31_QUERY: {
        enum fwlab_c31_lifecycle_state command_state;

        lower = headless->lifecycle.ops->command_state(
            headless->lifecycle.context, &record->command,
            &command_state);
        if (lower == FWLAB_C31_API_OK &&
            command_state == FWLAB_C31_CMD_ACCEPTED) {
            record->phase = C35_SUBMIT_COMMIT;
        } else if (lower == FWLAB_C31_API_OK) {
            record_finish(headless, record, C35_INVARIANT,
                          C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        } else if (lower == FWLAB_C31_API_STALE_TOKEN ||
                   lower == FWLAB_C31_API_NOT_FOUND) {
            record->command_valid = 0;
            record->phase = C35_SUBMIT_BINDING_ABORT;
        } else {
            fault_if_needed(headless, record, lower);
            cause_set(record, C35_CAUSE_C31, (uint32_t)lower);
        }
        break;
    }
    case C35_SUBMIT_COMMIT:
        result = headless->binding.ops->registration_commit(
            headless->binding.context, &record->registration_txid,
            &record->command);
        if (result != C35_OK) {
            binding_cause_set(headless, record, result);
            record->outcome = result;
        }
        record->phase = C35_SUBMIT_QUERY;
        break;
    case C35_SUBMIT_QUERY:
        result = headless->binding.ops->registration_query(
            headless->binding.context, &record->registration_txid, &state);
        if (result != C35_OK) {
            binding_cause_set(headless, record, result);
            break;
        }
        if (state == C35_REG_COMMITTED) {
            record_finish(headless, record, C35_OK,
                          C35_COMMIT_COMMITTED, C35_CLEANUP_COMPLETE);
        } else if (state == C35_REG_PREPARED || state == C35_REG_PARTIAL) {
            record->phase = C35_SUBMIT_BINDING_ABORT;
        } else if (state == C35_REG_POISONED) {
            record_finish(headless, record, C35_POISONED,
                          C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        } else if (record->command_valid) {
            record->phase = C35_SUBMIT_ABORT_REQUEST;
        } else {
            record->binding_retire = state == C35_REG_ABORTED;
            record_finish(headless, record,
                          record->outcome == C35_IN_PROGRESS ?
                              C35_PROVIDER_FAILURE :
                              (enum c35_result)record->outcome,
                          C35_COMMIT_NOT_STARTED,
                          record->outcome == C35_POISONED ?
                              C35_CLEANUP_POISONED : C35_CLEANUP_COMPLETE);
            if (record->outcome == C35_POISONED)
                headless->service_phase = C35_SERVICE_POISONED;
        }
        break;
    case C35_SUBMIT_BINDING_ABORT:
        result = headless->binding.ops->registration_abort(
            headless->binding.context, &record->registration_txid);
        if (result == C35_OK || result == C35_STALE) {
            record->binding_retire = 1;
            if (record->command_valid)
                record->phase = C35_SUBMIT_ABORT_REQUEST;
            else
                record_finish(headless, record,
                              (enum c35_result)record->outcome,
                              C35_COMMIT_NOT_STARTED,
                              record->outcome == C35_POISONED ?
                                  C35_CLEANUP_POISONED :
                                  C35_CLEANUP_COMPLETE);
            if (!record->command_valid && record->outcome == C35_POISONED)
                headless->service_phase = C35_SERVICE_POISONED;
        } else if (result == C35_POISONED) {
            binding_cause_set(headless, record, result);
            record_finish(headless, record, C35_POISONED,
                          C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        } else binding_cause_set(headless, record, result);
        break;
    case C35_SUBMIT_ABORT_REQUEST:
        lower = headless->lifecycle.ops->abort_request(
            headless->lifecycle.context, &record->command,
            &record->abort_ticket, &record->abort_outcome);
        if (lower == FWLAB_C31_API_OK) {
            record->abort_ticket_valid = 1;
            record->phase = C35_SUBMIT_ABORT_ACQUIRE;
        } else {
            fault_if_needed(headless, record, lower);
            cause_set(record, C35_CAUSE_C31, (uint32_t)lower);
            if (ticket_value_valid(
                    headless, &record->abort_ticket, &record->command)) {
                record->abort_ticket_valid = 1;
                record->phase = C35_SUBMIT_ABORT_REQUEST_QUERY;
            }
            if (headless->service_phase == C35_SERVICE_FAULTED_CLEANUP &&
                !record->abort_ticket_valid)
                record_finish(headless, record, map_c31(lower),
                              C35_COMMIT_UNKNOWN, C35_CLEANUP_PENDING);
        }
        break;
    case C35_SUBMIT_ABORT_REQUEST_QUERY:
        lower = headless->lifecycle.ops->abort_query(
            headless->lifecycle.context, &record->abort_ticket,
            &record->abort_outcome);
        if (lower == FWLAB_C31_API_OK) {
            record->phase = C35_SUBMIT_ABORT_ACQUIRE;
        } else if (lower == FWLAB_C31_API_STALE_TOKEN ||
                   lower == FWLAB_C31_API_NOT_FOUND) {
            record->abort_ticket_valid = 0;
            memset(&record->abort_ticket, 0, sizeof(record->abort_ticket));
            record->phase = C35_SUBMIT_ABORT_REQUEST;
        } else {
            fault_if_needed(headless, record, lower);
            cause_set(record, C35_CAUSE_C31, (uint32_t)lower);
        }
        break;
    case C35_SUBMIT_ABORT_ACQUIRE:
        lower = headless->lifecycle.ops->completion_acquire(
            headless->lifecycle.context, &record->command,
            &record->lease, &record->intent);
        if (lower == FWLAB_C31_API_OK) {
            record->lease_valid = 1;
            record->phase = C35_SUBMIT_ABORT_CONSUME;
        } else {
            fault_if_needed(headless, record, lower);
            cause_set(record, C35_CAUSE_C31, (uint32_t)lower);
            record->phase = C35_SUBMIT_ABORT_ACQUIRE_QUERY;
        }
        break;
    case C35_SUBMIT_ABORT_ACQUIRE_QUERY: {
        enum fwlab_c31_lifecycle_state command_state;

        lower = headless->lifecycle.ops->command_state(
            headless->lifecycle.context, &record->command, &command_state);
        if (lower == FWLAB_C31_API_OK &&
            command_state == FWLAB_C31_CMD_COMPLETION_LEASED &&
            lease_value_valid(headless, &record->lease, &record->command)) {
            record->lease_valid = 1;
            record->phase = C35_SUBMIT_ABORT_CONSUME;
        } else if (lower == FWLAB_C31_API_OK &&
                   command_state == FWLAB_C31_CMD_COMPLETION_READY) {
            memset(&record->lease, 0, sizeof(record->lease));
            record->phase = C35_SUBMIT_ABORT_ACQUIRE;
        } else if (lower != FWLAB_C31_API_OK) {
            fault_if_needed(headless, record, lower);
            cause_set(record, C35_CAUSE_C31, (uint32_t)lower);
        } else {
            record_finish(headless, record, C35_INVARIANT,
                          C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        }
        break;
    }
    case C35_SUBMIT_ABORT_CONSUME:
        if (record->consume_attempted) {
            enum fwlab_c31_lifecycle_state command_state;

            lower = headless->lifecycle.ops->command_state(
                headless->lifecycle.context, &record->command, &command_state);
            if ((lower == FWLAB_C31_API_STALE_TOKEN ||
                 lower == FWLAB_C31_API_NOT_FOUND) &&
                headless->lifecycle.ops->phase(
                    headless->lifecycle.context) ==
                    FWLAB_C31_INSTANCE_READY) {
                record->lease_valid = 0;
                record->phase = C35_SUBMIT_ABORT_ACK;
                break;
            }
            if (lower != FWLAB_C31_API_OK ||
                command_state != FWLAB_C31_CMD_COMPLETION_LEASED) {
                fault_if_needed(headless, record, lower);
                break;
            }
            record->consume_attempted = 0;
        }
        lower = headless->lifecycle.ops->completion_consume(
            headless->lifecycle.context, &record->lease);
        if (lower == FWLAB_C31_API_OK) {
            record->lease_valid = 0;
            record->phase = C35_SUBMIT_ABORT_ACK;
        } else {
            record->consume_attempted = 1;
            fault_if_needed(headless, record, lower);
        }
        break;
    case C35_SUBMIT_ABORT_ACK:
        if (record->ack_attempted) {
            lower = headless->lifecycle.ops->abort_query(
                headless->lifecycle.context, &record->abort_ticket,
                &record->abort_outcome);
            if (lower == FWLAB_C31_API_STALE_TOKEN ||
                lower == FWLAB_C31_API_NOT_FOUND) {
                record->abort_ticket_valid = 0;
                record_finish(headless, record,
                              (enum c35_result)record->outcome,
                              C35_COMMIT_ABORTED,
                              record->outcome == C35_POISONED ?
                                  C35_CLEANUP_POISONED :
                                  C35_CLEANUP_COMPLETE);
                if (record->outcome == C35_POISONED)
                    headless->service_phase = C35_SERVICE_POISONED;
                break;
            }
            if (lower != FWLAB_C31_API_OK) {
                fault_if_needed(headless, record, lower);
                cause_set(record, C35_CAUSE_C31, (uint32_t)lower);
                break;
            }
            record->ack_attempted = 0;
        }
        lower = headless->lifecycle.ops->abort_ack(
            headless->lifecycle.context, &record->abort_ticket);
        if (lower == FWLAB_C31_API_OK) {
            record->abort_ticket_valid = 0;
            record_finish(headless, record,
                          (enum c35_result)record->outcome,
                          C35_COMMIT_ABORTED,
                          record->outcome == C35_POISONED ?
                              C35_CLEANUP_POISONED : C35_CLEANUP_COMPLETE);
            if (record->outcome == C35_POISONED)
                headless->service_phase = C35_SERVICE_POISONED;
        } else {
            record->ack_attempted = 1;
            fault_if_needed(headless, record, lower);
        }
        break;
    default:
        record_finish(headless, record, C35_INVARIANT,
                      C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
        headless->service_phase = C35_SERVICE_POISONED;
        break;
    }
}

static void completion_progress(
    struct c35_headless *headless,
    struct c35_operation_record *record
)
{
    enum fwlab_c31_api_result lower;
    enum fwlab_c31_lifecycle_state state;
    enum c35_result result;
    enum c35_result_state result_state;

    switch (record->phase) {
    case C35_COMPLETE_WAIT_READY:
        lower = headless->lifecycle.ops->command_state(
            headless->lifecycle.context, &record->command, &state);
        if (lower == FWLAB_C31_API_NO_CAPACITY) {
            cause_set(record, C35_CAUSE_C31, (uint32_t)lower);
        } else if (lower != FWLAB_C31_API_OK) {
            fault_if_needed(headless, record, lower);
            record_finish(headless, record, map_c31(lower),
                          C35_COMMIT_NOT_STARTED, C35_CLEANUP_COMPLETE);
        } else if (state == FWLAB_C31_CMD_COMPLETION_READY) {
            record->phase = C35_COMPLETE_ACQUIRE;
        } else {
            struct fwlab_c31_step_result step;

            lower = headless->lifecycle.ops->step(
                headless->lifecycle.context, 1, &step);
            if (lower != FWLAB_C31_API_OK)
                fault_if_needed(headless, record, lower);
        }
        break;
    case C35_COMPLETE_ACQUIRE:
        lower = headless->lifecycle.ops->completion_acquire(
            headless->lifecycle.context, &record->command,
            &record->lease, &record->intent);
        if (lower == FWLAB_C31_API_OK) {
            record->lease_valid = 1;
            record->phase = C35_COMPLETE_COPY;
        } else {
            fault_if_needed(headless, record, lower);
            cause_set(record, C35_CAUSE_C31, (uint32_t)lower);
            record->phase = C35_COMPLETE_ACQUIRE_QUERY;
        }
        break;
    case C35_COMPLETE_ACQUIRE_QUERY:
        lower = headless->lifecycle.ops->command_state(
            headless->lifecycle.context, &record->command, &state);
        if (lower == FWLAB_C31_API_OK &&
            state == FWLAB_C31_CMD_COMPLETION_LEASED &&
            lease_value_valid(headless, &record->lease, &record->command)) {
            record->lease_valid = 1;
            record->phase = C35_COMPLETE_COPY;
        } else if (lower == FWLAB_C31_API_OK &&
                   state == FWLAB_C31_CMD_COMPLETION_READY) {
            memset(&record->lease, 0, sizeof(record->lease));
            record->phase = C35_COMPLETE_ACQUIRE;
        } else if (lower != FWLAB_C31_API_OK) {
            fault_if_needed(headless, record, lower);
            cause_set(record, C35_CAUSE_C31, (uint32_t)lower);
        } else {
            record_finish(headless, record, C35_INVARIANT,
                          C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        }
        break;
    case C35_COMPLETE_COPY:
        result = headless->binding.ops->result_prepare(
            headless->binding.context, &record->result_txid,
            &record->command, &record->intent, &record->semantic);
        if (result == C35_OK) {
            if (semantic_valid(&record->semantic)) {
                record->phase = C35_COMPLETE_CONSUME;
            } else {
                record->outcome = C35_INVARIANT;
                binding_cause_set(headless, record, C35_INVARIANT);
                record->phase = C35_COMPLETE_RELEASE;
            }
        } else {
            record->outcome = result;
            binding_cause_set(
                headless, record, (enum c35_result)record->outcome);
            record->phase = C35_COMPLETE_COPY_QUERY;
        }
        break;
    case C35_COMPLETE_COPY_QUERY:
        result = headless->binding.ops->result_query(
            headless->binding.context, &record->result_txid, &result_state);
        if (result == C35_POISONED) {
            record->outcome = C35_POISONED;
            binding_cause_set(headless, record, result);
            record->phase = C35_COMPLETE_RELEASE;
        } else if (result != C35_OK) {
            binding_cause_set(headless, record, result);
        } else if (result_state == C35_RESULT_PRESENT ||
                   result_state == C35_RESULT_PREPARED) {
            record->phase = C35_COMPLETE_COPY;
        } else if (result_state == C35_RESULT_ABSENT ||
                   result_state == C35_RESULT_ABORTED) {
            record->phase = C35_COMPLETE_RELEASE;
        } else {
            record->outcome = C35_POISONED;
            record->phase = C35_COMPLETE_RELEASE;
        }
        break;
    case C35_COMPLETE_RELEASE:
        lower = headless->lifecycle.ops->completion_release(
            headless->lifecycle.context, &record->lease);
        if (lower == FWLAB_C31_API_OK) {
            record->lease_valid = 0;
            record->phase = C35_COMPLETE_RESULT_ABORT;
        } else {
            fault_if_needed(headless, record, lower);
            cause_set(record, C35_CAUSE_C31, (uint32_t)lower);
            record->phase = C35_COMPLETE_RELEASE_QUERY;
        }
        break;
    case C35_COMPLETE_RELEASE_QUERY:
        lower = headless->lifecycle.ops->command_state(
            headless->lifecycle.context, &record->command, &state);
        if (lower == FWLAB_C31_API_OK &&
            state == FWLAB_C31_CMD_COMPLETION_READY) {
            record->lease_valid = 0;
            record->phase = C35_COMPLETE_RESULT_ABORT;
        } else if (lower == FWLAB_C31_API_OK &&
                   state == FWLAB_C31_CMD_COMPLETION_LEASED) {
            record->phase = C35_COMPLETE_RELEASE;
        } else if (lower != FWLAB_C31_API_OK) {
            fault_if_needed(headless, record, lower);
            cause_set(record, C35_CAUSE_C31, (uint32_t)lower);
        } else {
            record_finish(headless, record, C35_INVARIANT,
                          C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        }
        break;
    case C35_COMPLETE_RESULT_ABORT:
        result = headless->binding.ops->result_abort(
            headless->binding.context, &record->result_txid);
        if (result == C35_OK) {
            record->binding_retire = 1;
            record_finish(headless, record,
                          (enum c35_result)record->outcome,
                          C35_COMMIT_NOT_STARTED,
                          record->outcome == C35_POISONED ?
                              C35_CLEANUP_POISONED : C35_CLEANUP_COMPLETE);
            if (record->outcome == C35_POISONED)
                headless->service_phase = C35_SERVICE_POISONED;
        } else if (result == C35_POISONED) {
            binding_cause_set(headless, record, result);
            record_finish(headless, record, C35_POISONED,
                          C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        } else {
            binding_cause_set(headless, record, result);
            record->phase = C35_COMPLETE_RESULT_ABORT_QUERY;
        }
        break;
    case C35_COMPLETE_RESULT_ABORT_QUERY:
        result = headless->binding.ops->result_query(
            headless->binding.context, &record->result_txid, &result_state);
        if (result == C35_POISONED) {
            binding_cause_set(headless, record, result);
            record_finish(headless, record, C35_POISONED,
                          C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        } else if (result != C35_OK) {
            binding_cause_set(headless, record, result);
        } else if (result_state == C35_RESULT_ABORTED) {
            record->binding_retire = 1;
            record_finish(headless, record,
                          (enum c35_result)record->outcome,
                          C35_COMMIT_NOT_STARTED,
                          record->outcome == C35_POISONED ?
                              C35_CLEANUP_POISONED : C35_CLEANUP_COMPLETE);
            if (record->outcome == C35_POISONED)
                headless->service_phase = C35_SERVICE_POISONED;
        } else if (result_state == C35_RESULT_ABSENT) {
            record_finish(headless, record,
                          (enum c35_result)record->outcome,
                          C35_COMMIT_NOT_STARTED,
                          record->outcome == C35_POISONED ?
                              C35_CLEANUP_POISONED : C35_CLEANUP_COMPLETE);
            if (record->outcome == C35_POISONED)
                headless->service_phase = C35_SERVICE_POISONED;
        } else if (result_state == C35_RESULT_PRESENT ||
                   result_state == C35_RESULT_PREPARED) {
            record->phase = C35_COMPLETE_RESULT_ABORT;
        } else {
            record_finish(headless, record, C35_POISONED,
                          C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        }
        break;
    case C35_COMPLETE_CONSUME:
        if (record->consume_attempted) {
            lower = headless->lifecycle.ops->command_state(
                headless->lifecycle.context, &record->command, &state);
            if ((lower == FWLAB_C31_API_STALE_TOKEN ||
                 lower == FWLAB_C31_API_NOT_FOUND) &&
                headless->lifecycle.ops->phase(
                    headless->lifecycle.context) ==
                    FWLAB_C31_INSTANCE_READY) {
                record->lease_valid = 0;
                record->commit_state = C35_COMMIT_COMMITTED;
                completion_publication(headless, record);
                record->phase = C35_COMPLETE_ACK;
                break;
            }
            if (lower != FWLAB_C31_API_OK ||
                state != FWLAB_C31_CMD_COMPLETION_LEASED) {
                record_finish(headless, record, C35_INVARIANT,
                              C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
                headless->service_phase = C35_SERVICE_POISONED;
                break;
            }
            record->consume_attempted = 0;
        }
        lower = headless->lifecycle.ops->completion_consume(
            headless->lifecycle.context, &record->lease);
        if (lower == FWLAB_C31_API_OK) {
            record->lease_valid = 0;
            record->commit_state = C35_COMMIT_COMMITTED;
            completion_publication(headless, record);
            record->phase = C35_COMPLETE_ACK;
        } else {
            record->consume_attempted = 1;
            fault_if_needed(headless, record, lower);
        }
        break;
    case C35_COMPLETE_ACK:
        result = headless->binding.ops->result_ack(
            headless->binding.context, &record->result_txid,
            &record->command);
        if (result == C35_OK) {
            record->binding_retire = 1;
            record_finish(headless, record, C35_OK,
                          C35_COMMIT_COMMITTED, C35_CLEANUP_COMPLETE);
        } else if (result == C35_POISONED) {
            binding_cause_set(headless, record, result);
            record_finish(headless, record, C35_POISONED,
                          C35_COMMIT_COMMITTED, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        } else {
            binding_cause_set(headless, record, result);
            record->phase = C35_COMPLETE_ACK_QUERY;
        }
        break;
    case C35_COMPLETE_ACK_QUERY:
        result = headless->binding.ops->result_query(
            headless->binding.context, &record->result_txid, &result_state);
        if (result == C35_POISONED) {
            binding_cause_set(headless, record, result);
            record_finish(headless, record, C35_POISONED,
                          C35_COMMIT_COMMITTED, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        } else if (result != C35_OK) {
            binding_cause_set(headless, record, result);
        } else if (result_state == C35_RESULT_ACKED ||
                   result_state == C35_RESULT_CLEARED_BY_RESET) {
            record->binding_retire = 1;
            record_finish(headless, record, C35_OK,
                          C35_COMMIT_COMMITTED, C35_CLEANUP_COMPLETE);
        } else if (result_state == C35_RESULT_PRESENT) {
            record->phase = C35_COMPLETE_ACK;
        } else {
            record_finish(headless, record, C35_POISONED,
                          C35_COMMIT_COMMITTED, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        }
        break;
    default:
        record_finish(headless, record, C35_INVARIANT,
                      C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
        headless->service_phase = C35_SERVICE_POISONED;
        break;
    }
}

static int reconcile_abort_ticket(
    struct c35_headless *headless,
    struct c35_operation_record *record
)
{
    enum fwlab_c31_api_result lower;

    if (!record->abort_ticket_valid) return 0;
    lower = headless->lifecycle.ops->abort_query(
        headless->lifecycle.context, &record->abort_ticket,
        &record->abort_outcome);
    if (lower == FWLAB_C31_API_STALE_TOKEN ||
        lower == FWLAB_C31_API_NOT_FOUND) {
        record->abort_ticket_valid = 0;
        return 1;
    }
    if (lower != FWLAB_C31_API_OK ||
        record->abort_outcome == FWLAB_C31_ABORT_PENDING) return 1;
    lower = headless->lifecycle.ops->abort_ack(
        headless->lifecycle.context, &record->abort_ticket);
    if (lower == FWLAB_C31_API_OK) record->abort_ticket_valid = 0;
    return 1;
}

static int reconcile_any_ticket(struct c35_headless *headless)
{
    unsigned int index;

    for (index = 0; index < C35_OPERATION_SLOTS; ++index) {
        if (headless->operation[index].used &&
            headless->operation[index].abort_ticket_valid)
            return reconcile_abort_ticket(headless,
                                          &headless->operation[index]);
    }
    return 0;
}

static void supersede_operations(struct c35_headless *headless, int reset)
{
    unsigned int index;

    for (index = 0; index < C35_OPERATION_SLOTS; ++index) {
        struct c35_operation_record *record = &headless->operation[index];

        if (!record->used || record->finished) continue;
        if (record->commit_state == C35_COMMIT_COMMITTED ||
            record->publication.version == C35_PUBLICATION_VERSION) {
            record_finish(headless, record, C35_OK, C35_COMMIT_COMMITTED,
                          C35_CLEANUP_COMPLETE);
        } else {
            record_finish(headless, record, C35_OK, C35_COMMIT_SUPERSEDED,
                          C35_CLEANUP_COMPLETE);
        }
        if (reset) record->binding_retire = 1;
    }
    headless->active_slot = C35_NO_ACTIVE_SLOT;
}

static void reset_progress(
    struct c35_headless *headless,
    struct c35_operation_record *record
)
{
    enum fwlab_c31_instance_phase phase;
    enum fwlab_c31_api_result lower;
    enum c35_result result;
    enum c35_reset_state reset_state;
    bool quiescent;

    phase = headless->lifecycle.ops->phase(headless->lifecycle.context);
    switch (record->phase) {
    case C35_RESET_BEGIN:
        if (phase == FWLAB_C31_INSTANCE_RESET_DRAIN ||
            phase == FWLAB_C31_INSTANCE_RESET_ACK) {
            record->phase = C35_RESET_DRAIN;
            break;
        }
        lower = headless->lifecycle.ops->reset_begin(
            headless->lifecycle.context);
        if (lower == FWLAB_C31_API_OK) record->phase = C35_RESET_DRAIN;
        else {
            fault_if_needed(headless, record, lower);
            if (headless->service_phase == C35_SERVICE_FAULTED_CLEANUP)
                record_finish(headless, record, map_c31(lower),
                              C35_COMMIT_UNKNOWN, C35_CLEANUP_PENDING);
        }
        break;
    case C35_RESET_DRAIN:
        if (reconcile_any_ticket(headless)) break;
        if (phase == FWLAB_C31_INSTANCE_RESET_ACK) {
            record->phase = C35_RESET_ACK;
        } else {
            struct fwlab_c31_step_result step;

            lower = headless->lifecycle.ops->step(
                headless->lifecycle.context, 1, &step);
            if (lower != FWLAB_C31_API_OK)
                fault_if_needed(headless, record, lower);
        }
        break;
    case C35_RESET_ACK:
        if (phase == FWLAB_C31_INSTANCE_READY) {
            record->commit_state = C35_COMMIT_COMMITTED;
            publication_base(
                headless, record, C35_PUBLICATION_RESET, record->new_epoch);
            record->phase = C35_RESET_RECOVER;
            break;
        }
        lower = headless->lifecycle.ops->reset_ack(
            headless->lifecycle.context);
        if (lower == FWLAB_C31_API_OK) {
            record->commit_state = C35_COMMIT_COMMITTED;
            publication_base(
                headless, record, C35_PUBLICATION_RESET, record->new_epoch);
            record->phase = C35_RESET_RECOVER;
        } else fault_if_needed(headless, record, lower);
        break;
    case C35_RESET_RECOVER:
        result = headless->binding.ops->reset_recover(
            headless->binding.context, &record->registration_txid,
            record->old_epoch, record->new_epoch);
        if (result == C35_OK) record->phase = C35_RESET_QUIESCENT;
        else {
            binding_cause_set(headless, record, result);
            record->phase = C35_RESET_RECOVER_QUERY;
        }
        break;
    case C35_RESET_RECOVER_QUERY:
        result = headless->binding.ops->reset_query(
            headless->binding.context, &record->registration_txid,
            &reset_state);
        if (result == C35_OK && reset_state == C35_RESET_RECOVERED)
            record->phase = C35_RESET_QUIESCENT;
        else if (result == C35_OK && reset_state == C35_RESET_ABSENT)
            record->phase = C35_RESET_RECOVER;
        else if ((result == C35_OK && reset_state == C35_RESET_POISONED) ||
                 result == C35_POISONED) {
            record->binding_retire = 1;
            record_finish(headless, record,
                          result == C35_POISONED ? C35_POISONED :
                                                   C35_PROVIDER_FAILURE,
                          C35_COMMIT_COMMITTED, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_FAULTED_CLEANUP;
        } else binding_cause_set(headless, record, result);
        break;
    case C35_RESET_QUIESCENT:
        result = headless->binding.ops->quiescent(
            headless->binding.context, &quiescent);
        if (result == C35_POISONED) {
            binding_cause_set(headless, record, result);
            record_finish(headless, record, C35_POISONED,
                          C35_COMMIT_COMMITTED, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_FAULTED_CLEANUP;
        } else if (result != C35_OK)
            binding_cause_set(headless, record, result);
        else if (quiescent) {
            headless->owner_epoch = record->new_epoch;
            supersede_operations(headless, 1);
            record->binding_retire = 1;
            record_finish(headless, record, C35_OK,
                          C35_COMMIT_COMMITTED, C35_CLEANUP_COMPLETE);
            headless->service_phase = C35_SERVICE_READY;
        }
        break;
    default:
        record_finish(headless, record, C35_INVARIANT,
                      C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
        headless->service_phase = C35_SERVICE_POISONED;
        break;
    }
}

static void teardown_progress(
    struct c35_headless *headless,
    struct c35_operation_record *record
)
{
    enum fwlab_c31_instance_phase phase =
        headless->lifecycle.ops->phase(headless->lifecycle.context);
    enum fwlab_c31_api_result lower;
    enum c35_result result;
    bool quiescent;

    switch (record->phase) {
    case C35_TEARDOWN_ALIGN:
        if (phase == FWLAB_C31_INSTANCE_RESET_DRAIN) {
            if (!reconcile_any_ticket(headless)) {
                struct fwlab_c31_step_result step;
                lower = headless->lifecycle.ops->step(
                    headless->lifecycle.context, 1, &step);
                if (lower != FWLAB_C31_API_OK)
                    fault_if_needed(headless, record, lower);
            }
        } else if (phase == FWLAB_C31_INSTANCE_RESET_ACK) {
            lower = headless->lifecycle.ops->reset_ack(
                headless->lifecycle.context);
            if (lower != FWLAB_C31_API_OK)
                fault_if_needed(headless, record, lower);
        } else if (phase == FWLAB_C31_INSTANCE_TEARDOWN_DRAIN) {
            record->phase = C35_TEARDOWN_DRAIN;
        } else if (phase == FWLAB_C31_INSTANCE_TEARDOWN_ACK) {
            record->phase = C35_TEARDOWN_ACK;
        } else if (phase == FWLAB_C31_INSTANCE_DEAD) {
            record->commit_state = C35_COMMIT_COMMITTED;
            publication_base(headless, record, C35_PUBLICATION_TEARDOWN,
                             headless->owner_epoch);
            record->phase = C35_TEARDOWN_BINDING;
        } else {
            record->phase = C35_TEARDOWN_BEGIN;
        }
        break;
    case C35_TEARDOWN_BEGIN:
        lower = headless->lifecycle.ops->teardown_begin(
            headless->lifecycle.context);
        phase = headless->lifecycle.ops->phase(headless->lifecycle.context);
        if (lower == FWLAB_C31_API_OK ||
            phase == FWLAB_C31_INSTANCE_TEARDOWN_DRAIN ||
            phase == FWLAB_C31_INSTANCE_TEARDOWN_ACK)
            record->phase = C35_TEARDOWN_DRAIN;
        else {
            fault_if_needed(headless, record, lower);
            if (headless->lifecycle.ops->phase(
                    headless->lifecycle.context) ==
                FWLAB_C31_INSTANCE_FAULTED) {
                result = headless->binding.ops->quiescent(
                    headless->binding.context, &quiescent);
                if (result == C35_POISONED) {
                    binding_cause_set(headless, record, result);
                    record_finish(headless, record, C35_POISONED,
                                  C35_COMMIT_UNKNOWN,
                                  C35_CLEANUP_POISONED);
                    headless->service_phase = C35_SERVICE_POISONED;
                }
            }
        }
        break;
    case C35_TEARDOWN_DRAIN:
        if (phase == FWLAB_C31_INSTANCE_FAULTED) {
            result = headless->binding.ops->quiescent(
                headless->binding.context, &quiescent);
            if (result == C35_POISONED) {
                binding_cause_set(headless, record, result);
                record_finish(headless, record, C35_POISONED,
                              C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
                headless->service_phase = C35_SERVICE_POISONED;
            } else {
                record->phase = C35_TEARDOWN_BEGIN;
            }
            break;
        }
        if (reconcile_any_ticket(headless)) break;
        if (phase == FWLAB_C31_INSTANCE_TEARDOWN_ACK) {
            record->phase = C35_TEARDOWN_ACK;
        } else {
            struct fwlab_c31_step_result step;
            lower = headless->lifecycle.ops->step(
                headless->lifecycle.context, 1, &step);
            if (lower != FWLAB_C31_API_OK)
                fault_if_needed(headless, record, lower);
        }
        break;
    case C35_TEARDOWN_ACK:
        if (phase == FWLAB_C31_INSTANCE_DEAD) {
            record->commit_state = C35_COMMIT_COMMITTED;
            publication_base(headless, record, C35_PUBLICATION_TEARDOWN,
                             headless->owner_epoch);
            record->phase = C35_TEARDOWN_BINDING;
            break;
        }
        lower = headless->lifecycle.ops->teardown_ack(
            headless->lifecycle.context);
        if (lower == FWLAB_C31_API_OK) {
            record->commit_state = C35_COMMIT_COMMITTED;
            publication_base(headless, record, C35_PUBLICATION_TEARDOWN,
                             headless->owner_epoch);
            record->phase = C35_TEARDOWN_BINDING;
        } else fault_if_needed(headless, record, lower);
        break;
    case C35_TEARDOWN_BINDING:
        result = headless->binding.ops->teardown_finalize(
            headless->binding.context);
        if (result == C35_OK) record->phase = C35_TEARDOWN_QUIESCENT;
        else if (result == C35_POISONED) {
            binding_cause_set(headless, record, result);
            record_finish(headless, record, C35_POISONED,
                          C35_COMMIT_COMMITTED, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        } else binding_cause_set(headless, record, result);
        break;
    case C35_TEARDOWN_QUIESCENT:
        result = headless->binding.ops->quiescent(
            headless->binding.context, &quiescent);
        if (result == C35_POISONED) {
            binding_cause_set(headless, record, result);
            record_finish(headless, record, C35_POISONED,
                          C35_COMMIT_COMMITTED, C35_CLEANUP_POISONED);
            headless->service_phase = C35_SERVICE_POISONED;
        } else if (result != C35_OK)
            binding_cause_set(headless, record, result);
        else if (quiescent) {
            supersede_operations(headless, 0);
            record_finish(headless, record, C35_OK,
                          C35_COMMIT_COMMITTED, C35_CLEANUP_COMPLETE);
            headless->service_phase = C35_SERVICE_DEAD;
        }
        break;
    default:
        record_finish(headless, record, C35_INVARIANT,
                      C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
        headless->service_phase = C35_SERVICE_POISONED;
        break;
    }
}

static void progress_one(
    struct c35_headless *headless,
    struct c35_operation_record *record
)
{
    if (record->kind == C35_OPERATION_SUBMIT) submit_progress(headless, record);
    else if (record->kind == C35_OPERATION_COMPLETION)
        completion_progress(headless, record);
    else if (record->kind == C35_OPERATION_RESET)
        reset_progress(headless, record);
    else if (record->kind == C35_OPERATION_TEARDOWN)
        teardown_progress(headless, record);
    else {
        record_finish(headless, record, C35_INVALID,
                      C35_COMMIT_UNKNOWN, C35_CLEANUP_POISONED);
        headless->service_phase = C35_SERVICE_POISONED;
    }
}

enum c35_result c35_operation_progress(
    struct c35_headless *headless,
    const struct c35_operation_token *token,
    uint32_t budget,
    struct c35_operation_status *status
)
{
    struct c35_operation_record *record;
    uint32_t used = 0;

    if (headless == NULL || token == NULL || status == NULL) return C35_INVALID;
    record = record_find(headless, token);
    if (record == NULL) return C35_STALE;
    if (!record->finished &&
        (record->kind == C35_OPERATION_SUBMIT ||
         record->kind == C35_OPERATION_COMPLETION) &&
        (headless->service_phase == C35_SERVICE_RESETTING ||
         headless->service_phase == C35_SERVICE_TEARING_DOWN ||
         headless->service_phase == C35_SERVICE_POISONED)) {
        status_fill(headless, record, status);
        status->units_used = 0;
        return C35_IN_PROGRESS;
    }
    while (!record->finished && used < budget) {
        progress_one(headless, record);
        ++used;
        ++record->units_used;
    }
    status_fill(headless, record, status);
    status->units_used = used;
    return record->finished ? C35_OK : C35_IN_PROGRESS;
}

enum c35_result c35_operation_query(
    const struct c35_headless *headless,
    const struct c35_operation_token *token,
    struct c35_operation_status *status
)
{
    const struct c35_operation_record *record;

    if (headless == NULL || token == NULL || status == NULL) return C35_INVALID;
    record = record_find_const(headless, token);
    if (record == NULL) return C35_STALE;
    status_fill(headless, record, status);
    status->units_used = 0;
    return record->finished ? C35_OK : C35_IN_PROGRESS;
}

enum c35_result c35_operation_finalize(
    const struct c35_headless *headless,
    const struct c35_operation_token *token,
    struct c35_operation_status *status
)
{
    enum c35_result result = c35_operation_query(headless, token, status);

    return result == C35_OK ? C35_OK :
           result == C35_IN_PROGRESS ? C35_IN_PROGRESS : result;
}

enum c35_result c35_operation_retire(
    struct c35_headless *headless,
    const struct c35_operation_token *token
)
{
    struct c35_operation_record *record;
    enum c35_result result = C35_OK;
    unsigned int index;

    if (headless == NULL || token == NULL) return C35_INVALID;
    record = record_find(headless, token);
    if (record == NULL || !record->finished) return C35_STALE;
    if (record->binding_retire) {
        const struct c35_txid *txid = record->kind == C35_OPERATION_COMPLETION ?
            &record->result_txid : &record->registration_txid;
        result = headless->binding.ops->transaction_retire(
            headless->binding.context, txid);
        if (result != C35_OK && result != C35_STALE) {
            if (!record->retire_pending) {
                record->retire_saved_cleanup =
                    (uint8_t)record->cleanup_state;
                record->retire_saved_cause_domain =
                    (uint8_t)record->cause_domain;
                record->retire_saved_retry = record->retry_class !=
                    C35_RETRY_NONE ? record->retry_class :
                    (record->cleanup_state == C35_CLEANUP_PENDING ||
                     record->cleanup_state == C35_CLEANUP_POISONED) ?
                        C35_RETRY_REPAIR_REQUIRED : C35_RETRY_NONE;
                record->retire_saved_cause_code = record->cause_code;
            }
            record->retire_pending = 1;
            record->cleanup_state = C35_CLEANUP_PENDING;
            record->cause_domain = C35_CAUSE_BINDING;
            record->cause_code = (uint32_t)result;
            record->retry_class = C35_RETRY_SAME_TOKEN;
            return result;
        }
        if (record->retire_pending) {
            record->cleanup_state = record->retire_saved_cleanup;
            record->cause_domain = record->retire_saved_cause_domain;
            record->cause_code = record->retire_saved_cause_code;
            record->retry_class = record->retire_saved_retry;
            record->retire_pending = 0;
        }
    }
    if (record == &headless->control) {
        memset(record, 0, sizeof(*record));
        headless->control_active = 0;
        return C35_OK;
    }
    if (record == &headless->previous_control) {
        memset(record, 0, sizeof(*record));
        headless->previous_control_used = 0;
        return C35_OK;
    }
    for (index = 0; index < C35_OPERATION_SLOTS; ++index) {
        if (record == &headless->operation[index]) {
            memset(record, 0, sizeof(*record));
            return C35_OK;
        }
    }
    return C35_STALE;
}

static enum c35_result drive_to_done(
    struct c35_headless *headless,
    const struct c35_operation_token *token,
    uint32_t limit,
    struct c35_operation_status *status
)
{
    uint32_t used;
    enum c35_result result;

    for (used = 0; used < limit; ++used) {
        result = c35_operation_progress(headless, token, 1, status);
        if (result == C35_OK) return (enum c35_result)status->outcome;
        if (result != C35_IN_PROGRESS) return result;
    }
    result = c35_operation_query(headless, token, status);
    return result == C35_OK ? (enum c35_result)status->outcome : result;
}

static void compat_clear(
    struct c35_headless *headless,
    const struct c35_operation_token *token
)
{
    if (headless->compat_active &&
        token_equal(&headless->compat_token, token)) {
        memset(&headless->compat_token, 0, sizeof(headless->compat_token));
        headless->compat_active = 0;
    }
}

static enum c35_result compat_operation(
    struct c35_headless *headless,
    uint8_t kind,
    const struct c35_request *request,
    const struct fwlab_c31_command_handle *command,
    struct c35_operation_token *token
)
{
    struct c35_operation_record *record;
    enum c35_result result;

    if (headless == NULL || token == NULL ||
        kind < C35_OPERATION_SUBMIT || kind > C35_OPERATION_TEARDOWN)
        return C35_INVALID;
    if (headless->compat_active) {
        if (headless->compat_token.kind != kind) return C35_WRONG_STATE;
        record = record_find(headless, &headless->compat_token);
        if (record == NULL) return C35_INVARIANT;
        if (kind == C35_OPERATION_SUBMIT &&
            !request_equal(request, &record->request)) return C35_INVALID;
        if (kind == C35_OPERATION_COMPLETION &&
            !command_equal(command, &record->command)) return C35_INVALID;
        *token = headless->compat_token;
        return C35_OK;
    }
    if (kind == C35_OPERATION_SUBMIT)
        result = c35_submit_start(headless, request, token);
    else if (kind == C35_OPERATION_COMPLETION)
        result = c35_completion_start(headless, command, token);
    else if (kind == C35_OPERATION_RESET)
        result = c35_reset_start(headless, token);
    else
        result = c35_teardown_start(headless, token);
    if (result == C35_OK) {
        headless->compat_token = *token;
        headless->compat_active = 1;
    }
    return result;
}

static enum c35_result compat_retire_finished(
    struct c35_headless *headless,
    const struct c35_operation_token *token,
    enum c35_result operation,
    struct c35_operation_status *status
)
{
    struct c35_operation_record *record;
    enum c35_result result;
    uint8_t saved_cleanup = 0;
    uint8_t saved_cause_domain = 0;
    uint8_t saved_retry = 0;
    uint32_t saved_cause_code = 0;
    int retrying_retire;

    if (status->call_state != C35_CALL_DONE) return operation;
    record = record_find(headless, token);
    if (record == NULL) return C35_INVARIANT;
    retrying_retire = record->retire_pending;
    if (retrying_retire) {
        saved_cleanup = record->retire_saved_cleanup;
        saved_cause_domain = record->retire_saved_cause_domain;
        saved_retry = record->retire_saved_retry;
        saved_cause_code = record->retire_saved_cause_code;
    }
    result = c35_operation_retire(headless, token);
    if (result != C35_OK) {
        enum c35_result query = c35_operation_query(headless, token, status);

        return query == C35_OK ? result : query;
    }
    if (retrying_retire) {
        status->cleanup_state = saved_cleanup;
        status->cause_domain = saved_cause_domain;
        status->cause_code = saved_cause_code;
        status->retry_class = saved_retry;
    }
    if (token->kind == C35_OPERATION_TEARDOWN) {
        headless->compat_tombstone_token = *token;
        headless->compat_tombstone_status = *status;
        headless->compat_tombstone_valid = 1;
    }
    compat_clear(headless, token);
    return operation;
}

enum c35_result c35_headless_compat_query(
    const struct c35_headless *headless,
    struct c35_operation_token *token,
    struct c35_operation_status *status
)
{
    if (headless == NULL || token == NULL || status == NULL)
        return C35_INVALID;
    if (headless->compat_active) {
        *token = headless->compat_token;
        return c35_operation_query(headless, token, status);
    }
    if (headless->compat_tombstone_valid) {
        *token = headless->compat_tombstone_token;
        *status = headless->compat_tombstone_status;
        status->units_used = 0;
        return C35_OK;
    }
    return C35_NOT_FOUND;
}

enum c35_result c35_headless_compat_transfer(
    struct c35_headless *headless,
    const struct c35_operation_token *token
)
{
    if (headless == NULL || token == NULL) return C35_INVALID;
    if (headless->compat_active) {
        if (!token_equal(&headless->compat_token, token)) return C35_STALE;
        compat_clear(headless, token);
        return C35_OK;
    }
    if (headless->compat_tombstone_valid) {
        if (!token_equal(&headless->compat_tombstone_token, token))
            return C35_STALE;
        memset(&headless->compat_tombstone_token, 0,
               sizeof(headless->compat_tombstone_token));
        memset(&headless->compat_tombstone_status, 0,
               sizeof(headless->compat_tombstone_status));
        headless->compat_tombstone_valid = 0;
        return C35_OK;
    }
    return C35_NOT_FOUND;
}

enum c35_result c35_headless_submit_status(
    struct c35_headless *headless,
    const struct c35_request *request,
    uint32_t budget,
    struct c35_submission *submission,
    struct c35_operation_status *status
)
{
    struct c35_operation_token token;
    struct c35_operation_record *record;
    enum c35_result result;

    if (submission == NULL || status == NULL)
        return C35_INVALID;
    memset(submission, 0, sizeof(*submission));
    memset(status, 0, sizeof(*status));
    result = compat_operation(
        headless, C35_OPERATION_SUBMIT, request, NULL, &token);
    if (result != C35_OK) return result;
    result = drive_to_done(headless, &token, budget, status);
    record = record_find(headless, &token);
    if (record != NULL && status->call_state == C35_CALL_DONE &&
        status->outcome == C35_OK && record->command_valid) {
        submission->request = record->request_token;
        submission->command = record->command;
        submission->owner_epoch = record->registration_txid.owner_epoch;
    }
    return compat_retire_finished(headless, &token, result, status);
}

enum c35_result c35_headless_submit_observed(
    struct c35_headless *headless,
    const struct c35_request *request,
    struct c35_submission *submission
)
{
    struct c35_operation_status status;

    return c35_headless_submit_status(
        headless, request, 128, submission, &status);
}

enum c35_result c35_headless_submit(
    struct c35_headless *headless,
    const struct c35_request *request,
    struct fwlab_c31_command_handle *command
)
{
    struct c35_submission submission;
    enum c35_result result;

    if (command == NULL) return C35_INVALID;
    result = c35_headless_submit_observed(headless, request, &submission);
    if (result == C35_OK) *command = submission.command;
    return result;
}

enum c35_result c35_headless_complete_status(
    struct c35_headless *headless,
    const struct fwlab_c31_command_handle *command,
    uint32_t budget,
    struct c35_semantic_result *semantic,
    struct fwlab_c31_completion_intent *intent,
    struct c35_publication *publication,
    struct c35_operation_status *status
)
{
    struct c35_operation_token token;
    struct c35_operation_record *record;
    enum c35_result result;

    if (semantic == NULL || intent == NULL || publication == NULL ||
        status == NULL) return C35_INVALID;
    memset(semantic, 0, sizeof(*semantic));
    memset(intent, 0, sizeof(*intent));
    memset(publication, 0, sizeof(*publication));
    memset(status, 0, sizeof(*status));
    result = compat_operation(
        headless, C35_OPERATION_COMPLETION, NULL, command, &token);
    if (result != C35_OK) return result;
    result = drive_to_done(headless, &token, budget, status);
    record = record_find(headless, &token);
    if (record != NULL && record->commit_state == C35_COMMIT_COMMITTED) {
        *semantic = record->semantic;
        *intent = record->intent;
        *publication = record->publication;
    }
    return compat_retire_finished(headless, &token, result, status);
}

enum c35_result c35_headless_complete_observed(
    struct c35_headless *headless,
    const struct fwlab_c31_command_handle *command,
    struct c35_semantic_result *semantic,
    struct fwlab_c31_completion_intent *intent,
    struct c35_publication *publication
)
{
    struct c35_operation_status status;

    return c35_headless_complete_status(
        headless, command, 8192, semantic, intent, publication, &status);
}

enum c35_result c35_headless_complete(
    struct c35_headless *headless,
    const struct fwlab_c31_command_handle *command,
    struct c35_semantic_result *semantic,
    struct fwlab_c31_completion_intent *intent
)
{
    struct c35_publication publication;

    return c35_headless_complete_observed(
        headless, command, semantic, intent, &publication);
}

enum c35_result c35_headless_pump_quiescent(
    struct c35_headless *headless,
    uint32_t limit
)
{
    uint32_t iteration;

    if (headless == NULL || headless->service_phase != C35_SERVICE_READY)
        return C35_WRONG_STATE;
    for (iteration = 0; iteration < limit; ++iteration) {
        bool quiescent;
        struct fwlab_c31_step_result step;
        enum c35_result result = headless->binding.ops->quiescent(
            headless->binding.context, &quiescent);
        if (result != C35_OK) return result;
        if (quiescent) return C35_OK;
        if (headless->lifecycle.ops->step(
                headless->lifecycle.context, 1, &step) != FWLAB_C31_API_OK)
            return C35_PROVIDER_FAILURE;
    }
    return C35_IN_PROGRESS;
}

static enum c35_result control_status(
    struct c35_headless *headless,
    uint8_t kind,
    uint32_t budget,
    struct c35_publication *publication,
    struct c35_operation_status *status
)
{
    struct c35_operation_token token;
    enum c35_result result;

    if (headless == NULL || status == NULL) return C35_INVALID;
    memset(status, 0, sizeof(*status));
    if (publication != NULL) memset(publication, 0, sizeof(*publication));
    if (kind == C35_OPERATION_TEARDOWN && !headless->compat_active &&
        headless->compat_tombstone_valid) {
        *status = headless->compat_tombstone_status;
        status->units_used = 0;
        if (status->publication_valid && publication != NULL)
            *publication = status->publication;
        return (enum c35_result)status->outcome;
    }
    result = compat_operation(headless, kind, NULL, NULL, &token);
    if (result != C35_OK) return result;
    result = drive_to_done(headless, &token, budget, status);
    if (status->publication_valid && publication != NULL)
        *publication = status->publication;
    return compat_retire_finished(headless, &token, result, status);
}

enum c35_result c35_headless_reset_status(
    struct c35_headless *headless,
    uint32_t budget,
    struct c35_publication *publication,
    struct c35_operation_status *status
)
{
    return control_status(
        headless, C35_OPERATION_RESET, budget, publication, status);
}

enum c35_result c35_headless_teardown_status(
    struct c35_headless *headless,
    uint32_t budget,
    struct c35_publication *publication,
    struct c35_operation_status *status
)
{
    return control_status(
        headless, C35_OPERATION_TEARDOWN, budget, publication, status);
}

enum c35_result c35_headless_reset_observed(
    struct c35_headless *headless,
    uint32_t limit,
    struct c35_publication *publication
)
{
    struct c35_operation_status status;

    return c35_headless_reset_status(headless, limit, publication, &status);
}

enum c35_result c35_headless_reset(
    struct c35_headless *headless,
    uint32_t limit
)
{
    struct c35_publication publication;
    return c35_headless_reset_observed(headless, limit, &publication);
}

enum c35_result c35_headless_teardown_observed(
    struct c35_headless *headless,
    uint32_t limit,
    struct c35_publication *publication
)
{
    struct c35_operation_status status;

    return c35_headless_teardown_status(
        headless, limit, publication, &status);
}

enum c35_result c35_headless_teardown(
    struct c35_headless *headless,
    uint32_t limit
)
{
    struct c35_publication publication;
    return c35_headless_teardown_observed(headless, limit, &publication);
}
