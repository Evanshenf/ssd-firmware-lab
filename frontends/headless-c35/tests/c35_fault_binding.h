/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_FAULT_BINDING_H
#define FWLAB_C35_FAULT_BINDING_H

#include "../c35_binding.h"
#include "c35_fault_lifecycle.h"

enum c35_binding_cut {
    C35_BINDING_CUT_NONE = 0,
    C35_BINDING_CUT_REGISTRATION_PREPARE,
    C35_BINDING_CUT_REGISTRATION_COMMIT,
    C35_BINDING_CUT_REGISTRATION_QUERY,
    C35_BINDING_CUT_REGISTRATION_ABORT,
    C35_BINDING_CUT_RESULT_PREPARE,
    C35_BINDING_CUT_RESULT_QUERY,
    C35_BINDING_CUT_RESULT_ABORT,
    C35_BINDING_CUT_RESULT_ACK,
    C35_BINDING_CUT_RESET_RECOVER,
    C35_BINDING_CUT_RESET_QUERY,
    C35_BINDING_CUT_TRANSACTION_RETIRE,
    C35_BINDING_CUT_TEARDOWN_FINALIZE,
    C35_BINDING_CUT_SEMANTIC_SNAPSHOT,
    C35_BINDING_CUT_QUIESCENT,
    C35_BINDING_CUT_COUNT
};

struct c35_fault_binding {
    struct c35_binding inner;
    uint32_t calls[C35_BINDING_CUT_COUNT];
    uint32_t occurrence;
    enum c35_result injected_result;
    uint8_t target;
    uint8_t effect;
    uint8_t injected;
    uint8_t corrupt_candidate;
};

int c35_fault_binding_init(
    struct c35_fault_binding *fault,
    const struct c35_binding *inner,
    enum c35_binding_cut target,
    enum c35_fault_effect effect,
    uint32_t occurrence,
    enum c35_result injected_result
);
struct c35_binding c35_fault_binding_provider(struct c35_fault_binding *fault);
int c35_fault_binding_corrupt_result_init(
    struct c35_fault_binding *fault,
    const struct c35_binding *inner
);

#endif
