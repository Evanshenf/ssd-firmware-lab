/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C42_DUT_BFS_H
#define FWLAB_C42_DUT_BFS_H

#include <stdint.h>

struct c42_dut_bfs_summary {
    uint32_t families;
    uint32_t states;
    uint32_t transitions;
    uint32_t comparisons;
    uint32_t maximum_depth;
    uint32_t maximum_successors;
};

int c42_dut_bfs_run(
    const char *only_family,
    struct c42_dut_bfs_summary *summary
);

#endif
