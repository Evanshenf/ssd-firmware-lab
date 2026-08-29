/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_HEADLESS_H
#define FWLAB_C35_HEADLESS_H

#include "c35_binding.h"
#include "c35_trace.h"
#include "fwlab/portable/c31.h"

struct c35_headless {
    struct fwlab_c31 *lifecycle;
    struct c35_binding binding;
    struct c35_trace *trace;
    uint64_t instance_nonce;
    uint64_t next_request;
    uint32_t owner_epoch;
    uint8_t actor;
    uint8_t admission_open;
    uint8_t teardown_complete;
    uint8_t reserved;
};

struct c35_submission {
    struct fwlab_c31_request_token request;
    struct fwlab_c31_command_handle command;
    uint32_t owner_epoch;
};

enum c35_result c35_headless_init(
    struct c35_headless *headless,
    struct fwlab_c31 *lifecycle,
    const struct c35_binding *binding,
    struct c35_trace *trace,
    uint64_t instance_nonce,
    uint32_t owner_epoch,
    uint8_t actor
);

enum c35_result c35_headless_submit(
    struct c35_headless *headless,
    const struct c35_request *request,
    struct fwlab_c31_command_handle *command
);

enum c35_result c35_headless_submit_observed(
    struct c35_headless *headless,
    const struct c35_request *request,
    struct c35_submission *submission
);

enum c35_result c35_headless_complete(
    struct c35_headless *headless,
    const struct fwlab_c31_command_handle *command,
    struct c35_semantic_result *semantic,
    struct fwlab_c31_completion_intent *intent
);

enum c35_result c35_headless_pump_quiescent(
    struct c35_headless *headless,
    uint32_t limit
);
enum c35_result c35_headless_reset(struct c35_headless *headless, uint32_t limit);
enum c35_result c35_headless_teardown(
    struct c35_headless *headless,
    uint32_t limit
);

#endif
