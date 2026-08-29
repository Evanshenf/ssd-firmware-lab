/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_model.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

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
    uint64_t vector_hash = UINT64_C(1469598103934665603);
    unsigned int mutant;

    for (mutant = 0; mutant < C35_MODEL_MUTANTS; ++mutant) {
        const char *name = c35_model_mutant_name(mutant);
        uint32_t depth;
        char path[96];

        memset(path, 0, sizeof(path));
        if (name == NULL || !c35_model_find_counterexample(
                mutant, &depth, path, sizeof(path)) || depth > 16) {
            fprintf(stderr, "missing counterexample for mutant %u\n",
                    mutant);
            return 1;
        }
        vector_hash = hash_add(vector_hash, name);
        vector_hash = hash_add(vector_hash, path);
        printf("{\"mutant\":\"%s\",\"depth\":%u,"
               "\"path\":\"%s\"}\n", name, depth, path);
    }
    printf("C3.5 broken variants: PASS (16 shortest counterexamples, "
           "vector=%016" PRIx64 ")\n", vector_hash);
    return 0;
}
