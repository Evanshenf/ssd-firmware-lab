/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <inttypes.h>
#include <stdio.h>

#include "../c32_internal.h"

int main(void)
{
    struct c32_model_report report;

    if (!c32_model_run_positive(&report)) {
        fprintf(stderr,
                "C3.2 exhaustive model failed: runs=%" PRIu32
                " rejected=%" PRIu32 " states=%" PRIu32
                " terminals=%" PRIu32 " cuts=%" PRIu32
                " depth=%" PRIu32 " actions=%08" PRIx32
                " invariants=%04x hash=%016" PRIx64 "\n",
                report.scenario_runs, report.rejected_configurations,
                report.base_states, report.terminal_states,
                report.recovery_checks, report.max_depth,
                report.action_coverage, report.invariant_coverage,
                report.aggregate_hash);
        return 1;
    }
    printf("C3.2 exhaustive model: PASS (runs=%" PRIu32
           ", rejected=%" PRIu32 ", states=%" PRIu32
           ", terminals=%" PRIu32 ", cuts=%" PRIu32
           ", collisions=%" PRIu32 ", duplicates=%" PRIu32
           ", max-depth=%" PRIu32 ", actions=%08" PRIx32
           ", invariants=%04x, hash=%016" PRIx64 ")\n",
           report.scenario_runs, report.rejected_configurations,
           report.base_states, report.terminal_states,
           report.recovery_checks, report.canonical_collisions,
           report.duplicate_states, report.max_depth,
           report.action_coverage, report.invariant_coverage,
           report.aggregate_hash);
    return 0;
}
