#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Compare C3.4 portable program stdout on native and three ISAs."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "core/c34"
FILE = ROOT / "media/c34-file"
INCLUDE = ROOT / "include"
CORE_CODEC = [CORE / "c34_codec.c", CORE / "c34_recovery.c"]
CORE_FIXTURES = [
    CORE / "fakes/c34_memory_media.c", CORE / "fakes/c34_buffer.c"
]
COORDINATOR = [
    CORE / name for name in (
        "c34_mapping.c", "c34_journal.c", "c34_checkpoint.c",
        "c34_nfc_graph.c", "c34_coordinator.c", "c34_drive.c",
        "c34_provider.c",
    )
]
FROZEN = [
    ROOT / "core/c31.c", ROOT / "core/c31_codec.c",
    ROOT / "core/c32/c32_policy.c",
    ROOT / "nfc/nfc_model.c", ROOT / "nfc/nfc_scheduler.c",
    ROOT / "nfc/nfc_fault.c", ROOT / "nfc/nfc_media.c",
    ROOT / "nfc/nfc_codec.c",
]
FILE_ENGINE = [
    FILE / name for name in (
        "c34_file_codec.c", "c34_file_recovery.c",
        "c34_file_engine.c", "c34_file_media.c",
    )
]
CORE_SUPPORT = CORE / "tests/c34_test_support.c"
FILE_SUPPORT = FILE / "tests/c34_file_test_support.c"
FILE_ORACLE = FILE / "tests/c34_file_oracle.c"
PROGRAMS = {
    "codec-recovery": (
        CORE_CODEC + CORE_FIXTURES,
        CORE / "tests/test_codec_recovery.c",
    ),
    "flows": (
        CORE_CODEC + COORDINATOR + CORE_FIXTURES + FROZEN + [CORE_SUPPORT],
        CORE / "tests/test_flows.c",
    ),
    "crash-conformance": (
        CORE_CODEC + COORDINATOR + CORE_FIXTURES + FROZEN + [CORE_SUPPORT]
        + FILE_ENGINE + [FILE_SUPPORT],
        CORE / "tests/test_crash_conformance.c",
    ),
    "integration-model": (
        [CORE / "tests/c34_oracle.c"], CORE / "tests/model_c34.c"
    ),
    "integration-broken": (
        [CORE / "tests/c34_oracle.c"], CORE / "tests/broken_c34.c"
    ),
    "file-crash": (
        FILE_ENGINE + [FILE_SUPPORT, FILE_ORACLE],
        FILE / "tests/test_file_crash.c",
    ),
    "file-model": (
        FILE_ENGINE + [FILE_SUPPORT], FILE / "tests/model_c34_file.c"
    ),
    "file-broken": (
        FILE_ENGINE + [FILE_SUPPORT], FILE / "tests/broken_c34_file.c"
    ),
}
TARGETS = {
    "aarch64": ("aarch64-linux-gnu-gcc", "qemu-aarch64", "/usr/aarch64-linux-gnu"),
    "riscv64": ("riscv64-linux-gnu-gcc", "qemu-riscv64", "/usr/riscv64-linux-gnu"),
    "s390x-big-endian": ("s390x-linux-gnu-gcc", "qemu-s390x", "/usr/s390x-linux-gnu"),
}
CFLAGS = ["-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic"]


def tool(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        raise RuntimeError(f"required tool is missing: {name}")
    return path


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command, cwd=ROOT, check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240,
    )


def build(compiler: str, output: Path, sources: list[Path], main: Path):
    return run([
        compiler, f"-I{INCLUDE}", f"-I{CORE}", f"-I{FILE}",
        f"-I{FILE / 'tests'}", *CFLAGS, "-o", str(output),
        *(str(source) for source in sources), str(main),
    ])


def main() -> int:
    failures: list[str] = []
    try:
        native = tool("cc")
    except RuntimeError as error:
        print(f"C3.4 cross gate failed: {error}", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="fwlab-c34-cross-") as temporary:
        output_root = Path(temporary)
        expected: dict[str, str] = {}
        for name, (sources, main_source) in PROGRAMS.items():
            output = output_root / f"native-{name}"
            built = build(native, output, sources, main_source)
            if built.returncode:
                failures.append(f"native/{name}: build failed\n{built.stdout}")
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
                built = build(compiler, output, sources, main_source)
                if built.returncode:
                    failures.append(f"{target}/{name}: build failed\n{built.stdout}")
                    continue
                executed = run([runner, "-L", sysroot, str(output)])
                if executed.returncode or executed.stdout != expected[name]:
                    failures.append(
                        f"{target}/{name}: stdout differs from native "
                        f"(rc={executed.returncode})"
                    )
                    continue
                print(f"C3.4 cross {target}/{name}: PASS")
    if failures:
        print("C3.4 cross gate failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("C3.4 cross deterministic stdout: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
