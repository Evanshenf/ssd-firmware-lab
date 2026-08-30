/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fwlab/contracts/hif_action.h"

_Static_assert(sizeof(struct fwlab_hif_action_token) == 56,
               "unexpected action-token native size");
_Static_assert(sizeof(struct fwlab_hif_action_envelope) == 96,
               "unexpected action-envelope native size");
_Static_assert(sizeof(struct fwlab_hif_action_submit_result) == 96,
               "unexpected action-submit-result native size");
_Static_assert(sizeof(struct fwlab_hif_action_terminal) == 96,
               "unexpected action-terminal native size");

static int words_zero(const uint32_t *words, uint32_t count)
{
    uint32_t index;

    for (index = 0; index < count; ++index) {
        if (words[index] != 0) {
            return 0;
        }
    }
    return 1;
}

int fwlab_hif_action_token_valid(const struct fwlab_hif_action_token *token)
{
    if (token == 0 || token->command.instance_nonce == 0 ||
        token->command.command_uid == 0 ||
        token->command.controller_epoch == 0 ||
        token->command.generation == 0 ||
        (token->origin.word[0] == 0 && token->origin.word[1] == 0) ||
        token->action_uid == 0 || token->generation == 0 ||
        token->kind < FWLAB_HIF_ACTION_QUEUE_EFFECT ||
        token->kind > FWLAB_HIF_ACTION_BLOCK || token->reserved != 0) {
        return 0;
    }
    return 1;
}

int fwlab_hif_action_envelope_valid(
    const struct fwlab_hif_action_envelope *envelope)
{
    return envelope != 0 && envelope->version == FWLAB_HIF_ACTION_VERSION &&
           envelope->size == sizeof(*envelope) && envelope->reserved0 == 0 &&
           fwlab_hif_action_token_valid(&envelope->token) &&
           envelope->cookie != 0 && envelope->requested_units != 0 &&
           words_zero(envelope->reserved1, 4);
}

int fwlab_hif_action_terminal_valid(
    const struct fwlab_hif_action_terminal *terminal)
{
    return terminal != 0 && terminal->version == FWLAB_HIF_ACTION_VERSION &&
           terminal->size == sizeof(*terminal) && terminal->reserved0 == 0 &&
           fwlab_hif_action_token_valid(&terminal->token) &&
           terminal->cookie != 0 &&
           terminal->terminal_kind <= FWLAB_HIF_ACTION_FAILED &&
           terminal->effect_class <= FWLAB_NVME_EFFECT_UNKNOWN_PREFIX &&
           terminal->retry <= FWLAB_HIF_ACTION_RETRY_NEVER &&
           terminal->reserved1 == 0 && words_zero(terminal->reserved2, 2);
}

int fwlab_hif_action_submit_result_valid(
    const struct fwlab_hif_action_submit_result *result)
{
    return result != 0 && result->version == FWLAB_HIF_ACTION_VERSION &&
           result->size == sizeof(*result) && result->reserved0 == 0 &&
           fwlab_hif_action_token_valid(&result->token) &&
           result->disposition <= FWLAB_HIF_ACTION_REJECTED &&
           result->retry <= FWLAB_HIF_ACTION_RETRY_NEVER &&
           result->effect_class <= FWLAB_NVME_EFFECT_UNKNOWN_PREFIX &&
           result->reserved1[0] == 0 && result->reserved1[1] == 0 &&
           words_zero(result->reserved2, 4);
}
