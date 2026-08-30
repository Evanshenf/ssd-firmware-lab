/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_finalizer.h"

#include <string.h>

static int token_equal(
    const struct c35_operation_token *left,
    const struct c35_operation_token *right
)
{
    return left != NULL && right != NULL &&
           left->instance_nonce == right->instance_nonce &&
           left->uid == right->uid && left->generation == right->generation &&
           left->kind == C35_OPERATION_TEARDOWN &&
           right->kind == C35_OPERATION_TEARDOWN &&
           left->reserved[0] == 0 && left->reserved[1] == 0 &&
           left->reserved[2] == 0 && right->reserved[0] == 0 &&
           right->reserved[1] == 0 && right->reserved[2] == 0;
}

static void status_fill(
    const struct c35_finalizer *finalizer,
    struct c35_operation_status *status
)
{
    memset(status, 0, sizeof(*status));
    status->version = C35_OPERATION_VERSION;
    status->size = sizeof(*status);
    status->token = finalizer->token;
    status->call_state = finalizer->finished ?
        C35_CALL_DONE : C35_CALL_IN_PROGRESS;
    status->operation_kind = C35_OPERATION_TEARDOWN;
    status->commit_state = (uint8_t)finalizer->commit_state;
    status->cleanup_state = (uint8_t)finalizer->cleanup_state;
    status->outcome = finalizer->outcome;
    status->service_phase = finalizer->headless->service_phase;
    status->internal_phase = finalizer->phase;
    status->cause_domain = finalizer->cause_domain;
    status->cause_code = finalizer->cause_code;
    status->retry_class = finalizer->retry_class;
    status->publication_valid = finalizer->authoritative.publication_valid;
    if (status->publication_valid)
        status->publication = finalizer->authoritative.publication;
}

static void finish(
    struct c35_finalizer *finalizer,
    enum c35_result outcome,
    uint32_t cleanup_state
)
{
    finalizer->outcome = outcome;
    finalizer->cleanup_state = cleanup_state;
    if (outcome == C35_OK) {
        finalizer->cause_domain = C35_CAUSE_NONE;
        finalizer->cause_code = 0;
    }
    finalizer->retry_class = cleanup_state == C35_CLEANUP_POISONED ?
        C35_RETRY_REPAIR_REQUIRED : C35_RETRY_NONE;
    finalizer->phase = C35_FINALIZER_DONE;
    finalizer->finished = 1;
}

enum c35_result c35_finalizer_start(
    struct c35_finalizer *finalizer,
    struct c35_headless *headless,
    struct c35_bundle *bundle,
    uint64_t claimant,
    struct c35_operation_token *token
)
{
    enum c35_result result;

    if (finalizer == NULL || headless == NULL || token == NULL ||
        claimant == 0 ||
        (bundle != NULL && (!bundle->claimed || bundle->claimant != claimant)))
        return C35_INVALID;
    if (finalizer->used) return C35_WRONG_STATE;
    memset(finalizer, 0, sizeof(*finalizer));
    result = c35_teardown_start(headless, token);
    if (result != C35_OK) return result;
    finalizer->headless = headless;
    finalizer->bundle = bundle;
    finalizer->claimant = claimant;
    finalizer->token = *token;
    finalizer->phase = C35_FINALIZER_HEADLESS;
    finalizer->outcome = C35_IN_PROGRESS;
    finalizer->commit_state = C35_COMMIT_IN_PROGRESS;
    finalizer->cleanup_state = C35_CLEANUP_PENDING;
    finalizer->retry_class = C35_RETRY_SAME_TOKEN;
    finalizer->used = 1;
    return C35_OK;
}

static void progress_one(struct c35_finalizer *finalizer)
{
    enum c35_result result;

    switch (finalizer->phase) {
    case C35_FINALIZER_HEADLESS:
        result = c35_operation_progress(
            finalizer->headless, &finalizer->token, 1,
            &finalizer->authoritative);
        if (result == C35_OK) {
            finalizer->outcome = finalizer->authoritative.outcome;
            finalizer->commit_state = finalizer->authoritative.commit_state;
            finalizer->cause_domain = finalizer->authoritative.cause_domain;
            finalizer->cause_code = finalizer->authoritative.cause_code;
            finalizer->retry_class = finalizer->authoritative.retry_class;
            finalizer->phase = C35_FINALIZER_HEADLESS_RETIRE;
        } else if (result != C35_IN_PROGRESS) {
            finalizer->cause_domain = C35_CAUSE_C35;
            finalizer->cause_code = result;
            finish(finalizer, result, C35_CLEANUP_POISONED);
        }
        break;
    case C35_FINALIZER_HEADLESS_RETIRE:
        result = c35_operation_retire(
            finalizer->headless, &finalizer->token);
        if (result == C35_OK || result == C35_STALE) {
            finalizer->headless_retired = 1;
            if (finalizer->outcome == C35_OK &&
                finalizer->commit_state == C35_COMMIT_COMMITTED) {
                if (finalizer->bundle == NULL) {
                    finalizer->bundle_released = 1;
                    finish(finalizer, C35_OK, C35_CLEANUP_COMPLETE);
                } else {
                    finalizer->phase = C35_FINALIZER_BUNDLE_RELEASE;
                    finalizer->cleanup_state = C35_CLEANUP_PENDING;
                    finalizer->retry_class = C35_RETRY_SAME_TOKEN;
                }
            } else {
                finish(finalizer, (enum c35_result)finalizer->outcome,
                       C35_CLEANUP_POISONED);
            }
        } else {
            finalizer->cause_domain = C35_CAUSE_BINDING;
            finalizer->cause_code = result;
            finalizer->retry_class = C35_RETRY_SAME_TOKEN;
        }
        break;
    case C35_FINALIZER_BUNDLE_RELEASE:
        result = c35_bundle_release(
            finalizer->bundle, finalizer->claimant);
        if (result == C35_OK) {
            finalizer->bundle_released = 1;
            finish(finalizer, C35_OK, C35_CLEANUP_COMPLETE);
        } else if (result == C35_IN_PROGRESS) {
            finalizer->cause_domain = C35_CAUSE_BUNDLE;
            finalizer->cause_code = result;
            finalizer->retry_class = C35_RETRY_SAME_TOKEN;
        } else {
            finalizer->cause_domain = C35_CAUSE_BUNDLE;
            finalizer->cause_code = result;
            finalizer->phase = C35_FINALIZER_BUNDLE_QUERY;
        }
        break;
    case C35_FINALIZER_BUNDLE_QUERY: {
        bool released = false;

        result = c35_bundle_release_query(
            finalizer->bundle, finalizer->claimant, &released);
        if (result == C35_OK && released) {
            finalizer->bundle_released = 1;
            finish(finalizer, C35_OK, C35_CLEANUP_COMPLETE);
        } else if (result == C35_OK) {
            finalizer->phase = C35_FINALIZER_BUNDLE_RELEASE;
            finalizer->retry_class = C35_RETRY_SAME_TOKEN;
        } else {
            finalizer->cause_domain = C35_CAUSE_BUNDLE;
            finalizer->cause_code = result;
            finish(finalizer, C35_POISONED, C35_CLEANUP_POISONED);
        }
        break;
    }
    default:
        finalizer->cause_domain = C35_CAUSE_C35;
        finalizer->cause_code = C35_INVARIANT;
        finish(finalizer, C35_INVARIANT, C35_CLEANUP_POISONED);
        break;
    }
}

enum c35_result c35_finalizer_progress(
    struct c35_finalizer *finalizer,
    const struct c35_operation_token *token,
    uint32_t budget,
    struct c35_operation_status *status
)
{
    uint32_t used = 0;

    if (finalizer == NULL || token == NULL || status == NULL)
        return C35_INVALID;
    if (!finalizer->used || !token_equal(&finalizer->token, token))
        return C35_STALE;
    while (!finalizer->finished && used < budget) {
        progress_one(finalizer);
        ++used;
    }
    status_fill(finalizer, status);
    status->units_used = used;
    return finalizer->finished ? C35_OK : C35_IN_PROGRESS;
}

enum c35_result c35_finalizer_query(
    const struct c35_finalizer *finalizer,
    const struct c35_operation_token *token,
    struct c35_operation_status *status
)
{
    if (finalizer == NULL || token == NULL || status == NULL)
        return C35_INVALID;
    if (!finalizer->used || !token_equal(&finalizer->token, token))
        return C35_STALE;
    status_fill(finalizer, status);
    status->units_used = 0;
    return finalizer->finished ? C35_OK : C35_IN_PROGRESS;
}

enum c35_result c35_finalizer_finalize(
    const struct c35_finalizer *finalizer,
    const struct c35_operation_token *token,
    struct c35_operation_status *status
)
{
    return c35_finalizer_query(finalizer, token, status);
}

enum c35_result c35_finalizer_retire(
    struct c35_finalizer *finalizer,
    const struct c35_operation_token *token
)
{
    if (finalizer == NULL || token == NULL || !finalizer->used ||
        !finalizer->finished || !token_equal(&finalizer->token, token))
        return C35_STALE;
    memset(finalizer, 0, sizeof(*finalizer));
    return C35_OK;
}
