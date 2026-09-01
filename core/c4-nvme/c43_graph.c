/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_internal.h"

_Static_assert(FWLAB_C43_PHASE_RETIRED_TOMBSTONE == 13,
               "C4.3 observer must retain exactly 14 phases");
_Static_assert(FWLAB_C43_MAX_COMMANDS * FWLAB_C43_ACTIONS_PER_COMMAND ==
                   FWLAB_C43_MAX_ACTIONS,
               "C4.3 action capacity mismatch");

enum fwlab_c43_graph_result fwlab_c43_graph_step(
    struct fwlab_c43_graph *graph,
    uint32_t budget,
    struct fwlab_c43_step_result *result)
{
    if (!c43_graph_valid(graph) || result == NULL || budget == 0 ||
        budget > graph->config.ordinary_progress_maximum) {
        return FWLAB_C43_GRAPH_INVALID;
    }
    /* Phase 1 freezes layout and admission bounds before behavior. */
    return FWLAB_C43_GRAPH_NOT_IMPLEMENTED;
}
