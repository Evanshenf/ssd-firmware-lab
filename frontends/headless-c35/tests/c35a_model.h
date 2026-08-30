/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35A_MODEL_H
#define FWLAB_C35A_MODEL_H

#include <stdint.h>

#define C35A_MODEL_FAMILIES 16u
#define C35A_MODEL_STATE_CAP 8192u
#define C35A_MODEL_DEPTH_CAP 20u
#define C35A_MODEL_SUCCESSOR_CAP 6u

struct c35a_model_result {
    uint32_t states;
    uint32_t transitions;
    uint32_t max_depth;
    uint8_t violation;
    uint8_t terminal_reached;
    uint8_t cap_reached;
    uint8_t reserved;
    char path[512];
};

const char *c35a_invariant_name(unsigned int family);
const char *c35a_mutation_name(unsigned int family);
int c35a_model_explore(
    unsigned int family,
    int mutation_enabled,
    struct c35a_model_result *result
);

#endif
