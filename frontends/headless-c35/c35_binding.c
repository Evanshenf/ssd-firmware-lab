/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_binding.h"

#include <stddef.h>

void c35_cause_clear(struct c35_cause_detail *cause)
{
    if (cause == NULL) return;
    *cause = (struct c35_cause_detail){0};
    cause->version = C35_CAUSE_VERSION;
    cause->size = sizeof(*cause);
}

void c35_cause_record(
    struct c35_cause_detail *cause,
    uint8_t domain,
    uint32_t code,
    uint8_t retry_class
)
{
    c35_cause_clear(cause);
    if (cause == NULL) return;
    cause->domain = domain;
    cause->code = code;
    cause->retry_class = retry_class;
}

int c35_cause_valid(const struct c35_cause_detail *cause)
{
    return cause != NULL && cause->version == C35_CAUSE_VERSION &&
           cause->size == sizeof(*cause) && cause->domain <= C35_CAUSE_OBSERVER &&
           cause->retry_class <= C35_RETRY_REPAIR_REQUIRED &&
           cause->reserved == 0;
}

int c35_txid_equal(const struct c35_txid *left, const struct c35_txid *right)
{
    return left != NULL && right != NULL &&
           left->instance_nonce == right->instance_nonce &&
           left->uid == right->uid &&
           left->owner_epoch == right->owner_epoch &&
           left->generation == right->generation;
}

int c35_binding_valid(const struct c35_binding *binding)
{
    return binding != NULL && binding->ops != NULL &&
           binding->context != NULL &&
           binding->ops->version == C35_BINDING_OPS_VERSION &&
           binding->ops->size == sizeof(*binding->ops) &&
           binding->ops->reserved == 0 &&
           binding->ops->registration_prepare != NULL &&
           binding->ops->registration_commit != NULL &&
           binding->ops->registration_query != NULL &&
           binding->ops->registration_abort != NULL &&
           binding->ops->result_prepare != NULL &&
           binding->ops->result_query != NULL &&
           binding->ops->result_abort != NULL &&
           binding->ops->result_ack != NULL &&
           binding->ops->reset_recover != NULL &&
           binding->ops->reset_query != NULL &&
           binding->ops->transaction_retire != NULL &&
           binding->ops->teardown_finalize != NULL &&
           binding->ops->semantic_snapshot != NULL &&
           binding->ops->quiescent != NULL &&
           binding->ops->cause_query != NULL;
}
