/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fwlab/portable/persistence_policy.h"

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

static struct fwlab_persist_profile profile_make(
    uint8_t cache_enabled,
    uint8_t plp_kind,
    uint8_t capacity
)
{
    struct fwlab_persist_profile profile;

    memset(&profile, 0, sizeof(profile));
    profile.version = FWLAB_PERSIST_VERSION;
    profile.size = (uint16_t)sizeof(profile);
    profile.cache_enabled = cache_enabled;
    profile.plp_kind = plp_kind;
    profile.plp_capacity_credits = capacity;
    if (plp_kind == FWLAB_PERSIST_PLP_VALIDATED) {
        profile.survival_event_mask =
            FWLAB_PERSIST_EVENT_CONTROLLER_RESET |
            FWLAB_PERSIST_EVENT_POWER_LOSS |
            FWLAB_PERSIST_EVENT_DAEMON_CRASH;
    }
    return profile;
}

static struct fwlab_persist_request request_make(
    uint8_t kind,
    uint8_t atom_mask,
    uint64_t identifier
)
{
    struct fwlab_persist_request request;

    memset(&request, 0, sizeof(request));
    request.version = FWLAB_PERSIST_VERSION;
    request.size = (uint16_t)sizeof(request);
    request.kind = kind;
    request.atom_mask = atom_mask;
    request.token.word[0] = identifier;
    request.token.word[1] = ~identifier;
    request.owner_epoch = 1;
    request.scope = 7;
    request.sequence = (uint32_t)identifier;
    if (kind == FWLAB_PERSIST_FENCE) {
        request.frontier = (uint32_t)identifier;
    }
    return request;
}

static struct fwlab_persist_atom_fact fact_make(
    const struct fwlab_persist_request *request,
    uint8_t atom,
    uint32_t mask,
    uint8_t closure
)
{
    struct fwlab_persist_atom_fact fact;

    memset(&fact, 0, sizeof(fact));
    fact.version = FWLAB_PERSIST_VERSION;
    fact.size = (uint16_t)sizeof(fact);
    fact.token = request->token;
    fact.owner_epoch = request->owner_epoch;
    fact.scope = request->scope;
    fact.sequence = request->sequence;
    fact.atom = atom;
    fact.logical_version = 1;
    fact.predecessor_version = 0;
    fact.mutation_kind = FWLAB_PERSIST_WRITE;
    fact.fact_mask = mask;
    fact.closure = closure;
    return fact;
}

static struct fwlab_persist_plp_envelope envelope_make(
    const struct fwlab_persist_request *request,
    uint8_t atom_mask
)
{
    struct fwlab_persist_plp_envelope envelope;

    memset(&envelope, 0, sizeof(envelope));
    envelope.version = FWLAB_PERSIST_VERSION;
    envelope.size = (uint16_t)sizeof(envelope);
    envelope.token = request->token;
    envelope.owner_epoch = request->owner_epoch;
    envelope.sequence = request->sequence;
    envelope.atom_mask = atom_mask;
    envelope.capacity_cost = atom_mask == UINT8_C(0x03) ? 2 : 1;
    envelope.flags = FWLAB_PLP_REQUIRED_FLAGS;
    envelope.survival_event_mask = FWLAB_PERSIST_EVENT_MASK;
    envelope.persistent_order = request->sequence;
    return envelope;
}

static int test_profiles(void)
{
    struct fwlab_persist_profile profile =
        profile_make(1, FWLAB_PERSIST_PLP_NONE, 0);

    CHECK(fwlab_persist_profile_validate(&profile) == FWLAB_PERSIST_OK);
    profile = profile_make(1, FWLAB_PERSIST_PLP_VALIDATED, 2);
    CHECK(fwlab_persist_profile_validate(&profile) == FWLAB_PERSIST_OK);
    profile = profile_make(1, FWLAB_PERSIST_PLP_CLAIMED_UNVALIDATED, 2);
    CHECK(fwlab_persist_profile_validate(&profile) ==
          FWLAB_PERSIST_INVALID_CONTRACT);
    profile = profile_make(1, FWLAB_PERSIST_PLP_VALIDATED, 1);
    profile.survival_event_mask = FWLAB_PERSIST_EVENT_CONTROLLER_RESET;
    CHECK(fwlab_persist_profile_validate(&profile) ==
          FWLAB_PERSIST_INVALID_CONTRACT);
    return 0;
}

static int test_no_plp_witnesses(void)
{
    struct fwlab_persist_profile profile =
        profile_make(1, FWLAB_PERSIST_PLP_NONE, 0);
    struct fwlab_persist_request request =
        request_make(FWLAB_PERSIST_DEFAULT, 1, 1);
    struct fwlab_persist_atom_fact fact = fact_make(
        &request, 0, FWLAB_PERSIST_FACT_CAPTURED,
        FWLAB_PERSIST_CLOSE_OPEN);
    struct fwlab_persist_witness witness;

    CHECK(fwlab_persist_command_witness(&profile, &request, &fact, 1,
                                        NULL, 0, &witness) ==
          FWLAB_PERSIST_OK);
    CHECK(witness.witness_class == FWLAB_PERSIST_VOLATILE_ELIGIBLE);
    request.kind = FWLAB_PERSIST_SELF_DURABLE;
    CHECK(fwlab_persist_command_witness(&profile, &request, &fact, 1,
                                        NULL, 0, &witness) ==
          FWLAB_PERSIST_OK);
    CHECK(witness.witness_class == FWLAB_PERSIST_WITNESS_NONE);
    fact.fact_mask |= FWLAB_PERSIST_FACT_C_MAP |
                      FWLAB_PERSIST_FACT_LOGICAL_DURABLE;
    fact.closure = FWLAB_PERSIST_CLOSE_C_MAP;
    CHECK(fwlab_persist_command_witness(&profile, &request, &fact, 1,
                                        NULL, 0, &witness) ==
          FWLAB_PERSIST_OK);
    CHECK(witness.witness_class == FWLAB_PERSIST_DURABLE_ELIGIBLE);
    profile.cache_enabled = 0;
    request.kind = FWLAB_PERSIST_DEFAULT;
    fact.fact_mask = FWLAB_PERSIST_FACT_CAPTURED;
    fact.closure = FWLAB_PERSIST_CLOSE_OPEN;
    CHECK(fwlab_persist_command_witness(&profile, &request, &fact, 1,
                                        NULL, 0, &witness) ==
          FWLAB_PERSIST_OK);
    CHECK(witness.witness_class == FWLAB_PERSIST_WITNESS_NONE);
    return 0;
}

static int test_plp_atomic_credits(void)
{
    struct fwlab_persist_profile profile =
        profile_make(1, FWLAB_PERSIST_PLP_VALIDATED, 2);
    struct fwlab_persist_request request =
        request_make(FWLAB_PERSIST_DEFAULT, UINT8_C(0x03), 2);
    struct fwlab_persist_atom_fact facts[2];
    struct fwlab_persist_plp_envelope envelope =
        envelope_make(&request, UINT8_C(0x03));
    struct fwlab_persist_witness witness;

    facts[0] = fact_make(&request, 0,
                         FWLAB_PERSIST_FACT_CAPTURED |
                         FWLAB_PERSIST_FACT_PLP_ADMITTED,
                         FWLAB_PERSIST_CLOSE_PLP);
    facts[1] = fact_make(&request, 1,
                         FWLAB_PERSIST_FACT_CAPTURED |
                         FWLAB_PERSIST_FACT_PLP_ADMITTED,
                         FWLAB_PERSIST_CLOSE_PLP);
    CHECK(fwlab_persist_command_witness(&profile, &request, facts, 2,
                                        &envelope, 1, &witness) ==
          FWLAB_PERSIST_OK);
    CHECK(witness.witness_class == FWLAB_PERSIST_DURABLE_ELIGIBLE);
    profile.plp_capacity_credits = 1;
    CHECK(fwlab_persist_command_witness(&profile, &request, facts, 2,
                                        &envelope, 1, &witness) ==
          FWLAB_PERSIST_OK);
    CHECK(witness.witness_class == FWLAB_PERSIST_WITNESS_NONE);
    profile.plp_capacity_credits = 2;
    envelope.flags &= (uint8_t)~FWLAB_PLP_COMMIT_MARKER;
    CHECK(fwlab_persist_command_witness(&profile, &request, facts, 2,
                                        &envelope, 1, &witness) ==
          FWLAB_PERSIST_OK);
    CHECK(witness.witness_class == FWLAB_PERSIST_WITNESS_NONE);
    return 0;
}

static struct fwlab_persist_obligation obligation_make(
    uint32_t scope,
    uint32_t sequence,
    uint8_t closure,
    uint32_t fact_mask
)
{
    struct fwlab_persist_obligation obligation;

    memset(&obligation, 0, sizeof(obligation));
    obligation.version = FWLAB_PERSIST_VERSION;
    obligation.size = (uint16_t)sizeof(obligation);
    obligation.token.word[0] = sequence;
    obligation.token.word[1] = ~((uint64_t)sequence);
    obligation.owner_epoch = 1;
    obligation.scope = scope;
    obligation.sequence = sequence;
    obligation.atom_mask = 1;
    obligation.closure = closure;
    obligation.fact_mask = fact_mask;
    return obligation;
}

static int test_fixed_frontier_fence(void)
{
    struct fwlab_persist_profile profile =
        profile_make(1, FWLAB_PERSIST_PLP_NONE, 0);
    struct fwlab_persist_request fence =
        request_make(FWLAB_PERSIST_FENCE, 0, 5);
    struct fwlab_persist_obligation obligations[4];
    struct fwlab_persist_witness witness;

    fence.scope = 9;
    fence.frontier = 5;
    obligations[0] = obligation_make(
        9, 4, FWLAB_PERSIST_CLOSE_C_MAP,
        FWLAB_PERSIST_FACT_C_MAP | FWLAB_PERSIST_FACT_LOGICAL_DURABLE);
    obligations[1] = obligation_make(
        9, 5, FWLAB_PERSIST_CLOSE_NO_COMMIT,
        FWLAB_PERSIST_FACT_PROVABLE_NO_COMMIT);
    obligations[2] = obligation_make(
        9, 6, FWLAB_PERSIST_CLOSE_OPEN, FWLAB_PERSIST_FACT_CAPTURED);
    obligations[3] = obligation_make(
        10, 1, FWLAB_PERSIST_CLOSE_OPEN, FWLAB_PERSIST_FACT_CAPTURED);
    CHECK(fwlab_persist_fence_witness(&profile, &fence, obligations, 4,
                                      NULL, 0, &witness) ==
          FWLAB_PERSIST_OK);
    CHECK(witness.witness_class == FWLAB_PERSIST_DURABLE_ELIGIBLE);
    obligations[1].closure = FWLAB_PERSIST_CLOSE_INDETERMINATE;
    obligations[1].fact_mask = FWLAB_PERSIST_FACT_INDETERMINATE;
    CHECK(fwlab_persist_fence_witness(&profile, &fence, obligations, 4,
                                      NULL, 0, &witness) ==
          FWLAB_PERSIST_OK);
    CHECK(witness.witness_class == FWLAB_PERSIST_FAILED_INDETERMINATE);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        {"profiles", test_profiles},
        {"no_plp_witnesses", test_no_plp_witnesses},
        {"plp_atomic_credits", test_plp_atomic_credits},
        {"fixed_frontier_fence", test_fixed_frontier_fence},
    };
    unsigned int index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        int line = tests[index].run();

        if (line != 0) {
            fprintf(stderr, "C3.2 policy test %s failed at line %d\n",
                    tests[index].name, line);
            return 1;
        }
    }
    printf("C3.2 policy unit: PASS (%u cases)\n",
           (unsigned int)(sizeof(tests) / sizeof(tests[0])));
    return 0;
}
