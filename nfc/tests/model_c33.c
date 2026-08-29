/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <inttypes.h>
#include <stdio.h>

#include "c33_oracle.h"

int main(void)
{
    struct c33_model_report report;

    if (!c33_model_positive(&report)) {
        fprintf(stderr,
                "C3.3 bounded model failed: families=%" PRIu32
                " states=%" PRIu32 " cuts=%" PRIu32
                " actions=%08" PRIx32 " invariants=%08" PRIx32 "\n",
                report.family_runs, report.base_states, report.cut_checks,
                report.action_coverage, report.invariant_coverage);
        return 1;
    }
    printf("C3.3 bounded model: PASS (families=%" PRIu32
           ", states=%" PRIu32 ", terminals=%" PRIu32
           ", cuts=%" PRIu32 ", duplicates=%" PRIu32
           ", collisions=%" PRIu32 ", max-depth=%" PRIu32
           ", actions=%08" PRIx32 ", invariants=%08" PRIx32
           ", hash=%016" PRIx64 ")\n",
           report.family_runs, report.base_states, report.terminal_states,
           report.cut_checks, report.duplicate_states,
           report.hash_collisions, report.max_depth,
           report.action_coverage, report.invariant_coverage,
           report.aggregate_hash);
    return 0;
}
