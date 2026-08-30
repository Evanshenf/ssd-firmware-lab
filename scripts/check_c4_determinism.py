#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Build C4.1 fixtures with GCC/Clang and compare complete output bytes."""

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
CFLAGS = ["-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
          "-Wpedantic", "-fno-common"]


def build(compiler: str, output: Path, sources: list[Path], main: Path) -> None:
    subprocess.check_call([
        compiler, f"-I{INCLUDE}", f"-I{HIF}", *CFLAGS, "-o", str(output),
        *(str(path) for path in sources), str(main),
    ], cwd=ROOT)


def main() -> int:
    for compiler in ("gcc", "clang"):
        if shutil.which(compiler) is None:
            print(f"C4.1 determinism: FAIL: {compiler} missing", file=sys.stderr)
            return 1
    try:
        with tempfile.TemporaryDirectory(prefix="c41-determinism-") as directory:
            root = Path(directory)
            for name, (sources, source) in PROGRAMS.items():
                outputs: dict[str, bytes] = {}
                for compiler in ("gcc", "clang"):
                    binary = root / f"{compiler}-{name}"
                    build(compiler, binary, sources, source)
                    command = [str(binary), "bytes"] if name.endswith("fake") else [str(binary)]
                    outputs[compiler] = subprocess.check_output(command, cwd=ROOT)
                if outputs["gcc"] != outputs["clang"]:
                    raise RuntimeError(f"GCC/Clang bytes differ: {name}")
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"C4.1 determinism: FAIL: {error}", file=sys.stderr)
        return 1
    print("C4.1 determinism: PASS (GCC == Clang complete bytes; six programs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
