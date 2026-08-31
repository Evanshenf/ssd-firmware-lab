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


ROOT = Path(__file__).resolve().parents[1]
FRONTEND = ROOT / "frontends/headless-c4"
MAKEFILE = FRONTEND / "Makefile"
MAKE = Path("/usr/bin/make")
SAFE_PATH = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
TIMEOUT_SECONDS = 1200

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
        "TZ": "UTC",
    }


def hostile_environment() -> list[str]:
    names: list[str] = []
    for name, value in os.environ.items():
        if not value:
            continue
        if name in FORBIDDEN_ENV_EXACT or name.startswith("C42_") or \
                name.startswith("_FWLAB_"):
            names.append(name)
    return sorted(names)


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
    if run.returncode != 0 or expected_marker not in run.stdout:
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
    lines = receipt.read_text(encoding="utf-8").splitlines()
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
        with tempfile.TemporaryDirectory(prefix="c42-authoritative-") as name:
            temporary = Path(name)
            build = temporary / "build"
            home = temporary / "home"
            home.mkdir()
            environment = clean_environment(home)
            command = [
                str(MAKE), "-f", str(MAKEFILE),
                "-C", str(FRONTEND), "CC=cc", f"BUILD_DIR={build}",
                f"FWLAB_FAKE_OUTPUT={build / 'c42_headless_fake_link'}",
                "check-c42",
            ]
            run = subprocess.run(
                command, cwd=ROOT, env=environment, check=False, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                timeout=TIMEOUT_SECONDS,
            )
            if run.returncode != 0:
                return fail("fixed Make execution failed", run.stdout)

            paths = [build / binary for binary, _ in EXPECTED_BINARIES]
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
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        return fail(str(error))

    print(
        "C4.2 authoritative runner: PASS "
        f"binaries={len(EXPECTED_BINARIES)} distinct=exact receipt=exact"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
