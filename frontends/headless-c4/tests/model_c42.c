/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_model.h"

#include <stdio.h>

int main(void)
{
    struct c42_model_summary summary = {0};

    if (!c42_model_explore(&summary)) {
        fprintf(stderr, "C4.2 model: FAIL\n");
        return 1;
    }
    printf("C4.2 bounded model: PASS families=%u states=%u transitions=%u "
           "depth=%u successors=%u caps=32768/262144/20/8\n",
           summary.families, summary.states, summary.transitions,
           summary.maximum_depth, summary.maximum_successors);
    return 0;
}
