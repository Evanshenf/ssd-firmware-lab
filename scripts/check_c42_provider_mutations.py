#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Compile fresh C4.2 fake-provider variations and require exact event kills."""

from __future__ import annotations

import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_unique(path: Path, before: str, after: str) -> None:
    text = path.read_text(encoding="utf-8")
    if text.count(before) != 1:
        raise RuntimeError(f"non-unique provider mutation anchor: {path}")
    path.write_text(text.replace(before, after, 1), encoding="utf-8")


def variations() -> list[dict[str, object]]:
    return [
        {
            "name": "PM_BODY_RETURNS_WRONG_TOKEN",
            "label": "memory event all fields exact",
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
            "label": "consume caller output and applied effect exact",
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
                       "            *state = FWLAB_HIF_CONSUME_ABORTED;\n"
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
            "label": "memory rejects transactional OK without exact status",
            "edits": [("frontends/headless-c4/fakes/c42_memory.c",
                       "        (injection->result == C42_MEMORY_OK && output_required &&\n"
                       "         injection->write_status == 0) ||",
                       "        (0 != 0 && injection->result == C42_MEMORY_OK &&\n"
                       "         output_required && injection->write_status == 0) ||")],
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
                       "        !handle_equal(&command->handle, &record->prepared.handle) ||\n"
                       "        !origin_equal(&command->origin, &record->prepared.origin)) {",
                       "    if (record == NULL ||\n"
                       "        (0 != 0 &&\n"
                       "         record->prepare_key.client_uid != key->client_uid) ||\n"
                       "        !handle_equal(&command->handle, &record->prepared.handle) ||\n"
                       "        !origin_equal(&command->origin, &record->prepared.origin)) {")],
        },
        {
            "name": "PM_ADMIT_GENERATION_IDENTITY_REMOVED",
            "label": "mismatch admit generation query rejected unchanged",
            "edits": [("frontends/headless-c4/fakes/c42_command.c",
                       "           left->client_uid == right->client_uid &&\n"
                       "           left->generation == right->generation &&\n"
                       "           left->reserved == right->reserved;",
                       "           left->client_uid == right->client_uid &&\n"
                       "           left->reserved == right->reserved;")],
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
    compilers = [name for name in ("gcc", "clang") if shutil.which(name)]
    if len(compilers) != 2:
        print("C4.2 provider variations require gcc and clang", file=sys.stderr)
        return 1
    tracked = subprocess.check_output(
        ["git", "ls-files"], cwd=ROOT, text=True
    ).splitlines()
    baseline = {name: sha256(ROOT / name) for name in tracked}
    total = 0
    try:
        for variation in variations():
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
                        [str(binary)], cwd=mutant, check=False, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        timeout=120,
                    )
                    if run.returncode != 1 or \
                            str(variation["label"]) not in run.stdout:
                        raise RuntimeError(
                            f"{name}/{compiler} escaped exact provider oracle:\n"
                            f"{run.stdout}"
                        )
                    outputs.append(run.stdout)
                if outputs[0] != outputs[1]:
                    raise RuntimeError(f"{name} GCC/Clang output differs")
                total += 1
                print(f"C4.2 provider variation {name}: PASS")
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"C4.2 provider variations: FAIL: {error}", file=sys.stderr)
        return 1
    print(
        f"C4.2 provider variations: PASS variations={total} "
        f"compilers=2 binaries={total * 2}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
