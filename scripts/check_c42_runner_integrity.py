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

from check_c42_claim_models import ModelError, build_model


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
    try:
        owned = [
            obligation for obligation in build_model()["obligations"]
            if obligation.get("executor_id") == "runner_integrity"
        ]
        if len(owned) != 1 or \
                owned[0].get("mutant_id") != "BA_RUNNER_HOSTILE_MAKEFLAGS" or \
                owned[0].get("expected_diagnostic_ids") != [
                    "runner accepted inherited input: MAKEFLAGS"
                ]:
            raise ModelError("runner owned-canary mapping differs")
        runner = load_runner()
    except (ModelError, OSError, RuntimeError) as error:
        print(f"C4.2 authoritative runner integrity: FAIL: {error}",
              file=sys.stderr)
        return 1
    for arguments in (("-n",), ("-t",), ("-q",), ("-f", "other.mk")):
        result = invoke(arguments)
        if result.returncode != 1 or "arguments are forbidden" not in result.stdout:
            failures.append(f"runner accepted argv mode: {arguments[0]}")
    hostile_values = {
        "MAKEFLAGS": "-n", "MFLAGS": "-t", "GNUMAKEFLAGS": "-q",
        "MAKEFILES": "late.mk", "MAKE_RESTARTS": "1",
        "FWLAB_FAKE_OUTPUT": "/bin/true", "BUILD_DIR": "/tmp/alias",
    }
    hostile_values.update({
        "C42_PROVIDER_BIN": "/bin/true",
        "_FWLAB_FINAL_PARSE_GUARD": "override",
    })
    if set(hostile_values) != set(runner.FORBIDDEN_ENV_EXACT) | {
            "C42_PROVIDER_BIN", "_FWLAB_FINAL_PARSE_GUARD"}:
        failures.append("runner inherited-input fixture set is incomplete")
    for name, value in sorted(hostile_values.items()):
        result = invoke(extra_environment={name: value or "override"})
        if result.returncode != 1 or \
                "unsafe inherited environment" not in result.stdout:
            failures.append(f"runner accepted inherited input: {name}")

    try:
        with tempfile.TemporaryDirectory(
                prefix="c42-runner-integrity-") as name:
            root = Path(name)
            fake = root / "pass-script"
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
        unique_paths = [Path(f"/artifact-{index}")
                        for index in range(expected)]
        unique_inodes = [(1, index + 1) for index in range(expected)]
        unique_hashes = [f"{index:064x}" for index in range(expected)]
        alias_cases = (
            ("path", [Path("/same")] * expected,
             unique_inodes, unique_hashes),
            ("inode", unique_paths, [(1, 1)] * expected, unique_hashes),
            ("digest", unique_paths, unique_inodes,
             ["0" * 64] * expected),
        )
        for label, paths, inodes, digests in alias_cases:
            try:
                runner.validate_distinct_artifacts(paths, inodes, digests)
                failures.append(f"runner accepted duplicate {label}")
            except RuntimeError:
                pass

        with tempfile.TemporaryDirectory(
                prefix="c42-receipt-integrity-") as name:
            root = Path(name)
            paths = [root / f"artifact-{index}"
                     for index in range(expected)]
            digests = [f"{index + 1:064x}" for index in range(expected)]
            receipt = root / "receipt"
            valid_lines = [
                f"{digest} {path}" for digest, path in zip(digests, paths)
            ]

            def rejected(label: str) -> None:
                try:
                    runner.validate_receipt(receipt, paths, digests)
                    failures.append(f"runner accepted malformed receipt: {label}")
                except RuntimeError:
                    pass

            receipt.write_text(
                "\n".join(valid_lines[:-1]) + "\n", encoding="utf-8"
            )
            rejected("line-count")
            changed = valid_lines.copy()
            changed[0] += " extra"
            receipt.write_text("\n".join(changed) + "\n", encoding="utf-8")
            rejected("extra-field")
            changed = valid_lines.copy()
            changed[0] = f"{'f' * 64} {paths[0]}"
            receipt.write_text("\n".join(changed) + "\n", encoding="utf-8")
            rejected("digest")
            changed = valid_lines.copy()
            changed[0] = f"{digests[0]} {root / 'wrong'}"
            receipt.write_text("\n".join(changed) + "\n", encoding="utf-8")
            rejected("path")
            changed = valid_lines.copy()
            changed[0], changed[1] = changed[1], changed[0]
            receipt.write_text("\n".join(changed) + "\n", encoding="utf-8")
            rejected("order")
            receipt.write_bytes(b"\xff\xfe\n")
            rejected("utf-8")

        if runner.output_has_marker("not PASS", "PASS") or \
                runner.output_has_marker("embedded-PASS-text", "PASS") or \
                not runner.output_has_marker("PASS details", "PASS"):
            failures.append("runner runtime marker is not line anchored")
    except (OSError, RuntimeError) as error:
        failures.append(str(error))

    if failures:
        print("C4.2 authoritative runner integrity: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print(f"C4.2 owned kill: {owned[0]['id']}")
    print(
        "C4.2 authoritative runner integrity: PASS "
        "(argv/all-env rejected; non-ELF/path-inode-digest aliases rejected; "
        "receipt/marker negatives exact)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
