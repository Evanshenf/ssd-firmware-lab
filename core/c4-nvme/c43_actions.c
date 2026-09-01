/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_internal.h"

static int boolean_valid(uint8_t value)
{
    return value <= 1;
}

static int pair_nonzero(const uint64_t word[2])
{
    return word[0] != 0 || word[1] != 0;
}

static int pair_equal(const uint64_t left[2], const uint64_t right[2])
{
    return left[0] == right[0] && left[1] == right[1];
}

static int terminal_matches_envelope(
    const struct fwlab_hif_action_terminal *terminal,
    const struct fwlab_hif_action_envelope *envelope)
{
    return c43_action_token_equal(&terminal->token, &envelope->token) &&
           terminal->cookie == envelope->cookie;
}

static int effect_units_valid(uint8_t effect_class, uint32_t units)
{
    if (effect_class == FWLAB_NVME_EFFECT_NONE) {
        return units == 0;
    }
    if (effect_class == FWLAB_NVME_EFFECT_UNKNOWN_PREFIX) {
        return 1;
    }
    return units != 0;
}

static int queue_operation_role_valid(uint32_t operation, uint32_t role)
{
    if (operation == FWLAB_C43_QUEUE_CREATE_CQ ||
        operation == FWLAB_C43_QUEUE_DELETE_CQ) {
        return role == FWLAB_C43_QUEUE_ROLE_IO_CQ;
    }
    if (operation == FWLAB_C43_QUEUE_CREATE_SQ ||
        operation == FWLAB_C43_QUEUE_DELETE_SQ) {
        return role == FWLAB_C43_QUEUE_ROLE_IO_SQ;
    }
    return 0;
}

int fwlab_c43_queue_facts_valid(const struct fwlab_c43_queue_facts *facts)
{
    if (facts == NULL ||
        facts->version != FWLAB_C43_QUEUE_EFFECT_PORT_VERSION ||
        facts->size != sizeof(*facts) || facts->reserved0 != 0 ||
        !pair_nonzero(facts->transaction.word) ||
        facts->operation < FWLAB_C43_QUEUE_CREATE_CQ ||
        facts->operation > FWLAB_C43_QUEUE_DELETE_SQ ||
        facts->role < FWLAB_C43_QUEUE_ROLE_IO_CQ ||
        facts->role > FWLAB_C43_QUEUE_ROLE_IO_SQ ||
        !queue_operation_role_valid(facts->operation, facts->role) ||
        !boolean_valid(facts->address_present) ||
        !boolean_valid(facts->queue_exists) ||
        !boolean_valid(facts->associated_cq_exists) ||
        !boolean_valid(facts->association_matches) ||
        !boolean_valid(facts->active_commands_zero) ||
        !boolean_valid(facts->target_refs_zero) ||
        !boolean_valid(facts->reserved_publications_zero) ||
        !boolean_valid(facts->unacked_completions_zero) ||
        !boolean_valid(facts->current_relation) ||
        !c43_bytes_zero(facts->reserved1, sizeof(facts->reserved1)) ||
        !c43_bytes_zero(facts->reserved2, sizeof(facts->reserved2))) {
        return 0;
    }
    if (facts->queue_exists != pair_nonzero(facts->queue.word) ||
        facts->associated_cq_exists !=
            pair_nonzero(facts->associated_cq.word)) {
        return 0;
    }
    if (facts->role == FWLAB_C43_QUEUE_ROLE_IO_CQ) {
        return !facts->associated_cq_exists && !facts->association_matches;
    }
    return !facts->association_matches ||
           (facts->queue_exists && facts->associated_cq_exists);
}

int fwlab_c43_queue_effect_request_valid(
    const struct fwlab_c43_queue_effect_request *request)
{
    return request != NULL &&
           request->version == FWLAB_C43_QUEUE_EFFECT_PORT_VERSION &&
           request->size == sizeof(*request) && request->reserved0 == 0 &&
           fwlab_hif_action_envelope_valid(&request->common) &&
           request->common.token.kind == FWLAB_HIF_ACTION_QUEUE_EFFECT &&
           request->common.requested_units == 1 &&
           request->operation >= FWLAB_C43_QUEUE_CREATE_CQ &&
           request->operation <= FWLAB_C43_QUEUE_DELETE_SQ &&
           request->role >= FWLAB_C43_QUEUE_ROLE_IO_CQ &&
           request->role <= FWLAB_C43_QUEUE_ROLE_IO_SQ &&
           queue_operation_role_valid(request->operation, request->role) &&
           c43_bytes_zero(request->reserved, sizeof(request->reserved));
}

int fwlab_c43_queue_finish_request_valid(
    const struct fwlab_c43_queue_finish_request *request)
{
    return request != NULL &&
           request->version == FWLAB_C43_QUEUE_EFFECT_PORT_VERSION &&
           request->size == sizeof(*request) && request->reserved0 == 0 &&
           fwlab_hif_action_token_valid(&request->token) &&
           request->token.kind == FWLAB_HIF_ACTION_QUEUE_EFFECT &&
           pair_nonzero(request->transaction.word) &&
           request->decision >= FWLAB_C43_QUEUE_FINISH_COMMIT &&
           request->decision <= FWLAB_C43_QUEUE_FINISH_ABORT &&
           c43_bytes_zero(request->reserved1, sizeof(request->reserved1));
}

int fwlab_c43_queue_effect_terminal_valid(
    const struct fwlab_c43_queue_effect_terminal *terminal)
{
    if (terminal == NULL ||
        terminal->version != FWLAB_C43_QUEUE_EFFECT_PORT_VERSION ||
        terminal->size != sizeof(*terminal) || terminal->reserved0 != 0 ||
        !fwlab_hif_action_terminal_valid(&terminal->common) ||
        terminal->common.token.kind != FWLAB_HIF_ACTION_QUEUE_EFFECT ||
        !fwlab_c43_queue_facts_valid(&terminal->facts) ||
        terminal->decision > FWLAB_C43_QUEUE_FINISH_ABORT ||
        !c43_bytes_zero(terminal->reserved, sizeof(terminal->reserved))) {
        return 0;
    }
    switch (terminal->state) {
    case FWLAB_C43_QUEUE_EFFECT_PREPARED:
        return terminal->decision == 0 &&
               terminal->common.terminal_kind == FWLAB_HIF_ACTION_SUCCESS &&
               terminal->common.effect_class == FWLAB_NVME_EFFECT_NONE &&
               terminal->common.units_completed == 0;
    case FWLAB_C43_QUEUE_EFFECT_COMMITTED:
        return terminal->decision == FWLAB_C43_QUEUE_FINISH_COMMIT &&
               terminal->common.terminal_kind == FWLAB_HIF_ACTION_SUCCESS &&
               terminal->common.effect_class == FWLAB_NVME_EFFECT_FULL &&
               terminal->common.units_completed != 0;
    case FWLAB_C43_QUEUE_EFFECT_ABORTED:
        return terminal->decision == FWLAB_C43_QUEUE_FINISH_ABORT &&
               terminal->common.terminal_kind == FWLAB_HIF_ACTION_SUCCESS &&
               terminal->common.effect_class == FWLAB_NVME_EFFECT_NONE &&
               terminal->common.units_completed == 0;
    case FWLAB_C43_QUEUE_EFFECT_TOO_LATE:
        return terminal->decision == FWLAB_C43_QUEUE_FINISH_ABORT &&
               terminal->common.terminal_kind == FWLAB_HIF_ACTION_SUCCESS &&
               terminal->common.effect_class == FWLAB_NVME_EFFECT_FULL &&
               terminal->common.units_completed != 0;
    case FWLAB_C43_QUEUE_EFFECT_RESET_SUPERSEDED:
        return terminal->common.terminal_kind ==
                   FWLAB_HIF_ACTION_CANCELLED &&
               effect_units_valid(terminal->common.effect_class,
                                  terminal->common.units_completed);
    case FWLAB_C43_QUEUE_EFFECT_FAILED:
        return terminal->common.terminal_kind == FWLAB_HIF_ACTION_FAILED &&
               terminal->common.effect_class == FWLAB_NVME_EFFECT_NONE &&
               terminal->common.units_completed == 0;
    case FWLAB_C43_QUEUE_EFFECT_POISONED:
        return terminal->common.terminal_kind == FWLAB_HIF_ACTION_FAILED &&
               terminal->common.effect_class ==
                   FWLAB_NVME_EFFECT_UNKNOWN_PREFIX;
    default:
        return 0;
    }
}

int fwlab_c43_queue_effect_terminal_matches_request(
    const struct fwlab_c43_queue_effect_request *request,
    const struct fwlab_c43_queue_effect_terminal *terminal)
{
    return fwlab_c43_queue_effect_request_valid(request) &&
           fwlab_c43_queue_effect_terminal_valid(terminal) &&
           terminal_matches_envelope(&terminal->common, &request->common) &&
           terminal->facts.operation == request->operation &&
           terminal->facts.role == request->role &&
           ((terminal->state != FWLAB_C43_QUEUE_EFFECT_COMMITTED &&
             terminal->state != FWLAB_C43_QUEUE_EFFECT_TOO_LATE) ||
            terminal->common.units_completed ==
                request->common.requested_units);
}

int fwlab_c43_queue_finish_terminal_matches_request(
    const struct fwlab_c43_queue_finish_request *request,
    const struct fwlab_c43_queue_effect_terminal *terminal)
{
    return fwlab_c43_queue_finish_request_valid(request) &&
           fwlab_c43_queue_effect_terminal_valid(terminal) &&
           c43_action_token_equal(&request->token, &terminal->common.token) &&
           pair_equal(request->transaction.word,
                      terminal->facts.transaction.word) &&
           request->decision == terminal->decision;
}

int fwlab_c43_queue_effect_port_valid(
    const struct fwlab_c43_queue_effect_port *port)
{
    const struct fwlab_c43_queue_effect_port_ops *ops;

    if (port == NULL || port->ops == NULL || port->context == NULL ||
        port->generation == 0) {
        return 0;
    }
    ops = port->ops;
    return ops->version == FWLAB_C43_QUEUE_EFFECT_PORT_VERSION &&
           ops->size == sizeof(*ops) && ops->reserved0 == 0 &&
           ops->prepare_start != NULL && ops->prepare_query != NULL &&
           ops->finish_start != NULL && ops->finish_query != NULL &&
           ops->cancel != NULL && ops->retire != NULL &&
           ops->reset_begin != NULL && ops->quiescent != NULL &&
           c43_bytes_zero(ops->reserved1, sizeof(ops->reserved1));
}

int fwlab_c43_target_request_valid(
    const struct fwlab_c43_target_request *request)
{
    return request != NULL &&
           request->version == FWLAB_C43_TARGET_RESOLVER_PORT_VERSION &&
           request->size == sizeof(*request) && request->reserved0 == 0 &&
           fwlab_hif_action_envelope_valid(&request->common) &&
           request->common.token.kind == FWLAB_HIF_ACTION_QUEUE_EFFECT &&
           request->common.requested_units == 1 &&
           c43_ticket_valid(&request->abort_command) &&
           c43_handle_equal(&request->common.token.command,
                            &request->abort_command.handle) &&
           c43_origin_equal(&request->common.token.origin,
                            &request->abort_command.origin) &&
           request->operation == FWLAB_C43_TARGET_RESOLVE_ABORT &&
           c43_bytes_zero(request->reserved, sizeof(request->reserved));
}

int fwlab_c43_target_terminal_valid(
    const struct fwlab_c43_target_terminal *terminal)
{
    const int found = terminal != NULL &&
                      terminal->outcome == FWLAB_C43_TARGET_FOUND;

    if (terminal == NULL ||
        terminal->version != FWLAB_C43_TARGET_RESOLVER_PORT_VERSION ||
        terminal->size != sizeof(*terminal) || terminal->reserved0 != 0 ||
        !fwlab_hif_action_terminal_valid(&terminal->common) ||
        terminal->common.token.kind != FWLAB_HIF_ACTION_QUEUE_EFFECT ||
        terminal->common.effect_class != FWLAB_NVME_EFFECT_NONE ||
        terminal->common.units_completed != 0 ||
        terminal->outcome < FWLAB_C43_TARGET_FOUND ||
        terminal->outcome > FWLAB_C43_TARGET_RELEASED ||
        !c43_bytes_zero(terminal->reserved, sizeof(terminal->reserved))) {
        return 0;
    }
    if (found) {
        return c43_ticket_valid(&terminal->target) &&
               pair_nonzero(terminal->reference.word) &&
               terminal->target.handle.instance_nonce ==
                   terminal->common.token.command.instance_nonce &&
               terminal->target.handle.controller_epoch ==
                   terminal->common.token.command.controller_epoch &&
               !c43_handle_equal(&terminal->target.handle,
                                  &terminal->common.token.command) &&
               terminal->common.terminal_kind == FWLAB_HIF_ACTION_SUCCESS;
    }
    if (!c43_bytes_zero(&terminal->target, sizeof(terminal->target)) ||
        pair_nonzero(terminal->reference.word)) {
        return 0;
    }
    switch (terminal->outcome) {
    case FWLAB_C43_TARGET_NOT_FOUND:
    case FWLAB_C43_TARGET_TOO_LATE:
    case FWLAB_C43_TARGET_RELEASED:
        return terminal->common.terminal_kind == FWLAB_HIF_ACTION_SUCCESS;
    case FWLAB_C43_TARGET_STALE:
    case FWLAB_C43_TARGET_SUPERSEDED:
        return terminal->common.terminal_kind == FWLAB_HIF_ACTION_CANCELLED;
    case FWLAB_C43_TARGET_FAULT:
        return terminal->common.terminal_kind == FWLAB_HIF_ACTION_FAILED;
    default:
        return 0;
    }
}

int fwlab_c43_target_terminal_matches_request(
    const struct fwlab_c43_target_request *request,
    const struct fwlab_c43_target_terminal *terminal)
{
    return fwlab_c43_target_request_valid(request) &&
           fwlab_c43_target_terminal_valid(terminal) &&
           terminal_matches_envelope(&terminal->common, &request->common);
}

int fwlab_c43_target_resolver_port_valid(
    const struct fwlab_c43_target_resolver_port *port)
{
    const struct fwlab_c43_target_resolver_port_ops *ops;

    if (port == NULL || port->ops == NULL || port->context == NULL ||
        port->generation == 0) {
        return 0;
    }
    ops = port->ops;
    return ops->version == FWLAB_C43_TARGET_RESOLVER_PORT_VERSION &&
           ops->size == sizeof(*ops) && ops->reserved0 == 0 &&
           ops->submit != NULL && ops->query != NULL && ops->cancel != NULL &&
           ops->release != NULL && ops->release_query != NULL &&
           ops->reset_begin != NULL && ops->quiescent != NULL &&
           c43_bytes_zero(ops->reserved1, sizeof(ops->reserved1));
}

int fwlab_c43_block_action_request_valid(
    const struct fwlab_c43_block_action_request *request)
{
    uint32_t expected;

    if (request == NULL ||
        request->version != FWLAB_C43_BLOCK_ACTION_PORT_VERSION ||
        request->size != sizeof(*request) || request->reserved0 != 0 ||
        !fwlab_hif_action_envelope_valid(&request->common) ||
        request->common.token.kind != FWLAB_HIF_ACTION_BLOCK ||
        !fwlab_c43_block_intent_valid(&request->intent) ||
        !c43_handle_equal(&request->common.token.command,
                          &request->intent.command) ||
        !c43_origin_equal(&request->common.token.origin,
                          &request->intent.origin) ||
        !c43_witness_mask_valid(request->requested_witness_mask) ||
        !c43_bytes_zero(request->reserved, sizeof(request->reserved))) {
        return 0;
    }
    if (request->common.requested_units !=
        (request->intent.lba_count == 0 ? 1 : request->intent.lba_count)) {
        return 0;
    }
    if (request->requested_witness_mask ==
        FWLAB_C43_WITNESS_VALIDATED_ONLY) {
        return c43_ref_zero(&request->predecessor);
    }
    if (request->intent.operation == FWLAB_C43_BLOCK_READ) {
        expected = FWLAB_C43_WITNESS_BLOCK_READ_READY;
    } else if (request->intent.operation == FWLAB_C43_BLOCK_WRITE) {
        expected = FWLAB_C43_WITNESS_BLOCK_WRITE_COMPLETE;
        if (request->intent.durability ==
            FWLAB_C43_DURABILITY_REQUIRE_SELF) {
            expected |= FWLAB_C43_WITNESS_DURABILITY_COMPLETE;
        }
    } else {
        expected = FWLAB_C43_WITNESS_DURABILITY_COMPLETE;
    }
    return request->requested_witness_mask == expected &&
           !c43_ref_zero(&request->predecessor);
}

int fwlab_c43_block_action_terminal_valid(
    const struct fwlab_c43_block_action_terminal *terminal)
{
    if (terminal == NULL ||
        terminal->version != FWLAB_C43_BLOCK_ACTION_PORT_VERSION ||
        terminal->size != sizeof(*terminal) || terminal->reserved0 != 0 ||
        !fwlab_hif_action_terminal_valid(&terminal->common) ||
        terminal->common.token.kind != FWLAB_HIF_ACTION_BLOCK ||
        !fwlab_c43_completion_witness_valid(&terminal->witness) ||
        !c43_handle_equal(&terminal->common.token.command,
                          &terminal->witness.command) ||
        !c43_origin_equal(&terminal->common.token.origin,
                          &terminal->witness.origin) ||
        terminal->common.terminal_kind != terminal->witness.terminal_kind ||
        terminal->common.effect_class != terminal->witness.effect_class ||
        terminal->common.units_completed !=
            terminal->witness.units_completed ||
        terminal->block_terminal_kind < FWLAB_C43_BLOCK_VALIDATED_ONLY ||
        terminal->block_terminal_kind > FWLAB_C43_BLOCK_COMPLETED ||
        !c43_bytes_zero(terminal->reserved, sizeof(terminal->reserved))) {
        return 0;
    }
    if (terminal->block_terminal_kind == FWLAB_C43_BLOCK_VALIDATED_ONLY) {
        return terminal->common.terminal_kind == FWLAB_HIF_ACTION_SUCCESS &&
               terminal->witness.witness_mask ==
                   FWLAB_C43_WITNESS_VALIDATED_ONLY &&
               terminal->common.effect_class == FWLAB_NVME_EFFECT_NONE &&
               terminal->witness.effect_class == FWLAB_NVME_EFFECT_NONE &&
               terminal->witness.units_completed == 0;
    }
    if (terminal->block_terminal_kind ==
        FWLAB_C43_BLOCK_FAILED_NO_EFFECT) {
        return terminal->common.terminal_kind == FWLAB_HIF_ACTION_FAILED &&
               terminal->common.effect_class == FWLAB_NVME_EFFECT_NONE &&
               terminal->witness.witness_mask == 0 &&
               terminal->witness.effect_class == FWLAB_NVME_EFFECT_NONE &&
               terminal->witness.units_completed == 0;
    }
    if (terminal->block_terminal_kind == FWLAB_C43_BLOCK_CANCELLED) {
        return terminal->common.terminal_kind == FWLAB_HIF_ACTION_CANCELLED &&
               terminal->witness.witness_mask == 0 &&
               effect_units_valid(terminal->common.effect_class,
                                  terminal->common.units_completed);
    }
    return terminal->common.terminal_kind == FWLAB_HIF_ACTION_SUCCESS &&
           terminal->witness.witness_mask != 0 &&
           (terminal->witness.witness_mask &
            ~(FWLAB_C43_WITNESS_BLOCK_READ_READY |
              FWLAB_C43_WITNESS_BLOCK_WRITE_COMPLETE |
              FWLAB_C43_WITNESS_DURABILITY_COMPLETE)) == 0 &&
           !c43_ref_zero(&terminal->witness.predecessor) &&
           effect_units_valid(terminal->common.effect_class,
                              terminal->common.units_completed);
}

int fwlab_c43_block_action_request_valid_for_port(
    const struct fwlab_c43_block_action_request *request,
    const struct fwlab_c43_block_action_port *port)
{
    uint32_t required_capability = 0;

    if (!fwlab_c43_block_action_request_valid(request) ||
        !fwlab_c43_block_action_port_valid(port)) {
        return 0;
    }
    if (request->requested_witness_mask ==
        FWLAB_C43_WITNESS_VALIDATED_ONLY) {
        required_capability = FWLAB_C43_BLOCK_CAP_VALIDATION_ONLY;
    } else {
        if ((request->requested_witness_mask &
             (FWLAB_C43_WITNESS_BLOCK_READ_READY |
              FWLAB_C43_WITNESS_BLOCK_WRITE_COMPLETE)) != 0) {
            required_capability |= FWLAB_C43_BLOCK_CAP_DATA_EFFECT;
        }
        if ((request->requested_witness_mask &
             FWLAB_C43_WITNESS_DURABILITY_COMPLETE) != 0) {
            required_capability |= FWLAB_C43_BLOCK_CAP_DURABILITY;
        }
    }
    return (port->capability_bits & required_capability) ==
           required_capability;
}

int fwlab_c43_block_action_terminal_matches_request(
    const struct fwlab_c43_block_action_request *request,
    const struct fwlab_c43_block_action_terminal *terminal,
    const struct fwlab_c43_block_action_port *port)
{
    if (!fwlab_c43_block_action_request_valid_for_port(request, port) ||
        !fwlab_c43_block_action_terminal_valid(terminal) ||
        !terminal_matches_envelope(&terminal->common, &request->common) ||
        terminal->witness.provider_generation != port->generation) {
        return 0;
    }
    if (terminal->block_terminal_kind == FWLAB_C43_BLOCK_FAILED_NO_EFFECT ||
        terminal->block_terminal_kind == FWLAB_C43_BLOCK_CANCELLED) {
        return 1;
    }
    return c43_ref_equal(&terminal->witness.predecessor,
                         &request->predecessor) &&
           terminal->witness.witness_mask == request->requested_witness_mask;
}

int fwlab_c43_block_action_port_valid(
    const struct fwlab_c43_block_action_port *port)
{
    const struct fwlab_c43_block_action_port_ops *ops;

    if (port == NULL || port->ops == NULL || port->context == NULL ||
        port->generation == 0 || port->capability_bits == 0 ||
        (port->capability_bits & ~FWLAB_C43_BLOCK_CAP_ALL) != 0 ||
        port->reserved != 0) {
        return 0;
    }
    ops = port->ops;
    return ops->version == FWLAB_C43_BLOCK_ACTION_PORT_VERSION &&
           ops->size == sizeof(*ops) && ops->reserved0 == 0 &&
           ops->submit != NULL && ops->query != NULL && ops->cancel != NULL &&
           ops->retire != NULL && ops->reset_begin != NULL &&
           ops->quiescent != NULL &&
           c43_bytes_zero(ops->reserved1, sizeof(ops->reserved1));
}
