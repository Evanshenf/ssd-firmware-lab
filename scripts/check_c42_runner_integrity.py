#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Fast fail-closed controls for the source-bound C4.2 gate runner."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "scripts/run_c42_gate.py"


def invoke(
    arguments: tuple[str, ...] = (),
    extra_environment: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    for name in (
        "MAKEFLAGS", "MFLAGS", "GNUMAKEFLAGS", "MAKEFILES",
        "MAKE_RESTARTS", "FWLAB_FAKE_OUTPUT", "BUILD_DIR",
    ):
        environment.pop(name, None)
    if extra_environment:
        environment.update(extra_environment)
    return subprocess.run(
        [sys.executable, str(RUNNER), *arguments], cwd=ROOT,
        env=environment, check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10,
    )


def load_runner():
    spec = importlib.util.spec_from_file_location("c42_gate_runner", RUNNER)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load authoritative runner")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    failures: list[str] = []
    for arguments in (("-n",), ("-t",), ("-q",), ("-f", "other.mk")):
        result = invoke(arguments)
        if result.returncode != 1 or "arguments are forbidden" not in result.stdout:
            failures.append(f"runner accepted argv mode: {arguments[0]}")
    for name, value in (
        ("MAKEFLAGS", "-n"),
        ("GNUMAKEFLAGS", "-t"),
        ("MAKEFILES", "late.mk"),
        ("C42_PROVIDER_BIN", "/bin/true"),
        ("_FWLAB_FINAL_PARSE_GUARD", ""),
    ):
        result = invoke(extra_environment={name: value or "override"})
        if result.returncode != 1 or \
                "unsafe inherited environment" not in result.stdout:
            failures.append(f"runner accepted inherited input: {name}")

    try:
        runner = load_runner()
        with tempfile.TemporaryDirectory(
                prefix="c42-runner-integrity-") as name:
            fake = Path(name) / "pass-script"
            fake.write_text("#!/bin/sh\necho PASS\n", encoding="utf-8")
            fake.chmod(0o755)
            try:
                runner.validate_binary(
                    fake, "PASS", runner.clean_environment(Path(name))
                )
                failures.append("runner accepted a non-ELF PASS script")
            except RuntimeError:
                pass
        expected = len(runner.EXPECTED_BINARIES)
        duplicate_paths = [Path("/same")] * expected
        duplicate_inodes = [(1, 1)] * expected
        duplicate_hashes = ["0" * 64] * expected
        try:
            runner.validate_distinct_artifacts(
                duplicate_paths, duplicate_inodes, duplicate_hashes
            )
            failures.append("runner accepted aliased artifacts")
        except RuntimeError:
            pass
    except (OSError, RuntimeError) as error:
        failures.append(str(error))

    if failures:
        print("C4.2 authoritative runner integrity: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print(
        "C4.2 authoritative runner integrity: PASS "
        "(argv/env rejected; non-ELF/aliases rejected)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
