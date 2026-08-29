/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../c32_internal.h"

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

static struct c32_logical_state genesis(unsigned int atom)
{
    struct c32_logical_state state;

    memset(&state, 0, sizeof(state));
    state.kind = C32_LOGICAL_VALUE;
    state.atom = (uint8_t)atom;
    state.version = 0;
    state.value_token = (uint8_t)(10u + atom);
    state.state_id = (uint16_t)(1u + atom);
    return state;
}

static struct c32_persistent_record data_record(
    uint8_t group,
    uint8_t slot,
    uint8_t atom,
    uint8_t version,
    uint8_t value,
    uint16_t id,
    uint16_t c_sequence
)
{
    struct c32_persistent_record record;

    memset(&record, 0, sizeof(record));
    record.presence = C32_RECORD_VALID;
    record.kind = C32_REC_DATA;
    record.atom = atom;
    record.logical_version = version;
    record.value_token = value;
    record.body_complete = 1;
    record.checksum_ok = 1;
    record.c_applied = 1;
    record.record_id = id;
    record.c_sequence = c_sequence;
    record.self.group = group;
    record.self.slot = slot;
    record.self.erase_generation = 0;
    record.self.valid = 1;
    return record;
}

static struct c32_persistent_record map_record(
    uint8_t group,
    uint8_t slot,
    uint8_t atom,
    uint8_t version,
    uint8_t value,
    uint16_t predecessor,
    uint16_t id,
    uint16_t c_sequence,
    struct c32_phys_ref data_ref
)
{
    struct c32_persistent_record record =
        data_record(group, slot, atom, version, value, id, c_sequence);

    record.kind = C32_REC_MAP;
    record.predecessor_state_id = predecessor;
    record.predecessor_version = (uint8_t)(version - 1u);
    record.data_ref = data_ref;
    return record;
}

static void image_init(struct c32_logical_image *image)
{
    memset(image, 0, sizeof(*image));
    image->genesis[0] = genesis(0);
    image->genesis[1] = genesis(1);
}

static int test_map_dependency_and_torn(void)
{
    struct c32_logical_image image;
    struct c32_recovery_result result;

    image_init(&image);
    image.media[0][0] = data_record(0, 0, 0, 1, 20, 10, 1);
    image.media[0][1] = map_record(0, 1, 0, 1, 20,
                                   image.genesis[0].state_id, 11, 2,
                                   image.media[0][0].self);
    CHECK(c32_logical_recover(&image, C32_BROKEN_NONE, &result));
    CHECK(result.status == C32_RECOVERY_OK);
    CHECK(result.atom[0].version == 1 && result.atom[0].value_token == 20);

    image.media[0][0].c_applied = 0;
    CHECK(c32_logical_recover(&image, C32_BROKEN_NONE, &result));
    CHECK(result.atom[0].version == 0);
    CHECK(c32_logical_recover(&image,
                              C32_BM_MAP_OMIT_DATA_C_GUARD, &result));
    CHECK(result.atom[0].version == 1);

    image.media[0][0].c_applied = 1;
    image.media[0][1].checksum_ok = 0;
    CHECK(c32_logical_recover(&image, C32_BROKEN_NONE, &result));
    CHECK(result.atom[0].version == 0);
    CHECK(c32_logical_recover(&image,
                              C32_BM_TORN_SKIP_CHECKSUM, &result));
    CHECK(result.atom[0].version == 1);
    return 0;
}

static int test_checkpoint_selection_and_conflict(void)
{
    struct c32_logical_image image;
    struct c32_recovery_result result;

    image_init(&image);
    image.checkpoint[0].image_state = C32_IMAGE_VALID;
    image.checkpoint[0].generation = 1;
    image.checkpoint[0].body_complete = 1;
    image.checkpoint[0].checksum_ok = 1;
    image.checkpoint[0].c_applied = 1;
    image.checkpoint[0].provenance_ok = 1;
    image.checkpoint[0].watermark = 2;
    image.checkpoint[0].payload_hash = UINT64_C(0x1111);
    image.checkpoint[0].entry[0] = genesis(0);
    image.checkpoint[0].entry[1] = genesis(1);
    image.checkpoint[0].entry[0].version = 1;
    image.anchor[0].image_state = C32_IMAGE_VALID;
    image.anchor[0].generation = 1;
    image.anchor[0].target_slot = 0;
    image.anchor[0].body_complete = 1;
    image.anchor[0].checksum_ok = 1;
    image.anchor[0].c_applied = 1;
    image.anchor[0].watermark = 2;
    image.anchor[0].checkpoint_hash = UINT64_C(0x1111);

    image.checkpoint[1] = image.checkpoint[0];
    image.checkpoint[1].generation = 2;
    image.checkpoint[1].body_complete = 0;
    image.checkpoint[1].payload_hash = UINT64_C(0x2222);
    image.anchor[1] = image.anchor[0];
    image.anchor[1].generation = 2;
    image.anchor[1].target_slot = 1;
    image.anchor[1].checkpoint_hash = UINT64_C(0x2222);
    CHECK(c32_logical_recover(&image, C32_BROKEN_NONE, &result));
    CHECK(result.selected_checkpoint == 0);
    CHECK(c32_logical_recover(&image,
                              C32_BM_ANCHOR_BEFORE_CKPT_COMPLETE, &result));
    CHECK(result.selected_checkpoint == 1);

    image.checkpoint[1].body_complete = 1;
    image.checkpoint[1].generation = 1;
    image.anchor[1].generation = 1;
    CHECK(c32_logical_recover(&image, C32_BROKEN_NONE, &result));
    CHECK(result.status == C32_RECOVERY_AMBIGUOUS);
    return 0;
}

static int test_physical_settle_and_plp(void)
{
    struct c32_model_state cut;
    struct c32_physical_result physical;
    struct c32_recovery_result recovered;

    memset(&cut, 0, sizeof(cut));
    cut.current_epoch = 1;
    cut.profile.version = FWLAB_PERSIST_VERSION;
    cut.profile.size = (uint16_t)sizeof(cut.profile);
    cut.profile.cache_enabled = 1;
    cut.profile.plp_kind = FWLAB_PERSIST_PLP_VALIDATED;
    cut.profile.plp_capacity_credits = 1;
    cut.profile.survival_event_mask =
        FWLAB_PERSIST_EVENT_CONTROLLER_RESET |
        FWLAB_PERSIST_EVENT_POWER_LOSS |
        FWLAB_PERSIST_EVENT_DAEMON_CRASH;
    cut.genesis[0] = genesis(0);
    cut.genesis[1] = genesis(1);
    cut.plp[0].state = C32_PLP_ADMITTED;
    cut.plp[0].atom_mask = 1;
    cut.plp[0].capacity_cost = 1;
    cut.plp[0].flags = FWLAB_PLP_REQUIRED_FLAGS;
    cut.plp[0].survival_event_mask = cut.profile.survival_event_mask;
    cut.plp[0].drain_budget_reserved = 1;
    cut.plp[0].envelope_id = 1;
    cut.plp[0].mutation_id = 1;
    cut.plp[0].owner_epoch = 1;
    cut.plp[0].persistent_order = 1;
    cut.plp[0].atom[0].atom = 0;
    cut.plp[0].atom[0].version = 1;
    cut.plp[0].atom[0].target_kind = C32_LOGICAL_VALUE;
    cut.plp[0].atom[0].predecessor_version = 0;
    cut.plp[0].atom[0].predecessor_state_id = cut.genesis[0].state_id;
    cut.plp[0].atom[0].value_token = 30;
    cut.plp[0].atom[0].drain_data_ref =
        (struct c32_phys_ref){0, 0, 0, 1};
    cut.plp[0].atom[0].drain_metadata_ref =
        (struct c32_phys_ref){0, 1, 0, 1};
    CHECK(c32_physical_settle(&cut, C32_CUT_POWER_LOSS,
                              C32_BROKEN_NONE, &physical));
    CHECK(physical.status == C32_RECOVERY_OK);
    CHECK(physical.drained_envelopes == 1);
    CHECK(c32_logical_recover(&physical.image, C32_BROKEN_NONE, &recovered));
    CHECK(recovered.atom[0].version == 1);
    CHECK(c32_physical_settle(&cut, C32_CUT_POWER_LOSS,
                              C32_BM_READY_BEFORE_PLP_DRAIN, &physical));
    CHECK(c32_logical_recover(&physical.image, C32_BROKEN_NONE, &recovered));
    CHECK(recovered.atom[0].version == 0);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        {"map_dependency_and_torn", test_map_dependency_and_torn},
        {"checkpoint_selection_and_conflict",
         test_checkpoint_selection_and_conflict},
        {"physical_settle_and_plp", test_physical_settle_and_plp},
    };
    unsigned int index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        int line = tests[index].run();

        if (line != 0) {
            fprintf(stderr, "C3.2 recovery test %s failed at line %d\n",
                    tests[index].name, line);
            return 1;
        }
    }
    printf("C3.2 recovery unit: PASS (%u cases)\n",
           (unsigned int)(sizeof(tests) / sizeof(tests[0])));
    return 0;
}
