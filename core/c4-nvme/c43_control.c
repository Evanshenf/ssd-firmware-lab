/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_internal.h"

#include <string.h>

int c43_semantic_status_valid(uint32_t status)
{
    return status <= FWLAB_C43_STATUS_INTERNAL_FAILURE;
}

int c43_witness_mask_valid(uint32_t mask)
{
    return (mask & ~FWLAB_C43_WITNESS_ALL) == 0;
}

static int queue_ref_nonzero(const struct fwlab_c43_queue_live_ref *reference)
{
    return reference->word[0] != 0 || reference->word[1] != 0;
}

static int queue_ref_equal(
    const struct fwlab_c43_queue_live_ref *left,
    const struct fwlab_c43_queue_live_ref *right)
{
    return left->word[0] == right->word[0] &&
           left->word[1] == right->word[1];
}

static int target_ref_nonzero(
    const struct fwlab_c43_abort_target_ref *reference)
{
    return reference->word[0] != 0 || reference->word[1] != 0;
}

static int target_fields_zero(
    const struct fwlab_c43_command_observer *command)
{
    return command->action_outcome == 0 && !command->target_pin_present &&
           c43_bytes_zero(&command->target_ticket,
                          sizeof(command->target_ticket)) &&
           !target_ref_nonzero(&command->target_reference);
}

static int observer_ticket_matches_command(
    const struct fwlab_hif_command_ticket *ticket,
    const struct fwlab_c43_command_observer *command)
{
    return c43_ticket_valid(ticket) && command->in_use &&
           command->phase >= FWLAB_C43_PHASE_ADMITTED_POLICY &&
           c43_handle_equal(&ticket->handle, &command->handle) &&
           c43_origin_equal(&ticket->origin, &command->origin) &&
           ticket->ticket_uid == command->transaction_uid &&
           ticket->generation == command->action_generation;
}

static int command_ticket_equal(
    const struct fwlab_hif_command_ticket *left,
    const struct fwlab_hif_command_ticket *right)
{
    return c43_handle_equal(&left->handle, &right->handle) &&
           c43_origin_equal(&left->origin, &right->origin) &&
           left->ticket_uid == right->ticket_uid &&
           left->generation == right->generation &&
           left->reserved == right->reserved;
}

static int queue_public_state_needs_owner(uint8_t state)
{
    return (state >= FWLAB_C43_ACTION_STATE_SUBMIT_READY &&
            state <= FWLAB_C43_ACTION_STATE_RETIRE) ||
           state == FWLAB_C43_ACTION_STATE_FAULT;
}

static int queue_public_command_valid(
    const struct fwlab_c43_graph_observer *observer,
    uint32_t slot,
    const struct fwlab_c43_command_observer *command)
{
    int expected_resolution;
    int expected_phase;
    int witness_complete;
    int expected_success;
    const int needs_owner =
        queue_public_state_needs_owner(command->action_state);
    const int is_owner = observer->queue_txn_active &&
                         observer->queue_owner_slot_plus_one == slot + 1;

    if (command->action_domain != FWLAB_C43_ACTION_DOMAIN_QUEUE) {
        return 1;
    }
    if (command->action_state == FWLAB_C43_ACTION_STATE_NONE ||
        command->terminal_winner != FWLAB_C43_WINNER_NONE ||
        command->publication != FWLAB_C43_PUBLICATION_ELIGIBLE ||
        (command->required_witness_mask &
         ~FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED) != 0 ||
        (command->satisfied_witness_mask &
         ~FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED) != 0 ||
        !target_fields_zero(command) || command->block_terminal_kind != 0 ||
        needs_owner != is_owner) {
        return 0;
    }
    switch (command->action_state) {
    case FWLAB_C43_ACTION_STATE_SUBMIT_READY:
    case FWLAB_C43_ACTION_STATE_PREPARE_QUERY:
    case FWLAB_C43_ACTION_STATE_DECIDE:
        expected_phase = FWLAB_C43_PHASE_RESOLVE_WAIT;
        expected_resolution = 0;
        break;
    case FWLAB_C43_ACTION_STATE_FINISH_READY:
    case FWLAB_C43_ACTION_STATE_FINISH_QUERY:
    case FWLAB_C43_ACTION_STATE_APPLY_COMMIT:
    case FWLAB_C43_ACTION_STATE_APPLY_ABORT:
        expected_phase = FWLAB_C43_PHASE_RESOLVE_WAIT;
        expected_resolution = 1;
        break;
    case FWLAB_C43_ACTION_STATE_RETIRE:
    case FWLAB_C43_ACTION_STATE_TERMINAL_HELD:
    case FWLAB_C43_ACTION_STATE_REJECTED_NO_EFFECT:
    case FWLAB_C43_ACTION_STATE_FAULT:
        expected_phase = FWLAB_C43_PHASE_ACTION_WAIT;
        expected_resolution = 1;
        break;
    default:
        return 0;
    }
    witness_complete = command->required_witness_mask == 0 ||
        (command->satisfied_witness_mask &
         command->required_witness_mask) == command->required_witness_mask;
    expected_success = command->resolution_valid &&
                       command->resolved_status ==
                           FWLAB_C43_STATUS_SUCCESS &&
                       witness_complete;
    return command->phase == (uint32_t)expected_phase &&
           command->resolution_valid == (uint8_t)expected_resolution &&
           command->success_eligible == (uint8_t)expected_success &&
           (command->satisfied_witness_mask == 0 ||
            command->action_state == FWLAB_C43_ACTION_STATE_RETIRE ||
            command->action_state ==
                FWLAB_C43_ACTION_STATE_TERMINAL_HELD ||
            command->action_state == FWLAB_C43_ACTION_STATE_FAULT);
}

static int target_public_command_valid(
    const struct fwlab_c43_graph_observer *observer,
    uint32_t slot,
    const struct fwlab_c43_command_observer *command,
    uint16_t expected_incoming[FWLAB_C43_MAX_COMMANDS])
{
    uint32_t target_slot;
    int found = 0;

    if (command->action_domain != FWLAB_C43_ACTION_DOMAIN_TARGET) {
        return 1;
    }
    if (command->terminal_winner != FWLAB_C43_WINNER_NONE ||
        command->publication != FWLAB_C43_PUBLICATION_ELIGIBLE ||
        command->required_witness_mask != 0 ||
        command->satisfied_witness_mask != 0 ||
        command->block_terminal_kind != 0 ||
        (observer->queue_txn_active &&
         observer->queue_owner_slot_plus_one == slot + 1)) {
        return 0;
    }
    switch (command->action_state) {
    case FWLAB_C43_ACTION_STATE_SUBMIT_READY:
    case FWLAB_C43_ACTION_STATE_ACCEPTED_WAIT:
        return command->phase == FWLAB_C43_PHASE_RESOLVE_WAIT &&
               !command->resolution_valid && !command->success_eligible &&
               target_fields_zero(command);
    case FWLAB_C43_ACTION_STATE_TERMINAL_HELD:
        if (command->phase != FWLAB_C43_PHASE_ACTION_WAIT ||
            !command->resolution_valid ||
            command->resolved_status != FWLAB_C43_STATUS_SUCCESS ||
            !command->success_eligible ||
            command->action_outcome < FWLAB_C43_TARGET_FOUND ||
            command->action_outcome > FWLAB_C43_TARGET_SUPERSEDED) {
            return 0;
        }
        if (command->action_outcome != FWLAB_C43_TARGET_FOUND) {
            return !command->target_pin_present &&
                   c43_bytes_zero(&command->target_ticket,
                                  sizeof(command->target_ticket)) &&
                   !target_ref_nonzero(&command->target_reference);
        }
        if (!command->target_pin_present ||
            !target_ref_nonzero(&command->target_reference)) {
            return 0;
        }
        for (target_slot = 0; target_slot < FWLAB_C43_MAX_COMMANDS;
             ++target_slot) {
            if (target_slot != slot &&
                observer_ticket_matches_command(
                    &command->target_ticket,
                    &observer->commands[target_slot])) {
                if (found || expected_incoming[target_slot] != 0) {
                    return 0;
                }
                found = 1;
                expected_incoming[target_slot] = 1;
            }
        }
        return found;
    case FWLAB_C43_ACTION_STATE_FAULT:
        return command->phase == FWLAB_C43_PHASE_ACTION_WAIT &&
               command->resolution_valid &&
               command->resolved_status ==
                   FWLAB_C43_STATUS_INTERNAL_FAILURE &&
               !command->success_eligible &&
               (command->action_outcome == 0 ||
                command->action_outcome == FWLAB_C43_TARGET_FAULT) &&
               !command->target_pin_present &&
               c43_bytes_zero(&command->target_ticket,
                              sizeof(command->target_ticket)) &&
               !target_ref_nonzero(&command->target_reference);
    default:
        return 0;
    }
}

static int block_public_command_valid(
    const struct fwlab_c43_graph_observer *observer,
    uint32_t slot,
    const struct fwlab_c43_command_observer *command)
{
    if (command->action_domain != FWLAB_C43_ACTION_DOMAIN_BLOCK) {
        return 1;
    }
    if (command->terminal_winner != FWLAB_C43_WINNER_NONE ||
        command->publication != FWLAB_C43_PUBLICATION_ELIGIBLE ||
        command->required_witness_mask == 0 ||
        (command->required_witness_mask &
         FWLAB_C43_WITNESS_VALIDATED_ONLY) != 0 ||
        (command->satisfied_witness_mask &
         ~FWLAB_C43_WITNESS_VALIDATED_ONLY) != 0 ||
        command->success_eligible || !target_fields_zero(command) ||
        (observer->queue_txn_active &&
         observer->queue_owner_slot_plus_one == slot + 1)) {
        return 0;
    }
    switch (command->action_state) {
    case FWLAB_C43_ACTION_STATE_SUBMIT_READY:
    case FWLAB_C43_ACTION_STATE_ACCEPTED_WAIT:
        return command->phase == FWLAB_C43_PHASE_RESOLVE_WAIT &&
               !command->resolution_valid &&
               command->block_terminal_kind == 0 &&
               command->satisfied_witness_mask == 0;
    case FWLAB_C43_ACTION_STATE_TERMINAL_HELD:
        if (command->phase != FWLAB_C43_PHASE_ACTION_WAIT ||
            !command->resolution_valid) {
            return 0;
        }
        if (command->block_terminal_kind ==
            FWLAB_C43_BLOCK_VALIDATED_ONLY) {
            return command->resolved_status == FWLAB_C43_STATUS_SUCCESS &&
                   command->satisfied_witness_mask ==
                       FWLAB_C43_WITNESS_VALIDATED_ONLY;
        }
        return command->block_terminal_kind ==
                   FWLAB_C43_BLOCK_FAILED_NO_EFFECT &&
               command->resolved_status == FWLAB_C43_STATUS_MEDIA_FAILURE &&
               command->satisfied_witness_mask == 0;
    case FWLAB_C43_ACTION_STATE_REJECTED_NO_EFFECT:
        return command->phase == FWLAB_C43_PHASE_ACTION_WAIT &&
               command->resolution_valid &&
               command->resolved_status ==
                   FWLAB_C43_STATUS_RESOURCE_FAILURE &&
               command->block_terminal_kind == 0 &&
               command->satisfied_witness_mask == 0;
    case FWLAB_C43_ACTION_STATE_FAULT:
        return command->phase == FWLAB_C43_PHASE_ACTION_WAIT &&
               command->resolution_valid &&
               command->resolved_status ==
                   FWLAB_C43_STATUS_INTERNAL_FAILURE &&
               command->block_terminal_kind == 0 &&
               command->satisfied_witness_mask == 0;
    default:
        return 0;
    }
}

int fwlab_c43_graph_observer_valid(
    const struct fwlab_c43_graph_observer *observer)
{
    uint32_t index;
    uint32_t active_commands = 0;
    uint32_t active_actions = 0;
    uint32_t ready_count = 0;
    uint32_t cleanup_count = 0;
    uint32_t credit_count[10] = {0};
    uint16_t expected_incoming[FWLAB_C43_MAX_COMMANDS] = {0};

    if (observer == NULL || observer->version != FWLAB_C43_GRAPH_VERSION ||
        observer->size != sizeof(*observer) || observer->controller_epoch == 0 ||
        observer->instance_nonce == 0 ||
        observer->active_commands > FWLAB_C43_MAX_COMMANDS ||
        observer->active_actions > FWLAB_C43_MAX_ACTIONS ||
        observer->ready_count > FWLAB_C43_MAX_COMMANDS ||
        observer->cleanup_count > FWLAB_C43_MAX_COMMANDS ||
        observer->admission_closed > 1 || observer->resetting > 1 ||
        observer->tearing_down > 1 || observer->dead > 1 ||
        observer->reserved_intent_credits > FWLAB_C43_MAX_COMMANDS ||
        observer->reserved_ready_credits > FWLAB_C43_MAX_COMMANDS ||
        observer->reserved_lease_credits > FWLAB_C43_MAX_COMMANDS ||
        observer->reserved_consume_credits > FWLAB_C43_MAX_COMMANDS ||
        observer->reserved_finalizer_credits > FWLAB_C43_MAX_COMMANDS ||
        observer->reserved_abort_credits > FWLAB_C43_MAX_COMMANDS ||
        observer->reserved_target_credits > FWLAB_C43_MAX_COMMANDS ||
        observer->reserved_queue_transaction_credits >
            FWLAB_C43_MAX_COMMANDS ||
        observer->reserved_block_intent_credits >
            FWLAB_C43_MAX_COMMANDS ||
        observer->nq_state > FWLAB_C43_NQ_NEGOTIATED ||
        observer->queue_txn_active > 1 || observer->io_cq_present > 1 ||
        observer->io_sq_present > 1 ||
        !c43_bytes_zero(observer->reserved_queue_flags,
                        sizeof(observer->reserved_queue_flags)) ||
        !c43_bytes_zero(observer->reserved_queue,
                        sizeof(observer->reserved_queue))) {
        return 0;
    }
    if ((observer->queue_txn_active == 0) !=
            (observer->queue_owner_slot_plus_one == 0) ||
        observer->queue_owner_slot_plus_one > FWLAB_C43_MAX_COMMANDS ||
        observer->io_cq_present != queue_ref_nonzero(&observer->io_cq) ||
        observer->io_sq_present != queue_ref_nonzero(&observer->io_sq) ||
        ((observer->io_cq_present || observer->io_sq_present) &&
         observer->nq_state != FWLAB_C43_NQ_NEGOTIATED) ||
        (!observer->io_sq_present &&
         queue_ref_nonzero(&observer->sq_associated_cq)) ||
        (observer->io_sq_present &&
         (!observer->io_cq_present ||
          !queue_ref_equal(&observer->sq_associated_cq,
                           &observer->io_cq)))) {
        return 0;
    }
    for (index = 0; index < 4; ++index) {
        if (observer->provider_generation[index] == 0) {
            return 0;
        }
    }
    for (index = 0; index < FWLAB_C43_MAX_COMMANDS; ++index) {
        const struct fwlab_c43_command_observer *command =
            &observer->commands[index];

        if (command->phase > FWLAB_C43_PHASE_RETIRED_TOMBSTONE ||
            command->terminal_winner > FWLAB_C43_WINNER_FAULT ||
            command->publication > FWLAB_C43_PUBLICATION_SUPPRESSED ||
            !c43_witness_mask_valid(command->required_witness_mask) ||
            !c43_witness_mask_valid(command->satisfied_witness_mask) ||
            (command->required_witness_mask &
             FWLAB_C43_WITNESS_VALIDATED_ONLY) != 0 ||
            (command->satisfied_witness_mask &
             ~(command->required_witness_mask |
               FWLAB_C43_WITNESS_VALIDATED_ONLY)) != 0 ||
            command->action_count > FWLAB_C43_ACTIONS_PER_COMMAND ||
            command->in_use > 1 || command->success_eligible > 1 ||
            command->provider_generation_current > 1 ||
            command->action_domain > FWLAB_C43_ACTION_DOMAIN_BLOCK ||
            command->action_state > FWLAB_C43_ACTION_STATE_ACCEPTED_WAIT ||
            command->resolution_valid > 1 ||
            command->action_outcome > FWLAB_C43_TARGET_RELEASED ||
            command->block_terminal_kind > FWLAB_C43_BLOCK_COMPLETED ||
            command->incoming_target_pins > 1 ||
            command->target_pin_present > 1 ||
            command->reserved_action_flags != 0 ||
            !c43_bytes_zero(command->reserved_action,
                            sizeof(command->reserved_action)) ||
            (command->action_domain != FWLAB_C43_ACTION_DOMAIN_TARGET &&
             !target_fields_zero(command)) ||
            (command->action_domain != FWLAB_C43_ACTION_DOMAIN_BLOCK &&
             command->block_terminal_kind != 0) ||
            (!command->resolution_valid && command->resolved_status != 0) ||
            (command->resolution_valid &&
             !c43_semantic_status_valid(command->resolved_status)) ||
            ((command->action_domain == FWLAB_C43_ACTION_DOMAIN_NONE) !=
             (command->action_state == FWLAB_C43_ACTION_STATE_NONE)) ||
            (command->reservation_credit_mask &
             ~FWLAB_C43_CREDIT_ALL) != 0 ||
            ((command->action_count == 0) !=
             (command->first_action_uid == 0)) ||
            ((command->action_count == 0) !=
             (command->action_generation == 0)) ||
            (command->action_count != 0 &&
             command->first_action_uid >
                 UINT64_MAX - (command->action_count - 1))) {
            return 0;
        }
        if (command->in_use) {
            uint32_t other;
            uint32_t credit;
            const int witness_complete =
                command->required_witness_mask != 0 &&
                (command->satisfied_witness_mask &
                 command->required_witness_mask) ==
                    command->required_witness_mask;
            const int initial_phase =
                command->phase == FWLAB_C43_PHASE_PREPARED ||
                command->phase == FWLAB_C43_PHASE_ADMITTED_POLICY;

            if (command->phase == FWLAB_C43_PHASE_FREE ||
                (initial_phase &&
                 (command->action_count !=
                      FWLAB_C43_ACTIONS_PER_COMMAND ||
                  command->reservation_credit_mask != FWLAB_C43_CREDIT_ALL ||
                  command->terminal_winner != FWLAB_C43_WINNER_NONE ||
                  command->publication !=
                      FWLAB_C43_PUBLICATION_ELIGIBLE ||
                  command->provider_generation_current != 1 ||
                  command->action_domain !=
                      FWLAB_C43_ACTION_DOMAIN_NONE ||
                  command->action_state != FWLAB_C43_ACTION_STATE_NONE ||
                  command->resolution_valid != 0 ||
                  command->resolved_status != 0)) ||
                (command->phase == FWLAB_C43_PHASE_PREPARED &&
                 (command->required_witness_mask != 0 ||
                  command->satisfied_witness_mask != 0 ||
                  command->success_eligible != 0)) ||
                (command->phase == FWLAB_C43_PHASE_ADMITTED_POLICY &&
                 (command->satisfied_witness_mask != 0 ||
                  command->success_eligible != 0)) ||
                !c43_handle_valid(&command->handle) ||
                !c43_origin_valid(&command->origin) ||
                command->transaction_uid == 0 ||
                command->handle.instance_nonce != observer->instance_nonce ||
                command->handle.controller_epoch !=
                    observer->controller_epoch ||
                (command->action_domain != FWLAB_C43_ACTION_DOMAIN_QUEUE &&
                 command->required_witness_mask != 0 &&
                 command->success_eligible != witness_complete) ||
                (command->success_eligible &&
                 (((command->satisfied_witness_mask &
                    command->required_witness_mask) !=
                   command->required_witness_mask) ||
                  (command->satisfied_witness_mask &
                   FWLAB_C43_WITNESS_VALIDATED_ONLY) != 0))) {
                return 0;
            }
            if (!queue_public_command_valid(observer, index, command)) {
                return 0;
            }
            if (!target_public_command_valid(observer, index, command,
                                             expected_incoming) ||
                !block_public_command_valid(observer, index, command)) {
                return 0;
            }
            for (other = 0; other < index; ++other) {
                const struct fwlab_c43_command_observer *prior =
                    &observer->commands[other];

                if (prior->in_use &&
                    (c43_handle_equal(&command->handle, &prior->handle) ||
                     command->transaction_uid == prior->transaction_uid)) {
                    return 0;
                }
                if (prior->in_use && command->action_count != 0 &&
                    prior->action_count != 0 &&
                    command->first_action_uid <=
                        prior->first_action_uid + prior->action_count - 1 &&
                    prior->first_action_uid <=
                        command->first_action_uid + command->action_count - 1) {
                    return 0;
                }
            }
            ++active_commands;
            active_actions += command->action_count;
            for (credit = 0; credit < 10; ++credit) {
                if ((command->reservation_credit_mask &
                     (UINT32_C(1) << credit)) != 0) {
                    ++credit_count[credit];
                }
            }
            if (command->phase == FWLAB_C43_PHASE_INTENT_READY) {
                ++ready_count;
            }
            if (command->phase == FWLAB_C43_PHASE_CLEANUP_PENDING ||
                command->phase == FWLAB_C43_PHASE_RETIRED_TOMBSTONE) {
                ++cleanup_count;
            }
        } else {
            if (command->phase != FWLAB_C43_PHASE_FREE ||
                !c43_bytes_zero(&command->handle, sizeof(command->handle)) ||
                !c43_bytes_zero(&command->origin, sizeof(command->origin)) ||
                command->transaction_uid != 0 || command->action_count != 0 ||
                command->terminal_winner != FWLAB_C43_WINNER_NONE ||
                command->required_witness_mask != 0 ||
                command->satisfied_witness_mask != 0 ||
                command->success_eligible != 0 ||
                command->provider_generation_current != 0 ||
                command->reservation_credit_mask != 0 ||
                command->first_action_uid != 0 ||
                command->action_generation != 0 ||
                command->action_domain != FWLAB_C43_ACTION_DOMAIN_NONE ||
                command->action_state != FWLAB_C43_ACTION_STATE_NONE ||
                command->resolution_valid != 0 ||
                command->resolved_status != 0 ||
                command->action_outcome != 0 ||
                command->block_terminal_kind != 0 ||
                !c43_bytes_zero(&command->target_ticket,
                                sizeof(command->target_ticket)) ||
                target_ref_nonzero(&command->target_reference) ||
                command->incoming_target_pins != 0 ||
                command->target_pin_present != 0 ||
                command->reserved_action_flags != 0 ||
                !c43_bytes_zero(command->reserved_action,
                                sizeof(command->reserved_action)) ||
                command->publication != FWLAB_C43_PUBLICATION_ELIGIBLE) {
                return 0;
            }
        }
    }
    for (index = 0; index < FWLAB_C43_MAX_COMMANDS; ++index) {
        if (observer->commands[index].incoming_target_pins !=
            expected_incoming[index]) {
            return 0;
        }
    }
    if (observer->queue_txn_active) {
        const struct fwlab_c43_command_observer *owner =
            &observer->commands[observer->queue_owner_slot_plus_one - 1];

        if (!owner->in_use ||
            owner->action_domain != FWLAB_C43_ACTION_DOMAIN_QUEUE ||
            owner->action_state == FWLAB_C43_ACTION_STATE_NONE ||
            owner->action_state == FWLAB_C43_ACTION_STATE_TERMINAL_HELD ||
            owner->action_state ==
                FWLAB_C43_ACTION_STATE_REJECTED_NO_EFFECT) {
            return 0;
        }
    }
    return observer->active_commands == active_commands &&
           observer->active_actions == active_actions &&
           observer->ready_count == ready_count &&
           observer->cleanup_count == cleanup_count &&
           credit_count[0] == active_commands &&
           observer->reserved_intent_credits == credit_count[1] &&
           observer->reserved_ready_credits == credit_count[2] &&
           observer->reserved_lease_credits == credit_count[3] &&
           observer->reserved_consume_credits == credit_count[4] &&
           observer->reserved_finalizer_credits == credit_count[5] &&
           observer->reserved_abort_credits == credit_count[6] &&
           observer->reserved_target_credits == credit_count[7] &&
           observer->reserved_queue_transaction_credits == credit_count[8] &&
           observer->reserved_block_intent_credits == credit_count[9];
}

static int queue_command_kind(uint8_t kind)
{
    return kind >= FWLAB_C43_REQUEST_CREATE_IO_CQ &&
           kind <= FWLAB_C43_REQUEST_DELETE_IO_SQ;
}

static uint32_t queue_operation(uint8_t kind)
{
    switch (kind) {
    case FWLAB_C43_REQUEST_CREATE_IO_CQ:
        return FWLAB_C43_QUEUE_CREATE_CQ;
    case FWLAB_C43_REQUEST_CREATE_IO_SQ:
        return FWLAB_C43_QUEUE_CREATE_SQ;
    case FWLAB_C43_REQUEST_DELETE_IO_CQ:
        return FWLAB_C43_QUEUE_DELETE_CQ;
    default:
        return FWLAB_C43_QUEUE_DELETE_SQ;
    }
}

static uint32_t queue_role(uint32_t operation)
{
    return operation == FWLAB_C43_QUEUE_CREATE_CQ ||
                   operation == FWLAB_C43_QUEUE_DELETE_CQ
               ? FWLAB_C43_QUEUE_ROLE_IO_CQ
               : FWLAB_C43_QUEUE_ROLE_IO_SQ;
}

static uint32_t queue_public_action_state(uint32_t flow)
{
    switch (flow) {
    case C43_QUEUE_FLOW_PREPARE_START:
        return FWLAB_C43_ACTION_STATE_SUBMIT_READY;
    case C43_QUEUE_FLOW_PREPARE_QUERY:
        return FWLAB_C43_ACTION_STATE_PREPARE_QUERY;
    case C43_QUEUE_FLOW_DECIDE:
        return FWLAB_C43_ACTION_STATE_DECIDE;
    case C43_QUEUE_FLOW_FINISH_START:
        return FWLAB_C43_ACTION_STATE_FINISH_READY;
    case C43_QUEUE_FLOW_FINISH_QUERY:
        return FWLAB_C43_ACTION_STATE_FINISH_QUERY;
    case C43_QUEUE_FLOW_APPLY_COMMIT:
        return FWLAB_C43_ACTION_STATE_APPLY_COMMIT;
    case C43_QUEUE_FLOW_APPLY_ABORT:
        return FWLAB_C43_ACTION_STATE_APPLY_ABORT;
    case C43_QUEUE_FLOW_RETIRE:
        return FWLAB_C43_ACTION_STATE_RETIRE;
    case C43_QUEUE_FLOW_DONE:
        return FWLAB_C43_ACTION_STATE_TERMINAL_HELD;
    case C43_QUEUE_FLOW_REJECTED_NO_EFFECT:
        return FWLAB_C43_ACTION_STATE_REJECTED_NO_EFFECT;
    case C43_QUEUE_FLOW_FAULT:
        return FWLAB_C43_ACTION_STATE_FAULT;
    default:
        return FWLAB_C43_ACTION_STATE_NONE;
    }
}

static int queue_authority_valid(
    const struct fwlab_c43_graph *graph,
    const struct c43_queue_authority *authority)
{
    const struct fwlab_c43_graph_observer *observer = &graph->observer;

    if (authority->nq_state > FWLAB_C43_NQ_NEGOTIATED ||
        authority->txn_active > 1 || authority->io_cq_present > 1 ||
        authority->io_sq_present > 1 ||
        !c43_bytes_zero(authority->reserved0,
                        sizeof(authority->reserved0)) ||
        !c43_bytes_zero(authority->reserved1,
                        sizeof(authority->reserved1)) ||
        (authority->txn_active == 0) !=
            (authority->owner_slot_plus_one == 0) ||
        authority->owner_slot_plus_one > FWLAB_C43_MAX_COMMANDS ||
        authority->io_cq_present != queue_ref_nonzero(&authority->io_cq) ||
        authority->io_sq_present != queue_ref_nonzero(&authority->io_sq) ||
        ((authority->io_cq_present || authority->io_sq_present) &&
         authority->nq_state != FWLAB_C43_NQ_NEGOTIATED) ||
        (!authority->io_sq_present &&
         queue_ref_nonzero(&authority->sq_associated_cq)) ||
        (authority->io_sq_present &&
         (!authority->io_cq_present ||
          !queue_ref_equal(&authority->sq_associated_cq,
                           &authority->io_cq)))) {
        return 0;
    }
    return observer->nq_state == authority->nq_state &&
           observer->queue_txn_active == authority->txn_active &&
           observer->queue_owner_slot_plus_one ==
               authority->owner_slot_plus_one &&
           observer->io_cq_present == authority->io_cq_present &&
           observer->io_sq_present == authority->io_sq_present &&
           queue_ref_equal(&observer->io_cq, &authority->io_cq) &&
           queue_ref_equal(&observer->io_sq, &authority->io_sq) &&
           queue_ref_equal(&observer->sq_associated_cq,
                           &authority->sq_associated_cq);
}

static int queue_token_matches_record(
    const struct fwlab_hif_action_token *token,
    const struct c43_command_record *record)
{
    return c43_handle_equal(&token->command, &record->ticket.handle) &&
           c43_origin_equal(&token->origin, &record->ticket.origin) &&
           token->action_uid == record->actions[0].action_uid &&
           token->generation == record->actions[0].generation &&
           token->kind == FWLAB_HIF_ACTION_QUEUE_EFFECT &&
           token->reserved == 0;
}

static int queue_txn_ref_equal(
    const struct fwlab_c43_queue_txn_ref *left,
    const struct fwlab_c43_queue_txn_ref *right)
{
    return left->word[0] == right->word[0] &&
           left->word[1] == right->word[1];
}

static int queue_facts_immutable_equal(
    const struct fwlab_c43_queue_facts *prepared,
    const struct fwlab_c43_queue_facts *terminal)
{
    return queue_txn_ref_equal(&prepared->transaction,
                               &terminal->transaction) &&
           queue_ref_equal(&prepared->queue, &terminal->queue) &&
           queue_ref_equal(&prepared->associated_cq,
                           &terminal->associated_cq) &&
           prepared->operation == terminal->operation &&
           prepared->role == terminal->role &&
           prepared->queue_entries == terminal->queue_entries &&
           prepared->address_present == terminal->address_present;
}

static int queue_flow_requires_owner(uint32_t flow)
{
    return (flow >= C43_QUEUE_FLOW_PREPARE_START &&
            flow <= C43_QUEUE_FLOW_RETIRE) ||
           flow == C43_QUEUE_FLOW_FAULT;
}

static int queue_terminal_is_commit(
    const struct c43_queue_txn_record *queue)
{
    return (queue->terminal.state == FWLAB_C43_QUEUE_EFFECT_COMMITTED &&
            queue->decision == FWLAB_C43_QUEUE_FINISH_COMMIT) ||
           (queue->terminal.state == FWLAB_C43_QUEUE_EFFECT_TOO_LATE &&
            queue->decision == FWLAB_C43_QUEUE_FINISH_ABORT);
}

static int queue_regular_request_valid(
    const struct fwlab_c43_graph *graph,
    const struct c43_command_record *record)
{
    const struct c43_queue_txn_record *queue = &record->queue_txn;

    return queue_command_kind(record->request.kind) &&
           queue->provider_generation == graph->providers.queue.generation &&
           fwlab_c43_queue_effect_request_valid(&queue->prepare_request) &&
           queue_token_matches_record(&queue->prepare_request.common.token,
                                      record) &&
           queue->prepare_request.common.cookie == record->transaction_uid &&
           queue->prepare_request.operation ==
               queue_operation(record->request.kind);
}

static int queue_record_valid(
    const struct fwlab_c43_graph *graph,
    const struct c43_command_record *record,
    const struct fwlab_c43_command_observer *observer)
{
    const struct c43_queue_txn_record *queue = &record->queue_txn;
    const int prepared_zero = c43_bytes_zero(
        &queue->prepared_facts, sizeof(queue->prepared_facts));
    const int finish_zero = c43_bytes_zero(
        &queue->finish_request, sizeof(queue->finish_request));
    const int terminal_zero = c43_bytes_zero(
        &queue->terminal, sizeof(queue->terminal));
    int terminal_valid;

    if (queue->flow == C43_QUEUE_FLOW_NONE) {
        return c43_bytes_zero(queue, sizeof(*queue)) &&
               observer->action_domain == FWLAB_C43_ACTION_DOMAIN_NONE &&
               observer->action_state == FWLAB_C43_ACTION_STATE_NONE &&
               observer->resolution_valid == 0 &&
               observer->resolved_status == 0;
    }
    if (queue->flow > C43_QUEUE_FLOW_FAULT || queue->decision >
            FWLAB_C43_QUEUE_FINISH_ABORT || queue->resolution_valid > 1 ||
        queue->local_effect_applied > 1 || queue->provider_owned > 1 ||
        !c43_bytes_zero(queue->reserved1, sizeof(queue->reserved1)) ||
        (queue->flow != C43_QUEUE_FLOW_FAULT &&
         queue->fault_from_flow != C43_QUEUE_FLOW_NONE) ||
        (queue->flow == C43_QUEUE_FLOW_FAULT &&
         (queue->fault_from_flow < C43_QUEUE_FLOW_PREPARE_START ||
          queue->fault_from_flow > C43_QUEUE_FLOW_RETIRE)) ||
        (!queue->resolution_valid && queue->resolved_status != 0) ||
        (queue->resolution_valid &&
         !c43_semantic_status_valid(queue->resolved_status)) ||
        observer->action_domain != FWLAB_C43_ACTION_DOMAIN_QUEUE ||
        observer->action_state != queue_public_action_state(queue->flow) ||
        observer->resolution_valid != queue->resolution_valid ||
        observer->resolved_status != queue->resolved_status) {
        return 0;
    }
    if (record->request.kind == FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES) {
        return queue->flow == C43_QUEUE_FLOW_DONE &&
               queue->decision == 0 && queue->resolution_valid &&
               !queue->local_effect_applied && !queue->provider_owned &&
               queue->provider_generation == 0 &&
               (queue->resolved_status == record->plan.semantic_status ||
                (record->plan.semantic_status == FWLAB_C43_STATUS_SUCCESS &&
                 queue->resolved_status ==
                     FWLAB_C43_STATUS_COMMAND_SEQUENCE)) &&
               c43_bytes_zero(&queue->prepare_request,
                               sizeof(queue->prepare_request)) &&
               prepared_zero && finish_zero && terminal_zero;
    }
    if (queue->flow == C43_QUEUE_FLOW_REJECTED_NO_EFFECT) {
        const int no_prepare = queue->provider_generation == 0 &&
            c43_bytes_zero(&queue->prepare_request,
                           sizeof(queue->prepare_request));

        return queue_command_kind(record->request.kind) &&
               queue->resolution_valid && !queue->provider_owned &&
               !queue->local_effect_applied && queue->decision == 0 &&
               queue->resolved_status != FWLAB_C43_STATUS_SUCCESS &&
               prepared_zero && finish_zero && terminal_zero &&
               (no_prepare || queue_regular_request_valid(graph, record));
    }
    if (!queue_regular_request_valid(graph, record) ||
        queue->local_effect_applied !=
            ((observer->satisfied_witness_mask &
              FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED) != 0)) {
        return 0;
    }
    if (!finish_zero &&
        (!fwlab_c43_queue_finish_request_valid(&queue->finish_request) ||
         !c43_action_token_equal(&queue->finish_request.token,
                                 &queue->prepare_request.common.token) ||
         queue->finish_request.decision != queue->decision)) {
        return 0;
    }
    if (!prepared_zero &&
        !fwlab_c43_queue_facts_valid(&queue->prepared_facts)) {
        return 0;
    }
    terminal_valid = !terminal_zero &&
        fwlab_c43_queue_effect_terminal_valid(&queue->terminal);
    if (!terminal_zero &&
        (!terminal_valid ||
         !queue_facts_immutable_equal(&queue->prepared_facts,
                                      &queue->terminal.facts))) {
        return 0;
    }
    switch (queue->flow) {
    case C43_QUEUE_FLOW_PREPARE_START:
        return queue->decision == 0 && !queue->resolution_valid &&
               !queue->provider_owned && !queue->local_effect_applied &&
               prepared_zero && finish_zero && terminal_zero;
    case C43_QUEUE_FLOW_PREPARE_QUERY:
        return queue->decision == 0 && !queue->resolution_valid &&
               queue->provider_owned && !queue->local_effect_applied &&
               prepared_zero && finish_zero && terminal_zero;
    case C43_QUEUE_FLOW_DECIDE:
        return queue->decision == 0 && !queue->resolution_valid &&
               queue->provider_owned && !queue->local_effect_applied &&
               !prepared_zero && finish_zero && terminal_zero;
    case C43_QUEUE_FLOW_FINISH_START:
    case C43_QUEUE_FLOW_FINISH_QUERY:
        return queue->decision >= FWLAB_C43_QUEUE_FINISH_COMMIT &&
               queue->resolution_valid && queue->provider_owned &&
               !queue->local_effect_applied && !prepared_zero &&
               !finish_zero && terminal_zero;
    case C43_QUEUE_FLOW_APPLY_COMMIT:
        return queue->resolution_valid && queue->provider_owned &&
               !queue->local_effect_applied && !prepared_zero &&
               !finish_zero && terminal_valid &&
               queue_terminal_is_commit(queue) &&
               queue->resolved_status == FWLAB_C43_STATUS_SUCCESS;
    case C43_QUEUE_FLOW_APPLY_ABORT:
        return queue->decision == FWLAB_C43_QUEUE_FINISH_ABORT &&
               queue->resolution_valid && queue->provider_owned &&
               !queue->local_effect_applied && !prepared_zero &&
               !finish_zero && terminal_valid &&
               queue->terminal.state == FWLAB_C43_QUEUE_EFFECT_ABORTED &&
               queue->resolved_status != FWLAB_C43_STATUS_SUCCESS;
    case C43_QUEUE_FLOW_RETIRE:
    case C43_QUEUE_FLOW_DONE:
        return queue->resolution_valid &&
               queue->provider_owned ==
                   (queue->flow == C43_QUEUE_FLOW_RETIRE) &&
               !prepared_zero && !finish_zero && terminal_valid &&
               ((queue_terminal_is_commit(queue) &&
                 queue->local_effect_applied &&
                 queue->resolved_status == FWLAB_C43_STATUS_SUCCESS) ||
                (queue->terminal.state == FWLAB_C43_QUEUE_EFFECT_ABORTED &&
                 !queue->local_effect_applied &&
                 queue->resolved_status != FWLAB_C43_STATUS_SUCCESS));
    case C43_QUEUE_FLOW_FAULT:
        if (!queue->resolution_valid ||
            queue->resolved_status != FWLAB_C43_STATUS_INTERNAL_FAILURE ||
            (queue->fault_from_flow != C43_QUEUE_FLOW_PREPARE_START &&
             !queue->provider_owned) ||
            (queue->local_effect_applied &&
             (queue->fault_from_flow != C43_QUEUE_FLOW_RETIRE ||
              !terminal_valid || !queue_terminal_is_commit(queue)))) {
            return 0;
        }
        if (queue->fault_from_flow <= C43_QUEUE_FLOW_PREPARE_QUERY) {
            return queue->decision == 0 && prepared_zero && finish_zero &&
                   terminal_zero;
        }
        if (queue->fault_from_flow == C43_QUEUE_FLOW_DECIDE) {
            return !prepared_zero && finish_zero && terminal_zero;
        }
        if (queue->fault_from_flow <= C43_QUEUE_FLOW_FINISH_QUERY) {
            return !prepared_zero && !finish_zero &&
                   (terminal_zero || terminal_valid);
        }
        return !prepared_zero && !finish_zero && terminal_valid;
    default:
        return 0;
    }
}

_Static_assert(sizeof(struct c43_queue_txn_record) ==
                   sizeof(struct c43_target_txn_record),
               "target action storage must cover the full union");
_Static_assert(sizeof(struct c43_queue_txn_record) ==
                   sizeof(struct c43_block_txn_record),
               "block action storage must cover the full union");

static uint32_t target_public_action_state(uint32_t flow)
{
    switch (flow) {
    case C43_TARGET_FLOW_SUBMIT_START:
        return FWLAB_C43_ACTION_STATE_SUBMIT_READY;
    case C43_TARGET_FLOW_QUERY:
        return FWLAB_C43_ACTION_STATE_ACCEPTED_WAIT;
    case C43_TARGET_FLOW_DONE:
        return FWLAB_C43_ACTION_STATE_TERMINAL_HELD;
    case C43_TARGET_FLOW_FAULT:
        return FWLAB_C43_ACTION_STATE_FAULT;
    default:
        return FWLAB_C43_ACTION_STATE_NONE;
    }
}

static int target_find_slot(
    const struct fwlab_c43_graph *graph,
    uint32_t abort_slot,
    const struct fwlab_hif_command_ticket *ticket,
    uint32_t *target_slot)
{
    uint32_t slot;
    int found = 0;

    for (slot = 0; slot < FWLAB_C43_MAX_COMMANDS; ++slot) {
        if (slot != abort_slot && graph->commands[slot].in_use &&
            graph->commands[slot].state == C43_COMMAND_RECORD_ADMITTED &&
            command_ticket_equal(ticket, &graph->commands[slot].ticket)) {
            if (found) {
                return 0;
            }
            *target_slot = slot;
            found = 1;
        }
    }
    return found;
}

static int target_record_valid(
    const struct fwlab_c43_graph *graph,
    uint32_t slot,
    const struct c43_command_record *record,
    const struct fwlab_c43_command_observer *observer)
{
    const struct c43_target_txn_record *target = &record->target_txn;
    const int terminal_zero = c43_bytes_zero(
        &target->terminal, sizeof(target->terminal));
    uint32_t target_slot = 0;

    if (target->flow == C43_TARGET_FLOW_NONE) {
        return c43_bytes_zero(&record->queue_txn,
                              sizeof(record->queue_txn)) &&
               observer->action_domain == FWLAB_C43_ACTION_DOMAIN_NONE &&
               observer->action_state == FWLAB_C43_ACTION_STATE_NONE &&
               observer->resolution_valid == 0 &&
               observer->resolved_status == 0;
    }
    if (record->request.kind != FWLAB_C43_REQUEST_ABORT ||
        record->plan.kind != FWLAB_C43_PLAN_ABORT_RESOLVE ||
        target->flow > C43_TARGET_FLOW_FAULT ||
        target->outcome > FWLAB_C43_TARGET_FAULT ||
        target->resolution_valid > 1 || target->provider_owned > 1 ||
        target->pin_installed > 1 || target->reserved0 != 0 ||
        target->reserved1 != 0 ||
        !c43_bytes_zero(target->reserved2, sizeof(target->reserved2)) ||
        !c43_bytes_zero(target->reserved_tail,
                        sizeof(target->reserved_tail)) ||
        (target->flow != C43_TARGET_FLOW_FAULT &&
         target->fault_from_flow != C43_TARGET_FLOW_NONE) ||
        (target->flow == C43_TARGET_FLOW_FAULT &&
         (target->fault_from_flow < C43_TARGET_FLOW_SUBMIT_START ||
          target->fault_from_flow > C43_TARGET_FLOW_QUERY)) ||
        (!target->resolution_valid && target->resolved_status != 0) ||
        (target->resolution_valid &&
         !c43_semantic_status_valid(target->resolved_status)) ||
        target->provider_generation != graph->providers.target.generation ||
        !fwlab_c43_target_request_valid(&target->request) ||
        !queue_token_matches_record(&target->request.common.token, record) ||
        target->request.common.cookie != record->transaction_uid ||
        target->request.common.dependency_ordinal != UINT32_MAX ||
        !command_ticket_equal(&target->request.abort_command,
                              &record->ticket) ||
        observer->action_domain != FWLAB_C43_ACTION_DOMAIN_TARGET ||
        observer->action_state != target_public_action_state(target->flow) ||
        observer->resolution_valid != target->resolution_valid ||
        observer->resolved_status != target->resolved_status ||
        observer->action_outcome != target->outcome ||
        observer->target_pin_present != target->pin_installed) {
        return 0;
    }
    if (target->pin_installed) {
        if (target->target_slot_plus_one == 0 ||
            target->target_slot_plus_one > FWLAB_C43_MAX_COMMANDS ||
            terminal_zero ||
            !command_ticket_equal(&observer->target_ticket,
                                  &target->terminal.target) ||
            observer->target_reference.word[0] !=
                target->terminal.reference.word[0] ||
            observer->target_reference.word[1] !=
                target->terminal.reference.word[1]) {
            return 0;
        }
    } else if (target->target_slot_plus_one != 0 ||
               !c43_bytes_zero(&observer->target_ticket,
                               sizeof(observer->target_ticket)) ||
               target_ref_nonzero(&observer->target_reference)) {
        return 0;
    }
    switch (target->flow) {
    case C43_TARGET_FLOW_SUBMIT_START:
        return !target->resolution_valid && !target->provider_owned &&
               !target->pin_installed && target->outcome == 0 &&
               terminal_zero;
    case C43_TARGET_FLOW_QUERY:
        return !target->resolution_valid && target->provider_owned &&
               !target->pin_installed && target->outcome == 0 &&
               terminal_zero;
    case C43_TARGET_FLOW_DONE:
        if (!target->resolution_valid || !target->provider_owned ||
            target->resolved_status != FWLAB_C43_STATUS_SUCCESS ||
            !fwlab_c43_target_terminal_matches_request(
                &target->request, &target->terminal) ||
            target->outcome != target->terminal.outcome ||
            target->outcome < FWLAB_C43_TARGET_FOUND ||
            target->outcome > FWLAB_C43_TARGET_SUPERSEDED) {
            return 0;
        }
        if (target->outcome != FWLAB_C43_TARGET_FOUND) {
            return !target->pin_installed;
        }
        return target->pin_installed &&
               target_find_slot(graph, slot, &target->terminal.target,
                                &target_slot) &&
               target->target_slot_plus_one == target_slot + 1 &&
               graph->commands[target_slot].incoming_target_pins == 1;
    case C43_TARGET_FLOW_FAULT:
        return target->resolution_valid &&
               target->resolved_status == FWLAB_C43_STATUS_INTERNAL_FAILURE &&
               !target->pin_installed &&
               (target->fault_from_flow == C43_TARGET_FLOW_SUBMIT_START ||
                target->provider_owned) &&
               ((terminal_zero && target->outcome == 0) ||
                (fwlab_c43_target_terminal_matches_request(
                     &target->request, &target->terminal) &&
                 target->terminal.outcome == FWLAB_C43_TARGET_FAULT &&
                 target->outcome == FWLAB_C43_TARGET_FAULT));
    default:
        return 0;
    }
}

static uint32_t block_public_action_state(uint32_t flow)
{
    switch (flow) {
    case C43_BLOCK_FLOW_SUBMIT_START:
        return FWLAB_C43_ACTION_STATE_SUBMIT_READY;
    case C43_BLOCK_FLOW_QUERY:
        return FWLAB_C43_ACTION_STATE_ACCEPTED_WAIT;
    case C43_BLOCK_FLOW_DONE:
        return FWLAB_C43_ACTION_STATE_TERMINAL_HELD;
    case C43_BLOCK_FLOW_REJECTED_NO_EFFECT:
        return FWLAB_C43_ACTION_STATE_REJECTED_NO_EFFECT;
    case C43_BLOCK_FLOW_FAULT:
        return FWLAB_C43_ACTION_STATE_FAULT;
    default:
        return FWLAB_C43_ACTION_STATE_NONE;
    }
}

static int block_token_matches_record(
    const struct fwlab_hif_action_token *token,
    const struct c43_command_record *record)
{
    return c43_handle_equal(&token->command, &record->ticket.handle) &&
           c43_origin_equal(&token->origin, &record->ticket.origin) &&
           token->action_uid == record->actions[0].action_uid &&
           token->generation == record->actions[0].generation &&
           token->kind == FWLAB_HIF_ACTION_BLOCK && token->reserved == 0;
}

static int block_record_valid(
    const struct fwlab_c43_graph *graph,
    const struct c43_command_record *record,
    const struct fwlab_c43_command_observer *observer)
{
    const struct c43_block_txn_record *block = &record->block_txn;
    const int terminal_zero = c43_bytes_zero(
        &block->terminal, sizeof(block->terminal));

    if (block->flow == C43_BLOCK_FLOW_NONE) {
        return c43_bytes_zero(&record->queue_txn,
                              sizeof(record->queue_txn)) &&
               observer->action_domain == FWLAB_C43_ACTION_DOMAIN_NONE &&
               observer->action_state == FWLAB_C43_ACTION_STATE_NONE &&
               observer->resolution_valid == 0 &&
               observer->resolved_status == 0;
    }
    if (record->plan.kind != FWLAB_C43_PLAN_BLOCK ||
        block->flow > C43_BLOCK_FLOW_FAULT ||
        block->terminal_kind > FWLAB_C43_BLOCK_COMPLETED ||
        block->resolution_valid > 1 || block->provider_owned > 1 ||
        block->reserved0 != 0 || block->reserved1 != 0 ||
        block->reserved_alignment != 0 ||
        !c43_bytes_zero(block->reserved2, sizeof(block->reserved2)) ||
        !c43_bytes_zero(block->reserved_tail,
                        sizeof(block->reserved_tail)) ||
        (block->flow != C43_BLOCK_FLOW_FAULT &&
         block->fault_from_flow != C43_BLOCK_FLOW_NONE) ||
        (block->flow == C43_BLOCK_FLOW_FAULT &&
         (block->fault_from_flow < C43_BLOCK_FLOW_SUBMIT_START ||
          block->fault_from_flow > C43_BLOCK_FLOW_QUERY)) ||
        (!block->resolution_valid && block->resolved_status != 0) ||
        (block->resolution_valid &&
         !c43_semantic_status_valid(block->resolved_status)) ||
        block->provider_generation != graph->providers.block.generation ||
        !fwlab_c43_block_action_request_valid_for_port(
            &block->request, &graph->providers.block) ||
        !block_token_matches_record(&block->request.common.token, record) ||
        block->request.common.cookie != record->transaction_uid ||
        block->request.common.dependency_ordinal != UINT32_MAX ||
        block->request.requested_witness_mask !=
            FWLAB_C43_WITNESS_VALIDATED_ONLY ||
        !c43_ref_zero(&block->request.predecessor) ||
        memcmp(&block->request.intent, &record->plan.block,
               sizeof(block->request.intent)) != 0 ||
        observer->action_domain != FWLAB_C43_ACTION_DOMAIN_BLOCK ||
        observer->action_state != block_public_action_state(block->flow) ||
        observer->resolution_valid != block->resolution_valid ||
        observer->resolved_status != block->resolved_status ||
        observer->block_terminal_kind != block->terminal_kind) {
        return 0;
    }
    switch (block->flow) {
    case C43_BLOCK_FLOW_SUBMIT_START:
        return !block->resolution_valid && !block->provider_owned &&
               block->terminal_kind == 0 && terminal_zero;
    case C43_BLOCK_FLOW_QUERY:
        return !block->resolution_valid && block->provider_owned &&
               block->terminal_kind == 0 && terminal_zero;
    case C43_BLOCK_FLOW_DONE:
        if (!block->resolution_valid || !block->provider_owned ||
            !fwlab_c43_block_action_terminal_matches_request(
                &block->request, &block->terminal,
                &graph->providers.block) ||
            block->terminal_kind != block->terminal.block_terminal_kind) {
            return 0;
        }
        if (block->terminal_kind == FWLAB_C43_BLOCK_VALIDATED_ONLY) {
            return block->resolved_status == FWLAB_C43_STATUS_SUCCESS &&
                   observer->satisfied_witness_mask ==
                       FWLAB_C43_WITNESS_VALIDATED_ONLY;
        }
        return block->terminal_kind ==
                   FWLAB_C43_BLOCK_FAILED_NO_EFFECT &&
               block->resolved_status == FWLAB_C43_STATUS_MEDIA_FAILURE &&
               observer->satisfied_witness_mask == 0;
    case C43_BLOCK_FLOW_REJECTED_NO_EFFECT:
        return block->resolution_valid && !block->provider_owned &&
               block->resolved_status == FWLAB_C43_STATUS_RESOURCE_FAILURE &&
               block->terminal_kind == 0 && terminal_zero &&
               observer->satisfied_witness_mask == 0;
    case C43_BLOCK_FLOW_FAULT:
        return block->resolution_valid &&
               block->resolved_status == FWLAB_C43_STATUS_INTERNAL_FAILURE &&
               block->terminal_kind == 0 && terminal_zero &&
               observer->satisfied_witness_mask == 0 &&
               (block->fault_from_flow == C43_BLOCK_FLOW_SUBMIT_START ||
                block->provider_owned);
    default:
        return 0;
    }
}

int c43_phase4_state_valid(const struct fwlab_c43_graph *graph)
{
    uint32_t index;
    uint32_t owner_count = 0;
    uint32_t owner_slot_plus_one = 0;
    uint16_t expected_incoming[FWLAB_C43_MAX_COMMANDS] = {0};

    if (graph == NULL || graph->next_service_slot >= FWLAB_C43_MAX_COMMANDS ||
        !c43_bytes_zero(graph->reserved_scheduler,
                        sizeof(graph->reserved_scheduler)) ||
        !queue_authority_valid(graph, &graph->queue_authority)) {
        return 0;
    }
    for (index = 0; index < FWLAB_C43_MAX_COMMANDS; ++index) {
        const struct c43_command_record *record = &graph->commands[index];

        if (!record->in_use) {
            if (!c43_bytes_zero(&record->queue_txn,
                                sizeof(record->queue_txn))) {
                return 0;
            }
            continue;
        }
        if (record->state == C43_COMMAND_RECORD_ADMITTED &&
            record->plan.kind == FWLAB_C43_PLAN_ABORT_RESOLVE) {
            if (!target_record_valid(graph, index, record,
                                     &graph->observer.commands[index])) {
                return 0;
            }
            if (record->target_txn.flow == C43_TARGET_FLOW_DONE &&
                record->target_txn.outcome == FWLAB_C43_TARGET_FOUND) {
                const uint32_t target =
                    record->target_txn.target_slot_plus_one - 1;

                if (target >= FWLAB_C43_MAX_COMMANDS ||
                    expected_incoming[target] != 0) {
                    return 0;
                }
                expected_incoming[target] = 1;
            }
        } else if (record->state == C43_COMMAND_RECORD_ADMITTED &&
                   record->plan.kind == FWLAB_C43_PLAN_BLOCK) {
            if (!block_record_valid(graph, record,
                                    &graph->observer.commands[index])) {
                return 0;
            }
        } else {
            if (!queue_record_valid(graph, record,
                                    &graph->observer.commands[index])) {
                return 0;
            }
            if (queue_flow_requires_owner(record->queue_txn.flow)) {
                ++owner_count;
                owner_slot_plus_one = index + 1;
            }
        }
    }
    for (index = 0; index < FWLAB_C43_MAX_COMMANDS; ++index) {
        if (graph->commands[index].incoming_target_pins !=
            expected_incoming[index]) {
            return 0;
        }
    }
    return owner_count <= 1 &&
           graph->queue_authority.txn_active == (owner_count == 1) &&
           (owner_count == 0 ||
            graph->queue_authority.owner_slot_plus_one ==
                owner_slot_plus_one);
}

static void queue_authority_sync(struct fwlab_c43_graph *graph)
{
    const struct c43_queue_authority *authority = &graph->queue_authority;
    struct fwlab_c43_graph_observer *observer = &graph->observer;

    observer->nq_state = authority->nq_state;
    observer->queue_txn_active = authority->txn_active;
    observer->queue_owner_slot_plus_one = authority->owner_slot_plus_one;
    observer->io_cq_present = authority->io_cq_present;
    observer->io_sq_present = authority->io_sq_present;
    observer->io_cq = authority->io_cq;
    observer->io_sq = authority->io_sq;
    observer->sq_associated_cq = authority->sq_associated_cq;
}

static void queue_record_sync(
    struct c43_command_record *record,
    struct fwlab_c43_command_observer *observer)
{
    observer->action_domain = FWLAB_C43_ACTION_DOMAIN_QUEUE;
    observer->action_state =
        (uint8_t)queue_public_action_state(record->queue_txn.flow);
    observer->resolution_valid = record->queue_txn.resolution_valid;
    observer->resolved_status = record->queue_txn.resolved_status;
}

static void queue_release_owner(
    struct fwlab_c43_graph *graph,
    uint32_t slot)
{
    if (graph->queue_authority.txn_active &&
        graph->queue_authority.owner_slot_plus_one == slot + 1) {
        graph->queue_authority.txn_active = 0;
        graph->queue_authority.owner_slot_plus_one = 0;
        queue_authority_sync(graph);
    }
}

static void queue_resolve_no_effect(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t status)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_queue_txn_record *queue = &record->queue_txn;
    struct fwlab_c43_command_observer *observer =
        &graph->observer.commands[slot];

    queue->flow = C43_QUEUE_FLOW_REJECTED_NO_EFFECT;
    queue->resolution_valid = 1;
    queue->resolved_status = status;
    queue->fault_from_flow = C43_QUEUE_FLOW_NONE;
    queue->provider_owned = 0;
    observer->phase = FWLAB_C43_PHASE_ACTION_WAIT;
    observer->success_eligible = 0;
    queue_record_sync(record, observer);
    queue_release_owner(graph, slot);
}

static void queue_fault(
    struct fwlab_c43_graph *graph,
    uint32_t slot)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_queue_txn_record *queue = &record->queue_txn;
    struct fwlab_c43_command_observer *observer =
        &graph->observer.commands[slot];

    if (queue->flow != C43_QUEUE_FLOW_FAULT) {
        queue->fault_from_flow = (uint8_t)queue->flow;
    }
    queue->flow = C43_QUEUE_FLOW_FAULT;
    queue->resolution_valid = 1;
    queue->resolved_status = FWLAB_C43_STATUS_INTERNAL_FAILURE;
    observer->phase = FWLAB_C43_PHASE_ACTION_WAIT;
    observer->success_eligible = 0;
    queue_record_sync(record, observer);
}

static void queue_action_token(
    const struct c43_command_record *record,
    struct fwlab_hif_action_token *token)
{
    memset(token, 0, sizeof(*token));
    token->command = record->ticket.handle;
    token->origin = record->ticket.origin;
    token->action_uid = record->actions[0].action_uid;
    token->generation = record->actions[0].generation;
    token->kind = FWLAB_HIF_ACTION_QUEUE_EFFECT;
}

static int queue_prepare_request_build(
    const struct c43_command_record *record,
    struct fwlab_c43_queue_effect_request *request)
{
    memset(request, 0, sizeof(*request));
    request->version = FWLAB_C43_QUEUE_EFFECT_PORT_VERSION;
    request->size = sizeof(*request);
    request->common.version = FWLAB_HIF_ACTION_VERSION;
    request->common.size = sizeof(request->common);
    queue_action_token(record, &request->common.token);
    request->common.cookie = record->transaction_uid;
    request->common.requested_units = 1;
    request->operation = queue_operation(record->request.kind);
    request->role = queue_role(request->operation);
    return fwlab_c43_queue_effect_request_valid(request);
}

static uint32_t queue_precondition_status(
    const struct fwlab_c43_graph *graph,
    const struct c43_command_record *record)
{
    const struct c43_queue_authority *authority = &graph->queue_authority;

    if (record->plan.semantic_status != FWLAB_C43_STATUS_SUCCESS) {
        return record->plan.semantic_status;
    }
    switch (record->request.kind) {
    case FWLAB_C43_REQUEST_CREATE_IO_CQ:
        if (authority->nq_state != FWLAB_C43_NQ_NEGOTIATED) {
            return FWLAB_C43_STATUS_COMMAND_SEQUENCE;
        }
        return authority->io_cq_present || authority->io_sq_present
                   ? FWLAB_C43_STATUS_INVALID_QUEUE
                   : FWLAB_C43_STATUS_SUCCESS;
    case FWLAB_C43_REQUEST_CREATE_IO_SQ:
        if (authority->nq_state != FWLAB_C43_NQ_NEGOTIATED) {
            return FWLAB_C43_STATUS_COMMAND_SEQUENCE;
        }
        return !authority->io_cq_present || authority->io_sq_present
                   ? FWLAB_C43_STATUS_INVALID_QUEUE
                   : FWLAB_C43_STATUS_SUCCESS;
    case FWLAB_C43_REQUEST_DELETE_IO_SQ:
        return authority->io_sq_present
                   ? FWLAB_C43_STATUS_SUCCESS
                   : FWLAB_C43_STATUS_INVALID_QUEUE_DELETE;
    case FWLAB_C43_REQUEST_DELETE_IO_CQ:
        return authority->io_cq_present && !authority->io_sq_present
                   ? FWLAB_C43_STATUS_SUCCESS
                   : FWLAB_C43_STATUS_INVALID_QUEUE_DELETE;
    default:
        return FWLAB_C43_STATUS_INTERNAL_FAILURE;
    }
}

static void queue_begin_nq(
    struct fwlab_c43_graph *graph,
    uint32_t slot)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_queue_txn_record *queue = &record->queue_txn;
    struct fwlab_c43_command_observer *observer =
        &graph->observer.commands[slot];
    uint32_t status = record->plan.semantic_status;

    if (status == FWLAB_C43_STATUS_SUCCESS &&
        (graph->queue_authority.txn_active ||
         graph->queue_authority.io_cq_present ||
         graph->queue_authority.io_sq_present)) {
        status = FWLAB_C43_STATUS_COMMAND_SEQUENCE;
    }
    if (status == FWLAB_C43_STATUS_SUCCESS) {
        graph->queue_authority.nq_state = FWLAB_C43_NQ_NEGOTIATED;
    }
    queue->flow = C43_QUEUE_FLOW_DONE;
    queue->resolution_valid = 1;
    queue->resolved_status = status;
    observer->phase = FWLAB_C43_PHASE_ACTION_WAIT;
    observer->success_eligible = status == FWLAB_C43_STATUS_SUCCESS;
    queue_record_sync(record, observer);
    queue_authority_sync(graph);
}

static void queue_begin_transaction(
    struct fwlab_c43_graph *graph,
    uint32_t slot)
{
    struct c43_command_record *record = &graph->commands[slot];
    const uint32_t status = queue_precondition_status(graph, record);

    if (status != FWLAB_C43_STATUS_SUCCESS) {
        queue_resolve_no_effect(graph, slot, status);
        return;
    }
    if (graph->queue_authority.txn_active) {
        return;
    }
    memset(&record->queue_txn, 0, sizeof(record->queue_txn));
    record->queue_txn.flow = C43_QUEUE_FLOW_PREPARE_START;
    record->queue_txn.provider_generation =
        graph->providers.queue.generation;
    graph->queue_authority.txn_active = 1;
    graph->queue_authority.owner_slot_plus_one = (uint16_t)(slot + 1);
    graph->observer.commands[slot].phase = FWLAB_C43_PHASE_RESOLVE_WAIT;
    queue_record_sync(record, &graph->observer.commands[slot]);
    queue_authority_sync(graph);
    if (!queue_prepare_request_build(record,
                                     &record->queue_txn.prepare_request)) {
        queue_fault(graph, slot);
        return;
    }
}

static int queue_submit_result_valid(
    const struct fwlab_hif_action_submit_result *result,
    const struct fwlab_hif_action_token *token)
{
    return fwlab_hif_action_submit_result_valid(result) &&
           result->disposition == FWLAB_HIF_ACTION_ACCEPTED &&
           c43_action_token_equal(&result->token, token) &&
           result->fault_domain == 0 && result->fault_code == 0 &&
           result->retry == FWLAB_HIF_ACTION_RETRY_NONE &&
           result->effect_class == FWLAB_NVME_EFFECT_NONE;
}

static void queue_prepare_start_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_queue_txn_record *queue = &record->queue_txn;
    struct fwlab_hif_action_submit_result result;
    enum fwlab_hif_action_disposition disposition;

    memset(&result, 0xa5, sizeof(result));
    disposition = graph->providers.queue.ops->prepare_start(
        graph->providers.queue.context, &queue->prepare_request, &result);
    if (disposition == FWLAB_HIF_ACTION_BACKPRESSURE) {
        return;
    }
    if (disposition == FWLAB_HIF_ACTION_REJECTED) {
        queue_resolve_no_effect(graph, slot,
                                FWLAB_C43_STATUS_RESOURCE_FAILURE);
        *transitions = 1;
        return;
    }
    if (disposition == FWLAB_HIF_ACTION_ACCEPTED) {
        queue->provider_owned = 1;
    }
    if (disposition != FWLAB_HIF_ACTION_ACCEPTED ||
        !queue_submit_result_valid(&result,
                                   &queue->prepare_request.common.token)) {
        queue_fault(graph, slot);
        *transitions = 1;
        return;
    }
    queue->flow = C43_QUEUE_FLOW_PREPARE_QUERY;
    queue_record_sync(record, &graph->observer.commands[slot]);
    *transitions = 1;
}

static int bytes_are(const void *value, size_t size, unsigned char byte)
{
    const unsigned char *bytes = value;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != byte) {
            return 0;
        }
    }
    return 1;
}

static void queue_prepare_query_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_queue_txn_record *queue = &record->queue_txn;
    struct fwlab_c43_queue_effect_terminal terminal;
    bool ready = true;
    enum fwlab_c43_api_result result;

    memset(&terminal, 0xa5, sizeof(terminal));
    result = graph->providers.queue.ops->prepare_query(
        graph->providers.queue.context, &queue->prepare_request.common.token,
        &terminal, &ready);
    if (result == FWLAB_C43_API_IN_PROGRESS) {
        if (!ready || !bytes_are(&terminal, sizeof(terminal), 0xa5)) {
            queue_fault(graph, slot);
            *transitions = 1;
        }
        return;
    }
    if (result != FWLAB_C43_API_OK) {
        queue_fault(graph, slot);
        *transitions = 1;
        return;
    }
    if (!ready) {
        if (!bytes_are(&terminal, sizeof(terminal), 0xa5)) {
            queue_fault(graph, slot);
            *transitions = 1;
        }
        return;
    }
    if (!fwlab_c43_queue_effect_terminal_matches_request(
            &queue->prepare_request, &terminal) ||
        terminal.state != FWLAB_C43_QUEUE_EFFECT_PREPARED ||
        terminal.decision != 0) {
        queue_fault(graph, slot);
        *transitions = 1;
        return;
    }
    queue->prepared_facts = terminal.facts;
    queue->flow = C43_QUEUE_FLOW_DECIDE;
    queue_record_sync(record, &graph->observer.commands[slot]);
    *transitions = 1;
}

static int queue_facts_commit_ready(
    const struct fwlab_c43_graph *graph,
    const struct c43_command_record *record,
    const struct fwlab_c43_queue_facts *facts,
    uint32_t *failure_status)
{
    const struct c43_queue_authority *authority = &graph->queue_authority;

    *failure_status = FWLAB_C43_STATUS_INVALID_QUEUE;
    switch (record->request.kind) {
    case FWLAB_C43_REQUEST_CREATE_IO_CQ:
        if (!facts->address_present) {
            *failure_status = FWLAB_C43_STATUS_INVALID_FIELD;
            return 0;
        }
        if (facts->queue_entries !=
            graph->config.profile.integration_queue_depth) {
            *failure_status = FWLAB_C43_STATUS_INVALID_QUEUE_SIZE;
            return 0;
        }
        return facts->queue_exists && !facts->associated_cq_exists &&
               !authority->io_cq_present && !authority->io_sq_present;
    case FWLAB_C43_REQUEST_CREATE_IO_SQ:
        if (!facts->address_present) {
            *failure_status = FWLAB_C43_STATUS_INVALID_FIELD;
            return 0;
        }
        if (facts->queue_entries !=
            graph->config.profile.integration_queue_depth) {
            *failure_status = FWLAB_C43_STATUS_INVALID_QUEUE_SIZE;
            return 0;
        }
        return facts->queue_exists && facts->associated_cq_exists &&
               facts->association_matches && facts->current_relation &&
               authority->io_cq_present && !authority->io_sq_present &&
               queue_ref_equal(&facts->associated_cq, &authority->io_cq);
    case FWLAB_C43_REQUEST_DELETE_IO_SQ:
        *failure_status = FWLAB_C43_STATUS_INVALID_QUEUE_DELETE;
        return authority->io_sq_present && authority->io_cq_present &&
               facts->queue_exists && facts->associated_cq_exists &&
               facts->association_matches && facts->current_relation &&
               queue_ref_equal(&facts->queue, &authority->io_sq) &&
               queue_ref_equal(&facts->associated_cq, &authority->io_cq) &&
               facts->active_commands_zero && facts->target_refs_zero &&
               facts->reserved_publications_zero;
    case FWLAB_C43_REQUEST_DELETE_IO_CQ:
        *failure_status = FWLAB_C43_STATUS_INVALID_QUEUE_DELETE;
        return authority->io_cq_present && !authority->io_sq_present &&
               facts->queue_exists && !facts->associated_cq_exists &&
               facts->current_relation &&
               queue_ref_equal(&facts->queue, &authority->io_cq) &&
               facts->reserved_publications_zero &&
               facts->unacked_completions_zero;
    default:
        *failure_status = FWLAB_C43_STATUS_INTERNAL_FAILURE;
        return 0;
    }
}

static int queue_facts_decision_commit(
    const struct fwlab_c43_graph *graph,
    const struct c43_command_record *record,
    const struct fwlab_c43_queue_facts *facts,
    uint32_t *failure_status)
{
    const struct c43_queue_authority *authority = &graph->queue_authority;

    if (record->request.kind == FWLAB_C43_REQUEST_DELETE_IO_SQ) {
        *failure_status = FWLAB_C43_STATUS_INVALID_QUEUE_DELETE;
        return authority->io_sq_present && authority->io_cq_present &&
               facts->queue_exists && facts->associated_cq_exists &&
               facts->association_matches && facts->current_relation &&
               queue_ref_equal(&facts->queue, &authority->io_sq) &&
               queue_ref_equal(&facts->associated_cq, &authority->io_cq);
    }
    if (record->request.kind == FWLAB_C43_REQUEST_DELETE_IO_CQ) {
        *failure_status = FWLAB_C43_STATUS_INVALID_QUEUE_DELETE;
        return authority->io_cq_present && !authority->io_sq_present &&
               facts->queue_exists && !facts->associated_cq_exists &&
               facts->current_relation &&
               queue_ref_equal(&facts->queue, &authority->io_cq);
    }
    return queue_facts_commit_ready(graph, record, facts, failure_status);
}

static void queue_decide_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_queue_txn_record *queue = &record->queue_txn;
    uint32_t failure_status;

    queue->resolution_valid = 1;
    if (queue_facts_decision_commit(graph, record, &queue->prepared_facts,
                                    &failure_status)) {
        queue->decision = FWLAB_C43_QUEUE_FINISH_COMMIT;
        queue->resolved_status = FWLAB_C43_STATUS_SUCCESS;
    } else {
        queue->decision = FWLAB_C43_QUEUE_FINISH_ABORT;
        queue->resolved_status = failure_status;
    }
    memset(&queue->finish_request, 0, sizeof(queue->finish_request));
    queue->finish_request.version = FWLAB_C43_QUEUE_EFFECT_PORT_VERSION;
    queue->finish_request.size = sizeof(queue->finish_request);
    queue->finish_request.token = queue->prepare_request.common.token;
    queue->finish_request.transaction = queue->prepared_facts.transaction;
    queue->finish_request.decision = queue->decision;
    if (!fwlab_c43_queue_finish_request_valid(&queue->finish_request)) {
        memset(&queue->finish_request, 0, sizeof(queue->finish_request));
        queue_fault(graph, slot);
    } else {
        queue->flow = C43_QUEUE_FLOW_FINISH_START;
        queue_record_sync(record, &graph->observer.commands[slot]);
    }
    *transitions = 1;
}

static void queue_finish_start_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_queue_txn_record *queue = &record->queue_txn;
    struct fwlab_hif_action_submit_result result;
    enum fwlab_hif_action_disposition disposition;

    memset(&result, 0xa5, sizeof(result));
    disposition = graph->providers.queue.ops->finish_start(
        graph->providers.queue.context, &queue->finish_request, &result);
    if (disposition == FWLAB_HIF_ACTION_BACKPRESSURE) {
        return;
    }
    if (disposition != FWLAB_HIF_ACTION_ACCEPTED ||
        !queue_submit_result_valid(&result,
                                   &queue->finish_request.token)) {
        queue_fault(graph, slot);
        *transitions = 1;
        return;
    }
    queue->flow = C43_QUEUE_FLOW_FINISH_QUERY;
    queue_record_sync(record, &graph->observer.commands[slot]);
    *transitions = 1;
}

static void queue_finish_query_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_queue_txn_record *queue = &record->queue_txn;
    struct fwlab_c43_queue_effect_terminal terminal;
    bool ready = true;
    enum fwlab_c43_api_result result;

    memset(&terminal, 0xa5, sizeof(terminal));
    result = graph->providers.queue.ops->finish_query(
        graph->providers.queue.context, &queue->finish_request.token,
        &terminal, &ready);
    if (result == FWLAB_C43_API_IN_PROGRESS) {
        if (!ready || !bytes_are(&terminal, sizeof(terminal), 0xa5)) {
            queue_fault(graph, slot);
            *transitions = 1;
        }
        return;
    }
    if (result != FWLAB_C43_API_OK) {
        queue_fault(graph, slot);
        *transitions = 1;
        return;
    }
    if (!ready) {
        if (!bytes_are(&terminal, sizeof(terminal), 0xa5)) {
            queue_fault(graph, slot);
            *transitions = 1;
        }
        return;
    }
    if (!fwlab_c43_queue_finish_terminal_matches_request(
            &queue->finish_request, &terminal) ||
        terminal.common.cookie != queue->prepare_request.common.cookie ||
        terminal.facts.operation != queue->prepare_request.operation ||
        terminal.facts.role != queue->prepare_request.role ||
        !queue_facts_immutable_equal(&queue->prepared_facts,
                                     &terminal.facts)) {
        queue_fault(graph, slot);
        *transitions = 1;
        return;
    }
    queue->terminal = terminal;
    if (terminal.state == FWLAB_C43_QUEUE_EFFECT_COMMITTED) {
        queue->flow = C43_QUEUE_FLOW_APPLY_COMMIT;
    } else if (terminal.state == FWLAB_C43_QUEUE_EFFECT_ABORTED) {
        queue->flow = C43_QUEUE_FLOW_APPLY_ABORT;
    } else if (terminal.state == FWLAB_C43_QUEUE_EFFECT_TOO_LATE) {
        queue->flow = C43_QUEUE_FLOW_APPLY_COMMIT;
        queue->resolved_status = FWLAB_C43_STATUS_SUCCESS;
    } else {
        queue_fault(graph, slot);
        *transitions = 1;
        return;
    }
    queue_record_sync(record, &graph->observer.commands[slot]);
    *transitions = 1;
}

static void queue_apply_commit_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_queue_txn_record *queue = &record->queue_txn;
    struct c43_queue_authority *authority = &graph->queue_authority;
    struct fwlab_c43_command_observer *observer =
        &graph->observer.commands[slot];
    uint32_t failure_status;

    if (!queue_facts_immutable_equal(&queue->prepared_facts,
                                     &queue->terminal.facts) ||
        !queue_facts_commit_ready(graph, record, &queue->terminal.facts,
                                  &failure_status)) {
        queue_fault(graph, slot);
        *transitions = 1;
        return;
    }
    switch (record->request.kind) {
    case FWLAB_C43_REQUEST_CREATE_IO_CQ:
        authority->io_cq = queue->terminal.facts.queue;
        authority->io_cq_present = 1;
        break;
    case FWLAB_C43_REQUEST_CREATE_IO_SQ:
        authority->io_sq = queue->terminal.facts.queue;
        authority->sq_associated_cq = authority->io_cq;
        authority->io_sq_present = 1;
        break;
    case FWLAB_C43_REQUEST_DELETE_IO_SQ:
        memset(&authority->io_sq, 0, sizeof(authority->io_sq));
        memset(&authority->sq_associated_cq, 0,
               sizeof(authority->sq_associated_cq));
        authority->io_sq_present = 0;
        break;
    case FWLAB_C43_REQUEST_DELETE_IO_CQ:
        memset(&authority->io_cq, 0, sizeof(authority->io_cq));
        authority->io_cq_present = 0;
        break;
    default:
        queue_fault(graph, slot);
        *transitions = 1;
        return;
    }
    queue->local_effect_applied = 1;
    queue->flow = C43_QUEUE_FLOW_RETIRE;
    observer->phase = FWLAB_C43_PHASE_ACTION_WAIT;
    observer->satisfied_witness_mask |=
        FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED;
    observer->success_eligible =
        queue->resolved_status == FWLAB_C43_STATUS_SUCCESS;
    queue_record_sync(record, observer);
    queue_authority_sync(graph);
    *transitions = 1;
}

static void queue_apply_abort_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_queue_txn_record *queue = &record->queue_txn;
    struct fwlab_c43_command_observer *observer =
        &graph->observer.commands[slot];

    queue->flow = C43_QUEUE_FLOW_RETIRE;
    observer->phase = FWLAB_C43_PHASE_ACTION_WAIT;
    observer->success_eligible = 0;
    queue_record_sync(record, observer);
    *transitions = 1;
}

static void queue_retire_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_queue_txn_record *queue = &record->queue_txn;
    const enum fwlab_c43_api_result result =
        graph->providers.queue.ops->retire(
            graph->providers.queue.context,
            &queue->prepare_request.common.token);

    if (result == FWLAB_C43_API_IN_PROGRESS) {
        return;
    }
    if (result != FWLAB_C43_API_OK) {
        queue_fault(graph, slot);
        *transitions = 1;
        return;
    }
    queue->provider_owned = 0;
    queue->flow = C43_QUEUE_FLOW_DONE;
    queue_record_sync(record, &graph->observer.commands[slot]);
    queue_release_owner(graph, slot);
    *transitions = 1;
}

static int queue_record_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];

    *transitions = 0;
    if (record->queue_txn.flow == C43_QUEUE_FLOW_NONE) {
        if (record->request.kind ==
            FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES) {
            queue_begin_nq(graph, slot);
            *transitions = 1;
            return 1;
        }
        if (!queue_command_kind(record->request.kind)) {
            return 0;
        }
        if (graph->queue_authority.txn_active) {
            return 0;
        }
        queue_begin_transaction(graph, slot);
        *transitions = 1;
        return 1;
    }
    switch (record->queue_txn.flow) {
    case C43_QUEUE_FLOW_PREPARE_START:
        queue_prepare_start_step(graph, slot, transitions);
        return 1;
    case C43_QUEUE_FLOW_PREPARE_QUERY:
        queue_prepare_query_step(graph, slot, transitions);
        return 1;
    case C43_QUEUE_FLOW_DECIDE:
        queue_decide_step(graph, slot, transitions);
        return 1;
    case C43_QUEUE_FLOW_FINISH_START:
        queue_finish_start_step(graph, slot, transitions);
        return 1;
    case C43_QUEUE_FLOW_FINISH_QUERY:
        queue_finish_query_step(graph, slot, transitions);
        return 1;
    case C43_QUEUE_FLOW_APPLY_COMMIT:
        queue_apply_commit_step(graph, slot, transitions);
        return 1;
    case C43_QUEUE_FLOW_APPLY_ABORT:
        queue_apply_abort_step(graph, slot, transitions);
        return 1;
    case C43_QUEUE_FLOW_RETIRE:
        queue_retire_step(graph, slot, transitions);
        return 1;
    default:
        return 0;
    }
}

static void target_record_sync(
    struct c43_command_record *record,
    struct fwlab_c43_command_observer *observer)
{
    const struct c43_target_txn_record *target = &record->target_txn;

    observer->action_domain = FWLAB_C43_ACTION_DOMAIN_TARGET;
    observer->action_state =
        (uint8_t)target_public_action_state(target->flow);
    observer->resolution_valid = target->resolution_valid;
    observer->resolved_status = target->resolved_status;
    observer->action_outcome = target->outcome;
    observer->block_terminal_kind = 0;
    observer->target_pin_present = target->pin_installed;
    memset(&observer->target_ticket, 0, sizeof(observer->target_ticket));
    memset(&observer->target_reference, 0,
           sizeof(observer->target_reference));
    if (target->pin_installed) {
        observer->target_ticket = target->terminal.target;
        observer->target_reference = target->terminal.reference;
    }
}

static void target_fault(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t outcome)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_target_txn_record *target = &record->target_txn;
    struct fwlab_c43_command_observer *observer =
        &graph->observer.commands[slot];

    if (target->flow != C43_TARGET_FLOW_FAULT) {
        target->fault_from_flow = (uint8_t)target->flow;
    }
    target->flow = C43_TARGET_FLOW_FAULT;
    target->outcome = outcome;
    target->resolution_valid = 1;
    target->resolved_status = FWLAB_C43_STATUS_INTERNAL_FAILURE;
    target->pin_installed = 0;
    target->target_slot_plus_one = 0;
    observer->phase = FWLAB_C43_PHASE_ACTION_WAIT;
    observer->success_eligible = 0;
    target_record_sync(record, observer);
}

static int target_request_build(
    const struct c43_command_record *record,
    struct fwlab_c43_target_request *request)
{
    memset(request, 0, sizeof(*request));
    request->version = FWLAB_C43_TARGET_RESOLVER_PORT_VERSION;
    request->size = sizeof(*request);
    request->common.version = FWLAB_HIF_ACTION_VERSION;
    request->common.size = sizeof(request->common);
    queue_action_token(record, &request->common.token);
    request->common.cookie = record->transaction_uid;
    request->common.dependency_ordinal = UINT32_MAX;
    request->common.requested_units = 1;
    request->abort_command = record->ticket;
    request->operation = FWLAB_C43_TARGET_RESOLVE_ABORT;
    return fwlab_c43_target_request_valid(request);
}

static void target_begin(
    struct fwlab_c43_graph *graph,
    uint32_t slot)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_target_txn_record *target;
    struct fwlab_c43_command_observer *observer =
        &graph->observer.commands[slot];

    memset(&record->queue_txn, 0, sizeof(record->queue_txn));
    target = &record->target_txn;
    target->flow = C43_TARGET_FLOW_SUBMIT_START;
    target->provider_generation = graph->providers.target.generation;
    observer->phase = FWLAB_C43_PHASE_RESOLVE_WAIT;
    target_record_sync(record, observer);
    if (!target_request_build(record, &target->request)) {
        target_fault(graph, slot, 0);
    }
}

static void target_submit_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_target_txn_record *target = &record->target_txn;
    struct fwlab_hif_action_submit_result result;
    enum fwlab_hif_action_disposition disposition;

    memset(&result, 0xa5, sizeof(result));
    disposition = graph->providers.target.ops->submit(
        graph->providers.target.context, &target->request, &result);
    if (disposition == FWLAB_HIF_ACTION_BACKPRESSURE) {
        return;
    }
    if (disposition == FWLAB_HIF_ACTION_ACCEPTED) {
        target->provider_owned = 1;
    }
    if (disposition != FWLAB_HIF_ACTION_ACCEPTED ||
        !queue_submit_result_valid(&result,
                                   &target->request.common.token)) {
        target_fault(graph, slot, 0);
        *transitions = 1;
        return;
    }
    target->flow = C43_TARGET_FLOW_QUERY;
    target_record_sync(record, &graph->observer.commands[slot]);
    *transitions = 1;
}

static void target_query_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_target_txn_record *target = &record->target_txn;
    struct fwlab_c43_target_terminal terminal;
    struct fwlab_c43_command_observer *observer =
        &graph->observer.commands[slot];
    uint32_t target_slot = 0;
    bool ready = true;
    enum fwlab_c43_api_result result;

    memset(&terminal, 0xa5, sizeof(terminal));
    result = graph->providers.target.ops->query(
        graph->providers.target.context, &target->request.common.token,
        &terminal, &ready);
    if (result == FWLAB_C43_API_IN_PROGRESS) {
        if (!ready || !bytes_are(&terminal, sizeof(terminal), 0xa5)) {
            target_fault(graph, slot, 0);
            *transitions = 1;
        }
        return;
    }
    if (result != FWLAB_C43_API_OK) {
        target_fault(graph, slot, 0);
        *transitions = 1;
        return;
    }
    if (!ready) {
        if (!bytes_are(&terminal, sizeof(terminal), 0xa5)) {
            target_fault(graph, slot, 0);
            *transitions = 1;
        }
        return;
    }
    if (!fwlab_c43_target_terminal_matches_request(&target->request,
                                                   &terminal)) {
        target_fault(graph, slot, 0);
        *transitions = 1;
        return;
    }
    if (terminal.outcome == FWLAB_C43_TARGET_FOUND) {
        if (!target_find_slot(graph, slot, &terminal.target, &target_slot) ||
            graph->commands[target_slot].incoming_target_pins != 0) {
            target_fault(graph, slot, 0);
            *transitions = 1;
            return;
        }
        target->terminal = terminal;
        target->pin_installed = 1;
        target->target_slot_plus_one = (uint16_t)(target_slot + 1);
        graph->commands[target_slot].incoming_target_pins = 1;
        graph->observer.commands[target_slot].incoming_target_pins = 1;
    } else if (terminal.outcome >= FWLAB_C43_TARGET_NOT_FOUND &&
               terminal.outcome <= FWLAB_C43_TARGET_SUPERSEDED) {
        target->terminal = terminal;
    } else if (terminal.outcome == FWLAB_C43_TARGET_FAULT) {
        target->terminal = terminal;
        target_fault(graph, slot, FWLAB_C43_TARGET_FAULT);
        *transitions = 1;
        return;
    } else {
        target_fault(graph, slot, 0);
        *transitions = 1;
        return;
    }
    target->flow = C43_TARGET_FLOW_DONE;
    target->outcome = terminal.outcome;
    target->resolution_valid = 1;
    target->resolved_status = FWLAB_C43_STATUS_SUCCESS;
    observer->phase = FWLAB_C43_PHASE_ACTION_WAIT;
    observer->success_eligible = 1;
    target_record_sync(record, observer);
    *transitions = 1;
}

static int target_record_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];

    *transitions = 0;
    if (record->target_txn.flow == C43_TARGET_FLOW_NONE) {
        target_begin(graph, slot);
        *transitions = 1;
        return 1;
    }
    if (record->target_txn.flow == C43_TARGET_FLOW_SUBMIT_START) {
        target_submit_step(graph, slot, transitions);
        return 1;
    }
    if (record->target_txn.flow == C43_TARGET_FLOW_QUERY) {
        target_query_step(graph, slot, transitions);
        return 1;
    }
    return 0;
}

static void block_record_sync(
    struct c43_command_record *record,
    struct fwlab_c43_command_observer *observer)
{
    const struct c43_block_txn_record *block = &record->block_txn;

    observer->action_domain = FWLAB_C43_ACTION_DOMAIN_BLOCK;
    observer->action_state =
        (uint8_t)block_public_action_state(block->flow);
    observer->resolution_valid = block->resolution_valid;
    observer->resolved_status = block->resolved_status;
    observer->action_outcome = 0;
    observer->block_terminal_kind = block->terminal_kind;
    observer->target_pin_present = 0;
    memset(&observer->target_ticket, 0, sizeof(observer->target_ticket));
    memset(&observer->target_reference, 0,
           sizeof(observer->target_reference));
}

static void block_fault(
    struct fwlab_c43_graph *graph,
    uint32_t slot)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_block_txn_record *block = &record->block_txn;
    struct fwlab_c43_command_observer *observer =
        &graph->observer.commands[slot];

    if (block->flow != C43_BLOCK_FLOW_FAULT) {
        block->fault_from_flow = (uint8_t)block->flow;
    }
    block->flow = C43_BLOCK_FLOW_FAULT;
    block->terminal_kind = 0;
    block->resolution_valid = 1;
    block->resolved_status = FWLAB_C43_STATUS_INTERNAL_FAILURE;
    memset(&block->terminal, 0, sizeof(block->terminal));
    observer->phase = FWLAB_C43_PHASE_ACTION_WAIT;
    observer->satisfied_witness_mask = 0;
    observer->success_eligible = 0;
    block_record_sync(record, observer);
}

static void block_rejected(
    struct fwlab_c43_graph *graph,
    uint32_t slot)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_block_txn_record *block = &record->block_txn;
    struct fwlab_c43_command_observer *observer =
        &graph->observer.commands[slot];

    block->flow = C43_BLOCK_FLOW_REJECTED_NO_EFFECT;
    block->resolution_valid = 1;
    block->resolved_status = FWLAB_C43_STATUS_RESOURCE_FAILURE;
    block->provider_owned = 0;
    observer->phase = FWLAB_C43_PHASE_ACTION_WAIT;
    observer->satisfied_witness_mask = 0;
    observer->success_eligible = 0;
    block_record_sync(record, observer);
}

static int block_request_build(
    const struct c43_command_record *record,
    const struct fwlab_c43_block_action_port *port,
    struct fwlab_c43_block_action_request *request)
{
    memset(request, 0, sizeof(*request));
    request->version = FWLAB_C43_BLOCK_ACTION_PORT_VERSION;
    request->size = sizeof(*request);
    request->common.version = FWLAB_HIF_ACTION_VERSION;
    request->common.size = sizeof(request->common);
    queue_action_token(record, &request->common.token);
    request->common.token.kind = FWLAB_HIF_ACTION_BLOCK;
    request->common.cookie = record->transaction_uid;
    request->common.dependency_ordinal = UINT32_MAX;
    request->common.requested_units =
        record->plan.block.lba_count == 0 ? 1 :
                                            record->plan.block.lba_count;
    request->intent = record->plan.block;
    request->requested_witness_mask = FWLAB_C43_WITNESS_VALIDATED_ONLY;
    return fwlab_c43_block_action_request_valid_for_port(request, port);
}

static void block_begin(
    struct fwlab_c43_graph *graph,
    uint32_t slot)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_block_txn_record *block;
    struct fwlab_c43_command_observer *observer =
        &graph->observer.commands[slot];

    memset(&record->queue_txn, 0, sizeof(record->queue_txn));
    block = &record->block_txn;
    block->flow = C43_BLOCK_FLOW_SUBMIT_START;
    block->provider_generation = graph->providers.block.generation;
    observer->phase = FWLAB_C43_PHASE_RESOLVE_WAIT;
    block_record_sync(record, observer);
    if (!block_request_build(record, &graph->providers.block,
                             &block->request)) {
        block_fault(graph, slot);
    }
}

static void block_submit_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_block_txn_record *block = &record->block_txn;
    struct fwlab_hif_action_submit_result result;
    enum fwlab_hif_action_disposition disposition;

    memset(&result, 0xa5, sizeof(result));
    disposition = graph->providers.block.ops->submit(
        graph->providers.block.context, &block->request, &result);
    if (disposition == FWLAB_HIF_ACTION_BACKPRESSURE) {
        return;
    }
    if (disposition == FWLAB_HIF_ACTION_REJECTED) {
        block_rejected(graph, slot);
        *transitions = 1;
        return;
    }
    if (disposition == FWLAB_HIF_ACTION_ACCEPTED) {
        block->provider_owned = 1;
    }
    if (disposition != FWLAB_HIF_ACTION_ACCEPTED ||
        !queue_submit_result_valid(&result,
                                   &block->request.common.token)) {
        block_fault(graph, slot);
        *transitions = 1;
        return;
    }
    block->flow = C43_BLOCK_FLOW_QUERY;
    block_record_sync(record, &graph->observer.commands[slot]);
    *transitions = 1;
}

static void block_query_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];
    struct c43_block_txn_record *block = &record->block_txn;
    struct fwlab_c43_block_action_terminal terminal;
    struct fwlab_c43_command_observer *observer =
        &graph->observer.commands[slot];
    bool ready = true;
    enum fwlab_c43_api_result result;

    memset(&terminal, 0xa5, sizeof(terminal));
    result = graph->providers.block.ops->query(
        graph->providers.block.context, &block->request.common.token,
        &terminal, &ready);
    if (result == FWLAB_C43_API_IN_PROGRESS) {
        if (!ready || !bytes_are(&terminal, sizeof(terminal), 0xa5)) {
            block_fault(graph, slot);
            *transitions = 1;
        }
        return;
    }
    if (result != FWLAB_C43_API_OK) {
        block_fault(graph, slot);
        *transitions = 1;
        return;
    }
    if (!ready) {
        if (!bytes_are(&terminal, sizeof(terminal), 0xa5)) {
            block_fault(graph, slot);
            *transitions = 1;
        }
        return;
    }
    if (!fwlab_c43_block_action_terminal_matches_request(
            &block->request, &terminal, &graph->providers.block)) {
        block_fault(graph, slot);
        *transitions = 1;
        return;
    }
    if (terminal.block_terminal_kind ==
        FWLAB_C43_BLOCK_VALIDATED_ONLY) {
        block->resolved_status = FWLAB_C43_STATUS_SUCCESS;
        observer->satisfied_witness_mask |=
            FWLAB_C43_WITNESS_VALIDATED_ONLY;
    } else if (terminal.block_terminal_kind ==
               FWLAB_C43_BLOCK_FAILED_NO_EFFECT) {
        block->resolved_status = FWLAB_C43_STATUS_MEDIA_FAILURE;
    } else {
        block_fault(graph, slot);
        *transitions = 1;
        return;
    }
    block->terminal = terminal;
    block->terminal_kind = terminal.block_terminal_kind;
    block->resolution_valid = 1;
    block->flow = C43_BLOCK_FLOW_DONE;
    observer->phase = FWLAB_C43_PHASE_ACTION_WAIT;
    observer->success_eligible = 0;
    block_record_sync(record, observer);
    *transitions = 1;
}

static int block_record_step(
    struct fwlab_c43_graph *graph,
    uint32_t slot,
    uint32_t *transitions)
{
    struct c43_command_record *record = &graph->commands[slot];

    *transitions = 0;
    if (record->block_txn.flow == C43_BLOCK_FLOW_NONE) {
        block_begin(graph, slot);
        *transitions = 1;
        return 1;
    }
    if (record->block_txn.flow == C43_BLOCK_FLOW_SUBMIT_START) {
        block_submit_step(graph, slot, transitions);
        return 1;
    }
    if (record->block_txn.flow == C43_BLOCK_FLOW_QUERY) {
        block_query_step(graph, slot, transitions);
        return 1;
    }
    return 0;
}

int c43_phase4_step(
    struct fwlab_c43_graph *graph,
    uint32_t *transitions)
{
    uint32_t offset;

    *transitions = 0;
    for (offset = 0; offset < FWLAB_C43_MAX_COMMANDS; ++offset) {
        const uint32_t slot =
            (graph->next_service_slot + offset) % FWLAB_C43_MAX_COMMANDS;
        struct c43_command_record *record = &graph->commands[slot];

        if (!record->in_use ||
            record->state != C43_COMMAND_RECORD_ADMITTED) {
            continue;
        }
        if ((record->plan.kind == FWLAB_C43_PLAN_ABORT_RESOLVE &&
             target_record_step(graph, slot, transitions)) ||
            (record->plan.kind == FWLAB_C43_PLAN_BLOCK &&
             block_record_step(graph, slot, transitions)) ||
            ((record->plan.kind != FWLAB_C43_PLAN_ABORT_RESOLVE &&
              record->plan.kind != FWLAB_C43_PLAN_BLOCK) &&
             queue_record_step(graph, slot, transitions))) {
            graph->next_service_slot =
                (slot + 1) % FWLAB_C43_MAX_COMMANDS;
            return 1;
        }
    }
    return 0;
}
