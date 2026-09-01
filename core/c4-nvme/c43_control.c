/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_internal.h"

int c43_semantic_status_valid(uint32_t status)
{
    return status <= FWLAB_C43_STATUS_INTERNAL_FAILURE;
}

int c43_witness_mask_valid(uint32_t mask)
{
    return (mask & ~FWLAB_C43_WITNESS_ALL) == 0;
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
            FWLAB_C43_MAX_COMMANDS) {
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
            (command->reservation_credit_mask &
             ~FWLAB_C43_CREDIT_ALL) != 0 ||
            ((command->action_count == 0) !=
             (command->first_action_uid == 0)) ||
            ((command->action_count == 0) !=
             (command->action_generation == 0)) ||
            (command->action_count != 0 &&
             command->first_action_uid >
                 UINT64_MAX - (command->action_count - 1)) ||
            !c43_bytes_zero(command->reserved0,
                            sizeof(command->reserved0)) ||
            command->reserved2 != 0) {
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

            if (command->phase == FWLAB_C43_PHASE_FREE ||
                (command->phase == FWLAB_C43_PHASE_PREPARED &&
                 (command->action_count !=
                      FWLAB_C43_ACTIONS_PER_COMMAND ||
                  command->reservation_credit_mask != FWLAB_C43_CREDIT_ALL ||
                  command->terminal_winner != FWLAB_C43_WINNER_NONE ||
                  command->publication !=
                      FWLAB_C43_PUBLICATION_ELIGIBLE ||
                  command->required_witness_mask != 0 ||
                  command->satisfied_witness_mask != 0 ||
                  command->success_eligible != 0 ||
                  command->provider_generation_current != 1)) ||
                !c43_handle_valid(&command->handle) ||
                !c43_origin_valid(&command->origin) ||
                command->transaction_uid == 0 ||
                command->handle.instance_nonce != observer->instance_nonce ||
                command->handle.controller_epoch !=
                    observer->controller_epoch ||
                (command->required_witness_mask != 0 &&
                 command->success_eligible != witness_complete) ||
                (command->success_eligible &&
                 (((command->satisfied_witness_mask &
                    command->required_witness_mask) !=
                   command->required_witness_mask) ||
                  (command->satisfied_witness_mask &
                   FWLAB_C43_WITNESS_VALIDATED_ONLY) != 0))) {
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
                command->publication != FWLAB_C43_PUBLICATION_ELIGIBLE) {
                return 0;
            }
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
