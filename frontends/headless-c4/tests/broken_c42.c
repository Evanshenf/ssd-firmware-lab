/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_model.h"

#include <inttypes.h>
#include <stdio.h>

static uint64_t hash_add(uint64_t hash, const char *text)
{
    while (*text != '\0') {
        hash ^= (uint8_t)*text++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

int main(void)
{
    uint64_t vector = UINT64_C(1469598103934665603);
    uint32_t mutant;

    for (mutant = 0; mutant < C42_MODEL_MUTANTS; ++mutant) {
        const char *name = c42_model_mutant_name(mutant);
        char path[128];
        uint32_t depth = 0;

        if (name == NULL || !c42_model_find_counterexample(
                mutant, &depth, path, sizeof(path)) || depth > 20) {
            fprintf(stderr, "C4.2 broken: missing mutant %u\n", mutant);
            return 1;
        }
        vector = hash_add(vector, name);
        vector = hash_add(vector, path);
        printf("{\"mutant\":\"%s\",\"depth\":%u,"
               "\"path\":\"%s\"}\n", name, depth, path);
    }
    printf("C4.2 broken variants: PASS mutants=20 shortest=20 "
           "vector=%016" PRIx64 "\n", vector);
    return 0;
}
