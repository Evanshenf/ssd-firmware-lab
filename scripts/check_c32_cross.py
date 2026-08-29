#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Compare all C3.2 stdout on native, AArch64, RISC-V and s390x."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "core/c32"
INCLUDE = ROOT / "include"
COMMON = [
    CORE / "c32_policy.c",
    CORE / "c32_canonical.c",
    CORE / "c32_recovery.c",
    CORE / "c32_invariants.c",
]
PROGRAMS = {
    "policy": (COMMON, CORE / "tests/test_policy.c"),
    "recovery": (COMMON[1:], CORE / "tests/test_recovery.c"),
    "invariants": (COMMON[1:], CORE / "tests/test_invariants.c"),
    "model": (COMMON + [CORE / "c32_model.c"], CORE / "tests/model_c32.c"),
    "broken": (COMMON + [CORE / "c32_model.c"], CORE / "tests/broken_c32.c"),
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
        timeout=180,
    )


def build(
    compiler: str, output: Path, sources: list[Path], main_source: Path
) -> subprocess.CompletedProcess[str]:
    return run(
        [
            compiler,
            f"-I{INCLUDE}",
            *CFLAGS,
            "-o",
            str(output),
            *(str(path) for path in sources),
            str(main_source),
        ]
    )


def main() -> int:
    failures: list[str] = []
    try:
        native_compiler = require_tool("cc")
    except RuntimeError as error:
        print(f"C3.2 cross gate failed: {error}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="fwlab-c32-cross-") as temporary:
        output_root = Path(temporary)
        expected: dict[str, str] = {}
        for program, (sources, main_source) in PROGRAMS.items():
            output = output_root / f"native-{program}"
            result = build(native_compiler, output, sources, main_source)
            if result.returncode:
                failures.append(f"native/{program}: build failed\n{result.stdout}")
                continue
            execute = run([str(output)])
            if execute.returncode:
                failures.append(
                    f"native/{program}: execution failed rc={execute.returncode}\n"
                    f"{execute.stdout}"
                )
                continue
            expected[program] = execute.stdout

        for target, config in TARGETS.items():
            try:
                compiler = require_tool(config["compiler"])
                runner = require_tool(config["runner"])
            except RuntimeError as error:
                failures.append(f"{target}: {error}")
                continue
            for program, (sources, main_source) in PROGRAMS.items():
                if program not in expected:
                    continue
                output = output_root / f"{target}-{program}"
                result = build(compiler, output, sources, main_source)
                if result.returncode:
                    failures.append(
                        f"{target}/{program}: build failed\n{result.stdout}"
                    )
                    continue
                execute = run(
                    [runner, "-L", config["sysroot"], str(output)]
                )
                if execute.returncode or execute.stdout != expected[program]:
                    failures.append(
                        f"{target}/{program}: stdout differs from native "
                        f"(rc={execute.returncode})"
                    )
                    continue
                print(f"C3.2 cross {target}/{program}: PASS")

    if failures:
        print("C3.2 cross gate failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("C3.2 cross deterministic stdout: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
