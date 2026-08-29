/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PRIVATE_C34_PHYSICAL_TXN_H
#define FWLAB_PRIVATE_C34_PHYSICAL_TXN_H

#include <stdbool.h>
#include <stdint.h>

#include "fwlab/portable/c31_types.h"
#include "fwlab/portable/nfc_types.h"
#include "fwlab/contracts/persistence_facts.h"

#define C34_PHYSICAL_TXN_VERSION 1u

enum c34_physical_txn_result {
    C34_PHYSICAL_TXN_OK = 0,
    C34_PHYSICAL_TXN_INVALID = 1,
    C34_PHYSICAL_TXN_BUSY = 2,
    C34_PHYSICAL_TXN_NOT_FOUND = 3,
    C34_PHYSICAL_TXN_STALE = 4,
    C34_PHYSICAL_TXN_IO = 5,
    C34_PHYSICAL_TXN_CORRUPT = 6
};

struct c34_physical_binding {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint64_t physical_op_id;
    uint64_t commit_sequence;
    struct fwlab_nfc_operation_token inner;
    struct fwlab_c31_operation_token outer;
    struct fwlab_persist_mutation_token mutation;
    struct fwlab_nfc_ppa ppa;
    uint64_t payload_digest;
    uint32_t main_length;
    uint32_t oob_length;
    uint8_t operation_kind;
    uint8_t reserved1[7];
};

struct c34_physical_receipt {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint64_t physical_op_id;
    uint64_t commit_sequence;
    struct fwlab_nfc_operation_token inner;
    struct fwlab_nfc_ppa ppa;
    uint64_t payload_digest;
    uint64_t physical_generation;
    uint32_t applied_main_bytes;
    uint32_t applied_oob_bytes;
    uint32_t applied_pages;
    uint16_t base_erase_generation;
    uint16_t final_erase_generation;
    uint8_t operation_kind;
    uint8_t physical_outcome;
    uint8_t integrity;
    uint8_t applied_region_mask;
    uint8_t committed;
    uint8_t reserved1[7];
};

typedef enum c34_physical_txn_result
(*c34_physical_bind_fn)(
    void *context,
    const struct c34_physical_binding *binding
);

typedef enum c34_physical_txn_result
(*c34_physical_abandon_fn)(
    void *context,
    const struct fwlab_nfc_operation_token *inner
);

typedef enum c34_physical_txn_result
(*c34_physical_receipt_fn)(
    void *context,
    const struct fwlab_nfc_operation_token *inner,
    struct c34_physical_receipt *receipt
);

typedef enum c34_physical_txn_result
(*c34_physical_quiescent_fn)(void *context, bool *quiescent);

struct c34_physical_txn_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    c34_physical_bind_fn bind;
    c34_physical_abandon_fn abandon;
    c34_physical_receipt_fn receipt;
    c34_physical_quiescent_fn quiescent;
};

struct c34_physical_txn_provider {
    const struct c34_physical_txn_ops *ops;
    void *context;
};

#endif
