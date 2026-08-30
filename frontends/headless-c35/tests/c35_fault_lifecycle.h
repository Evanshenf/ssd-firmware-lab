/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_FAULT_LIFECYCLE_H
#define FWLAB_C35_FAULT_LIFECYCLE_H

#include "../c35_lifecycle_port.h"

enum c35_lifecycle_cut {
    C35_LIFECYCLE_CUT_NONE = 0,
    C35_LIFECYCLE_CUT_SUBMIT,
    C35_LIFECYCLE_CUT_STEP,
    C35_LIFECYCLE_CUT_COMMAND_STATE,
    C35_LIFECYCLE_CUT_COMPLETION_ACQUIRE,
    C35_LIFECYCLE_CUT_COMPLETION_RELEASE,
    C35_LIFECYCLE_CUT_COMPLETION_CONSUME,
    C35_LIFECYCLE_CUT_ABORT_REQUEST,
    C35_LIFECYCLE_CUT_ABORT_QUERY,
    C35_LIFECYCLE_CUT_ABORT_ACK,
    C35_LIFECYCLE_CUT_RESET_BEGIN,
    C35_LIFECYCLE_CUT_RESET_ACK,
    C35_LIFECYCLE_CUT_TEARDOWN_BEGIN,
    C35_LIFECYCLE_CUT_TEARDOWN_ACK,
    C35_LIFECYCLE_CUT_COUNT
};

enum c35_fault_effect {
    C35_FAULT_BEFORE_EFFECT = 0,
    C35_FAULT_AFTER_EFFECT = 1
};

struct c35_fault_lifecycle {
    struct c35_lifecycle_port inner;
    uint32_t calls[C35_LIFECYCLE_CUT_COUNT];
    uint32_t occurrence;
    enum fwlab_c31_api_result injected_result;
    uint8_t target;
    uint8_t effect;
    uint8_t injected;
    uint8_t reserved;
};

int c35_fault_lifecycle_init(
    struct c35_fault_lifecycle *fault,
    const struct c35_lifecycle_port *inner,
    enum c35_lifecycle_cut target,
    enum c35_fault_effect effect,
    uint32_t occurrence,
    enum fwlab_c31_api_result injected_result
);
struct c35_lifecycle_port c35_fault_lifecycle_port(
    struct c35_fault_lifecycle *fault
);

#endif
