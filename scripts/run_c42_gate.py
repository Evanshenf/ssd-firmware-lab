#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Run the authoritative C4.2 gate outside mutable GNU Make semantics."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile

from c42_authority import (
    AuthorityError,
    AuthorityReceipt,
    materialize_c35_reference,
    verify_authority_lock,
    verify_c35_manifest,
)


ROOT = Path(__file__).resolve().parents[1]
FRONTEND = ROOT / "frontends/headless-c4"
MAKEFILE = FRONTEND / "Makefile"
MAKE = Path("/usr/bin/make")
SAFE_PATH = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
TIMEOUT_SECONDS = 1200
WORKSPACE_SIDE_EFFECT_PATHS = (
    ROOT / "frontends/headless-c35/build",
    ROOT / "scripts/__pycache__",
)
C35_MANIFEST = (
    ROOT / "frontends/headless-c4/evidence/c42a-p1/c35-reference.toml"
)
AUTHORITY_LOCK = (
    ROOT / "frontends/headless-c4/evidence/c42a-p1/authority.lock.toml"
)
EXPECTED_AUTHORITY_NODES = (
    "inventory-clang",
    "inventory-gcc",
    "obligation-generator",
    "provider-obligation-generator",
    "claim-validator",
    "c42-build-tests",
    "c42-architecture",
    "c4-architecture",
    "c35-build",
    "c35-checker",
    "authority-integrity",
    "dynamic-mutations",
    "provider-mutations",
    "make-integrity",
    "runner-integrity",
)

EXPECTED_BINARIES = (
    ("c42_queue_unit", "C4.2 queue unit: PASS"),
    ("c42_publication_unit", "C4.2 publication unit: PASS"),
    ("c42_identity_unit", "C4.2 identity unit: PASS"),
    ("c42_reset_delete_unit", "C4.2 reset/delete unit: PASS"),
    ("c42_remediation_unit", "C4.2a targeted remediation: PASS"),
    ("c42_provider_matrix", "C4.2 provider matrix: PASS"),
    ("c42_phase_cuts", "C4.2 phase cuts: PASS"),
    ("c42_dut_replay", "C4.2 DUT reference BFS: PASS"),
    ("c42_public_abi", "C4.2 public ABI: PASS"),
    ("c42_model", "C4.2 bounded model: PASS"),
    ("c42_broken", "C4.2 broken variants: PASS"),
    ("c42_fuzz", "C4.2 deterministic fuzz: PASS"),
    ("c42_headless_fake_link", "c42-fake-v1"),
)

FORBIDDEN_ENV_EXACT = {
    "MAKEFLAGS", "MFLAGS", "GNUMAKEFLAGS", "MAKEFILES",
    "MAKE_RESTARTS", "FWLAB_FAKE_OUTPUT", "BUILD_DIR",
}


def fail(message: str, detail: str | None = None) -> int:
    print(f"C4.2 authoritative runner: FAIL: {message}", file=sys.stderr)
    if detail:
        print(detail, file=sys.stderr)
    return 1


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def clean_environment(home: Path) -> dict[str, str]:
    return {
        "HOME": str(home),
        "LANG": "C",
        "LC_ALL": "C",
        "PATH": SAFE_PATH,
        "PYTHONDONTWRITEBYTECODE": "1",
        "TZ": "UTC",
    }


def output_has_marker(output: str, expected_marker: str) -> bool:
    """Accept one deliberate marker line, never an arbitrary substring."""
    return any(
        line == expected_marker or line.startswith(expected_marker + " ")
        for line in output.splitlines()
    )


def hostile_environment() -> list[str]:
    names: list[str] = []
    for name, value in os.environ.items():
        if not value:
            continue
        if name in FORBIDDEN_ENV_EXACT or name.startswith("C42_") or \
                name.startswith("_FWLAB_"):
            names.append(name)
    return sorted(names)


def workspace_side_effect_state() -> tuple[tuple[object, ...], ...]:
    state: list[tuple[object, ...]] = []

    for root in WORKSPACE_SIDE_EFFECT_PATHS:
        relative_root = root.relative_to(ROOT).as_posix()
        if not root.exists() and not root.is_symlink():
            state.append((relative_root, "absent"))
            continue
        for path in [root, *sorted(root.rglob("*"))]:
            status = path.lstat()
            relative = path.relative_to(ROOT).as_posix()
            if path.is_symlink():
                state.append((relative, "symlink", os.readlink(path)))
            elif path.is_file():
                state.append((
                    relative, "file", status.st_mode, status.st_size,
                    status.st_mtime_ns,
                ))
            else:
                state.append((relative, "directory", status.st_mode))
    return tuple(state)


def validate_binary(
    path: Path,
    expected_marker: str,
    environment: dict[str, str],
) -> tuple[str, tuple[int, int]]:
    if path.is_symlink() or not path.is_file():
        raise RuntimeError(f"missing or non-regular artifact: {path.name}")
    mode = path.stat().st_mode
    if not stat.S_ISREG(mode) or mode & 0o111 == 0:
        raise RuntimeError(f"artifact is not executable: {path.name}")
    with path.open("rb") as stream:
        if stream.read(4) != b"\x7fELF":
            raise RuntimeError(f"artifact is not ELF: {path.name}")
    run = subprocess.run(
        [str(path)], cwd=ROOT, env=environment, check=False,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=180,
    )
    if run.returncode != 0 or not output_has_marker(
            run.stdout, expected_marker):
        raise RuntimeError(
            f"artifact execution mismatch: {path.name}: rc={run.returncode}"
        )
    status = path.stat()
    return sha256(path), (status.st_dev, status.st_ino)


def validate_receipt(
    receipt: Path,
    paths: list[Path],
    digests: list[str],
) -> None:
    if receipt.is_symlink() or not receipt.is_file():
        raise RuntimeError("execution receipt is missing or non-regular")
    try:
        lines = receipt.read_text(encoding="utf-8").splitlines()
    except UnicodeError as error:
        raise RuntimeError("execution receipt is not valid UTF-8") from error
    if len(lines) != len(paths):
        raise RuntimeError("execution receipt line count differs")
    for index, line in enumerate(lines):
        fields = line.split()
        if len(fields) != 2 or fields[0] != digests[index] or \
                Path(fields[1]) != paths[index]:
            raise RuntimeError(
                f"execution receipt entry differs at ordinal {index}"
            )


def validate_distinct_artifacts(
    realpaths: list[Path],
    inodes: list[tuple[int, int]],
    digests: list[str],
) -> None:
    expected = len(EXPECTED_BINARIES)
    if len(realpaths) != expected or len(set(realpaths)) != expected or \
            len(inodes) != expected or len(set(inodes)) != expected:
        raise RuntimeError("artifact path or inode alias detected")
    if len(digests) != expected or len(set(digests)) != expected:
        raise RuntimeError("artifact digests are not distinct")


def main() -> int:
    if len(sys.argv) != 1:
        return fail("arguments are forbidden")
    hostile = hostile_environment()
    if hostile:
        return fail("unsafe inherited environment", ",".join(hostile))
    if not MAKE.is_file() or not MAKEFILE.is_file():
        return fail("fixed Make executable or Makefile is unavailable")

    try:
        verify_authority_lock(ROOT, AUTHORITY_LOCK)
        authority_lock_digest = sha256(AUTHORITY_LOCK)
        workspace_before = workspace_side_effect_state()
        with tempfile.TemporaryDirectory(prefix="c42-authoritative-") as name:
            temporary = Path(name)
            build = temporary / "build"
            home = temporary / "home"
            home.mkdir()
            environment = clean_environment(home)
            authority = AuthorityReceipt(ROOT, temporary)

            def authorized(
                node_id: str,
                argv: list[str],
                *,
                cwd: Path = ROOT,
                timeout: int = TIMEOUT_SECONDS,
                outputs: tuple[Path, ...] = (),
            ) -> subprocess.CompletedProcess[str]:
                result = authority.run(
                    node_id, argv, cwd=cwd, environment=environment,
                    timeout=timeout, expected_outputs=outputs,
                )
                if result.returncode != 0:
                    raise RuntimeError(
                        f"authority child failed: {node_id}:\n{result.stdout}"
                    )
                return result

            python = sys.executable
            authorized("inventory-clang", [
                python, str(ROOT / "scripts/extract_c42_interface_inventory.py"),
                "--check", "--cc", "clang",
            ], timeout=120)
            authorized("inventory-gcc", [
                python, str(ROOT / "scripts/extract_c42_interface_inventory.py"),
                "--check", "--cc", "gcc",
            ], timeout=120)
            authorized("obligation-generator", [
                python, str(ROOT / "scripts/gen_c42_obligations.py"), "--check",
            ], timeout=120)
            provider_stimuli = temporary / "provider-obligations.inc"
            authorized("provider-obligation-generator", [
                python,
                str(ROOT / "scripts/gen_c42_provider_obligations.py"),
                "--output", str(provider_stimuli),
            ], timeout=120, outputs=(provider_stimuli,))
            authorized("claim-validator", [
                python, str(ROOT / "scripts/check_c42_claim_models.py"),
                "--self-test",
            ], timeout=120)

            paths = [build / binary for binary, _ in EXPECTED_BINARIES]
            command = [
                str(MAKE), "-f", str(MAKEFILE),
                "-C", str(FRONTEND), "CC=cc", f"BUILD_DIR={build}",
                f"FWLAB_FAKE_OUTPUT={build / 'c42_headless_fake_link'}",
                "check-c42-build-tests",
            ]
            authorized(
                "c42-build-tests", command,
                outputs=tuple([*paths, build / "c42_execution.receipt"]),
            )

            authorized("c42-architecture", [
                python, str(ROOT / "scripts/check_c42_architecture.py"),
                "--cc", "cc", "--no-c4-child",
            ], timeout=900)
            authorized("c4-architecture", [
                python, str(ROOT / "scripts/check_c4_architecture.py"),
                "--cc", "cc", "--no-c35-child",
            ], timeout=300)

            c35_manifest = verify_c35_manifest(ROOT, C35_MANIFEST, "cc")
            c35_root = temporary / "c35-reference"
            materialize_c35_reference(ROOT, c35_manifest, c35_root)
            c35_component = c35_root / "frontends/headless-c35"
            c35_build = c35_component / "build"
            c35_outputs = tuple(
                c35_build / value for value in (
                    "libfwlab_portable_core_c31_c34.a",
                    "c35_lane_s", "c35_lane_m", "c35_lane_b", "c35_lane_p",
                )
            )
            authorized("c35-build", [
                "make", "-C", str(c35_component), "CC=cc", "AR=ar",
                "BUILD_DIR=build",
                "CFLAGS=-std=c11 -O2 -g0 -Wall -Wextra -Werror "
                "-Wpedantic -fno-common",
                "LDFLAGS=", "lanes",
            ], cwd=c35_root, timeout=600, outputs=c35_outputs)
            c35_checked = authorized("c35-checker", [
                python, str(c35_root / "scripts/check_c35_architecture.py"),
            ], cwd=c35_root, timeout=300)
            if "C3.5c architecture: PASS " not in c35_checked.stdout or \
                    "archive=b1f95f2787fe9a3d585f467d4ba72e6de32938c51273d4ad7f411dc1e644cf6f" \
                    not in c35_checked.stdout:
                raise RuntimeError("direct C3.5 checker marker/hash differs")

            authorized("authority-integrity", [
                python, str(ROOT / "scripts/check_c42_authority.py"),
            ], timeout=300)

            authorized("dynamic-mutations", [
                python, str(ROOT / "scripts/check_c42_dynamic_mutations.py"),
            ], timeout=900)
            authorized("provider-mutations", [
                python, str(ROOT / "scripts/check_c42_provider_mutations.py"),
            ], timeout=900)
            authorized("make-integrity", [
                python, str(ROOT / "scripts/check_c42_make_integrity.py"),
            ], timeout=300)
            authorized("runner-integrity", [
                python, str(ROOT / "scripts/check_c42_runner_integrity.py"),
            ], timeout=300)

            digests: list[str] = []
            inodes: list[tuple[int, int]] = []
            realpaths: list[Path] = []
            for path, (_, marker) in zip(paths, EXPECTED_BINARIES):
                digest, inode = validate_binary(path, marker, environment)
                resolved = path.resolve(strict=True)
                realpaths.append(resolved)
                inodes.append(inode)
                digests.append(digest)
            validate_distinct_artifacts(realpaths, inodes, digests)
            validate_receipt(
                build / "c42_execution.receipt", paths, digests
            )
            authority_digest, authority_nodes = authority.finalize(
                EXPECTED_AUTHORITY_NODES
            )
        if workspace_side_effect_state() != workspace_before:
            raise RuntimeError("gate modified guarded source-workspace output")
    except (AuthorityError, OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        return fail(str(error))

    print(
        "C4.2 authoritative runner: PASS "
        f"binaries={len(EXPECTED_BINARIES)} distinct=exact receipt=exact "
        f"authority={authority_digest} nodes={authority_nodes} "
        f"lock={authority_lock_digest} "
        "workspace=unchanged"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
