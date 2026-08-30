/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_C34_BINDING_H
#define FWLAB_C35_C34_BINDING_H

#include "../c35_binding.h"
#include "../../../core/c34/c34.h"

#define C35_CREDIT_VERSION 2u
#define C35_C34_INNER_CREDIT_LIMIT 32u
#define C35_C34_PHYSICAL_CREDIT_LIMIT 16u
#define C35_C34_CACHE_CREDIT_LIMIT 32u
#define C35_C34_RECORD_CREDIT_LIMIT 15u

struct c35_persistent_credits {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    uint32_t record_used;
    uint8_t known;
    uint8_t reserved1[3];
};

static inline void c35_persistent_credits_init(
    struct c35_persistent_credits *credits
)
{
    if (!credits) return;
    *credits = (struct c35_persistent_credits){0};
    credits->version = C35_CREDIT_VERSION;
    credits->size = sizeof(*credits);
    credits->known = 1;
}

struct c35_c34_registration {
    uint8_t used;
    uint8_t state;
    uint16_t reserved;
    struct c35_txid txid;
    struct fwlab_c31_request_token token;
    struct fwlab_c31_command_handle command;
    struct c34_request request;
    uint16_t inner_credits;
    uint16_t physical_credits;
    uint16_t cache_credits;
    uint16_t record_credits;
    uint8_t credits_reserved;
    uint8_t reserved1[7];
};

struct c35_c34_result_entry {
    uint8_t used;
    uint8_t state;
    uint16_t registration_index;
    struct c35_txid txid;
    struct fwlab_c31_command_handle command;
    struct c35_semantic_result semantic;
};

struct c35_c34_binding {
    struct c34 *firmware;
    struct c35_persistent_credits *persistent;
    uint64_t instance_nonce;
    uint32_t owner_epoch;
    uint32_t reserved;
    uint32_t inner_used;
    uint32_t physical_used;
    uint32_t physical_sequence_used;
    uint32_t cache_used;
    uint32_t nfc_uid_used;
    uint32_t nfc_generation_used;
    uint32_t nfc_submit_used;
    uint32_t nfc_trace_used;
    uint32_t nfc_tick_used;
    struct c35_c34_registration registration[C35_BINDING_SLOTS];
    struct c35_c34_result_entry result[C35_BINDING_SLOTS];
    struct c35_txid reset_txid;
    struct c35_cause_detail cause;
    uint8_t reset_state;
    uint8_t reserved1[7];
};

enum c35_result c35_c34_binding_init(
    struct c35_c34_binding *binding,
    struct c34 *firmware,
    struct c35_persistent_credits *persistent,
    uint64_t instance_nonce,
    uint32_t owner_epoch
);
struct c35_binding c35_c34_binding_provider(
    struct c35_c34_binding *binding
);

#endif
