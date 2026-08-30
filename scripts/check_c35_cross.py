#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Build and byte-compare C3.5c S/M/B/P projections on three 64-bit ABIs."""

from __future__ import annotations

import hashlib
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "frontends" / "headless-c35"

TARGETS = {
    "aarch64": {
        "cc": "aarch64-linux-gnu-gcc",
        "ar": "aarch64-linux-gnu-ar",
        "qemu": "qemu-aarch64",
        "sysroot": "/usr/aarch64-linux-gnu",
        "elf_data": 1,
    },
    "riscv64": {
        "cc": "riscv64-linux-gnu-gcc",
        "ar": "riscv64-linux-gnu-ar",
        "qemu": "qemu-riscv64",
        "sysroot": "/usr/riscv64-linux-gnu",
        "elf_data": 1,
    },
    "s390x": {
        "cc": "s390x-linux-gnu-gcc",
        "ar": "s390x-linux-gnu-ar",
        "qemu": "qemu-s390x",
        "sysroot": "/usr/s390x-linux-gnu",
        "elf_data": 2,
    },
}

MODES = {
    "life": "smbp",
    "semantic": "mbp",
    "container": "bp",
    "two-atom": "mbp",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def build_native() -> None:
    subprocess.check_call(
        ["make", "-C", str(COMPONENT), "lanes"], cwd=ROOT)


def build_target(name: str, config: dict[str, object]) -> Path:
    for tool in (str(config["cc"]), str(config["ar"]),
                 str(config["qemu"])):
        if shutil.which(tool) is None:
            fail(f"missing cross tool: {tool}")
    build = f"build/cross/{name}"
    subprocess.check_call([
        "make", "-C", str(COMPONENT), "-j2", "lanes",
        f"CC={config['cc']}", f"AR={config['ar']}",
        f"BUILD_DIR={build}",
    ], cwd=ROOT)
    return COMPONENT / build


def invoke(binary: Path, mode: str, runner: list[str] | None = None) -> bytes:
    command = ([] if runner is None else runner) + [str(binary), mode]
    return subprocess.check_output(command, cwd=ROOT)


def main() -> int:
    try:
        build_native()
        native_build = COMPONENT / "build"
        reference: dict[tuple[str, str], bytes] = {}
        for mode, lanes in MODES.items():
            for lane in lanes:
                reference[(mode, lane)] = invoke(
                    native_build / f"c35_lane_{lane}", mode)

        for name, config in TARGETS.items():
            build = build_target(name, config)
            runner = [str(config["qemu"]), "-L", str(config["sysroot"])]
            for lane in "smbp":
                binary = build / f"c35_lane_{lane}"
                header = binary.read_bytes()[:16]
                if len(header) != 16 or header[:4] != b"\x7fELF" or \
                        header[4] != 2 or header[5] != config["elf_data"]:
                    fail(f"unexpected {name}/{lane} ELF class or byte order")
            for mode, lanes in MODES.items():
                for lane in lanes:
                    output = invoke(
                        build / f"c35_lane_{lane}", mode, runner)
                    if output != reference[(mode, lane)]:
                        fail(f"canonical bytes differ: {name}/{lane}/{mode}")

        hashes = {
            "life": hashlib.sha256(reference[("life", "s")]).hexdigest(),
            "semantic_raw": hashlib.sha256(
                reference[("semantic", "m")]).hexdigest(),
            "container": hashlib.sha256(
                reference[("container", "b")]).hexdigest(),
            "two_atom": hashlib.sha256(
                reference[("two-atom", "m")]).hexdigest(),
        }
    except (RuntimeError, subprocess.CalledProcessError, OSError) as error:
        print(f"C3.5c cross ABI: FAIL: {error}", file=sys.stderr)
        return 1
    print("C3.5c cross ABI: PASS (native/aarch64/riscv64/s390x; "
          "s390x big-endian proven) " + " ".join(
              f"{key}={value}" for key, value in hashes.items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
