/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C42_MODEL_H
#define FWLAB_C42_MODEL_H

#include <stddef.h>
#include <stdint.h>

#define C42_MODEL_FAMILIES 12u
#define C42_MODEL_MUTANTS 20u

struct c42_model_summary {
    uint32_t families;
    uint32_t states;
    uint32_t transitions;
    uint32_t maximum_depth;
    uint32_t maximum_successors;
};

int c42_model_explore(struct c42_model_summary *summary);
const char *c42_model_mutant_name(uint32_t mutant);
int c42_model_find_counterexample(
    uint32_t mutant,
    uint32_t *depth,
    char *path,
    size_t path_size
);

#endif
