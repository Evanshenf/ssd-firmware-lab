/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_oracle.h"

#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    struct c34o_metrics metrics;

    if (!c34o_run_positive(&metrics)) {
        return 1;
    }
    puts("C3.4 bounded integration model: PASS");
    printf("  families=%" PRIu32 " states=%" PRIu32
           " cuts=%" PRIu32 " terminals=%" PRIu32 "\n",
           metrics.families, metrics.states, metrics.cuts,
           metrics.terminals);
    printf("  invariants=%08" PRIx32 " hash=%016" PRIx64 "\n",
           metrics.invariant_mask, metrics.hash);
    return 0;
}
