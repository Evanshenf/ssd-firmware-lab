/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_PRIVATE_EXECUTION_PROGRESS_H
#define FWLAB_PRIVATE_EXECUTION_PROGRESS_H

#include <stdint.h>

/* Private per-call scheduling facts, not a wire/observer ABI or authority.
 * advanced: work/effect/ownership actually changed during this call.
 * runnable: at return, a bounded local next step is known not to require an
 * external event. False may mean unknown/waiting; it never proves quiescence.
 * Neither occupied records nor consumed polling budget establishes a fact. */
struct fwlab_execution_progress {
    uint32_t advanced;
    uint32_t runnable;
};

static inline int fwlab_execution_progress_valid(
    const struct fwlab_execution_progress *progress)
{
    return progress && progress->advanced <= 1 && progress->runnable <= 1;
}

#endif
