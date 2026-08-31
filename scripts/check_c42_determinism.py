#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Build all C4.2 programs with GCC/Clang and compare complete output."""

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
    HIF / "fakes/c42_event.c", HIF / "fakes/c42_memory.c",
    HIF / "fakes/c42_command.c",
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
    "c42_remediation_unit": (
        [*COMMON], HIF / "tests/test_c42_remediation.c"
    ),
    "c42_provider_matrix": (
        [*COMMON], HIF / "tests/test_c42_provider_matrix.c"
    ),
    "c42_phase_cuts": ([*COMMON], HIF / "tests/test_c42_phase_cuts.c"),
    "c42_dut_replay": (
        [*COMMON, HIF / "tests/c42_reference.c",
         HIF / "tests/c42_dut_bfs.c"],
        HIF / "tests/test_c42_dut_replay.c"
    ),
    "c42_public_abi": ([], HIF / "tests/test_c42_public_abi.c"),
    "c42_model": ([*MODEL], HIF / "tests/model_c42.c"),
    "c42_broken": ([*MODEL], HIF / "tests/broken_c42.c"),
    "c42_fuzz": ([*COMMON], HIF / "tests/fuzz_c42.c"),
    "c42_fake_link": ([*COMMON], HIF / "fakes/c42_fake_main.c"),
}
CFLAGS = [
    "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
    "-Wpedantic", "-fno-common",
]


def build(compiler: str, output: Path, sources: list[Path], main: Path) -> None:
    subprocess.check_call([
        compiler, f"-I{INCLUDE}", f"-I{HIF}", *CFLAGS, "-o", str(output),
        *(str(path) for path in sources), str(main),
    ], cwd=ROOT)


def main() -> int:
    for compiler in ("gcc", "clang"):
        if shutil.which(compiler) is None:
            print(f"C4.2 determinism: FAIL: {compiler} missing", file=sys.stderr)
            return 1
    try:
        with tempfile.TemporaryDirectory(prefix="c42-determinism-") as directory:
            output = Path(directory)
            for name, (sources, source) in PROGRAMS.items():
                values: dict[str, bytes] = {}
                for compiler in ("gcc", "clang"):
                    binary = output / f"{compiler}-{name}"
                    build(compiler, binary, sources, source)
                    values[compiler] = subprocess.check_output(
                        [str(binary)], cwd=ROOT, timeout=300
                    )
                if values["gcc"] != values["clang"]:
                    raise RuntimeError(f"GCC/Clang output differs: {name}")
    except (OSError, RuntimeError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired) as error:
        print(f"C4.2 determinism: FAIL: {error}", file=sys.stderr)
        return 1
    print("C4.2 determinism: PASS (GCC == Clang complete output; "
          f"{len(PROGRAMS)} programs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
