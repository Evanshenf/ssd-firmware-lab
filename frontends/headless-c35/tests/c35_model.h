/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_MODEL_H
#define FWLAB_C35_MODEL_H

#include <stdint.h>

#define C35_MODEL_FAMILIES 13u
#define C35_MODEL_MUTANTS 16u

struct c35_model_totals {
    uint32_t families;
    uint32_t states;
    uint32_t transitions;
    uint32_t probes;
    uint32_t max_depth;
};

int c35_model_run_all(struct c35_model_totals *totals, int verbose);

const char *c35_model_mutant_name(unsigned int index);
int c35_model_find_counterexample(
    unsigned int index,
    uint32_t *depth,
    char *path,
    uint32_t path_capacity
);

#endif
