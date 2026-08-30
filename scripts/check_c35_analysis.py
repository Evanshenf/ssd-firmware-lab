#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Run GCC and Clang static analyzers over the exact C3.5a production graph."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]

SOURCES = [
    ROOT / "core" / "c31.c",
    ROOT / "core" / "c31_codec.c",
    ROOT / "core" / "c32" / "c32_policy.c",
    *(sorted((ROOT / "core" / "c34").glob("*.c"))),
    ROOT / "frontends" / "headless-c35" / "c35_binding.c",
    ROOT / "frontends" / "headless-c35" / "c35_lifecycle_port.c",
    ROOT / "frontends" / "headless-c35" / "c35_profile.c",
    ROOT / "frontends" / "headless-c35" / "c35_headless.c",
    ROOT / "frontends" / "headless-c35" / "c35_finalizer.c",
    ROOT / "frontends" / "headless-c35" / "c35_trace.c",
    ROOT / "frontends" / "headless-c35" / "c35_bundle.c",
    ROOT / "frontends" / "headless-c35" / "bindings" / "c35_c34.c",
    ROOT / "frontends" / "headless-c35" / "bindings" / "c35_scripted.c",
]

INCLUDES = [
    ROOT / "include",
    ROOT / "core" / "c34",
    ROOT / "core" / "c34" / "fakes",
    ROOT / "core" / "fakes",
    ROOT / "frontends" / "headless-c35",
    ROOT / "frontends" / "headless-c35" / "bindings",
]


def command_base(compiler: str) -> list[str]:
    command = [
        compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
        "-Wpedantic", "-fno-common",
    ]
    for include in INCLUDES:
        command.extend(["-I", str(include)])
    return command


def main() -> int:
    if shutil.which("gcc") is None or shutil.which("clang") is None:
        print("C3.5a analysis: FAIL: gcc/clang missing", file=sys.stderr)
        return 1
    try:
        with tempfile.TemporaryDirectory(prefix="c35-analysis-") as directory:
            output = Path(directory)
            for index, source in enumerate(SOURCES):
                subprocess.check_call([
                    *command_base("gcc"), "-fanalyzer", "-c", str(source),
                    "-o", str(output / f"gcc-{index}.o"),
                ], cwd=ROOT)
                subprocess.check_call([
                    *command_base("clang"), "--analyze", str(source),
                    "-o", str(output / f"clang-{index}.plist"),
                ], cwd=ROOT)
    except subprocess.CalledProcessError as error:
        print(f"C3.5a analysis: FAIL: {error}", file=sys.stderr)
        return 1
    print(f"C3.5a analysis: PASS ({len(SOURCES)} exact sources; "
          "GCC -fanalyzer + Clang --analyze)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
