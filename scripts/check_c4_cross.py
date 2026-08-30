#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Compare complete C4.1 output on native, AArch64, RISC-V and s390x."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / "include"
CORE = ROOT / "core/c4-nvme"
HIF = ROOT / "frontends/headless-c4"
CORE_SOURCES = [
    CORE / "c41_codec.c", CORE / "c41_profile.c", CORE / "c41_action.c",
]
HIF_SOURCES = [HIF / "c41_wire.c"]
PROGRAMS = {
    "core-fake": ([*CORE_SOURCES], CORE / "fakes/c41_fake_main.c"),
    "core-unit": ([*CORE_SOURCES], CORE / "tests/test_c41_portable.c"),
    "core-model": ([*CORE_SOURCES], CORE / "tests/model_c41.c"),
    "hif-fake": ([*CORE_SOURCES, *HIF_SOURCES], HIF / "fakes/c41_fake_main.c"),
    "hif-unit": ([*CORE_SOURCES, *HIF_SOURCES], HIF / "tests/test_c41_wire.c"),
    "hif-fuzz": ([*CORE_SOURCES, *HIF_SOURCES], HIF / "tests/fuzz_c41.c"),
}
TARGETS = {
    "aarch64": ("aarch64-linux-gnu-gcc", "qemu-aarch64", "/usr/aarch64-linux-gnu", 1),
    "riscv64": ("riscv64-linux-gnu-gcc", "qemu-riscv64", "/usr/riscv64-linux-gnu", 1),
    "s390x-big-endian": ("s390x-linux-gnu-gcc", "qemu-s390x", "/usr/s390x-linux-gnu", 2),
}
CFLAGS = ["-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
          "-Wpedantic", "-fno-common"]


def require(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise RuntimeError(f"required tool is missing: {name}")
    return path


def build(compiler: str, output: Path, sources: list[Path], main: Path) -> None:
    subprocess.check_call([
        compiler, f"-I{INCLUDE}", f"-I{HIF}", *CFLAGS, "-o", str(output),
        *(str(path) for path in sources), str(main),
    ], cwd=ROOT)


def main() -> int:
    failures: list[str] = []
    try:
        native = require("cc")
        with tempfile.TemporaryDirectory(prefix="c41-cross-") as directory:
            output = Path(directory)
            expected: dict[str, bytes] = {}
            for name, (sources, source) in PROGRAMS.items():
                binary = output / f"native-{name}"
                build(native, binary, sources, source)
                command = [str(binary), "bytes"] if name.endswith("fake") else [str(binary)]
                expected[name] = subprocess.check_output(command, cwd=ROOT)
            for target, (compiler_name, runner_name, sysroot, elf_data) in TARGETS.items():
                compiler = require(compiler_name)
                runner = require(runner_name)
                for name, (sources, source) in PROGRAMS.items():
                    binary = output / f"{target}-{name}"
                    build(compiler, binary, sources, source)
                    header = binary.read_bytes()[:16]
                    if len(header) != 16 or header[:4] != b"\x7fELF" or \
                            header[4] != 2 or header[5] != elf_data:
                        failures.append(f"{target}/{name}: ELF class/byte order differs")
                        continue
                    command = [runner, "-L", sysroot, str(binary)]
                    if name.endswith("fake"):
                        command.append("bytes")
                    actual = subprocess.check_output(
                        command, cwd=ROOT,
                        timeout=180,
                    )
                    if actual != expected[name]:
                        failures.append(f"{target}/{name}: stdout differs from native")
                    else:
                        print(f"C4.1 cross {target}/{name}: PASS")
    except (OSError, RuntimeError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired) as error:
        failures.append(str(error))
    if failures:
        print("C4.1 cross gate failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("C4.1 cross deterministic bytes: PASS "
          "(native/aarch64/riscv64/s390x; s390x big-endian proven)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
