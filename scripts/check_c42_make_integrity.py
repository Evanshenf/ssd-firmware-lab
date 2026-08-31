#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Prove C4.2 Make gates fail closed under ignore and shell attacks."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
FRONTEND = ROOT / "frontends/headless-c4"
MAKEFILE = FRONTEND / "Makefile"


def unsafe_directive(text: str) -> str | None:
    logical = re.sub(r"\\\n[ \t]*", " ", text)
    if re.search(r"^[ \t]*\.IGNORE(?:[ \t]|:)", logical, re.MULTILINE):
        return ".IGNORE"
    if re.search(
        r"^[ \t]*(?:override[ \t]+)?(?:MAKEFLAGS|MFLAGS)"
        r"[ \t]*[:+?]?=",
        logical,
        re.MULTILINE,
    ):
        return "MAKEFLAGS/MFLAGS"
    return None


def run(
    command: list[str],
    *,
    cwd: Path = ROOT,
    env: dict[str, str] | None = None,
    timeout: int = 120,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        env=env,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
    )


def parse_guard_mutant(source: str, addition: str) -> bool:
    with tempfile.TemporaryDirectory(prefix="c42-make-integrity-") as name:
        root = Path(name)
        shutil.copytree(FRONTEND, root / "headless-c4")
        license_line = "# SPDX-License-" "Identifier: BSD-3-Clause\n"
        mutant = source.replace(
            license_line,
            license_line + "\n" + addition,
            1,
        )
        (root / "headless-c4/Makefile").write_text(
            mutant, encoding="utf-8"
        )
        result = run(
            ["make", "-C", str(root / "headless-c4"), "-n", "check-c42-unit"]
        )
        return result.returncode != 0 and "C4.2 refuses" in result.stdout


def main() -> int:
    source = MAKEFILE.read_text(encoding="utf-8")
    failures: list[str] = []

    if unsafe_directive(source) is not None:
        failures.append("live Makefile contains an unsafe directive")
    mutants = {
        ".IGNORE": "CC := /bin/false\n.IGNORE:\n",
        "MAKEFLAGS": "CC := /bin/false\nMAKEFLAGS += -i\n",
    }
    for name, addition in mutants.items():
        mutant = source + "\n" + addition
        if unsafe_directive(mutant) is None:
            failures.append(f"static {name} negative escaped")
        if not parse_guard_mutant(source, addition):
            failures.append(f"executed {name} negative escaped")

    inherited = os.environ.copy()
    inherited["MAKEFLAGS"] = "-i"
    ignored = run(
        ["make", "-C", str(FRONTEND), "-n", "check-c42-unit"],
        env=inherited,
    )
    if ignored.returncode == 0 or "C4.2 refuses" not in ignored.stdout:
        failures.append("inherited ignore-error mode escaped")

    inherited = os.environ.copy()
    inherited["MAKEFLAGS"] = "SHELL=/bin/true"
    shell = run(["make", "-C", str(FRONTEND), "-prRn"], env=inherited)
    if shell.returncode != 0 or "SHELL := /bin/sh" not in shell.stdout or \
            ".SHELLFLAGS := -eu -c" not in shell.stdout:
        failures.append("inherited shell override was not neutralized")

    with tempfile.TemporaryDirectory(prefix="c42-broken-cc-") as name:
        output = Path(name)
        binary = output / "c42_provider_matrix"
        broken = run(
            [
                "make", "-C", str(FRONTEND), "CC=/bin/false",
                f"BUILD_DIR={output}", str(binary),
            ]
        )
        if broken.returncode == 0 or binary.exists():
            failures.append("broken compiler produced a false-green artifact")

    if failures:
        print("C4.2 Make integrity: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print(
        "C4.2 Make integrity: PASS (.IGNORE/MAKEFLAGS exact negatives; "
        "inherited flags/shell sanitized; broken compiler no artifact)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
