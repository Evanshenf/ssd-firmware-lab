/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35a_model.h"

#include <stdio.h>

int main(void)
{
    unsigned int family;

    for (family = 0; family < C35A_MODEL_FAMILIES; ++family) {
        struct c35a_model_result result;

        if (!c35a_model_explore(family, 1, &result) || !result.violation ||
            result.cap_reached || result.path[0] == '\0') {
            fprintf(stderr, "C3.5b broken: FAIL: %s\n",
                    c35a_mutation_name(family));
            return 1;
        }
        printf("{\"mutant\":\"%s\",\"invariant\":\"%s\","
               "\"depth\":%u,\"path\":\"%s\"}\n",
               c35a_mutation_name(family), c35a_invariant_name(family),
               result.max_depth, result.path);
    }
    puts("C3.5b broken variants: PASS (16 shortest counterexamples)");
    return 0;
}
