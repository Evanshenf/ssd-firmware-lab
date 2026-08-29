/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_NFC_TEST_ORACLE_H
#define FWLAB_NFC_TEST_ORACLE_H

#include <stdint.h>

#define C33_INVARIANTS 18u
#define C33_FAMILIES 13u
#define C33_ACTIONS 14u
#define C33_MODEL_MAX_DEPTH 28u
#define C33_MODEL_MAX_STATES 131072u
#define C33_MODEL_HASH_SLOTS 262144u
#define C33_MODEL_MAX_CUTS 262144u
#define C33_MODEL_MAX_TERMINALS 65536u

enum c33_invariant_id {
    C33_N_GEOMETRY = 1,
    C33_N_IDENTITY = 2,
    C33_N_PROGRAM_ERASED = 3,
    C33_N_BIT_MONOTONIC = 4,
    C33_N_NO_PARTIAL_CLAIM = 5,
    C33_N_ERASE_SCOPE = 6,
    C33_N_PAGE_OOB = 7,
    C33_N_BAD_BLOCK = 8,
    C33_N_ECC = 9,
    C33_N_RETRY = 10,
    C33_N_WEAR = 11,
    C33_N_SEED_REPLAY = 12,
    C33_N_TIME_SERIAL = 13,
    C33_N_CUT = 14,
    C33_N_EPOCH = 15,
    C33_N_ISOLATION = 16,
    C33_N_PROVIDER_EQUIV = 17,
    C33_N_NO_FTL_HOST = 18
};

enum c33_broken_variant {
    C33_BROKEN_NONE = 0,
    C33_BM_GEOMETRY_ALIAS_OOB = 1,
    C33_BM_CACHE_MATCH_PLANE_ONLY = 2,
    C33_BM_PROGRAM_CHECK_SUBMIT_ONLY = 3,
    C33_BM_PROGRAM_ASSIGN_BYTES = 4,
    C33_BM_PARTIAL_REPORTS_SUCCESS = 5,
    C33_BM_ERASE_IGNORES_PLANE = 6,
    C33_BM_PROGRAM_DROPS_OOB = 7,
    C33_BM_ERASE_CLEARS_BAD = 8,
    C33_BM_ECC_STRICT_LT = 9,
    C33_BM_RETRY_OMITS_STEP = 10,
    C33_BM_WEAR_GT_NOT_GE = 11,
    C33_BM_FAULT_XORS_VIRTUAL_NOW = 12,
    C33_BM_FORGET_LUN_TAIL = 13,
    C33_BM_CUT_ROLLBACK_COMMITTED = 14,
    C33_BM_EVENT_MATCH_SLOT_ONLY = 15,
    C33_BM_GLOBAL_MEDIA_CONTEXT = 16,
    C33_BM_FAKE_LOSES_RAW_STATUS = 17,
    C33_BM_HOST_CACHE_SELECTS_PPA = 18
};

enum c33_model_action_kind {
    C33_ACT_REGISTER_DESCRIPTOR = 0,
    C33_ACT_SUBMIT_READ_TRIGGER = 1,
    C33_ACT_SUBMIT_READ_TRANSFER = 2,
    C33_ACT_SUBMIT_PROGRAM_TRANSFER = 3,
    C33_ACT_SUBMIT_PROGRAM_EXECUTE = 4,
    C33_ACT_SUBMIT_ERASE = 5,
    C33_ACT_SUBMIT_STATUS = 6,
    C33_ACT_CANCEL = 7,
    C33_ACT_START_READY = 8,
    C33_ACT_ADVANCE_TIME = 9,
    C33_ACT_COMMIT_EFFECT_UNIT = 10,
    C33_ACT_FINISH_OPERATION = 11,
    C33_ACT_POLL_EVENT = 12,
    C33_ACT_RESET_BEGIN = 13
};

struct c33_model_report {
    uint32_t family_runs;
    uint32_t base_states;
    uint32_t terminal_states;
    uint32_t cut_checks;
    uint32_t duplicate_states;
    uint32_t hash_collisions;
    uint32_t max_depth;
    uint32_t action_coverage;
    uint32_t invariant_coverage;
    uint64_t aggregate_hash;
};

struct c33_counterexample {
    uint16_t schema_version;
    uint8_t invariant_id;
    uint8_t broken_variant;
    uint8_t family;
    uint8_t minimal_depth;
    uint8_t cut_kind;
    uint8_t action_count;
    uint32_t violation_mask;
    uint64_t geometry_hash;
    uint64_t profile_hash;
    uint64_t seed;
    uint64_t initial_hash;
    uint64_t precut_hash;
    uint64_t media_before_hash;
    uint64_t media_after_hash;
    uint64_t event_hash;
    uint64_t oracle_hash;
    uint8_t action[C33_MODEL_MAX_DEPTH];
};

int c33_model_positive(struct c33_model_report *report);
int c33_model_counterexample(
    enum c33_broken_variant broken,
    struct c33_counterexample *counterexample
);

const char *c33_invariant_name(enum c33_invariant_id invariant);
const char *c33_broken_name(enum c33_broken_variant broken);
const char *c33_action_name(enum c33_model_action_kind action);
const char *c33_family_name(unsigned int family);

#endif
