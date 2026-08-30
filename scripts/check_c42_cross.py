#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Compare all eight C4.2 program outputs across four architectures."""

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
COMMON = [
    CORE / "c41_codec.c", CORE / "c41_profile.c", CORE / "c41_action.c",
    HIF / "c41_wire.c",
    HIF / "hif/c42_identity.c", HIF / "hif/c42_queue.c",
    HIF / "hif/c42_publication.c", HIF / "hif/c42_runtime.c",
    HIF / "fakes/c42_memory.c", HIF / "fakes/c42_command.c",
    HIF / "tests/c42_support.c",
]
MODEL = [HIF / "tests/c42_model.c"]
PROGRAMS = {
    "c42_queue_unit": ([*COMMON], HIF / "tests/test_c42_queue.c"),
    "c42_publication_unit": (
        [*COMMON], HIF / "tests/test_c42_publication.c"
    ),
    "c42_identity_unit": ([*COMMON], HIF / "tests/test_c42_identity.c"),
    "c42_reset_delete_unit": (
        [*COMMON], HIF / "tests/test_c42_reset_delete.c"
    ),
    "c42_model": ([*MODEL], HIF / "tests/model_c42.c"),
    "c42_broken": ([*MODEL], HIF / "tests/broken_c42.c"),
    "c42_fuzz": ([*COMMON], HIF / "tests/fuzz_c42.c"),
    "c42_fake_link": ([*COMMON], HIF / "fakes/c42_fake_main.c"),
}
TARGETS = {
    "aarch64": (
        "aarch64-linux-gnu-gcc", "qemu-aarch64", "/usr/aarch64-linux-gnu", 1
    ),
    "riscv64": (
        "riscv64-linux-gnu-gcc", "qemu-riscv64", "/usr/riscv64-linux-gnu", 1
    ),
    "s390x-big-endian": (
        "s390x-linux-gnu-gcc", "qemu-s390x", "/usr/s390x-linux-gnu", 2
    ),
}
CFLAGS = [
    "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
    "-Wpedantic", "-fno-common",
]


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
        with tempfile.TemporaryDirectory(prefix="c42-cross-") as directory:
            output = Path(directory)
            expected: dict[str, bytes] = {}
            for name, (sources, source) in PROGRAMS.items():
                binary = output / f"native-{name}"
                build(native, binary, sources, source)
                expected[name] = subprocess.check_output(
                    [str(binary)], cwd=ROOT, timeout=300
                )
            for target, (compiler_name, runner_name, sysroot, elf_data) in \
                    TARGETS.items():
                compiler = require(compiler_name)
                runner = require(runner_name)
                for name, (sources, source) in PROGRAMS.items():
                    binary = output / f"{target}-{name}"
                    build(compiler, binary, sources, source)
                    header = binary.read_bytes()[:16]
                    if len(header) != 16 or header[:4] != b"\x7fELF" or \
                            header[4] != 2 or header[5] != elf_data:
                        failures.append(f"{target}/{name}: ELF identity differs")
                        continue
                    actual = subprocess.check_output(
                        [runner, "-L", sysroot, str(binary)],
                        cwd=ROOT,
                        timeout=300,
                    )
                    if actual != expected[name]:
                        failures.append(f"{target}/{name}: output differs")
                    else:
                        print(f"C4.2 cross {target}/{name}: PASS")
    except (OSError, RuntimeError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired) as error:
        failures.append(str(error))
    if failures:
        print("C4.2 cross gate failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("C4.2 cross deterministic output: PASS "
          "(native/aarch64/riscv64/s390x; s390x big-endian proven; 8 programs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
