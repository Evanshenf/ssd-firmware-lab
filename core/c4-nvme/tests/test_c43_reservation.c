/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_fake_services.h"

#include "../c43_internal.h"

#include "fwlab/portable/nvme_codec.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "C4.3 reservation check failed at line %d\n",      \
                    __LINE__);                                                  \
            return 1;                                                           \
        }                                                                       \
    } while (0)

struct fixture {
    _Alignas(max_align_t) uint8_t arena[8192];
    struct c43_fake_services services;
    struct fwlab_c43_graph_providers providers;
    struct fwlab_c43_graph_config config;
    struct fwlab_c43_graph *graph;
    size_t arena_size;
};

static int bytes_zero(const void *value, size_t size)
{
    const unsigned char *bytes = value;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static struct fwlab_c43_graph_config config_fixed(void)
{
    struct fwlab_c43_graph_config config = {0};
    const struct fwlab_c43_counter_seed seed = {1, 1000};

    config.version = FWLAB_C43_GRAPH_VERSION;
    config.size = sizeof(config);
    fwlab_nvme_profile_fixed(&config.profile);
    config.command_capacity = FWLAB_C43_MAX_COMMANDS;
    config.actions_per_command = FWLAB_C43_ACTIONS_PER_COMMAND;
    config.queue_mailbox_capacity = 8;
    config.target_mailbox_capacity = 4;
    config.block_mailbox_capacity = 20;
    config.service_gap_maximum = FWLAB_C43_SERVICE_GAP_MAXIMUM;
    config.ordinary_progress_maximum = FWLAB_C43_PROGRESS_MAXIMUM;
    config.control_progress_maximum = FWLAB_C43_CONTROL_PROGRESS_MAXIMUM;
    config.safety_generation = 1;
    config.instance_nonce = UINT64_C(0xc430000000000001);
    config.controller_epoch = 1;
    config.command_uid = seed;
    config.action_uid = seed;
    config.transaction_uid = seed;
    config.lease_uid = seed;
    config.consume_uid = seed;
    config.finalizer_uid = seed;
    return config;
}

static struct fwlab_hif_prepare_key prepare_key(uint64_t uid)
{
    struct fwlab_hif_prepare_key key = {0};

    key.version = FWLAB_HIF_COMMAND_PORT_VERSION;
    key.size = sizeof(key);
    key.origin.word[0] = UINT64_C(0x4300000000000000) + uid;
    key.origin.word[1] = UINT64_C(0x4400000000000000) + uid;
    key.client_uid = uid;
    key.instance_nonce = UINT64_C(0xc430000000000001);
    key.controller_epoch = 1;
    key.client_generation = 1;
    key.queue_class = FWLAB_NVME_QUEUE_IO;
    key.worst_case_actions = FWLAB_C43_ACTIONS_PER_COMMAND;
    return key;
}

static struct fwlab_c43_policy_request admission_request(
    const struct fwlab_hif_prepared_token *prepared,
    uint8_t kind)
{
    struct fwlab_c43_policy_request request = {0};

    request.version = FWLAB_C43_POLICY_VERSION;
    request.size = sizeof(request);
    request.handle = prepared->handle;
    request.origin = prepared->origin;
    request.transaction_uid = prepared->reservation_uid;
    request.kind = kind;
    request.queue_class =
        kind == FWLAB_C43_REQUEST_READ ||
                kind == FWLAB_C43_REQUEST_WRITE ||
                kind == FWLAB_C43_REQUEST_FLUSH
            ? FWLAB_NVME_QUEUE_IO
            : FWLAB_NVME_QUEUE_ADMIN;
    request.pointer_format = FWLAB_NVME_DATA_POINTER_PRP;
    if (kind <= FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST ||
        kind == FWLAB_C43_REQUEST_READ ||
        kind == FWLAB_C43_REQUEST_WRITE) {
        request.data_present = 1;
    }
    if (kind == FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE ||
        kind == FWLAB_C43_REQUEST_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST ||
        kind == FWLAB_C43_REQUEST_READ ||
        kind == FWLAB_C43_REQUEST_WRITE ||
        kind == FWLAB_C43_REQUEST_FLUSH) {
        request.namespace_id = 1;
    }
    if (kind == FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES) {
        request.feature_selector = FWLAB_C43_FEATURE_NUMBER_OF_QUEUES;
    }
    if (kind == FWLAB_C43_REQUEST_CREATE_IO_CQ ||
        kind == FWLAB_C43_REQUEST_CREATE_IO_SQ) {
        request.queue_entries = 4;
    }
    return request;
}

static int admit_result_matches(
    const struct fwlab_c43_admit_result *result,
    const struct fwlab_hif_prepared_token *prepared)
{
    return fwlab_c43_admit_result_valid(result) &&
           result->ticket.handle.instance_nonce ==
               prepared->handle.instance_nonce &&
           result->ticket.handle.command_uid == prepared->handle.command_uid &&
           result->ticket.handle.controller_epoch ==
               prepared->handle.controller_epoch &&
           result->ticket.handle.generation == prepared->handle.generation &&
           result->ticket.origin.word[0] == prepared->origin.word[0] &&
           result->ticket.origin.word[1] == prepared->origin.word[1] &&
           result->ticket.ticket_uid == prepared->reservation_uid &&
           result->ticket.generation == prepared->generation;
}

static int fixture_init(
    struct fixture *fixture,
    const struct fwlab_c43_graph_config *config)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->config = *config;
    c43_fake_services_init(&fixture->services);
    c43_fake_services_providers(&fixture->services, &fixture->providers);
    fixture->arena_size = fwlab_c43_graph_arena_size(&fixture->config);
    if (fixture->arena_size == 0 ||
        fixture->arena_size > sizeof(fixture->arena)) {
        return 0;
    }
    return fwlab_c43_graph_init(
               fixture->arena, fixture->arena_size, &fixture->config,
               &fixture->providers, &fixture->graph) == FWLAB_C43_GRAPH_OK;
}

static int result_reserved_valid(
    const struct fwlab_hif_prepare_result *result,
    const struct fwlab_hif_prepare_key *key,
    uint64_t expected_command_uid,
    uint64_t expected_transaction_uid)
{
    return result->version == FWLAB_HIF_COMMAND_PORT_VERSION &&
           result->size == sizeof(*result) &&
           result->disposition == FWLAB_HIF_PREPARE_RESERVED &&
           result->prepared.handle.instance_nonce == key->instance_nonce &&
           result->prepared.handle.command_uid == expected_command_uid &&
           result->prepared.handle.controller_epoch ==
               key->controller_epoch &&
           result->prepared.handle.generation == 1 &&
           result->prepared.origin.word[0] == key->origin.word[0] &&
           result->prepared.origin.word[1] == key->origin.word[1] &&
           result->prepared.reservation_uid == expected_transaction_uid &&
           result->prepared.generation == 1 &&
           result->prepared.reserved == 0 && result->fault_domain == 0 &&
           result->fault_code == 0 &&
           bytes_zero(result->reserved, sizeof(result->reserved));
}

static int observer_read(
    const struct fixture *fixture,
    struct fwlab_c43_graph_observer *observer)
{
    return fwlab_c43_graph_observer_read(fixture->graph, observer) ==
               FWLAB_C43_GRAPH_OK &&
           fwlab_c43_graph_observer_valid(observer);
}

static int admit_kind(
    struct fixture *fixture,
    uint64_t uid,
    uint8_t kind,
    struct fwlab_hif_prepare_result *prepared,
    struct fwlab_c43_admit_result *admitted)
{
    struct fwlab_hif_prepare_key key = prepare_key(uid);
    struct fwlab_c43_policy_request request;

    if (kind != FWLAB_C43_REQUEST_READ &&
        kind != FWLAB_C43_REQUEST_WRITE &&
        kind != FWLAB_C43_REQUEST_FLUSH) {
        key.queue_class = FWLAB_NVME_QUEUE_ADMIN;
    }
    if (fwlab_c43_graph_prepare_start(fixture->graph, &key, prepared) !=
        FWLAB_C43_GRAPH_OK) {
        return 0;
    }
    request = admission_request(&prepared->prepared, kind);
    return fwlab_c43_graph_admit_start(
               fixture->graph, &prepared->prepared, &request, admitted) ==
               FWLAB_C43_GRAPH_OK &&
           admit_result_matches(admitted, &prepared->prepared);
}

static int step_once(
    struct fixture *fixture,
    struct fwlab_c43_graph_observer *observer,
    uint32_t *event_delta)
{
    const uint32_t events_before = fixture->services.event_count;
    struct fwlab_c43_step_result result;

    memset(&result, 0xa5, sizeof(result));
    if (fwlab_c43_graph_step(fixture->graph, 1, &result) !=
            FWLAB_C43_GRAPH_OK ||
        result.version != FWLAB_C43_GRAPH_VERSION ||
        result.size != sizeof(result) || result.requested_budget != 1 ||
        result.units_executed > 1 || result.transitions > 1 ||
        result.ready_events != 0 || result.service_gap_maximum > 1 ||
        !bytes_zero(result.reserved, sizeof(result.reserved)) ||
        !observer_read(fixture, observer)) {
        return 0;
    }
    *event_delta = fixture->services.event_count - events_before;
    return *event_delta <= 1;
}

static struct fwlab_c43_queue_facts queue_facts(
    uint32_t operation,
    uint64_t transaction,
    uint64_t queue,
    uint64_t associated_cq)
{
    struct fwlab_c43_queue_facts facts = {0};

    facts.version = FWLAB_C43_QUEUE_EFFECT_PORT_VERSION;
    facts.size = sizeof(facts);
    facts.transaction.word[0] = transaction;
    facts.queue.word[0] = queue;
    facts.operation = operation;
    facts.role = operation == FWLAB_C43_QUEUE_CREATE_CQ ||
                         operation == FWLAB_C43_QUEUE_DELETE_CQ
                     ? FWLAB_C43_QUEUE_ROLE_IO_CQ
                     : FWLAB_C43_QUEUE_ROLE_IO_SQ;
    facts.queue_entries = 4;
    facts.queue_exists = 1;
    facts.current_relation = 1;
    if (operation == FWLAB_C43_QUEUE_CREATE_CQ ||
        operation == FWLAB_C43_QUEUE_CREATE_SQ) {
        facts.address_present = 1;
    }
    if (associated_cq != 0) {
        facts.associated_cq.word[0] = associated_cq;
        facts.associated_cq_exists = 1;
        facts.association_matches = 1;
    }
    if (operation == FWLAB_C43_QUEUE_DELETE_SQ) {
        facts.active_commands_zero = 1;
        facts.target_refs_zero = 1;
        facts.reserved_publications_zero = 1;
    }
    if (operation == FWLAB_C43_QUEUE_DELETE_CQ) {
        facts.reserved_publications_zero = 1;
        facts.unacked_completions_zero = 1;
    }
    return facts;
}

static uint32_t expected_queue_operation(uint8_t kind)
{
    switch (kind) {
    case FWLAB_C43_REQUEST_CREATE_IO_CQ:
        return FWLAB_C43_QUEUE_CREATE_CQ;
    case FWLAB_C43_REQUEST_CREATE_IO_SQ:
        return FWLAB_C43_QUEUE_CREATE_SQ;
    case FWLAB_C43_REQUEST_DELETE_IO_CQ:
        return FWLAB_C43_QUEUE_DELETE_CQ;
    default:
        return FWLAB_C43_QUEUE_DELETE_SQ;
    }
}

static struct fwlab_c43_queue_effect_request expected_prepare_request(
    const struct fwlab_hif_command_ticket *ticket,
    const struct fwlab_c43_command_observer *observer,
    uint8_t kind)
{
    struct fwlab_c43_queue_effect_request request = {0};
    const uint32_t operation = expected_queue_operation(kind);

    request.version = FWLAB_C43_QUEUE_EFFECT_PORT_VERSION;
    request.size = sizeof(request);
    request.common.version = FWLAB_HIF_ACTION_VERSION;
    request.common.size = sizeof(request.common);
    request.common.token.command = ticket->handle;
    request.common.token.origin = ticket->origin;
    request.common.token.action_uid = observer->first_action_uid;
    request.common.token.generation = observer->action_generation;
    request.common.token.kind = FWLAB_HIF_ACTION_QUEUE_EFFECT;
    request.common.cookie = observer->transaction_uid;
    request.common.requested_units = 1;
    request.operation = operation;
    request.role = operation == FWLAB_C43_QUEUE_CREATE_CQ ||
                           operation == FWLAB_C43_QUEUE_DELETE_CQ
                       ? FWLAB_C43_QUEUE_ROLE_IO_CQ
                       : FWLAB_C43_QUEUE_ROLE_IO_SQ;
    return request;
}

static struct fwlab_hif_action_submit_result expected_submit_result(
    const struct fwlab_hif_action_token *token)
{
    struct fwlab_hif_action_submit_result result = {0};

    result.version = FWLAB_HIF_ACTION_VERSION;
    result.size = sizeof(result);
    result.token = *token;
    result.disposition = FWLAB_HIF_ACTION_ACCEPTED;
    return result;
}

static struct fwlab_c43_queue_finish_request expected_finish_request(
    const struct fwlab_c43_queue_effect_request *prepare,
    const struct fwlab_c43_queue_facts *facts,
    uint32_t decision)
{
    struct fwlab_c43_queue_finish_request request = {0};

    request.version = FWLAB_C43_QUEUE_EFFECT_PORT_VERSION;
    request.size = sizeof(request);
    request.token = prepare->common.token;
    request.transaction = facts->transaction;
    request.decision = decision;
    return request;
}

static struct fwlab_c43_queue_effect_terminal expected_queue_terminal(
    const struct fwlab_c43_queue_effect_request *prepare,
    const struct fwlab_c43_queue_finish_request *finish,
    const struct fwlab_c43_queue_facts *facts,
    uint32_t state)
{
    struct fwlab_c43_queue_effect_terminal terminal = {0};

    terminal.version = FWLAB_C43_QUEUE_EFFECT_PORT_VERSION;
    terminal.size = sizeof(terminal);
    terminal.common.version = FWLAB_HIF_ACTION_VERSION;
    terminal.common.size = sizeof(terminal.common);
    terminal.common.token = prepare->common.token;
    terminal.common.cookie = prepare->common.cookie;
    terminal.facts = *facts;
    terminal.state = state;
    if (state == FWLAB_C43_QUEUE_EFFECT_PREPARED) {
        terminal.common.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
        return terminal;
    }
    terminal.decision = finish->decision;
    if (state == FWLAB_C43_QUEUE_EFFECT_COMMITTED ||
        state == FWLAB_C43_QUEUE_EFFECT_TOO_LATE) {
        terminal.common.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
        terminal.common.effect_class = FWLAB_NVME_EFFECT_FULL;
        terminal.common.units_completed = 1;
    } else if (state == FWLAB_C43_QUEUE_EFFECT_POISONED) {
        terminal.common.terminal_kind = FWLAB_HIF_ACTION_FAILED;
        terminal.common.effect_class = FWLAB_NVME_EFFECT_UNKNOWN_PREFIX;
    } else {
        terminal.common.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
    }
    return terminal;
}

static int run_until_action_state(
    struct fixture *fixture,
    uint32_t slot,
    uint32_t target_state,
    struct fwlab_c43_graph_observer *observer,
    uint32_t maximum_steps)
{
    uint32_t step;

    for (step = 0; step < maximum_steps; ++step) {
        uint32_t event_delta;

        if (!observer_read(fixture, observer)) {
            return 0;
        }
        if (observer->commands[slot].action_state == target_state) {
            return 1;
        }
        if (!step_once(fixture, observer, &event_delta)) {
            return 0;
        }
    }
    return observer->commands[slot].action_state == target_state;
}

static int bootstrap_nq(
    struct fixture *fixture,
    uint64_t uid,
    uint32_t slot,
    struct fwlab_c43_graph_observer *observer)
{
    struct fwlab_hif_prepare_result prepared;
    struct fwlab_c43_admit_result admitted;

    return admit_kind(fixture, uid,
                      FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES,
                      &prepared, &admitted) &&
           run_until_action_state(
               fixture, slot, FWLAB_C43_ACTION_STATE_TERMINAL_HELD,
               observer, 4) &&
           observer->nq_state == FWLAB_C43_NQ_NEGOTIATED;
}

static int scripted_queue_command(
    struct fixture *fixture,
    uint64_t uid,
    uint8_t kind,
    uint32_t slot,
    const struct fwlab_c43_queue_facts *facts,
    uint32_t finish_state,
    struct fwlab_c43_graph_observer *observer)
{
    struct fwlab_hif_prepare_result prepared;
    struct fwlab_c43_admit_result admitted;

    c43_fake_queue_script_configure(
        &fixture->services, facts, finish_state, 0, 0, 0, 0);
    return admit_kind(fixture, uid, kind, &prepared, &admitted) &&
           run_until_action_state(
               fixture, slot, FWLAB_C43_ACTION_STATE_TERMINAL_HELD,
               observer, 16);
}

static int test_capacity_query_and_atomicity(void)
{
    union {
        struct fwlab_hif_prepare_key key;
        struct fwlab_hif_prepare_result result;
        unsigned char bytes[sizeof(struct fwlab_hif_prepare_key)];
    } alias;
    unsigned char alias_before[sizeof(alias)];
    struct fixture fixture;
    struct fwlab_c43_graph_config config = config_fixed();
    struct fwlab_hif_prepare_key keys[5];
    struct fwlab_hif_prepare_result results[4];
    struct fwlab_hif_prepare_result output;
    struct fwlab_hif_prepare_result output_before;
    struct fwlab_c43_graph_observer observer;
    struct fwlab_c43_graph_observer observer_before;
    struct fwlab_c43_graph_observer altered;
    struct fwlab_c43_step_result step;
    struct fwlab_c43_step_result step_before;
    uint8_t arena_before[8192];
    uint32_t index;

    CHECK(fixture_init(&fixture, &config));
    CHECK(observer_read(&fixture, &observer));
    CHECK(observer.active_commands == 0 && observer.active_actions == 0 &&
          fixture.services.event_count == 0);

    for (index = 0; index < 5; ++index) {
        keys[index] = prepare_key(index + 1);
    }
    for (index = 0; index < 4; ++index) {
        memset(&output, 0xa5, sizeof(output));
        CHECK(fwlab_c43_graph_prepare_start(
                  fixture.graph, &keys[index], &output) ==
              FWLAB_C43_GRAPH_OK);
        CHECK(result_reserved_valid(&output, &keys[index], index + 1,
                                    index + 1));
        memcpy(&results[index], &output, sizeof(output));
        CHECK(observer_read(&fixture, &observer));
        CHECK(observer.active_commands == index + 1 &&
              observer.active_actions ==
                  (index + 1) * FWLAB_C43_ACTIONS_PER_COMMAND &&
              observer.ready_count == 0 && observer.cleanup_count == 0 &&
              observer.commands[index].in_use == 1 &&
              observer.commands[index].phase == FWLAB_C43_PHASE_PREPARED &&
              observer.commands[index].action_count ==
                  FWLAB_C43_ACTIONS_PER_COMMAND &&
              observer.commands[index].reservation_credit_mask ==
                  FWLAB_C43_CREDIT_ALL &&
              observer.commands[index].first_action_uid ==
                  UINT64_C(1) + index * FWLAB_C43_ACTIONS_PER_COMMAND &&
              observer.commands[index].action_generation == 1 &&
              observer.commands[index].terminal_winner ==
                  FWLAB_C43_WINNER_NONE &&
              observer.commands[index].success_eligible == 0 &&
              observer.reserved_intent_credits == index + 1 &&
              observer.reserved_ready_credits == index + 1 &&
              observer.reserved_lease_credits == index + 1 &&
              observer.reserved_consume_credits == index + 1 &&
              observer.reserved_finalizer_credits == index + 1 &&
              observer.reserved_abort_credits == index + 1 &&
              observer.reserved_target_credits == index + 1 &&
              observer.reserved_queue_transaction_credits == index + 1 &&
              observer.reserved_block_intent_credits == index + 1);
        if (index == 0) {
            altered = observer;
            altered.commands[0].action_count =
                FWLAB_C43_ACTIONS_PER_COMMAND - 1;
            --altered.active_actions;
            CHECK(!fwlab_c43_graph_observer_valid(&altered));
            altered = observer;
            altered.commands[0].terminal_winner = FWLAB_C43_WINNER_NORMAL;
            CHECK(!fwlab_c43_graph_observer_valid(&altered));
            altered = observer;
            altered.commands[0].publication = FWLAB_C43_PUBLICATION_CONSUMED;
            CHECK(!fwlab_c43_graph_observer_valid(&altered));
            altered = observer;
            altered.commands[0].success_eligible = 1;
            CHECK(!fwlab_c43_graph_observer_valid(&altered));
            altered = observer;
            altered.commands[0].provider_generation_current = 0;
            CHECK(!fwlab_c43_graph_observer_valid(&altered));
        }

        observer_before = observer;
        memcpy(arena_before, fixture.arena, fixture.arena_size);
        memset(&output, 0xa5, sizeof(output));
        CHECK(fwlab_c43_graph_prepare_query(
                  fixture.graph, &keys[index], &output) ==
              FWLAB_C43_GRAPH_OK);
        CHECK(memcmp(&output, &results[index], sizeof(output)) == 0);
        CHECK(observer_read(&fixture, &observer));
        CHECK(memcmp(&observer, &observer_before, sizeof(observer)) == 0);

        memset(&output, 0xa5, sizeof(output));
        memcpy(&output_before, &output, sizeof(output));
        CHECK(fwlab_c43_graph_prepare_start(
                  fixture.graph, &keys[index], &output) ==
              FWLAB_C43_GRAPH_IN_PROGRESS);
        CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
        CHECK(memcmp(arena_before, fixture.arena,
                     fixture.arena_size) == 0);
        CHECK(observer_read(&fixture, &observer));
        CHECK(memcmp(&observer, &observer_before, sizeof(observer)) == 0);
    }

    observer_before = observer;
    memcpy(arena_before, fixture.arena, fixture.arena_size);
    memset(&output, 0xa5, sizeof(output));
    CHECK(fwlab_c43_graph_prepare_start(fixture.graph, &keys[4], &output) ==
          FWLAB_C43_GRAPH_OK);
    CHECK(output.version == FWLAB_HIF_COMMAND_PORT_VERSION &&
          output.size == sizeof(output) &&
          output.disposition == FWLAB_HIF_PREPARE_BACKPRESSURE &&
          bytes_zero(&output.prepared, sizeof(output.prepared)) &&
          output.fault_domain == 0 && output.fault_code == 0 &&
          bytes_zero(output.reserved, sizeof(output.reserved)));
    CHECK(observer_read(&fixture, &observer));
    CHECK(memcmp(&observer, &observer_before, sizeof(observer)) == 0);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);

    keys[4] = keys[0];
    ++keys[4].origin.word[0];
    memset(&output, 0xa5, sizeof(output));
    memcpy(&output_before, &output, sizeof(output));
    CHECK(fwlab_c43_graph_prepare_query(fixture.graph, &keys[4], &output) ==
          FWLAB_C43_GRAPH_POISONED);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);

    keys[4] = prepare_key(99);
    keys[4].origin = keys[0].origin;
    memset(&output, 0xa5, sizeof(output));
    memcpy(&output_before, &output, sizeof(output));
    CHECK(fwlab_c43_graph_prepare_start(fixture.graph, &keys[4], &output) ==
          FWLAB_C43_GRAPH_POISONED);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);

    keys[4] = keys[0];
    keys[4].queue_class = FWLAB_NVME_QUEUE_ADMIN;
    memset(&output, 0xa5, sizeof(output));
    memcpy(&output_before, &output, sizeof(output));
    CHECK(fwlab_c43_graph_prepare_start(fixture.graph, &keys[4], &output) ==
          FWLAB_C43_GRAPH_POISONED);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);

    keys[4] = prepare_key(99);
    memset(&output, 0xa5, sizeof(output));
    memcpy(&output_before, &output, sizeof(output));
    CHECK(fwlab_c43_graph_prepare_query(fixture.graph, &keys[4], &output) ==
          FWLAB_C43_GRAPH_STALE);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);
    ++keys[4].controller_epoch;
    memset(&output, 0xa5, sizeof(output));
    memcpy(&output_before, &output, sizeof(output));
    CHECK(fwlab_c43_graph_prepare_query(fixture.graph, &keys[4], &output) ==
          FWLAB_C43_GRAPH_STALE);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);
    memset(&output, 0xa5, sizeof(output));
    memcpy(&output_before, &output, sizeof(output));
    CHECK(fwlab_c43_graph_prepare_start(fixture.graph, &keys[4], &output) ==
          FWLAB_C43_GRAPH_STALE);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);
    --keys[4].controller_epoch;
    keys[4].worst_case_actions = FWLAB_C43_ACTIONS_PER_COMMAND - 1;
    memset(&output, 0xa5, sizeof(output));
    memcpy(&output_before, &output, sizeof(output));
    CHECK(fwlab_c43_graph_prepare_start(fixture.graph, &keys[4], &output) ==
          FWLAB_C43_GRAPH_INVALID);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);
    keys[4].worst_case_actions = FWLAB_C43_ACTIONS_PER_COMMAND + 1;
    memset(&output, 0xa5, sizeof(output));
    memcpy(&output_before, &output, sizeof(output));
    CHECK(fwlab_c43_graph_prepare_start(fixture.graph, &keys[4], &output) ==
          FWLAB_C43_GRAPH_INVALID);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);
    CHECK(observer_read(&fixture, &observer));
    CHECK(memcmp(&observer, &observer_before, sizeof(observer)) == 0);

    CHECK(observer_read(&fixture, &observer_before));
    memset(&step, 0xa5, sizeof(step));
    memset(&step_before, 0, sizeof(step_before));
    step_before.version = FWLAB_C43_GRAPH_VERSION;
    step_before.size = sizeof(step_before);
    step_before.requested_budget = 1;
    CHECK(fwlab_c43_graph_step(fixture.graph, 1, &step) ==
          FWLAB_C43_GRAPH_OK);
    CHECK(memcmp(&step, &step_before, sizeof(step)) == 0);
    CHECK(observer_read(&fixture, &observer));
    CHECK(memcmp(&observer, &observer_before, sizeof(observer)) == 0);

    memcpy(arena_before, fixture.arena, fixture.arena_size);
    CHECK(fwlab_c43_graph_prepare_start(
              fixture.graph, &keys[0],
              (struct fwlab_hif_prepare_result *)(void *)fixture.graph) ==
          FWLAB_C43_GRAPH_INVALID);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);
    CHECK(fwlab_c43_graph_observer_read(
              fixture.graph,
              (struct fwlab_c43_graph_observer *)(void *)fixture.graph) ==
          FWLAB_C43_GRAPH_INVALID);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);

    memset(&alias, 0, sizeof(alias));
    alias.key = keys[0];
    memcpy(alias_before, &alias, sizeof(alias));
    CHECK(fwlab_c43_graph_prepare_query(
              fixture.graph, &alias.key, &alias.result) ==
          FWLAB_C43_GRAPH_INVALID);
    CHECK(memcmp(alias_before, &alias, sizeof(alias)) == 0);
    CHECK(fixture.services.event_count == 0);
    return 0;
}

static void limit_counter(
    struct fwlab_c43_graph_config *config,
    uint32_t selector)
{
    struct fwlab_c43_counter_seed *seed;

    switch (selector) {
    case 0:
        seed = &config->command_uid;
        break;
    case 1:
        seed = &config->action_uid;
        break;
    case 2:
        seed = &config->transaction_uid;
        break;
    case 3:
        seed = &config->lease_uid;
        break;
    case 4:
        seed = &config->consume_uid;
        break;
    default:
        seed = &config->finalizer_uid;
        break;
    }
    seed->maximum = selector == 1 ? FWLAB_C43_ACTIONS_PER_COMMAND : 1;
}

static int test_counter_exhaustion(void)
{
    uint32_t selector;

    for (selector = 0; selector < 6; ++selector) {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_hif_prepare_key first = prepare_key(1);
        struct fwlab_hif_prepare_key second = prepare_key(2);
        struct fwlab_hif_prepare_result result;
        struct fwlab_hif_prepare_result before;
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_graph_observer observer_before;
        uint8_t arena_before[8192];

        limit_counter(&config, selector);
        CHECK(fixture_init(&fixture, &config));
        CHECK(fwlab_c43_graph_prepare_start(fixture.graph, &first, &result) ==
              FWLAB_C43_GRAPH_OK);
        CHECK(result_reserved_valid(&result, &first, 1, 1));
        CHECK(observer_read(&fixture, &observer_before));
        memcpy(arena_before, fixture.arena, fixture.arena_size);
        memset(&result, 0xa5, sizeof(result));
        memcpy(&before, &result, sizeof(result));
        CHECK(fwlab_c43_graph_prepare_start(
                  fixture.graph, &second, &result) ==
              FWLAB_C43_GRAPH_COUNTER_EXHAUSTED);
        CHECK(memcmp(&result, &before, sizeof(result)) == 0);
        CHECK(observer_read(&fixture, &observer));
        CHECK(memcmp(&observer, &observer_before, sizeof(observer)) == 0);
        CHECK(memcmp(arena_before, fixture.arena,
                     fixture.arena_size) == 0);
        CHECK(fwlab_c43_graph_prepare_query(fixture.graph, &first, &result) ==
              FWLAB_C43_GRAPH_OK);
        CHECK(result_reserved_valid(&result, &first, 1, 1));
        CHECK(fixture.services.event_count == 0);
    }

    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_hif_prepare_key key = prepare_key(1);
        struct fwlab_hif_prepare_result result;
        struct fwlab_hif_prepare_result before;
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_graph_observer observer_before;
        uint8_t arena_before[8192];

        config.action_uid.maximum = FWLAB_C43_ACTIONS_PER_COMMAND - 1;
        CHECK(fixture_init(&fixture, &config));
        CHECK(observer_read(&fixture, &observer_before));
        memcpy(arena_before, fixture.arena, fixture.arena_size);
        memset(&result, 0xa5, sizeof(result));
        memcpy(&before, &result, sizeof(result));
        CHECK(fwlab_c43_graph_prepare_start(fixture.graph, &key, &result) ==
              FWLAB_C43_GRAPH_COUNTER_EXHAUSTED);
        CHECK(memcmp(&result, &before, sizeof(result)) == 0);
        CHECK(observer_read(&fixture, &observer));
        CHECK(memcmp(&observer, &observer_before, sizeof(observer)) == 0);
        CHECK(memcmp(arena_before, fixture.arena,
                     fixture.arena_size) == 0);
    }
    return 0;
}

static int test_counter_endpoints_and_domains(void)
{
    struct fixture fixture;
    struct fwlab_c43_graph_config config = config_fixed();
    struct fwlab_hif_prepare_key keys[FWLAB_C43_MAX_COMMANDS + 1];
    struct fwlab_hif_prepare_result results[FWLAB_C43_MAX_COMMANDS];
    struct fwlab_hif_prepare_result output;
    struct fwlab_c43_graph_observer observer;
    uint32_t command;

    config.command_uid.next = UINT64_MAX - 3;
    config.command_uid.maximum = UINT64_MAX;
    config.action_uid.next = UINT64_MAX - 31;
    config.action_uid.maximum = UINT64_MAX;
    config.transaction_uid.next = 100;
    config.transaction_uid.maximum = 103;
    config.lease_uid.next = 200;
    config.lease_uid.maximum = 203;
    config.consume_uid.next = 300;
    config.consume_uid.maximum = 303;
    config.finalizer_uid.next = 400;
    config.finalizer_uid.maximum = 403;
    CHECK(fixture_init(&fixture, &config));

    for (command = 0; command < FWLAB_C43_MAX_COMMANDS + 1; ++command) {
        keys[command] = prepare_key(command + 1);
    }
    for (command = 0; command < FWLAB_C43_MAX_COMMANDS; ++command) {
        CHECK(fwlab_c43_graph_prepare_start(
                  fixture.graph, &keys[command], &output) ==
              FWLAB_C43_GRAPH_OK);
        CHECK(result_reserved_valid(
            &output, &keys[command], UINT64_MAX - 3 + command,
            UINT64_C(100) + command));
        memcpy(&results[command], &output, sizeof(output));
        CHECK(observer_read(&fixture, &observer));
        CHECK(observer.commands[command].first_action_uid ==
                  UINT64_MAX - 31 +
                      command * FWLAB_C43_ACTIONS_PER_COMMAND &&
              observer.commands[command].action_generation == 1);
    }
    CHECK(observer.active_commands == FWLAB_C43_MAX_COMMANDS &&
          observer.active_actions == FWLAB_C43_MAX_ACTIONS &&
          observer.commands[3].handle.command_uid == UINT64_MAX &&
          observer.commands[3].first_action_uid == UINT64_MAX - 7);
    for (command = 0; command < FWLAB_C43_MAX_COMMANDS; ++command) {
        uint32_t other;
        uint32_t ordinal;

        CHECK(fwlab_c43_graph_prepare_query(
                  fixture.graph, &keys[command], &output) ==
              FWLAB_C43_GRAPH_OK);
        CHECK(memcmp(&output, &results[command], sizeof(output)) == 0);
        for (other = 0; other < FWLAB_C43_MAX_COMMANDS; ++other) {
            uint32_t other_ordinal;

            for (ordinal = 0; ordinal < FWLAB_C43_ACTIONS_PER_COMMAND;
                 ++ordinal) {
                for (other_ordinal = 0;
                     other_ordinal < FWLAB_C43_ACTIONS_PER_COMMAND;
                     ++other_ordinal) {
                    if (command != other || ordinal != other_ordinal) {
                        CHECK(observer.commands[command].first_action_uid +
                                  ordinal !=
                              observer.commands[other].first_action_uid +
                                  other_ordinal);
                    }
                }
            }
        }
    }
    CHECK(fwlab_c43_graph_prepare_start(
              fixture.graph, &keys[FWLAB_C43_MAX_COMMANDS], &output) ==
          FWLAB_C43_GRAPH_OK);
    CHECK(output.disposition == FWLAB_HIF_PREPARE_BACKPRESSURE);
    CHECK(fixture.services.event_count == 0);
    return 0;
}

static int test_admission_identity_and_policy(void)
{
    static const uint8_t kinds[FWLAB_C43_MAX_COMMANDS] = {
        FWLAB_C43_REQUEST_IDENTIFY_CONTROLLER,
        FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES,
        FWLAB_C43_REQUEST_ABORT,
        FWLAB_C43_REQUEST_READ,
    };
    union {
        struct fwlab_c43_policy_request request;
        struct fwlab_c43_admit_result result;
    } alias;
    struct fixture fixture;
    struct fwlab_c43_graph_config config = config_fixed();
    struct fwlab_hif_prepare_key keys[FWLAB_C43_MAX_COMMANDS];
    struct fwlab_hif_prepare_result prepared[FWLAB_C43_MAX_COMMANDS];
    struct fwlab_c43_policy_request requests[FWLAB_C43_MAX_COMMANDS];
    struct fwlab_c43_admit_result admitted[FWLAB_C43_MAX_COMMANDS];
    struct fwlab_c43_admit_result output;
    struct fwlab_c43_admit_result output_before;
    struct fwlab_c43_graph_observer observer;
    struct fwlab_c43_graph_observer observer_before;
    struct fwlab_c43_graph_observer altered;
    uint8_t arena_before[8192];
    unsigned char alias_before[sizeof(alias)];
    uint32_t index;

    CHECK(fixture_init(&fixture, &config));
    for (index = 0; index < FWLAB_C43_MAX_COMMANDS; ++index) {
        keys[index] = prepare_key(index + 1);
        if (kinds[index] != FWLAB_C43_REQUEST_READ &&
            kinds[index] != FWLAB_C43_REQUEST_WRITE &&
            kinds[index] != FWLAB_C43_REQUEST_FLUSH) {
            keys[index].queue_class = FWLAB_NVME_QUEUE_ADMIN;
        }
        CHECK(fwlab_c43_graph_prepare_start(
                  fixture.graph, &keys[index], &prepared[index]) ==
              FWLAB_C43_GRAPH_OK);
        requests[index] = admission_request(&prepared[index].prepared,
                                            kinds[index]);
    }
    requests[3].namespace_id = 2;

    CHECK(observer_read(&fixture, &observer_before));
    memcpy(arena_before, fixture.arena, fixture.arena_size);
    memset(&output, 0xa5, sizeof(output));
    memcpy(&output_before, &output, sizeof(output));
    CHECK(fwlab_c43_graph_admit_query(
              fixture.graph, &prepared[0].prepared, &requests[0], &output) ==
          FWLAB_C43_GRAPH_WRONG_STATE);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);

    {
        struct fwlab_c43_policy_request wrong_queue = requests[0];

        wrong_queue.queue_class = FWLAB_NVME_QUEUE_IO;
        memset(&output, 0xa5, sizeof(output));
        memcpy(&output_before, &output, sizeof(output));
        CHECK(fwlab_c43_graph_admit_start(
                  fixture.graph, &prepared[0].prepared, &wrong_queue,
                  &output) == FWLAB_C43_GRAPH_POISONED);
        CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
        CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);
        CHECK(fixture.services.event_count == 0);
    }

    for (index = 0; index < FWLAB_C43_MAX_COMMANDS; ++index) {
        CHECK(fwlab_c43_graph_admit_start(
                  fixture.graph, &prepared[index].prepared, &requests[index],
                  &output) == FWLAB_C43_GRAPH_OK);
        CHECK(admit_result_matches(&output, &prepared[index].prepared));
        memcpy(&admitted[index], &output, sizeof(output));
        CHECK(observer_read(&fixture, &observer));
        CHECK(observer.commands[index].phase ==
                  FWLAB_C43_PHASE_ADMITTED_POLICY &&
              observer.commands[index].terminal_winner ==
                  FWLAB_C43_WINNER_NONE &&
              observer.commands[index].publication ==
                  FWLAB_C43_PUBLICATION_ELIGIBLE &&
              observer.commands[index].satisfied_witness_mask == 0);
        if (index == 0) {
            CHECK(observer.commands[index].required_witness_mask ==
                      (FWLAB_C43_WITNESS_PAYLOAD_READY |
                       FWLAB_C43_WITNESS_DMA_OUT_COMPLETE) &&
                  observer.commands[index].success_eligible == 0);
        } else {
            CHECK(observer.commands[index].required_witness_mask == 0 &&
                  observer.commands[index].success_eligible == 0);
        }
        if (index == 0) {
            altered = observer;
            altered.commands[0].satisfied_witness_mask =
                FWLAB_C43_WITNESS_PAYLOAD_READY;
            CHECK(!fwlab_c43_graph_observer_valid(&altered));
        } else if (index == 1) {
            altered = observer;
            altered.commands[1].success_eligible = 1;
            CHECK(!fwlab_c43_graph_observer_valid(&altered));
        }
    }
    CHECK(fixture.services.event_count == 0);

    CHECK(observer_read(&fixture, &observer_before));
    memcpy(arena_before, fixture.arena, fixture.arena_size);
    memset(&output, 0xa5, sizeof(output));
    CHECK(fwlab_c43_graph_admit_query(
              fixture.graph, &prepared[0].prepared, &requests[0], &output) ==
          FWLAB_C43_GRAPH_OK);
    CHECK(memcmp(&output, &admitted[0], sizeof(output)) == 0);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);

    memset(&output, 0xa5, sizeof(output));
    memcpy(&output_before, &output, sizeof(output));
    CHECK(fwlab_c43_graph_admit_start(
              fixture.graph, &prepared[0].prepared, &requests[0], &output) ==
          FWLAB_C43_GRAPH_IN_PROGRESS);
    CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);

    {
        struct fwlab_c43_policy_request changed = requests[0];

        changed.namespace_id = 1;
        memset(&output, 0xa5, sizeof(output));
        memcpy(&output_before, &output, sizeof(output));
        CHECK(fwlab_c43_graph_admit_query(
                  fixture.graph, &prepared[0].prepared, &changed, &output) ==
              FWLAB_C43_GRAPH_POISONED);
        CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
        CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);
    }
    {
        struct fwlab_c43_policy_request changed = requests[0];

        changed.fua = 1;
        memset(&output, 0xa5, sizeof(output));
        memcpy(&output_before, &output, sizeof(output));
        CHECK(fwlab_c43_graph_admit_query(
                  fixture.graph, &prepared[0].prepared, &changed, &output) ==
              FWLAB_C43_GRAPH_POISONED);
        CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
        CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);
    }
    {
        struct fwlab_c43_policy_request changed = requests[0];

        ++changed.handle.command_uid;
        memset(&output, 0xa5, sizeof(output));
        memcpy(&output_before, &output, sizeof(output));
        CHECK(fwlab_c43_graph_admit_start(
                  fixture.graph, &prepared[0].prepared, &changed, &output) ==
              FWLAB_C43_GRAPH_POISONED);
        CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
        CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);
    }
    {
        struct fwlab_hif_prepared_token stale = prepared[0].prepared;

        stale.handle.command_uid += 100;
        stale.origin.word[0] += 100;
        stale.reservation_uid += 100;
        memset(&output, 0xa5, sizeof(output));
        memcpy(&output_before, &output, sizeof(output));
        CHECK(fwlab_c43_graph_admit_start(
                  fixture.graph, &stale, &requests[0], &output) ==
              FWLAB_C43_GRAPH_STALE);
        CHECK(memcmp(&output, &output_before, sizeof(output)) == 0);
        CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);
    }

    memcpy(arena_before, fixture.arena, fixture.arena_size);
    CHECK(fwlab_c43_graph_admit_query(
              fixture.graph, &prepared[0].prepared, &requests[0],
              (struct fwlab_c43_admit_result *)(void *)fixture.graph) ==
          FWLAB_C43_GRAPH_INVALID);
    CHECK(memcmp(arena_before, fixture.arena, fixture.arena_size) == 0);

    memset(&alias, 0, sizeof(alias));
    alias.request = requests[0];
    memcpy(alias_before, &alias, sizeof(alias));
    CHECK(fwlab_c43_graph_admit_query(
              fixture.graph, &prepared[0].prepared, &alias.request,
              &alias.result) == FWLAB_C43_GRAPH_INVALID);
    CHECK(memcmp(alias_before, &alias, sizeof(alias)) == 0);
    CHECK(observer_read(&fixture, &observer));
    CHECK(memcmp(&observer, &observer_before, sizeof(observer)) == 0);
    CHECK(fixture.services.event_count == 0);
    return 0;
}

static int test_cross_instance_admit_stale(void)
{
    struct fixture local;
    struct fixture foreign;
    struct fwlab_c43_graph_config local_config = config_fixed();
    struct fwlab_c43_graph_config foreign_config = config_fixed();
    struct fwlab_hif_prepare_key local_key = prepare_key(1);
    struct fwlab_hif_prepare_key foreign_key = prepare_key(1);
    struct fwlab_hif_prepare_result local_prepared;
    struct fwlab_hif_prepare_result foreign_prepared;
    struct fwlab_c43_policy_request local_request;
    struct fwlab_c43_policy_request foreign_request;
    struct fwlab_c43_admit_result output;
    struct fwlab_c43_admit_result before;
    uint8_t arena_before[8192];

    foreign_config.instance_nonce = UINT64_C(0xc430000000000002);
    local_key.queue_class = FWLAB_NVME_QUEUE_ADMIN;
    foreign_key.queue_class = FWLAB_NVME_QUEUE_ADMIN;
    foreign_key.instance_nonce = foreign_config.instance_nonce;
    CHECK(fixture_init(&local, &local_config));
    CHECK(fixture_init(&foreign, &foreign_config));
    CHECK(fwlab_c43_graph_prepare_start(
              local.graph, &local_key, &local_prepared) ==
          FWLAB_C43_GRAPH_OK);
    CHECK(fwlab_c43_graph_prepare_start(
              foreign.graph, &foreign_key, &foreign_prepared) ==
          FWLAB_C43_GRAPH_OK);
    local_request = admission_request(&local_prepared.prepared,
                                      FWLAB_C43_REQUEST_IDENTIFY_CONTROLLER);
    foreign_request = admission_request(
        &foreign_prepared.prepared, FWLAB_C43_REQUEST_IDENTIFY_CONTROLLER);
    CHECK(fwlab_c43_graph_admit_start(
              local.graph, &local_prepared.prepared, &local_request,
              &output) == FWLAB_C43_GRAPH_OK);

    memcpy(arena_before, local.arena, local.arena_size);
    memset(&output, 0xa5, sizeof(output));
    memcpy(&before, &output, sizeof(output));
    CHECK(fwlab_c43_graph_admit_query(
              local.graph, &foreign_prepared.prepared, &foreign_request,
              &output) == FWLAB_C43_GRAPH_STALE);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    CHECK(memcmp(arena_before, local.arena, local.arena_size) == 0);
    CHECK(fwlab_c43_graph_admit_start(
              local.graph, &foreign_prepared.prepared, &foreign_request,
              &output) == FWLAB_C43_GRAPH_STALE);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    CHECK(memcmp(arena_before, local.arena, local.arena_size) == 0);
    CHECK(local.services.event_count == 0 &&
          foreign.services.event_count == 0);
    return 0;
}

static int test_nq_and_create_cq_2pc(void)
{
    struct fixture fixture;
    struct fwlab_c43_graph_config config = config_fixed();
    struct fwlab_hif_prepare_result prepared;
    struct fwlab_c43_admit_result admitted;
    struct fwlab_c43_graph_observer observer;
    struct fwlab_c43_queue_facts facts = queue_facts(
        FWLAB_C43_QUEUE_CREATE_CQ, UINT64_C(0x1001), UINT64_C(0x2001), 0);
    struct fwlab_c43_queue_effect_request expected_prepare;
    struct fwlab_c43_queue_finish_request expected_finish;
    struct fwlab_c43_queue_effect_terminal expected_prepared_terminal;
    struct fwlab_c43_queue_effect_terminal expected_finished_terminal;
    struct fwlab_hif_action_submit_result expected_submit;
    struct c43_fake_event_record expected_events[10] = {{0}};
    uint32_t event_delta;
    uint32_t index;

    CHECK(fixture_init(&fixture, &config));
    CHECK(admit_kind(&fixture, 1,
                     FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES,
                     &prepared, &admitted));
    CHECK(fixture.services.event_count == 0);
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 0 &&
          observer.nq_state == FWLAB_C43_NQ_NEGOTIATED &&
          observer.commands[0].action_domain ==
              FWLAB_C43_ACTION_DOMAIN_QUEUE &&
          observer.commands[0].action_state ==
              FWLAB_C43_ACTION_STATE_TERMINAL_HELD &&
          observer.commands[0].resolution_valid == 1 &&
          observer.commands[0].resolved_status == FWLAB_C43_STATUS_SUCCESS &&
          observer.commands[0].success_eligible == 1 &&
          observer.ready_count == 0);

    CHECK(admit_kind(&fixture, 2,
                     FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES,
                     &prepared, &admitted));
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 0 &&
          observer.nq_state == FWLAB_C43_NQ_NEGOTIATED &&
          observer.commands[1].resolved_status == FWLAB_C43_STATUS_SUCCESS &&
          observer.commands[1].success_eligible == 1 &&
          fixture.services.event_count == 0);

    c43_fake_queue_script_configure(
        &fixture.services, &facts, FWLAB_C43_QUEUE_EFFECT_COMMITTED,
        1, 1, 1, 1);
    fixture.services.queue_script.retire_in_progress_remaining = 1;
    CHECK(admit_kind(&fixture, 3, FWLAB_C43_REQUEST_CREATE_IO_CQ,
                     &prepared, &admitted));
    CHECK(fixture.services.event_count == 0);

    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 0 && observer.queue_txn_active == 1 &&
          observer.queue_owner_slot_plus_one == 3 &&
          observer.io_cq_present == 0 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_SUBMIT_READY);
    expected_prepare = expected_prepare_request(
        &admitted.ticket, &observer.commands[2],
        FWLAB_C43_REQUEST_CREATE_IO_CQ);
    expected_finish = expected_finish_request(
        &expected_prepare, &facts, FWLAB_C43_QUEUE_FINISH_COMMIT);
    expected_prepared_terminal = expected_queue_terminal(
        &expected_prepare, &expected_finish, &facts,
        FWLAB_C43_QUEUE_EFFECT_PREPARED);
    expected_finished_terminal = expected_queue_terminal(
        &expected_prepare, &expected_finish, &facts,
        FWLAB_C43_QUEUE_EFFECT_COMMITTED);
    expected_submit = expected_submit_result(&expected_prepare.common.token);

    for (index = 0; index < 10; ++index) {
        expected_events[index].sequence = index + 1;
    }
    expected_events[0].kind = C43_FAKE_QUEUE_PREPARE_START;
    expected_events[0].returned = FWLAB_HIF_ACTION_BACKPRESSURE;
    expected_events[0].input.queue_prepare = expected_prepare;
    expected_events[1].kind = C43_FAKE_QUEUE_PREPARE_START;
    expected_events[1].returned = FWLAB_HIF_ACTION_ACCEPTED;
    expected_events[1].output_written = 1;
    expected_events[1].input.queue_prepare = expected_prepare;
    expected_events[1].output.submit = expected_submit;
    expected_events[2].kind = C43_FAKE_QUEUE_PREPARE_QUERY;
    expected_events[2].returned = FWLAB_C43_API_OK;
    expected_events[2].ready_written = 1;
    expected_events[2].input.token = expected_prepare.common.token;
    expected_events[3].kind = C43_FAKE_QUEUE_PREPARE_QUERY;
    expected_events[3].returned = FWLAB_C43_API_OK;
    expected_events[3].ready_written = 1;
    expected_events[3].ready_value = 1;
    expected_events[3].output_written = 1;
    expected_events[3].input.token = expected_prepare.common.token;
    expected_events[3].output.queue_terminal = expected_prepared_terminal;
    expected_events[4].kind = C43_FAKE_QUEUE_FINISH_START;
    expected_events[4].returned = FWLAB_HIF_ACTION_BACKPRESSURE;
    expected_events[4].input.queue_finish = expected_finish;
    expected_events[5].kind = C43_FAKE_QUEUE_FINISH_START;
    expected_events[5].returned = FWLAB_HIF_ACTION_ACCEPTED;
    expected_events[5].output_written = 1;
    expected_events[5].input.queue_finish = expected_finish;
    expected_events[5].output.submit = expected_submit;
    expected_events[6].kind = C43_FAKE_QUEUE_FINISH_QUERY;
    expected_events[6].returned = FWLAB_C43_API_OK;
    expected_events[6].ready_written = 1;
    expected_events[6].input.token = expected_prepare.common.token;
    expected_events[7].kind = C43_FAKE_QUEUE_FINISH_QUERY;
    expected_events[7].returned = FWLAB_C43_API_OK;
    expected_events[7].ready_written = 1;
    expected_events[7].ready_value = 1;
    expected_events[7].output_written = 1;
    expected_events[7].input.token = expected_prepare.common.token;
    expected_events[7].output.queue_terminal = expected_finished_terminal;
    expected_events[8].kind = C43_FAKE_QUEUE_RETIRE;
    expected_events[8].returned = FWLAB_C43_API_IN_PROGRESS;
    expected_events[8].input.token = expected_prepare.common.token;
    expected_events[9].kind = C43_FAKE_QUEUE_RETIRE;
    expected_events[9].returned = FWLAB_C43_API_OK;
    expected_events[9].input.token = expected_prepare.common.token;
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 1 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_SUBMIT_READY);
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 1 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_PREPARE_QUERY);
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 1 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_PREPARE_QUERY);
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 1 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_DECIDE);
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 0 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_FINISH_READY &&
          observer.commands[2].resolution_valid == 1 &&
          observer.commands[2].resolved_status == FWLAB_C43_STATUS_SUCCESS);
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 1 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_FINISH_READY);
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 1 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_FINISH_QUERY);
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 1 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_FINISH_QUERY);
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 1 && observer.io_cq_present == 0 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_APPLY_COMMIT);
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 0 && observer.io_cq_present == 1 &&
          observer.io_cq.word[0] == UINT64_C(0x2001) &&
          observer.queue_txn_active == 1 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_RETIRE &&
          observer.commands[2].satisfied_witness_mask ==
              FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED &&
          observer.commands[2].success_eligible == 1 &&
          observer.ready_count == 0);
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 1 && observer.queue_txn_active == 1 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_RETIRE);
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 1 && observer.queue_txn_active == 0 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_TERMINAL_HELD);

    CHECK(fixture.services.event_count == 10 && !fixture.services.overflow &&
          fixture.services.queue_script.fault == 0 &&
          fixture.services.queue_script.prepare_start_calls == 2 &&
          fixture.services.queue_script.finish_start_calls == 2 &&
          memcmp(&fixture.services.queue_script.first_prepare_request,
                 &fixture.services.queue_script.last_prepare_request,
                 sizeof(fixture.services.queue_script.first_prepare_request)) ==
              0 &&
          memcmp(&fixture.services.queue_script.first_finish_request,
                 &fixture.services.queue_script.last_finish_request,
                 sizeof(fixture.services.queue_script.first_finish_request)) ==
              0);
    CHECK(memcmp(fixture.services.events, expected_events,
                 sizeof(expected_events)) == 0);
    return 0;
}

static int test_queue_order_abort_and_delete(void)
{
    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;
        struct fwlab_c43_graph_observer observer;
        uint32_t event_delta;

        CHECK(fixture_init(&fixture, &config));
        CHECK(admit_kind(&fixture, 1, FWLAB_C43_REQUEST_CREATE_IO_CQ,
                         &prepared, &admitted));
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(event_delta == 0 && fixture.services.event_count == 0 &&
              observer.nq_state == FWLAB_C43_NQ_UNNEGOTIATED &&
              observer.io_cq_present == 0 &&
              observer.commands[0].action_state ==
                  FWLAB_C43_ACTION_STATE_REJECTED_NO_EFFECT &&
              observer.commands[0].resolved_status ==
                  FWLAB_C43_STATUS_COMMAND_SEQUENCE &&
              observer.commands[0].success_eligible == 0);
    }

    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts cq = queue_facts(
            FWLAB_C43_QUEUE_CREATE_CQ, 1, UINT64_C(0x3101), 0);
        struct fwlab_c43_queue_facts bad_sq = queue_facts(
            FWLAB_C43_QUEUE_CREATE_SQ, 2, UINT64_C(0x3102),
            UINT64_C(0x3101));
        struct fwlab_c43_queue_facts good_sq = queue_facts(
            FWLAB_C43_QUEUE_CREATE_SQ, 3, UINT64_C(0x3103),
            UINT64_C(0x3101));

        CHECK(fixture_init(&fixture, &config));
        CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
        CHECK(scripted_queue_command(
            &fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ, 1, &cq,
            FWLAB_C43_QUEUE_EFFECT_COMMITTED, &observer));
        CHECK(observer.io_cq_present == 1 &&
              observer.io_cq.word[0] == UINT64_C(0x3101));

        bad_sq.association_matches = 0;
        CHECK(scripted_queue_command(
            &fixture, 3, FWLAB_C43_REQUEST_CREATE_IO_SQ, 2, &bad_sq,
            FWLAB_C43_QUEUE_EFFECT_ABORTED, &observer));
        CHECK(observer.io_sq_present == 0 &&
              observer.commands[2].resolved_status ==
                  FWLAB_C43_STATUS_INVALID_QUEUE &&
              observer.commands[2].satisfied_witness_mask == 0 &&
              observer.commands[2].success_eligible == 0 &&
              fixture.services.queue_script.first_finish_request.decision ==
                  FWLAB_C43_QUEUE_FINISH_ABORT);

        CHECK(scripted_queue_command(
            &fixture, 4, FWLAB_C43_REQUEST_CREATE_IO_SQ, 3, &good_sq,
            FWLAB_C43_QUEUE_EFFECT_COMMITTED, &observer));
        CHECK(observer.io_sq_present == 1 &&
              observer.io_sq.word[0] == UINT64_C(0x3103) &&
              observer.sq_associated_cq.word[0] == UINT64_C(0x3101));
    }

    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts cq = queue_facts(
            FWLAB_C43_QUEUE_CREATE_CQ, 1, UINT64_C(0x3201), 0);
        struct fwlab_c43_queue_facts sq = queue_facts(
            FWLAB_C43_QUEUE_CREATE_SQ, 2, UINT64_C(0x3202),
            UINT64_C(0x3201));
        struct fwlab_c43_queue_facts delete_sq = queue_facts(
            FWLAB_C43_QUEUE_DELETE_SQ, 3, UINT64_C(0x3202),
            UINT64_C(0x3201));

        CHECK(fixture_init(&fixture, &config));
        CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
        CHECK(scripted_queue_command(
            &fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ, 1, &cq,
            FWLAB_C43_QUEUE_EFFECT_COMMITTED, &observer));
        CHECK(scripted_queue_command(
            &fixture, 3, FWLAB_C43_REQUEST_CREATE_IO_SQ, 2, &sq,
            FWLAB_C43_QUEUE_EFFECT_COMMITTED, &observer));
        CHECK(scripted_queue_command(
            &fixture, 4, FWLAB_C43_REQUEST_DELETE_IO_SQ, 3, &delete_sq,
            FWLAB_C43_QUEUE_EFFECT_COMMITTED, &observer));
        CHECK(observer.io_sq_present == 0 && observer.io_cq_present == 1 &&
              observer.nq_state == FWLAB_C43_NQ_NEGOTIATED);
    }

    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts cq = queue_facts(
            FWLAB_C43_QUEUE_CREATE_CQ, 1, UINT64_C(0x3231), 0);
        struct fwlab_c43_queue_facts sq = queue_facts(
            FWLAB_C43_QUEUE_CREATE_SQ, 2, UINT64_C(0x3232),
            UINT64_C(0x3231));
        struct fwlab_c43_queue_facts busy_delete = queue_facts(
            FWLAB_C43_QUEUE_DELETE_SQ, 3, UINT64_C(0x3232),
            UINT64_C(0x3231));
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;
        uint32_t event_delta;

        busy_delete.active_commands_zero = 0;
        CHECK(fixture_init(&fixture, &config));
        CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
        CHECK(scripted_queue_command(
            &fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ, 1, &cq,
            FWLAB_C43_QUEUE_EFFECT_COMMITTED, &observer));
        CHECK(scripted_queue_command(
            &fixture, 3, FWLAB_C43_REQUEST_CREATE_IO_SQ, 2, &sq,
            FWLAB_C43_QUEUE_EFFECT_COMMITTED, &observer));
        c43_fake_queue_script_configure(
            &fixture.services, &busy_delete,
            FWLAB_C43_QUEUE_EFFECT_COMMITTED, 0, 0, 0, 1);
        CHECK(admit_kind(&fixture, 4, FWLAB_C43_REQUEST_DELETE_IO_SQ,
                         &prepared, &admitted));
        CHECK(run_until_action_state(
            &fixture, 3, FWLAB_C43_ACTION_STATE_FINISH_QUERY,
            &observer, 8));
        CHECK(fixture.services.queue_script.first_finish_request.decision ==
                  FWLAB_C43_QUEUE_FINISH_COMMIT &&
              observer.commands[3].resolved_status ==
                  FWLAB_C43_STATUS_SUCCESS &&
              observer.io_sq_present == 1);
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(event_delta == 1 && observer.io_sq_present == 1 &&
              observer.commands[3].action_state ==
                  FWLAB_C43_ACTION_STATE_FINISH_QUERY);
        fixture.services.queue_script.facts.active_commands_zero = 1;
        CHECK(run_until_action_state(
            &fixture, 3, FWLAB_C43_ACTION_STATE_TERMINAL_HELD,
            &observer, 8));
        CHECK(observer.io_sq_present == 0 &&
              observer.commands[3].resolved_status ==
                  FWLAB_C43_STATUS_SUCCESS &&
              observer.commands[3].satisfied_witness_mask ==
                  FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED &&
              observer.commands[3].success_eligible == 1);
    }

    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts cq = queue_facts(
            FWLAB_C43_QUEUE_CREATE_CQ, 1, UINT64_C(0x3251), 0);
        struct fwlab_c43_queue_facts sq = queue_facts(
            FWLAB_C43_QUEUE_CREATE_SQ, 2, UINT64_C(0x3252),
            UINT64_C(0x3251));
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;
        uint32_t event_delta;
        uint32_t events_before;

        CHECK(fixture_init(&fixture, &config));
        CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
        CHECK(scripted_queue_command(
            &fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ, 1, &cq,
            FWLAB_C43_QUEUE_EFFECT_COMMITTED, &observer));
        CHECK(scripted_queue_command(
            &fixture, 3, FWLAB_C43_REQUEST_CREATE_IO_SQ, 2, &sq,
            FWLAB_C43_QUEUE_EFFECT_COMMITTED, &observer));
        events_before = fixture.services.event_count;
        CHECK(admit_kind(&fixture, 4, FWLAB_C43_REQUEST_DELETE_IO_CQ,
                         &prepared, &admitted));
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(event_delta == 0 &&
              fixture.services.event_count == events_before &&
              observer.commands[3].action_state ==
                  FWLAB_C43_ACTION_STATE_REJECTED_NO_EFFECT &&
              observer.commands[3].resolved_status ==
                  FWLAB_C43_STATUS_INVALID_QUEUE_DELETE &&
              observer.io_cq_present == 1 && observer.io_sq_present == 1);
    }

    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts cq = queue_facts(
            FWLAB_C43_QUEUE_CREATE_CQ, 1, UINT64_C(0x3301), 0);
        struct fwlab_c43_queue_facts delete_cq = queue_facts(
            FWLAB_C43_QUEUE_DELETE_CQ, 2, UINT64_C(0x3301), 0);
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;
        uint32_t event_delta;

        CHECK(fixture_init(&fixture, &config));
        CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
        CHECK(scripted_queue_command(
            &fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ, 1, &cq,
            FWLAB_C43_QUEUE_EFFECT_COMMITTED, &observer));
        CHECK(admit_kind(&fixture, 3,
                         FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES,
                         &prepared, &admitted));
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(event_delta == 0 && observer.io_cq_present == 1 &&
              observer.commands[2].resolved_status ==
                  FWLAB_C43_STATUS_COMMAND_SEQUENCE &&
              observer.commands[2].success_eligible == 0);
        CHECK(scripted_queue_command(
            &fixture, 4, FWLAB_C43_REQUEST_DELETE_IO_CQ, 3, &delete_cq,
            FWLAB_C43_QUEUE_EFFECT_COMMITTED, &observer));
        CHECK(observer.io_cq_present == 0 && observer.io_sq_present == 0 &&
              observer.nq_state == FWLAB_C43_NQ_NEGOTIATED);
    }
    return 0;
}

static int setup_live_queues(
    struct fixture *fixture,
    struct fwlab_c43_graph_observer *observer,
    int with_sq,
    uint64_t cq_ref,
    uint64_t sq_ref)
{
    struct fwlab_c43_graph_config config = config_fixed();
    struct fwlab_c43_queue_facts cq = queue_facts(
        FWLAB_C43_QUEUE_CREATE_CQ, 1, cq_ref, 0);
    struct fwlab_c43_queue_facts sq = queue_facts(
        FWLAB_C43_QUEUE_CREATE_SQ, 2, sq_ref, cq_ref);

    return fixture_init(fixture, &config) &&
           bootstrap_nq(fixture, 1, 0, observer) &&
           scripted_queue_command(
               fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ, 1, &cq,
               FWLAB_C43_QUEUE_EFFECT_COMMITTED, observer) &&
           (!with_sq || scripted_queue_command(
               fixture, 3, FWLAB_C43_REQUEST_CREATE_IO_SQ, 2, &sq,
               FWLAB_C43_QUEUE_EFFECT_COMMITTED, observer));
}

static int test_nq_active_transaction_guard(void)
{
    struct fixture fixture;
    struct fwlab_c43_graph_config config = config_fixed();
    struct fwlab_c43_graph_observer observer;
    struct fwlab_c43_queue_facts facts = queue_facts(
        FWLAB_C43_QUEUE_CREATE_CQ, 1, UINT64_C(0x3401), 0);
    struct fwlab_hif_prepare_result prepared;
    struct fwlab_c43_admit_result admitted;
    uint32_t event_delta;

    CHECK(fixture_init(&fixture, &config));
    CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
    c43_fake_queue_script_configure(
        &fixture.services, &facts, FWLAB_C43_QUEUE_EFFECT_COMMITTED,
        0, 0, 0, 0);
    CHECK(admit_kind(&fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ,
                     &prepared, &admitted));
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 0 && observer.queue_txn_active == 1 &&
          observer.queue_owner_slot_plus_one == 2);
    CHECK(admit_kind(&fixture, 3,
                     FWLAB_C43_REQUEST_SET_NUMBER_OF_QUEUES,
                     &prepared, &admitted));
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(event_delta == 0 && observer.queue_txn_active == 1 &&
          observer.queue_owner_slot_plus_one == 2 &&
          observer.commands[2].action_state ==
              FWLAB_C43_ACTION_STATE_TERMINAL_HELD &&
          observer.commands[2].resolved_status ==
              FWLAB_C43_STATUS_COMMAND_SEQUENCE &&
          observer.commands[2].success_eligible == 0 &&
          observer.io_cq_present == 0);
    return 0;
}

static int test_provider_outcomes_and_ownership(void)
{
    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_c43_graph_observer observer;
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;
        struct fwlab_c43_queue_effect_request expected_request;
        struct c43_fake_event_record expected_event = {0};
        uint32_t event_delta;

        CHECK(fixture_init(&fixture, &config));
        CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
        CHECK(admit_kind(&fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ,
                         &prepared, &admitted));
        CHECK(step_once(&fixture, &observer, &event_delta));
        expected_request = expected_prepare_request(
            &admitted.ticket, &observer.commands[1],
            FWLAB_C43_REQUEST_CREATE_IO_CQ);
        CHECK(step_once(&fixture, &observer, &event_delta));
        expected_event.sequence = 1;
        expected_event.kind = C43_FAKE_QUEUE_PREPARE_START;
        expected_event.returned = FWLAB_HIF_ACTION_REJECTED;
        expected_event.input.queue_prepare = expected_request;
        CHECK(event_delta == 1 && fixture.services.event_count == 1 &&
              memcmp(&fixture.services.events[0], &expected_event,
                     sizeof(expected_event)) == 0 &&
              observer.queue_txn_active == 0 &&
              observer.io_cq_present == 0 &&
              observer.commands[1].action_state ==
                  FWLAB_C43_ACTION_STATE_REJECTED_NO_EFFECT &&
              observer.commands[1].resolved_status ==
                  FWLAB_C43_STATUS_RESOURCE_FAILURE &&
              fixture.graph->commands[1].queue_txn.provider_owned == 0);
    }

    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts facts = queue_facts(
            FWLAB_C43_QUEUE_CREATE_CQ, 1, UINT64_C(0x3501), 0);
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;
        uint32_t event_delta;

        CHECK(fixture_init(&fixture, &config));
        CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
        c43_fake_queue_script_configure(
            &fixture.services, &facts, FWLAB_C43_QUEUE_EFFECT_COMMITTED,
            0, 0, 0, 0);
        fixture.services.queue_script.corrupt_prepare_submit_token = 1;
        CHECK(admit_kind(&fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ,
                         &prepared, &admitted));
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(event_delta == 1 && fixture.services.event_count == 1 &&
              fixture.services.events[0].returned ==
                  FWLAB_HIF_ACTION_ACCEPTED &&
              fixture.services.events[0].output_written == 1 &&
              fixture.services.events[0].output.submit.token.action_uid !=
                  fixture.services.events[0].input.queue_prepare.common.token
                      .action_uid &&
              observer.commands[1].action_state ==
                  FWLAB_C43_ACTION_STATE_FAULT &&
              observer.queue_txn_active == 1 &&
              fixture.graph->commands[1].queue_txn.provider_owned == 1 &&
              fixture.graph->commands[1].queue_txn.fault_from_flow ==
                  C43_QUEUE_FLOW_PREPARE_START &&
              c43_graph_valid(fixture.graph));
    }

    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts facts = queue_facts(
            FWLAB_C43_QUEUE_CREATE_CQ, 1, UINT64_C(0x3502), 0);
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;
        uint32_t event_delta;

        CHECK(fixture_init(&fixture, &config));
        CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
        c43_fake_queue_script_configure(
            &fixture.services, &facts, FWLAB_C43_QUEUE_EFFECT_COMMITTED,
            0, 0, 0, 0);
        CHECK(admit_kind(&fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ,
                         &prepared, &admitted));
        CHECK(run_until_action_state(
            &fixture, 1, FWLAB_C43_ACTION_STATE_FINISH_READY,
            &observer, 8));
        fixture.services.queue_script.fault = 1;
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(event_delta == 1 &&
              fixture.services.events[2].kind ==
                  C43_FAKE_QUEUE_FINISH_START &&
              fixture.services.events[2].returned ==
                  FWLAB_HIF_ACTION_REJECTED &&
              fixture.services.events[2].output_written == 0 &&
              memcmp(&fixture.services.events[2].input.queue_finish,
                     &fixture.services.queue_script.first_finish_request,
                     sizeof(fixture.services.queue_script
                                .first_finish_request)) == 0 &&
              observer.commands[1].action_state ==
                  FWLAB_C43_ACTION_STATE_FAULT &&
              observer.queue_txn_active == 1 &&
              fixture.graph->commands[1].queue_txn.provider_owned == 1);
    }
    return 0;
}

static int test_finish_exactness_fail_closed(void)
{
    uint32_t mutation;

    for (mutation = 0; mutation < 5; ++mutation) {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts facts = queue_facts(
            FWLAB_C43_QUEUE_CREATE_CQ, 1,
            UINT64_C(0x3600) + mutation, 0);
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;
        uint32_t event_delta;

        CHECK(fixture_init(&fixture, &config));
        CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
        c43_fake_queue_script_configure(
            &fixture.services, &facts, FWLAB_C43_QUEUE_EFFECT_COMMITTED,
            0, 0, 0, 0);
        CHECK(admit_kind(&fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ,
                         &prepared, &admitted));
        CHECK(run_until_action_state(
            &fixture, 1, FWLAB_C43_ACTION_STATE_FINISH_QUERY,
            &observer, 8));
        switch (mutation) {
        case 0:
            fixture.services.queue_script.facts.queue.word[0] ^=
                UINT64_C(0x100000);
            break;
        case 1:
            fixture.services.queue_script.first_prepare_request.common.cookie ^=
                UINT64_C(1);
            break;
        case 2:
            fixture.services.queue_script.facts.transaction.word[0] ^=
                UINT64_C(1);
            break;
        case 3:
            fixture.services.queue_script.facts.operation =
                FWLAB_C43_QUEUE_CREATE_SQ;
            fixture.services.queue_script.facts.role =
                FWLAB_C43_QUEUE_ROLE_IO_SQ;
            break;
        default:
            fixture.services.queue_script.first_finish_request.token
                .action_uid ^= UINT64_C(1);
            break;
        }
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(event_delta == 1 && observer.io_cq_present == 0 &&
              observer.queue_txn_active == 1 &&
              observer.commands[1].action_state ==
                  FWLAB_C43_ACTION_STATE_FAULT &&
              observer.commands[1].resolved_status ==
                  FWLAB_C43_STATUS_INTERNAL_FAILURE &&
              fixture.graph->commands[1].queue_txn.provider_owned == 1);
    }

    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts facts = queue_facts(
            FWLAB_C43_QUEUE_CREATE_CQ, 1, UINT64_C(0x36ff), 0);
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;
        uint32_t event_delta;

        CHECK(fixture_init(&fixture, &config));
        CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
        c43_fake_queue_script_configure(
            &fixture.services, &facts, FWLAB_C43_QUEUE_EFFECT_COMMITTED,
            0, 0, 0, 0);
        CHECK(admit_kind(&fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ,
                         &prepared, &admitted));
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(observer.commands[1].action_state ==
                  FWLAB_C43_ACTION_STATE_PREPARE_QUERY);
        fixture.services.queue_script.first_prepare_request.common.token
            .action_uid ^= UINT64_C(1);
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(event_delta == 1 &&
              fixture.services.events[1].returned == FWLAB_C43_API_STALE &&
              observer.commands[1].action_state ==
                  FWLAB_C43_ACTION_STATE_FAULT &&
              observer.queue_txn_active == 1);
    }
    return 0;
}

static int test_delete_barrier_matrix(void)
{
    uint32_t barrier;

    for (barrier = 0; barrier < 3; ++barrier) {
        struct fixture fixture;
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts facts = queue_facts(
            FWLAB_C43_QUEUE_DELETE_SQ, 3, UINT64_C(0x3702),
            UINT64_C(0x3701));
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;
        uint32_t event_delta;

        if (barrier == 0) {
            facts.active_commands_zero = 0;
        } else if (barrier == 1) {
            facts.target_refs_zero = 0;
        } else {
            facts.reserved_publications_zero = 0;
        }
        CHECK(setup_live_queues(
            &fixture, &observer, 1, UINT64_C(0x3701),
            UINT64_C(0x3702)));
        c43_fake_queue_script_configure(
            &fixture.services, &facts, FWLAB_C43_QUEUE_EFFECT_COMMITTED,
            0, 0, 0, 0);
        CHECK(admit_kind(&fixture, 4, FWLAB_C43_REQUEST_DELETE_IO_SQ,
                         &prepared, &admitted));
        CHECK(run_until_action_state(
            &fixture, 3, FWLAB_C43_ACTION_STATE_APPLY_COMMIT,
            &observer, 8));
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(event_delta == 0 && observer.io_sq_present == 1 &&
              observer.commands[3].action_state ==
                  FWLAB_C43_ACTION_STATE_FAULT &&
              observer.commands[3].resolved_status ==
                  FWLAB_C43_STATUS_INTERNAL_FAILURE);
    }

    for (barrier = 0; barrier < 2; ++barrier) {
        struct fixture fixture;
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts facts = queue_facts(
            FWLAB_C43_QUEUE_DELETE_CQ, 2, UINT64_C(0x3801), 0);
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;
        uint32_t event_delta;

        if (barrier == 0) {
            facts.reserved_publications_zero = 0;
        } else {
            facts.unacked_completions_zero = 0;
        }
        CHECK(setup_live_queues(
            &fixture, &observer, 0, UINT64_C(0x3801), 0));
        c43_fake_queue_script_configure(
            &fixture.services, &facts, FWLAB_C43_QUEUE_EFFECT_COMMITTED,
            0, 0, 0, 0);
        CHECK(admit_kind(&fixture, 3, FWLAB_C43_REQUEST_DELETE_IO_CQ,
                         &prepared, &admitted));
        CHECK(run_until_action_state(
            &fixture, 2, FWLAB_C43_ACTION_STATE_APPLY_COMMIT,
            &observer, 8));
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(event_delta == 0 && observer.io_cq_present == 1 &&
              observer.commands[2].action_state ==
                  FWLAB_C43_ACTION_STATE_FAULT &&
              observer.commands[2].resolved_status ==
                  FWLAB_C43_STATUS_INTERNAL_FAILURE);
    }
    return 0;
}

static int test_queue_state_validator_negatives(void)
{
    struct fixture fixture;
    struct fwlab_c43_graph_config config = config_fixed();
    struct fwlab_c43_graph_observer observer;
    struct fwlab_c43_queue_facts facts = queue_facts(
        FWLAB_C43_QUEUE_CREATE_CQ, 1, UINT64_C(0x3901), 0);
    struct fwlab_hif_prepare_result prepared;
    struct fwlab_c43_admit_result admitted;
    uint32_t event_delta;

    CHECK(fixture_init(&fixture, &config));
    CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
    c43_fake_queue_script_configure(
        &fixture.services, &facts, FWLAB_C43_QUEUE_EFFECT_COMMITTED,
        0, 0, 0, 0);
    CHECK(admit_kind(&fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ,
                     &prepared, &admitted));
    CHECK(step_once(&fixture, &observer, &event_delta));
    CHECK(c43_graph_valid(fixture.graph) &&
          fixture.graph->commands[1].queue_txn.flow ==
              C43_QUEUE_FLOW_PREPARE_START);

    fixture.graph->queue_authority.txn_active = 0;
    fixture.graph->queue_authority.owner_slot_plus_one = 0;
    fixture.graph->observer.queue_txn_active = 0;
    fixture.graph->observer.queue_owner_slot_plus_one = 0;
    CHECK(!c43_graph_valid(fixture.graph));
    fixture.graph->queue_authority.txn_active = 1;
    fixture.graph->queue_authority.owner_slot_plus_one = 2;
    fixture.graph->observer.queue_txn_active = 1;
    fixture.graph->observer.queue_owner_slot_plus_one = 2;
    CHECK(c43_graph_valid(fixture.graph));

    fixture.graph->commands[1].queue_txn.local_effect_applied = 1;
    CHECK(!c43_graph_valid(fixture.graph));
    fixture.graph->commands[1].queue_txn.local_effect_applied = 0;
    CHECK(c43_graph_valid(fixture.graph));

    fixture.graph->observer.commands[1].terminal_winner =
        FWLAB_C43_WINNER_NORMAL;
    CHECK(!c43_graph_valid(fixture.graph));
    fixture.graph->observer.commands[1].terminal_winner =
        FWLAB_C43_WINNER_NONE;
    fixture.graph->observer.commands[1].phase = FWLAB_C43_PHASE_INTENT_READY;
    fixture.graph->observer.ready_count = 1;
    CHECK(!c43_graph_valid(fixture.graph));
    return 0;
}

static int test_queue_fault_and_too_late(void)
{
    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts facts = queue_facts(
            FWLAB_C43_QUEUE_CREATE_CQ, 1, UINT64_C(0x4101), 0);
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;
        uint32_t event_delta;
        uint32_t events_before;

        CHECK(fixture_init(&fixture, &config));
        CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
        c43_fake_queue_script_configure(
            &fixture.services, &facts, FWLAB_C43_QUEUE_EFFECT_COMMITTED,
            0, 0, 0, 0);
        CHECK(admit_kind(&fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ,
                         &prepared, &admitted));
        CHECK(run_until_action_state(
            &fixture, 1, FWLAB_C43_ACTION_STATE_FINISH_QUERY,
            &observer, 8));
        fixture.services.queue_script.facts.queue.word[0] =
            UINT64_C(0x9999);
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(observer.queue_txn_active == 1 &&
              observer.io_cq_present == 0 &&
              observer.commands[1].action_state ==
                  FWLAB_C43_ACTION_STATE_FAULT &&
              observer.commands[1].resolved_status ==
                  FWLAB_C43_STATUS_INTERNAL_FAILURE &&
              observer.commands[1].success_eligible == 0);
        events_before = fixture.services.event_count;
        CHECK(step_once(&fixture, &observer, &event_delta));
        CHECK(event_delta == 0 &&
              fixture.services.event_count == events_before &&
              observer.commands[1].action_state ==
                  FWLAB_C43_ACTION_STATE_FAULT);
    }

    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts facts = queue_facts(
            FWLAB_C43_QUEUE_CREATE_CQ, 1, UINT64_C(0x4201), 0);
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;

        CHECK(fixture_init(&fixture, &config));
        CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
        c43_fake_queue_script_configure(
            &fixture.services, &facts, FWLAB_C43_QUEUE_EFFECT_POISONED,
            0, 0, 0, 0);
        CHECK(admit_kind(&fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ,
                         &prepared, &admitted));
        CHECK(run_until_action_state(
            &fixture, 1, FWLAB_C43_ACTION_STATE_FAULT, &observer, 12));
        CHECK(observer.queue_txn_active == 1 &&
              observer.io_cq_present == 0 &&
              observer.commands[1].resolved_status ==
                  FWLAB_C43_STATUS_INTERNAL_FAILURE &&
              observer.commands[1].satisfied_witness_mask == 0 &&
              observer.commands[1].success_eligible == 0);
    }

    {
        struct fixture fixture;
        struct fwlab_c43_graph_config config = config_fixed();
        struct fwlab_c43_graph_observer observer;
        struct fwlab_c43_queue_facts cq = queue_facts(
            FWLAB_C43_QUEUE_CREATE_CQ, 1, UINT64_C(0x4301), 0);
        struct fwlab_c43_queue_facts sq = queue_facts(
            FWLAB_C43_QUEUE_CREATE_SQ, 2, UINT64_C(0x4302),
            UINT64_C(0x4301));
        struct fwlab_hif_prepare_result prepared;
        struct fwlab_c43_admit_result admitted;

        CHECK(fixture_init(&fixture, &config));
        CHECK(bootstrap_nq(&fixture, 1, 0, &observer));
        CHECK(scripted_queue_command(
            &fixture, 2, FWLAB_C43_REQUEST_CREATE_IO_CQ, 1, &cq,
            FWLAB_C43_QUEUE_EFFECT_COMMITTED, &observer));
        sq.association_matches = 0;
        c43_fake_queue_script_configure(
            &fixture.services, &sq, FWLAB_C43_QUEUE_EFFECT_TOO_LATE,
            0, 0, 0, 0);
        CHECK(admit_kind(&fixture, 3, FWLAB_C43_REQUEST_CREATE_IO_SQ,
                         &prepared, &admitted));
        CHECK(run_until_action_state(
            &fixture, 2, FWLAB_C43_ACTION_STATE_FINISH_QUERY,
            &observer, 12));
        fixture.services.queue_script.facts.association_matches = 1;
        CHECK(run_until_action_state(
            &fixture, 2, FWLAB_C43_ACTION_STATE_TERMINAL_HELD,
            &observer, 8));
        CHECK(observer.io_sq_present == 1 &&
              observer.io_sq.word[0] == UINT64_C(0x4302) &&
              observer.commands[2].resolved_status ==
                  FWLAB_C43_STATUS_SUCCESS &&
              observer.commands[2].satisfied_witness_mask ==
                  FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED &&
              observer.commands[2].success_eligible == 1);
    }
    return 0;
}

int main(void)
{
    CHECK(test_capacity_query_and_atomicity() == 0);
    CHECK(test_counter_exhaustion() == 0);
    CHECK(test_counter_endpoints_and_domains() == 0);
    CHECK(test_admission_identity_and_policy() == 0);
    CHECK(test_cross_instance_admit_stale() == 0);
    CHECK(test_nq_and_create_cq_2pc() == 0);
    CHECK(test_queue_order_abort_and_delete() == 0);
    CHECK(test_nq_active_transaction_guard() == 0);
    CHECK(test_provider_outcomes_and_ownership() == 0);
    CHECK(test_finish_exactness_fail_closed() == 0);
    CHECK(test_delete_barrier_matrix() == 0);
    CHECK(test_queue_state_validator_negatives() == 0);
    CHECK(test_queue_fault_and_too_late() == 0);
    puts("C4.3 phase4 queue graph: PASS commands=4 actions=32 counters=6");
    return 0;
}
