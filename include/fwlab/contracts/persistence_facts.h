/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CONTRACTS_PERSISTENCE_FACTS_H
#define FWLAB_CONTRACTS_PERSISTENCE_FACTS_H

#include <stdint.h>

#define FWLAB_PERSIST_VERSION 1u
#define FWLAB_PERSIST_MAX_ATOMS 2u

#define FWLAB_PERSIST_EVENT_CONTROLLER_RESET UINT8_C(0x01)
#define FWLAB_PERSIST_EVENT_POWER_LOSS UINT8_C(0x02)
#define FWLAB_PERSIST_EVENT_DAEMON_CRASH UINT8_C(0x04)
#define FWLAB_PERSIST_EVENT_HOST_CRASH UINT8_C(0x08)
#define FWLAB_PERSIST_EVENT_MASK UINT8_C(0x0f)

#define FWLAB_PERSIST_FACT_CAPTURED UINT32_C(0x0001)
#define FWLAB_PERSIST_FACT_C_PHYS_APPLIED UINT32_C(0x0002)
#define FWLAB_PERSIST_FACT_C_PHYS_NO_EFFECT UINT32_C(0x0004)
#define FWLAB_PERSIST_FACT_DATA_STABLE UINT32_C(0x0008)
#define FWLAB_PERSIST_FACT_C_MAP UINT32_C(0x0010)
#define FWLAB_PERSIST_FACT_LOGICAL_DURABLE UINT32_C(0x0020)
#define FWLAB_PERSIST_FACT_PLP_ADMITTED UINT32_C(0x0040)
#define FWLAB_PERSIST_FACT_C_CKPT UINT32_C(0x0080)
#define FWLAB_PERSIST_FACT_PROVABLE_NO_COMMIT UINT32_C(0x0100)
#define FWLAB_PERSIST_FACT_INDETERMINATE UINT32_C(0x0200)
#define FWLAB_PERSIST_FACT_MASK UINT32_C(0x03ff)

enum fwlab_persist_result {
    FWLAB_PERSIST_OK = 0,
    FWLAB_PERSIST_INVALID_CONTRACT = 1,
    FWLAB_PERSIST_UNSUPPORTED_VERSION = 2
};

enum fwlab_persist_plp_kind {
    FWLAB_PERSIST_PLP_NONE = 0,
    FWLAB_PERSIST_PLP_VALIDATED = 1,
    FWLAB_PERSIST_PLP_CLAIMED_UNVALIDATED = 2
};

enum fwlab_persist_request_kind {
    FWLAB_PERSIST_DEFAULT = 0,
    FWLAB_PERSIST_SELF_DURABLE = 1,
    FWLAB_PERSIST_FENCE = 2
};

enum fwlab_persist_mutation_kind {
    FWLAB_PERSIST_WRITE = 0,
    FWLAB_PERSIST_TRIM = 1,
    FWLAB_PERSIST_RELOCATION = 2,
    FWLAB_PERSIST_CHECKPOINT = 3
};

enum fwlab_persist_closure {
    FWLAB_PERSIST_CLOSE_OPEN = 0,
    FWLAB_PERSIST_CLOSE_C_MAP = 1,
    FWLAB_PERSIST_CLOSE_PLP = 2,
    FWLAB_PERSIST_CLOSE_NO_COMMIT = 3,
    FWLAB_PERSIST_CLOSE_INDETERMINATE = 4
};

enum fwlab_persist_witness_class {
    FWLAB_PERSIST_WITNESS_NONE = 0,
    FWLAB_PERSIST_VOLATILE_ELIGIBLE = 1,
    FWLAB_PERSIST_DURABLE_ELIGIBLE = 2,
    FWLAB_PERSIST_FAILED_INDETERMINATE = 3
};

enum fwlab_persist_reason {
    FWLAB_PERSIST_REASON_NONE = 0,
    FWLAB_PERSIST_REASON_MISSING_ATOM = 1,
    FWLAB_PERSIST_REASON_MISSING_DURABLE_FACT = 2,
    FWLAB_PERSIST_REASON_INVALID_PLP = 3,
    FWLAB_PERSIST_REASON_PLP_CAPACITY = 4,
    FWLAB_PERSIST_REASON_OPEN_OBLIGATION = 5,
    FWLAB_PERSIST_REASON_INDETERMINATE = 6,
    FWLAB_PERSIST_REASON_IDENTITY_MISMATCH = 7
};

enum fwlab_persist_invariant_id {
    FWLAB_P_UNIQUE = 1,
    FWLAB_P_NO_TORN = 2,
    FWLAB_P_DEPEND = 3,
    FWLAB_P_DURABLE_FLOOR = 4,
    FWLAB_P_VOLATILE_BOUND = 5,
    FWLAB_P_TRIM = 6,
    FWLAB_P_GC = 7,
    FWLAB_P_CHECKPOINT = 8,
    FWLAB_P_EPOCH = 9,
    FWLAB_P_FENCE = 10,
    FWLAB_P_PLP = 11,
    FWLAB_P_CONSERVE = 12,
    FWLAB_P_NO_HOST_AUTHORITY = 13
};

struct fwlab_persist_mutation_token {
    uint64_t word[2];
};

struct fwlab_persist_profile {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint8_t cache_enabled;
    uint8_t plp_kind;
    uint8_t plp_capacity_credits;
    uint8_t survival_event_mask;
    uint32_t reserved1;
};

struct fwlab_persist_request {
    uint16_t version;
    uint16_t size;
    uint8_t kind;
    uint8_t atom_mask;
    uint16_t reserved0;
    struct fwlab_persist_mutation_token token;
    uint32_t owner_epoch;
    uint32_t scope;
    uint32_t sequence;
    uint32_t frontier;
    uint32_t reserved1[2];
};

struct fwlab_persist_atom_fact {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_persist_mutation_token token;
    uint32_t owner_epoch;
    uint32_t scope;
    uint32_t sequence;
    uint32_t fact_mask;
    uint8_t atom;
    uint8_t logical_version;
    uint8_t predecessor_version;
    uint8_t mutation_kind;
    uint8_t closure;
    uint8_t reserved1[3];
};

#define FWLAB_PLP_BODY_COMPLETE UINT8_C(0x01)
#define FWLAB_PLP_CHECKSUM_OK UINT8_C(0x02)
#define FWLAB_PLP_COMMIT_MARKER UINT8_C(0x04)
#define FWLAB_PLP_CAPACITY_RESERVED UINT8_C(0x08)
#define FWLAB_PLP_DRAIN_BUDGET_RESERVED UINT8_C(0x10)
#define FWLAB_PLP_REQUIRED_FLAGS UINT8_C(0x1f)

struct fwlab_persist_plp_envelope {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_persist_mutation_token token;
    uint32_t owner_epoch;
    uint32_t sequence;
    uint8_t atom_mask;
    uint8_t capacity_cost;
    uint8_t flags;
    uint8_t survival_event_mask;
    uint8_t drained_atom_mask;
    uint8_t reserved1[3];
    uint32_t persistent_order;
};

struct fwlab_persist_obligation {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_persist_mutation_token token;
    uint32_t owner_epoch;
    uint32_t scope;
    uint32_t sequence;
    uint32_t fact_mask;
    uint8_t atom_mask;
    uint8_t closure;
    uint8_t reserved1[2];
};

struct fwlab_persist_witness {
    uint16_t version;
    uint16_t size;
    uint8_t witness_class;
    uint8_t required_atom_mask;
    uint8_t satisfied_atom_mask;
    uint8_t reserved0;
    uint32_t reason;
    uint32_t reserved1;
};

#endif
