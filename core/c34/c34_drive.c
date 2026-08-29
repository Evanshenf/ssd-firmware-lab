/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_internal.h"

#include <string.h>

static struct c34_mutation *active_mutation(struct c34 *instance)
{
    if (instance->graph.active_atom >= C34_ATOMS ||
        !instance->graph.mutation[instance->graph.active_atom].used) {
        return NULL;
    }
    return &instance->graph.mutation[instance->graph.active_atom];
}

static int select_next_atom(struct c34 *instance)
{
    unsigned int atom;

    for (atom = instance->graph.active_atom + 1u; atom < C34_ATOMS; ++atom) {
        if (instance->graph.mutation[atom].used) {
            instance->graph.active_atom = (uint8_t)atom;
            return 1;
        }
    }
    return 0;
}

static enum c34_result prepare_outer_data(struct c34 *instance)
{
    struct c34_mutation *mutation = active_mutation(instance);
    struct fwlab_nfc_ppa ppa;
    enum c34_result result;

    if (mutation == NULL) {
        return C34_INVARIANT_FAILURE;
    }
    result = c34_allocate_data(instance, &ppa);
    if (result != C34_OK) {
        return result;
    }
    result = c34_build_data_record(
        instance, mutation, &ppa, 0, mutation->payload,
        &instance->graph.record);
    if (result != C34_OK) {
        return result;
    }
    instance->graph.operation_ppa = ppa;
    return c34_record_encode(
        &instance->graph.record, instance->graph.main,
        instance->graph.oob);
}

static enum c34_result prepare_outer_meta(struct c34 *instance)
{
    struct c34_mutation *mutation = active_mutation(instance);
    struct fwlab_nfc_ppa ppa;
    enum c34_result result;

    if (mutation == NULL) {
        return C34_INVARIANT_FAILURE;
    }
    if (instance->graph.request.kind == C34_REQUEST_WRITE) {
        result = c34_build_mapping_record(
            instance, mutation, &instance->graph.record);
    } else {
        result = c34_build_tombstone_record(
            instance, mutation, &instance->graph.record);
    }
    if (result != C34_OK) {
        return result;
    }
    result = c34_allocate_journal(instance, &ppa);
    if (result != C34_OK) {
        return result;
    }
    instance->graph.operation_ppa = ppa;
    mutation->metadata_record_id = instance->graph.record.record_id;
    return c34_record_encode(
        &instance->graph.record, instance->graph.main,
        instance->graph.oob);
}

static enum c34_result prepare_relocation_meta(struct c34 *instance)
{
    struct c34_mutation *mutation = active_mutation(instance);
    const struct c34_logical_entry *source;
    struct fwlab_nfc_ppa ppa;
    enum c34_result result;

    if (mutation == NULL) {
        return C34_INVARIANT_FAILURE;
    }
    source = &instance->l2p[mutation->atom];
    result = c34_build_relocation_record(
        instance, mutation, source, &instance->graph.record);
    if (result != C34_OK ||
        c34_allocate_journal(instance, &ppa) != C34_OK) {
        return result != C34_OK ? result : C34_NO_CAPACITY;
    }
    instance->graph.operation_ppa = ppa;
    mutation->metadata_record_id = instance->graph.record.record_id;
    return c34_record_encode(
        &instance->graph.record, instance->graph.main,
        instance->graph.oob);
}

static enum c34_result start_data_transfer(struct c34 *instance)
{
    enum c34_result result;

    if (instance->graph.record.type == 0) {
        if (instance->graph.kind == C34_GRAPH_OUTER) {
            result = prepare_outer_data(instance);
        } else {
            result = C34_INVARIANT_FAILURE;
        }
        if (result != C34_OK) {
            return result;
        }
    }
    result = c34_nfc_program_transfer(
        instance, &instance->graph.operation_ppa);
    if (result == C34_OK) {
        instance->graph.state = C34_GRAPH_WAIT_DATA_TRANSFER;
    }
    return result;
}

static enum c34_result start_meta_transfer(struct c34 *instance)
{
    enum c34_result result;

    if (instance->graph.record.type == 0) {
        if (instance->graph.kind == C34_GRAPH_OUTER) {
            result = prepare_outer_meta(instance);
        } else if (instance->graph.kind == C34_GRAPH_RELOCATION) {
            result = prepare_relocation_meta(instance);
        } else {
            result = C34_INVARIANT_FAILURE;
        }
        if (result != C34_OK) {
            return result;
        }
    }
    result = c34_nfc_program_transfer(
        instance, &instance->graph.operation_ppa);
    if (result == C34_OK) {
        instance->graph.state = C34_GRAPH_WAIT_META_TRANSFER;
    }
    return result;
}

static int program_complete(const struct fwlab_nfc_completion *completion)
{
    return completion->terminal == FWLAB_NFC_TERMINAL_SUCCESS &&
           completion->physical_outcome == FWLAB_NFC_PHYS_APPLIED &&
           completion->integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
           completion->applied_region_mask == FWLAB_NFC_REGION_MASK;
}

static uint8_t failure_effect(
    const struct fwlab_nfc_completion *completion
)
{
    if (completion->physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT) {
        return FWLAB_C31_EFFECT_NONE;
    }
    return completion->integrity == FWLAB_NFC_INTEGRITY_COMPLETE ?
        FWLAB_C31_EFFECT_FULL : FWLAB_C31_EFFECT_UNKNOWN_PREFIX;
}

static enum c34_result finish_outer_mutations(struct c34 *instance)
{
    struct c34_graph *graph = &instance->graph;
    struct c34_obligation_entry *entry =
        &instance->obligations[graph->obligation_index];
    struct fwlab_persist_atom_fact facts[C34_ATOMS];
    size_t fact_count = 0;
    unsigned int atom;

    entry->obligation.fact_mask |=
        FWLAB_PERSIST_FACT_C_MAP | FWLAB_PERSIST_FACT_LOGICAL_DURABLE;
    entry->obligation.closure = FWLAB_PERSIST_CLOSE_C_MAP;
    for (atom = 0; atom < C34_ATOMS; ++atom) {
        if (!graph->mutation[atom].used) {
            continue;
        }
        facts[fact_count++] = graph->mutation[atom].fact;
        instance->overlay_valid[atom] = 0;
        instance->overlay_kind[atom] = 0;
        memset(instance->overlay_payload[atom], 0, C34_ATOM_BYTES);
    }
    if (fwlab_persist_command_witness(
            &instance->config.persistence, &graph->persist_request, facts,
            fact_count, NULL, 0, &graph->witness) != FWLAB_PERSIST_OK ||
        graph->witness.witness_class != FWLAB_PERSIST_DURABLE_ELIGIBLE) {
        return C34_INVARIANT_FAILURE;
    }
    graph->maintenance_result = C34_COMMAND_SUCCESS;
    graph->terminal = FWLAB_C31_PROVIDER_SUCCESS;
    if (!graph->outer_event_sent) {
        graph->outer_event_ready = 1;
    }
    graph->state = C34_GRAPH_DONE;
    if (!instance->sidecar[graph->sidecar_index].used) {
        memset(entry, 0, sizeof(*entry));
    }
    return C34_OK;
}

static enum c34_result after_data_execute(struct c34 *instance)
{
    struct c34_graph *graph = &instance->graph;
    enum c34_result result = c34_nfc_finish_physical(instance);

    if (result != C34_OK) {
        return result;
    }
    if (!program_complete(&graph->completion)) {
        (void)c34_refresh(instance, false);
        c34_graph_fail(instance, C34_COMMAND_MEDIA_FAILURE,
                       failure_effect(&graph->completion));
        return C34_OK;
    }
    result = c34_refresh(instance, false);
    if (result != C34_OK) {
        return result;
    }
    if (graph->kind == C34_GRAPH_OUTER) {
        struct c34_mutation *mutation = active_mutation(instance);

        if (mutation == NULL) {
            return C34_INVARIANT_FAILURE;
        }
        mutation->fact.fact_mask |= FWLAB_PERSIST_FACT_C_PHYS_APPLIED |
                                   FWLAB_PERSIST_FACT_DATA_STABLE;
        memset(&graph->record, 0, sizeof(graph->record));
        graph->state = C34_GRAPH_PREPARE_META_TRANSFER;
        return C34_OK;
    }
    if (graph->kind == C34_GRAPH_RELOCATION) {
        memset(&graph->record, 0, sizeof(graph->record));
        graph->state = C34_GRAPH_PREPARE_META_TRANSFER;
        return C34_OK;
    }
    if (graph->kind == C34_GRAPH_CHECKPOINT) {
        uint8_t slot =
            (uint8_t)(graph->checkpoint_record.checkpoint_generation - 1u);
        uint32_t crc = c34_crc32c(graph->main, C34_MAIN_BYTES);

        result = c34_build_anchor_record(
            instance, &graph->checkpoint_record, slot, crc, &graph->record);
        if (result != C34_OK) {
            return result;
        }
        graph->operation_ppa = c34_ppa(
            (uint16_t)(C34_CHECKPOINT_BLOCK0 + slot), 1);
        result = c34_record_encode(
            &graph->record, graph->main, graph->oob);
        if (result != C34_OK) {
            return result;
        }
        graph->state = C34_GRAPH_PREPARE_META_TRANSFER;
        return C34_OK;
    }
    return C34_INVARIANT_FAILURE;
}

static enum c34_result after_meta_execute(struct c34 *instance)
{
    struct c34_graph *graph = &instance->graph;
    enum c34_result result = c34_nfc_finish_physical(instance);

    if (result != C34_OK) {
        return result;
    }
    if (!program_complete(&graph->completion)) {
        (void)c34_refresh(instance, false);
        c34_graph_fail(instance, C34_COMMAND_MEDIA_FAILURE,
                       failure_effect(&graph->completion));
        return C34_OK;
    }
    result = c34_refresh(instance, false);
    if (result != C34_OK) {
        return result;
    }
    if (graph->kind == C34_GRAPH_OUTER) {
        struct c34_mutation *mutation = active_mutation(instance);

        if (mutation == NULL) {
            return C34_INVARIANT_FAILURE;
        }
        mutation->fact.fact_mask |= FWLAB_PERSIST_FACT_C_PHYS_APPLIED |
                                   FWLAB_PERSIST_FACT_C_MAP |
                                   FWLAB_PERSIST_FACT_LOGICAL_DURABLE;
        mutation->fact.closure = FWLAB_PERSIST_CLOSE_C_MAP;
        instance->overlay_valid[mutation->atom] = 0;
        memset(&graph->record, 0, sizeof(graph->record));
        if (select_next_atom(instance)) {
            graph->state = graph->request.kind == C34_REQUEST_WRITE ?
                C34_GRAPH_PREPARE_DATA_TRANSFER :
                C34_GRAPH_PREPARE_META_TRANSFER;
            return C34_OK;
        }
        return finish_outer_mutations(instance);
    }
    if (graph->kind == C34_GRAPH_RELOCATION) {
        const struct c34_logical_entry *entry =
            &instance->l2p[graph->active_atom];

        if (entry->kind != C34_LOGICAL_VALUE ||
            entry->copy_sequence != 1 ||
            !c34_ppa_equal(&entry->data_ppa, &graph->destination)) {
            return C34_INVARIANT_FAILURE;
        }
        graph->operation_ppa = graph->victim;
        graph->operation_ppa.page = 0;
        graph->state = C34_GRAPH_PREPARE_ERASE;
        return C34_OK;
    }
    if (graph->kind == C34_GRAPH_CHECKPOINT) {
        if (instance->checkpoint_generation !=
            graph->checkpoint_record.checkpoint_generation) {
            return C34_INVARIANT_FAILURE;
        }
        graph->operation_ppa = c34_ppa(C34_JOURNAL_BLOCK, 0);
        graph->state = C34_GRAPH_PREPARE_ERASE;
        return C34_OK;
    }
    return C34_INVARIANT_FAILURE;
}

static enum c34_result validate_read_payload(struct c34 *instance)
{
    struct c34_graph *graph = &instance->graph;
    struct fwlab_nfc_buffer_ref main_ref;
    struct fwlab_nfc_buffer_ref oob_ref;
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
    struct c34_record record;
    const struct c34_logical_entry *expected =
        &instance->l2p[graph->active_atom];

    memset(&main_ref, 0, sizeof(main_ref));
    main_ref.controller_region = instance->config.controller_region;
    main_ref.offset = instance->config.controller_buffer_offset;
    main_ref.length = C34_MAIN_BYTES;
    memset(&oob_ref, 0, sizeof(oob_ref));
    oob_ref.controller_region = instance->config.controller_region;
    oob_ref.offset = instance->config.controller_buffer_offset +
                     C34_MAIN_BYTES;
    oob_ref.length = C34_OOB_BYTES;
    if (instance->buffers.ops->read(
            instance->buffers.context, &main_ref, graph->main,
            C34_MAIN_BYTES) != FWLAB_NFC_API_OK ||
        instance->buffers.ops->read(
            instance->buffers.context, &oob_ref, graph->oob,
            C34_OOB_BYTES) != FWLAB_NFC_API_OK) {
        return C34_INVARIANT_FAILURE;
    }
    memset(&page, 0, sizeof(page));
    page.version = FWLAB_NFC_CONTRACT_VERSION;
    page.size = sizeof(page);
    page.erase_generation_seen = expected->data_erase_generation;
    page.state = FWLAB_NAND_PAGE_VALID;
    page.program_count = 1;
    block = instance->blocks[expected->data_ppa.block];
    if (c34_record_decode(
            graph->main, graph->oob, &page, &block, &record) !=
            C34_DECODE_OK || record.type != C34_RECORD_DATA ||
        record.atom != graph->active_atom ||
        record.logical_version != expected->version ||
        record.copy_sequence != expected->copy_sequence ||
        record.logical_state_id != expected->logical_state_id ||
        record.record_id != expected->data_record_id ||
        record.value_crc32c != expected->value_crc32c) {
        return C34_CORRUPT;
    }
    return C34_OK;
}

static enum c34_result after_read_transfer(struct c34 *instance)
{
    struct c34_graph *graph = &instance->graph;
    enum c34_result result;

    if (graph->completion.terminal != FWLAB_NFC_TERMINAL_SUCCESS ||
        graph->completion.ecc_status == FWLAB_NFC_ECC_UNCORRECTABLE) {
        c34_graph_fail(instance, C34_COMMAND_MEDIA_FAILURE,
                       FWLAB_C31_EFFECT_NONE);
        return C34_OK;
    }
    result = validate_read_payload(instance);
    if (result != C34_OK) {
        c34_graph_fail(instance, C34_COMMAND_MEDIA_FAILURE,
                       FWLAB_C31_EFFECT_NONE);
        return C34_OK;
    }
    if (graph->kind == C34_GRAPH_OUTER) {
        graph->result_present_mask = (uint8_t)(1u << graph->active_atom);
        graph->maintenance_result = C34_COMMAND_SUCCESS;
        graph->terminal = FWLAB_C31_PROVIDER_SUCCESS;
        graph->outer_event_ready = 1;
        graph->state = C34_GRAPH_DONE;
        return C34_OK;
    }
    if (graph->kind == C34_GRAPH_RELOCATION) {
        struct c34_mutation *mutation = active_mutation(instance);

        if (mutation == NULL) {
            return C34_INVARIANT_FAILURE;
        }
        memcpy(mutation->payload, graph->main, C34_ATOM_BYTES);
        result = c34_build_data_record(
            instance, mutation, &graph->destination, 1, mutation->payload,
            &graph->record);
        if (result != C34_OK) {
            return result;
        }
        graph->operation_ppa = graph->destination;
        result = c34_record_encode(
            &graph->record, graph->main, graph->oob);
        if (result != C34_OK) {
            return result;
        }
        graph->state = C34_GRAPH_PREPARE_DATA_TRANSFER;
        return C34_OK;
    }
    return C34_INVARIANT_FAILURE;
}

static enum c34_result after_erase(struct c34 *instance)
{
    struct c34_graph *graph = &instance->graph;
    enum c34_result result = c34_nfc_finish_physical(instance);
    int success;

    if (result != C34_OK) {
        return result;
    }
    success = graph->completion.terminal == FWLAB_NFC_TERMINAL_SUCCESS &&
              graph->completion.physical_outcome ==
                  FWLAB_NFC_PHYS_APPLIED &&
              graph->completion.integrity == FWLAB_NFC_INTEGRITY_COMPLETE;
    result = c34_refresh(instance, false);
    if (result != C34_OK) {
        return result;
    }
    graph->maintenance_result = success ? C34_COMMAND_SUCCESS :
                                          C34_COMMAND_MEDIA_FAILURE;
    graph->state = C34_GRAPH_DONE;
    return C34_OK;
}

static enum c34_result handle_completed_inner(struct c34 *instance)
{
    struct c34_graph *graph = &instance->graph;

    if (instance->phase == 1) {
        if (graph->physical_bound &&
            c34_nfc_finish_physical(instance) != C34_OK) {
            return C34_INVARIANT_FAILURE;
        }
        (void)c34_refresh(instance, false);
        c34_graph_fail(instance, C34_COMMAND_CANCELLED,
                       failure_effect(&graph->completion));
        return C34_OK;
    }
    if (graph->completion.terminal == FWLAB_NFC_TERMINAL_CANCELLED) {
        c34_graph_fail(instance, C34_COMMAND_CANCELLED,
                       failure_effect(&graph->completion));
        return C34_OK;
    }
    switch ((enum c34_graph_state)graph->state) {
    case C34_GRAPH_WAIT_DATA_TRANSFER:
        if (graph->completion.terminal != FWLAB_NFC_TERMINAL_SUCCESS) {
            c34_graph_fail(instance, C34_COMMAND_MEDIA_FAILURE,
                           FWLAB_C31_EFFECT_NONE);
        } else {
            graph->cache = graph->completion.cache;
            graph->state = C34_GRAPH_PREPARE_DATA_EXECUTE;
        }
        return C34_OK;
    case C34_GRAPH_WAIT_DATA_EXECUTE:
        return after_data_execute(instance);
    case C34_GRAPH_WAIT_META_TRANSFER:
        if (graph->completion.terminal != FWLAB_NFC_TERMINAL_SUCCESS) {
            c34_graph_fail(instance, C34_COMMAND_MEDIA_FAILURE,
                           FWLAB_C31_EFFECT_NONE);
        } else {
            graph->cache = graph->completion.cache;
            graph->state = C34_GRAPH_PREPARE_META_EXECUTE;
        }
        return C34_OK;
    case C34_GRAPH_WAIT_META_EXECUTE:
        return after_meta_execute(instance);
    case C34_GRAPH_WAIT_READ_TRIGGER:
        if (graph->completion.terminal != FWLAB_NFC_TERMINAL_SUCCESS) {
            c34_graph_fail(instance, C34_COMMAND_MEDIA_FAILURE,
                           FWLAB_C31_EFFECT_NONE);
        } else {
            graph->cache = graph->completion.cache;
            graph->state = C34_GRAPH_PREPARE_READ_TRANSFER;
        }
        return C34_OK;
    case C34_GRAPH_WAIT_READ_TRANSFER:
        return after_read_transfer(instance);
    case C34_GRAPH_WAIT_ERASE:
        return after_erase(instance);
    default:
        return C34_INVARIANT_FAILURE;
    }
}

static void cleanup_completed_graph(struct c34 *instance)
{
    struct c34_graph *graph = &instance->graph;

    if (graph->state != C34_GRAPH_DONE &&
        graph->state != C34_GRAPH_FAILED) {
        return;
    }
    if (graph->kind == C34_GRAPH_OUTER && graph->outer_event_ready &&
        !graph->outer_event_sent) {
        return;
    }
    memset(graph, 0, sizeof(*graph));
}

static enum c34_result drive_ready_state(struct c34 *instance)
{
    struct c34_graph *graph = &instance->graph;
    enum c34_result result;

    switch ((enum c34_graph_state)graph->state) {
    case C34_GRAPH_PREPARE_DATA_TRANSFER:
        result = start_data_transfer(instance);
        break;
    case C34_GRAPH_PREPARE_DATA_EXECUTE:
        result = c34_nfc_program_execute(instance);
        if (result == C34_OK) {
            graph->state = C34_GRAPH_WAIT_DATA_EXECUTE;
        }
        break;
    case C34_GRAPH_PREPARE_META_TRANSFER:
        result = start_meta_transfer(instance);
        break;
    case C34_GRAPH_PREPARE_META_EXECUTE:
        result = c34_nfc_program_execute(instance);
        if (result == C34_OK) {
            graph->state = C34_GRAPH_WAIT_META_EXECUTE;
        }
        break;
    case C34_GRAPH_PREPARE_READ_TRIGGER:
        result = c34_nfc_read_trigger(instance, &graph->victim);
        if (result == C34_OK) {
            graph->state = C34_GRAPH_WAIT_READ_TRIGGER;
        }
        break;
    case C34_GRAPH_PREPARE_READ_TRANSFER:
        result = c34_nfc_read_transfer(instance);
        if (result == C34_OK) {
            graph->state = C34_GRAPH_WAIT_READ_TRANSFER;
        }
        break;
    case C34_GRAPH_PREPARE_ERASE:
        result = c34_nfc_erase(instance, &graph->operation_ppa);
        if (result == C34_OK) {
            graph->state = C34_GRAPH_WAIT_ERASE;
        }
        break;
    default:
        return C34_INVARIANT_FAILURE;
    }
    if (result == C34_NO_CAPACITY) {
        return C34_OK;
    }
    return result;
}

enum c34_result c34_drive_one(struct c34 *instance)
{
    bool completed;
    enum c34_result result;

    if (!c34_instance_valid(instance)) {
        return C34_INVALID_CONTRACT;
    }
    if (instance->graph.kind == C34_GRAPH_NONE) {
        return C34_OK;
    }
    if (instance->graph.inner_pending) {
        result = c34_nfc_progress(instance, &completed);
        if (result != C34_OK || !completed) {
            return result;
        }
        result = handle_completed_inner(instance);
        if (result != C34_OK) {
            instance->phase = 2;
            return result;
        }
        cleanup_completed_graph(instance);
        return C34_OK;
    }
    if (instance->phase == 1) {
        memset(instance->overlay_valid, 0, sizeof(instance->overlay_valid));
        memset(instance->overlay_kind, 0, sizeof(instance->overlay_kind));
        memset(instance->overlay_payload, 0,
               sizeof(instance->overlay_payload));
        c34_graph_fail(instance, C34_COMMAND_CANCELLED,
                       FWLAB_C31_EFFECT_NONE);
        cleanup_completed_graph(instance);
        return C34_OK;
    }
    cleanup_completed_graph(instance);
    if (instance->graph.kind == C34_GRAPH_NONE ||
        instance->graph.state == C34_GRAPH_DONE ||
        instance->graph.state == C34_GRAPH_FAILED) {
        return C34_OK;
    }
    result = drive_ready_state(instance);
    if (result != C34_OK) {
        if (result == C34_COUNTER_EXHAUSTED) {
            instance->phase = 2;
        }
        c34_graph_fail(instance,
                       result == C34_NO_CAPACITY ? C34_COMMAND_NO_SPACE :
                                                  C34_COMMAND_MEDIA_FAILURE,
                       FWLAB_C31_EFFECT_NONE);
        return result;
    }
    return C34_OK;
}
