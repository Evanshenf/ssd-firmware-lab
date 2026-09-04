#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Fail closed on bounded S0 and J0 command-spine checkpoints."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import stat
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "core" / "command-spine"
BASE = "8002355dd43a36a085df864174613cadd508021a"
S0A_CHECKPOINT = "fa40ccac01c76f75e9a8c536000db087aa619f8a"
S0A_PATH_MANIFEST = \
    "601ac20de15d91c9c9e0ba0afb111cae617a5b21d70ca3435a70e87d65eab2a0"
S0A_CONTENT_MANIFEST = \
    "d420e88da5806c2bc2fd39e246f600d5571e00eafafcbd949b0044d7c38059fb"
S0B_CHECKPOINT = "924007d3ec185ca641ebab8b3129466203952c43"
S0B_PATH_MANIFEST = \
    "215c94614033a596c03837568e10425c45456d8af364ed31eda6eadd76854d73"
S0B_CONTENT_MANIFEST = \
    "90de973eb65bda98867c725a586e7fcf328f4626990d268105ab85fa1b8addad"
J0_COMPONENT = ROOT / "frontends" / "headless-j0"

J0A_NEW_PATHS = [
    "core/m3p/fakes/m3p_fake_adjacent.c",
    "core/m3p/fakes/m3p_fake_adjacent.h",
    "core/m3p/m3p.h",
    "core/m3p/m3p_codec.c",
    "core/m3p/m3p_gc.c",
    "core/m3p/m3p_internal.h",
    "core/m3p/m3p_mapping.c",
    "core/m3p/m3p_nfc.c",
    "core/m3p/m3p_recovery.c",
    "core/m3p/m3p_runtime.c",
    "core/m3p/tests/test_j0a_lower.c",
    "frontends/headless-j0/Makefile",
    "frontends/headless-j0/j0.mk",
    "media/file-nand-v0/file_nand.h",
    "media/file-nand-v0/file_nand_codec.c",
    "media/file-nand-v0/file_nand_engine.c",
    "media/file-nand-v0/file_nand_internal.h",
    "media/file-nand-v0/file_nand_media.c",
    "media/file-nand-v0/file_nand_posix.c",
]
J0A_MODIFIED_PATHS = [
    "scripts/check_repo_policy.py",
    "scripts/check_spine_architecture.py",
]
J0A_PATHS = sorted([*J0A_NEW_PATHS, *J0A_MODIFIED_PATHS])

J0_M3P_SOURCES = [
    "../../core/m3p/m3p_codec.c",
    "../../core/m3p/m3p_mapping.c",
    "../../core/m3p/m3p_nfc.c",
    "../../core/m3p/m3p_gc.c",
    "../../core/m3p/m3p_recovery.c",
    "../../core/m3p/m3p_runtime.c",
]
J0_M3P_MEMBERS = [
    "m3p_codec.o", "m3p_mapping.o", "m3p_nfc.o", "m3p_gc.o",
    "m3p_recovery.o", "m3p_runtime.o",
]
J0_NFC_SOURCES = [
    "../../nfc/nfc_model.c", "../../nfc/nfc_scheduler.c",
    "../../nfc/nfc_fault.c", "../../nfc/nfc_media.c",
]
J0_NFC_MEMBERS = [
    "nfc_model.o", "nfc_scheduler.o", "nfc_fault.o", "nfc_media.o",
]
J0_FILE_SOURCES = [
    "../../media/file-nand-v0/file_nand_codec.c",
    "../../media/file-nand-v0/file_nand_engine.c",
    "../../media/file-nand-v0/file_nand_media.c",
    "../../media/file-nand-v0/file_nand_posix.c",
]
J0_FILE_MEMBERS = [
    "file_nand_codec.o", "file_nand_engine.o", "file_nand_media.o",
    "file_nand_posix.o",
]
J0A_M3P_OBJECTS = [f"build/j0a/{member}" for member in J0_M3P_MEMBERS]
J0A_NFC_OBJECTS = [f"build/j0a/{member}" for member in J0_NFC_MEMBERS]
J0A_FILE_OBJECTS = [f"build/j0a/{member}" for member in J0_FILE_MEMBERS]
J0A_FAKE_OBJECT = "build/j0a/m3p_fake_adjacent.o"
J0A_TEST_OBJECT = "build/j0a/test_j0a_lower.o"
J0A_ARCHIVES = [
    "build/j0a/libfwlab_m3p_v0.a",
    "build/j0a/libfwlab_nfc_v1.a",
    "build/j0a/libfwlab_file_nand_v0.a",
]
J0A_MATRIX = "build/j0a/j0a_lower_matrix"
J0A_INTERMEDIATES = [
    *J0A_M3P_OBJECTS, *J0A_NFC_OBJECTS, *J0A_FILE_OBJECTS,
    J0A_FAKE_OBJECT, J0A_TEST_OBJECT, *J0A_ARCHIVES,
]
J0A_OWNED = [*J0A_INTERMEDIATES, J0A_MATRIX]
J0_FORBIDDEN_MEMBERS = [
    "nfc_codec.o", "nfc_adapter.o", "nfc_fake_main.o",
    "nfc_memory_media.o", "spine_fake_adjacent.o",
    "tiny_profile_fixture.o", "m4_frontend.o", "m4_nvme.o",
    "m4_media_fixture.o",
]

PUBLIC_HEADERS = [
    "include/fwlab/portable/host_action_program_v0.h",
    "include/fwlab/contracts/controller_buffer_v0.h",
    "include/fwlab/contracts/host_data_v0.h",
    "include/fwlab/contracts/block_service_v0.h",
    "include/fwlab/contracts/owner_control_v0.h",
]
COMPONENT_PATHS = [
    "core/command-spine/Makefile",
    "core/command-spine/README.md",
    "core/command-spine/fakes/s0a_fake_main.c",
    "core/command-spine/spine.mk",
    "core/command-spine/spine_anchor_internal.h",
    "core/command-spine/spine_construction.c",
    "core/command-spine/spine_contracts.c",
    "core/command-spine/tests/negative_token_substitution.c",
    "core/command-spine/tests/test_s0a_contracts.c",
]
CHECKPOINT_PATHS = sorted([
    *PUBLIC_HEADERS,
    *COMPONENT_PATHS,
    "scripts/check_spine_architecture.py",
])

S0B_ADDED_PATHS = [
    "core/command-spine/fakes/spine_fake_adjacent.c",
    "core/command-spine/fakes/spine_fake_adjacent.h",
    "core/command-spine/profiles/c43_p1_adapter.c",
    "core/command-spine/profiles/linux_profile_v1_adapter.c",
    "core/command-spine/spine_internal.h",
    "core/command-spine/spine_lifecycle.c",
    "core/command-spine/tests/test_s0b_lifecycle.c",
    "core/command-spine/tests/tiny_profile_fixture.c",
]
S0B_MODIFIED_PATHS = [
    "core/command-spine/Makefile",
    "core/command-spine/README.md",
    "core/command-spine/spine.mk",
    "scripts/check_spine_architecture.py",
]
S0B_TRANSITION_PATHS = sorted([*S0B_ADDED_PATHS, *S0B_MODIFIED_PATHS])
S0A_IMMUTABLE_PATHS = sorted(
    set(CHECKPOINT_PATHS) - set(S0B_MODIFIED_PATHS)
)

EXPECTED_SOURCES = ["spine_contracts.c", "spine_construction.c"]
EXPECTED_MEMBERS = ["spine_contracts.o", "spine_construction.o"]
EXPECTED_OBJECTS = [
    "build/s0a/spine_contracts.o",
    "build/s0a/spine_construction.o",
]
PUBLIC_TEST_SOURCE = ["tests/test_s0a_contracts.c"]
FAKE_SOURCE = ["fakes/s0a_fake_main.c"]
NEGATIVE_SOURCE = ["tests/negative_token_substitution.c"]
PUBLIC_TEST_OBJECT = ["build/s0a/test_s0a_contracts.o"]
FAKE_OBJECT = ["build/s0a/s0a_fake_main.o"]
ARCHIVE_PATH = ["build/s0a/libfwlab_spine_contracts_v0.a"]
PUBLIC_PATH = ["build/s0a/s0a_public_contracts"]
FAKE_PATH = ["build/s0a/s0a_fake_adjacent_link"]
EXPECTED_MEMBER_SYMBOLS = {
    "spine_contracts.o": [
        "fwlab_host_action_program_v0_valid",
        "fwlab_host_completion_intent_v0_valid_for_program",
        "fwlab_controller_buffer_port_v0_valid",
        "fwlab_host_data_port_v0_valid",
        "fwlab_block_service_v0_valid",
        "fwlab_owner_control_port_v0_valid",
    ],
    "spine_construction.o": [
        "fwlab_spine_construction_valid",
        "fwlab_spine_construction_v0",
    ],
}
EXPECTED_HEADERS = [
    "../../include/fwlab/portable/host_action_program_v0.h",
    "../../include/fwlab/contracts/controller_buffer_v0.h",
    "../../include/fwlab/contracts/host_data_v0.h",
    "../../include/fwlab/contracts/block_service_v0.h",
    "../../include/fwlab/contracts/owner_control_v0.h",
]
ANCHORS = [
    "fwlab_authoritative_sq_consumer_v0",
    "fwlab_authoritative_cqe_publisher_v0",
]
ANCHOR_OBJECT_TYPE = "R"
AUTHORITATIVE_TARGET = ["ssd_fwlab_m4_spine_v0"]
AUTHORITATIVE_MEMBERS = [
    "m4_main.o",
    "m4_pci_transport.o",
    "linux_hif_binding_v0.o",
    "m4_authoritative_construct.o",
]
FIXTURE_TARGET = ["ssd_fwlab_m4_poc_fixture"]
FIXTURE_MEMBERS = [
    "fixture/m4_pci_main.o",
    "fixture/m4_pci.o",
    "fixture/m4_frontend.o",
    "fixture/m4_nvme.o",
    "fixture/m4_media_fixture.o",
]
LEGACY_MEMBERS = [
    "fixture/m4_frontend.o",
    "fixture/m4_nvme.o",
    "fixture/m4_media_fixture.o",
]
FORBIDDEN_SYMBOLS = [
    "fwlab_m4_frontend_",
    "fwlab_m4_nvme_fixture_ops",
    "fwlab_m4_media_fixture_create",
    "fwlab_m4_media_read",
    "fwlab_m4_media_write",
    "fwlab_m4_media_flush",
    "nvme_mode",
]

S0B_LIFECYCLE_SOURCES = ["spine_contracts.c", "spine_lifecycle.c"]
S0B_LIFECYCLE_MEMBERS = ["spine_contracts.o", "spine_lifecycle.o"]
S0B_LIFECYCLE_OBJECTS = [
    "build/s0b/spine_contracts.o",
    "build/s0b/spine_lifecycle.o",
]
S0B_PROFILE_SOURCES = [
    "profiles/c43_p1_adapter.c",
    "profiles/linux_profile_v1_adapter.c",
]
S0B_PROFILE_MEMBERS = ["c43_p1_adapter.o", "linux_profile_v1_adapter.o"]
S0B_PROFILE_OBJECTS = [
    "build/s0b/c43_p1_adapter.o",
    "build/s0b/linux_profile_v1_adapter.o",
]
S0B_INTERMEDIATES = [
    *S0B_LIFECYCLE_OBJECTS,
    *S0B_PROFILE_OBJECTS,
    "build/s0b/c41_codec.o",
    "build/s0b/spine_fake_adjacent.o",
    "build/s0b/tiny_profile_fixture.o",
    "build/s0b/test_s0b_lifecycle.o",
]
S0B_ARTIFACTS = [
    "build/s0b/libfwlab_spine_lifecycle_v0.a",
    "build/s0b/libfwlab_spine_profiles_v0.a",
    "build/s0b/s0b_profile_matrix",
]
S0B_OWNED = [*S0B_INTERMEDIATES, *S0B_ARTIFACTS]
EXPECTED_MANIFEST = {
    "SPINE_S0A_BUILD_DIR": ["build/s0a"],
    "SPINE_S0A_CONTRACT_SOURCES": EXPECTED_SOURCES,
    "SPINE_S0A_CONTRACT_MEMBERS": EXPECTED_MEMBERS,
    "SPINE_S0A_CONTRACT_OBJECTS": EXPECTED_OBJECTS,
    "SPINE_S0A_PUBLIC_TEST_SOURCE": PUBLIC_TEST_SOURCE,
    "SPINE_S0A_FAKE_SOURCE": FAKE_SOURCE,
    "SPINE_S0A_NEGATIVE_SOURCE": NEGATIVE_SOURCE,
    "SPINE_S0A_PUBLIC_TEST_OBJECT": PUBLIC_TEST_OBJECT,
    "SPINE_S0A_FAKE_OBJECT": FAKE_OBJECT,
    "SPINE_S0A_ARCHIVE": ARCHIVE_PATH,
    "SPINE_S0A_PUBLIC_CONTRACTS": PUBLIC_PATH,
    "SPINE_S0A_FAKE_ADJACENT": FAKE_PATH,
    "SPINE_S0A_PUBLIC_HEADERS": EXPECTED_HEADERS,
    "SPINE_S0A_ANCHOR_HEADER": ["spine_anchor_internal.h"],
    "SPINE_S0A_ANCHOR_SYMBOLS": ANCHORS,
    "SPINE_S0B_BUILD_DIR": ["build/s0b"],
    "SPINE_S0B_LIFECYCLE_SOURCES": S0B_LIFECYCLE_SOURCES,
    "SPINE_S0B_LIFECYCLE_MEMBERS": S0B_LIFECYCLE_MEMBERS,
    "SPINE_S0B_LIFECYCLE_OBJECTS": S0B_LIFECYCLE_OBJECTS,
    "SPINE_S0B_PROFILE_SOURCES": S0B_PROFILE_SOURCES,
    "SPINE_S0B_PROFILE_MEMBERS": S0B_PROFILE_MEMBERS,
    "SPINE_S0B_PROFILE_OBJECTS": S0B_PROFILE_OBJECTS,
    "SPINE_S0B_C41_SOURCE": ["../c4-nvme/c41_codec.c"],
    "SPINE_S0B_C41_OBJECT": ["build/s0b/c41_codec.o"],
    "SPINE_S0B_FAKE_SOURCE": ["fakes/spine_fake_adjacent.c"],
    "SPINE_S0B_FAKE_OBJECT": ["build/s0b/spine_fake_adjacent.o"],
    "SPINE_S0B_TINY_SOURCE": ["tests/tiny_profile_fixture.c"],
    "SPINE_S0B_TINY_OBJECT": ["build/s0b/tiny_profile_fixture.o"],
    "SPINE_S0B_MATRIX_SOURCE": ["tests/test_s0b_lifecycle.c"],
    "SPINE_S0B_MATRIX_OBJECT": ["build/s0b/test_s0b_lifecycle.o"],
    "SPINE_S0B_INTERMEDIATES": S0B_INTERMEDIATES,
    "SPINE_S0B_LIFECYCLE_ARCHIVE": [S0B_ARTIFACTS[0]],
    "SPINE_S0B_PROFILE_ARCHIVE": [S0B_ARTIFACTS[1]],
    "SPINE_S0B_MATRIX": [S0B_ARTIFACTS[2]],
    "SPINE_S0B_ARTIFACTS": S0B_ARTIFACTS,
    "SPINE_S0B_OWNED": S0B_OWNED,
    "SPINE_M4_AUTHORITATIVE_TARGET": AUTHORITATIVE_TARGET,
    "SPINE_M4_AUTHORITATIVE_MEMBERS": AUTHORITATIVE_MEMBERS,
    "SPINE_M4_FIXTURE_TARGET": FIXTURE_TARGET,
    "SPINE_M4_FIXTURE_MEMBERS": FIXTURE_MEMBERS,
    "SPINE_M4_LEGACY_FIXTURE_MEMBERS": LEGACY_MEMBERS,
}

EXACT_INCLUDES = {
    "include/fwlab/portable/host_action_program_v0.h": {
        "stdint.h",
        "fwlab/portable/nvme_types.h",
    },
    "include/fwlab/contracts/controller_buffer_v0.h": {
        "stddef.h",
        "stdint.h",
        "fwlab/portable/nvme_types.h",
    },
    "include/fwlab/contracts/host_data_v0.h": {
        "stdint.h",
        "fwlab/contracts/controller_buffer_v0.h",
        "fwlab/portable/host_action_program_v0.h",
    },
    "include/fwlab/contracts/block_service_v0.h": {
        "stdint.h",
        "fwlab/contracts/controller_buffer_v0.h",
        "fwlab/portable/host_action_program_v0.h",
    },
    "include/fwlab/contracts/owner_control_v0.h": {
        "stdint.h",
        "fwlab/portable/host_action_program_v0.h",
    },
    "core/command-spine/spine_anchor_internal.h": {"stdint.h"},
    "core/command-spine/spine_contracts.c": {
        "stddef.h",
        "stdint.h",
        "string.h",
        "fwlab/contracts/block_service_v0.h",
        "fwlab/contracts/host_data_v0.h",
        "fwlab/contracts/owner_control_v0.h",
        "fwlab/portable/host_action_program_v0.h",
    },
    "core/command-spine/spine_construction.c": {
        "spine_anchor_internal.h",
    },
    "core/command-spine/fakes/s0a_fake_main.c": {
        "stdio.h",
        "string.h",
        "spine_anchor_internal.h",
        "fwlab/contracts/block_service_v0.h",
        "fwlab/contracts/controller_buffer_v0.h",
        "fwlab/contracts/host_data_v0.h",
        "fwlab/contracts/owner_control_v0.h",
    },
    "core/command-spine/tests/test_s0a_contracts.c": {
        "stdio.h",
        "string.h",
        "fwlab/contracts/block_service_v0.h",
        "fwlab/contracts/controller_buffer_v0.h",
        "fwlab/contracts/host_data_v0.h",
        "fwlab/contracts/owner_control_v0.h",
        "fwlab/portable/host_action_program_v0.h",
    },
    "core/command-spine/tests/negative_token_substitution.c": {
        "spine_anchor_internal.h",
        "fwlab/contracts/controller_buffer_v0.h",
        "fwlab/contracts/hif_action.h",
        "fwlab/contracts/host_data_v0.h",
    },
    "core/command-spine/spine_internal.h": {
        "stddef.h",
        "stdint.h",
        "fwlab/portable/host_action_program_v0.h",
    },
    "core/command-spine/spine_lifecycle.c": {
        "spine_internal.h",
        "stddef.h",
        "stdint.h",
        "string.h",
    },
    "core/command-spine/profiles/c43_p1_adapter.c": {
        "spine_internal.h",
        "stddef.h",
        "stdint.h",
        "string.h",
        "fwlab/portable/nvme_codec.h",
    },
    "core/command-spine/profiles/linux_profile_v1_adapter.c": {
        "spine_internal.h",
        "stddef.h",
        "stdint.h",
        "string.h",
        "fwlab/portable/nvme_codec.h",
    },
    "core/command-spine/fakes/spine_fake_adjacent.h": {
        "stddef.h",
        "stdint.h",
        "spine_internal.h",
    },
    "core/command-spine/fakes/spine_fake_adjacent.c": {
        "fakes/spine_fake_adjacent.h",
        "stddef.h",
        "stdint.h",
        "string.h",
    },
    "core/command-spine/tests/tiny_profile_fixture.c": {
        "fakes/spine_fake_adjacent.h",
        "stddef.h",
        "stdint.h",
        "string.h",
        "fwlab/portable/nvme_codec.h",
    },
    "core/command-spine/tests/test_s0b_lifecycle.c": {
        "fakes/spine_fake_adjacent.h",
        "stddef.h",
        "stdint.h",
        "stdio.h",
        "string.h",
    },
}

INCLUDE_DIRECTIVE_RE = re.compile(
    r"^\s*#\s*(include|include_next)\b(.*)$", re.MULTILINE
)
LITERAL_INCLUDE_RE = re.compile(r'^\s*(?:<([^>]+)>|"([^"]+)")\s*$')
MANIFEST_ASSIGNMENT_RE = re.compile(
    r"^[ \t]*(?P<override>override[ \t]+)?"
    r"(?P<name>SPINE_[A-Za-z0-9_]+)[ \t]*"
    r"(?P<operator>[:+?!]*=)",
    re.MULTILINE,
)
SOURCE_INPUT_SUFFIXES = {
    ".asm", ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp",
    ".hxx", ".inc", ".inl", ".ipp", ".mk", ".s", ".tcc",
}


ACTIVE_PHASE = "s0a"


def fail(message: str) -> None:
    print(f"{ACTIVE_PHASE.upper()} architecture failed: {message}",
          file=sys.stderr)
    raise SystemExit(1)


def git_bytes(*arguments: str) -> bytes:
    return subprocess.run(
        ["git", "-C", str(ROOT), *arguments],
        check=True,
        stdout=subprocess.PIPE,
    ).stdout


def s0a_checkpoint_identity() -> None:
    paths = git_bytes(
        "diff", "--name-only", "--no-renames", BASE, S0A_CHECKPOINT, "--"
    ).decode("utf-8").splitlines()
    paths = sorted(path for path in paths if path)
    path_bytes = "".join(f"{path}\n" for path in paths).encode("utf-8")
    if hashlib.sha256(path_bytes).hexdigest() != S0A_PATH_MANIFEST:
        fail("S0-A checkpoint path manifest does not reconstruct")
    content = bytearray()
    for path in paths:
        digest = hashlib.sha256(
            git_bytes("show", f"{S0A_CHECKPOINT}:{path}")
        ).hexdigest()
        content.extend(f"{digest}  {path}\n".encode("utf-8"))
    if hashlib.sha256(content).hexdigest() != S0A_CONTENT_MANIFEST:
        fail("S0-A checkpoint content manifest does not reconstruct")


def check_regular_paths(paths: list[str]) -> None:
    for path in paths:
        candidate = ROOT / path
        try:
            mode = candidate.lstat().st_mode
        except OSError as error:
            fail(f"checkpoint path cannot be inspected: {path}: {error}")
        if candidate.is_symlink() or not stat.S_ISREG(mode):
            fail(f"checkpoint path is not a regular non-symlink file: {path}")


def check_ignored(allowed_relative: list[str], label: str) -> None:
    ignored = git_bytes(
        "ls-files", "--others", "--ignored", "--exclude-standard", "-z",
        "--", "core/command-spine",
    ).decode("utf-8").split("\0")
    allowed = {
        f"core/command-spine/{path}" for path in allowed_relative
    }
    unexpected = sorted(path for path in ignored if path and path not in allowed)
    if unexpected:
        source_inputs = [
            path for path in unexpected
            if Path(path).suffix.lower() in SOURCE_INPUT_SUFFIXES or
            Path(path).name in {"Makefile", "Kbuild", "meson.build"}
        ]
        if source_inputs:
            fail(f"ignored non-artifact source inputs: {source_inputs}")
        fail(f"unexpected ignored {label} build inputs/artifacts: {unexpected}")


def changed_paths(phase: str) -> list[str]:
    if subprocess.run(
        ["git", "-C", str(ROOT), "merge-base", "--is-ancestor",
         S0A_CHECKPOINT, "HEAD"],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ).returncode:
        fail(f"HEAD is not descended from S0-A checkpoint {S0A_CHECKPOINT}")
    s0a_checkpoint_identity()
    for path in S0A_IMMUTABLE_PATHS:
        if (ROOT / path).read_bytes() != git_bytes(
                "show", f"{S0A_CHECKPOINT}:{path}"):
            fail(f"immutable S0-A leaf differs from {S0A_CHECKPOINT}: {path}")
    if phase == "s0a":
        check_regular_paths(CHECKPOINT_PATHS)
        allowed_s0a = [
            *EXPECTED_OBJECTS,
            PUBLIC_TEST_OBJECT[0],
            FAKE_OBJECT[0],
            ARCHIVE_PATH[0],
            PUBLIC_PATH[0],
            FAKE_PATH[0],
        ]
        check_ignored(allowed_s0a, "S0-A")
        return CHECKPOINT_PATHS

    if subprocess.run(
        ["git", "-C", str(ROOT), "merge-base", "--is-ancestor",
         S0B_CHECKPOINT, "HEAD"], check=False,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode == 0:
        s0b_checkpoint_identity()
        for path in S0B_TRANSITION_PATHS:
            if path == "scripts/check_spine_architecture.py":
                continue
            if (ROOT / path).read_bytes() != git_bytes(
                    "show", f"{S0B_CHECKPOINT}:{path}"):
                fail(f"frozen S0-B leaf differs from {S0B_CHECKPOINT}: {path}")
        check_regular_paths(S0B_TRANSITION_PATHS)
        check_ignored(S0B_OWNED, "S0-B")
        return S0B_TRANSITION_PATHS

    tracked = git_bytes(
        "diff", "--name-only", "--no-renames", "-z", S0A_CHECKPOINT, "--"
    ).decode("utf-8").split("\0")
    untracked = git_bytes(
        "ls-files", "--others", "--exclude-standard", "-z"
    ).decode("utf-8").split("\0")
    paths = sorted(set(path for path in [*tracked, *untracked] if path))
    if paths != S0B_TRANSITION_PATHS:
        missing = sorted(set(S0B_TRANSITION_PATHS) - set(paths))
        extra = sorted(set(paths) - set(S0B_TRANSITION_PATHS))
        fail(f"S0-B transition path manifest differs: "
             f"missing={missing} extra={extra}")
    check_regular_paths(paths)
    for path in S0B_ADDED_PATHS:
        if subprocess.run(
            ["git", "-C", str(ROOT), "cat-file", "-e",
             f"{S0A_CHECKPOINT}:{path}"], check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode == 0:
            fail(f"S0-B added path existed at the exact base: {path}")
    for path in S0B_MODIFIED_PATHS:
        if subprocess.run(
            ["git", "-C", str(ROOT), "cat-file", "-e",
             f"{S0A_CHECKPOINT}:{path}"], check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode != 0:
            fail(f"S0-B modified authority absent at exact base: {path}")
    check_ignored(S0B_OWNED, "S0-B")
    return paths


def make_words(text: str, variable: str) -> list[str]:
    match = re.search(
        rf"^override {re.escape(variable)}\s*:=\s*(.*?)"
        rf"(?=^override [A-Z0-9_]+\s*:=|\Z)",
        text,
        re.MULTILINE | re.DOTALL,
    )
    if match is None:
        fail(f"manifest declaration missing: {variable}")
    value = re.sub(r"(?m)#.*$", "", match.group(1))
    value = value.replace("\\\n", " ")
    return value.split()


def check_manifest() -> None:
    manifest = (COMPONENT / "spine.mk").read_text(encoding="utf-8")
    assignments = list(MANIFEST_ASSIGNMENT_RE.finditer(manifest))
    counts: dict[str, int] = {}

    for assignment in assignments:
        name = assignment.group("name")
        counts[name] = counts.get(name, 0) + 1
        if (assignment.group("override") or "").strip() != "override" or \
                assignment.group("operator") != ":=":
            fail(f"manifest assignment is not fixed override ':=': {name}")
    unknown = sorted(set(counts) - set(EXPECTED_MANIFEST))
    missing = sorted(set(EXPECTED_MANIFEST) - set(counts))
    duplicate = sorted(name for name, count in counts.items() if count != 1)
    if unknown or missing or duplicate:
        fail(
            "manifest assignment set differs: "
            f"unknown={unknown} missing={missing} duplicate={duplicate}"
        )
    for variable, words in EXPECTED_MANIFEST.items():
        actual = make_words(manifest, variable)
        if actual != words:
            fail(f"{variable} differs: {actual}")

    sources = make_words(manifest, "SPINE_S0A_CONTRACT_SOURCES")
    derived_members = [Path(source).with_suffix(".o").name
                       for source in sources]
    if derived_members != make_words(
            manifest, "SPINE_S0A_CONTRACT_MEMBERS"):
        fail("contract archive members are not derived from ordered sources")
    build_dir = make_words(manifest, "SPINE_S0A_BUILD_DIR")
    if len(build_dir) != 1:
        fail("build directory must be one direct path")
    derived_objects = [f"{build_dir[0]}/{member}"
                       for member in derived_members]
    if derived_objects != make_words(
            manifest, "SPINE_S0A_CONTRACT_OBJECTS"):
        fail("contract objects are not derived from source/member identity")
    derived_test_object = [
        f"{build_dir[0]}/{Path(PUBLIC_TEST_SOURCE[0]).with_suffix('.o').name}"
    ]
    derived_fake_object = [
        f"{build_dir[0]}/{Path(FAKE_SOURCE[0]).with_suffix('.o').name}"
    ]
    if derived_test_object != PUBLIC_TEST_OBJECT or \
            derived_fake_object != FAKE_OBJECT:
        fail("test/fake object identity is not source-derived")
    for variable in (
        "SPINE_S0A_CONTRACT_OBJECTS",
        "SPINE_S0A_PUBLIC_TEST_OBJECT",
        "SPINE_S0A_FAKE_OBJECT",
        "SPINE_S0A_ARCHIVE",
        "SPINE_S0A_PUBLIC_CONTRACTS",
        "SPINE_S0A_FAKE_ADJACENT",
    ):
        if any("$(" in word or not word.startswith("build/s0a/")
               for word in make_words(manifest, variable)):
            fail(f"{variable} is not an exact direct artifact path")

    s0b_build = make_words(manifest, "SPINE_S0B_BUILD_DIR")
    if s0b_build != ["build/s0b"]:
        fail("S0-B build directory differs")
    lifecycle_sources = make_words(
        manifest, "SPINE_S0B_LIFECYCLE_SOURCES"
    )
    lifecycle_members = [
        Path(source).with_suffix(".o").name for source in lifecycle_sources
    ]
    if lifecycle_members != S0B_LIFECYCLE_MEMBERS:
        fail("S0-B lifecycle members are not source-derived")
    profile_sources = make_words(manifest, "SPINE_S0B_PROFILE_SOURCES")
    profile_members = [
        Path(source).with_suffix(".o").name for source in profile_sources
    ]
    if profile_members != S0B_PROFILE_MEMBERS:
        fail("S0-B profile members are not source-derived")
    derived_s0b_objects = [
        f"build/s0b/{member}"
        for member in [*lifecycle_members, *profile_members]
    ]
    if derived_s0b_objects != [
            *S0B_LIFECYCLE_OBJECTS, *S0B_PROFILE_OBJECTS]:
        fail("S0-B archive objects are not source/member-derived")
    for variable in (
        "SPINE_S0B_LIFECYCLE_OBJECTS",
        "SPINE_S0B_PROFILE_OBJECTS",
        "SPINE_S0B_C41_OBJECT",
        "SPINE_S0B_FAKE_OBJECT",
        "SPINE_S0B_TINY_OBJECT",
        "SPINE_S0B_MATRIX_OBJECT",
        "SPINE_S0B_INTERMEDIATES",
        "SPINE_S0B_LIFECYCLE_ARCHIVE",
        "SPINE_S0B_PROFILE_ARCHIVE",
        "SPINE_S0B_MATRIX",
        "SPINE_S0B_ARTIFACTS",
        "SPINE_S0B_OWNED",
    ):
        words = make_words(manifest, variable)
        if any("$(" in word or not word.startswith("build/s0b/")
               for word in words) or len(words) != len(set(words)):
            fail(f"{variable} is not a unique direct S0-B path list")
    if set(S0B_LIFECYCLE_MEMBERS) & set(S0B_PROFILE_MEMBERS):
        fail("S0-B archive member lists overlap")
    if S0B_OWNED != [*S0B_INTERMEDIATES, *S0B_ARTIFACTS]:
        fail("S0-B owned set is not intermediates followed by artifacts")
    if "$(wildcard" in manifest or re.search(r"(?m)^\s*[A-Z0-9_]+\s*\?=", manifest):
        fail("construction manifest contains a wildcard or overridable binding")
    if set(AUTHORITATIVE_MEMBERS) & set(FIXTURE_MEMBERS):
        fail("authoritative and fixture member lists overlap")
    if not set(LEGACY_MEMBERS) <= set(FIXTURE_MEMBERS):
        fail("fixture list does not contain every legacy member")
    if set(LEGACY_MEMBERS) & set(AUTHORITATIVE_MEMBERS):
        fail("legacy member occurs in authoritative membership")

    makefile = (COMPONENT / "Makefile").read_text(encoding="utf-8")
    if makefile.count("include spine.mk") != 1:
        fail("component Makefile must include spine.mk exactly once")
    for literal in [*EXPECTED_SOURCES, *ANCHORS, *AUTHORITATIVE_MEMBERS,
                    *FIXTURE_MEMBERS]:
        if literal in makefile:
            fail(f"component Makefile duplicates manifest literal: {literal}")
    fresh = re.search(
        r"(?m)^check-s0a:\s*$\n"
        r"\t\$\(MAKE\) --no-print-directory clean-s0a-owned\n"
        r"\t\$\(MAKE\) --no-print-directory check-s0a-owned$",
        makefile,
    )
    if fresh is None or makefile.count("check-s0a:") != 1:
        fail("check-s0a does not perform one fresh owned-artifact rebuild")
    fresh_s0b = re.search(
        r"(?m)^check-s0b:\s*$\n"
        r"\t\$\(MAKE\) --no-print-directory "
        r"clean-s0a-owned clean-s0b-owned\n"
        r"\t\$\(MAKE\) --no-print-directory check-s0b-owned$",
        makefile,
    )
    if fresh_s0b is None or makefile.count("check-s0b:") != 1:
        fail("check-s0b does not perform the exact fresh owned build")
    if makefile.count("--phase s0b") != 1 or \
            "check-s0b-owned: check-s0b-architecture" not in makefile:
        fail("S0-B gate is not bound to the one literal checker phase")
    for literal in [*S0B_LIFECYCLE_SOURCES, *S0B_PROFILE_SOURCES,
                    "../c4-nvme/c41_codec.c",
                    "fakes/spine_fake_adjacent.c",
                    "tests/tiny_profile_fixture.c",
                    "tests/test_s0b_lifecycle.c"]:
        if literal in makefile:
            fail(f"component Makefile duplicates S0-B manifest literal: "
                 f"{literal}")
    for required in (
        'grep -Eiq "undefined reference to .*$$anchor"',
        'grep -Eiq "multiple definition of .*$$anchor"',
        "missing-anchor link failed for a second class",
        "duplicate-anchor link failed for a second class",
    ):
        if required not in makefile:
            fail(f"exact anchor-link failure classification is absent: "
                 f"{required}")


def includes(path: Path) -> set[str]:
    text = path.read_text(encoding="utf-8")
    imported: list[str] = []

    for directive in INCLUDE_DIRECTIVE_RE.finditer(text):
        kind = directive.group(1)
        body = directive.group(2)
        match = LITERAL_INCLUDE_RE.fullmatch(body)
        if kind != "include" or match is None:
            line = text.count("\n", 0, directive.start()) + 1
            fail(f"unparsed or macro include directive: {path}:{line}")
        imported.append(match.group(1) or match.group(2))
    if len(imported) != len(set(imported)):
        fail(f"duplicate include directive: {path.relative_to(ROOT)}")
    return set(imported)


def check_imports() -> None:
    for relative, expected in EXACT_INCLUDES.items():
        path = ROOT / relative
        actual = includes(path)
        if actual != expected:
            missing = sorted(expected - actual)
            extra = sorted(actual - expected)
            fail(f"include allowlist differs in {relative}: "
                 f"missing={missing} extra={extra}")

    for relative in PUBLIC_HEADERS:
        path = ROOT / relative
        text = path.read_text(encoding="utf-8")
        if "__attribute__((packed))" in text or "#pragma pack" in text:
            fail(f"packed public ABI in {relative}")
        if re.search(r"\b(?:register|unregister)\s*\(", text):
            fail(f"runtime registration API in {relative}")

    block = ROOT / "include/fwlab/contracts/block_service_v0.h"
    buffer = ROOT / "include/fwlab/contracts/controller_buffer_v0.h"
    if "fwlab/contracts/host_data_v0.h" in includes(block):
        fail("block service imports Host-DMA")
    if "fwlab/contracts/host_data_v0.h" in includes(buffer):
        fail("controller-buffer contract imports Host-DMA")

    for relative in [*PUBLIC_HEADERS, "core/command-spine/spine_contracts.c"]:
        for imported in includes(ROOT / relative):
            lowered = imported.lower()
            for fragment in (
                "linux/", "vfio", "qemu", "libvfio", "c43_",
            ):
                if fragment in lowered:
                    fail(f"forbidden import {imported!r} in {relative}")

    anchor_header = COMPONENT / "spine_anchor_internal.h"
    for relative in (
        "spine_construction.c",
        "fakes/s0a_fake_main.c",
        "tests/negative_token_substitution.c",
    ):
        if "spine_anchor_internal.h" not in includes(COMPONENT / relative):
            fail(f"{relative} does not use the single typed anchor declaration")
    for relative in ("spine_construction.c", "fakes/s0a_fake_main.c"):
        text = (COMPONENT / relative).read_text(encoding="utf-8")
        if re.search(r"\bextern\b[^;]*(?:" + "|".join(ANCHORS) + r")", text,
                     re.DOTALL):
            fail(f"local anchor redeclaration in {relative}")
    header_text = anchor_header.read_text(encoding="utf-8")
    typed_declarations = {
        ANCHORS[0]: "fwlab_sq_consumer_anchor_v0",
        ANCHORS[1]: "fwlab_cqe_publisher_anchor_v0",
    }
    for anchor, type_name in typed_declarations.items():
        declaration = re.compile(
            rf"extern\s+const\s+struct\s+{type_name}\s+"
            rf"{anchor}\s*;"
        )
        if header_text.count(anchor) != 1 or \
                len(declaration.findall(header_text)) != 1:
            fail(f"typed marker declaration/count differs for {anchor}")

    construction_text = (COMPONENT / "spine_construction.c").read_text(
        encoding="utf-8"
    )
    fake_text = (COMPONENT / "fakes/s0a_fake_main.c").read_text(
        encoding="utf-8"
    )
    negative_text = (
        COMPONENT / "tests/negative_token_substitution.c"
    ).read_text(encoding="utf-8")
    for anchor in ANCHORS:
        if construction_text.count(f"&{anchor}") != 2:
            fail(f"construction does not store/check exactly one marker address: "
                 f"{anchor}")
        for name, text in (("fake", fake_text), ("negative", negative_text)):
            if re.search(rf"\b{anchor}\s*\(", text):
                fail(f"{name} treats link-only marker as a runtime callback: "
                     f"{anchor}")

    lifecycle_text = (COMPONENT / "spine_lifecycle.c").read_text(
        encoding="utf-8"
    )
    lifecycle_forbidden = re.compile(
        r"(?:\bc43\b|\blinux\b|\bopcode\b|\bnamespace_id\b|\bnsid\b|"
        r"\bqid\b|\bcid\b|\bdma\b|\bblock\b|\bnfc\b|\bppa\b|"
        r"\bmedia\b|vfio|qemu|filesystem)", re.IGNORECASE
    )
    match = lifecycle_forbidden.search(lifecycle_text)
    if match is not None:
        fail(f"profile/transport/storage word in neutral lifecycle: "
             f"{match.group(0)!r}")

    for relative in S0B_PROFILE_SOURCES:
        text = (COMPONENT / relative).read_text(encoding="utf-8")
        for forbidden in (
            "fwlab_nvme_profile_valid",
            "fwlab_nvme_profile_encode",
            "fwlab_nvme_profile_decode",
            "fwlab_nvme_command_encode",
            "fwlab_nvme_command_decode",
            "fwlab_nvme_completion_encode",
            "fwlab_nvme_completion_decode",
            "fwlab_c43_policy_",
            "fwlab_c43_graph_",
            "fwlab_c43_instance_",
            "c43_control",
        ):
            if forbidden in text:
                fail(f"forbidden donor/profile symbol in {relative}: "
                     f"{forbidden}")

    adjacent_text = (
        COMPONENT / "fakes/spine_fake_adjacent.c"
    ).read_text(encoding="utf-8")
    if re.search(r"\bfwlab_spine_lifecycle_v0_[a-z0-9_]+\s*\(",
                 adjacent_text):
        fail("fake adjacent code reenters the lifecycle")
    for required in (
        "expected->binding.argument_read(",
        "expected->binding.payload_read(",
        "expected->binding.result_latch(",
        "fwlab_spine_fake_v0_abort_candidate_append(",
        "fake_relation_source(",
    ):
        if required not in adjacent_text:
            fail(f"typed fake-adjacent construction is absent: {required}")
    matrix_text = (
        COMPONENT / "tests/test_s0b_lifecycle.c"
    ).read_text(encoding="utf-8")
    if "payload_copy_v0" in matrix_text:
        fail("matrix bypasses PAYLOAD_FILL through a direct adapter copy")


def checked_artifact(path_text: str, expected_name: str) -> Path:
    expected_relative = f"build/s0a/{expected_name}"

    if path_text != expected_relative:
        fail(f"artifact path is not the exact direct manifest path: "
             f"expected={expected_relative!r} actual={path_text!r}")
    path = COMPONENT / path_text
    for directory in (COMPONENT / "build", COMPONENT / "build" / "s0a"):
        try:
            mode = directory.lstat().st_mode
        except OSError as error:
            fail(f"artifact directory cannot be inspected: {directory}: {error}")
        if directory.is_symlink() or not stat.S_ISDIR(mode):
            fail(f"artifact directory is not a real directory: {directory}")
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        fail(f"required artifact cannot be inspected: {path}: {error}")
    if path.is_symlink() or not stat.S_ISREG(mode):
        fail(f"required artifact is not a regular non-symlink file: {path}")
    return path


def nm(path: Path, *options: str) -> str:
    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    result = subprocess.run(
        ["nm", *options, str(path)],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=environment,
    )
    if result.returncode != 0 or not result.stdout.strip():
        fail(f"nm precondition failed for {path.name}: "
             f"status={result.returncode} stderr={result.stderr.strip()!r}")
    return result.stdout


def symbol_types(text: str, symbol: str) -> list[str]:
    found = []
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[-1] == symbol:
            found.append(fields[-2])
    return found


def authoritative_symbol_names(text: str) -> set[str]:
    names = set()

    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 2 and re.fullmatch(
                r"fwlab_authoritative_[a-z0-9_]+_v0", fields[-1]):
            if len(fields[-2]) != 1:
                fail(f"unparsed nm symbol type for {fields[-1]}: {line!r}")
            names.add(fields[-1])
    return names


def check_artifacts(archive: Path, public: Path, fake: Path) -> None:
    if os.path.samefile(archive, public) or os.path.samefile(archive, fake) or \
            os.path.samefile(public, fake):
        fail("S0-A artifacts must be distinct")
    if not os.access(public, os.X_OK) or not os.access(fake, os.X_OK):
        fail("S0-A ELF artifacts must be executable")

    members = subprocess.run(
        ["ar", "t", str(archive)], check=True, text=True,
        stdout=subprocess.PIPE,
    ).stdout.splitlines()
    if members != EXPECTED_MEMBERS:
        fail(f"archive members differ: {members}")

    archive_defined = nm(archive, "-A", "--defined-only")
    archive_undefined = nm(archive, "-u")
    public_defined = nm(public, "--defined-only")
    public_undefined = nm(public, "-u")
    fake_defined = nm(fake, "--defined-only")
    fake_undefined = nm(fake, "-u")
    for member, symbols in EXPECTED_MEMBER_SYMBOLS.items():
        for symbol in symbols:
            if not any(member in line and line.rstrip().endswith(f" {symbol}")
                       for line in archive_defined.splitlines()):
                fail(f"archive member {member} does not own {symbol}")
            if symbol_types(fake_defined, symbol) == []:
                fail(f"fake adjacency did not whole-link {member}:{symbol}")

    anchor_namespace = set()
    for output in (
        archive_defined, archive_undefined, public_defined, public_undefined,
        fake_defined, fake_undefined,
    ):
        anchor_namespace.update(authoritative_symbol_names(output))
    if anchor_namespace != set(ANCHORS):
        fail(f"authoritative anchor symbol set differs: "
             f"{sorted(anchor_namespace)}")
    for anchor in ANCHORS:
        if symbol_types(archive_defined, anchor):
            fail(f"construction archive defines selected anchor {anchor}")
        if symbol_types(archive_undefined, anchor) != ["U"]:
            fail(f"construction archive anchor reference differs: {anchor}")
        if symbol_types(public_defined, anchor) or \
                symbol_types(public_undefined, anchor):
            fail(f"public contract ELF unexpectedly contains anchor {anchor}")
        types = symbol_types(fake_defined, anchor)
        if types != [ANCHOR_OBJECT_TYPE]:
            fail(f"anchor {anchor} is not exactly one strong marker object: "
                 f"{types}")
        if symbol_types(fake_undefined, anchor):
            fail(f"fake adjacency leaves marker unresolved: {anchor}")

    provenance = (archive_defined + "\n" + archive_undefined + "\n" +
                  fake_defined + "\n" + fake_undefined).lower()
    for symbol in FORBIDDEN_SYMBOLS:
        if symbol.lower() in provenance:
            fail(f"legacy authoritative symbol is reachable: {symbol}")


def checked_s0b_artifact(path_text: str, expected: str) -> Path:
    if path_text != expected:
        fail(f"S0-B artifact path differs: expected={expected!r} "
             f"actual={path_text!r}")
    path = COMPONENT / path_text
    for directory in (COMPONENT / "build", COMPONENT / "build" / "s0b"):
        try:
            mode = directory.lstat().st_mode
        except OSError as error:
            fail(f"S0-B artifact directory cannot be inspected: {error}")
        if directory.is_symlink() or not stat.S_ISDIR(mode):
            fail(f"S0-B artifact directory is not real: {directory}")
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        fail(f"required S0-B artifact cannot be inspected: {path}: {error}")
    if path.is_symlink() or not stat.S_ISREG(mode):
        fail(f"S0-B artifact is not a regular file: {path}")
    return path


def undefined_symbols(text: str) -> set[str]:
    symbols = set()

    for line in text.splitlines():
        fields = line.split()
        if fields and (" U " in f" {line} " or
                       (len(fields) >= 2 and fields[-2] == "U")):
            symbols.add(fields[-1])
    return symbols


def require_member_symbol(
    defined: str,
    member: str,
    symbol: str,
) -> None:
    if not any(member in line and line.rstrip().endswith(f" {symbol}")
               for line in defined.splitlines()):
        fail(f"archive member {member} does not own {symbol}")


def check_s0b_artifacts(
    lifecycle_archive: Path,
    profile_archive: Path,
    matrix: Path,
    receipt_text: str,
) -> str:
    if os.path.samefile(lifecycle_archive, profile_archive) or \
            os.path.samefile(lifecycle_archive, matrix) or \
            os.path.samefile(profile_archive, matrix):
        fail("S0-B artifacts must be three distinct files")
    if not os.access(matrix, os.X_OK):
        fail("S0-B matrix is not executable")
    for relative in S0B_INTERMEDIATES:
        path = COMPONENT / relative
        try:
            mode = path.lstat().st_mode
        except OSError as error:
            fail(f"S0-B intermediate absent during authority check: "
                 f"{relative}: {error}")
        if path.is_symlink() or not stat.S_ISREG(mode):
            fail(f"S0-B intermediate is not a regular file: {relative}")

    lifecycle_members = subprocess.run(
        ["ar", "t", str(lifecycle_archive)], check=True, text=True,
        stdout=subprocess.PIPE,
    ).stdout.splitlines()
    profile_members = subprocess.run(
        ["ar", "t", str(profile_archive)], check=True, text=True,
        stdout=subprocess.PIPE,
    ).stdout.splitlines()
    if lifecycle_members != S0B_LIFECYCLE_MEMBERS:
        fail(f"S0-B lifecycle archive members differ: {lifecycle_members}")
    if profile_members != S0B_PROFILE_MEMBERS:
        fail(f"S0-B profile archive members differ: {profile_members}")

    lifecycle_defined = nm(lifecycle_archive, "-A", "--defined-only")
    lifecycle_undefined = nm(lifecycle_archive, "-u")
    profile_defined = nm(profile_archive, "-A", "--defined-only")
    profile_undefined = nm(profile_archive, "-u")
    matrix_defined = nm(matrix, "--defined-only")
    matrix_undefined = nm(matrix, "-u")

    for symbol in (
        "fwlab_spine_lifecycle_v0_symbol_owner",
        "fwlab_spine_lifecycle_v0_admit_start",
        "fwlab_spine_lifecycle_v0_step",
        "fwlab_spine_lifecycle_v0_intent_read",
        "fwlab_spine_lifecycle_v0_epoch_close_start",
        "fwlab_spine_lifecycle_v0_fini",
    ):
        require_member_symbol(lifecycle_defined, "spine_lifecycle.o", symbol)
    require_member_symbol(
        lifecycle_defined, "spine_contracts.o",
        "fwlab_host_action_program_v0_valid"
    )
    require_member_symbol(
        profile_defined, "c43_p1_adapter.o",
        "fwlab_c43_p1_adapter_v0_init"
    )
    require_member_symbol(
        profile_defined, "linux_profile_v1_adapter.o",
        "fwlab_linux_profile_v1_adapter_init"
    )

    if symbol_types(matrix_defined,
                    "fwlab_spine_lifecycle_v0_symbol_owner") != ["R"]:
        fail("matrix does not contain exactly one strong lifecycle owner")
    if symbol_types(matrix_undefined,
                    "fwlab_spine_lifecycle_v0_symbol_owner"):
        fail("matrix leaves lifecycle owner unresolved")
    for prefix in (
        "fwlab_spine_fake_", "fwlab_tiny_profile_",
        "test_s0b_", "main",
    ):
        if any(prefix in line for line in lifecycle_defined.splitlines()) or \
                any(prefix in line for line in profile_defined.splitlines()):
            fail(f"fake/tiny/test owner leaked into production archive: "
                 f"{prefix}")

    profile_imports = undefined_symbols(profile_undefined)
    nvme_imports = {
        symbol for symbol in profile_imports
        if symbol.startswith("fwlab_nvme_")
    }
    expected_nvme_imports = {
        "fwlab_nvme_command_valid",
        "fwlab_nvme_completion_valid",
    }
    if nvme_imports != expected_nvme_imports:
        fail(f"adapter C41 import set differs: {sorted(nvme_imports)}")
    for forbidden in (
        "fwlab_nvme_profile_valid",
        "fwlab_nvme_profile_encode",
        "fwlab_nvme_profile_decode",
        "fwlab_nvme_command_encode",
        "fwlab_nvme_command_decode",
        "fwlab_nvme_completion_encode",
        "fwlab_nvme_completion_decode",
    ):
        if forbidden in profile_imports:
            fail(f"adapter references forbidden C41 donor symbol: {forbidden}")

    provenance = "\n".join((
        lifecycle_defined, lifecycle_undefined,
        profile_defined, profile_undefined,
    )).lower()
    for forbidden in (
        "fwlab_c43_policy_", "fwlab_c43_graph_", "fwlab_c43_instance_",
        "fwlab_c43_action_", "fwlab_m4_frontend_", "fwlab_m4_nvme_",
        "fwlab_m4_media_", "nvme_mode",
    ):
        if forbidden in provenance:
            fail(f"legacy executor symbol is reachable in S0-B: {forbidden}")
    lifecycle_symbols = "\n".join((
        lifecycle_defined, lifecycle_undefined,
    )).lower()
    for forbidden in ("c43", "linux_profile", "opcode", "namespace_id"):
        if forbidden in lifecycle_symbols:
            fail(f"profile symbol leaked into lifecycle archive: {forbidden}")

    member_bytes = subprocess.run(
        ["ar", "p", str(lifecycle_archive), "spine_lifecycle.o"],
        check=True, stdout=subprocess.PIPE,
    ).stdout
    lifecycle_digest = hashlib.sha256(member_bytes).hexdigest()

    if not receipt_text or len(receipt_text.encode("utf-8")) > 16384 or \
            "\0" in receipt_text:
        fail("S0-B bounded matrix receipt input is absent or invalid")
    lines = receipt_text.splitlines()
    expected_profiles = {
        "C43-P1": ("c430", "11", "20", "15"),
        "Linux-profile-v1": ("7100", "12", "20", "15"),
        "tiny-HARNESS-PROVISIONAL": ("7700", "13", "2", "1"),
    }
    seen = {}
    row_pattern = re.compile(
        r"^S0B_PROFILE_ROW\|profile=([^|]+)\|adapter=([0-9a-f]+):"
        r"([1-9][0-9]*)\|ticket=([1-9][0-9]*):([1-9][0-9]*)\|"
        r"actions=([1-9][0-9]*)\|tokens=([0-9a-f]{16})\|"
        r"intents=([0-9a-f]{16})\|epoch=([1-9][0-9]*)\|"
        r"retained=([1-9][0-9]*)\|close=1\|fini_calls=([1-9][0-9]*)\|"
        r"owner=([0-9a-f]+)\|object=([0-9a-f]{64})$"
    )
    for line in lines:
        match = row_pattern.fullmatch(line)
        if match is not None:
            profile = match.group(1)
            if profile in seen:
                fail(f"duplicate S0-B profile receipt: {profile}")
            seen[profile] = match.groups()[1:]
    if set(seen) != set(expected_profiles):
        fail(f"S0-B profile receipt set differs: {sorted(seen)}")
    owners = set()
    token_digests = set()
    intent_digests = set()
    for profile, expected in expected_profiles.items():
        (adapter_nonce, generation, ticket_uid, command_uid, actions,
         tokens, intents, epoch, retained, fini_calls, owner,
         object_digest) = seen[profile]
        expected_nonce, expected_generation, expected_actions, \
            expected_retained = expected
        if (adapter_nonce, generation, actions, retained) != (
                expected_nonce, expected_generation, expected_actions,
                expected_retained):
            fail(f"S0-B dynamic receipt differs for {profile}: "
                 f"{seen[profile]}")
        if ticket_uid != "1" or command_uid != "1" or epoch != "7" or \
                int(tokens, 16) == 0 or int(intents, 16) == 0 or \
                int(fini_calls) < 2 or object_digest != lifecycle_digest:
            fail(f"S0-B lifecycle identity receipt invalid for {profile}")
        owners.add(owner)
        token_digests.add(tokens)
        intent_digests.add(intents)
    if owners != {"4c49464543593030"}:
        fail(f"S0-B rows do not share the strong lifecycle owner: {owners}")
    if len(token_digests) != 3 or len(intent_digests) != 3:
        fail("S0-B per-profile token/intent identities are not distinct")
    lifecycle_line = (
        "S0B_LIFECYCLE_CASES|admit-retry=1|rollback=1|dependency=1|"
        "backpressure=1|response-loss=1|typed-sidecars=1|"
        "normalized-results=1|substitution=1|first-failure=1|"
        "abort-cases=14|close-cuts=3|close-fair=1|close-resume=1|"
        "intent-repeat=1|"
        "cpls-advance=0|PASS"
    )
    if lines.count(lifecycle_line) != 1 or lines.count(
            "S0-B profile matrix: PASS (profiles=3 artifacts=3)") != 1:
        fail("S0-B lifecycle/final matrix receipt differs")
    if len(lines) != 5:
        fail(f"S0-B matrix emitted an unbounded/extra receipt: {lines}")
    return lifecycle_digest


def s0b_checkpoint_identity() -> None:
    paths = git_bytes(
        "diff", "--name-only", "--no-renames", S0A_CHECKPOINT,
        S0B_CHECKPOINT, "--"
    ).decode("utf-8").splitlines()
    paths = sorted(path for path in paths if path)
    encoded = "".join(f"{path}\n" for path in paths).encode("utf-8")
    if hashlib.sha256(encoded).hexdigest() != S0B_PATH_MANIFEST:
        fail("S0-B checkpoint path manifest does not reconstruct")
    content = bytearray()
    for path in paths:
        digest = hashlib.sha256(
            git_bytes("show", f"{S0B_CHECKPOINT}:{path}")
        ).hexdigest()
        content.extend(f"{digest}  {path}\n".encode("utf-8"))
    if hashlib.sha256(content).hexdigest() != S0B_CONTENT_MANIFEST:
        fail("S0-B checkpoint content manifest does not reconstruct")


def changed_paths_j0a() -> list[str]:
    if subprocess.run(
        ["git", "-C", str(ROOT), "merge-base", "--is-ancestor",
         S0B_CHECKPOINT, "HEAD"], check=False,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    ).returncode:
        fail(f"HEAD is not descended from S0-B checkpoint {S0B_CHECKPOINT}")
    s0b_checkpoint_identity()
    for path in S0B_TRANSITION_PATHS:
        if path == "scripts/check_spine_architecture.py":
            continue
        if (ROOT / path).read_bytes() != git_bytes(
                "show", f"{S0B_CHECKPOINT}:{path}"):
            fail(f"frozen S0-B leaf differs from {S0B_CHECKPOINT}: {path}")
    tracked = git_bytes(
        "diff", "--name-only", "--no-renames", "-z", S0B_CHECKPOINT, "--"
    ).decode("utf-8").split("\0")
    untracked = git_bytes(
        "ls-files", "--others", "--exclude-standard", "-z"
    ).decode("utf-8").split("\0")
    paths = sorted(set(path for path in [*tracked, *untracked] if path))
    if paths != J0A_PATHS:
        missing = sorted(set(J0A_PATHS) - set(paths))
        extra = sorted(set(paths) - set(J0A_PATHS))
        fail(f"J0-A path manifest differs: missing={missing} extra={extra}")
    check_regular_paths(paths)
    for path in J0A_NEW_PATHS:
        if subprocess.run(
            ["git", "-C", str(ROOT), "cat-file", "-e",
             f"{S0B_CHECKPOINT}:{path}"], check=False,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        ).returncode == 0:
            fail(f"J0-A new path existed at its exact base: {path}")
    for path in J0A_MODIFIED_PATHS:
        if subprocess.run(
            ["git", "-C", str(ROOT), "cat-file", "-e",
             f"{S0B_CHECKPOINT}:{path}"], check=False,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        ).returncode != 0:
            fail(f"J0-A modified authority absent at exact base: {path}")
    ignored = git_bytes(
        "ls-files", "--others", "--ignored", "--exclude-standard", "-z",
        "--", "core/m3p", "media/file-nand-v0", "frontends/headless-j0",
    ).decode("utf-8").split("\0")
    allowed = {f"frontends/headless-j0/{path}" for path in J0A_OWNED}
    unexpected = sorted(path for path in ignored if path and path not in allowed)
    if unexpected:
        fail(f"unexpected ignored J0-A inputs/artifacts: {unexpected}")
    return paths


def j0_manifest_expected() -> dict[str, list[str]]:
    return {
        "J0_BASE_COMMIT": [S0B_CHECKPOINT],
        "J0_S0B_PATH_MANIFEST": [S0B_PATH_MANIFEST],
        "J0_S0B_CONTENT_MANIFEST": [S0B_CONTENT_MANIFEST],
        "J0_M3P_SOURCES": J0_M3P_SOURCES,
        "J0_M3P_MEMBERS": J0_M3P_MEMBERS,
        "J0_NFC_SOURCES": J0_NFC_SOURCES,
        "J0_NFC_MEMBERS": J0_NFC_MEMBERS,
        "J0_FILE_SOURCES": J0_FILE_SOURCES,
        "J0_FILE_MEMBERS": J0_FILE_MEMBERS,
        "J0A_BUILD_DIR": ["build/j0a"],
        "J0A_M3P_OBJECTS": J0A_M3P_OBJECTS,
        "J0A_NFC_OBJECTS": J0A_NFC_OBJECTS,
        "J0A_FILE_OBJECTS": J0A_FILE_OBJECTS,
        "J0A_FAKE_SOURCE": ["../../core/m3p/fakes/m3p_fake_adjacent.c"],
        "J0A_FAKE_OBJECT": [J0A_FAKE_OBJECT],
        "J0A_TEST_SOURCE": ["../../core/m3p/tests/test_j0a_lower.c"],
        "J0A_TEST_OBJECT": [J0A_TEST_OBJECT],
        "J0A_M3P_ARCHIVE": [J0A_ARCHIVES[0]],
        "J0A_NFC_ARCHIVE": [J0A_ARCHIVES[1]],
        "J0A_FILE_ARCHIVE": [J0A_ARCHIVES[2]],
        "J0A_MATRIX": [J0A_MATRIX],
        "J0A_INTERMEDIATES": J0A_INTERMEDIATES,
        "J0A_ARTIFACTS": [J0A_MATRIX],
        "J0A_OWNED": J0A_OWNED,
        "J0_FORBIDDEN_MEMBERS": J0_FORBIDDEN_MEMBERS,
        "J0_FORBIDDEN_SYMBOL_PREFIXES": [
            "c34_", "c35_", "fwlab_c31_", "fwlab_m4_",
        ],
    }


def check_j0_manifest() -> None:
    manifest = (J0_COMPONENT / "j0.mk").read_text(encoding="utf-8")
    assignment_re = re.compile(
        r"^[ \t]*(?P<override>override[ \t]+)?"
        r"(?P<name>J0[A-Z0-9_]+)[ \t]*(?P<operator>[:+?!]*=)",
        re.MULTILINE,
    )
    expected = j0_manifest_expected()
    counts: dict[str, int] = {}
    for assignment in assignment_re.finditer(manifest):
        name = assignment.group("name")
        counts[name] = counts.get(name, 0) + 1
        if (assignment.group("override") or "").strip() != "override" or \
                assignment.group("operator") != ":=":
            fail(f"J0 manifest assignment is not fixed override ':=': {name}")
    unknown = sorted(set(counts) - set(expected))
    missing = sorted(set(expected) - set(counts))
    duplicate = sorted(name for name, count in counts.items() if count != 1)
    if unknown or missing or duplicate:
        fail(f"J0 manifest assignment set differs: unknown={unknown} "
             f"missing={missing} duplicate={duplicate}")
    for variable, words in expected.items():
        if make_words(manifest, variable) != words:
            fail(f"{variable} differs from the frozen literal list")
    if "$(wildcard" in manifest or re.search(
            r"(?m)^\s*J0[A-Z0-9_]+\s*\?=", manifest):
        fail("J0 manifest contains discovery or an overridable binding")
    for variable in ("J0A_INTERMEDIATES", "J0A_OWNED"):
        if any("$(" in word for word in make_words(manifest, variable)):
            fail(f"{variable} contains a derived/nonliteral member")
    if [Path(source).with_suffix(".o").name for source in J0_M3P_SOURCES] != \
            J0_M3P_MEMBERS or \
       [Path(source).with_suffix(".o").name for source in J0_NFC_SOURCES] != \
            J0_NFC_MEMBERS or \
       [Path(source).with_suffix(".o").name for source in J0_FILE_SOURCES] != \
            J0_FILE_MEMBERS:
        fail("J0 source/member derivation differs")
    makefile = (J0_COMPONENT / "Makefile").read_text(encoding="utf-8")
    if makefile.count("--phase j0a") != 1 or \
            makefile.count("--receipts -") != 1 or \
            makefile.count("$(J0A_MATRIX) --m3p-sha") != 1:
        fail("J0-A matrix-to-checker authority pipeline differs")
    matrix_position = makefile.find("$(J0A_MATRIX) --m3p-sha")
    checker_position = makefile.find("--phase j0a")
    if matrix_position < 0 or checker_position <= matrix_position:
        fail("J0-A checker is not downstream of its single matrix execution")
    for required in (
        "override SHELL := /bin/sh",
        "override J0A_ACTIVE_SHORT_FLAGS :=",
        "check-j0a: j0a-authority-guard",
        "$(words $(MAKEFILE_LIST))",
        "$(word 1,$(MAKEFILE_LIST))",
        "$(word 2,$(MAKEFILE_LIST))",
        "if ! receipts=$$($(J0A_MATRIX)",
        "printf '%s\\n' \"$$receipts\" | python3",
    ):
        if makefile.count(required) != 1:
            fail(f"J0-A make authority fragment differs: {required}")
    for short_mode in ("n", "t", "q"):
        if makefile.count(
                f"findstring {short_mode},$(J0A_ACTIVE_SHORT_FLAGS)"
        ) != 1:
            fail(f"J0-A make mode rejection differs: {short_mode}")


def check_j0_sources() -> None:
    immutable = [
        "nfc/nfc_model.c", "nfc/nfc_scheduler.c", "nfc/nfc_fault.c",
        "nfc/nfc_media.c", "include/fwlab/portable/nfc_types.h",
        "include/fwlab/portable/nfc_model.h",
        "include/fwlab/contracts/nfc_provider.h",
        "include/fwlab/contracts/nand_media.h",
        "include/fwlab/contracts/block_service_v0.h",
        "include/fwlab/contracts/controller_buffer_v0.h",
    ]
    for path in immutable:
        if (ROOT / path).read_bytes() != git_bytes(
                "show", f"{S0B_CHECKPOINT}:{path}"):
            fail(f"J0-A changed a frozen C3/S0 contract leaf: {path}")
    if includes(ROOT / "core/m3p/m3p.h") != {
        "stddef.h", "stdint.h", "fwlab/contracts/block_service_v0.h",
        "fwlab/contracts/controller_buffer_v0.h",
        "fwlab/contracts/nfc_provider.h",
    }:
        fail("M3-P public include boundary differs")
    if includes(ROOT / "media/file-nand-v0/file_nand.h") != {
        "stddef.h", "stdint.h", "fwlab/contracts/nand_media.h",
    }:
        fail("file-NAND public include boundary differs")
    m3p_text = "\n".join(
        (ROOT / Path(source).as_posix().removeprefix("../../")).read_text(
            encoding="utf-8") for source in J0_M3P_SOURCES
    )
    for forbidden in (
        "file_nand", "openat", "pread", "pwrite", "fdatasync", "execveat",
        "host_dma", "prp", "iova", "qid", "cid",
    ):
        if re.search(rf"\b{re.escape(forbidden)}\b", m3p_text,
                     re.IGNORECASE):
            fail(f"M3-P imports a forbidden lower/Host concept: {forbidden}")
    file_text = "\n".join(
        (ROOT / Path(source).as_posix().removeprefix("../../")).read_text(
            encoding="utf-8") for source in J0_FILE_SOURCES
    )
    for forbidden in ("lba", "nsid", "nvme", "host_dma", "prp", "iova"):
        if re.search(rf"\b{forbidden}\b", file_text, re.IGNORECASE):
            fail(f"file-NAND imports a logical/Host concept: {forbidden}")
    engine_text = (ROOT / "media/file-nand-v0/file_nand_engine.c").read_text(
        encoding="utf-8"
    )
    for forbidden in (
        "open", "openat", "close", "pread", "pwrite", "ftruncate",
        "fdatasync", "fsync", "fcntl", "unlink", "unlinkat",
    ):
        if re.search(rf"(?<![>.])\b{forbidden}\s*\(", engine_text):
            fail(f"file-NAND engine contains POSIX operation: {forbidden}")


def checked_j0_artifact(path_text: str, expected: str) -> Path:
    if path_text != expected:
        fail(f"J0-A artifact path differs: expected={expected!r} "
             f"actual={path_text!r}")
    path = J0_COMPONENT / path_text
    for directory in (J0_COMPONENT / "build", J0_COMPONENT / "build/j0a"):
        try:
            mode = directory.lstat().st_mode
        except OSError as error:
            fail(f"J0-A artifact directory cannot be inspected: {error}")
        if directory.is_symlink() or not stat.S_ISDIR(mode):
            fail(f"J0-A artifact directory is not real: {directory}")
    try:
        mode = path.lstat().st_mode
    except OSError as error:
        fail(f"J0-A artifact cannot be inspected: {path}: {error}")
    if path.is_symlink() or not stat.S_ISREG(mode):
        fail(f"J0-A artifact is not a regular file: {path}")
    return path


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_j0_receipts(receipt_text: str, m3p_sha: str, nfc_sha: str,
                      file_sha: str, elf_sha: str) -> None:
    if not receipt_text or len(receipt_text.encode("utf-8")) > 16384 or \
            "\0" in receipt_text:
        fail("J0-A bounded receipt input is absent or invalid")
    lines = receipt_text.splitlines()
    if len(lines) != 10:
        fail(f"J0-A emitted absent/extra receipts: count={len(lines)}")
    binding = re.fullmatch(
        r"J0A_BINDING\|m3p=([0-9a-f]{64})\|nfc=([0-9a-f]{64})\|"
        r"file=([0-9a-f]{64})\|elf=([0-9a-f]{64})\|"
        r"uuid=([0-9a-f]{32})\|geometry=1x1x1x16x32x4096\+128",
        lines[0],
    )
    if binding is None or binding.groups()[:4] != (
            m3p_sha, nfc_sha, file_sha, elf_sha) or \
            int(binding.group(5), 16) == 0:
        fail("J0-A binding receipt does not match exact artifacts")
    if lines[1] != (
        "J0A_ADJACENCY|block_upper=1|scripted_nfc_lower=1|"
        "file_memory_lower=1|barrier_fault=1"
    ):
        fail("J0-A adjacency receipt differs")
    rmw = re.fullmatch(
        r"J0A_RMW\|one_sector=1\|unaligned_16_lba=1\|lpn_span=3\|"
        r"data_ppas=([0-9a-f]{16})", lines[2]
    )
    if rmw is None or int(rmw.group(1), 16) == 0:
        fail("J0-A RMW receipt differs")
    if lines[3] != (
        "J0A_TRIM|partial_mask=1|whole_tombstone=1|flush_survives=1"
    ):
        fail("J0-A Trim receipt differs")
    durability = re.fullmatch(
        r"J0A_DURABILITY\|volatile_lost=1\|self_survives=1\|"
        r"frontier=([1-9][0-9]*)\|later_uncovered=0", lines[4]
    )
    if durability is None:
        fail("J0-A durability receipt differs")
    checkpoint = re.fullmatch(
        r"J0A_CHECKPOINT\|generation=([1-9][0-9]*)\|"
        r"covered=([1-9][0-9]*)\|tail_ignored=1\|interior_quarantine=1",
        lines[5],
    )
    if checkpoint is None:
        fail("J0-A checkpoint receipt differs")
    gc = re.fullmatch(
        r"J0A_GC\|victim=([0-9])\|live=([0-9]|1[0-9]|2[0-5])\|"
        r"reclaimable=([7-9]|[12][0-9]|3[0-2])\|"
        r"gc_uid=([1-9][0-9]*)\|switch=([1-9][0-9]*)\|"
        r"erase_count=([1-9][0-9]*)", lines[6],
    )
    if gc is None:
        fail("J0-A GC receipt differs")
    before = re.fullmatch(
        r"J0A_CUT\|phase=BEFORE\|exit=90\|physical_delta=0\|"
        r"logical=old\|elf_devino=([0-9a-f]+:[0-9a-f]+)\|"
        r"elf=([0-9a-f]{64})", lines[7],
    )
    after = re.fullmatch(
        r"J0A_CUT\|phase=AFTER\|exit=91\|physical_delta=1\|"
        r"logical=old\|p2l=ORPHAN\|"
        r"elf_devino=([0-9a-f]+:[0-9a-f]+)\|elf=([0-9a-f]{64})",
        lines[8],
    )
    if before is None or after is None or before.group(1) != after.group(1) or \
            before.group(2) != elf_sha or after.group(2) != elf_sha:
        fail("J0-A cut identity/effect receipt differs")
    if lines[9] != "J0-A lower matrix: PASS (rows=9 artifacts=1)":
        fail("J0-A final receipt differs")


def check_j0a_artifacts(m3p_archive: Path, nfc_archive: Path,
                        file_archive: Path, matrix: Path,
                        receipt_text: str) -> tuple[str, str, str, str, str]:
    artifacts = [m3p_archive, nfc_archive, file_archive, matrix]
    if len({os.path.realpath(path) for path in artifacts}) != len(artifacts):
        fail("J0-A artifacts are not four distinct files")
    if not os.access(matrix, os.X_OK):
        fail("J0-A matrix is not executable")
    for relative in J0A_INTERMEDIATES:
        path = J0_COMPONENT / relative
        try:
            mode = path.lstat().st_mode
        except OSError as error:
            fail(f"J0-A intermediate absent during authority check: "
                 f"{relative}: {error}")
        if path.is_symlink() or not stat.S_ISREG(mode):
            fail(f"J0-A intermediate is not regular: {relative}")
    archive_specs = (
        (m3p_archive, J0_M3P_MEMBERS, J0A_M3P_OBJECTS, J0_M3P_SOURCES),
        (nfc_archive, J0_NFC_MEMBERS, J0A_NFC_OBJECTS, J0_NFC_SOURCES),
        (file_archive, J0_FILE_MEMBERS, J0A_FILE_OBJECTS, J0_FILE_SOURCES),
    )
    authority = hashlib.sha256()
    for archive, expected, objects, sources in archive_specs:
        members = subprocess.run(
            ["ar", "t", str(archive)], check=True, text=True,
            stdout=subprocess.PIPE,
        ).stdout.splitlines()
        if members != expected:
            fail(f"J0-A archive members differ for {archive.name}: {members}")
        if set(members) & set(J0_FORBIDDEN_MEMBERS):
            fail(f"forbidden object entered {archive.name}")
        for member, object_relative, source_relative in zip(
                members, objects, sources, strict=True):
            object_path = J0_COMPONENT / object_relative
            member_bytes = subprocess.run(
                ["ar", "p", str(archive), member], check=True,
                stdout=subprocess.PIPE,
            ).stdout
            object_bytes = object_path.read_bytes()
            if member_bytes != object_bytes:
                fail(f"archive member differs byte-for-byte from object: "
                     f"{archive.name}:{member}")
            source_path = ROOT / Path(source_relative).as_posix().removeprefix(
                "../../"
            )
            source_digest = hashlib.sha256(source_path.read_bytes()).hexdigest()
            object_digest = hashlib.sha256(object_bytes).hexdigest()
            authority.update(
                f"{source_digest} {source_path.relative_to(ROOT)} "
                f"{object_digest} {object_relative} {member} "
                f"{file_sha256(archive)}\n".encode("utf-8")
            )
    for source_relative, object_relative in (
        ("core/m3p/fakes/m3p_fake_adjacent.c", J0A_FAKE_OBJECT),
        ("core/m3p/tests/test_j0a_lower.c", J0A_TEST_OBJECT),
    ):
        source_digest = hashlib.sha256(
            (ROOT / source_relative).read_bytes()
        ).hexdigest()
        object_digest = hashlib.sha256(
            (J0_COMPONENT / object_relative).read_bytes()
        ).hexdigest()
        authority.update(
            f"{source_digest} {source_relative} {object_digest} "
            f"{object_relative} direct-link\n".encode("utf-8")
        )
    expected_symbols = (
        (m3p_archive, {
            "m3p_codec.o": "m3p_crc32c",
            "m3p_mapping.o": "m3p_mapping_reset",
            "m3p_nfc.o": "m3p_child_read_start",
            "m3p_gc.o": "fwlab_m3p_force_gc_start",
            "m3p_recovery.o": "fwlab_m3p_recover_start",
            "m3p_runtime.o": "fwlab_m3p_init",
        }),
        (nfc_archive, {
            "nfc_model.o": "fwlab_nfc_model_init",
            "nfc_scheduler.o": "c33_schedule_operation",
            "nfc_fault.o": "c33_fault_word",
            "nfc_media.o": "c33_finish_operation",
        }),
        (file_archive, {
            "file_nand_codec.o": "file_nand_crc32c",
            "file_nand_engine.o": "file_nand_engine_format",
            "file_nand_media.o": "fwlab_file_nand_v0_media",
            "file_nand_posix.o": "fwlab_file_nand_v0_posix_format",
        }),
    )
    provenance = []
    for archive, symbols in expected_symbols:
        defined = nm(archive, "-A", "--defined-only")
        undefined = nm(archive, "-u")
        provenance.extend((defined, undefined))
        for member, symbol in symbols.items():
            require_member_symbol(defined, member, symbol)
    joined = "\n".join(provenance).lower()
    for prefix in ("c34_", "c35_", "fwlab_c31_", "fwlab_m4_"):
        if prefix in joined:
            fail(f"forbidden donor symbol entered J0-A: {prefix}")
    digests = tuple(file_sha256(path) for path in artifacts)
    check_j0_receipts(receipt_text, *digests)
    return (*digests, authority.hexdigest())


def main() -> int:
    global ACTIVE_PHASE

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--phase", choices=("s0a", "s0b", "j0a"), default="s0a"
    )
    parser.add_argument(
        "--archive", default="build/s0a/libfwlab_spine_contracts_v0.a"
    )
    parser.add_argument(
        "--public", default="build/s0a/s0a_public_contracts"
    )
    parser.add_argument(
        "--fake", default="build/s0a/s0a_fake_adjacent_link"
    )
    parser.add_argument("--lifecycle")
    parser.add_argument("--profiles")
    parser.add_argument("--m3p")
    parser.add_argument("--nfc")
    parser.add_argument("--file")
    parser.add_argument("--matrix")
    parser.add_argument("--receipts")
    arguments = parser.parse_args()
    ACTIVE_PHASE = arguments.phase

    if arguments.phase == "j0a":
        if arguments.m3p is None or arguments.nfc is None or \
                arguments.file is None or arguments.matrix is None or \
                arguments.receipts != "-":
            fail("J0-A requires three archives, matrix and stdin receipts")
        if arguments.lifecycle is not None or arguments.profiles is not None:
            fail("S0-B artifacts are forbidden in J0-A phase")
        paths = changed_paths_j0a()
        check_j0_manifest()
        check_j0_sources()
        m3p_archive = checked_j0_artifact(arguments.m3p, J0A_ARCHIVES[0])
        nfc_archive = checked_j0_artifact(arguments.nfc, J0A_ARCHIVES[1])
        file_archive = checked_j0_artifact(arguments.file, J0A_ARCHIVES[2])
        matrix = checked_j0_artifact(arguments.matrix, J0A_MATRIX)
        digests = check_j0a_artifacts(
            m3p_archive, nfc_archive, file_archive, matrix, sys.stdin.read()
        )
        print(
            "J0-A lower-spine architecture: PASS "
            f"(paths={len(paths)} intermediates={len(J0A_INTERMEDIATES)} "
            f"archive_members={len(J0_M3P_MEMBERS) + len(J0_NFC_MEMBERS) + len(J0_FILE_MEMBERS)} "
            f"artifacts=1 matrix_sha256={digests[3]} "
            f"source_object_sha256={digests[4]})"
        )
        return 0

    if arguments.phase == "s0b":
        if arguments.lifecycle is None or arguments.profiles is None or \
                arguments.matrix is None or arguments.receipts != "-":
            fail("S0-B requires lifecycle/profiles/matrix and stdin receipts")
        if arguments.m3p is not None or arguments.nfc is not None or \
                arguments.file is not None or arguments.archive != \
                "build/s0a/libfwlab_spine_contracts_v0.a" or \
                arguments.public != "build/s0a/s0a_public_contracts" or \
                arguments.fake != "build/s0a/s0a_fake_adjacent_link":
            fail("S0-A artifact arguments are forbidden in S0-B phase")
        paths = changed_paths("s0b")
        check_manifest()
        check_imports()
        lifecycle = checked_s0b_artifact(
            arguments.lifecycle, S0B_ARTIFACTS[0]
        )
        profiles = checked_s0b_artifact(
            arguments.profiles, S0B_ARTIFACTS[1]
        )
        matrix = checked_s0b_artifact(arguments.matrix, S0B_ARTIFACTS[2])
        receipt_text = sys.stdin.read()
        digest = check_s0b_artifacts(
            lifecycle, profiles, matrix, receipt_text
        )
        print(
            "S0-B shared lifecycle architecture: PASS "
            f"(paths={len(paths)} intermediates={len(S0B_INTERMEDIATES)} "
            f"archive_members=4 profiles=3 artifacts=3 "
            f"lifecycle_object_sha256={digest})"
        )
        return 0

    if arguments.lifecycle is not None or arguments.profiles is not None or \
            arguments.m3p is not None or arguments.nfc is not None or \
            arguments.file is not None or arguments.matrix is not None or \
            arguments.receipts is not None:
        fail("S0-B artifacts require literal --phase s0b")
    paths = changed_paths("s0a")
    check_manifest()
    check_imports()
    archive = checked_artifact(arguments.archive,
                               "libfwlab_spine_contracts_v0.a")
    public = checked_artifact(arguments.public, "s0a_public_contracts")
    fake = checked_artifact(arguments.fake, "s0a_fake_adjacent_link")
    check_artifacts(archive, public, fake)

    print(
        "S0-A contract architecture: PASS "
        f"(paths={len(paths)} archive_members={len(EXPECTED_MEMBERS)} "
        f"anchors={len(ANCHORS)} artifacts=3)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
