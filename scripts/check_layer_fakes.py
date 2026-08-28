#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Build each implemented architecture layer against its fake dependencies."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib


ROOT = Path(__file__).resolve().parents[1]
POLICY = ROOT / "policy/source-boundaries.toml"
EXPECTED_POLICY = {
    "roots": ["core", "nfc", "media", "frontends"],
    "target": "fake-link",
    "output_variable": "FWLAB_FAKE_OUTPUT",
    "source_extensions": [".asm", ".c", ".cc", ".cpp", ".cxx", ".s"],
    "ignored_directories": ["build", "fakes", "out", "tests"],
    "require_elf": True,
    "run_output": True,
}


def load_policy() -> dict:
    try:
        with POLICY.open("rb") as stream:
            policy = tomllib.load(stream).get("layer_fakes", {})
    except (OSError, tomllib.TOMLDecodeError) as error:
        print(f"Layer fake policy is unavailable: {error}", file=sys.stderr)
        raise SystemExit(1) from error
    if policy != EXPECTED_POLICY:
        print("Layer fake policy changed or is incomplete", file=sys.stderr)
        raise SystemExit(1)
    return policy


def main() -> int:
    policy = load_policy()
    suffixes = set(policy["source_extensions"])
    ignored = set(policy["ignored_directories"])
    failures = []
    implemented: list[Path] = []
    for root_name in policy["roots"]:
        layer = ROOT / root_name
        if not layer.is_dir() or layer.is_symlink():
            failures.append(f"{root_name}: layer root is missing or not a directory")
            continue
        sources = []
        for path in sorted(layer.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in suffixes:
                continue
            relative = path.relative_to(layer)
            if ignored.intersection(relative.parts):
                continue
            sources.append(relative)
        if not sources:
            print(f"Layer fake link: SKIP ({root_name}/ remains design-only)")
            continue
        implemented.append(layer)

    with tempfile.TemporaryDirectory(prefix="fwlab-layer-fakes-") as temporary:
        output_root = Path(temporary)
        for index, layer in enumerate(implemented):
            makefile = layer / "Makefile"
            label = layer.relative_to(ROOT)
            if not makefile.is_file() or makefile.is_symlink():
                failures.append(f"{label}: implementation exists without a Makefile")
                continue
            output = output_root / f"layer-{index}.fake"
            print(f"Layer fake link: {label}")
            try:
                result = subprocess.run(
                    [
                        "make", "-f", "Makefile", "-C", str(layer),
                        policy["target"],
                        f"{policy['output_variable']}={output}",
                    ],
                    cwd=ROOT,
                    check=False,
                    timeout=120,
                )
            except (OSError, subprocess.TimeoutExpired) as error:
                failures.append(f"{label}: cannot run fake-link target: {error}")
                continue
            if result.returncode:
                failures.append(
                    f"{label}: {policy['target']} exited with "
                    f"{result.returncode}"
                )
                continue
            if not output.is_file() or output.is_symlink():
                failures.append(
                    f"{label}: {policy['target']} did not create "
                    f"{policy['output_variable']}"
                )
                continue
            if policy["require_elf"]:
                with output.open("rb") as stream:
                    if stream.read(4) != b"\x7fELF":
                        failures.append(
                            f"{label}: fake-link output is not an ELF binary"
                        )
                        continue
            if policy["run_output"]:
                if not os.access(output, os.X_OK):
                    failures.append(f"{label}: fake-link output is not executable")
                    continue
                try:
                    run = subprocess.run(
                        [str(output)], cwd=ROOT, check=False, timeout=30
                    )
                except (OSError, subprocess.TimeoutExpired) as error:
                    failures.append(
                        f"{label}: cannot execute fake-link output: {error}"
                    )
                    continue
                if run.returncode:
                    failures.append(
                        f"{label}: fake-link output exited with "
                        f"{run.returncode}"
                    )

    if failures:
        print("Layer fake links failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("Layer fake links: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
