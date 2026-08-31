#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Enforce the C4.1 address-free policy and raw-memory-HIF split."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "core/c4-nvme"
HIF = ROOT / "frontends/headless-c4"
INCLUDE = ROOT / "include"
PUBLIC = [
    INCLUDE / "fwlab/portable/nvme_types.h",
    INCLUDE / "fwlab/portable/nvme_codec.h",
    INCLUDE / "fwlab/contracts/hif_action.h",
]
CORE_SOURCES = [
    CORE / "c41_codec.c", CORE / "c41_profile.c", CORE / "c41_action.c",
]
HIF_SOURCES = [HIF / "c41_wire.c"]
RAW_FIELD = re.compile(
    r"\b(?:command_id|submission_queue_id|submission_queue_head|"
    r"metadata_pointer|data_pointer1|data_pointer2|prp1|prp2|mptr|"
    r"qid|cid|sqid|sqhd|gpa|hpa|iova|pfn)\b",
    re.IGNORECASE,
)
FORBIDDEN_PORTABLE_INCLUDE = re.compile(
    r'^\s*#\s*include\s*[<"](?:linux/|sys/|asm/|qemu|hw/|sysemu/|vfio|'
    r"c41_wire|c35_|c34_|c31_)",
    re.MULTILINE | re.IGNORECASE,
)
FORBIDDEN_PORTABLE_RUNTIME = re.compile(
    r"\b(?:malloc|calloc|realloc|free|pthread_create|clock_gettime|"
    r"nanosleep|open|close|pread|pwrite|ioctl|mmap)\b"
)


def run(command: list[str], timeout: int = 180) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
    )


def symbols(paths: list[Path], undefined: bool) -> set[str]:
    flag = "-u" if undefined else "--defined-only"
    result = run(["nm", "-A", flag, *(str(path) for path in paths)])
    if result.returncode:
        raise RuntimeError(f"nm failed:\n{result.stdout}")
    found: set[str] = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if fields:
            found.add(fields[-1].split("@", 1)[0])
    return found


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--no-c35-child", action="store_true")
    arguments = parser.parse_args()
    failures: list[str] = []

    for path in [*PUBLIC, *CORE_SOURCES]:
        text = path.read_text(encoding="utf-8")
        if FORBIDDEN_PORTABLE_INCLUDE.search(text):
            failures.append(f"forbidden include in portable source: {path.relative_to(ROOT)}")
        match = RAW_FIELD.search(text)
        if match:
            failures.append(
                f"raw HIF field {match.group(0)!r} entered portable source: "
                f"{path.relative_to(ROOT)}"
            )
    if RAW_FIELD.search("struct mutation { unsigned cid; };") is None:
        failures.append("raw-field architecture mutation was not detected")

    codec_text = (CORE / "c41_codec.c").read_text(encoding="utf-8")
    for token in ("memcpy", "__attribute__((packed", "#pragma pack"):
        if token in codec_text:
            failures.append(f"native serialization token in canonical codec: {token}")
    core_make = (CORE / "Makefile").read_text(encoding="utf-8")
    for token in ("headless-c4", "c41_wire", "c31", "c35"):
        if token in core_make:
            failures.append(f"portable component Makefile depends on {token}")
    hif_text = "\n".join(path.read_text(encoding="utf-8") for path in HIF_SOURCES)
    for token in ("command_id", "data_pointer1", "metadata_pointer"):
        if token not in hif_text:
            failures.append(f"raw HIF does not retain private field: {token}")

    try:
        with tempfile.TemporaryDirectory(prefix="fwlab-c41-architecture-") as directory:
            output = Path(directory)
            objects: list[Path] = []
            for index, source in enumerate(CORE_SOURCES):
                obj = output / f"core-{index}.o"
                built = run([
                    arguments.cc, f"-I{INCLUDE}", "-std=c11", "-O2",
                    "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                    "-fno-common", "-c", str(source), "-o", str(obj),
                ])
                if built.returncode:
                    failures.append(f"portable compile failed for {source.name}:\n{built.stdout}")
                else:
                    objects.append(obj)
            if objects:
                defined = symbols(objects, False)
                undefined = symbols(objects, True) - defined
                allowed = {"__stack_chk_fail"}
                if undefined - allowed:
                    failures.append(
                        f"portable undefined symbols: {sorted(undefined - allowed)}"
                    )
                writable = run(["nm", "-A", "--defined-only", *(str(path) for path in objects)])
                for line in writable.stdout.splitlines():
                    if re.search(r"\s[BCDGSV]\s+\S+$", line):
                        failures.append(f"writable portable global: {line}")
                joined = " ".join(defined | undefined)
                if FORBIDDEN_PORTABLE_RUNTIME.search(joined):
                    failures.append("portable object contains forbidden runtime symbol")
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        failures.append(f"architecture object audit failed: {error}")

    if not arguments.no_c35_child:
        try:
            with tempfile.TemporaryDirectory(
                    prefix="fwlab-c41-c35-architecture-") as directory:
                isolated_root = Path(directory) / "repo"
                shutil.copytree(
                    ROOT, isolated_root,
                    ignore=shutil.ignore_patterns(
                        ".git", "build", "__pycache__", "*.pyc", "*.o"
                    ),
                )
                c35 = run([
                    "make", "-C", str(isolated_root / "frontends/headless-c35"),
                    "CC=cc", "AR=ar", "BUILD_DIR=build",
                    "CFLAGS=-std=c11 -O2 -g -Wall -Wextra -Werror "
                    "-Wpedantic -fno-common",
                    "LDFLAGS=", "check-architecture",
                ], timeout=300)
                if c35.returncode:
                    failures.append(
                        f"C3.5 coexistence regression failed:\n{c35.stdout}"
                    )
                elif "archive=b1f95f2787fe9a3d585f467d4ba72e6de32938c51273d4ad7f411dc1e644cf6f" \
                        not in c35.stdout:
                    failures.append(
                        "C3.5 coexistence did not prove the frozen archive hash"
                    )
        except (OSError, subprocess.TimeoutExpired) as error:
            failures.append(f"C3.5 coexistence isolation failed: {error}")

    if failures:
        print("C4.1 architecture isolation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    coexistence = "C3 child delegated" if arguments.no_c35_child \
        else "C3 archive/lane hashes unchanged"
    print("C4.1 architecture isolation: PASS (address-free portable core; "
          f"raw HIF private; {coexistence})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
