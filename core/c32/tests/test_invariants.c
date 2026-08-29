/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../c32_internal.h"

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

struct invariant_fixture {
    struct c32_model_state cut;
    struct c32_physical_result physical;
    struct c32_recovery_result recovered;
};

static struct c32_logical_state floor_state(unsigned int atom)
{
    struct c32_logical_state state;

    memset(&state, 0, sizeof(state));
    state.kind = C32_LOGICAL_VALUE;
    state.atom = (uint8_t)atom;
    state.value_token = (uint8_t)(10u + atom);
    state.state_id = (uint16_t)(1u + atom);
    return state;
}

static void fixture_init(struct invariant_fixture *fixture)
{
    unsigned int atom;

    memset(fixture, 0, sizeof(*fixture));
    fixture->cut.current_epoch = 1;
    fixture->cut.phase = C32_MODEL_READY;
    fixture->cut.profile.version = FWLAB_PERSIST_VERSION;
    fixture->cut.profile.size = (uint16_t)sizeof(fixture->cut.profile);
    fixture->cut.profile.cache_enabled = 1;
    fixture->cut.profile.plp_kind = FWLAB_PERSIST_PLP_NONE;
    fixture->cut.free_bitmap = UINT16_C(0x01ff);
    fixture->physical.status = C32_RECOVERY_OK;
    fixture->recovered.status = C32_RECOVERY_OK;
    fixture->recovered.selected_checkpoint = UINT8_MAX;
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        fixture->cut.genesis[atom] = floor_state(atom);
        fixture->cut.durable_floor[atom] = floor_state(atom);
        fixture->physical.image.genesis[atom] = floor_state(atom);
        fixture->recovered.atom[atom] = floor_state(atom);
    }
    fixture->recovered.hash = c32_recovery_hash(&fixture->recovered);
}

static int expect_fail(
    struct invariant_fixture *fixture,
    enum fwlab_persist_invariant_id invariant
)
{
    struct c32_invariant_result result;

    CHECK(c32_check_invariant(invariant, &fixture->cut,
                              C32_CUT_POWER_LOSS, &fixture->physical,
                              &fixture->recovered, &result));
    CHECK(result.invariant_id == invariant);
    CHECK(result.passed == 0);
    CHECK(result.reason != 0);
    return 0;
}

static struct c32_persistent_record record_make(
    uint8_t kind,
    uint16_t id
)
{
    struct c32_persistent_record record;

    memset(&record, 0, sizeof(record));
    record.presence = C32_RECORD_VALID;
    record.kind = kind;
    record.atom = 0;
    record.logical_version = 1;
    record.value_token = 20;
    record.predecessor_state_id = 1;
    record.body_complete = 1;
    record.checksum_ok = 1;
    record.c_applied = 1;
    record.record_id = id;
    record.c_sequence = 2;
    record.self = (struct c32_phys_ref){0, 0, 0, 1};
    return record;
}

static int test_all_positive(void)
{
    struct invariant_fixture fixture;
    unsigned int invariant;

    fixture_init(&fixture);
    for (invariant = FWLAB_P_UNIQUE;
         invariant <= FWLAB_P_NO_HOST_AUTHORITY; ++invariant) {
        struct c32_invariant_result result;

        CHECK(c32_check_invariant(
            (enum fwlab_persist_invariant_id)invariant, &fixture.cut,
            C32_CUT_POWER_LOSS, &fixture.physical, &fixture.recovered,
            &result));
        CHECK(result.passed == 1);
        CHECK(result.invariant_id == invariant);
    }
    return 0;
}

static int test_unique(void)
{
    struct invariant_fixture fixture;

    fixture_init(&fixture);
    fixture.recovered.status = C32_RECOVERY_AMBIGUOUS;
    return expect_fail(&fixture, FWLAB_P_UNIQUE);
}

static int test_no_torn(void)
{
    struct invariant_fixture fixture;

    fixture_init(&fixture);
    fixture.physical.image.media[0][0] = record_make(C32_REC_MAP, 10);
    fixture.physical.image.media[0][0].checksum_ok = 0;
    fixture.recovered.atom[0].authority_record_id = 10;
    return expect_fail(&fixture, FWLAB_P_NO_TORN);
}

static int test_depend(void)
{
    struct invariant_fixture fixture;

    fixture_init(&fixture);
    fixture.physical.image.media[0][0] = record_make(C32_REC_MAP, 11);
    fixture.physical.image.media[0][0].data_ref =
        (struct c32_phys_ref){1, 0, 0, 1};
    fixture.recovered.atom[0].authority_record_id = 11;
    return expect_fail(&fixture, FWLAB_P_DEPEND);
}

static int test_durable_floor(void)
{
    struct invariant_fixture fixture;

    fixture_init(&fixture);
    fixture.cut.durable_floor[0].version = 1;
    fixture.cut.durable_floor[0].value_token = 20;
    return expect_fail(&fixture, FWLAB_P_DURABLE_FLOOR);
}

static int test_volatile_bound(void)
{
    struct invariant_fixture fixture;

    fixture_init(&fixture);
    fixture.recovered.atom[0].version = 2;
    fixture.recovered.atom[0].value_token = 30;
    return expect_fail(&fixture, FWLAB_P_VOLATILE_BOUND);
}

static int test_trim(void)
{
    struct invariant_fixture fixture;

    fixture_init(&fixture);
    fixture.cut.mutation[0].used = 1;
    fixture.cut.mutation[0].atom_mask = 1;
    fixture.cut.mutation[0].publication = C32_PUBLISH_DURABLE;
    fixture.cut.mutation[0].target_kind[0] = C32_LOGICAL_TOMBSTONE;
    fixture.cut.mutation[0].target_version[0] = 1;
    fixture.recovered.atom[0].version = 1;
    return expect_fail(&fixture, FWLAB_P_TRIM);
}

static int test_gc(void)
{
    struct invariant_fixture fixture;

    fixture_init(&fixture);
    fixture.cut.gc.used = 1;
    fixture.cut.gc.atom = 0;
    fixture.cut.gc.version = 1;
    fixture.cut.gc.stage = 4;
    fixture.cut.gc.lease_held = 1;
    return expect_fail(&fixture, FWLAB_P_GC);
}

static int test_checkpoint(void)
{
    struct invariant_fixture fixture;

    fixture_init(&fixture);
    fixture.recovered.selected_checkpoint = 0;
    return expect_fail(&fixture, FWLAB_P_CHECKPOINT);
}

static int test_epoch(void)
{
    struct invariant_fixture fixture;

    fixture_init(&fixture);
    fixture.cut.inflight[0].phase = C32_OP_B;
    fixture.cut.inflight[0].op_id = 1;
    fixture.cut.inflight[0].owner_epoch = 0;
    fixture.cut.inflight[0].outcome_delivered = 1;
    return expect_fail(&fixture, FWLAB_P_EPOCH);
}

static int test_fence(void)
{
    struct invariant_fixture fixture;

    fixture_init(&fixture);
    fixture.cut.fence.used = 1;
    fixture.cut.fence.success = 1;
    fixture.cut.fence.owner_epoch = 1;
    fixture.cut.fence.scope = 4;
    fixture.cut.fence.frontier = 1;
    fixture.cut.mutation[0].used = 1;
    fixture.cut.mutation[0].mutation_id = 1;
    fixture.cut.mutation[0].owner_epoch = 1;
    fixture.cut.mutation[0].scope = 4;
    fixture.cut.mutation[0].accept_sequence = 1;
    fixture.cut.mutation[0].atom_mask = 1;
    fixture.cut.mutation[0].closure[0] = FWLAB_PERSIST_CLOSE_OPEN;
    return expect_fail(&fixture, FWLAB_P_FENCE);
}

static int test_plp(void)
{
    struct invariant_fixture fixture;

    fixture_init(&fixture);
    fixture.cut.profile.plp_kind = FWLAB_PERSIST_PLP_VALIDATED;
    fixture.cut.profile.plp_capacity_credits = 1;
    fixture.cut.mutation[0].used = 1;
    fixture.cut.mutation[0].mutation_id = 1;
    fixture.cut.mutation[0].publication = C32_PUBLISH_DURABLE;
    fixture.cut.mutation[0].closure[0] = FWLAB_PERSIST_CLOSE_PLP;
    return expect_fail(&fixture, FWLAB_P_PLP);
}

static int test_conserve(void)
{
    struct invariant_fixture fixture;

    fixture_init(&fixture);
    fixture.cut.free_bitmap = 0;
    return expect_fail(&fixture, FWLAB_P_CONSERVE);
}

static int test_no_host(void)
{
    struct invariant_fixture fixture;

    fixture_init(&fixture);
    fixture.recovered.atom[0].version = 1;
    fixture.recovered.atom[0].value_token = 99;
    fixture.recovered.atom[0].state_id = 999;
    fixture.cut.host_cache[0] = fixture.recovered.atom[0];
    fixture.cut.host_adversarial_mask = 1;
    return expect_fail(&fixture, FWLAB_P_NO_HOST_AUTHORITY);
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        {"all_positive", test_all_positive},
        {"P-UNIQUE", test_unique},
        {"P-NO-TORN", test_no_torn},
        {"P-DEPEND", test_depend},
        {"P-DURABLE-FLOOR", test_durable_floor},
        {"P-VOLATILE-BOUND", test_volatile_bound},
        {"P-TRIM", test_trim},
        {"P-GC", test_gc},
        {"P-CHECKPOINT", test_checkpoint},
        {"P-EPOCH", test_epoch},
        {"P-FENCE", test_fence},
        {"P-PLP", test_plp},
        {"P-CONSERVE", test_conserve},
        {"P-NO-HOST-AUTHORITY", test_no_host},
    };
    unsigned int index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        int line = tests[index].run();

        if (line != 0) {
            fprintf(stderr, "C3.2 invariant test %s failed at line %d\n",
                    tests[index].name, line);
            return 1;
        }
    }
    printf("C3.2 invariant unit: PASS (%u cases)\n",
           (unsigned int)(sizeof(tests) / sizeof(tests[0])));
    return 0;
}
