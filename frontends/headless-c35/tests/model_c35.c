/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_model.h"

#include <stdio.h>

int main(void)
{
    struct c35_model_totals totals;

    if (!c35_model_run_all(&totals, 1)) {
        fputs("C3.5 composition model: FAIL\n", stderr);
        return 1;
    }
    printf("C3.5 composition model: PASS (%u families / %u states / "
           "%u transitions / %u stale probes / depth<=%u)\n",
           totals.families, totals.states, totals.transitions,
           totals.probes, totals.max_depth);
    return 0;
}
