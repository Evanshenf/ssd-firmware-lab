#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Run GCC and Clang analyzers over the exact C4.2 HIF and fake sources."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "include"
HIF = ROOT / "frontends/headless-c4"
SOURCES = [
    HIF / "hif/c42_identity.c",
    HIF / "hif/c42_queue.c",
    HIF / "hif/c42_publication.c",
    HIF / "hif/c42_runtime.c",
    HIF / "fakes/c42_event.c",
    HIF / "fakes/c42_memory.c",
    HIF / "fakes/c42_command.c",
]


def flags(compiler: str) -> list[str]:
    return [
        compiler, f"-I{INCLUDE}", f"-I{HIF}", "-std=c11", "-O1",
        "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fno-common",
    ]


def main() -> int:
    if shutil.which("gcc") is None or shutil.which("clang") is None:
        print("C4.2 analysis: FAIL: gcc/clang missing", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="c42-analysis-") as directory:
            output = Path(directory)
            for index, source in enumerate(SOURCES):
                subprocess.check_call([
                    *flags("gcc"), "-fanalyzer", "-c", str(source),
                    "-o", str(output / f"gcc-{index}.o"),
                ], cwd=ROOT)
                subprocess.check_call([
                    *flags("clang"), "--analyze", str(source),
                    "-o", str(output / f"clang-{index}.plist"),
                ], cwd=ROOT)
    except subprocess.CalledProcessError as error:
        print(f"C4.2 analysis: FAIL: {error}", file=sys.stderr)
        return 1
    print(f"C4.2 analysis: PASS ({len(SOURCES)} exact sources; "
          "GCC -fanalyzer + Clang --analyze)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
