#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Compile fresh C4.2 fake-provider variations and require exact event kills."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from check_c42_claim_models import ModelError, build_model


ROOT = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_unique(path: Path, before: str, after: str) -> None:
    text = path.read_text(encoding="utf-8")
    if text.count(before) != 1:
        raise RuntimeError(f"non-unique provider mutation anchor: {path}")
    path.write_text(text.replace(before, after, 1), encoding="utf-8")


def failure_diagnostics(output: str) -> set[str]:
    return set(re.findall(r"^provider matrix FAIL: (.+)$", output, re.MULTILINE))


def variations() -> list[dict[str, object]]:
    return [
        {
            "name": "PM_REQUIRED_ZERO_OUTPUT_VIOLATION",
            "label": "prepare_start exact event",
            "case_id": "prepare-start",
            "operator_ids": ["field_required_zero_violation"],
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "    result->disposition = disposition;\n"
                       "    if (record != NULL && disposition == "
                       "FWLAB_HIF_PREPARE_RESERVED) {",
                       "    result->disposition = disposition;\n"
                       "    result->reserved[0] = 1;\n"
                       "    if (record != NULL && disposition == "
                       "FWLAB_HIF_PREPARE_RESERVED) {")],
        },
        {
            "name": "PM_CALL_KIND_SUBSTITUTED",
            "label": "prepare_start exact event",
            "case_id": "prepare-start",
            "operator_ids": ["entrypoint_call_kind_substitute"],
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "        context, C42_FAKE_COMMAND_PREPARE, "
                       "C42_FAKE_CALL_START, call,",
                       "        context, C42_FAKE_COMMAND_PREPARE, "
                       "C42_FAKE_CALL_QUERY, call,")],
        },
        {
            "name": "PM_PRESERVED_OUTPUT_OVERWRITTEN",
            "label": "prepare-start in-progress preserves output",
            "case_id": "prepare-in-progress",
            "operator_ids": ["field_preserve_overwrite"],
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "    if (command->script.prepare_delay != 0) {\n"
                       "        return FWLAB_HIF_PORT_IN_PROGRESS;\n"
                       "    }",
                       "    if (command->script.prepare_delay != 0) {\n"
                       "        result->version = FWLAB_HIF_COMMAND_PORT_VERSION;\n"
                       "        return FWLAB_HIF_PORT_IN_PROGRESS;\n"
                       "    }")],
        },
        {
            "name": "PM_POLL_SEQUENCE_STALLED",
            "label": "poll ready sequence strictly advances",
            "case_id": "poll-sequence",
            "operator_ids": ["relation_stall"],
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "    events[0].sequence = command->next_ready_sequence++;\n"
                       "    selected->ready_sent = 1;",
                       "    events[0].sequence = command->next_ready_sequence;\n"
                       "    selected->ready_sent = 1;")],
        },
        {
            "name": "PM_COMMAND_EFFECT_SKIPPED",
            "label": "effect CONSUME_COMMIT caller/provider state",
            "case_id": "command-effects",
            "operator_ids": ["effect_skip"],
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "    if (injection_take(\n"
                       "            command, C42_FAKE_COMMAND_CONSUME_COMMIT,\n"
                       "            &injected, &value, &omit)) {\n"
                       "        if ((command->injection_flags & "
                       "C42_FAKE_APPLY_EFFECT) != 0) {\n"
                       "            record->consume_committed = 1;\n"
                       "            mark_injected_effect(\n"
                       "                command, "
                       "C42_FAKE_COMMAND_EFFECT_CONSUME_COMMITTED\n"
                       "            );",
                       "    if (injection_take(\n"
                       "            command, C42_FAKE_COMMAND_CONSUME_COMMIT,\n"
                       "            &injected, &value, &omit)) {\n"
                       "        if ((command->injection_flags & "
                       "C42_FAKE_APPLY_EFFECT) != 0) {\n"
                       "            mark_injected_effect(\n"
                       "                command, "
                       "C42_FAKE_COMMAND_EFFECT_CONSUME_COMMITTED\n"
                       "            );")],
        },
        {
            "name": "PM_MEMORY_EFFECT_DUPLICATED",
            "label": "memory capture exact 64-byte output and provider effect",
            "case_id": "memory-direct",
            "operator_ids": ["effect_duplicate"],
            "edits": [("frontends/headless-c4/fakes/c42_memory.c",
                       "        if (direct->apply_effect != 0) {\n"
                       "            memory->capture_count++;\n"
                       "            mark_direct_effect(memory, "
                       "direct->applied_effect);",
                       "        if (direct->apply_effect != 0) {\n"
                       "            memory->capture_count += 2;\n"
                       "            mark_direct_effect(memory, "
                       "direct->applied_effect);")],
        },
        {
            "name": "PM_RESPONSE_LOSS_ORDER_COLLAPSED",
            "label": "response lost before effect",
            "case_id": "response-order",
            "operator_ids": ["response_order_swap"],
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "        if (command->injection_requested_effect !=\n"
                       "                C42_FAKE_COMMAND_EFFECT_NONE &&\n"
                       "            event.output_write_mask == 0 &&",
                       "        if (command->injection_applied_effect !=\n"
                       "                C42_FAKE_COMMAND_EFFECT_NONE &&\n"
                       "            event.output_write_mask == 0 &&")],
        },
        {
            "name": "PM_BODY_RETURNS_WRONG_TOKEN",
            "operator_ids": ["field_corrupt"],
            "label": "memory event all fields exact",
            "case_id": "memory-direct",
            "edits": [("frontends/headless-c4/fakes/c42_memory.c",
                       "        direct_status_fill(memory, status, client_token, direct);\n"
                       "        memory->body_call_count++;",
                       "        direct_status_fill(memory, status, client_token, direct);\n"
                       "        if (direct->write_status != 0 &&\n"
                       "            direct->token_variant ==\n"
                       "                C42_FAKE_MEMORY_TOKEN_EXACT) {\n"
                       "            status->token.uid++;\n"
                       "        }\n"
                       "        memory->body_call_count++;")],
        },
        {
            "name": "PM_CONSUME_RETURNS_WRONG_STATE",
            "operator_ids": ["field_invalid_enum"],
            "label": "consume caller output and applied effect exact",
            "case_id": "command-injection",
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "        if ((command->injection_write_mask & C42_FAKE_WRITE_VALUE) != 0) {\n"
                       "            provider_output_mark(command, C42_FAKE_WRITE_VALUE);\n"
                       "            *state = (enum fwlab_hif_consume_state)value;\n"
                       "        }\n"
                       "        (void)omit;\n"
                       "        return injected;\n"
                       "    }\n"
                       "    record->consume_queries++;",
                       "        if ((command->injection_write_mask & C42_FAKE_WRITE_VALUE) != 0) {\n"
                       "            provider_output_mark(command, C42_FAKE_WRITE_VALUE);\n"
                       "            *state = (enum fwlab_hif_consume_state)UINT32_MAX;\n"
                       "        }\n"
                       "        (void)omit;\n"
                       "        return injected;\n"
                       "    }\n"
                       "    record->consume_queries++;")],
        },
        {
            "name": "PM_CONSUME_INPUT_MATCH_COLLAPSE",
            "label": "consume input structural/match facts separated",
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "static enum fwlab_hif_command_port_result logged_consume_commit(\n"
                       "    void *context,\n"
                       "    const struct fwlab_hif_consume_token *token,\n"
                       "    enum fwlab_hif_consume_state *state)\n"
                       "{\n"
                       "    int input_match = context != NULL && token != NULL &&\n"
                       "        find_consume(context, token) != NULL;",
                       "static enum fwlab_hif_command_port_result logged_consume_commit(\n"
                       "    void *context,\n"
                       "    const struct fwlab_hif_consume_token *token,\n"
                       "    enum fwlab_hif_consume_state *state)\n"
                       "{\n"
                       "    int input_match = fwlab_hif_consume_token_valid(token);")],
        },
        {
            "name": "PM_CONSUME_APPLIED_EFFECT_WRONG",
            "label": "consume caller output and applied effect exact",
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "    if (injection_take(\n"
                       "            command, C42_FAKE_COMMAND_CONSUME_COMMIT,\n"
                       "            &injected, &value, &omit)) {\n"
                       "        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0) {\n"
                       "            record->consume_committed = 1;\n"
                       "            mark_injected_effect(\n"
                       "                command, C42_FAKE_COMMAND_EFFECT_CONSUME_COMMITTED\n"
                       "            );",
                       "    if (injection_take(\n"
                       "            command, C42_FAKE_COMMAND_CONSUME_COMMIT,\n"
                       "            &injected, &value, &omit)) {\n"
                       "        if ((command->injection_flags & C42_FAKE_APPLY_EFFECT) != 0) {\n"
                       "            record->consume_committed = 1;\n"
                       "            mark_injected_effect(\n"
                       "                command, C42_FAKE_COMMAND_EFFECT_CONSUME_ABORTED\n"
                       "            );")],
        },
        {
            "name": "PM_ALLOW_OK_WITHOUT_STATUS",
            "operator_ids": ["field_required_omission"],
            "label": "memory rejects transactional OK without exact status",
            "case_id": "scrub-status-validation",
            "edits": [("frontends/headless-c4/fakes/c42_memory.c",
                       "        (injection->result == C42_MEMORY_OK && output_required &&\n"
                       "         injection->write_status == 0) ||",
                       "        (injection->result == C42_MEMORY_OK && output_required &&\n"
                       "         injection->write_status == 0 &&\n"
                       "         injection->operation != C42_FAKE_MEMORY_SCRUB) ||")],
        },
        {
            "name": "PM_ALLOW_UNUSED_EFFECT_METADATA",
            "label": "memory rejects unused applied-effect metadata",
            "edits": [("frontends/headless-c4/fakes/c42_memory.c",
                       "        (injection->apply_effect == 0 && injection->applied_effect != 0) ||",
                       "        (0 != 0 && injection->apply_effect == 0 &&\n"
                       "         injection->applied_effect != 0) ||")],
        },
        {
            "name": "PM_SAME_WRITE_NOT_RECORDED",
            "label": "same-value bool write remains observable",
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "    record->retired = 1;\n"
                       "    provider_output_mark(command, C42_FAKE_WRITE_VALUE);\n"
                       "    *aborted = true;",
                       "    record->retired = 1;\n"
                       "    *aborted = true;")],
        },
        {
            "name": "PM_ADMIT_INPUT_MATCH_COLLAPSE",
            "label": "mismatch admit client event",
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "    if (record == NULL ||\n"
                       "        record->prepare_key.client_uid != key->client_uid ||\n"
                       "        record->prepare_key.client_generation != key->generation ||\n"
                       "        !handle_equal(&command->handle, &record->prepared.handle) ||\n"
                       "        !origin_equal(&command->origin, &record->prepared.origin)) {",
                       "    if (record == NULL ||\n"
                       "        (0 != 0 &&\n"
                       "         record->prepare_key.client_uid != key->client_uid) ||\n"
                       "        record->prepare_key.client_generation != key->generation ||\n"
                       "        !handle_equal(&command->handle, &record->prepared.handle) ||\n"
                       "        !origin_equal(&command->origin, &record->prepared.origin)) {")],
        },
        {
            "name": "PM_FRESH_ADMIT_GENERATION_RELATION_REMOVED",
            "label": "mismatch fresh admit generation rejected unchanged",
            "operator_ids": ["field_stale_key", "relation_split"],
            "case_id": "command-input-match",
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "    if (record == NULL || record->prepare_key.client_uid != key->client_uid ||\n"
                       "        record->prepare_key.client_generation != key->generation ||\n"
                       "        !handle_equal(&canonical->handle, &record->prepared.handle) ||",
                       "    if (record == NULL || record->prepare_key.client_uid != key->client_uid ||\n"
                       "        !handle_equal(&canonical->handle, &record->prepared.handle) ||")],
        },
        {
            "name": "PM_ADMIT_GENERATION_IDENTITY_REMOVED",
            "label": "mismatch admit generation query rejected unchanged",
            "edits": [
                ("frontends/headless-c4/fakes/c42_command.c",
                 "    if (record == NULL || record->prepare_key.client_uid != key->client_uid ||\n"
                 "        record->prepare_key.client_generation != key->generation ||\n"
                 "        !handle_equal(&canonical->handle, &record->prepared.handle) ||",
                 "    if (record == NULL || record->prepare_key.client_uid != key->client_uid ||\n"
                 "        !handle_equal(&canonical->handle, &record->prepared.handle) ||"),
                ("frontends/headless-c4/fakes/c42_command.c",
                       "           left->client_uid == right->client_uid &&\n"
                       "           left->generation == right->generation &&\n"
                       "           left->reserved == right->reserved;",
                       "           left->client_uid == right->client_uid &&\n"
                       "           left->reserved == right->reserved;")
            ],
        },
        {
            "name": "PM_ADMIT_CANONICAL_DWORD_IDENTITY_REMOVED",
            "label": "mismatch complete canonical query rejected unchanged",
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "           memcmp(left->command_dword10_15, right->command_dword10_15,\n"
                       "                  sizeof(left->command_dword10_15)) == 0 &&\n"
                       "           left->transport_fault == right->transport_fault &&",
                       "           left->transport_fault == right->transport_fault &&")],
        },
        {
            "name": "PM_RELEASE_CLIENT_IDENTITY_REMOVED",
            "label": "mismatch release client query rejected unchanged",
            "edits": [
                ("frontends/headless-c4/fakes/c42_command.c",
                 "    if ((start == 0 && record->release_started == 0) ||\n"
                 "        (record->release_started != 0 &&\n"
                 "         record->release_client_uid != client_uid)) {",
                 "    if (start == 0 && record->release_started == 0) {"),
                ("frontends/headless-c4/fakes/c42_command.c",
                 "    return record->release_client_uid == client_uid;",
                 "    return 1;")
            ],
        },
        {
            "name": "PM_ADMIT_OUTPUT_MATCH_COLLAPSE",
            "label": "ADMIT output variant event",
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "        context, C42_FAKE_COMMAND_ADMIT, C42_FAKE_CALL_START, call,\n"
                       "        state == NULL ? UINT32_MAX : (uint32_t)*state, write_mask,\n"
                       "        admission_request_valid(key, command),\n"
                       "        input_match,\n"
                       "        write_mask != 0 &&\n"
                       "            (((write_mask & C42_FAKE_EVENT_WRITE_VALUE) == 0) ||\n"
                       "             (state != NULL && *state <= FWLAB_HIF_ADMISSION_POISONED)) &&\n"
                       "            (((write_mask & C42_FAKE_EVENT_WRITE_OBJECT) == 0) ||\n"
                       "             fwlab_hif_command_ticket_valid(ticket)),\n"
                       "        (write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 &&\n"
                       "            context != NULL && ticket != NULL &&\n"
                       "            find_ticket(context, ticket) != NULL,\n"
                       "        key == NULL ? 0 : key->client_uid,",
                       "        context, C42_FAKE_COMMAND_ADMIT, C42_FAKE_CALL_START, call,\n"
                       "        state == NULL ? UINT32_MAX : (uint32_t)*state, write_mask,\n"
                       "        admission_request_valid(key, command),\n"
                       "        input_match,\n"
                       "        write_mask != 0 &&\n"
                       "            (((write_mask & C42_FAKE_EVENT_WRITE_VALUE) == 0) ||\n"
                       "             (state != NULL && *state <= FWLAB_HIF_ADMISSION_POISONED)) &&\n"
                       "            (((write_mask & C42_FAKE_EVENT_WRITE_OBJECT) == 0) ||\n"
                       "             fwlab_hif_command_ticket_valid(ticket)),\n"
                       "        (write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 &&\n"
                       "            fwlab_hif_command_ticket_valid(ticket),\n"
                       "        key == NULL ? 0 : key->client_uid,")],
        },
        {
            "name": "PM_ADMIT_QUERY_OUTPUT_MATCH_COLLAPSE",
            "label": "ADMIT query output variant event",
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "        context, C42_FAKE_COMMAND_ADMIT, C42_FAKE_CALL_QUERY, call,\n"
                       "        state == NULL ? UINT32_MAX : (uint32_t)*state, write_mask,\n"
                       "        admission_request_valid(key, command),\n"
                       "        input_match,\n"
                       "        write_mask != 0 &&\n"
                       "            (((write_mask & C42_FAKE_EVENT_WRITE_VALUE) == 0) ||\n"
                       "             (state != NULL && *state <= FWLAB_HIF_ADMISSION_POISONED)) &&\n"
                       "            (((write_mask & C42_FAKE_EVENT_WRITE_OBJECT) == 0) ||\n"
                       "             fwlab_hif_command_ticket_valid(ticket)),\n"
                       "        (write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 &&\n"
                       "            context != NULL && ticket != NULL &&\n"
                       "            find_ticket(context, ticket) != NULL,\n"
                       "        key == NULL ? 0 : key->client_uid,",
                       "        context, C42_FAKE_COMMAND_ADMIT, C42_FAKE_CALL_QUERY, call,\n"
                       "        state == NULL ? UINT32_MAX : (uint32_t)*state, write_mask,\n"
                       "        admission_request_valid(key, command),\n"
                       "        input_match,\n"
                       "        write_mask != 0 &&\n"
                       "            (((write_mask & C42_FAKE_EVENT_WRITE_VALUE) == 0) ||\n"
                       "             (state != NULL && *state <= FWLAB_HIF_ADMISSION_POISONED)) &&\n"
                       "            (((write_mask & C42_FAKE_EVENT_WRITE_OBJECT) == 0) ||\n"
                       "             fwlab_hif_command_ticket_valid(ticket)),\n"
                       "        (write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 &&\n"
                       "            fwlab_hif_command_ticket_valid(ticket),\n"
                       "        key == NULL ? 0 : key->client_uid,")],
        },
        {
            "name": "PM_ACQUIRE_OUTPUT_MATCH_COLLAPSE",
            "label": "acquire output variant event",
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "        (write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 &&\n"
                       "            output_record != NULL && intent != NULL &&\n"
                       "            memcmp(&output_record->intent, intent, sizeof(*intent)) == 0,",
                       "        (write_mask & C42_FAKE_EVENT_WRITE_OBJECT) != 0 &&\n"
                       "            (output_record != NULL ||\n"
                       "             fwlab_hif_completion_lease_valid(lease)),")],
        },
        {
            "name": "PM_BODY_EXPECTED_IDENTITY_REMOVED",
            "label": "memory mismatch body expected call",
            "edits": [("frontends/headless-c4/fakes/c42_memory.c",
                       "               record->slot != slot ||\n"
                       "               memcmp(record->expected, expected, C42_CQE_BYTES) != 0) {",
                       "               record->slot != slot) {")],
        },
        {
            "name": "PM_SCRUB_INVERSE_IDENTITY_REMOVED",
            "label": "memory mismatch scrub inverse-phase call",
            "edits": [("frontends/headless-c4/fakes/c42_memory.c",
                       "               record->depth != depth ||\n"
                       "               record->inverse_phase != inverse_phase) {\n"
                       "        return C42_MEMORY_STALE;\n"
                       "    }\n"
                       "    if (direct_result_take(\n"
                       "            memory, C42_FAKE_MEMORY_SCRUB, &direct)) {",
                       "               record->depth != depth) {\n"
                       "        return C42_MEMORY_STALE;\n"
                       "    }\n"
                       "    if (direct_result_take(\n"
                       "            memory, C42_FAKE_MEMORY_SCRUB, &direct)) {")],
        },
        {
            "name": "PM_BODY_REPEAT_START_IDENTITY_REMOVED",
            "label": "memory mismatch repeated body expected call",
            "edits": [("frontends/headless-c4/fakes/c42_memory.c",
                       "             !cap_equal(&record->capability, capability) ||\n"
                       "             record->slot != slot ||\n"
                       "             memcmp(record->expected, expected, C42_CQE_BYTES) != 0)) {\n"
                       "            return C42_MEMORY_POISONED;",
                       "             !cap_equal(&record->capability, capability) ||\n"
                       "             record->slot != slot)) {\n"
                       "            return C42_MEMORY_POISONED;")],
        },
        {
            "name": "PM_CAPTURE_LAST_BYTE_CORRUPT",
            "label": "memory capture exact 64-byte output and provider effect",
            "edits": [("frontends/headless-c4/fakes/c42_memory.c",
                       "            memcpy(\n"
                       "                output, memory->sq[capability->queue_id][slot],\n"
                       "                C42_SQE_BYTES\n"
                       "            );\n"
                       "        }\n"
                       "        if (direct->apply_effect != 0) {",
                       "            memcpy(\n"
                       "                output, memory->sq[capability->queue_id][slot],\n"
                       "                C42_SQE_BYTES\n"
                       "            );\n"
                       "            output[C42_SQE_BYTES - 1u] ^= UINT8_C(1);\n"
                       "        }\n"
                       "        if (direct->apply_effect != 0) {")],
        },
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--name", action="append", default=[],
        help="run only the named provider variation (repeatable)",
    )
    arguments = parser.parse_args()
    selected = variations()
    try:
        model = build_model()
        provider_obligations = [
            obligation for obligation in model["obligations"]
            if obligation.get("model_kind") == "provider"
            and obligation.get("executor_id") == "provider_mutations"
        ]
        provider_operator_ids = {
            str(obligation["operator_id"])
            for obligation in provider_obligations
        }
    except (ModelError, OSError) as error:
        print(
            f"C4.2 provider variations: FAIL: cannot derive operators: {error}",
            file=sys.stderr,
        )
        return 2
    if arguments.name:
        requested = set(arguments.name)
        known = {str(variation["name"]) for variation in selected}
        unknown = sorted(requested - known)
        if unknown:
            print(
                "C4.2 provider variations: FAIL: unknown variation(s): "
                + ",".join(unknown), file=sys.stderr,
            )
            return 2
        selected = [
            variation for variation in selected
            if str(variation["name"]) in requested
        ]
    declared_operator_ids = {
        str(operator_id)
        for variation in selected
        for operator_id in variation.get("operator_ids", [])
    }
    unknown_operator_ids = sorted(
        declared_operator_ids - provider_operator_ids
    )
    if unknown_operator_ids:
        print(
            "C4.2 provider variations: FAIL: unknown operator canary: "
            + ",".join(unknown_operator_ids), file=sys.stderr,
        )
        return 2
    if not arguments.name and declared_operator_ids != provider_operator_ids:
        missing = sorted(provider_operator_ids - declared_operator_ids)
        print(
            "C4.2 provider variations: FAIL: operator canaries incomplete: "
            + ",".join(missing), file=sys.stderr,
        )
        return 2
    owned_by_mutant: dict[str, list[dict[str, object]]] = {}
    for obligation in provider_obligations:
        owned_by_mutant.setdefault(
            str(obligation["mutant_id"]), []
        ).append(obligation)
    known_variations = {str(variation["name"]): variation for variation in variations()}
    unknown_owned = sorted(set(owned_by_mutant) - set(known_variations))
    if unknown_owned:
        print(
            "C4.2 provider variations: FAIL: ownership references unknown "
            "variation(s): " + ",".join(unknown_owned), file=sys.stderr,
        )
        return 2
    try:
        for mutant_id, owned in owned_by_mutant.items():
            variation = known_variations[mutant_id]
            owner_operators = {
                str(obligation["operator_id"]) for obligation in owned
            }
            if owner_operators != set(variation.get("operator_ids", [])):
                raise RuntimeError(
                    f"{mutant_id} ownership/operator set differs"
                )
            changed = {str(edit[0]) for edit in variation["edits"]}
            for obligation in owned:
                if changed != set(obligation["changed_file_ids"]):
                    raise RuntimeError(
                        f"{mutant_id} ownership changed-file set differs"
                    )
            expected = {
                str(value) for obligation in owned
                for value in obligation["expected_diagnostic_ids"]
            }
            if str(variation["label"]) not in expected:
                raise RuntimeError(
                    f"{mutant_id} ownership diagnostic set differs"
                )
    except RuntimeError as error:
        print(f"C4.2 provider variations: FAIL: {error}", file=sys.stderr)
        return 2
    compilers = [name for name in ("gcc", "clang") if shutil.which(name)]
    if len(compilers) != 2:
        print("C4.2 provider variations require gcc and clang", file=sys.stderr)
        return 1
    tracked = subprocess.check_output(
        ["git", "ls-files"], cwd=ROOT, text=True
    ).splitlines()
    tracked = [name for name in tracked if (ROOT / name).is_file()]
    baseline = {name: sha256(ROOT / name) for name in tracked}
    total = 0
    executed_owned = 0
    try:
        for variation in selected:
            name = str(variation["name"])
            allowed = {str(edit[0]) for edit in variation["edits"]}
            outputs: list[str] = []
            with tempfile.TemporaryDirectory(
                    prefix=f"c42-provider-{name.lower()}-") as directory:
                mutant = Path(directory) / "repo"
                shutil.copytree(
                    ROOT, mutant,
                    ignore=shutil.ignore_patterns(
                        ".git", "build", "__pycache__", "*.pyc", "*.o"
                    ),
                )
                for relative, before, after in variation["edits"]:
                    replace_unique(mutant / relative, before, after)
                changed = {
                    relative for relative in tracked
                    if sha256(mutant / relative) != baseline[relative]
                }
                if changed != allowed:
                    raise RuntimeError(
                        f"{name} changed unexpected files: {sorted(changed ^ allowed)}"
                    )
                owned = owned_by_mutant.get(name, [])
                for compiler in compilers:
                    output = Path(directory) / f"build-{compiler}"
                    binary = output / "c42_provider_matrix"
                    built = subprocess.run(
                        [
                            "make", "-C",
                            str(mutant / "frontends/headless-c4"),
                            f"CC={compiler}", f"BUILD_DIR={output}",
                            str(binary),
                        ],
                        cwd=mutant, check=False, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        timeout=300,
                    )
                    if built.returncode != 0 or not binary.is_file():
                        raise RuntimeError(
                            f"{name}/{compiler} did not compile:\n{built.stdout}"
                        )
                    run = subprocess.run(
                        [
                            str(binary),
                            *(
                                ["--owned-case", str(variation["case_id"])]
                                if owned else []
                            ),
                        ], cwd=mutant, check=False, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        timeout=120,
                    )
                    if run.returncode != 1 or \
                            str(variation["label"]) not in run.stdout:
                        raise RuntimeError(
                            f"{name}/{compiler} escaped exact provider oracle:\n"
                            f"{run.stdout}"
                        )
                    if owned:
                        expected = {
                            str(value) for obligation in owned
                            for value in obligation["expected_diagnostic_ids"]
                        }
                        actual = failure_diagnostics(run.stdout)
                        if actual != expected:
                            raise RuntimeError(
                                f"{name}/{compiler} owner diagnostics differ: "
                                f"expected={sorted(expected)} "
                                f"actual={sorted(actual)}\n{run.stdout}"
                            )
                    outputs.append(run.stdout)
                if outputs[0] != outputs[1]:
                    raise RuntimeError(f"{name} GCC/Clang output differs")
                total += 1
                executed_owned += len(owned_by_mutant.get(name, []))
                for obligation in owned_by_mutant.get(name, []):
                    print(f"C4.2 owned kill: {obligation['id']}")
                print(f"C4.2 provider variation {name}: PASS")
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"C4.2 provider variations: FAIL: {error}", file=sys.stderr)
        return 1
    print(
        f"C4.2 provider variations: PASS variations={total} "
        f"compilers=2 binaries={total * 2} "
        f"operator_canaries={len(declared_operator_ids)} "
        f"owned_obligations={executed_owned}/"
        f"{sum(len(value) for value in owned_by_mutant.values())}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
