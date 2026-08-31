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
        r"^[ \t]*(?:override[ \t]+)?"
        r"(?:MAKEFLAGS|MFLAGS|GNUMAKEFLAGS|MAKEFILES)"
        r"[ \t]*[:+?]?=",
        logical,
        re.MULTILINE,
    ):
        return "MAKEFLAGS/MFLAGS/GNUMAKEFLAGS/MAKEFILES"
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
            ["make", "-C", str(root / "headless-c4"),
             "check-c42-build-closure"]
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
        "GNUMAKEFLAGS": "GNUMAKEFLAGS += -i\n",
        "MAKEFILES": "MAKEFILES := injected.mk\n",
    }
    for name, addition in mutants.items():
        mutant = source + "\n" + addition
        if unsafe_directive(mutant) is None:
            failures.append(f"static {name} negative escaped")
        if not parse_guard_mutant(source, addition):
            failures.append(f"executed {name} negative escaped")

    inherited = os.environ.copy()
    inherited.pop("MAKEFILES", None)
    inherited.pop("GNUMAKEFLAGS", None)
    inherited["MAKEFLAGS"] = "-i"
    ignored = run(
        ["make", "-C", str(FRONTEND), "check-c42-build-closure"],
        env=inherited,
    )
    if ignored.returncode == 0 or "C4.2 refuses" not in ignored.stdout:
        failures.append("inherited ignore-error mode escaped")

    for flag in ("-n", "-t", "-q", "--dry-run", "--touch", "--question"):
        inherited = os.environ.copy()
        inherited.pop("MAKEFILES", None)
        inherited.pop("MAKEFLAGS", None)
        inherited.pop("GNUMAKEFLAGS", None)
        mode = run(
            ["make", flag, "-C", str(FRONTEND),
             "check-c42-build-closure"], env=inherited
        )
        if mode.returncode == 0 or \
                "refuses non-executing Make modes" not in mode.stdout:
            failures.append(f"command-line Make mode escaped: {flag}")

    for name in ("MAKEFLAGS", "GNUMAKEFLAGS"):
        inherited = os.environ.copy()
        inherited.pop("MAKEFILES", None)
        inherited.pop("MAKEFLAGS", None)
        inherited.pop("GNUMAKEFLAGS", None)
        inherited[name] = "-n"
        mode = run(
            ["make", "-C", str(FRONTEND),
             "check-c42-build-closure"], env=inherited
        )
        if mode.returncode == 0 or \
                "refuses non-executing Make modes" not in mode.stdout:
            failures.append(f"inherited non-executing mode escaped: {name}")

    inherited = os.environ.copy()
    inherited.pop("MAKEFILES", None)
    inherited.pop("GNUMAKEFLAGS", None)
    inherited["MAKEFLAGS"] = "--eval=.IGNORE:"
    evaluated = run(
        ["make", "-C", str(FRONTEND), "check-c42-build-closure"],
        env=inherited,
    )
    if evaluated.returncode == 0 or "C4.2 refuses" not in evaluated.stdout:
        failures.append("inherited MAKEFLAGS --eval escaped")

    inherited = os.environ.copy()
    inherited.pop("MAKEFILES", None)
    inherited.pop("MAKEFLAGS", None)
    inherited["GNUMAKEFLAGS"] = "-E .IGNORE:"
    evaluated = run(
        ["make", "-C", str(FRONTEND), "check-c42-build-closure"],
        env=inherited,
    )
    if evaluated.returncode == 0 or "C4.2 refuses" not in evaluated.stdout:
        failures.append("inherited GNUMAKEFLAGS -E escaped")

    with tempfile.TemporaryDirectory(prefix="c42-inherited-makefiles-") as name:
        root = Path(name)
        benign = root / "benign.mk"
        ignored_file = root / "ignored.mk"
        nested = root / "nested.mk"
        benign.write_text("# benign inherited makefile\n", encoding="utf-8")
        ignored_file.write_text(".IGNORE:\n", encoding="utf-8")
        nested.write_text(
            f"include {ignored_file}\n", encoding="utf-8"
        )
        neutralized = root / "neutralized.mk"
        neutralized.write_text(
            "MAKEFILES :=\ncheck-c42-unit: SHELL := /bin/true\n",
            encoding="utf-8",
        )
        for label, makefiles in (
            ("multiple", f"{benign} {ignored_file}"),
            ("nested", str(nested)),
            ("neutralized", str(neutralized)),
        ):
            inherited = os.environ.copy()
            inherited.pop("MAKEFLAGS", None)
            inherited.pop("GNUMAKEFLAGS", None)
            inherited["MAKEFILES"] = makefiles
            result = run(
                ["make", "-C", str(FRONTEND),
                 "check-c42-build-closure"],
                env=inherited,
            )
            if result.returncode == 0 or \
                    "C4.2 refuses" not in result.stdout:
                failures.append(f"inherited {label} MAKEFILES escaped")

        for label, makefiles in (
            ("before", (benign, MAKEFILE)),
            ("after", (MAKEFILE, benign)),
            ("late-ignore", (MAKEFILE, ignored_file)),
            ("late-target-shell", (MAKEFILE, neutralized)),
        ):
            result = run(
                [
                    "make", "-f", str(makefiles[0]), "-f",
                    str(makefiles[1]), "-C", str(FRONTEND),
                    "check-c42-build-closure",
                ],
            )
            if result.returncode == 0 or \
                    "C4.2 refuses" not in result.stdout:
                failures.append(
                    f"multiple -f primary Makefiles ({label}) escaped"
                )

    inherited = os.environ.copy()
    inherited.pop("MAKEFILES", None)
    inherited.pop("GNUMAKEFLAGS", None)
    inherited["MAKEFLAGS"] = "SHELL=/bin/true"
    shell = run(
        ["make", "-C", str(FRONTEND), "-pRr",
         "check-c42-build-closure"], env=inherited
    )
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
        "MAKEFILES/eval/non-executing modes/shell rejected; "
        "broken compiler no artifact)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
