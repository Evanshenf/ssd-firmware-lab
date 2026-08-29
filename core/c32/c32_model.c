/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c32_internal.h"

#include <limits.h>
#include <string.h>

#include "fwlab/portable/persistence_policy.h"

#define C32_MEDIA_MASK UINT16_C(0x01ff)
#define C32_MAX_ENABLED_ACTIONS 48u
#define C32_STEP_BIT(step) (UINT32_C(1) << (step))

struct c32_bfs_node {
    struct c32_model_state state;
    struct c32_model_action action;
    uint32_t parent;
    uint8_t depth;
    uint8_t reserved0[3];
};

struct c32_action_list {
    struct c32_model_action item[C32_MAX_ENABLED_ACTIONS];
    unsigned int count;
};

/*
 * The arrays are BSS-backed and touched only as states become reachable.  The
 * capacity equals the frozen hard limit: reaching it is a gate failure, never
 * a reason to prune a transition.
 */
static struct c32_bfs_node bfs_nodes[C32_MAX_BASE_STATES];
static uint32_t visited_slots[C32_HASH_SLOTS];

static const char *const action_names[C32_ACTION_COUNT] = {
    "ACCEPT_WRITE", "ACCEPT_TRIM", "CAPTURE_FENCE", "PLP_PREPARE",
    "PLP_ADMIT", "B_PHYS", "A_PHYS", "C_PHYS", "DELIVER_OUTCOME",
    "PUBLISH_VOLATILE", "PUBLISH_DURABLE", "CLOSE_NO_COMMIT",
    "CLOSE_INDETERMINATE", "PUBLISH_FENCE", "START_CHECKPOINT",
    "START_GC_COPY", "RELEASE_GC_LEASE"
};

static const char *const scenario_names[C32_SCENARIO_FAMILIES] = {
    "single-atom", "dual-atom", "trim", "checkpoint", "gc", "fence",
    "unrelated", "plp", "epoch", "invalid-media", "limits-host"
};

static const char *const profile_names[C32_PROFILE_VARIANTS] = {
    "wc-off-no-plp", "wc-on-no-plp", "wc-off-plp-cap2",
    "wc-on-plp-cap2", "wc-on-plp-cap1", "claimed-unvalidated-plp"
};

static const char *const invariant_names[C32_INVARIANT_COUNT] = {
    "P-UNIQUE", "P-NO-TORN", "P-DEPEND", "P-DURABLE-FLOOR",
    "P-VOLATILE-BOUND", "P-TRIM", "P-GC", "P-CHECKPOINT", "P-EPOCH",
    "P-FENCE", "P-PLP", "P-CONSERVE", "P-NO-HOST-AUTHORITY"
};

static const char *const broken_names[C32_INVARIANT_COUNT + 1u] = {
    "NONE", "BM_UNIQUE_KEEP_PREDECESSOR", "BM_TORN_SKIP_CHECKSUM",
    "BM_MAP_OMIT_DATA_C_GUARD", "BM_RECOVERY_SKIP_TAIL_AFTER_CKPT",
    "BM_RECOVER_HIGHEST_DATA_WITHOUT_MAP",
    "BM_RELOC_OVERRIDES_TOMBSTONE", "BM_GC_ERASE_AFTER_COPY",
    "BM_ANCHOR_BEFORE_CKPT_COMPLETE", "BM_DELIVERY_MATCH_SLOT_ONLY",
    "BM_FENCE_LT_FRONTIER", "BM_READY_BEFORE_PLP_DRAIN",
    "BM_ALLOC_LEAVES_FREE_BIT", "BM_HOST_FALLBACK_ON_NO_MAP"
};

const char *c32_action_name(enum c32_model_action_kind action)
{
    return action < C32_ACTION_COUNT ? action_names[action] : "INVALID";
}

const char *c32_scenario_name(enum c32_scenario_family family)
{
    return family < C32_SCENARIO_FAMILIES ? scenario_names[family] :
                                            "invalid-scenario";
}

const char *c32_profile_name(enum c32_profile_variant profile)
{
    return profile < C32_PROFILE_VARIANTS ? profile_names[profile] :
                                           "invalid-profile";
}

const char *c32_invariant_name(enum fwlab_persist_invariant_id invariant)
{
    return invariant >= FWLAB_P_UNIQUE &&
                   invariant <= FWLAB_P_NO_HOST_AUTHORITY ?
           invariant_names[(unsigned int)invariant - 1u] :
           "P-INVALID";
}

const char *c32_broken_name(enum c32_broken_variant broken)
{
    return broken <= C32_BM_HOST_FALLBACK_ON_NO_MAP ?
           broken_names[broken] : "BM_INVALID";
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int byte;

    for (byte = 0; byte < 8; ++byte) {
        hash ^= (uint8_t)(value >> (byte * 8u));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static unsigned int bit_count(uint8_t value)
{
    unsigned int count = 0;

    while (value != 0) {
        count += value & 1u;
        value >>= 1;
    }
    return count;
}

static uint16_t slot_bit(unsigned int group, unsigned int slot)
{
    return (uint16_t)(UINT16_C(1) <<
                      (group * C32_SLOTS_PER_GROUP + slot));
}

static struct c32_phys_ref phys_ref(
    const struct c32_model_state *state,
    unsigned int group,
    unsigned int slot
)
{
    struct c32_phys_ref ref;

    memset(&ref, 0, sizeof(ref));
    ref.group = (uint8_t)group;
    ref.slot = (uint8_t)slot;
    ref.erase_generation = state->erase_generation[group];
    ref.valid = 1;
    return ref;
}

static struct c32_logical_state genesis_state(unsigned int atom)
{
    struct c32_logical_state logical;

    memset(&logical, 0, sizeof(logical));
    logical.kind = C32_LOGICAL_VALUE;
    logical.atom = (uint8_t)atom;
    logical.version = 0;
    logical.value_token = 1;
    logical.state_id = (uint16_t)(atom + 1u);
    return logical;
}

static struct fwlab_persist_profile profile_make(
    enum c32_profile_variant variant
)
{
    struct fwlab_persist_profile profile;

    memset(&profile, 0, sizeof(profile));
    profile.version = FWLAB_PERSIST_VERSION;
    profile.size = (uint16_t)sizeof(profile);
    switch (variant) {
    case C32_PROFILE_WC_OFF_NO_PLP:
        break;
    case C32_PROFILE_WC_ON_NO_PLP:
        profile.cache_enabled = 1;
        break;
    case C32_PROFILE_WC_OFF_PLP_CAP2:
        profile.plp_kind = FWLAB_PERSIST_PLP_VALIDATED;
        profile.plp_capacity_credits = 2;
        profile.survival_event_mask =
            FWLAB_PERSIST_EVENT_CONTROLLER_RESET |
            FWLAB_PERSIST_EVENT_POWER_LOSS |
            FWLAB_PERSIST_EVENT_DAEMON_CRASH;
        break;
    case C32_PROFILE_WC_ON_PLP_CAP2:
        profile.cache_enabled = 1;
        profile.plp_kind = FWLAB_PERSIST_PLP_VALIDATED;
        profile.plp_capacity_credits = 2;
        profile.survival_event_mask =
            FWLAB_PERSIST_EVENT_CONTROLLER_RESET |
            FWLAB_PERSIST_EVENT_POWER_LOSS |
            FWLAB_PERSIST_EVENT_DAEMON_CRASH;
        break;
    case C32_PROFILE_WC_ON_PLP_CAP1:
        profile.cache_enabled = 1;
        profile.plp_kind = FWLAB_PERSIST_PLP_VALIDATED;
        profile.plp_capacity_credits = 1;
        profile.survival_event_mask =
            FWLAB_PERSIST_EVENT_CONTROLLER_RESET |
            FWLAB_PERSIST_EVENT_POWER_LOSS |
            FWLAB_PERSIST_EVENT_DAEMON_CRASH;
        break;
    case C32_PROFILE_CLAIMED_UNVALIDATED:
        profile.cache_enabled = 1;
        profile.plp_kind = FWLAB_PERSIST_PLP_CLAIMED_UNVALIDATED;
        profile.plp_capacity_credits = 2;
        profile.survival_event_mask =
            FWLAB_PERSIST_EVENT_CONTROLLER_RESET |
            FWLAB_PERSIST_EVENT_POWER_LOSS;
        break;
    default:
        profile.version = 0;
        break;
    }
    return profile;
}

static void occupy_record(
    struct c32_model_state *state,
    unsigned int group,
    unsigned int slot,
    const struct c32_persistent_record *record
)
{
    state->media[group][slot] = *record;
    state->free_bitmap &= (uint16_t)~slot_bit(group, slot);
}

static struct c32_persistent_record data_record(
    const struct c32_model_state *state,
    unsigned int group,
    unsigned int slot,
    unsigned int atom,
    unsigned int version,
    unsigned int value,
    uint16_t record_id,
    uint16_t mutation_id,
    uint16_t c_sequence
)
{
    struct c32_persistent_record record;

    memset(&record, 0, sizeof(record));
    record.presence = C32_RECORD_VALID;
    record.kind = C32_REC_DATA;
    record.atom = (uint8_t)atom;
    record.logical_version = (uint8_t)version;
    record.value_token = (uint8_t)value;
    record.body_complete = 1;
    record.checksum_ok = 1;
    record.c_applied = 1;
    record.record_id = record_id;
    record.mutation_id = mutation_id;
    record.c_sequence = c_sequence;
    record.self = phys_ref(state, group, slot);
    return record;
}

static struct c32_persistent_record metadata_record(
    const struct c32_model_state *state,
    unsigned int group,
    unsigned int slot,
    enum c32_record_kind kind,
    unsigned int atom,
    unsigned int version,
    unsigned int value,
    uint16_t predecessor_state_id,
    uint16_t record_id,
    uint16_t mutation_id,
    uint16_t c_sequence,
    struct c32_phys_ref data
)
{
    struct c32_persistent_record record = data_record(
        state, group, slot, atom, version, value, record_id, mutation_id,
        c_sequence);

    record.kind = (uint8_t)kind;
    record.predecessor_version = version == 0 ? 0 : (uint8_t)(version - 1u);
    record.predecessor_state_id = predecessor_state_id;
    record.data_ref = data;
    return record;
}

static struct c32_logical_state logical_from_record(
    const struct c32_persistent_record *record
)
{
    struct c32_logical_state logical;

    memset(&logical, 0, sizeof(logical));
    logical.kind = record->kind == C32_REC_TOMBSTONE ?
                   C32_LOGICAL_TOMBSTONE : C32_LOGICAL_VALUE;
    logical.atom = record->atom;
    logical.version = record->logical_version;
    logical.copy_discriminator = record->copy_discriminator;
    logical.value_token = record->value_token;
    logical.state_id = record->record_id;
    logical.predecessor_state_id = record->predecessor_state_id;
    logical.authority_record_id = record->record_id;
    logical.data_ref = record->data_ref;
    return logical;
}

static struct c32_logical_state install_value(
    struct c32_model_state *state,
    unsigned int atom,
    unsigned int version,
    unsigned int value,
    unsigned int data_group,
    unsigned int data_slot,
    unsigned int map_group,
    unsigned int map_slot,
    uint16_t data_id,
    uint16_t map_id,
    uint16_t data_sequence,
    uint16_t map_sequence,
    uint16_t predecessor_state_id
)
{
    struct c32_persistent_record data = data_record(
        state, data_group, data_slot, atom, version, value, data_id, 0,
        data_sequence);
    struct c32_persistent_record map = metadata_record(
        state, map_group, map_slot, C32_REC_MAP, atom, version, value,
        predecessor_state_id, map_id, 0, map_sequence, data.self);

    occupy_record(state, data_group, data_slot, &data);
    occupy_record(state, map_group, map_slot, &map);
    return logical_from_record(&map);
}

static uint16_t operation_record_id(
    unsigned int subject,
    enum c32_op_purpose purpose
)
{
    return (uint16_t)(UINT16_C(100) + (subject + 1u) * 16u +
                      (unsigned int)purpose * 2u);
}

static int find_record_by_mutation(
    const struct c32_model_state *state,
    uint16_t mutation_id,
    enum c32_record_kind kind,
    struct c32_persistent_record *record
)
{
    unsigned int group;
    unsigned int slot;

    for (group = 0; group < C32_GROUPS; ++group) {
        for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
            const struct c32_persistent_record *candidate =
                &state->media[group][slot];

            if (candidate->presence == C32_RECORD_VALID &&
                candidate->c_applied != 0 &&
                candidate->mutation_id == mutation_id &&
                candidate->kind == kind) {
                if (record != NULL) {
                    *record = *candidate;
                }
                return 1;
            }
        }
    }
    return 0;
}

static int find_record_id(
    const struct c32_model_state *state,
    uint16_t record_id,
    struct c32_persistent_record *record
)
{
    unsigned int group;
    unsigned int slot;

    for (group = 0; group < C32_GROUPS; ++group) {
        for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
            if (state->media[group][slot].record_id == record_id) {
                if (record != NULL) {
                    *record = state->media[group][slot];
                }
                return 1;
            }
        }
    }
    return 0;
}

static void checkpoint_pair_install(
    struct c32_model_state *state,
    unsigned int slot,
    unsigned int generation,
    uint16_t watermark,
    uint64_t payload_hash,
    const struct c32_logical_state entry[C32_ATOMS]
)
{
    unsigned int atom;

    memset(&state->checkpoint[slot], 0, sizeof(state->checkpoint[slot]));
    state->checkpoint[slot].image_state = C32_IMAGE_VALID;
    state->checkpoint[slot].generation = (uint8_t)generation;
    state->checkpoint[slot].body_complete = 1;
    state->checkpoint[slot].checksum_ok = 1;
    state->checkpoint[slot].c_applied = 1;
    state->checkpoint[slot].provenance_ok = 1;
    state->checkpoint[slot].watermark = watermark;
    state->checkpoint[slot].c_sequence = watermark;
    state->checkpoint[slot].payload_hash = payload_hash;
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        state->checkpoint[slot].entry[atom] = entry[atom];
    }
    memset(&state->anchor[slot], 0, sizeof(state->anchor[slot]));
    state->anchor[slot].image_state = C32_IMAGE_VALID;
    state->anchor[slot].generation = (uint8_t)generation;
    state->anchor[slot].target_slot = (uint8_t)slot;
    state->anchor[slot].body_complete = 1;
    state->anchor[slot].checksum_ok = 1;
    state->anchor[slot].c_applied = 1;
    state->anchor[slot].watermark = watermark;
    state->anchor[slot].c_sequence = watermark;
    state->anchor[slot].checkpoint_hash = payload_hash;
}

static void mutation_observation(
    struct c32_model_state *state,
    unsigned int index,
    uint8_t atom_mask,
    uint8_t publication,
    uint8_t target_kind,
    uint8_t target_version,
    uint8_t value_token
)
{
    unsigned int atom;
    struct c32_mutation *mutation = &state->mutation[index];

    memset(mutation, 0, sizeof(*mutation));
    mutation->used = 1;
    mutation->atom_mask = atom_mask;
    mutation->publication = publication;
    mutation->mutation_id = (uint16_t)(index + 1u);
    mutation->command_id = (uint16_t)(index + 1u);
    mutation->owner_epoch = state->current_epoch;
    mutation->accept_sequence = (uint16_t)index;
    mutation->scope = 1;
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        if ((atom_mask & (uint8_t)(1u << atom)) != 0) {
            mutation->target_kind[atom] = target_kind;
            mutation->target_version[atom] = target_version;
            mutation->predecessor_version[atom] =
                target_version == 0 ? 0 : (uint8_t)(target_version - 1u);
            mutation->value_token[atom] = value_token;
        }
    }
}

static void state_base_init(
    struct c32_model_state *state,
    enum c32_scenario_family family,
    enum c32_profile_variant profile,
    unsigned int variant,
    enum fwlab_persist_request_kind request
)
{
    unsigned int atom;

    memset(state, 0, sizeof(*state));
    state->profile = profile_make(profile);
    state->phase = C32_MODEL_READY;
    state->current_epoch = 1;
    state->next_commit_sequence = 1;
    state->free_bitmap = C32_MEDIA_MASK;
    state->scenario_family = (uint8_t)family;
    state->scenario_profile = (uint8_t)profile;
    state->scenario_variant = (uint8_t)variant;
    state->scenario_request = (uint8_t)request;
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        state->genesis[atom] = genesis_state(atom);
        state->firmware_ram[atom] = state->genesis[atom];
        state->host_cache[atom] = state->genesis[atom];
        state->durable_floor[atom] = state->genesis[atom];
    }
}

static void scenario_trim_init(struct c32_model_state *state)
{
    struct c32_logical_state live = install_value(
        state, 0, 1, 2, 0, 0, 0, 1, 10, 11, 1, 2,
        state->genesis[0].state_id);
    struct c32_persistent_record copy;
    struct c32_persistent_record relocation;
    uint16_t tomb_id = operation_record_id(0, C32_PURPOSE_TOMB);

    state->firmware_ram[0] = live;
    state->durable_floor[0] = live;
    state->next_commit_sequence = 3;
    copy = data_record(state, 1, 0, 0, 1, 2, 40, 0, 4);
    relocation = metadata_record(state, 1, 1, C32_REC_RELOCATION, 0, 1,
                                 2, tomb_id, 41, 0, 5, copy.self);
    relocation.predecessor_version = 2;
    relocation.copy_discriminator = 1;
    occupy_record(state, 1, 0, &copy);
    occupy_record(state, 1, 1, &relocation);
}

static void scenario_checkpoint_init(struct c32_model_state *state)
{
    struct c32_logical_state checkpoint_entry[C32_ATOMS];
    struct c32_logical_state version1;
    struct c32_logical_state version2;

    version1 = install_value(state, 0, 1, 2, 0, 0, 0, 1, 10, 11, 1,
                             2, state->genesis[0].state_id);
    checkpoint_entry[0] = version1;
    checkpoint_entry[1] = state->genesis[1];
    checkpoint_pair_install(state, 0, 1, 2, UINT64_C(0x11110001),
                            checkpoint_entry);
    version2 = install_value(state, 0, 2, 3, 0, 2, 1, 0, 12, 13, 3, 4,
                             version1.state_id);
    state->firmware_ram[0] = version2;
    state->durable_floor[0] = version2;
    state->next_commit_sequence = 5;
}

static void scenario_gc_init(struct c32_model_state *state)
{
    struct c32_logical_state live = install_value(
        state, 0, 1, 2, 0, 0, 0, 1, 10, 11, 1, 2,
        state->genesis[0].state_id);
    struct c32_logical_state checkpoint_entry[C32_ATOMS];

    state->firmware_ram[0] = live;
    state->durable_floor[0] = live;
    checkpoint_entry[0] = live;
    checkpoint_entry[1] = state->genesis[1];
    checkpoint_pair_install(state, 0, 1, 2, UINT64_C(0x33330003),
                            checkpoint_entry);
    state->next_commit_sequence = 3;
}

static void scenario_checkpoint_invalid_init(
    struct c32_model_state *state,
    unsigned int variant
)
{
    struct c32_persistent_record data;
    struct c32_persistent_record map;

    mutation_observation(state, 0, 1, C32_PUBLISH_VOLATILE,
                         C32_LOGICAL_VALUE, 1, 2);
    data = data_record(state, 0, 0, 0, 1, 2, 10, 1, 1);
    map = metadata_record(state, 0, 1, C32_REC_MAP, 0, 1, 2,
                          state->genesis[0].state_id, 11, 1, 2, data.self);
    if (variant == 0) {
        map.checksum_ok = 0;
    } else if (variant == 1) {
        data.c_applied = 0;
    } else if (variant == 2) {
        state->erase_generation[0] = 1;
    } else {
        state->genesis[0].version = 1;
        state->firmware_ram[0] = state->genesis[0];
        state->durable_floor[0] = state->genesis[0];
        map.logical_version = 1;
    }
    occupy_record(state, 0, 0, &data);
    occupy_record(state, 0, 1, &map);
}

static void scenario_epoch_init(
    struct c32_model_state *state,
    unsigned int variant
)
{
    struct c32_physical_op *operation = &state->inflight[0];
    struct c32_persistent_record record = data_record(
        state, 0, 0, 0, 1, 2, 60, 1, 1);

    memset(operation, 0, sizeof(*operation));
    operation->phase = variant == 0 ? C32_OP_B :
                       variant == 1 ? C32_OP_A_APPLIED : C32_OP_C_APPLIED;
    operation->purpose = C32_PURPOSE_DATA;
    operation->frozen_outcome = C32_PHYS_APPLIED;
    operation->target_domain = C32_TARGET_MEDIA;
    operation->target_group = 0;
    operation->target_slot = 0;
    operation->op_id = 1;
    operation->owner_epoch = 0;
    operation->frozen_record = record;
    if (operation->phase == C32_OP_C_APPLIED) {
        occupy_record(state, 0, 0, &record);
    } else {
        state->free_bitmap &= (uint16_t)~slot_bit(0, 0);
        state->reserved_bitmap |= slot_bit(0, 0);
        if (operation->phase == C32_OP_A_APPLIED) {
            record.c_applied = 0;
            occupy_record(state, 0, 0, &record);
            state->reserved_bitmap |= slot_bit(0, 0);
        }
    }
}

static void scenario_limits_init(
    struct c32_model_state *state,
    unsigned int variant
)
{
    unsigned int group;
    unsigned int slot;

    if (variant == 0) {
        for (group = 0; group < C32_ATOMS; ++group) {
            state->genesis[group].version = C32_MAX_VERSION;
            state->firmware_ram[group] = state->genesis[group];
            state->durable_floor[group] = state->genesis[group];
        }
    } else if (variant == 2) {
        for (group = 0; group < C32_GROUPS; ++group) {
            for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
                struct c32_persistent_record invalid;

                memset(&invalid, 0, sizeof(invalid));
                invalid.presence = C32_RECORD_INVALID;
                invalid.record_id = (uint16_t)(200u + group * 3u + slot);
                invalid.self = phys_ref(state, group, slot);
                occupy_record(state, group, slot, &invalid);
            }
        }
    } else if (variant == 3) {
        state->host_cache[0] = state->genesis[0];
        state->host_adversarial_mask = 1;
        state->host_cache[0].state_id = 999;
        state->host_cache[0].authority_record_id = 0;
    }
}

/* Returns 1 for a runnable initial state and 0 for an expected rejection. */
static int scenario_init(
    struct c32_model_state *state,
    enum c32_scenario_family family,
    enum c32_profile_variant profile,
    unsigned int variant,
    enum fwlab_persist_request_kind request
)
{
    state_base_init(state, family, profile, variant, request);
    if (fwlab_persist_profile_validate(&state->profile) != FWLAB_PERSIST_OK) {
        return 0;
    }
    switch (family) {
    case C32_SCENARIO_TRIM:
        scenario_trim_init(state);
        break;
    case C32_SCENARIO_CHECKPOINT:
        scenario_checkpoint_init(state);
        break;
    case C32_SCENARIO_GC:
        scenario_gc_init(state);
        break;
    case C32_SCENARIO_EPOCH:
        scenario_epoch_init(state, variant);
        break;
    case C32_SCENARIO_INVALID_MEDIA:
        scenario_checkpoint_invalid_init(state, variant);
        break;
    case C32_SCENARIO_LIMITS_HOST:
        scenario_limits_init(state, variant);
        break;
    default:
        break;
    }
    return 1;
}

static int action_less(
    const struct c32_model_action *left,
    const struct c32_model_action *right
)
{
    if (left->kind != right->kind) {
        return left->kind < right->kind;
    }
    if (left->purpose != right->purpose) {
        return left->purpose < right->purpose;
    }
    if (left->outcome != right->outcome) {
        return left->outcome < right->outcome;
    }
    if (left->subject != right->subject) {
        return left->subject < right->subject;
    }
    if (left->variant != right->variant) {
        return left->variant < right->variant;
    }
    return left->step < right->step;
}

static int emit_action(
    struct c32_action_list *list,
    enum c32_model_action_kind kind,
    enum c32_op_purpose purpose,
    enum c32_phys_outcome outcome,
    unsigned int subject,
    unsigned int step,
    unsigned int variant
)
{
    struct c32_model_action action;
    unsigned int position;

    if (list->count >= C32_MAX_ENABLED_ACTIONS || step >= 32) {
        return 0;
    }
    memset(&action, 0, sizeof(action));
    action.kind = (uint8_t)kind;
    action.purpose = (uint8_t)purpose;
    action.outcome = (uint8_t)outcome;
    action.subject = (uint8_t)subject;
    action.step = (uint8_t)step;
    action.variant = (uint8_t)variant;
    position = list->count;
    while (position != 0 && action_less(&action, &list->item[position - 1])) {
        list->item[position] = list->item[position - 1];
        --position;
    }
    list->item[position] = action;
    ++list->count;
    return 1;
}

static int done(const struct c32_model_state *state, unsigned int step)
{
    return (state->grammar_progress & C32_STEP_BIT(step)) != 0;
}

static int choice_applied(
    const struct c32_model_state *state,
    unsigned int step
)
{
    return (state->grammar_choice & C32_STEP_BIT(step)) != 0;
}

static unsigned int find_operation_index(
    const struct c32_model_state *state,
    enum c32_op_purpose purpose,
    unsigned int subject
)
{
    unsigned int index;

    for (index = 0; index < C32_PHYSICAL_OPS; ++index) {
        const struct c32_physical_op *operation = &state->inflight[index];
        uint16_t mutation_id = (uint16_t)(subject + 1u);

        if (operation->phase == C32_OP_FREE ||
            operation->purpose != purpose) {
            continue;
        }
        if (purpose <= C32_PURPOSE_RELOCATION &&
            operation->frozen_record.mutation_id != mutation_id &&
            purpose != C32_PURPOSE_GC_COPY &&
            purpose != C32_PURPOSE_RELOCATION) {
            continue;
        }
        if ((purpose == C32_PURPOSE_CKPT_IMAGE ||
             purpose == C32_PURPOSE_CKPT_ANCHOR) &&
            operation->target_slot != subject) {
            continue;
        }
        return index;
    }
    return C32_PHYSICAL_OPS;
}

static struct c32_physical_op *find_operation(
    struct c32_model_state *state,
    enum c32_op_purpose purpose,
    unsigned int subject
)
{
    unsigned int index = find_operation_index(state, purpose, subject);

    return index < C32_PHYSICAL_OPS ? &state->inflight[index] : NULL;
}

static const struct c32_physical_op *find_operation_const(
    const struct c32_model_state *state,
    enum c32_op_purpose purpose,
    unsigned int subject
)
{
    unsigned int index = find_operation_index(state, purpose, subject);

    return index < C32_PHYSICAL_OPS ? &state->inflight[index] : NULL;
}

static int operation_phase_is(
    const struct c32_model_state *state,
    enum c32_op_purpose purpose,
    unsigned int subject,
    enum c32_op_phase first,
    enum c32_op_phase second
)
{
    const struct c32_physical_op *operation =
        find_operation_const(state, purpose, subject);

    return operation != NULL &&
           (operation->phase == first || operation->phase == second);
}

static int emit_b_pair(
    struct c32_action_list *list,
    enum c32_op_purpose purpose,
    unsigned int subject,
    unsigned int step
)
{
    return emit_action(list, C32_ACTION_B_PHYS, purpose,
                       C32_PHYS_APPLIED, subject, step, 0) &&
           emit_action(list, C32_ACTION_B_PHYS, purpose,
                       C32_PHYS_NO_EFFECT, subject, step, 0);
}

static int emit_a_if_ready(
    const struct c32_model_state *state,
    struct c32_action_list *list,
    enum c32_op_purpose purpose,
    unsigned int subject,
    unsigned int step
)
{
    if (!done(state, step) &&
        operation_phase_is(state, purpose, subject, C32_OP_B,
                           C32_OP_B)) {
        return emit_action(list, C32_ACTION_A_PHYS, purpose,
                           C32_PHYS_APPLIED, subject, step, 0);
    }
    return 1;
}

static int emit_c_if_ready(
    const struct c32_model_state *state,
    struct c32_action_list *list,
    enum c32_op_purpose purpose,
    unsigned int subject,
    unsigned int step
)
{
    if (!done(state, step) &&
        operation_phase_is(state, purpose, subject, C32_OP_A_APPLIED,
                           C32_OP_A_NO_EFFECT)) {
        return emit_action(list, C32_ACTION_C_PHYS, purpose,
                           C32_PHYS_APPLIED, subject, step, 0);
    }
    return 1;
}

static int emit_delivery_if_ready(
    const struct c32_model_state *state,
    struct c32_action_list *list,
    enum c32_op_purpose purpose,
    unsigned int subject,
    unsigned int step,
    enum c32_broken_variant broken
)
{
    const struct c32_physical_op *operation =
        find_operation_const(state, purpose, subject);

    if (!done(state, step) && operation != NULL &&
        (operation->phase == C32_OP_C_APPLIED ||
         operation->phase == C32_OP_C_NO_EFFECT) &&
        (operation->owner_epoch == state->current_epoch ||
         broken == C32_BM_DELIVERY_MATCH_SLOT_ONLY)) {
        return emit_action(list, C32_ACTION_DELIVER_OUTCOME, purpose,
                           C32_PHYS_APPLIED, subject, step, 0);
    }
    return 1;
}

static int generate_single(
    const struct c32_model_state *state,
    struct c32_action_list *list
)
{
    const struct c32_mutation *mutation = &state->mutation[0];

    if (!done(state, 0) && state->firmware_ram[0].version < C32_MAX_VERSION &&
        !emit_action(list, C32_ACTION_ACCEPT_WRITE, C32_PURPOSE_DATA,
                     C32_PHYS_APPLIED, 0, 0, 1)) {
        return 0;
    }
    if (done(state, 0) && !done(state, 1) &&
        !emit_b_pair(list, C32_PURPOSE_DATA, 0, 1)) {
        return 0;
    }
    if (!emit_a_if_ready(state, list, C32_PURPOSE_DATA, 0, 2) ||
        !emit_c_if_ready(state, list, C32_PURPOSE_DATA, 0, 3)) {
        return 0;
    }
    if (done(state, 3) && choice_applied(state, 1) && !done(state, 4) &&
        !emit_b_pair(list, C32_PURPOSE_MAP, 0, 4)) {
        return 0;
    }
    if (!emit_a_if_ready(state, list, C32_PURPOSE_MAP, 0, 5) ||
        !emit_c_if_ready(state, list, C32_PURPOSE_MAP, 0, 6)) {
        return 0;
    }
    if (done(state, 0) && !done(state, 7) &&
        state->profile.cache_enabled != 0 &&
        state->scenario_request == FWLAB_PERSIST_DEFAULT &&
        mutation->publication == C32_PUBLISH_NONE &&
        !emit_action(list, C32_ACTION_PUBLISH_VOLATILE, C32_PURPOSE_MAP,
                     C32_PHYS_APPLIED, 0, 7, 0)) {
        return 0;
    }
    if (done(state, 6) && choice_applied(state, 4) && !done(state, 8) &&
        mutation->publication == C32_PUBLISH_NONE &&
        !emit_action(list, C32_ACTION_PUBLISH_DURABLE, C32_PURPOSE_MAP,
                     C32_PHYS_APPLIED, 0, 8, 0)) {
        return 0;
    }
    if (!done(state, 9) &&
        ((done(state, 3) && !choice_applied(state, 1)) ||
         (done(state, 6) && !choice_applied(state, 4))) &&
        !emit_action(list, C32_ACTION_CLOSE_NO_COMMIT, C32_PURPOSE_MAP,
                     C32_PHYS_NO_EFFECT, 0, 9, 0)) {
        return 0;
    }
    if (done(state, 1) && !done(state, 2) && !done(state, 10) &&
        !emit_action(list, C32_ACTION_CLOSE_INDETERMINATE,
                     C32_PURPOSE_DATA, C32_PHYS_APPLIED, 0, 10, 0)) {
        return 0;
    }
    return 1;
}

static int generate_dual(
    const struct c32_model_state *state,
    struct c32_action_list *list
)
{
    if (!done(state, 0) &&
        !emit_action(list, C32_ACTION_ACCEPT_WRITE, C32_PURPOSE_DATA,
                     C32_PHYS_APPLIED, 0, 0, 3)) {
        return 0;
    }
    if (done(state, 0) && !done(state, 1) &&
        !emit_action(list, C32_ACTION_PLP_PREPARE, C32_PURPOSE_MAP,
                     C32_PHYS_APPLIED, 0, 1, 0)) {
        return 0;
    }
    if (done(state, 1) && !done(state, 2) &&
        state->profile.plp_kind == FWLAB_PERSIST_PLP_VALIDATED &&
        state->profile.plp_capacity_credits >= 2 &&
        !emit_action(list, C32_ACTION_PLP_ADMIT, C32_PURPOSE_MAP,
                     C32_PHYS_APPLIED, 0, 2, 0)) {
        return 0;
    }
    if (done(state, 2) && !done(state, 3) &&
        !emit_action(list, C32_ACTION_PUBLISH_DURABLE, C32_PURPOSE_MAP,
                     C32_PHYS_APPLIED, 0, 3, 0)) {
        return 0;
    }
    return 1;
}

static int generate_trim(
    const struct c32_model_state *state,
    struct c32_action_list *list
)
{
    const struct c32_mutation *mutation = &state->mutation[0];

    if (!done(state, 0) &&
        !emit_action(list, C32_ACTION_ACCEPT_TRIM, C32_PURPOSE_TOMB,
                     C32_PHYS_APPLIED, 0, 0, 1)) {
        return 0;
    }
    if (done(state, 0) && !done(state, 1) &&
        !emit_b_pair(list, C32_PURPOSE_TOMB, 0, 1)) {
        return 0;
    }
    if (!emit_a_if_ready(state, list, C32_PURPOSE_TOMB, 0, 2) ||
        !emit_c_if_ready(state, list, C32_PURPOSE_TOMB, 0, 3)) {
        return 0;
    }
    if (done(state, 0) && !done(state, 4) &&
        state->profile.cache_enabled != 0 &&
        state->scenario_request == FWLAB_PERSIST_DEFAULT &&
        mutation->publication == C32_PUBLISH_NONE &&
        !emit_action(list, C32_ACTION_PUBLISH_VOLATILE, C32_PURPOSE_TOMB,
                     C32_PHYS_APPLIED, 0, 4, 0)) {
        return 0;
    }
    if (done(state, 3) && choice_applied(state, 1) && !done(state, 5) &&
        mutation->publication == C32_PUBLISH_NONE &&
        !emit_action(list, C32_ACTION_PUBLISH_DURABLE, C32_PURPOSE_TOMB,
                     C32_PHYS_APPLIED, 0, 5, 0)) {
        return 0;
    }
    if (done(state, 3) && choice_applied(state, 1) && !done(state, 6) &&
        state->firmware_ram[0].version < C32_MAX_VERSION &&
        !emit_action(list, C32_ACTION_ACCEPT_WRITE, C32_PURPOSE_DATA,
                     C32_PHYS_APPLIED, 1, 6, 1)) {
        return 0;
    }
    return 1;
}

static int generate_checkpoint(
    const struct c32_model_state *state,
    struct c32_action_list *list,
    enum c32_broken_variant broken
)
{
    (void)broken;
    if (!done(state, 0) &&
        !emit_action(list, C32_ACTION_START_CHECKPOINT,
                     C32_PURPOSE_CKPT_IMAGE, C32_PHYS_APPLIED, 1, 0,
                     state->scenario_variant)) {
        return 0;
    }
    if (done(state, 0) && !done(state, 1) &&
        !emit_b_pair(list, C32_PURPOSE_CKPT_IMAGE, 1, 1)) {
        return 0;
    }
    if (!emit_a_if_ready(state, list, C32_PURPOSE_CKPT_IMAGE, 1, 2) ||
        !emit_c_if_ready(state, list, C32_PURPOSE_CKPT_IMAGE, 1, 3) ||
        !emit_delivery_if_ready(state, list, C32_PURPOSE_CKPT_IMAGE, 1, 4,
                                broken)) {
        return 0;
    }
    if (done(state, 3) && choice_applied(state, 1) && !done(state, 5) &&
        !emit_action(list, C32_ACTION_START_CHECKPOINT,
                     C32_PURPOSE_CKPT_ANCHOR, C32_PHYS_APPLIED, 1, 5,
                     state->scenario_variant)) {
        return 0;
    }
    if (done(state, 5) && !done(state, 6) &&
        !emit_b_pair(list, C32_PURPOSE_CKPT_ANCHOR, 1, 6)) {
        return 0;
    }
    if (!emit_a_if_ready(state, list, C32_PURPOSE_CKPT_ANCHOR, 1, 7) ||
        !emit_c_if_ready(state, list, C32_PURPOSE_CKPT_ANCHOR, 1, 8)) {
        return 0;
    }
    return 1;
}

static int generate_gc(
    const struct c32_model_state *state,
    struct c32_action_list *list,
    enum c32_broken_variant broken
)
{
    if (!done(state, 0) &&
        !emit_action(list, C32_ACTION_START_GC_COPY, C32_PURPOSE_GC_COPY,
                     C32_PHYS_APPLIED, 0, 0, 0)) {
        return 0;
    }
    if (done(state, 0) && !done(state, 1) &&
        !emit_b_pair(list, C32_PURPOSE_GC_COPY, 0, 1)) {
        return 0;
    }
    if (!emit_a_if_ready(state, list, C32_PURPOSE_GC_COPY, 0, 2) ||
        !emit_c_if_ready(state, list, C32_PURPOSE_GC_COPY, 0, 3) ||
        !emit_delivery_if_ready(state, list, C32_PURPOSE_GC_COPY, 0, 4,
                                broken)) {
        return 0;
    }
    if (done(state, 4) && choice_applied(state, 1) && !done(state, 5) &&
        !emit_action(list, C32_ACTION_START_GC_COPY,
                     C32_PURPOSE_RELOCATION, C32_PHYS_APPLIED, 0, 5, 0)) {
        return 0;
    }
    if (done(state, 5) && !done(state, 6) &&
        !emit_b_pair(list, C32_PURPOSE_RELOCATION, 0, 6)) {
        return 0;
    }
    if (!emit_a_if_ready(state, list, C32_PURPOSE_RELOCATION, 0, 7) ||
        !emit_c_if_ready(state, list, C32_PURPOSE_RELOCATION, 0, 8) ||
        !emit_delivery_if_ready(state, list, C32_PURPOSE_RELOCATION, 0, 9,
                                broken)) {
        return 0;
    }
    if (done(state, 9) && choice_applied(state, 6) && !done(state, 10) &&
        !emit_action(list, C32_ACTION_RELEASE_GC_LEASE,
                     C32_PURPOSE_ERASE, C32_PHYS_APPLIED, 0, 10, 0)) {
        return 0;
    }
    if (!done(state, 11) &&
        ((done(state, 10) && state->gc.lease_held == 0) ||
         (broken == C32_BM_GC_ERASE_AFTER_COPY && done(state, 3) &&
          choice_applied(state, 1))) &&
        !emit_b_pair(list, C32_PURPOSE_ERASE, 0, 11)) {
        return 0;
    }
    if (!emit_a_if_ready(state, list, C32_PURPOSE_ERASE, 0, 12) ||
        !emit_c_if_ready(state, list, C32_PURPOSE_ERASE, 0, 13)) {
        return 0;
    }
    return 1;
}

static int mutation_closed(const struct c32_mutation *mutation)
{
    unsigned int atom;

    for (atom = 0; atom < C32_ATOMS; ++atom) {
        if ((mutation->atom_mask & (uint8_t)(1u << atom)) != 0 &&
            mutation->closure[atom] != FWLAB_PERSIST_CLOSE_C_MAP &&
            mutation->closure[atom] != FWLAB_PERSIST_CLOSE_PLP &&
            mutation->closure[atom] != FWLAB_PERSIST_CLOSE_NO_COMMIT) {
            return 0;
        }
    }
    return 1;
}

static int generate_fence(
    const struct c32_model_state *state,
    struct c32_action_list *list,
    enum c32_broken_variant broken
)
{
    if (!done(state, 0) &&
        !emit_action(list, C32_ACTION_ACCEPT_WRITE, C32_PURPOSE_DATA,
                     C32_PHYS_APPLIED, 0, 0, 1)) {
        return 0;
    }
    if (done(state, 0) && !done(state, 1) &&
        state->mutation[0].publication == C32_PUBLISH_NONE &&
        !emit_action(list, C32_ACTION_PUBLISH_VOLATILE, C32_PURPOSE_DATA,
                     C32_PHYS_APPLIED, 0, 1, 0)) {
        return 0;
    }
    if (done(state, 0) && !done(state, 2) &&
        !emit_action(list, C32_ACTION_CAPTURE_FENCE, C32_PURPOSE_MAP,
                     C32_PHYS_APPLIED, 0, 2, 0)) {
        return 0;
    }
    if (done(state, 2) && !done(state, 3) &&
        !emit_action(list, C32_ACTION_ACCEPT_WRITE, C32_PURPOSE_DATA,
                     C32_PHYS_APPLIED, 1, 3, 2)) {
        return 0;
    }
    if (done(state, 0) && !done(state, 4) &&
        !emit_action(list, C32_ACTION_CLOSE_NO_COMMIT, C32_PURPOSE_MAP,
                     C32_PHYS_NO_EFFECT, 0, 4, 0)) {
        return 0;
    }
    if (done(state, 2) && !done(state, 5) &&
        (mutation_closed(&state->mutation[0]) ||
         broken == C32_BM_FENCE_LT_FRONTIER) &&
        !emit_action(list, C32_ACTION_PUBLISH_FENCE, C32_PURPOSE_MAP,
                     C32_PHYS_APPLIED, 0, 5, 0)) {
        return 0;
    }
    return 1;
}

static int generate_unrelated(
    const struct c32_model_state *state,
    struct c32_action_list *list
)
{
    if (!done(state, 0) &&
        !emit_action(list, C32_ACTION_ACCEPT_WRITE, C32_PURPOSE_DATA,
                     C32_PHYS_APPLIED, 0, 0, 1)) {
        return 0;
    }
    if (done(state, 0) && !done(state, 1) &&
        !emit_action(list, C32_ACTION_ACCEPT_WRITE, C32_PURPOSE_DATA,
                     C32_PHYS_APPLIED, 1, 1, 2)) {
        return 0;
    }
    if (done(state, 1) && !done(state, 2) &&
        !emit_action(list, C32_ACTION_PLP_PREPARE, C32_PURPOSE_MAP,
                     C32_PHYS_APPLIED, 1, 2, 0)) {
        return 0;
    }
    if (done(state, 2) && !done(state, 3) &&
        !emit_action(list, C32_ACTION_PLP_ADMIT, C32_PURPOSE_MAP,
                     C32_PHYS_APPLIED, 1, 3, 0)) {
        return 0;
    }
    if (done(state, 3) && !done(state, 4) &&
        !emit_action(list, C32_ACTION_PUBLISH_DURABLE, C32_PURPOSE_MAP,
                     C32_PHYS_APPLIED, 1, 4, 0)) {
        return 0;
    }
    return 1;
}

static int generate_plp(
    const struct c32_model_state *state,
    struct c32_action_list *list
)
{
    uint8_t mask = state->scenario_variant == 0 ? 1 : 3;
    unsigned int cost = bit_count(mask);

    if (!done(state, 0) &&
        !emit_action(list, C32_ACTION_ACCEPT_WRITE, C32_PURPOSE_DATA,
                     C32_PHYS_APPLIED, 0, 0, mask)) {
        return 0;
    }
    if (done(state, 0) && !done(state, 1) &&
        !emit_action(list, C32_ACTION_PLP_PREPARE, C32_PURPOSE_MAP,
                     C32_PHYS_APPLIED, 0, 1, 0)) {
        return 0;
    }
    if (done(state, 1) && !done(state, 2) &&
        state->profile.plp_capacity_credits >= cost &&
        !emit_action(list, C32_ACTION_PLP_ADMIT, C32_PURPOSE_MAP,
                     C32_PHYS_APPLIED, 0, 2, 0)) {
        return 0;
    }
    if (done(state, 2) && !done(state, 3) &&
        !emit_action(list, C32_ACTION_PUBLISH_DURABLE, C32_PURPOSE_MAP,
                     C32_PHYS_APPLIED, 0, 3, 0)) {
        return 0;
    }
    return 1;
}

static int generate_epoch(
    const struct c32_model_state *state,
    struct c32_action_list *list,
    enum c32_broken_variant broken
)
{
    if (!done(state, 0) && broken == C32_BM_DELIVERY_MATCH_SLOT_ONLY &&
        !emit_action(list, C32_ACTION_DELIVER_OUTCOME, C32_PURPOSE_DATA,
                     C32_PHYS_APPLIED, 0, 0, 0)) {
        return 0;
    }
    return 1;
}

static int generate_actions(
    const struct c32_model_state *state,
    enum c32_broken_variant broken,
    struct c32_action_list *list
)
{
    memset(list, 0, sizeof(*list));
    switch ((enum c32_scenario_family)state->scenario_family) {
    case C32_SCENARIO_SINGLE_ATOM:
        return generate_single(state, list);
    case C32_SCENARIO_DUAL_ATOM:
        return generate_dual(state, list);
    case C32_SCENARIO_TRIM:
        return generate_trim(state, list);
    case C32_SCENARIO_CHECKPOINT:
        return generate_checkpoint(state, list, broken);
    case C32_SCENARIO_GC:
        return generate_gc(state, list, broken);
    case C32_SCENARIO_FENCE:
        return generate_fence(state, list, broken);
    case C32_SCENARIO_UNRELATED:
        return generate_unrelated(state, list);
    case C32_SCENARIO_PLP:
        return generate_plp(state, list);
    case C32_SCENARIO_EPOCH:
        return generate_epoch(state, list, broken);
    case C32_SCENARIO_INVALID_MEDIA:
    case C32_SCENARIO_LIMITS_HOST:
        return 1;
    default:
        return 0;
    }
}

static int allocate_slot(
    struct c32_model_state *state,
    enum c32_op_purpose purpose,
    enum c32_broken_variant broken,
    uint8_t *group_out,
    uint8_t *slot_out
)
{
    unsigned int first_group =
        purpose == C32_PURPOSE_GC_COPY ||
        purpose == C32_PURPOSE_RELOCATION ? 1u : 0u;
    unsigned int group;
    unsigned int slot;

    for (group = first_group; group < C32_GROUPS; ++group) {
        for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
            uint16_t bit = slot_bit(group, slot);

            if ((state->free_bitmap & bit) == 0) {
                continue;
            }
            if (broken != C32_BM_ALLOC_LEAVES_FREE_BIT) {
                state->free_bitmap &= (uint16_t)~bit;
            }
            state->reserved_bitmap |= bit;
            *group_out = (uint8_t)group;
            *slot_out = (uint8_t)slot;
            return 1;
        }
    }
    return 0;
}

static struct c32_physical_op *free_operation(
    struct c32_model_state *state
)
{
    unsigned int index;

    for (index = 0; index < C32_PHYSICAL_OPS; ++index) {
        if (state->inflight[index].phase == C32_OP_FREE) {
            return &state->inflight[index];
        }
    }
    return NULL;
}

static int start_media_operation(
    struct c32_model_state *state,
    struct c32_physical_op *operation,
    const struct c32_model_action *action,
    enum c32_broken_variant broken
)
{
    struct c32_mutation *mutation = &state->mutation[action->subject];
    struct c32_persistent_record record;
    struct c32_persistent_record dependency;
    uint8_t group;
    uint8_t slot;
    unsigned int atom = 0;

    if (action->purpose == C32_PURPOSE_ERASE) {
        operation->target_domain = C32_TARGET_ERASE_GROUP;
        operation->target_group = state->gc.source.group;
        return operation->target_group < C32_GROUPS;
    }
    if (!allocate_slot(state, (enum c32_op_purpose)action->purpose, broken,
                       &group, &slot)) {
        return 0;
    }
    operation->target_domain = C32_TARGET_MEDIA;
    operation->target_group = group;
    operation->target_slot = slot;
    if (action->purpose == C32_PURPOSE_GC_COPY) {
        record = data_record(state, group, slot, state->gc.atom,
                             state->gc.version,
                             state->firmware_ram[state->gc.atom].value_token,
                             UINT16_C(300), 0,
                             operation->commit_sequence);
    } else if (action->purpose == C32_PURPOSE_RELOCATION) {
        if (!find_record_id(state, UINT16_C(300), &dependency)) {
            return 0;
        }
        record = metadata_record(
            state, group, slot, C32_REC_RELOCATION, state->gc.atom,
            state->gc.version,
            state->firmware_ram[state->gc.atom].value_token,
            state->firmware_ram[state->gc.atom].state_id, UINT16_C(301), 0,
            operation->commit_sequence, dependency.self);
        record.predecessor_version = state->gc.version;
        record.copy_discriminator = 1;
        record.source_ref = state->gc.source;
    } else {
        if (!mutation->used) {
            return 0;
        }
        while (atom < C32_ATOMS &&
               (mutation->atom_mask & (uint8_t)(1u << atom)) == 0) {
            ++atom;
        }
        if (atom == C32_ATOMS) {
            return 0;
        }
        if (action->purpose == C32_PURPOSE_DATA) {
            record = data_record(
                state, group, slot, atom, mutation->target_version[atom],
                mutation->value_token[atom],
                operation_record_id(action->subject, C32_PURPOSE_DATA),
                mutation->mutation_id, operation->commit_sequence);
        } else if (action->purpose == C32_PURPOSE_MAP) {
            if (!find_record_by_mutation(state, mutation->mutation_id,
                                         C32_REC_DATA, &dependency)) {
                return 0;
            }
            record = metadata_record(
                state, group, slot, C32_REC_MAP, atom,
                mutation->target_version[atom], mutation->value_token[atom],
                state->firmware_ram[atom].predecessor_state_id,
                operation_record_id(action->subject, C32_PURPOSE_MAP),
                mutation->mutation_id, operation->commit_sequence,
                dependency.self);
            record.predecessor_version = mutation->predecessor_version[atom];
        } else if (action->purpose == C32_PURPOSE_TOMB) {
            record = metadata_record(
                state, group, slot, C32_REC_TOMBSTONE, atom,
                mutation->target_version[atom], 0,
                state->firmware_ram[atom].predecessor_state_id,
                operation_record_id(action->subject, C32_PURPOSE_TOMB),
                mutation->mutation_id, operation->commit_sequence,
                (struct c32_phys_ref){0, 0, 0, 0});
            record.predecessor_version = mutation->predecessor_version[atom];
        } else {
            return 0;
        }
    }
    record.c_applied = 0;
    operation->frozen_record = record;
    return 1;
}

static int start_checkpoint_operation(
    struct c32_model_state *state,
    struct c32_physical_op *operation,
    const struct c32_model_action *action
)
{
    unsigned int atom;
    uint64_t payload_hash = UINT64_C(0x22220002);

    if (action->subject >= C32_CHECKPOINTS) {
        return 0;
    }
    operation->target_slot = action->subject;
    if (action->purpose == C32_PURPOSE_CKPT_IMAGE) {
        operation->target_domain = C32_TARGET_CHECKPOINT;
        operation->frozen_checkpoint.image_state = C32_IMAGE_VALID;
        operation->frozen_checkpoint.generation = 2;
        operation->frozen_checkpoint.body_complete =
            state->scenario_variant == 1 ? 0 : 1;
        operation->frozen_checkpoint.checksum_ok = 1;
        operation->frozen_checkpoint.provenance_ok =
            state->scenario_variant == 2 ? 0 : 1;
        operation->frozen_checkpoint.watermark = 4;
        operation->frozen_checkpoint.c_sequence = 4;
        operation->frozen_checkpoint.payload_hash = payload_hash;
        for (atom = 0; atom < C32_ATOMS; ++atom) {
            operation->frozen_checkpoint.entry[atom] =
                state->durable_floor[atom];
        }
    } else {
        operation->target_domain = C32_TARGET_ANCHOR;
        operation->frozen_anchor.image_state = C32_IMAGE_VALID;
        operation->frozen_anchor.generation = 2;
        operation->frozen_anchor.target_slot = action->subject;
        operation->frozen_anchor.body_complete = 1;
        operation->frozen_anchor.checksum_ok = 1;
        operation->frozen_anchor.watermark = 4;
        operation->frozen_anchor.c_sequence = 4;
        operation->frozen_anchor.checkpoint_hash = payload_hash;
    }
    return 1;
}

static int start_operation(
    struct c32_model_state *state,
    const struct c32_model_action *action,
    enum c32_broken_variant broken
)
{
    struct c32_physical_op *operation = free_operation(state);

    if (operation == NULL || state->next_begin_order > 3) {
        return 0;
    }
    memset(operation, 0, sizeof(*operation));
    operation->phase = C32_OP_B;
    operation->purpose = action->purpose;
    operation->frozen_outcome = action->outcome;
    operation->op_id = (uint16_t)(state->next_begin_order + 1u);
    operation->owner_epoch = state->current_epoch;
    operation->begin_order = state->next_begin_order++;
    operation->commit_sequence = state->next_commit_sequence++;
    if (action->purpose == C32_PURPOSE_CKPT_IMAGE ||
        action->purpose == C32_PURPOSE_CKPT_ANCHOR) {
        return start_checkpoint_operation(state, operation, action);
    }
    return start_media_operation(state, operation, action, broken);
}

static int model_materialize(
    struct c32_model_state *state,
    struct c32_physical_op *operation,
    int committed
)
{
    unsigned int slot;

    if (operation->target_domain == C32_TARGET_MEDIA) {
        struct c32_persistent_record record = operation->frozen_record;

        record.self = phys_ref(state, operation->target_group,
                               operation->target_slot);
        record.c_applied = committed != 0;
        state->media[operation->target_group][operation->target_slot] = record;
        return 1;
    }
    if (operation->target_domain == C32_TARGET_CHECKPOINT) {
        state->checkpoint[operation->target_slot] =
            operation->frozen_checkpoint;
        state->checkpoint[operation->target_slot].c_applied = committed != 0;
        return 1;
    }
    if (operation->target_domain == C32_TARGET_ANCHOR) {
        state->anchor[operation->target_slot] = operation->frozen_anchor;
        state->anchor[operation->target_slot].c_applied = committed != 0;
        return 1;
    }
    if (operation->target_domain == C32_TARGET_ERASE_GROUP && committed) {
        if (operation->target_group >= C32_GROUPS ||
            state->erase_generation[operation->target_group] >= 1) {
            return 0;
        }
        ++state->erase_generation[operation->target_group];
        for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
            memset(&state->media[operation->target_group][slot], 0,
                   sizeof(state->media[operation->target_group][slot]));
            state->free_bitmap |= slot_bit(operation->target_group, slot);
        }
        return 1;
    }
    return operation->target_domain == C32_TARGET_ERASE_GROUP;
}

static int apply_a(
    struct c32_model_state *state,
    const struct c32_model_action *action
)
{
    struct c32_physical_op *operation = find_operation(
        state, (enum c32_op_purpose)action->purpose, action->subject);

    if (operation == NULL || operation->phase != C32_OP_B) {
        return 0;
    }
    if (operation->frozen_outcome == C32_PHYS_APPLIED) {
        operation->phase = C32_OP_A_APPLIED;
        return model_materialize(state, operation, 0);
    }
    operation->phase = C32_OP_A_NO_EFFECT;
    return 1;
}

static void close_operation_mutation(
    struct c32_model_state *state,
    const struct c32_physical_op *operation,
    int applied
)
{
    unsigned int mutation_index;
    unsigned int atom;

    if (operation->purpose == C32_PURPOSE_GC_COPY) {
        if (applied) {
            state->gc.stage = 2;
            state->gc.destination = operation->frozen_record.self;
            state->gc.destination.group = operation->target_group;
            state->gc.destination.slot = operation->target_slot;
            state->gc.destination.erase_generation =
                state->erase_generation[operation->target_group];
            state->gc.destination.valid = 1;
        }
        return;
    }
    if (operation->purpose == C32_PURPOSE_RELOCATION) {
        if (applied) {
            state->gc.stage = 3;
        }
        return;
    }
    if (operation->purpose == C32_PURPOSE_ERASE) {
        if (applied) {
            state->gc.stage = 5;
        }
        return;
    }
    if (operation->purpose != C32_PURPOSE_MAP &&
        operation->purpose != C32_PURPOSE_TOMB) {
        return;
    }
    mutation_index = operation->frozen_record.mutation_id - 1u;
    if (mutation_index >= C32_MUTATIONS) {
        return;
    }
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        if ((state->mutation[mutation_index].atom_mask &
             (uint8_t)(1u << atom)) != 0) {
            state->mutation[mutation_index].closure[atom] =
                applied ? FWLAB_PERSIST_CLOSE_C_MAP :
                          FWLAB_PERSIST_CLOSE_NO_COMMIT;
        }
    }
}

static int apply_c(
    struct c32_model_state *state,
    const struct c32_model_action *action
)
{
    struct c32_physical_op *operation = find_operation(
        state, (enum c32_op_purpose)action->purpose, action->subject);
    uint16_t bit = 0;
    int applied;

    if (operation == NULL ||
        (operation->phase != C32_OP_A_APPLIED &&
         operation->phase != C32_OP_A_NO_EFFECT)) {
        return 0;
    }
    applied = operation->phase == C32_OP_A_APPLIED;
    if (operation->target_domain == C32_TARGET_MEDIA) {
        bit = slot_bit(operation->target_group, operation->target_slot);
    }
    if (applied) {
        if (!model_materialize(state, operation, 1)) {
            return 0;
        }
        operation->phase = C32_OP_C_APPLIED;
    } else {
        if (bit != 0) {
            state->free_bitmap |= bit;
            memset(&state->media[operation->target_group]
                                [operation->target_slot], 0,
                   sizeof(state->media[operation->target_group]
                                      [operation->target_slot]));
        }
        operation->phase = C32_OP_C_NO_EFFECT;
    }
    state->reserved_bitmap &= (uint16_t)~bit;
    close_operation_mutation(state, operation, applied);
    return 1;
}

static int accept_mutation(
    struct c32_model_state *state,
    const struct c32_model_action *action,
    enum fwlab_persist_mutation_kind kind
)
{
    struct c32_mutation *mutation;
    uint8_t atom_mask = action->variant;
    unsigned int atom;

    if (action->subject >= C32_MUTATIONS || atom_mask == 0 ||
        (atom_mask & ~UINT8_C(0x03)) != 0 ||
        state->next_accept_sequence > 1) {
        return 0;
    }
    mutation = &state->mutation[action->subject];
    if (mutation->used) {
        return 0;
    }
    memset(mutation, 0, sizeof(*mutation));
    mutation->used = 1;
    mutation->atom_mask = atom_mask;
    mutation->request_kind = state->scenario_request;
    mutation->mutation_id = (uint16_t)(action->subject + 1u);
    mutation->command_id = (uint16_t)(action->subject + 1u);
    mutation->owner_epoch = state->current_epoch;
    mutation->accept_sequence = state->next_accept_sequence++;
    mutation->scope = state->scenario_family == C32_SCENARIO_UNRELATED ?
                      (uint16_t)(action->subject + 1u) : 1;
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        struct c32_logical_state predecessor = state->firmware_ram[atom];

        if ((atom_mask & (uint8_t)(1u << atom)) == 0 ||
            predecessor.version >= C32_MAX_VERSION) {
            continue;
        }
        mutation->target_kind[atom] =
            kind == FWLAB_PERSIST_TRIM ? C32_LOGICAL_TOMBSTONE :
                                        C32_LOGICAL_VALUE;
        mutation->target_version[atom] = (uint8_t)(predecessor.version + 1u);
        mutation->predecessor_version[atom] = predecessor.version;
        mutation->value_token[atom] =
            kind == FWLAB_PERSIST_TRIM ? 0 :
            (uint8_t)(2u + action->subject);
        state->firmware_ram[atom].kind = mutation->target_kind[atom];
        state->firmware_ram[atom].version = mutation->target_version[atom];
        state->firmware_ram[atom].value_token = mutation->value_token[atom];
        state->firmware_ram[atom].predecessor_state_id =
            predecessor.state_id;
        state->firmware_ram[atom].state_id = 0;
        state->firmware_ram[atom].authority_record_id = 0;
    }
    return 1;
}

static struct c32_plp_envelope *plp_for_subject(
    struct c32_model_state *state,
    unsigned int subject
)
{
    unsigned int index;
    uint16_t mutation_id = (uint16_t)(subject + 1u);

    for (index = 0; index < C32_PLP_SLOTS; ++index) {
        if (state->plp[index].state != C32_PLP_FREE &&
            state->plp[index].mutation_id == mutation_id) {
            return &state->plp[index];
        }
    }
    return NULL;
}

static int prepare_plp(
    struct c32_model_state *state,
    unsigned int subject
)
{
    struct c32_plp_envelope *envelope;
    struct c32_mutation *mutation;
    unsigned int index;
    unsigned int atom;

    if (subject >= C32_MUTATIONS || !state->mutation[subject].used ||
        state->profile.plp_kind != FWLAB_PERSIST_PLP_VALIDATED) {
        return 0;
    }
    mutation = &state->mutation[subject];
    envelope = plp_for_subject(state, subject);
    if (envelope != NULL) {
        return 0;
    }
    for (index = 0; index < C32_PLP_SLOTS; ++index) {
        if (state->plp[index].state == C32_PLP_FREE) {
            envelope = &state->plp[index];
            break;
        }
    }
    if (envelope == NULL) {
        return 0;
    }
    memset(envelope, 0, sizeof(*envelope));
    envelope->state = C32_PLP_PREPARED;
    envelope->atom_mask = mutation->atom_mask;
    envelope->capacity_cost = (uint8_t)bit_count(mutation->atom_mask);
    envelope->flags = FWLAB_PLP_BODY_COMPLETE |
                      FWLAB_PLP_CHECKSUM_OK |
                      FWLAB_PLP_CAPACITY_RESERVED |
                      FWLAB_PLP_DRAIN_BUDGET_RESERVED;
    envelope->survival_event_mask = state->profile.survival_event_mask;
    envelope->drain_budget_reserved = envelope->capacity_cost;
    envelope->envelope_id = (uint16_t)(index + 1u);
    envelope->mutation_id = mutation->mutation_id;
    envelope->owner_epoch = mutation->owner_epoch;
    envelope->accept_sequence = mutation->accept_sequence;
    envelope->persistent_order = (uint16_t)(index + 1u);
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        struct c32_plp_atom *redo = &envelope->atom[atom];

        if ((mutation->atom_mask & (uint8_t)(1u << atom)) == 0) {
            continue;
        }
        redo->atom = (uint8_t)atom;
        redo->version = mutation->target_version[atom];
        redo->target_kind = mutation->target_kind[atom];
        redo->predecessor_version = mutation->predecessor_version[atom];
        redo->value_token = mutation->value_token[atom];
        redo->predecessor_state_id =
            state->firmware_ram[atom].predecessor_state_id;
        redo->drain_data_ref = phys_ref(state, 0, atom * 2u);
        redo->drain_metadata_ref = phys_ref(state, 0, atom * 2u + 1u);
    }
    return 1;
}

static int persist_plp_atom(
    struct c32_model_state *state,
    struct c32_plp_envelope *envelope,
    unsigned int atom
)
{
    struct c32_plp_atom *redo = &envelope->atom[atom];
    struct c32_persistent_record data;
    struct c32_persistent_record metadata;

    if (redo->target_kind == C32_LOGICAL_VALUE) {
        data = data_record(
            state, redo->drain_data_ref.group, redo->drain_data_ref.slot,
            atom, redo->version, redo->value_token,
            (uint16_t)(envelope->envelope_id * 8u + atom * 2u + 1u),
            envelope->mutation_id,
            (uint16_t)(envelope->persistent_order * 4u + atom * 2u + 1u));
        occupy_record(state, redo->drain_data_ref.group,
                      redo->drain_data_ref.slot, &data);
    }
    metadata = metadata_record(
        state, redo->drain_metadata_ref.group,
        redo->drain_metadata_ref.slot,
        redo->target_kind == C32_LOGICAL_TOMBSTONE ? C32_REC_TOMBSTONE :
                                                     C32_REC_MAP,
        atom, redo->version, redo->value_token, redo->predecessor_state_id,
        (uint16_t)(envelope->envelope_id * 8u + atom * 2u + 2u),
        envelope->mutation_id,
        (uint16_t)(envelope->persistent_order * 4u + atom * 2u + 2u),
        redo->drain_data_ref);
    occupy_record(state, redo->drain_metadata_ref.group,
                  redo->drain_metadata_ref.slot, &metadata);
    envelope->drained_atom_mask |= (uint8_t)(1u << atom);
    return 1;
}

static int admit_plp(
    struct c32_model_state *state,
    unsigned int subject
)
{
    struct c32_plp_envelope *envelope = plp_for_subject(state, subject);
    unsigned int cost;

    if (envelope == NULL || envelope->state != C32_PLP_PREPARED) {
        return 0;
    }
    cost = bit_count(envelope->atom_mask);
    if (cost == 0 || cost > state->profile.plp_capacity_credits) {
        return 0;
    }
    envelope->flags = FWLAB_PLP_REQUIRED_FLAGS;
    envelope->state = C32_PLP_ADMITTED;
    if (state->scenario_family == C32_SCENARIO_PLP &&
        state->scenario_variant == 2 && (envelope->atom_mask & 1u) != 0 &&
        !persist_plp_atom(state, envelope, 0)) {
        return 0;
    }
    for (cost = 0; cost < C32_ATOMS; ++cost) {
        if ((state->mutation[subject].atom_mask & (uint8_t)(1u << cost)) != 0) {
            state->mutation[subject].closure[cost] =
                FWLAB_PERSIST_CLOSE_PLP;
        }
    }
    return 1;
}

static int mutation_logical_state(
    struct c32_model_state *state,
    unsigned int subject,
    unsigned int atom,
    struct c32_logical_state *logical
)
{
    struct c32_mutation *mutation = &state->mutation[subject];
    struct c32_persistent_record metadata;

    memset(logical, 0, sizeof(*logical));
    logical->kind = mutation->target_kind[atom];
    logical->atom = (uint8_t)atom;
    logical->version = mutation->target_version[atom];
    logical->value_token = mutation->value_token[atom];
    logical->predecessor_state_id =
        state->firmware_ram[atom].predecessor_state_id;
    if (mutation->closure[atom] == FWLAB_PERSIST_CLOSE_PLP) {
        struct c32_plp_envelope *envelope = plp_for_subject(state, subject);

        if (envelope == NULL) {
            return 0;
        }
        logical->state_id = (uint16_t)(envelope->envelope_id * 8u +
                                       atom * 2u + 2u);
        logical->authority_record_id = logical->state_id;
        logical->data_ref = envelope->atom[atom].drain_data_ref;
        return 1;
    }
    if (!find_record_by_mutation(
            state, mutation->mutation_id,
            mutation->target_kind[atom] == C32_LOGICAL_TOMBSTONE ?
                C32_REC_TOMBSTONE : C32_REC_MAP,
            &metadata)) {
        return 0;
    }
    *logical = logical_from_record(&metadata);
    return 1;
}

static int publish_mutation(
    struct c32_model_state *state,
    unsigned int subject,
    uint8_t publication
)
{
    struct c32_mutation *mutation;
    unsigned int atom;

    if (subject >= C32_MUTATIONS || !state->mutation[subject].used) {
        return 0;
    }
    mutation = &state->mutation[subject];
    if (mutation->publication != C32_PUBLISH_NONE) {
        return 0;
    }
    if (publication == C32_PUBLISH_DURABLE) {
        for (atom = 0; atom < C32_ATOMS; ++atom) {
            struct c32_logical_state logical;

            if ((mutation->atom_mask & (uint8_t)(1u << atom)) == 0) {
                continue;
            }
            if ((mutation->closure[atom] != FWLAB_PERSIST_CLOSE_C_MAP &&
                 mutation->closure[atom] != FWLAB_PERSIST_CLOSE_PLP) ||
                !mutation_logical_state(state, subject, atom, &logical)) {
                return 0;
            }
            state->durable_floor[atom] = logical;
        }
    }
    mutation->publication = publication;
    return 1;
}

static int close_mutation(
    struct c32_model_state *state,
    unsigned int subject,
    uint8_t closure,
    uint8_t publication
)
{
    unsigned int atom;
    struct c32_mutation *mutation;

    if (subject >= C32_MUTATIONS || !state->mutation[subject].used) {
        return 0;
    }
    mutation = &state->mutation[subject];
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        if ((mutation->atom_mask & (uint8_t)(1u << atom)) != 0) {
            mutation->closure[atom] = closure;
        }
    }
    if (mutation->publication == C32_PUBLISH_NONE) {
        mutation->publication = publication;
    }
    return 1;
}

static int inject_unique_conflict(struct c32_model_state *state)
{
    struct c32_persistent_record original_map;
    struct c32_persistent_record original_data;
    struct c32_persistent_record duplicate_data;
    struct c32_persistent_record duplicate_map;
    uint8_t data_group;
    uint8_t data_slot;
    uint8_t map_group;
    uint8_t map_slot;

    if (!find_record_by_mutation(state, 1, C32_REC_MAP, &original_map)) {
        return 0;
    }
    if (original_map.data_ref.valid == 0 ||
        original_map.data_ref.group >= C32_GROUPS ||
        original_map.data_ref.slot >= C32_SLOTS_PER_GROUP) {
        return 0;
    }
    if (!allocate_slot(state, C32_PURPOSE_DATA, C32_BROKEN_NONE,
                       &data_group, &data_slot)) {
        return 0;
    }
    if (!allocate_slot(state, C32_PURPOSE_MAP, C32_BROKEN_NONE,
                       &map_group, &map_slot)) {
        return 0;
    }
    original_data = state->media[original_map.data_ref.group]
                                [original_map.data_ref.slot];
    if (original_data.presence != C32_RECORD_VALID ||
        original_data.kind != C32_REC_DATA) {
        return 0;
    }
    duplicate_data = original_data;
    duplicate_data.record_id = (uint16_t)(original_data.record_id + 50u);
    duplicate_data.value_token = (uint8_t)(original_data.value_token + 1u);
    duplicate_data.self = phys_ref(state, data_group, data_slot);
    duplicate_map = original_map;
    duplicate_map.record_id = (uint16_t)(original_map.record_id + 50u);
    duplicate_map.value_token = duplicate_data.value_token;
    duplicate_map.data_ref = duplicate_data.self;
    duplicate_map.self = phys_ref(state, map_group, map_slot);
    occupy_record(state, data_group, data_slot, &duplicate_data);
    occupy_record(state, map_group, map_slot, &duplicate_map);
    state->reserved_bitmap &= (uint16_t)~slot_bit(data_group, data_slot);
    state->reserved_bitmap &= (uint16_t)~slot_bit(map_group, map_slot);
    return 1;
}

static int apply_action(
    const struct c32_model_state *source,
    const struct c32_model_action *action,
    enum c32_broken_variant broken,
    struct c32_model_state *next
)
{
    struct c32_physical_op *operation;
    int result = 0;

    *next = *source;
    switch ((enum c32_model_action_kind)action->kind) {
    case C32_ACTION_ACCEPT_WRITE:
        result = accept_mutation(next, action, FWLAB_PERSIST_WRITE);
        break;
    case C32_ACTION_ACCEPT_TRIM:
        result = accept_mutation(next, action, FWLAB_PERSIST_TRIM);
        break;
    case C32_ACTION_CAPTURE_FENCE:
        if (!next->fence.used && next->next_accept_sequence != 0) {
            next->fence.used = 1;
            next->fence.fence_id = 1;
            next->fence.owner_epoch = next->current_epoch;
            next->fence.scope = 1;
            next->fence.frontier =
                (uint16_t)(next->next_accept_sequence - 1u);
            result = 1;
        }
        break;
    case C32_ACTION_PLP_PREPARE:
        result = prepare_plp(next, action->subject);
        break;
    case C32_ACTION_PLP_ADMIT:
        result = admit_plp(next, action->subject);
        break;
    case C32_ACTION_B_PHYS:
        result = start_operation(next, action, broken);
        break;
    case C32_ACTION_A_PHYS:
        result = apply_a(next, action);
        break;
    case C32_ACTION_C_PHYS:
        result = apply_c(next, action);
        if (result && broken == C32_BM_UNIQUE_KEEP_PREDECESSOR &&
            action->purpose == C32_PURPOSE_MAP &&
            next->mutation[action->subject].publication ==
                C32_PUBLISH_VOLATILE &&
            find_record_by_mutation(next,
                                    (uint16_t)(action->subject + 1u),
                                    C32_REC_MAP, NULL)) {
            result = inject_unique_conflict(next);
        }
        break;
    case C32_ACTION_DELIVER_OUTCOME:
        operation = find_operation(next,
            (enum c32_op_purpose)action->purpose, action->subject);
        if (operation != NULL &&
            (((operation->phase == C32_OP_C_APPLIED ||
               operation->phase == C32_OP_C_NO_EFFECT) &&
              operation->owner_epoch == next->current_epoch) ||
             (broken == C32_BM_DELIVERY_MATCH_SLOT_ONLY &&
              operation->phase != C32_OP_FREE))) {
            operation->outcome_delivered = 1;
            if (operation->owner_epoch == next->current_epoch) {
                memset(operation, 0, sizeof(*operation));
            }
            result = 1;
        }
        break;
    case C32_ACTION_PUBLISH_VOLATILE:
        result = publish_mutation(next, action->subject,
                                  C32_PUBLISH_VOLATILE);
        if (result && broken == C32_BM_UNIQUE_KEEP_PREDECESSOR &&
            find_record_by_mutation(next, 1, C32_REC_MAP, NULL)) {
            result = inject_unique_conflict(next);
        }
        break;
    case C32_ACTION_PUBLISH_DURABLE:
        result = publish_mutation(next, action->subject,
                                  C32_PUBLISH_DURABLE);
        break;
    case C32_ACTION_CLOSE_NO_COMMIT:
        result = close_mutation(next, action->subject,
                                FWLAB_PERSIST_CLOSE_NO_COMMIT,
                                C32_PUBLISH_FAILED_NO_COMMIT);
        break;
    case C32_ACTION_CLOSE_INDETERMINATE:
        result = close_mutation(next, action->subject,
                                FWLAB_PERSIST_CLOSE_INDETERMINATE,
                                C32_PUBLISH_FAILED_INDETERMINATE);
        break;
    case C32_ACTION_PUBLISH_FENCE:
        if (next->fence.used &&
            (mutation_closed(&next->mutation[0]) ||
             broken == C32_BM_FENCE_LT_FRONTIER)) {
            next->fence.success = 1;
            result = 1;
        }
        break;
    case C32_ACTION_START_CHECKPOINT:
        result = 1;
        break;
    case C32_ACTION_START_GC_COPY:
        if (action->purpose == C32_PURPOSE_GC_COPY && !next->gc.used) {
            next->gc.used = 1;
            next->gc.atom = 0;
            next->gc.version = next->firmware_ram[0].version;
            next->gc.stage = 1;
            next->gc.lease_held = 1;
            next->gc.source = next->firmware_ram[0].data_ref;
            result = 1;
        } else if (action->purpose == C32_PURPOSE_RELOCATION &&
                   next->gc.stage == 2) {
            result = 1;
        }
        break;
    case C32_ACTION_RELEASE_GC_LEASE:
        if (next->gc.used && next->gc.stage == 3 && next->gc.lease_held) {
            next->gc.lease_held = 0;
            next->gc.stage = 4;
            result = 1;
        }
        break;
    default:
        break;
    }
    if (!result) {
        return 0;
    }
    if (broken == C32_BM_GC_ERASE_AFTER_COPY &&
        action->kind == C32_ACTION_B_PHYS &&
        action->purpose == C32_PURPOSE_ERASE && next->gc.lease_held) {
        next->gc.stage = 4;
    }
    next->grammar_progress |= C32_STEP_BIT(action->step);
    if (action->kind == C32_ACTION_B_PHYS &&
        action->outcome == C32_PHYS_APPLIED) {
        next->grammar_choice |= C32_STEP_BIT(action->step);
    }
    return 1;
}

static int canonical_equal(
    const struct c32_model_state *left,
    const struct c32_model_state *right
)
{
    uint8_t left_bytes[C32_CANONICAL_BYTES];
    uint8_t right_bytes[C32_CANONICAL_BYTES];
    size_t left_length = c32_state_encode(left, left_bytes,
                                          sizeof(left_bytes));
    size_t right_length = c32_state_encode(right, right_bytes,
                                           sizeof(right_bytes));

    return left_length != 0 && left_length == right_length &&
           memcmp(left_bytes, right_bytes, left_length) == 0;
}

/* 1 = inserted, 0 = duplicate, -1 = hard capacity/canonical failure. */
static int visited_insert(
    const struct c32_model_state *state,
    uint32_t node_index,
    struct c32_model_report *report
)
{
    uint64_t hash = c32_state_hash(state);
    uint32_t slot;
    uint32_t probes;

    if (hash == 0) {
        return -1;
    }
    slot = (uint32_t)hash & (C32_HASH_SLOTS - 1u);
    for (probes = 0; probes < C32_HASH_SLOTS; ++probes) {
        uint32_t existing = visited_slots[slot];

        if (existing == 0) {
            visited_slots[slot] = node_index + 1u;
            return 1;
        }
        if (c32_state_hash(&bfs_nodes[existing - 1u].state) == hash) {
            if (canonical_equal(state, &bfs_nodes[existing - 1u].state)) {
                if (report != NULL) {
                    ++report->duplicate_states;
                }
                return 0;
            }
            if (report != NULL) {
                ++report->canonical_collisions;
            }
        }
        slot = (slot + 1u) & (C32_HASH_SLOTS - 1u);
    }
    return -1;
}

static uint16_t logical_bit(const struct c32_logical_state *logical)
{
    return logical->kind == C32_LOGICAL_TOMBSTONE ? UINT16_C(1) << 8 :
           (uint16_t)(UINT16_C(1) << logical->version);
}

static void allowed_sets(
    const struct c32_model_state *state,
    uint16_t allowed[C32_ATOMS]
)
{
    unsigned int atom;
    unsigned int mutation_index;

    for (atom = 0; atom < C32_ATOMS; ++atom) {
        allowed[atom] = logical_bit(&state->durable_floor[atom]);
    }
    for (mutation_index = 0; mutation_index < C32_MUTATIONS;
         ++mutation_index) {
        const struct c32_mutation *mutation = &state->mutation[mutation_index];

        if (!mutation->used) {
            continue;
        }
        for (atom = 0; atom < C32_ATOMS; ++atom) {
            uint16_t target;

            if ((mutation->atom_mask & (uint8_t)(1u << atom)) == 0) {
                continue;
            }
            target = mutation->target_kind[atom] == C32_LOGICAL_TOMBSTONE ?
                     UINT16_C(1) << 8 :
                     (uint16_t)(UINT16_C(1) <<
                                mutation->target_version[atom]);
            if (mutation->publication == C32_PUBLISH_DURABLE ||
                mutation->closure[atom] == FWLAB_PERSIST_CLOSE_C_MAP ||
                mutation->closure[atom] == FWLAB_PERSIST_CLOSE_PLP) {
                allowed[atom] = target;
            } else if (mutation->publication == C32_PUBLISH_VOLATILE ||
                       mutation->publication ==
                           C32_PUBLISH_FAILED_INDETERMINATE) {
                allowed[atom] |= target;
            }
        }
    }
}

static uint64_t oracle_result_hash(
    uint64_t hash,
    const struct c32_invariant_result *result
)
{
    hash = hash_u64(hash, result->passed);
    hash = hash_u64(hash, result->invariant_id);
    hash = hash_u64(hash, result->atom);
    hash = hash_u64(hash, result->record_id);
    return hash_u64(hash, result->reason);
}

static int check_cut(
    const struct c32_model_state *state,
    enum c32_cut_kind event,
    enum c32_broken_variant broken,
    uint16_t *violation_mask,
    uint64_t *oracle_hash,
    struct c32_physical_result *physical_out,
    struct c32_recovery_result *recovered_out,
    struct c32_model_report *report
)
{
    struct c32_physical_result physical;
    struct c32_recovery_result recovered;
    unsigned int invariant;
    uint16_t mask = 0;
    uint64_t hash = UINT64_C(1469598103934665603);

    if (!c32_physical_settle(state, event, broken, &physical)) {
        return 0;
    }
    memset(&recovered, 0, sizeof(recovered));
    recovered.selected_checkpoint = UINT8_MAX;
    if (physical.status == C32_RECOVERY_OK) {
        if (!c32_logical_recover(&physical.image, broken, &recovered)) {
            return 0;
        }
    } else {
        recovered.status = C32_RECOVERY_FAIL_CLOSED;
        memcpy(recovered.atom, state->genesis, sizeof(recovered.atom));
        recovered.hash = c32_recovery_hash(&recovered);
    }
    if (broken == C32_BM_HOST_FALLBACK_ON_NO_MAP &&
        state->host_cache[0].state_id != 0) {
        recovered.atom[0] = state->host_cache[0];
        recovered.hash = c32_recovery_hash(&recovered);
    }
    for (invariant = FWLAB_P_UNIQUE;
         invariant <= FWLAB_P_NO_HOST_AUTHORITY; ++invariant) {
        struct c32_invariant_result result;

        if (!c32_check_invariant(
                (enum fwlab_persist_invariant_id)invariant, state, event,
                &physical, &recovered, &result)) {
            return 0;
        }
        if (!result.passed) {
            mask |= (uint16_t)(UINT16_C(1) << (invariant - 1u));
        }
        hash = oracle_result_hash(hash, &result);
        if (report != NULL) {
            report->invariant_coverage |=
                (uint16_t)(UINT16_C(1) << (invariant - 1u));
        }
    }
    if (violation_mask != NULL) {
        *violation_mask = mask;
    }
    if (oracle_hash != NULL) {
        *oracle_hash = hash;
    }
    if (physical_out != NULL) {
        *physical_out = physical;
    }
    if (recovered_out != NULL) {
        *recovered_out = recovered;
    }
    return 1;
}

static unsigned int cut_count_for_state(const struct c32_model_state *state)
{
    return state->scenario_family == C32_SCENARIO_LIMITS_HOST &&
                   state->scenario_variant == 3 ? 4u : 3u;
}

static int reconstruct_trace(
    uint32_t node_index,
    struct c32_counterexample *counterexample
)
{
    struct c32_model_action reverse[C32_MAX_TRACE_DEPTH];
    unsigned int count = 0;
    unsigned int index;

    while (bfs_nodes[node_index].parent != UINT32_MAX) {
        if (count >= C32_MAX_TRACE_DEPTH) {
            return 0;
        }
        reverse[count++] = bfs_nodes[node_index].action;
        node_index = bfs_nodes[node_index].parent;
    }
    counterexample->action_count = (uint8_t)count;
    for (index = 0; index < count; ++index) {
        counterexample->action[index] = reverse[count - index - 1u];
    }
    return 1;
}

static int run_bfs(
    const struct c32_model_state *initial,
    enum c32_broken_variant broken,
    enum fwlab_persist_invariant_id target,
    struct c32_model_report *report,
    struct c32_counterexample *counterexample
)
{
    uint32_t head = 0;
    uint32_t tail = 1;
    uint64_t initial_hash = c32_state_hash(initial);

    memset(visited_slots, 0, sizeof(visited_slots));
    memset(&bfs_nodes[0], 0, sizeof(bfs_nodes[0]));
    bfs_nodes[0].state = *initial;
    bfs_nodes[0].parent = UINT32_MAX;
    if (visited_insert(initial, 0, report) != 1) {
        return 0;
    }
    while (head < tail) {
        struct c32_bfs_node *node = &bfs_nodes[head];
        struct c32_action_list actions;
        unsigned int event;
        unsigned int event_count = cut_count_for_state(&node->state);
        unsigned int action_index;

        if (report != NULL) {
            if (report->base_states >= C32_MAX_BASE_STATES) {
                return 0;
            }
            ++report->base_states;
            if (node->depth > report->max_depth) {
                report->max_depth = node->depth;
            }
        }
        for (event = 0; event < event_count; ++event) {
            uint16_t violations;
            uint64_t oracle_hash;
            struct c32_physical_result physical;
            struct c32_recovery_result recovered;

            if (report != NULL &&
                report->recovery_checks >= C32_MAX_CUT_RECOVERY) {
                return 0;
            }
            if (!check_cut(&node->state, (enum c32_cut_kind)event, broken,
                           &violations, &oracle_hash, &physical, &recovered,
                           report)) {
                return 0;
            }
            if (report != NULL) {
                ++report->recovery_checks;
                report->aggregate_hash = hash_u64(
                    report->aggregate_hash, c32_state_hash(&node->state));
                report->aggregate_hash = hash_u64(
                    report->aggregate_hash,
                    c32_logical_image_hash(&physical.image));
                report->aggregate_hash = hash_u64(
                    report->aggregate_hash, recovered.hash);
                report->aggregate_hash = hash_u64(report->aggregate_hash,
                                                  oracle_hash);
                report->aggregate_hash = hash_u64(report->aggregate_hash,
                                                  event);
            }
            if (broken == C32_BROKEN_NONE && violations != 0) {
                return 0;
            }
            if (broken != C32_BROKEN_NONE &&
                (violations & (UINT16_C(1) << (target - 1u))) != 0) {
                unsigned int atom;

                if (counterexample == NULL) {
                    return 0;
                }
                memset(counterexample, 0, sizeof(*counterexample));
                counterexample->schema_version = 1;
                counterexample->invariant_id = (uint8_t)target;
                counterexample->broken_variant = (uint8_t)broken;
                counterexample->minimal_depth = node->depth;
                counterexample->scenario_family =
                    node->state.scenario_family;
                counterexample->profile_variant =
                    node->state.scenario_profile;
                counterexample->initial_variant =
                    node->state.scenario_variant;
                counterexample->request_kind =
                    node->state.scenario_request;
                counterexample->cut_kind = (uint8_t)event;
                counterexample->selected_checkpoint =
                    recovered.selected_checkpoint;
                counterexample->plp_drained_envelopes =
                    physical.drained_envelopes;
                counterexample->violation_mask = violations;
                counterexample->initial_hash = initial_hash;
                counterexample->precut_hash =
                    c32_state_hash(&node->state);
                counterexample->physical_hash =
                    c32_logical_image_hash(&physical.image);
                counterexample->recovered_hash = recovered.hash;
                counterexample->oracle_hash = oracle_hash;
                memcpy(counterexample->durable_floor,
                       node->state.durable_floor,
                       sizeof(counterexample->durable_floor));
                memcpy(counterexample->recovered, recovered.atom,
                       sizeof(counterexample->recovered));
                allowed_sets(&node->state, counterexample->allowed_set);
                if (!reconstruct_trace(head, counterexample)) {
                    return 0;
                }
                for (atom = 0; atom < counterexample->action_count; ++atom) {
                    counterexample->oracle_hash = hash_u64(
                        counterexample->oracle_hash,
                        counterexample->action[atom].kind);
                }
                return 2;
            }
        }
        if (!generate_actions(&node->state, broken, &actions)) {
            return 0;
        }
        if (actions.count == 0 && report != NULL) {
            ++report->terminal_states;
        }
        if (node->depth >= C32_MAX_TRACE_DEPTH && actions.count != 0) {
            return 0;
        }
        for (action_index = 0; action_index < actions.count; ++action_index) {
            struct c32_model_state next;
            int inserted;

            if (!apply_action(&node->state, &actions.item[action_index],
                              broken, &next)) {
                return 0;
            }
            if (report != NULL) {
                report->action_coverage |=
                    UINT32_C(1) << actions.item[action_index].kind;
            }
            if (tail >= C32_MAX_BASE_STATES) {
                return 0;
            }
            bfs_nodes[tail].state = next;
            bfs_nodes[tail].parent = head;
            bfs_nodes[tail].action = actions.item[action_index];
            bfs_nodes[tail].depth = (uint8_t)(node->depth + 1u);
            inserted = visited_insert(&next, tail, report);
            if (inserted < 0) {
                return 0;
            }
            if (inserted > 0) {
                ++tail;
            }
        }
        ++head;
    }
    return broken == C32_BROKEN_NONE ? 1 : 0;
}

static int run_positive_case(
    enum c32_scenario_family family,
    enum c32_profile_variant profile,
    unsigned int variant,
    enum fwlab_persist_request_kind request,
    struct c32_model_report *report
)
{
    struct c32_model_state initial;

    if (!scenario_init(&initial, family, profile, variant, request)) {
        ++report->rejected_configurations;
        return profile == C32_PROFILE_CLAIMED_UNVALIDATED;
    }
    ++report->scenario_runs;
    report->aggregate_hash = hash_u64(report->aggregate_hash, family);
    report->aggregate_hash = hash_u64(report->aggregate_hash, profile);
    report->aggregate_hash = hash_u64(report->aggregate_hash, variant);
    report->aggregate_hash = hash_u64(report->aggregate_hash, request);
    return run_bfs(&initial, C32_BROKEN_NONE, FWLAB_P_UNIQUE, report, NULL) ==
           1;
}

int c32_model_run_positive(struct c32_model_report *report)
{
    unsigned int profile;
    unsigned int request;
    unsigned int variant;

    if (report == NULL) {
        return 0;
    }
    memset(report, 0, sizeof(*report));
    report->aggregate_hash = UINT64_C(1469598103934665603);

    for (profile = 0; profile < C32_PROFILE_VARIANTS; ++profile) {
        for (request = FWLAB_PERSIST_DEFAULT;
             request <= FWLAB_PERSIST_SELF_DURABLE; ++request) {
            if (!run_positive_case(C32_SCENARIO_SINGLE_ATOM,
                                   (enum c32_profile_variant)profile, 0,
                                   (enum fwlab_persist_request_kind)request,
                                   report)) {
                return 0;
            }
        }
    }
    for (profile = C32_PROFILE_WC_OFF_PLP_CAP2;
         profile <= C32_PROFILE_WC_ON_PLP_CAP1; ++profile) {
        if (!run_positive_case(C32_SCENARIO_DUAL_ATOM,
                               (enum c32_profile_variant)profile, 0,
                               FWLAB_PERSIST_SELF_DURABLE, report)) {
            return 0;
        }
    }
    for (profile = C32_PROFILE_WC_OFF_NO_PLP;
         profile <= C32_PROFILE_WC_ON_NO_PLP; ++profile) {
        for (request = FWLAB_PERSIST_DEFAULT;
             request <= FWLAB_PERSIST_SELF_DURABLE; ++request) {
            if (!run_positive_case(C32_SCENARIO_TRIM,
                                   (enum c32_profile_variant)profile, 0,
                                   (enum fwlab_persist_request_kind)request,
                                   report)) {
                return 0;
            }
        }
    }
    for (variant = 0; variant < 3; ++variant) {
        if (!run_positive_case(C32_SCENARIO_CHECKPOINT,
                               C32_PROFILE_WC_OFF_NO_PLP, variant,
                               FWLAB_PERSIST_SELF_DURABLE, report)) {
            return 0;
        }
    }
    if (!run_positive_case(C32_SCENARIO_GC, C32_PROFILE_WC_OFF_NO_PLP, 0,
                           FWLAB_PERSIST_SELF_DURABLE, report) ||
        !run_positive_case(C32_SCENARIO_FENCE, C32_PROFILE_WC_ON_NO_PLP, 0,
                           FWLAB_PERSIST_DEFAULT, report) ||
        !run_positive_case(C32_SCENARIO_UNRELATED,
                           C32_PROFILE_WC_ON_PLP_CAP1, 0,
                           FWLAB_PERSIST_SELF_DURABLE, report)) {
        return 0;
    }
    for (profile = C32_PROFILE_WC_ON_PLP_CAP2;
         profile <= C32_PROFILE_WC_ON_PLP_CAP1; ++profile) {
        for (variant = 0; variant < 3; ++variant) {
            if (!run_positive_case(C32_SCENARIO_PLP,
                                   (enum c32_profile_variant)profile,
                                   variant, FWLAB_PERSIST_SELF_DURABLE,
                                   report)) {
                return 0;
            }
        }
    }
    for (variant = 0; variant < 3; ++variant) {
        if (!run_positive_case(C32_SCENARIO_EPOCH,
                               C32_PROFILE_WC_OFF_NO_PLP, variant,
                               FWLAB_PERSIST_DEFAULT, report)) {
            return 0;
        }
    }
    for (variant = 0; variant < 4; ++variant) {
        if (!run_positive_case(C32_SCENARIO_INVALID_MEDIA,
                               C32_PROFILE_WC_OFF_NO_PLP, variant,
                               FWLAB_PERSIST_DEFAULT, report)) {
            return 0;
        }
    }
    for (variant = 0; variant < 4; ++variant) {
        enum c32_profile_variant selected =
            variant == 1 ? C32_PROFILE_CLAIMED_UNVALIDATED :
                           C32_PROFILE_WC_OFF_NO_PLP;

        if (!run_positive_case(C32_SCENARIO_LIMITS_HOST, selected, variant,
                               FWLAB_PERSIST_DEFAULT, report)) {
            return 0;
        }
    }
    return report->scenario_runs != 0 &&
           report->rejected_configurations == 3 &&
           report->invariant_coverage == UINT16_C(0x1fff) &&
           report->action_coverage ==
               ((UINT32_C(1) << C32_ACTION_COUNT) - 1u);
}

struct negative_case {
    uint8_t family;
    uint8_t profile;
    uint8_t variant;
    uint8_t request;
};

static const struct negative_case negative_cases[C32_INVARIANT_COUNT] = {
    {C32_SCENARIO_SINGLE_ATOM, C32_PROFILE_WC_ON_NO_PLP, 0,
     FWLAB_PERSIST_DEFAULT},
    {C32_SCENARIO_INVALID_MEDIA, C32_PROFILE_WC_OFF_NO_PLP, 0,
     FWLAB_PERSIST_DEFAULT},
    {C32_SCENARIO_INVALID_MEDIA, C32_PROFILE_WC_OFF_NO_PLP, 1,
     FWLAB_PERSIST_DEFAULT},
    {C32_SCENARIO_CHECKPOINT, C32_PROFILE_WC_OFF_NO_PLP, 0,
     FWLAB_PERSIST_SELF_DURABLE},
    {C32_SCENARIO_SINGLE_ATOM, C32_PROFILE_WC_OFF_NO_PLP, 0,
     FWLAB_PERSIST_SELF_DURABLE},
    {C32_SCENARIO_TRIM, C32_PROFILE_WC_OFF_NO_PLP, 0,
     FWLAB_PERSIST_SELF_DURABLE},
    {C32_SCENARIO_GC, C32_PROFILE_WC_OFF_NO_PLP, 0,
     FWLAB_PERSIST_SELF_DURABLE},
    {C32_SCENARIO_CHECKPOINT, C32_PROFILE_WC_OFF_NO_PLP, 1,
     FWLAB_PERSIST_SELF_DURABLE},
    {C32_SCENARIO_EPOCH, C32_PROFILE_WC_OFF_NO_PLP, 0,
     FWLAB_PERSIST_DEFAULT},
    {C32_SCENARIO_FENCE, C32_PROFILE_WC_ON_NO_PLP, 0,
     FWLAB_PERSIST_DEFAULT},
    {C32_SCENARIO_PLP, C32_PROFILE_WC_ON_PLP_CAP1, 0,
     FWLAB_PERSIST_SELF_DURABLE},
    {C32_SCENARIO_SINGLE_ATOM, C32_PROFILE_WC_OFF_NO_PLP, 0,
     FWLAB_PERSIST_SELF_DURABLE},
    {C32_SCENARIO_LIMITS_HOST, C32_PROFILE_WC_OFF_NO_PLP, 3,
     FWLAB_PERSIST_DEFAULT}
};

int c32_model_find_counterexample(
    enum c32_broken_variant broken,
    struct c32_counterexample *counterexample
)
{
    const struct negative_case *selected;
    struct c32_model_state initial;
    enum fwlab_persist_invariant_id invariant;
    int result;

    if (counterexample == NULL || broken < C32_BM_UNIQUE_KEEP_PREDECESSOR ||
        broken > C32_BM_HOST_FALLBACK_ON_NO_MAP) {
        return 0;
    }
    selected = &negative_cases[(unsigned int)broken - 1u];
    memset(counterexample, 0, sizeof(*counterexample));
    if (!scenario_init(
            &initial, (enum c32_scenario_family)selected->family,
            (enum c32_profile_variant)selected->profile, selected->variant,
            (enum fwlab_persist_request_kind)selected->request)) {
        return 0;
    }
    invariant = (enum fwlab_persist_invariant_id)broken;
    result = run_bfs(&initial, broken, invariant, NULL, counterexample);
    return result == 2 && counterexample->invariant_id == invariant &&
           counterexample->broken_variant == broken;
}
