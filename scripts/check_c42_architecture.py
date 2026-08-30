#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Enforce the C4.2 memory-queue HIF ownership and identity split."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
HIF = ROOT / "frontends/headless-c4/hif"
INCLUDE = ROOT / "include"
PRODUCTION = [
    HIF / "c42_identity.c",
    HIF / "c42_queue.c",
    HIF / "c42_publication.c",
    HIF / "c42_runtime.c",
]
PUBLIC = [
    HIF / "c42.h",
    HIF / "c42_memory_port.h",
    INCLUDE / "fwlab/contracts/hif_command_port.h",
]
FROZEN_C41 = {
    "core/c4-nvme/c41_action.c":
        "8932d127ae3603b48cfdd0143c675bff7e3fd3e062ae286308307ef4f70f3963",
    "core/c4-nvme/c41_codec.c":
        "2fb0b5acd94266f023c300e6ab120607e322c230bc52ff12674fa0fea7dc50ac",
    "core/c4-nvme/c41_profile.c":
        "b975f04eddb38a0d08c6c23c4007f8bb90ac719f5513132ebf2e38df739a82ec",
    "core/c4-nvme/fakes/c41_fake_main.c":
        "6073f06e770cc2dd94ea73bfea93525835f7ac4716e4ebba39a888de30b7fc8b",
    "core/c4-nvme/tests/model_c41.c":
        "d2f18c925c4f64384dc539db5547bab4858275d3a18bc7749370ae462956940f",
    "core/c4-nvme/tests/test_c41_portable.c":
        "392b3991fed86a55316ce3a7a3546aa186ce3cbddddac28e25ce5539ffb05b97",
    "frontends/headless-c4/c41_wire.c":
        "3b9f682bcc253cfcd4b384a29d8c279363a8203c98c5c63ee133445dc322671b",
    "frontends/headless-c4/c41_wire.h":
        "0c0f188312cb67e7f57d19697fbf32767b3dcf2b161c0670a29de8882937ebea",
    "frontends/headless-c4/fakes/c41_fake_main.c":
        "8bb93e73c7458bc066de2d2e085cd3d22b15f685232ff5e108f37b2a189e1542",
    "frontends/headless-c4/tests/fuzz_c41.c":
        "946a0cb16c2512e37bdc2cde8f858c942640f734e4fe641aaa7ca23d7624cd58",
    "frontends/headless-c4/tests/test_c41_wire.c":
        "32258f426a86bbf2d4080b7e4d3c0a2b8ebc46288dc2586071530bb116477a49",
    "include/fwlab/contracts/hif_action.h":
        "c1afc74c228f1d467671faba256d94a452bd948d1c2fe0f94765b1db3b878956",
    "include/fwlab/portable/nvme_codec.h":
        "5ae3d488412bbfd69fa8cc1f441472f0cb9d3da8b3ba833177aaeaa671f8370f",
    "include/fwlab/portable/nvme_types.h":
        "f65e9313e33206c9c98b2b485a5cae74a0523cf4062403b18658516f2e08019f",
    "docs/adr/0008-generalized-nvme-command-graph-boundary.md":
        "2ee8f526bff088d96b0c9669ae9187ecdb4c8b36f50be7439f97f50bfe8b8db0",
    "docs/legal/c4-1-source-boundary-review.md":
        "952b09bb63fbbad7d52ac2c87ac10d77ef9f441cf3ef55ebc4ec74d252dedcbf",
    "docs/results/2026-08-30-c4-1-source-profile-wire.md":
        "976749d535542c110fc4138e9c120a1e431afeb53e6af4a5b1a3de2ba388db23",
}


def run(command: list[str], timeout: int = 300) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
    )


def origin_encodes_raw(text: str) -> bool:
    return re.search(
        r"origin\.word\s*\[[01]\]\s*=\s*[^;]*\b"
        r"(?:command_id|queue_id|cid|qid|sqid|raw)\b",
        text,
    ) is not None


def hif_mints_handle(text: str) -> bool:
    return re.search(
        r"\.handle\.(?:instance_nonce|command_uid|controller_epoch|generation)"
        r"\s*=",
        text,
    ) is not None


def missing_stable_query(text: str) -> bool:
    required = (
        "prepare_query", "admit_query", "prepare_abort_query",
        "completion_release_query", "consume_query",
        "reset_quiescent", "teardown_quiescent",
    )
    required_calls = (
        "prepare_query", "admit_query", "prepare_abort_query",
        "completion_release_query", "consume_query",
    )
    return any(token not in text for token in required) or any(
        re.search(rf"ops->{token}\s*\(", text) is None
        for token in required_calls
    )


def queue_cap_as_action(text: str) -> bool:
    return re.search(
        r"c42_queue_memory_cap[^;\n]*(?:fwlab_hif_action_token)|"
        r"fwlab_hif_action_token[^;\n]*(?:c42_queue_memory_cap)",
        text,
    ) is not None or re.search(
        r"union\s*\{[^}]*c42_queue_memory_cap[^}]*"
        r"fwlab_hif_action_token[^}]*\}",
        text,
        re.DOTALL,
    ) is not None


def shared_generation(text: str) -> bool:
    return re.search(
        r"(?:ring_generation|mapping_generation)\s*=\s*[^;\n]*"
        r"(?:active_generation|handle\.generation)|"
        r"active_generation\s*=\s*[^;\n]*"
        r"(?:ring_generation|mapping_generation)",
        text,
    ) is not None


def writable_global_identity(text: str) -> bool:
    return re.search(
        r"^\s*static\s+(?!const\b)[^();\n]*\b\w*"
        r"(?:queue|origin|controller)\w*\s*"
        r"(?:=|;|\[)",
        text,
        re.MULTILINE,
    ) is not None


def context_from_intent(text: str) -> bool:
    return re.search(
        r"context\.(?:handle|origin|submission_queue_head|"
        r"submission_queue_id|command_id|phase)\s*=\s*[^;\n]*intent",
        text,
    ) is not None


def raw_in_trace(text: str) -> bool:
    return re.search(
        r"(?:printf|fprintf|puts|write)\s*\([^;\n]*"
        r"(?:raw_bytes|raw_snapshot|raw)",
        text,
    ) is not None


def replace_unique(path: Path, before: str, after: str) -> None:
    text = path.read_text(encoding="utf-8")
    count = text.count(before)
    if count != 1:
        raise RuntimeError(
            f"mutation anchor count is {count}, expected 1: {path}: {before!r}"
        )
    changed = text.replace(before, after, 1)
    if changed == text:
        raise RuntimeError(f"mutation did not change bytes: {path}")
    path.write_text(changed, encoding="utf-8")


def source_mutations() -> list[dict[str, object]]:
    return [
        {
            "name": "AM_ORIGIN_ENCODES_RAW_ID",
            "needle": "HIF origin encodes a raw queue identity",
            "edits": [("frontends/headless-c4/hif/c42_queue.c",
                       "command->origin.word[1] = origin_uid;",
                       "command->origin.word[1] = command->command_id;")],
        },
        {
            "name": "AM_HIF_MINTS_GRAPH_HANDLE",
            "needle": "HIF mints a graph-owned handle field",
            "edits": [("frontends/headless-c4/hif/c42_queue.c",
                       "    *command = local;\n    command->sq_index = sq_index;",
                       "    *command = local;\n"
                       "    command->command.handle.command_uid = 7;\n"
                       "    command->sq_index = sq_index;")],
        },
        {
            "name": "AM_PORT_ADMISSION_WITHOUT_STABLE_QUERY",
            "needle": "command port lacks a stable query/reconcile path",
            "edits": [("frontends/headless-c4/hif/c42_queue.c",
                       "controller->providers.command.ops->prepare_query(",
                       "controller->providers.command.ops->prepare_start(")],
        },
        {
            "name": "AM_QUEUE_CAP_AS_HIF_ACTION_TOKEN",
            "needle": "queue-memory cap is interchangeable with action token",
            "edits": [
                ("frontends/headless-c4/hif/c42.h",
                 "#include \"fwlab/contracts/hif_command_port.h\"",
                 "#include \"fwlab/contracts/hif_command_port.h\"\n"
                 "#include \"fwlab/contracts/hif_action.h\""),
                ("frontends/headless-c4/hif/c42.h",
                 "    struct c42_queue_memory_cap memory;",
                 "    union {\n"
                 "        struct c42_queue_memory_cap memory;\n"
                 "        struct fwlab_hif_action_token action;\n"
                 "    };")
            ],
        },
        {
            "name": "AM_SHARED_GENERATION_COUNTER",
            "needle": "generation domains are shared",
            "edits": [("frontends/headless-c4/hif/c42_queue.c",
                       "command->sq_ring_generation = sq->ring_generation;",
                       "command->sq_ring_generation = command->active_generation;")],
        },
        {
            "name": "AM_GLOBAL_QUEUE_OR_ORIGIN_STATE",
            "needle": "writable global queue/origin/controller state",
            "edits": [("frontends/headless-c4/hif/c42_queue.c",
                       "static int generation_available(uint32_t generation)\n"
                       "{\n"
                       "    return generation != 0 && generation != UINT32_MAX;\n"
                       "}",
                       "static uint32_t global_queue_state;\n\n"
                       "static int generation_available(uint32_t generation)\n"
                       "{\n"
                       "    return generation != 0 && generation != UINT32_MAX &&\n"
                       "           global_queue_state == 0;\n"
                       "}")],
        },
        {
            "name": "AM_CONTEXT_DERIVED_FROM_INTENT",
            "needle": "CQ publication context is derived from completion intent",
            "edits": [("frontends/headless-c4/hif/c42_publication.c",
                       "context.handle = command->command.handle;",
                       "context.handle = command->intent.handle;")],
        },
        {
            "name": "AM_RAW_SNAPSHOT_IN_DEFAULT_TRACE",
            "needle": "raw SQ snapshot entered a default trace/output path",
            "edits": [("frontends/headless-c4/hif/c42_runtime.c",
                       "    uint32_t index;\n\n"
                       "    if (!c42_controller_valid(controller) || handle == NULL ||",
                       "    uint32_t index;\n"
                       "    extern int puts(const char *);\n\n"
                       "    (void)puts((const char *)controller->commands[0].raw_bytes);\n"
                       "    if (!c42_controller_valid(controller) || handle == NULL ||")],
        },
    ]


def check_source_mutations() -> list[str]:
    failures: list[str] = []
    compilers = [name for name in ("gcc", "clang") if shutil.which(name)]

    if len(compilers) != 2:
        return ["architecture source mutations require gcc and clang"]
    for mutation in source_mutations():
        name = str(mutation["name"])
        try:
            with tempfile.TemporaryDirectory(
                    prefix=f"c42-architecture-{name.lower()}-") as directory:
                mutated_root = Path(directory) / "repo"
                shutil.copytree(
                    ROOT, mutated_root,
                    ignore=shutil.ignore_patterns(
                        ".git", "build", "__pycache__", "*.pyc", "*.o"
                    ),
                )
                for relative, before, after in mutation["edits"]:
                    replace_unique(mutated_root / relative, before, after)
                for compiler in compilers:
                    build = subprocess.run(
                        ["make", "-C", str(mutated_root / "frontends/headless-c4"),
                         f"CC={compiler}",
                         f"BUILD_DIR={Path(directory) / ('build-' + compiler)}",
                         "fake-link-c42"],
                        cwd=mutated_root, check=False, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        timeout=300,
                    )
                    if build.returncode != 0:
                        raise RuntimeError(
                            f"{name}/{compiler} did not compile:\n{build.stdout}"
                        )
                    child = subprocess.run(
                        ["python3",
                         str(mutated_root / "scripts/check_c42_architecture.py"),
                         "--root", str(mutated_root), "--cc", compiler,
                         "--no-mutation-selftests"],
                        cwd=mutated_root, check=False, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        timeout=600,
                    )
                    if child.returncode == 0 or str(mutation["needle"]) not in child.stdout:
                        raise RuntimeError(
                            f"{name}/{compiler} escaped expected audit:\n{child.stdout}"
                        )
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            failures.append(str(error))
    return failures


def main() -> int:
    global ROOT, HIF, INCLUDE, PRODUCTION, PUBLIC

    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--no-mutation-selftests", action="store_true")
    arguments = parser.parse_args()
    ROOT = arguments.root.resolve()
    HIF = ROOT / "frontends/headless-c4/hif"
    INCLUDE = ROOT / "include"
    PRODUCTION = [
        HIF / "c42_identity.c", HIF / "c42_queue.c",
        HIF / "c42_publication.c", HIF / "c42_runtime.c",
    ]
    PUBLIC = [
        HIF / "c42.h", HIF / "c42_memory_port.h",
        INCLUDE / "fwlab/contracts/hif_command_port.h",
    ]
    failures: list[str] = []

    for name, expected in FROZEN_C41.items():
        path = ROOT / name
        if not path.is_file():
            failures.append(f"C4.1 freeze path missing: {name}")
            continue
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != expected:
            failures.append(f"C4.1 freeze mismatch: {name}")

    production_text = "\n".join(
        path.read_text(encoding="utf-8") for path in PRODUCTION
    )
    contract_text = "\n".join(
        path.read_text(encoding="utf-8") for path in PUBLIC
    )
    command_port = (INCLUDE / "fwlab/contracts/hif_command_port.h").read_text(
        encoding="utf-8"
    )
    if origin_encodes_raw(production_text):
        failures.append("HIF origin encodes a raw queue identity")
    if hif_mints_handle(production_text):
        failures.append("HIF mints a graph-owned handle field")
    if missing_stable_query(command_port + production_text):
        failures.append("command port lacks a stable query/reconcile path")
    if queue_cap_as_action(contract_text + production_text):
        failures.append("queue-memory cap is interchangeable with action token")
    if shared_generation(production_text):
        failures.append("ring/map and active/handle generation domains are shared")
    if writable_global_identity(production_text):
        failures.append("writable global queue/origin/controller state")
    if context_from_intent(production_text):
        failures.append("CQ publication context is derived from completion intent")
    if raw_in_trace(production_text):
        failures.append("raw SQ snapshot entered a default trace/output path")
    if not arguments.no_mutation_selftests:
        failures.extend(check_source_mutations())

    forbidden_include = re.compile(
        r'^\s*#\s*include\s*[<"](?:linux/|sys/|asm/|qemu|hw/|sysemu/|'
        r'vfio|c31_|c34_|c35_|nfc/|media/)',
        re.MULTILINE | re.IGNORECASE,
    )
    if forbidden_include.search(production_text + contract_text):
        failures.append("C4.2 production has a forbidden layer/platform include")
    forbidden_runtime = re.compile(
        r"\b(?:malloc|calloc|realloc|free|pthread_create|clock_gettime|"
        r"nanosleep|open|pread|pwrite|ioctl|mmap)\s*\("
    )
    if forbidden_runtime.search(production_text):
        failures.append("C4.2 production has heap/thread/time/OS runtime use")
    if re.search(r"\b(?:qid|cid|sqid|sqhd|phase|command_id|raw_address)\b",
                 command_port, re.IGNORECASE):
        failures.append("address-free command port contains raw queue identity")
    internal = (HIF / "c42_internal.h").read_text(encoding="utf-8")
    for token in (
        "next_active_generation", "ring_generation", "mapping_generation",
        "next_candidate_generation", "next_target_generation",
    ):
        if token not in internal:
            failures.append(f"independent generation domain missing: {token}")

    try:
        with tempfile.TemporaryDirectory(prefix="c42-architecture-") as directory:
            objects: list[Path] = []
            for index, source in enumerate(PRODUCTION):
                output = Path(directory) / f"production-{index}.o"
                built = run([
                    arguments.cc, f"-I{INCLUDE}",
                    f"-I{ROOT / 'frontends/headless-c4'}",
                    "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-Wpedantic", "-fno-common", "-c", str(source),
                    "-o", str(output),
                ])
                if built.returncode:
                    failures.append(
                        f"production compile failed for {source.name}:\n{built.stdout}"
                    )
                else:
                    objects.append(output)
            if objects:
                symbols = run([
                    "nm", "-A", "--defined-only",
                    *(str(path) for path in objects),
                ])
                if symbols.returncode:
                    failures.append(f"nm failed:\n{symbols.stdout}")
                for line in symbols.stdout.splitlines():
                    if re.search(r"\s[BCDGSV]\s+\S+$", line):
                        failures.append(f"writable production global: {line}")
    except (OSError, subprocess.TimeoutExpired) as error:
        failures.append(f"C4.2 object audit failed: {error}")

    c41 = run([
        "python3", str(ROOT / "scripts/check_c4_architecture.py"),
        "--cc", arguments.cc,
    ], timeout=600)
    if c41.returncode:
        failures.append(f"C4.1/C3 coexistence failed:\n{c41.stdout}")

    if failures:
        print("C4.2 architecture isolation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    mutation_text = "source mutations skipped" if arguments.no_mutation_selftests \
                    else "8 compile-valid full-source mutations killed"
    print("C4.2 architecture isolation: PASS (C4.1 freeze; "
          f"{mutation_text}; distinct queue/graph/action identities; "
          "C3 coexistence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
