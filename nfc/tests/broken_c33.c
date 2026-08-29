/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <inttypes.h>
#include <stdio.h>

#include "c33_oracle.h"

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int byte;

    for (byte = 0; byte < 8; ++byte) {
        hash ^= (uint8_t)(value >> (byte * 8u));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void print_counterexample(const struct c33_counterexample *example)
{
    unsigned int index;

    printf("{\"schema\":%u,\"invariant\":\"%s\","
           "\"broken_mutation\":\"%s\",\"minimal_depth\":%u,"
           "\"scenario\":\"%s\",\"geometry_hash\":\"%016" PRIx64
           "\",\"profile_hash\":\"%016" PRIx64
           "\",\"seed\":\"%016" PRIx64
           "\",\"initial_hash\":\"%016" PRIx64
           "\",\"precut_hash\":\"%016" PRIx64
           "\",\"cut\":%u,\"actions\":[",
           example->schema_version,
           c33_invariant_name((enum c33_invariant_id)example->invariant_id),
           c33_broken_name((enum c33_broken_variant)
                           example->broken_variant),
           example->minimal_depth, c33_family_name(example->family),
           example->geometry_hash, example->profile_hash, example->seed,
           example->initial_hash, example->precut_hash, example->cut_kind);
    for (index = 0; index < example->action_count; ++index) {
        printf("%s\"%s\"", index == 0 ? "" : ",",
               c33_action_name((enum c33_model_action_kind)
                               example->action[index]));
    }
    printf("],\"frozen\":{\"virtual_ticks\":%u},"
           "\"resource_intervals\":\"canonical\","
           "\"before_media_hash\":\"%016" PRIx64
           "\",\"after_media_hash\":\"%016" PRIx64
           "\",\"event_hash\":\"%016" PRIx64
           "\",\"oracle_hash\":\"%016" PRIx64
           "\",\"expected\":\"named invariant passes\","
           "\"actual_violation_mask\":\"%08" PRIx32 "\"}\n",
           example->minimal_depth, example->media_before_hash,
           example->media_after_hash, example->event_hash,
           example->oracle_hash, example->violation_mask);
}

int main(void)
{
    uint64_t aggregate = UINT64_C(1469598103934665603);
    unsigned int broken;

    for (broken = C33_BM_GEOMETRY_ALIAS_OOB;
         broken <= C33_BM_HOST_CACHE_SELECTS_PPA; ++broken) {
        struct c33_counterexample example;
        unsigned int action;

        if (!c33_model_counterexample(
                (enum c33_broken_variant)broken, &example) ||
            example.action_count != example.minimal_depth ||
            (example.violation_mask &
             (UINT32_C(1) << (broken - 1u))) == 0) {
            fprintf(stderr, "C3.3 broken model failed for %s\n",
                    c33_broken_name((enum c33_broken_variant)broken));
            return 1;
        }
        print_counterexample(&example);
        aggregate = hash_u64(aggregate, example.broken_variant);
        aggregate = hash_u64(aggregate, example.minimal_depth);
        aggregate = hash_u64(aggregate, example.precut_hash);
        aggregate = hash_u64(aggregate, example.media_after_hash);
        aggregate = hash_u64(aggregate, example.event_hash);
        aggregate = hash_u64(aggregate, example.oracle_hash);
        for (action = 0; action < example.action_count; ++action) {
            aggregate = hash_u64(aggregate, example.action[action]);
        }
    }
    printf("C3.3 broken variants: PASS (18 shortest counterexamples, "
           "hash=%016" PRIx64 ")\n", aggregate);
    return 0;
}
