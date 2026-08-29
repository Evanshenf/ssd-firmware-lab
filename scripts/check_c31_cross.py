#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Cross-build and execute the C3.1 gates on AArch64 and RISC-V."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "core"
INCLUDE = ROOT / "include"
SOURCES = [
    CORE / "c31.c",
    CORE / "c31_codec.c",
    CORE / "fakes/c31_fake_provider.c",
    CORE / "fakes/c31_fake_dma.c",
    CORE / "fakes/c31_fake_nfc.c",
]
PROGRAMS = {
    "unit": (
        CORE / "tests/test_c31.c",
        "C3.1 unit tests: PASS (17 cases)",
    ),
    "model": (
        CORE / "tests/model_c31.c",
        "C3.1 bounded model: PASS "
        "(1944 traces, hash=a8b8770a7a6ce007)",
    ),
    "fuzz": (
        CORE / "tests/fuzz_c31.c",
        "C3.1 deterministic fuzz: PASS "
        "(seed=9b6d3e7a4c2158f1 iterations=5000 "
        "hash=2842aa9bd174fa6f)",
    ),
}
TARGETS = {
    "aarch64": {
        "compiler": "aarch64-linux-gnu-gcc",
        "runner": "qemu-aarch64",
        "sysroot": "/usr/aarch64-linux-gnu",
    },
    "riscv64": {
        "compiler": "riscv64-linux-gnu-gcc",
        "runner": "qemu-riscv64",
        "sysroot": "/usr/riscv64-linux-gnu",
    },
    "s390x-big-endian": {
        "compiler": "s390x-linux-gnu-gcc",
        "runner": "qemu-s390x",
        "sysroot": "/usr/s390x-linux-gnu",
    },
}
CFLAGS = [
    "-std=c11",
    "-O2",
    "-g",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wpedantic",
]


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise RuntimeError(f"required tool is missing: {name}")
    return path


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=120,
    )


def main() -> int:
    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="fwlab-c31-cross-") as temporary:
        output_root = Path(temporary)
        for target, config in TARGETS.items():
            try:
                compiler = require_tool(config["compiler"])
                runner = require_tool(config["runner"])
            except RuntimeError as error:
                failures.append(f"{target}: {error}")
                continue
            for program, (main_source, expected) in PROGRAMS.items():
                output = output_root / f"{target}-{program}"
                sources = [CORE / "c31.c", CORE / "c31_codec.c"]
                if program != "fuzz":
                    sources = SOURCES
                build = run(
                    [
                        compiler,
                        f"-I{INCLUDE}",
                        f"-I{CORE / 'fakes'}",
                        *CFLAGS,
                        "-o",
                        str(output),
                        *(str(path) for path in sources),
                        str(main_source),
                    ]
                )
                if build.returncode:
                    failures.append(
                        f"{target}/{program}: build failed\n{build.stdout}"
                    )
                    continue
                execute = run(
                    [runner, "-L", config["sysroot"], str(output)]
                )
                observed = execute.stdout.strip()
                if execute.returncode or observed != expected:
                    failures.append(
                        f"{target}/{program}: expected {expected!r}, "
                        f"got rc={execute.returncode} {observed!r}"
                    )
                    continue
                print(f"C3.1 cross {target}/{program}: PASS")
    if failures:
        print("C3.1 cross gate failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("C3.1 cross gate: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
