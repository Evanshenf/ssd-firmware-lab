#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Fail closed on C4.3 phase-1 layering, recipe and freeze violations."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tomllib


ROOT = Path(__file__).resolve().parents[1]
BASE = "5368ab6b41223f72487dc519dda1af3971de82e9"
EXPECTED_SOURCES = [
    "c43_instance.c",
    "c43_policy.c",
    "c43_identify.c",
    "c43_graph.c",
    "c43_control.c",
    "c43_completion.c",
    "c43_actions.c",
]
EXPECTED_MEMBERS = [name.removesuffix(".c") + ".o" for name in EXPECTED_SOURCES]
EXPECTED_MEMBER_SYMBOLS = {
    "c43_instance.o": "fwlab_c43_graph_init",
    "c43_policy.o": "fwlab_c43_policy_request_valid",
    "c43_identify.o": "fwlab_c43_identify_encode",
    "c43_graph.o": "fwlab_c43_graph_step",
    "c43_control.o": "fwlab_c43_graph_observer_valid",
    "c43_completion.o": "fwlab_c43_policy_plan_valid",
    "c43_actions.o": "fwlab_c43_queue_effect_port_valid",
}
PUBLIC_HEADERS = [
    "include/fwlab/portable/c4_command_graph.h",
    "include/fwlab/portable/nvme_policy.h",
    "include/fwlab/contracts/hif_queue_effect_port.h",
    "include/fwlab/contracts/hif_target_resolver_port.h",
    "include/fwlab/contracts/block_action_port.h",
]
CORE_PATHS = [
    *[f"core/c4-nvme/{name}" for name in EXPECTED_SOURCES],
    "core/c4-nvme/c43_internal.h",
    *PUBLIC_HEADERS,
]
CHECKPOINT_PATHS = sorted([
    "core/c4-nvme/Makefile",
    "core/c4-nvme/README.md",
    "core/c4-nvme/c43.mk",
    *[f"core/c4-nvme/{name}" for name in EXPECTED_SOURCES],
    "core/c4-nvme/c43_internal.h",
    "core/c4-nvme/fakes/c43_fake_services.c",
    "core/c4-nvme/fakes/c43_fake_services.h",
    "core/c4-nvme/fakes/c43_phase1_fake_main.c",
    "core/c4-nvme/tests/test_c43_public_abi.c",
    "core/c4-nvme/tests/test_c43_reservation.c",
    "core/c4-nvme/tests/test_c43_policy.c",
    "docs/adr/0011-c4-command-graph-v1.md",
    "docs/adr/README.md",
    "docs/architecture.md",
    *PUBLIC_HEADERS,
    "scripts/check_c43_architecture.py",
])
PREEXISTING_ALLOWLIST = {
    "core/c4-nvme/Makefile",
    "core/c4-nvme/README.md",
    "frontends/Makefile",
    "frontends/headless-c4/README.md",
    "docs/provenance/sources.yaml",
    "docs/adr/README.md",
    "docs/architecture.md",
}
FORBIDDEN_CORE = [
    "frontends/headless-c4",
    "c42_",
    "c31_",
    "c35_",
    "<linux/",
    "vfio",
    "qemu",
    "pci_",
]
FORBIDDEN_PORTABLE_STATE = [
    "struct fwlab_nvme_command ",
    "command_dword",
    "submission_queue_id",
    "command_id;",
    "queue_id;",
]


def fail(message: str) -> None:
    print(f"C4.3 architecture failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def git_bytes(*arguments: str) -> bytes:
    result = subprocess.run(
        ["git", "-C", str(ROOT), *arguments],
        check=True,
        stdout=subprocess.PIPE,
    )
    return result.stdout


def changed_paths() -> list[str]:
    ancestry = subprocess.run(
        ["git", "-C", str(ROOT), "merge-base", "--is-ancestor", BASE, "HEAD"],
        check=False,
    )
    if ancestry.returncode != 0:
        fail(f"HEAD is not descended from frozen C4.3 base {BASE}")
    tracked = git_bytes(
        "diff", "--name-only", "--no-renames", "-z", BASE, "--"
    ).decode("utf-8").split("\0")
    untracked = git_bytes(
        "ls-files", "--others", "--exclude-standard", "-z"
    ).decode("utf-8").split("\0")
    paths = sorted(set(path for path in [*tracked, *untracked] if path))
    if len(paths) > 51:
        fail(f"changed-path cap exceeded: {len(paths)} > 51")
    if paths != CHECKPOINT_PATHS:
        missing = sorted(set(CHECKPOINT_PATHS) - set(paths))
        extra = sorted(set(paths) - set(CHECKPOINT_PATHS))
        fail(f"checkpoint path manifest differs: missing={missing} extra={extra}")
    preexisting = []
    for path in paths:
        exists = subprocess.run(
            ["git", "-C", str(ROOT), "cat-file", "-e", f"{BASE}:{path}"],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if exists.returncode == 0:
            preexisting.append(path)
    if len(preexisting) > 7 or not set(preexisting) <= PREEXISTING_ALLOWLIST:
        fail(f"pre-existing path allowlist differs: {preexisting}")
    return paths


def check_freezes(paths: list[str]) -> None:
    policy = tomllib.loads(
        (ROOT / "policy/source-boundaries.toml").read_text(encoding="utf-8")
    )
    for freeze_id, freeze in policy.get("freeze", {}).items():
        protected_paths = [*freeze.get("closed_build_input_roots", [])]
        protected_paths.extend(freeze.get("files", {}).keys())
        for protected in protected_paths:
            prefix = protected.rstrip("/") + "/"
            for path in paths:
                if path == protected or path.startswith(prefix):
                    fail(f"{path} intersects {freeze_id}:{protected}")


def make_sources(text: str) -> list[str]:
    match = re.search(
        r"^override C43_ARCHIVE_SOURCES\s*:=\s*(.*?)"
        r"\noverride C43_ARCHIVE_OBJECTS\s*:=",
        text,
        re.MULTILINE | re.DOTALL,
    )
    if match is None:
        fail("c43.mk archive source declaration missing")
    return re.findall(r"c43_[a-z0-9_]+\.c", match.group(1))


def checked_artifact(path_text: str, expected_name: str) -> Path:
    path = Path(path_text)
    if not path.is_absolute():
        path = (ROOT / "core/c4-nvme" / path).resolve()
    else:
        path = path.resolve()
    component = (ROOT / "core/c4-nvme/build").resolve()
    if path.name != expected_name or component not in path.parents:
        fail(f"unexpected {expected_name} artifact path: {path}")
    if not path.is_file():
        fail(f"required artifact is absent: {path}")
    return path


def nm_text(path: Path, *options: str) -> str:
    result = subprocess.run(
        ["nm", *options, str(path)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    return result.stdout


def check_artifacts(
    archive: Path, abi: Path, fake: Path, graph: Path, policy: Path
) -> None:
    artifacts = [archive, abi, fake, graph, policy]
    for left in range(len(artifacts)):
        for right in range(left):
            if os.path.samefile(artifacts[left], artifacts[right]):
                fail("C43 archive and ELF artifacts must be distinct")
    if not all(os.access(path, os.X_OK)
               for path in (abi, fake, graph, policy)):
        fail("C43 ELF artifacts must be executable")

    members = subprocess.run(
        ["ar", "t", str(archive)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout.splitlines()
    if members != EXPECTED_MEMBERS:
        fail(f"actual archive members differ: {members}")
    archive_defined = nm_text(archive, "-A", "--defined-only")
    fake_defined = nm_text(fake, "--defined-only")
    for member, symbol in EXPECTED_MEMBER_SYMBOLS.items():
        if not any(member in line and line.rstrip().endswith(f" {symbol}")
                   for line in archive_defined.splitlines()):
            fail(f"archive member {member} does not own {symbol}")
        if not any(line.rstrip().endswith(f" {symbol}")
                   for line in fake_defined.splitlines()):
            fail(f"fake-link did not whole-link {member}:{symbol}")
    graph_defined = nm_text(graph, "--defined-only")
    if not any(line.rstrip().endswith(" fwlab_c43_graph_prepare_start")
               for line in graph_defined.splitlines()):
        fail("graph ELF does not link graph prepare authority")
    policy_defined = nm_text(policy, "--defined-only")
    for symbol in ("fwlab_c43_policy_begin", "fwlab_c43_identify_encode"):
        if not any(line.rstrip().endswith(f" {symbol}")
                   for line in policy_defined.splitlines()):
            fail(f"policy ELF does not link {symbol}")

    provenance = (
        nm_text(archive, "-u") + "\n" + nm_text(fake, "-u") + "\n" +
        nm_text(graph, "-u") + "\n" + nm_text(policy, "-u")
    ).lower()
    for fragment in ("c42_", "c31_", "c35_", "vfio", "qemu", "pci_"):
        if fragment in provenance:
            fail(f"forbidden archive/fake symbol provenance: {fragment}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--archive", default="build/c43/libfwlab_c43.a"
    )
    parser.add_argument("--abi", default="build/c43/c43_public_abi")
    parser.add_argument("--fake", default="build/c43/c43_core_fake_link")
    parser.add_argument("--graph", default="build/c43/c43_graph_unit")
    parser.add_argument("--policy", default="build/c43/c43_policy_unit")
    arguments = parser.parse_args()

    paths = changed_paths()
    check_freezes(paths)

    c43_make = (ROOT / "core/c4-nvme/c43.mk").read_text(encoding="utf-8")
    if make_sources(c43_make) != EXPECTED_SOURCES:
        fail("archive membership or order differs from ADR-0011")
    for required in (
        "override C43_ARCHIVE_SOURCES :=",
        "override C43_ARCHIVE_OBJECTS :=",
        "override C43_PUBLIC_ABI_BIN :=",
        "override C43_FAKE_OUTPUT :=",
        "override C43_GRAPH_BIN :=",
        "override C43_POLICY_BIN :=",
        "C43_FROZEN_HEADERS",
        "-Wl,--whole-archive $(C43_ARCHIVE) -Wl,--no-whole-archive",
        "rm -f --",
    ):
        if required not in c43_make:
            fail(f"c43.mk authority fragment missing: {required}")
    shared_make = (ROOT / "core/c4-nvme/Makefile").read_text(encoding="utf-8")
    if shared_make.count("include c43.mk") != 1:
        fail("shared Makefile must include c43.mk exactly once")
    for forbidden in ("C43_ARCHIVE_SOURCES", "libfwlab_c43.a", "c43_instance.o"):
        if forbidden in shared_make:
            fail(f"shared Makefile duplicates C43 authority: {forbidden}")

    for relative in CORE_PATHS:
        path = ROOT / relative
        if not path.is_file():
            fail(f"missing required source: {relative}")
        text = path.read_text(encoding="utf-8")
        lowered = text.lower()
        for fragment in FORBIDDEN_CORE:
            if fragment.lower() in lowered:
                fail(f"forbidden core dependency {fragment!r} in {relative}")
        if relative.startswith("core/c4-nvme/c43"):
            for fragment in FORBIDDEN_PORTABLE_STATE:
                if fragment in text:
                    fail(f"raw/canonical state fragment {fragment!r} in {relative}")

    for relative in PUBLIC_HEADERS:
        text = (ROOT / relative).read_text(encoding="utf-8")
        if "__attribute__((packed))" in text or "#pragma pack" in text:
            fail(f"packed public ABI in {relative}")

    archive = checked_artifact(arguments.archive, "libfwlab_c43.a")
    abi = checked_artifact(arguments.abi, "c43_public_abi")
    fake = checked_artifact(arguments.fake, "c43_core_fake_link")
    graph = checked_artifact(arguments.graph, "c43_graph_unit")
    policy = checked_artifact(arguments.policy, "c43_policy_unit")
    check_artifacts(archive, abi, fake, graph, policy)

    path_digest = hashlib.sha256(
        ("\n".join(paths) + "\n").encode("utf-8")
    ).hexdigest()

    print(
        "C4.3 checkpoint architecture: PASS "
        f"(archive_members={len(EXPECTED_SOURCES)} changed_paths={len(paths)} "
        f"path_manifest={path_digest})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
