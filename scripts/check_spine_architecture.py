#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Fail closed on the bounded S0-A contract and construction checkpoint."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import stat
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "core" / "command-spine"
BASE = "8002355dd43a36a085df864174613cadd508021a"

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


def fail(message: str) -> None:
    print(f"S0-A architecture failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def git_bytes(*arguments: str) -> bytes:
    return subprocess.run(
        ["git", "-C", str(ROOT), *arguments],
        check=True,
        stdout=subprocess.PIPE,
    ).stdout


def changed_paths() -> list[str]:
    if subprocess.run(
        ["git", "-C", str(ROOT), "merge-base", "--is-ancestor", BASE, "HEAD"],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ).returncode:
        fail(f"HEAD is not descended from S0-A base {BASE}")
    tracked = git_bytes(
        "diff", "--name-only", "--no-renames", "-z", BASE, "--"
    ).decode("utf-8").split("\0")
    untracked = git_bytes(
        "ls-files", "--others", "--exclude-standard", "-z"
    ).decode("utf-8").split("\0")
    paths = sorted(set(path for path in [*tracked, *untracked] if path))
    if paths != CHECKPOINT_PATHS:
        missing = sorted(set(CHECKPOINT_PATHS) - set(paths))
        extra = sorted(set(paths) - set(CHECKPOINT_PATHS))
        fail(f"checkpoint path manifest differs: missing={missing} extra={extra}")
    for path in paths:
        candidate = ROOT / path
        try:
            mode = candidate.lstat().st_mode
        except OSError as error:
            fail(f"checkpoint path cannot be inspected: {path}: {error}")
        if candidate.is_symlink() or not stat.S_ISREG(mode):
            fail(f"checkpoint path is not a regular non-symlink file: {path}")
        if subprocess.run(
            ["git", "-C", str(ROOT), "cat-file", "-e", f"{BASE}:{path}"],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode == 0:
            fail(f"S0-A path unexpectedly existed at the exact base: {path}")

    ignored = git_bytes(
        "ls-files", "--others", "--ignored", "--exclude-standard", "-z",
        "--", "core/command-spine",
    ).decode("utf-8").split("\0")
    allowed_ignored = {
        *(f"core/command-spine/{path}" for path in EXPECTED_OBJECTS),
        f"core/command-spine/{PUBLIC_TEST_OBJECT[0]}",
        f"core/command-spine/{FAKE_OBJECT[0]}",
        f"core/command-spine/{ARCHIVE_PATH[0]}",
        f"core/command-spine/{PUBLIC_PATH[0]}",
        f"core/command-spine/{FAKE_PATH[0]}",
    }
    unexpected_ignored = sorted(
        path for path in ignored if path and path not in allowed_ignored
    )
    if unexpected_ignored:
        source_inputs = [
            path for path in unexpected_ignored
            if Path(path).suffix.lower() in SOURCE_INPUT_SUFFIXES or
            Path(path).name in {"Makefile", "Kbuild", "meson.build"}
        ]
        if source_inputs:
            fail(f"ignored non-artifact source inputs: {source_inputs}")
        fail(f"unexpected ignored S0-A build inputs/artifacts: {unexpected_ignored}")
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--archive", default="build/s0a/libfwlab_spine_contracts_v0.a"
    )
    parser.add_argument(
        "--public", default="build/s0a/s0a_public_contracts"
    )
    parser.add_argument(
        "--fake", default="build/s0a/s0a_fake_adjacent_link"
    )
    arguments = parser.parse_args()

    paths = changed_paths()
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
