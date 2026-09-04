/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_fake_services.h"

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
    step_before = step;
    CHECK(fwlab_c43_graph_step(fixture.graph, 1, &step) ==
          FWLAB_C43_GRAPH_NOT_IMPLEMENTED);
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

int main(void)
{
    CHECK(test_capacity_query_and_atomicity() == 0);
    CHECK(test_counter_exhaustion() == 0);
    CHECK(test_counter_endpoints_and_domains() == 0);
    CHECK(test_admission_identity_and_policy() == 0);
    CHECK(test_cross_instance_admit_stale() == 0);
    puts("C4.3 phase3 prepare/admit: PASS commands=4 actions=32 counters=6");
    return 0;
}
