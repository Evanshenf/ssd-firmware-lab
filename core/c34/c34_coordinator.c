/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_internal.h"

#include <string.h>

static int pair_equal(const uint64_t left[2], const uint64_t right[2])
{
    return left[0] == right[0] && left[1] == right[1];
}

static int pair_zero(const uint64_t words[2])
{
    return words[0] == 0 && words[1] == 0;
}

static int bytes_zero(const uint8_t *bytes, size_t length)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        if (bytes[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int request_valid(const struct c34 *instance,
                         const struct c34_request *request)
{
    unsigned int atom;

    if (request == NULL || request->version != C34_CONTRACT_VERSION ||
        request->size != sizeof(*request) || request->kind > C34_REQUEST_FENCE ||
        request->owner_epoch != instance->current_epoch ||
        request->reserved0[0] != 0 || request->reserved0[1] != 0) {
        return 0;
    }
    if (request->kind == C34_REQUEST_READ) {
        return request->durability_kind == FWLAB_PERSIST_DEFAULT &&
               request->atom_mask == 0 && request->atom < C34_ATOMS &&
               request->sequence == 0 && request->frontier == 0 &&
               bytes_zero(&request->payload[0][0], sizeof(request->payload));
    }
    if (request->kind == C34_REQUEST_FENCE) {
        return request->durability_kind == FWLAB_PERSIST_FENCE &&
               request->atom_mask == 0 && request->sequence == 0 &&
               request->frontier != 0 &&
               bytes_zero(&request->payload[0][0], sizeof(request->payload));
    }
    if ((request->durability_kind != FWLAB_PERSIST_DEFAULT &&
         request->durability_kind != FWLAB_PERSIST_SELF_DURABLE) ||
        request->atom_mask == 0 ||
        (request->atom_mask & ~UINT8_C(0x03)) != 0 ||
        request->sequence == 0 || request->frontier != 0) {
        return 0;
    }
    for (atom = 0; atom < C34_ATOMS; ++atom) {
        if ((request->atom_mask & (uint8_t)(1u << atom)) == 0 &&
            !bytes_zero(request->payload[atom], C34_ATOM_BYTES)) {
            return 0;
        }
        if (request->kind == C34_REQUEST_TRIM &&
            !bytes_zero(request->payload[atom], C34_ATOM_BYTES)) {
            return 0;
        }
    }
    return 1;
}

static struct c34_registry_entry *registry_find(
    struct c34 *instance,
    const struct fwlab_c31_request_token *token
)
{
    unsigned int index;

    for (index = 0; index < C34_REQUEST_SLOTS; ++index) {
        if (instance->registry[index].used &&
            pair_equal(instance->registry[index].token.word, token->word)) {
            return &instance->registry[index];
        }
    }
    return NULL;
}

enum c34_result c34_request_register(
    struct c34 *instance,
    const struct fwlab_c31_request_token *token,
    const struct c34_request *request
)
{
    unsigned int index;

    if (!c34_instance_valid(instance) || token == NULL ||
        pair_zero(token->word) || !request_valid(instance, request)) {
        return C34_INVALID_CONTRACT;
    }
    if (registry_find(instance, token) != NULL) {
        return C34_WRONG_STATE;
    }
    for (index = 0; index < C34_REQUEST_SLOTS; ++index) {
        if (!instance->registry[index].used) {
            instance->registry[index].used = 1;
            instance->registry[index].token = *token;
            instance->registry[index].request = *request;
            return C34_OK;
        }
    }
    return C34_NO_CAPACITY;
}

static int free_sidecar(const struct c34 *instance)
{
    unsigned int index;

    for (index = 0; index < C34_SIDECAR_SLOTS; ++index) {
        if (!instance->sidecar[index].used) {
            return (int)index;
        }
    }
    return -1;
}

static int free_obligation(const struct c34 *instance)
{
    unsigned int index;

    for (index = 0; index < C34_OBLIGATION_SLOTS; ++index) {
        if (!instance->obligations[index].used) {
            return (int)index;
        }
    }
    return -1;
}

static unsigned int bit_count(uint8_t mask)
{
    return (unsigned int)((mask & 1u) != 0) +
           (unsigned int)((mask & 2u) != 0);
}

static unsigned int free_data_pages(const struct c34 *instance)
{
    unsigned int count = 0;
    unsigned int index;

    for (index = 0; index < C34_DATA_PAGES; ++index) {
        count += instance->p2l[index].kind == C34_P2L_FREE;
    }
    return count;
}

static int overlay_present(const struct c34 *instance)
{
    return instance->overlay_valid[0] || instance->overlay_valid[1];
}

static void mutation_token(
    const struct c34 *instance,
    const struct fwlab_c31_operation_token *outer,
    const struct c34_request *request,
    struct fwlab_persist_mutation_token *token
)
{
    token->word[0] = instance->config.instance_nonce ^
                     outer->command.command_uid ^ request->sequence ^
                     UINT64_C(0x9e3779b97f4a7c15);
    token->word[1] = ((uint64_t)request->owner_epoch << 32) ^
                     request->scope ^ outer->cookie ^
                     UINT64_C(0xd1b54a32d192ed03);
    if (pair_zero(token->word)) {
        token->word[1] = 1;
    }
}

static size_t collect_facts(
    const struct c34_graph *graph,
    struct fwlab_persist_atom_fact facts[C34_ATOMS]
)
{
    size_t count = 0;
    unsigned int atom;

    for (atom = 0; atom < C34_ATOMS; ++atom) {
        if (graph->mutation[atom].used) {
            facts[count++] = graph->mutation[atom].fact;
        }
    }
    return count;
}

static enum c34_result evaluate_command_witness(struct c34 *instance)
{
    struct fwlab_persist_atom_fact facts[C34_ATOMS];
    size_t count = collect_facts(&instance->graph, facts);

    if (fwlab_persist_command_witness(
            &instance->config.persistence,
            &instance->graph.persist_request, facts, count, NULL, 0,
            &instance->graph.witness) != FWLAB_PERSIST_OK) {
        return C34_INVARIANT_FAILURE;
    }
    return C34_OK;
}

static void reserve_sidecar(struct c34 *instance, int index)
{
    memset(&instance->sidecar[index], 0, sizeof(instance->sidecar[index]));
    instance->sidecar[index].used = 1;
    instance->graph.sidecar_index = (uint8_t)index;
}

static enum c34_result accept_read(
    struct c34 *instance,
    const struct c34_request *request
)
{
    uint8_t atom = request->atom;

    instance->graph.active_atom = atom;
    if (instance->overlay_valid[atom]) {
        if (instance->overlay_kind[atom] == C34_LOGICAL_VALUE) {
            instance->graph.result_present_mask = (uint8_t)(1u << atom);
            memcpy(instance->graph.main, instance->overlay_payload[atom],
                   C34_ATOM_BYTES);
        }
        instance->graph.terminal = FWLAB_C31_PROVIDER_SUCCESS;
        instance->graph.outer_event_ready = 1;
        instance->graph.state = C34_GRAPH_DONE;
        return C34_OK;
    }
    if (instance->l2p[atom].kind != C34_LOGICAL_VALUE) {
        instance->graph.terminal = FWLAB_C31_PROVIDER_SUCCESS;
        instance->graph.outer_event_ready = 1;
        instance->graph.state = C34_GRAPH_DONE;
        return C34_OK;
    }
    instance->graph.victim = instance->l2p[atom].data_ppa;
    instance->graph.state = C34_GRAPH_PREPARE_READ_TRIGGER;
    return C34_OK;
}

static enum c34_result accept_fence(
    struct c34 *instance,
    const struct c34_request *request
)
{
    struct fwlab_persist_obligation obligations[C34_OBLIGATION_SLOTS];
    struct fwlab_persist_request fence;
    size_t count = 0;
    unsigned int index;

    memset(&fence, 0, sizeof(fence));
    fence.version = FWLAB_PERSIST_VERSION;
    fence.size = sizeof(fence);
    fence.kind = FWLAB_PERSIST_FENCE;
    fence.owner_epoch = request->owner_epoch;
    fence.scope = request->scope;
    fence.frontier = request->frontier;
    for (index = 0; index < C34_OBLIGATION_SLOTS; ++index) {
        if (instance->obligations[index].used) {
            obligations[count++] = instance->obligations[index].obligation;
        }
    }
    if (fwlab_persist_fence_witness(
            &instance->config.persistence, &fence, obligations, count, NULL,
            0, &instance->graph.witness) != FWLAB_PERSIST_OK) {
        return C34_INVARIANT_FAILURE;
    }
    instance->graph.terminal = instance->graph.witness.witness_class ==
            FWLAB_PERSIST_DURABLE_ELIGIBLE ?
        FWLAB_C31_PROVIDER_SUCCESS : FWLAB_C31_PROVIDER_FAILED;
    instance->graph.maintenance_result =
        instance->graph.terminal == FWLAB_C31_PROVIDER_SUCCESS ?
            C34_COMMAND_SUCCESS : C34_COMMAND_INDETERMINATE;
    instance->graph.failure_effect = FWLAB_C31_EFFECT_NONE;
    instance->graph.outer_event_ready = 1;
    instance->graph.state = instance->graph.terminal ==
            FWLAB_C31_PROVIDER_SUCCESS ?
        C34_GRAPH_DONE : C34_GRAPH_FAILED;
    return C34_OK;
}

static enum c34_result accept_mutation(
    struct c34 *instance,
    const struct c34_request *request
)
{
    struct fwlab_persist_mutation_token token;
    struct c34_obligation_entry *obligation;
    unsigned int atoms = bit_count(request->atom_mask);
    unsigned int atom;
    int obligation_index = free_obligation(instance);
    enum c34_result result;

    if (obligation_index < 0 || request->sequence <= instance->last_sequence ||
        overlay_present(instance) ||
        instance->blocks[C34_JOURNAL_BLOCK].next_program_page + atoms >
            C34_PAGES_PER_BLOCK ||
        (request->kind == C34_REQUEST_WRITE &&
         free_data_pages(instance) < atoms) ||
        instance->next_logical_state_id + atoms - 1u > C34_RECORD_LIMIT ||
        instance->next_record_id +
                (request->kind == C34_REQUEST_WRITE ? atoms * 2u : atoms) -
                1u >
            C34_RECORD_LIMIT ||
        instance->next_commit_sequence +
                (request->kind == C34_REQUEST_WRITE ? atoms * 2u : atoms) -
                1u >
            C34_RECORD_LIMIT) {
        return C34_NO_CAPACITY;
    }
    mutation_token(instance, &instance->graph.outer, request, &token);
    memset(&instance->graph.persist_request, 0,
           sizeof(instance->graph.persist_request));
    instance->graph.persist_request.version = FWLAB_PERSIST_VERSION;
    instance->graph.persist_request.size =
        sizeof(instance->graph.persist_request);
    instance->graph.persist_request.kind = request->durability_kind;
    instance->graph.persist_request.atom_mask = request->atom_mask;
    instance->graph.persist_request.token = token;
    instance->graph.persist_request.owner_epoch = request->owner_epoch;
    instance->graph.persist_request.scope = request->scope;
    instance->graph.persist_request.sequence = request->sequence;

    for (atom = 0; atom < C34_ATOMS; ++atom) {
        struct c34_mutation *mutation = &instance->graph.mutation[atom];

        if ((request->atom_mask & (uint8_t)(1u << atom)) == 0) {
            continue;
        }
        mutation->used = 1;
        mutation->atom = (uint8_t)atom;
        mutation->target_version =
            (uint8_t)(instance->l2p[atom].version + 1u);
        mutation->predecessor_version = instance->l2p[atom].version;
        mutation->logical_state_id = instance->next_logical_state_id++;
        mutation->predecessor_state_id =
            instance->l2p[atom].logical_state_id;
        mutation->mutation_id = request->sequence * C34_ATOMS + atom;
        memcpy(mutation->payload, request->payload[atom], C34_ATOM_BYTES);
        memset(&mutation->fact, 0, sizeof(mutation->fact));
        mutation->fact.version = FWLAB_PERSIST_VERSION;
        mutation->fact.size = sizeof(mutation->fact);
        mutation->fact.token = token;
        mutation->fact.owner_epoch = request->owner_epoch;
        mutation->fact.scope = request->scope;
        mutation->fact.sequence = request->sequence;
        mutation->fact.fact_mask = FWLAB_PERSIST_FACT_CAPTURED;
        mutation->fact.atom = (uint8_t)atom;
        mutation->fact.logical_version = mutation->target_version;
        mutation->fact.predecessor_version = mutation->predecessor_version;
        mutation->fact.mutation_kind = request->kind == C34_REQUEST_WRITE ?
            FWLAB_PERSIST_WRITE : FWLAB_PERSIST_TRIM;
        mutation->fact.closure = FWLAB_PERSIST_CLOSE_OPEN;
    }
    obligation = &instance->obligations[obligation_index];
    memset(obligation, 0, sizeof(*obligation));
    obligation->used = 1;
    obligation->command = instance->graph.outer.command;
    obligation->obligation.version = FWLAB_PERSIST_VERSION;
    obligation->obligation.size = sizeof(obligation->obligation);
    obligation->obligation.token = token;
    obligation->obligation.owner_epoch = request->owner_epoch;
    obligation->obligation.scope = request->scope;
    obligation->obligation.sequence = request->sequence;
    obligation->obligation.fact_mask = FWLAB_PERSIST_FACT_CAPTURED;
    obligation->obligation.atom_mask = request->atom_mask;
    obligation->obligation.closure = FWLAB_PERSIST_CLOSE_OPEN;
    instance->graph.obligation_index = (uint8_t)obligation_index;
    instance->last_sequence = request->sequence;
    instance->graph.active_atom = request->atom_mask & 1u ? 0 : 1;
    if (request->kind == C34_REQUEST_WRITE) {
        instance->graph.state = C34_GRAPH_PREPARE_DATA_TRANSFER;
    } else {
        instance->graph.state = C34_GRAPH_PREPARE_META_TRANSFER;
    }
    result = evaluate_command_witness(instance);
    if (result != C34_OK) {
        return result;
    }
    if (instance->graph.witness.witness_class ==
        FWLAB_PERSIST_VOLATILE_ELIGIBLE) {
        for (atom = 0; atom < C34_ATOMS; ++atom) {
            if ((request->atom_mask & (uint8_t)(1u << atom)) == 0) {
                continue;
            }
            instance->overlay_valid[atom] = 1;
            instance->overlay_kind[atom] =
                request->kind == C34_REQUEST_WRITE ?
                    C34_LOGICAL_VALUE : C34_LOGICAL_TOMBSTONE;
            memcpy(instance->overlay_payload[atom], request->payload[atom],
                   C34_ATOM_BYTES);
        }
        obligation->externally_volatile = 1;
        instance->graph.terminal = FWLAB_C31_PROVIDER_SUCCESS;
        instance->graph.maintenance_result = C34_COMMAND_SUCCESS;
        instance->graph.outer_event_ready = 1;
    }
    return C34_OK;
}

enum c34_result c34_accept_outer(
    struct c34 *instance,
    const struct fwlab_c31_provider_request *outer,
    const struct c34_request *request
)
{
    int sidecar_index;
    enum c34_result result;
    uint32_t saved_next_state;
    uint32_t saved_last_sequence;
    struct c34_obligation_entry saved_obligations[C34_OBLIGATION_SLOTS];
    uint8_t saved_overlay_valid[C34_ATOMS];
    uint8_t saved_overlay_kind[C34_ATOMS];
    uint8_t saved_overlay_payload[C34_ATOMS][C34_ATOM_BYTES];

    if (!c34_instance_valid(instance) || outer == NULL ||
        !request_valid(instance, request) || instance->phase != 0 ||
        instance->graph.kind != C34_GRAPH_NONE ||
        outer->operation.command.instance_nonce !=
            instance->config.instance_nonce ||
        outer->operation.command.controller_epoch != instance->current_epoch) {
        return C34_WRONG_STATE;
    }
    sidecar_index = free_sidecar(instance);
    if (sidecar_index < 0) {
        return C34_NO_CAPACITY;
    }
    saved_next_state = instance->next_logical_state_id;
    saved_last_sequence = instance->last_sequence;
    memcpy(saved_obligations, instance->obligations,
           sizeof(saved_obligations));
    memcpy(saved_overlay_valid, instance->overlay_valid,
           sizeof(saved_overlay_valid));
    memcpy(saved_overlay_kind, instance->overlay_kind,
           sizeof(saved_overlay_kind));
    memcpy(saved_overlay_payload, instance->overlay_payload,
           sizeof(saved_overlay_payload));
    memset(&instance->graph, 0, sizeof(instance->graph));
    instance->graph.kind = C34_GRAPH_OUTER;
    instance->graph.state = C34_GRAPH_CAPTURED;
    instance->graph.outer = outer->operation;
    instance->graph.request = *request;
    reserve_sidecar(instance, sidecar_index);
    if (request->kind == C34_REQUEST_READ) {
        result = accept_read(instance, request);
    } else if (request->kind == C34_REQUEST_FENCE) {
        result = accept_fence(instance, request);
    } else {
        result = accept_mutation(instance, request);
    }
    if (result != C34_OK) {
        instance->next_logical_state_id = saved_next_state;
        instance->last_sequence = saved_last_sequence;
        memcpy(instance->obligations, saved_obligations,
               sizeof(saved_obligations));
        memcpy(instance->overlay_valid, saved_overlay_valid,
               sizeof(saved_overlay_valid));
        memcpy(instance->overlay_kind, saved_overlay_kind,
               sizeof(saved_overlay_kind));
        memcpy(instance->overlay_payload, saved_overlay_payload,
               sizeof(saved_overlay_payload));
        memset(&instance->sidecar[sidecar_index], 0,
               sizeof(instance->sidecar[sidecar_index]));
        memset(&instance->graph, 0, sizeof(instance->graph));
    }
    return result;
}

enum c34_result c34_cancel_outer(
    struct c34 *instance,
    const struct fwlab_c31_operation_token *outer
)
{
    if (!c34_instance_valid(instance) || outer == NULL ||
        instance->graph.kind != C34_GRAPH_OUTER ||
        !c34_outer_equal(&instance->graph.outer, outer)) {
        return C34_NOT_FOUND;
    }
    if (instance->graph.outer_event_sent) {
        return C34_WRONG_STATE;
    }
    instance->graph.cancel_requested = 1;
    if (instance->graph.inner_pending) {
        return instance->nfc.ops->cancel(
                   instance->nfc.context, &instance->graph.inner) ==
                       FWLAB_NFC_API_OK ?
                   C34_OK : C34_INVARIANT_FAILURE;
    }
    c34_graph_fail(instance, C34_COMMAND_CANCELLED,
                   FWLAB_C31_EFFECT_NONE);
    return C34_OK;
}

void c34_graph_fail(
    struct c34 *instance,
    enum c34_command_status status,
    uint8_t effect
)
{
    struct c34_graph *graph = &instance->graph;

    graph->maintenance_result = (uint8_t)status;
    graph->failure_effect = effect;
    graph->state = C34_GRAPH_FAILED;
    if (graph->kind == C34_GRAPH_OUTER) {
        if (graph->request.kind == C34_REQUEST_WRITE ||
            graph->request.kind == C34_REQUEST_TRIM) {
            struct c34_obligation_entry *entry =
                &instance->obligations[graph->obligation_index];
            unsigned int atom;
            int partial = 0;

            for (atom = 0; atom < C34_ATOMS; ++atom) {
                partial |= graph->mutation[atom].used &&
                    (graph->mutation[atom].fact.fact_mask &
                     FWLAB_PERSIST_FACT_C_MAP) != 0;
            }
            if (graph->outer_event_sent || partial) {
                entry->obligation.fact_mask |=
                    FWLAB_PERSIST_FACT_INDETERMINATE;
                entry->obligation.closure =
                    FWLAB_PERSIST_CLOSE_INDETERMINATE;
            } else {
                entry->obligation.fact_mask |=
                    FWLAB_PERSIST_FACT_PROVABLE_NO_COMMIT;
                entry->obligation.closure =
                    FWLAB_PERSIST_CLOSE_NO_COMMIT;
            }
        }
        if (!graph->outer_event_sent) {
            graph->terminal = status == C34_COMMAND_CANCELLED ?
                FWLAB_C31_PROVIDER_CANCELLED : FWLAB_C31_PROVIDER_FAILED;
            graph->outer_event_ready = 1;
        }
    }
}

enum c34_result c34_result_read(
    const struct c34 *instance,
    const struct fwlab_c31_command_handle *command,
    struct c34_command_result *result
)
{
    unsigned int index;

    if (!c34_instance_valid(instance) || command == NULL || result == NULL) {
        return C34_INVALID_CONTRACT;
    }
    for (index = 0; index < C34_SIDECAR_SLOTS; ++index) {
        if (instance->sidecar[index].used && instance->sidecar[index].ready &&
            instance->sidecar[index].result.outer.command.instance_nonce ==
                command->instance_nonce &&
            instance->sidecar[index].result.outer.command.command_uid ==
                command->command_uid &&
            instance->sidecar[index].result.outer.command.controller_epoch ==
                command->controller_epoch &&
            instance->sidecar[index].result.outer.command.slot ==
                command->slot &&
            instance->sidecar[index].result.outer.command.slot_generation ==
                command->slot_generation) {
            *result = instance->sidecar[index].result;
            return C34_OK;
        }
    }
    return C34_NOT_FOUND;
}

enum c34_result c34_result_ack(
    struct c34 *instance,
    const struct fwlab_c31_command_handle *command
)
{
    unsigned int index;

    if (!c34_instance_valid(instance) || command == NULL) {
        return C34_INVALID_CONTRACT;
    }
    for (index = 0; index < C34_SIDECAR_SLOTS; ++index) {
        if (instance->sidecar[index].used && instance->sidecar[index].ready &&
            instance->sidecar[index].result.outer.command.instance_nonce ==
                command->instance_nonce &&
            instance->sidecar[index].result.outer.command.command_uid ==
                command->command_uid &&
            instance->sidecar[index].result.outer.command.controller_epoch ==
                command->controller_epoch &&
            instance->sidecar[index].result.outer.command.slot ==
                command->slot &&
            instance->sidecar[index].result.outer.command.slot_generation ==
                command->slot_generation) {
            memset(&instance->sidecar[index], 0,
                   sizeof(instance->sidecar[index]));
            {
                unsigned int obligation;

                for (obligation = 0;
                     obligation < C34_OBLIGATION_SLOTS; ++obligation) {
                    struct c34_obligation_entry *entry =
                        &instance->obligations[obligation];

                    if (entry->used &&
                        entry->command.instance_nonce ==
                            command->instance_nonce &&
                        entry->command.command_uid == command->command_uid &&
                        entry->command.controller_epoch ==
                            command->controller_epoch &&
                        entry->command.slot == command->slot &&
                        entry->command.slot_generation ==
                            command->slot_generation &&
                        entry->obligation.closure !=
                            FWLAB_PERSIST_CLOSE_OPEN &&
                        entry->obligation.closure !=
                            FWLAB_PERSIST_CLOSE_INDETERMINATE) {
                        memset(entry, 0, sizeof(*entry));
                    }
                }
            }
            return C34_OK;
        }
    }
    return C34_NOT_FOUND;
}

enum c34_result c34_checkpoint_start(struct c34 *instance)
{
    struct c34_record checkpoint;
    uint32_t generation;
    uint16_t block;
    unsigned int index;

    if (!c34_instance_valid(instance) || instance->phase != 0 ||
        instance->graph.kind != C34_GRAPH_NONE || overlay_present(instance) ||
        instance->checkpoint_generation >= C34_CHECKPOINT_LIMIT) {
        return C34_WRONG_STATE;
    }
    for (index = 0; index < C34_OBLIGATION_SLOTS; ++index) {
        if (instance->obligations[index].used &&
            instance->obligations[index].obligation.closure ==
                FWLAB_PERSIST_CLOSE_OPEN) {
            return C34_WRONG_STATE;
        }
    }
    generation = instance->checkpoint_generation + 1u;
    block = (uint16_t)(C34_CHECKPOINT_BLOCK0 + generation - 1u);
    if (instance->blocks[block].next_program_page != 0 ||
        c34_build_checkpoint_record(instance, generation, &checkpoint) !=
            C34_OK) {
        return C34_NO_CAPACITY;
    }
    memset(&instance->graph, 0, sizeof(instance->graph));
    instance->graph.kind = C34_GRAPH_CHECKPOINT;
    instance->graph.state = C34_GRAPH_PREPARE_DATA_TRANSFER;
    instance->graph.record = checkpoint;
    instance->graph.checkpoint_record = checkpoint;
    instance->graph.operation_ppa = c34_ppa(block, 0);
    if (c34_record_encode(&checkpoint, instance->graph.main,
                          instance->graph.oob) != C34_OK) {
        memset(&instance->graph, 0, sizeof(instance->graph));
        return C34_INVARIANT_FAILURE;
    }
    return C34_OK;
}

enum c34_result c34_relocation_start(struct c34 *instance)
{
    struct c34_logical_entry *source;
    struct c34_mutation *mutation;
    uint8_t atom;
    enum c34_result result;

    if (!c34_instance_valid(instance) || instance->phase != 0 ||
        instance->graph.kind != C34_GRAPH_NONE || overlay_present(instance) ||
        instance->next_record_id + 1u > C34_RECORD_LIMIT ||
        instance->next_commit_sequence + 1u > C34_RECORD_LIMIT ||
        instance->blocks[C34_JOURNAL_BLOCK].next_program_page >=
            C34_PAGES_PER_BLOCK) {
        return C34_WRONG_STATE;
    }
    memset(&instance->graph, 0, sizeof(instance->graph));
    result = c34_choose_relocation(
        instance, &atom, &instance->graph.victim,
        &instance->graph.destination);
    if (result != C34_OK) {
        memset(&instance->graph, 0, sizeof(instance->graph));
        return result;
    }
    instance->graph.kind = C34_GRAPH_RELOCATION;
    instance->graph.state = C34_GRAPH_PREPARE_READ_TRIGGER;
    instance->graph.active_atom = atom;
    source = &instance->l2p[atom];
    mutation = &instance->graph.mutation[atom];
    mutation->used = 1;
    mutation->atom = atom;
    mutation->target_version = source->version;
    mutation->predecessor_version = source->version;
    mutation->copy_sequence = 1;
    mutation->logical_state_id = source->logical_state_id;
    mutation->predecessor_state_id = source->logical_state_id;
    mutation->mutation_id = UINT32_C(0x80000000) |
                            instance->next_commit_sequence;
    return C34_OK;
}
