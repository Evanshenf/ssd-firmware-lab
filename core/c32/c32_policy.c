/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fwlab/portable/persistence_policy.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static int token_equal(
    const struct fwlab_persist_mutation_token *left,
    const struct fwlab_persist_mutation_token *right
)
{
    return left->word[0] == right->word[0] &&
           left->word[1] == right->word[1];
}

static unsigned int bit_count(uint8_t value)
{
    unsigned int count = 0;

    while (value != 0) {
        count += value & UINT8_C(1);
        value >>= 1;
    }
    return count;
}

static int profile_shape_valid(const struct fwlab_persist_profile *profile)
{
    uint8_t required_survival =
        FWLAB_PERSIST_EVENT_CONTROLLER_RESET |
        FWLAB_PERSIST_EVENT_POWER_LOSS;

    if (profile == NULL || profile->size != sizeof(*profile) ||
        profile->reserved0 != 0 || profile->reserved1 != 0 ||
        profile->cache_enabled > 1 ||
        (profile->survival_event_mask & ~FWLAB_PERSIST_EVENT_MASK) != 0) {
        return 0;
    }
    if (profile->plp_kind == FWLAB_PERSIST_PLP_NONE) {
        return profile->plp_capacity_credits == 0 &&
               profile->survival_event_mask == 0;
    }
    if (profile->plp_kind == FWLAB_PERSIST_PLP_VALIDATED) {
        return profile->plp_capacity_credits > 0 &&
               profile->plp_capacity_credits <= FWLAB_PERSIST_MAX_ATOMS &&
               (profile->survival_event_mask & required_survival) ==
                   required_survival;
    }
    return 0;
}

enum fwlab_persist_result fwlab_persist_profile_validate(
    const struct fwlab_persist_profile *profile
)
{
    if (profile == NULL) {
        return FWLAB_PERSIST_INVALID_CONTRACT;
    }
    if (profile->version != FWLAB_PERSIST_VERSION) {
        return FWLAB_PERSIST_UNSUPPORTED_VERSION;
    }
    return profile_shape_valid(profile) ? FWLAB_PERSIST_OK :
                                          FWLAB_PERSIST_INVALID_CONTRACT;
}

static int plp_shape_valid(
    const struct fwlab_persist_profile *profile,
    const struct fwlab_persist_plp_envelope *envelope
)
{
    uint8_t atom_mask = envelope->atom_mask & UINT8_C(0x03);

    return envelope->size == sizeof(*envelope) &&
           envelope->reserved0 == 0 &&
           envelope->reserved1[0] == 0 &&
           envelope->reserved1[1] == 0 &&
           envelope->reserved1[2] == 0 &&
           atom_mask != 0 && envelope->atom_mask == atom_mask &&
           envelope->capacity_cost == bit_count(atom_mask) &&
           envelope->capacity_cost <= profile->plp_capacity_credits &&
           (envelope->flags & FWLAB_PLP_REQUIRED_FLAGS) ==
               FWLAB_PLP_REQUIRED_FLAGS &&
           (envelope->flags & ~FWLAB_PLP_REQUIRED_FLAGS) == 0 &&
           (envelope->survival_event_mask & profile->survival_event_mask) ==
               profile->survival_event_mask &&
           (envelope->survival_event_mask & ~FWLAB_PERSIST_EVENT_MASK) == 0 &&
           (envelope->drained_atom_mask & ~atom_mask) == 0;
}

enum fwlab_persist_result fwlab_persist_plp_validate(
    const struct fwlab_persist_profile *profile,
    const struct fwlab_persist_plp_envelope *envelope
)
{
    enum fwlab_persist_result result =
        fwlab_persist_profile_validate(profile);

    if (result != FWLAB_PERSIST_OK || envelope == NULL) {
        return result != FWLAB_PERSIST_OK ? result :
                                           FWLAB_PERSIST_INVALID_CONTRACT;
    }
    if (envelope->version != FWLAB_PERSIST_VERSION) {
        return FWLAB_PERSIST_UNSUPPORTED_VERSION;
    }
    if (profile->plp_kind != FWLAB_PERSIST_PLP_VALIDATED) {
        return FWLAB_PERSIST_INVALID_CONTRACT;
    }
    return plp_shape_valid(profile, envelope) ? FWLAB_PERSIST_OK :
                                                FWLAB_PERSIST_INVALID_CONTRACT;
}

static int request_shape_valid(const struct fwlab_persist_request *request)
{
    if (request == NULL || request->size != sizeof(*request) ||
        request->reserved0 != 0 || request->reserved1[0] != 0 ||
        request->reserved1[1] != 0 || request->owner_epoch == 0) {
        return 0;
    }
    if (request->kind == FWLAB_PERSIST_FENCE) {
        return request->atom_mask == 0;
    }
    return (request->kind == FWLAB_PERSIST_DEFAULT ||
            request->kind == FWLAB_PERSIST_SELF_DURABLE) &&
           request->atom_mask != 0 &&
           (request->atom_mask & ~UINT8_C(0x03)) == 0 &&
           request->frontier == 0;
}

static int fact_shape_valid(const struct fwlab_persist_atom_fact *fact)
{
    uint32_t mask;

    if (fact->version != FWLAB_PERSIST_VERSION ||
        fact->size != sizeof(*fact) || fact->reserved0 != 0 ||
        fact->reserved1[0] != 0 || fact->reserved1[1] != 0 ||
        fact->reserved1[2] != 0 || fact->atom >= FWLAB_PERSIST_MAX_ATOMS ||
        fact->logical_version > 3 || fact->predecessor_version > 3 ||
        fact->mutation_kind > FWLAB_PERSIST_CHECKPOINT ||
        fact->closure > FWLAB_PERSIST_CLOSE_INDETERMINATE) {
        return 0;
    }
    mask = fact->fact_mask;
    if ((mask & ~FWLAB_PERSIST_FACT_MASK) != 0 ||
        ((mask & FWLAB_PERSIST_FACT_PROVABLE_NO_COMMIT) != 0 &&
         (mask & FWLAB_PERSIST_FACT_INDETERMINATE) != 0) ||
        ((mask & FWLAB_PERSIST_FACT_C_MAP) != 0 &&
         (mask & FWLAB_PERSIST_FACT_LOGICAL_DURABLE) == 0)) {
        return 0;
    }
    return 1;
}

static const struct fwlab_persist_plp_envelope *find_envelope(
    const struct fwlab_persist_mutation_token *token,
    uint32_t owner_epoch,
    uint32_t sequence,
    const struct fwlab_persist_plp_envelope *envelopes,
    size_t envelope_count
)
{
    size_t index;

    for (index = 0; index < envelope_count; ++index) {
        if (token_equal(token, &envelopes[index].token) &&
            owner_epoch == envelopes[index].owner_epoch &&
            sequence == envelopes[index].sequence) {
            return &envelopes[index];
        }
    }
    return NULL;
}

static void witness_init(
    struct fwlab_persist_witness *witness,
    uint8_t required_mask
)
{
    memset(witness, 0, sizeof(*witness));
    witness->version = FWLAB_PERSIST_VERSION;
    witness->size = (uint16_t)sizeof(*witness);
    witness->required_atom_mask = required_mask;
}

enum fwlab_persist_result fwlab_persist_command_witness(
    const struct fwlab_persist_profile *profile,
    const struct fwlab_persist_request *request,
    const struct fwlab_persist_atom_fact *facts,
    size_t fact_count,
    const struct fwlab_persist_plp_envelope *envelopes,
    size_t envelope_count,
    struct fwlab_persist_witness *witness
)
{
    enum fwlab_persist_result result =
        fwlab_persist_profile_validate(profile);
    uint8_t volatile_mask = 0;
    uint8_t durable_mask = 0;
    size_t index;

    if (result != FWLAB_PERSIST_OK || request == NULL || witness == NULL ||
        (fact_count != 0 && facts == NULL) ||
        (envelope_count != 0 && envelopes == NULL)) {
        return result != FWLAB_PERSIST_OK ? result :
                                           FWLAB_PERSIST_INVALID_CONTRACT;
    }
    if (request->version != FWLAB_PERSIST_VERSION) {
        return FWLAB_PERSIST_UNSUPPORTED_VERSION;
    }
    if (!request_shape_valid(request) || request->kind == FWLAB_PERSIST_FENCE) {
        return FWLAB_PERSIST_INVALID_CONTRACT;
    }
    witness_init(witness, request->atom_mask);
    for (index = 0; index < fact_count; ++index) {
        const struct fwlab_persist_atom_fact *fact = &facts[index];
        uint8_t bit;
        int plp_ok = 0;

        if (!fact_shape_valid(fact)) {
            return FWLAB_PERSIST_INVALID_CONTRACT;
        }
        if (!token_equal(&request->token, &fact->token) ||
            request->owner_epoch != fact->owner_epoch ||
            request->scope != fact->scope ||
            request->sequence != fact->sequence) {
            continue;
        }
        bit = (uint8_t)(UINT8_C(1) << fact->atom);
        if ((request->atom_mask & bit) == 0) {
            continue;
        }
        if ((fact->fact_mask & FWLAB_PERSIST_FACT_INDETERMINATE) != 0 ||
            fact->closure == FWLAB_PERSIST_CLOSE_INDETERMINATE) {
            witness->witness_class = FWLAB_PERSIST_FAILED_INDETERMINATE;
            witness->reason = FWLAB_PERSIST_REASON_INDETERMINATE;
            return FWLAB_PERSIST_OK;
        }
        if ((fact->fact_mask & FWLAB_PERSIST_FACT_LOGICAL_DURABLE) != 0 &&
            fact->closure == FWLAB_PERSIST_CLOSE_C_MAP) {
            durable_mask |= bit;
            volatile_mask |= bit;
            continue;
        }
        if ((fact->fact_mask & FWLAB_PERSIST_FACT_PLP_ADMITTED) != 0 &&
            fact->closure == FWLAB_PERSIST_CLOSE_PLP &&
            profile->cache_enabled != 0 &&
            profile->plp_kind == FWLAB_PERSIST_PLP_VALIDATED) {
            const struct fwlab_persist_plp_envelope *envelope =
                find_envelope(&fact->token, fact->owner_epoch,
                              fact->sequence, envelopes, envelope_count);

            plp_ok = envelope != NULL &&
                     fwlab_persist_plp_validate(profile, envelope) ==
                         FWLAB_PERSIST_OK &&
                     (envelope->atom_mask & request->atom_mask) ==
                         request->atom_mask;
            if (plp_ok) {
                durable_mask |= bit;
                volatile_mask |= bit;
                continue;
            }
        }
        if ((fact->fact_mask & FWLAB_PERSIST_FACT_CAPTURED) != 0) {
            volatile_mask |= bit;
        }
    }
    witness->satisfied_atom_mask = durable_mask;
    if (durable_mask == request->atom_mask) {
        witness->witness_class = FWLAB_PERSIST_DURABLE_ELIGIBLE;
        return FWLAB_PERSIST_OK;
    }
    if (request->kind == FWLAB_PERSIST_DEFAULT && profile->cache_enabled != 0 &&
        profile->plp_kind == FWLAB_PERSIST_PLP_NONE &&
        volatile_mask == request->atom_mask) {
        witness->witness_class = FWLAB_PERSIST_VOLATILE_ELIGIBLE;
        witness->satisfied_atom_mask = volatile_mask;
        return FWLAB_PERSIST_OK;
    }
    witness->witness_class = FWLAB_PERSIST_WITNESS_NONE;
    witness->reason = volatile_mask == request->atom_mask ?
                      FWLAB_PERSIST_REASON_MISSING_DURABLE_FACT :
                      FWLAB_PERSIST_REASON_MISSING_ATOM;
    return FWLAB_PERSIST_OK;
}

enum fwlab_persist_result fwlab_persist_fence_witness(
    const struct fwlab_persist_profile *profile,
    const struct fwlab_persist_request *fence,
    const struct fwlab_persist_obligation *obligations,
    size_t obligation_count,
    const struct fwlab_persist_plp_envelope *envelopes,
    size_t envelope_count,
    struct fwlab_persist_witness *witness
)
{
    enum fwlab_persist_result result =
        fwlab_persist_profile_validate(profile);
    uint8_t covered = 0;
    uint8_t satisfied = 0;
    size_t index;

    if (result != FWLAB_PERSIST_OK || fence == NULL || witness == NULL ||
        (obligation_count != 0 && obligations == NULL) ||
        (envelope_count != 0 && envelopes == NULL)) {
        return result != FWLAB_PERSIST_OK ? result :
                                           FWLAB_PERSIST_INVALID_CONTRACT;
    }
    if (fence->version != FWLAB_PERSIST_VERSION) {
        return FWLAB_PERSIST_UNSUPPORTED_VERSION;
    }
    if (!request_shape_valid(fence) || fence->kind != FWLAB_PERSIST_FENCE) {
        return FWLAB_PERSIST_INVALID_CONTRACT;
    }
    witness_init(witness, 0);
    for (index = 0; index < obligation_count; ++index) {
        const struct fwlab_persist_obligation *obligation =
            &obligations[index];
        int obligation_satisfied = 0;

        if (obligation->version != FWLAB_PERSIST_VERSION ||
            obligation->size != sizeof(*obligation) ||
            obligation->reserved0 != 0 || obligation->reserved1[0] != 0 ||
            obligation->reserved1[1] != 0 ||
            obligation->atom_mask == 0 ||
            (obligation->atom_mask & ~UINT8_C(0x03)) != 0 ||
            obligation->closure > FWLAB_PERSIST_CLOSE_INDETERMINATE ||
            (obligation->fact_mask & ~FWLAB_PERSIST_FACT_MASK) != 0) {
            return FWLAB_PERSIST_INVALID_CONTRACT;
        }
        if (obligation->owner_epoch != fence->owner_epoch ||
            obligation->scope != fence->scope ||
            obligation->sequence > fence->frontier) {
            continue;
        }
        covered |= obligation->atom_mask;
        if (obligation->closure == FWLAB_PERSIST_CLOSE_INDETERMINATE ||
            (obligation->fact_mask & FWLAB_PERSIST_FACT_INDETERMINATE) != 0) {
            witness->witness_class = FWLAB_PERSIST_FAILED_INDETERMINATE;
            witness->reason = FWLAB_PERSIST_REASON_INDETERMINATE;
            return FWLAB_PERSIST_OK;
        }
        if (obligation->closure == FWLAB_PERSIST_CLOSE_NO_COMMIT &&
            (obligation->fact_mask &
             FWLAB_PERSIST_FACT_PROVABLE_NO_COMMIT) != 0) {
            obligation_satisfied = 1;
        } else if (obligation->closure == FWLAB_PERSIST_CLOSE_C_MAP &&
                   (obligation->fact_mask &
                    FWLAB_PERSIST_FACT_LOGICAL_DURABLE) != 0) {
            obligation_satisfied = 1;
        } else if (obligation->closure == FWLAB_PERSIST_CLOSE_PLP &&
                   (obligation->fact_mask &
                    FWLAB_PERSIST_FACT_PLP_ADMITTED) != 0) {
            const struct fwlab_persist_plp_envelope *envelope =
                find_envelope(&obligation->token, obligation->owner_epoch,
                              obligation->sequence, envelopes,
                              envelope_count);

            obligation_satisfied = envelope != NULL &&
                fwlab_persist_plp_validate(profile, envelope) ==
                    FWLAB_PERSIST_OK &&
                (envelope->atom_mask & obligation->atom_mask) ==
                    obligation->atom_mask;
        }
        if (!obligation_satisfied) {
            witness->witness_class = FWLAB_PERSIST_WITNESS_NONE;
            witness->required_atom_mask = covered;
            witness->satisfied_atom_mask = satisfied;
            witness->reason = obligation->closure ==
                                  FWLAB_PERSIST_CLOSE_PLP ?
                              FWLAB_PERSIST_REASON_INVALID_PLP :
                              FWLAB_PERSIST_REASON_OPEN_OBLIGATION;
            return FWLAB_PERSIST_OK;
        }
        satisfied |= obligation->atom_mask;
    }
    witness->required_atom_mask = covered;
    witness->satisfied_atom_mask = satisfied;
    witness->witness_class = FWLAB_PERSIST_DURABLE_ELIGIBLE;
    return FWLAB_PERSIST_OK;
}
