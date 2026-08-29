#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Compare all C3.3 program stdout on native and three cross architectures."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
NFC = ROOT / "nfc"
INCLUDE = ROOT / "include"
MODEL = [
    NFC / "nfc_model.c",
    NFC / "nfc_scheduler.c",
    NFC / "nfc_fault.c",
    NFC / "nfc_media.c",
    NFC / "nfc_codec.c",
]
FIXTURES = [
    NFC / "fakes/nfc_buffer.c",
    NFC / "fakes/nfc_memory_media.c",
    NFC / "fakes/nfc_scripted.c",
]
SUPPORT = NFC / "tests/c33_test_support.c"
ORACLE = NFC / "tests/c33_oracle.c"
PROGRAMS = {
    "contract": (MODEL + FIXTURES + [SUPPORT], NFC / "tests/test_contract.c"),
    "legality": (MODEL + FIXTURES + [SUPPORT], NFC / "tests/test_legality.c"),
    "codec": ([NFC / "nfc_codec.c"], NFC / "tests/test_codec.c"),
    "replace": (
        MODEL + [NFC / "nfc_adapter.c"] + FIXTURES + [SUPPORT],
        NFC / "tests/test_replaceability.c",
    ),
    "scheduler": (MODEL + FIXTURES + [SUPPORT], NFC / "tests/test_scheduler.c"),
    "ecc-wear": (MODEL + FIXTURES + [SUPPORT], NFC / "tests/test_ecc_wear.c"),
    "reset-power": (
        MODEL + FIXTURES + [SUPPORT], NFC / "tests/test_reset_power.c"
    ),
    "isolation": (MODEL + FIXTURES + [SUPPORT], NFC / "tests/test_isolation.c"),
    "model": ([ORACLE], NFC / "tests/model_c33.c"),
    "broken": ([ORACLE], NFC / "tests/broken_c33.c"),
}
TARGETS = {
    "aarch64": ("aarch64-linux-gnu-gcc", "qemu-aarch64", "/usr/aarch64-linux-gnu"),
    "riscv64": ("riscv64-linux-gnu-gcc", "qemu-riscv64", "/usr/riscv64-linux-gnu"),
    "s390x-big-endian": ("s390x-linux-gnu-gcc", "qemu-s390x", "/usr/s390x-linux-gnu"),
}
CFLAGS = [
    "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic"
]


def tool(name: str) -> str:
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


def build(compiler: str, output: Path, sources: list[Path], main: Path):
    return run([
        compiler, f"-I{INCLUDE}", f"-I{NFC}", *CFLAGS, "-o", str(output),
        *(str(source) for source in sources), str(main),
    ])


def main() -> int:
    failures: list[str] = []
    try:
        native = tool("cc")
    except RuntimeError as error:
        print(f"C3.3 cross gate failed: {error}", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="fwlab-c33-cross-") as temporary:
        output_root = Path(temporary)
        expected: dict[str, str] = {}
        for name, (sources, main_source) in PROGRAMS.items():
            output = output_root / f"native-{name}"
            result = build(native, output, sources, main_source)
            if result.returncode:
                failures.append(f"native/{name}: build failed\n{result.stdout}")
                continue
            executed = run([str(output)])
            if executed.returncode:
                failures.append(
                    f"native/{name}: execution failed rc={executed.returncode}\n"
                    f"{executed.stdout}"
                )
                continue
            expected[name] = executed.stdout
        for target, (compiler_name, runner_name, sysroot) in TARGETS.items():
            try:
                compiler = tool(compiler_name)
                runner = tool(runner_name)
            except RuntimeError as error:
                failures.append(f"{target}: {error}")
                continue
            for name, (sources, main_source) in PROGRAMS.items():
                if name not in expected:
                    continue
                output = output_root / f"{target}-{name}"
                result = build(compiler, output, sources, main_source)
                if result.returncode:
                    failures.append(f"{target}/{name}: build failed\n{result.stdout}")
                    continue
                executed = run([runner, "-L", sysroot, str(output)])
                if executed.returncode or executed.stdout != expected[name]:
                    failures.append(
                        f"{target}/{name}: stdout differs from native "
                        f"(rc={executed.returncode})"
                    )
                    continue
                print(f"C3.3 cross {target}/{name}: PASS")
    if failures:
        print("C3.3 cross gate failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("C3.3 cross deterministic stdout: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
