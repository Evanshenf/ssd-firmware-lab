/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35a_model.h"

#include <stdio.h>

int main(void)
{
    uint32_t states = 0;
    uint32_t transitions = 0;
    uint32_t depth = 0;
    unsigned int family;

    for (family = 0; family < C35A_MODEL_FAMILIES; ++family) {
        struct c35a_model_result result;

        if (!c35a_model_explore(family, 0, &result) || result.violation ||
            result.cap_reached || !result.terminal_reached) {
            fprintf(stderr, "C3.5b model: FAIL: %s\n",
                    c35a_invariant_name(family));
            return 1;
        }
        states += result.states;
        transitions += result.transitions;
        if (result.max_depth > depth) depth = result.max_depth;
        printf("{\"invariant\":\"%s\",\"states\":%u,"
               "\"transitions\":%u,\"max_depth\":%u}\n",
               c35a_invariant_name(family), result.states,
               result.transitions, result.max_depth);
    }
    printf("C3.5b remediation model: PASS (16 invariants / %u states / "
           "%u transitions / depth<=%u / cap=%u / successors<=%u)\n",
           states, transitions, depth, C35A_MODEL_STATE_CAP,
           C35A_MODEL_SUCCESSOR_CAP);
    return 0;
}
