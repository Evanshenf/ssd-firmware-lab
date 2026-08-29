/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <inttypes.h>
#include <stdio.h>

#include "../c32_internal.h"

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int byte;

    for (byte = 0; byte < 8; ++byte) {
        hash ^= (uint8_t)(value >> (byte * 8u));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void print_logical(const struct c32_logical_state *state)
{
    printf("{\"kind\":%u,\"version\":%u,\"value\":%u,"
           "\"state_id\":%u,\"authority\":%u}",
           state->kind, state->version, state->value_token,
           state->state_id, state->authority_record_id);
}

static void print_counterexample(const struct c32_counterexample *example)
{
    unsigned int index;
    int first;

    printf("{\"schema\":%u,\"invariant\":\"%s\","
           "\"broken_mutation\":\"%s\",\"minimal_depth\":%u,"
           "\"scenario\":\"%s\",\"profile\":\"%s\","
           "\"initial_variant\":%u,\"request\":%u,"
           "\"initial_hash\":\"%016" PRIx64 "\","
           "\"precut_hash\":\"%016" PRIx64 "\","
           "\"physical_hash\":\"%016" PRIx64 "\","
           "\"recovered_hash\":\"%016" PRIx64 "\","
           "\"oracle_hash\":\"%016" PRIx64 "\","
           "\"cut\":%u,\"selected_checkpoint\":%u,"
           "\"plp_drained_envelopes\":%u,\"actions\":[",
           example->schema_version,
           c32_invariant_name((enum fwlab_persist_invariant_id)
                              example->invariant_id),
           c32_broken_name((enum c32_broken_variant)
                           example->broken_variant),
           example->minimal_depth,
           c32_scenario_name((enum c32_scenario_family)
                             example->scenario_family),
           c32_profile_name((enum c32_profile_variant)
                            example->profile_variant),
           example->initial_variant, example->request_kind,
           example->initial_hash, example->precut_hash,
           example->physical_hash, example->recovered_hash,
           example->oracle_hash, example->cut_kind,
           example->selected_checkpoint,
           example->plp_drained_envelopes);
    for (index = 0; index < example->action_count; ++index) {
        const struct c32_model_action *action = &example->action[index];

        printf("%s{\"name\":\"%s\",\"purpose\":%u,"
               "\"outcome\":%u,\"subject\":%u,\"step\":%u}",
               index == 0 ? "" : ",",
               c32_action_name((enum c32_model_action_kind)action->kind),
               action->purpose, action->outcome, action->subject,
               action->step);
    }
    printf("],\"frozen_physical_outcomes\":[");
    first = 1;
    for (index = 0; index < example->action_count; ++index) {
        const struct c32_model_action *action = &example->action[index];

        if (action->kind != C32_ACTION_B_PHYS) {
            continue;
        }
        printf("%s{\"purpose\":%u,\"outcome\":%u}",
               first ? "" : ",", action->purpose, action->outcome);
        first = 0;
    }
    printf("],\"durable_floors\":[");
    print_logical(&example->durable_floor[0]);
    printf(",");
    print_logical(&example->durable_floor[1]);
    printf("],\"allowed_set\":[%u,%u],\"recovered_atoms\":[",
           example->allowed_set[0], example->allowed_set[1]);
    print_logical(&example->recovered[0]);
    printf(",");
    print_logical(&example->recovered[1]);
    printf("],\"expected\":\"named invariant rejects mutation\","
           "\"actual_violation_mask\":\"%04x\"}\n",
           example->violation_mask);
}

int main(void)
{
    uint64_t aggregate = UINT64_C(1469598103934665603);
    unsigned int broken;

    for (broken = C32_BM_UNIQUE_KEEP_PREDECESSOR;
         broken <= C32_BM_HOST_FALLBACK_ON_NO_MAP; ++broken) {
        struct c32_counterexample example;
        unsigned int action;

        if (!c32_model_find_counterexample(
                (enum c32_broken_variant)broken, &example) ||
            example.action_count != example.minimal_depth ||
            (example.violation_mask &
             (uint16_t)(UINT16_C(1) << (broken - 1u))) == 0) {
            fprintf(stderr, "C3.2 broken model failed for %s\n",
                    c32_broken_name((enum c32_broken_variant)broken));
            return 1;
        }
        print_counterexample(&example);
        aggregate = hash_u64(aggregate, example.broken_variant);
        aggregate = hash_u64(aggregate, example.minimal_depth);
        aggregate = hash_u64(aggregate, example.initial_hash);
        aggregate = hash_u64(aggregate, example.precut_hash);
        aggregate = hash_u64(aggregate, example.physical_hash);
        aggregate = hash_u64(aggregate, example.recovered_hash);
        aggregate = hash_u64(aggregate, example.oracle_hash);
        for (action = 0; action < example.action_count; ++action) {
            aggregate = hash_u64(aggregate, example.action[action].kind);
            aggregate = hash_u64(aggregate, example.action[action].purpose);
            aggregate = hash_u64(aggregate, example.action[action].outcome);
        }
    }
    printf("C3.2 broken variants: PASS (13 shortest counterexamples, "
           "hash=%016" PRIx64 ")\n", aggregate);
    return 0;
}
