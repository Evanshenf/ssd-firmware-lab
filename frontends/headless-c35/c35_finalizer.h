/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_FINALIZER_H
#define FWLAB_C35_FINALIZER_H

#include "c35_bundle.h"
#include "c35_headless.h"

#define C35_FINALIZER_VERSION 2u

enum c35_finalizer_phase {
    C35_FINALIZER_HEADLESS = 1,
    C35_FINALIZER_HEADLESS_RETIRE = 2,
    C35_FINALIZER_PENDING_RETIRE = 3,
    C35_FINALIZER_BUNDLE_RELEASE = 4,
    C35_FINALIZER_BUNDLE_QUERY = 5,
    C35_FINALIZER_DONE = 6
};

struct c35_finalizer {
    struct c35_headless *headless;
    struct c35_bundle *bundle;
    uint64_t claimant;
    struct c35_operation_token token;
    struct c35_operation_token pending_token;
    struct c35_operation_status authoritative;
    uint32_t phase;
    uint32_t outcome;
    uint32_t commit_state;
    uint32_t cleanup_state;
    uint32_t cause_domain;
    uint32_t cause_code;
    uint8_t used;
    uint8_t finished;
    uint8_t headless_retired;
    uint8_t pending_retire;
    uint8_t bundle_released;
    uint8_t retry_class;
    uint8_t reserved[2];
};

enum c35_result c35_finalizer_start(
    struct c35_finalizer *finalizer,
    struct c35_headless *headless,
    struct c35_bundle *bundle,
    uint64_t claimant,
    struct c35_operation_token *token
);
enum c35_result c35_finalizer_progress(
    struct c35_finalizer *finalizer,
    const struct c35_operation_token *token,
    uint32_t budget,
    struct c35_operation_status *status
);
enum c35_result c35_finalizer_query(
    const struct c35_finalizer *finalizer,
    const struct c35_operation_token *token,
    struct c35_operation_status *status
);
enum c35_result c35_finalizer_finalize(
    const struct c35_finalizer *finalizer,
    const struct c35_operation_token *token,
    struct c35_operation_status *status
);
enum c35_result c35_finalizer_retire(
    struct c35_finalizer *finalizer,
    const struct c35_operation_token *token
);

#endif
