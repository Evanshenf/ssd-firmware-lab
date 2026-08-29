/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CORE_C32_INTERNAL_H
#define FWLAB_CORE_C32_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fwlab/contracts/persistence_facts.h"

#define C32_ATOMS 2u
#define C32_GROUPS 3u
#define C32_SLOTS_PER_GROUP 3u
#define C32_CHECKPOINTS 2u
#define C32_ANCHORS 2u
#define C32_MUTATIONS 2u
#define C32_PHYSICAL_OPS 2u
#define C32_PLP_SLOTS 2u
#define C32_MAX_VERSION 3u
#define C32_MAX_TRACE_DEPTH 32u
#define C32_MAX_BASE_STATES 131072u
#define C32_HASH_SLOTS 262144u
#define C32_MAX_CUT_RECOVERY 393216u
#define C32_CANONICAL_BYTES 4096u
#define C32_SCENARIO_FAMILIES 11u
#define C32_PROFILE_VARIANTS 6u
#define C32_INVARIANT_COUNT 13u

enum c32_cut_kind {
    C32_CUT_CONTROLLER_RESET = 0,
    C32_CUT_POWER_LOSS = 1,
    C32_CUT_DAEMON_CRASH = 2,
    C32_CUT_HOST_CRASH = 3
};

enum c32_record_presence {
    C32_RECORD_EMPTY = 0,
    C32_RECORD_INVALID = 1,
    C32_RECORD_VALID = 2
};

enum c32_record_kind {
    C32_REC_DATA = 0,
    C32_REC_MAP = 1,
    C32_REC_TOMBSTONE = 2,
    C32_REC_RELOCATION = 3
};

enum c32_logical_kind {
    C32_LOGICAL_NONE = 0,
    C32_LOGICAL_VALUE = 1,
    C32_LOGICAL_TOMBSTONE = 2
};

enum c32_op_phase {
    C32_OP_FREE = 0,
    C32_OP_B = 1,
    C32_OP_A_APPLIED = 2,
    C32_OP_A_NO_EFFECT = 3,
    C32_OP_C_APPLIED = 4,
    C32_OP_C_NO_EFFECT = 5
};

enum c32_phys_outcome {
    C32_PHYS_APPLIED = 0,
    C32_PHYS_NO_EFFECT = 1
};

enum c32_op_purpose {
    C32_PURPOSE_DATA = 0,
    C32_PURPOSE_MAP = 1,
    C32_PURPOSE_TOMB = 2,
    C32_PURPOSE_GC_COPY = 3,
    C32_PURPOSE_RELOCATION = 4,
    C32_PURPOSE_ERASE = 5,
    C32_PURPOSE_CKPT_IMAGE = 6,
    C32_PURPOSE_CKPT_ANCHOR = 7
};

enum c32_target_domain {
    C32_TARGET_MEDIA = 0,
    C32_TARGET_CHECKPOINT = 1,
    C32_TARGET_ANCHOR = 2,
    C32_TARGET_ERASE_GROUP = 3
};

enum c32_image_state {
    C32_IMAGE_EMPTY = 0,
    C32_IMAGE_INVALID = 1,
    C32_IMAGE_VALID = 2
};

enum c32_plp_state {
    C32_PLP_FREE = 0,
    C32_PLP_PREPARED = 1,
    C32_PLP_ADMITTED = 2,
    C32_PLP_DRAINING = 3
};

enum c32_publication {
    C32_PUBLISH_NONE = 0,
    C32_PUBLISH_VOLATILE = 1,
    C32_PUBLISH_DURABLE = 2,
    C32_PUBLISH_FAILED_NO_COMMIT = 3,
    C32_PUBLISH_FAILED_INDETERMINATE = 4
};

enum c32_model_phase {
    C32_MODEL_READY = 0,
    C32_MODEL_RESET_DRAIN = 1,
    C32_MODEL_FAULTED = 2
};

enum c32_recovery_status {
    C32_RECOVERY_OK = 0,
    C32_RECOVERY_AMBIGUOUS = 1,
    C32_RECOVERY_FAIL_CLOSED = 2
};

enum c32_broken_variant {
    C32_BROKEN_NONE = 0,
    C32_BM_UNIQUE_KEEP_PREDECESSOR = 1,
    C32_BM_TORN_SKIP_CHECKSUM = 2,
    C32_BM_MAP_OMIT_DATA_C_GUARD = 3,
    C32_BM_RECOVERY_SKIP_TAIL_AFTER_CKPT = 4,
    C32_BM_RECOVER_HIGHEST_DATA_WITHOUT_MAP = 5,
    C32_BM_RELOC_OVERRIDES_TOMBSTONE = 6,
    C32_BM_GC_ERASE_AFTER_COPY = 7,
    C32_BM_ANCHOR_BEFORE_CKPT_COMPLETE = 8,
    C32_BM_DELIVERY_MATCH_SLOT_ONLY = 9,
    C32_BM_FENCE_LT_FRONTIER = 10,
    C32_BM_READY_BEFORE_PLP_DRAIN = 11,
    C32_BM_ALLOC_LEAVES_FREE_BIT = 12,
    C32_BM_HOST_FALLBACK_ON_NO_MAP = 13
};

enum c32_scenario_family {
    C32_SCENARIO_SINGLE_ATOM = 0,
    C32_SCENARIO_DUAL_ATOM = 1,
    C32_SCENARIO_TRIM = 2,
    C32_SCENARIO_CHECKPOINT = 3,
    C32_SCENARIO_GC = 4,
    C32_SCENARIO_FENCE = 5,
    C32_SCENARIO_UNRELATED = 6,
    C32_SCENARIO_PLP = 7,
    C32_SCENARIO_EPOCH = 8,
    C32_SCENARIO_INVALID_MEDIA = 9,
    C32_SCENARIO_LIMITS_HOST = 10
};

enum c32_profile_variant {
    C32_PROFILE_WC_OFF_NO_PLP = 0,
    C32_PROFILE_WC_ON_NO_PLP = 1,
    C32_PROFILE_WC_OFF_PLP_CAP2 = 2,
    C32_PROFILE_WC_ON_PLP_CAP2 = 3,
    C32_PROFILE_WC_ON_PLP_CAP1 = 4,
    C32_PROFILE_CLAIMED_UNVALIDATED = 5
};

/* The numeric order is part of the deterministic model traversal. */
enum c32_model_action_kind {
    C32_ACTION_ACCEPT_WRITE = 0,
    C32_ACTION_ACCEPT_TRIM = 1,
    C32_ACTION_CAPTURE_FENCE = 2,
    C32_ACTION_PLP_PREPARE = 3,
    C32_ACTION_PLP_ADMIT = 4,
    C32_ACTION_B_PHYS = 5,
    C32_ACTION_A_PHYS = 6,
    C32_ACTION_C_PHYS = 7,
    C32_ACTION_DELIVER_OUTCOME = 8,
    C32_ACTION_PUBLISH_VOLATILE = 9,
    C32_ACTION_PUBLISH_DURABLE = 10,
    C32_ACTION_CLOSE_NO_COMMIT = 11,
    C32_ACTION_CLOSE_INDETERMINATE = 12,
    C32_ACTION_PUBLISH_FENCE = 13,
    C32_ACTION_START_CHECKPOINT = 14,
    C32_ACTION_START_GC_COPY = 15,
    C32_ACTION_RELEASE_GC_LEASE = 16,
    C32_ACTION_COUNT = 17
};

struct c32_model_action {
    uint8_t kind;
    uint8_t purpose;
    uint8_t outcome;
    uint8_t subject;
    uint8_t step;
    uint8_t variant;
    uint16_t reserved0;
};

struct c32_phys_ref {
    uint8_t group;
    uint8_t slot;
    uint8_t erase_generation;
    uint8_t valid;
};

struct c32_persistent_record {
    uint8_t presence;
    uint8_t kind;
    uint8_t atom;
    uint8_t logical_version;
    uint8_t value_token;
    uint8_t predecessor_version;
    uint8_t copy_discriminator;
    uint8_t body_complete;
    uint8_t checksum_ok;
    uint8_t c_applied;
    uint16_t record_id;
    uint16_t mutation_id;
    uint16_t predecessor_state_id;
    uint16_t c_sequence;
    struct c32_phys_ref self;
    struct c32_phys_ref data_ref;
    struct c32_phys_ref source_ref;
};

struct c32_logical_state {
    uint8_t kind;
    uint8_t atom;
    uint8_t version;
    uint8_t copy_discriminator;
    uint8_t value_token;
    uint8_t reserved0;
    uint16_t state_id;
    uint16_t predecessor_state_id;
    uint16_t authority_record_id;
    struct c32_phys_ref data_ref;
};

struct c32_checkpoint {
    uint8_t image_state;
    uint8_t generation;
    uint8_t body_complete;
    uint8_t checksum_ok;
    uint8_t c_applied;
    uint8_t provenance_ok;
    uint16_t watermark;
    uint16_t c_sequence;
    uint16_t reserved0;
    uint64_t payload_hash;
    struct c32_logical_state entry[C32_ATOMS];
};

struct c32_anchor {
    uint8_t image_state;
    uint8_t generation;
    uint8_t target_slot;
    uint8_t body_complete;
    uint8_t checksum_ok;
    uint8_t c_applied;
    uint16_t watermark;
    uint16_t c_sequence;
    uint16_t reserved0;
    uint64_t checkpoint_hash;
};

struct c32_physical_op {
    uint8_t phase;
    uint8_t purpose;
    uint8_t frozen_outcome;
    uint8_t target_domain;
    uint8_t target_group;
    uint8_t target_slot;
    uint16_t op_id;
    uint16_t owner_epoch;
    uint16_t begin_order;
    uint16_t commit_sequence;
    uint8_t outcome_delivered;
    uint8_t reserved0;
    struct c32_persistent_record frozen_record;
    struct c32_checkpoint frozen_checkpoint;
    struct c32_anchor frozen_anchor;
};

struct c32_plp_atom {
    uint8_t atom;
    uint8_t version;
    uint8_t target_kind;
    uint8_t predecessor_version;
    uint8_t value_token;
    uint8_t reserved0[3];
    uint16_t predecessor_state_id;
    uint16_t reserved1;
    struct c32_phys_ref drain_data_ref;
    struct c32_phys_ref drain_metadata_ref;
};

struct c32_plp_envelope {
    uint8_t state;
    uint8_t atom_mask;
    uint8_t capacity_cost;
    uint8_t flags;
    uint8_t survival_event_mask;
    uint8_t drained_atom_mask;
    uint8_t drain_budget_reserved;
    uint8_t reserved0;
    uint16_t envelope_id;
    uint16_t mutation_id;
    uint16_t owner_epoch;
    uint16_t accept_sequence;
    uint16_t persistent_order;
    struct c32_plp_atom atom[C32_ATOMS];
};

struct c32_mutation {
    uint8_t used;
    uint8_t atom_mask;
    uint8_t request_kind;
    uint8_t publication;
    uint16_t mutation_id;
    uint16_t command_id;
    uint16_t owner_epoch;
    uint16_t accept_sequence;
    uint16_t scope;
    uint8_t target_kind[C32_ATOMS];
    uint8_t target_version[C32_ATOMS];
    uint8_t predecessor_version[C32_ATOMS];
    uint8_t value_token[C32_ATOMS];
    uint8_t closure[C32_ATOMS];
};

struct c32_fence {
    uint8_t used;
    uint8_t success;
    uint16_t fence_id;
    uint16_t owner_epoch;
    uint16_t scope;
    uint16_t frontier;
    uint8_t covered_mutation_mask;
    uint8_t reserved0;
};

struct c32_gc_plan {
    uint8_t used;
    uint8_t atom;
    uint8_t version;
    uint8_t stage;
    uint8_t lease_held;
    uint8_t reserved0[3];
    struct c32_phys_ref source;
    struct c32_phys_ref destination;
};

struct c32_model_state {
    struct fwlab_persist_profile profile;
    uint8_t phase;
    uint8_t current_epoch;
    uint8_t next_accept_sequence;
    uint8_t next_begin_order;
    uint8_t next_commit_sequence;
    uint8_t cut_used;
    uint8_t erase_generation[C32_GROUPS];
    uint8_t reserved0;
    uint16_t free_bitmap;
    uint16_t reserved_bitmap;
    uint8_t scenario_family;
    uint8_t scenario_profile;
    uint8_t scenario_variant;
    uint8_t scenario_request;
    uint32_t grammar_progress;
    uint32_t grammar_choice;
    uint8_t host_adversarial_mask;
    uint8_t reserved1[3];
    struct c32_persistent_record media[C32_GROUPS][C32_SLOTS_PER_GROUP];
    struct c32_physical_op inflight[C32_PHYSICAL_OPS];
    struct c32_checkpoint checkpoint[C32_CHECKPOINTS];
    struct c32_anchor anchor[C32_ANCHORS];
    struct c32_plp_envelope plp[C32_PLP_SLOTS];
    struct c32_mutation mutation[C32_MUTATIONS];
    struct c32_fence fence;
    struct c32_gc_plan gc;
    struct c32_logical_state firmware_ram[C32_ATOMS];
    struct c32_logical_state host_cache[C32_ATOMS];
    struct c32_logical_state durable_floor[C32_ATOMS];
    struct c32_logical_state genesis[C32_ATOMS];
};

/* This projection intentionally has no ledger, RAM, Host cache or observations. */
struct c32_logical_image {
    uint8_t erase_generation[C32_GROUPS];
    uint8_t reserved0;
    struct c32_persistent_record media[C32_GROUPS][C32_SLOTS_PER_GROUP];
    struct c32_checkpoint checkpoint[C32_CHECKPOINTS];
    struct c32_anchor anchor[C32_ANCHORS];
    struct c32_logical_state genesis[C32_ATOMS];
};

struct c32_physical_result {
    uint8_t status;
    uint8_t settled_operations;
    uint8_t drained_envelopes;
    uint8_t stale_deliveries_blocked;
    struct c32_logical_image image;
};

struct c32_recovery_result {
    uint8_t status;
    uint8_t selected_checkpoint;
    uint16_t watermark;
    struct c32_logical_state atom[C32_ATOMS];
    uint64_t hash;
};

struct c32_invariant_result {
    uint8_t passed;
    uint8_t invariant_id;
    uint8_t atom;
    uint8_t reserved0;
    uint16_t record_id;
    uint16_t reason;
};

struct c32_counterexample {
    uint16_t schema_version;
    uint8_t invariant_id;
    uint8_t broken_variant;
    uint8_t minimal_depth;
    uint8_t scenario_family;
    uint8_t profile_variant;
    uint8_t initial_variant;
    uint8_t request_kind;
    uint8_t cut_kind;
    uint8_t selected_checkpoint;
    uint8_t plp_drained_envelopes;
    uint8_t action_count;
    uint16_t violation_mask;
    uint16_t allowed_set[C32_ATOMS];
    uint64_t initial_hash;
    uint64_t precut_hash;
    uint64_t physical_hash;
    uint64_t recovered_hash;
    uint64_t oracle_hash;
    struct c32_logical_state durable_floor[C32_ATOMS];
    struct c32_logical_state recovered[C32_ATOMS];
    struct c32_model_action action[C32_MAX_TRACE_DEPTH];
};

struct c32_model_report {
    uint32_t scenario_runs;
    uint32_t rejected_configurations;
    uint32_t base_states;
    uint32_t terminal_states;
    uint32_t recovery_checks;
    uint32_t canonical_collisions;
    uint32_t duplicate_states;
    uint32_t max_depth;
    uint32_t action_coverage;
    uint16_t invariant_coverage;
    uint16_t reserved0;
    uint64_t aggregate_hash;
};

size_t c32_state_encode(
    const struct c32_model_state *state,
    uint8_t *bytes,
    size_t capacity
);

size_t c32_logical_image_encode(
    const struct c32_logical_image *image,
    uint8_t *bytes,
    size_t capacity
);

uint64_t c32_hash_bytes(const uint8_t *bytes, size_t length);
uint64_t c32_state_hash(const struct c32_model_state *state);
uint64_t c32_logical_image_hash(const struct c32_logical_image *image);
uint64_t c32_recovery_hash(const struct c32_recovery_result *result);

int c32_physical_settle(
    const struct c32_model_state *cut,
    enum c32_cut_kind event,
    enum c32_broken_variant broken,
    struct c32_physical_result *result
);

int c32_logical_recover(
    const struct c32_logical_image *image,
    enum c32_broken_variant broken,
    struct c32_recovery_result *result
);

int c32_check_invariant(
    enum fwlab_persist_invariant_id invariant,
    const struct c32_model_state *cut,
    enum c32_cut_kind event,
    const struct c32_physical_result *physical,
    const struct c32_recovery_result *recovered,
    struct c32_invariant_result *result
);

int c32_model_run_positive(struct c32_model_report *report);

int c32_model_find_counterexample(
    enum c32_broken_variant broken,
    struct c32_counterexample *counterexample
);

const char *c32_action_name(enum c32_model_action_kind action);
const char *c32_scenario_name(enum c32_scenario_family family);
const char *c32_profile_name(enum c32_profile_variant profile);
const char *c32_invariant_name(enum fwlab_persist_invariant_id invariant);
const char *c32_broken_name(enum c32_broken_variant broken);

#endif
