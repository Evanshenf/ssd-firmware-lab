#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Enforce C3.4 coordinator, firmware recovery and physical-file boundaries."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "core/c34"
FILE = ROOT / "media/c34-file"
INCLUDE = ROOT / "include"
CORE_SOURCES = [
    CORE / name for name in (
        "c34_codec.c", "c34_recovery.c", "c34_mapping.c",
        "c34_journal.c", "c34_checkpoint.c", "c34_nfc_graph.c",
        "c34_coordinator.c", "c34_drive.c", "c34_provider.c",
    )
]
FILE_ENGINE = [
    FILE / name for name in (
        "c34_file_codec.c", "c34_file_recovery.c",
        "c34_file_engine.c", "c34_file_media.c",
    )
]
POSIX = FILE / "c34_file_posix.c"
FORBIDDEN_CORE_INCLUDE = re.compile(
    r'^\s*#\s*include\s*[<"](?:asm/|linux/|sys/|unistd\.h|fcntl\.h|'
    r'pthread\.h|qemu|hw/|sysemu/|vfio)',
    re.MULTILINE | re.IGNORECASE,
)
FORBIDDEN_CORE_TERM = re.compile(
    r"\b(?:NVMe|IOVA|eventfd|MSI-X|BAR|PCI|QEMU|VFIO|GPA|HPA|PFN|"
    r"pathname|fdatasync|fsync|pwrite|pread|ioctl|discard|mmap)\b",
    re.IGNORECASE,
)
FORBIDDEN_PRIVATE_INCLUDE = re.compile(
    r'#\s*include\s*[<"](?:.*c31_internal|.*c32_internal|'
    r'.*nfc_internal|.*nfc/fakes|.*core/c32)',
    re.IGNORECASE,
)
FORBIDDEN_FILE_LOGICAL = re.compile(
    r"\b(?:L2P|P2L|logical_state_id|logical_atom|authority_record_id|"
    r"tombstone|relocation_record|decoded_map|host_cache)\b",
    re.IGNORECASE,
)
FORBIDDEN_RUNTIME = re.compile(
    r"\b(?:malloc|calloc|realloc|free|pthread_create|pthread_join|"
    r"clock_gettime|nanosleep|sleep|rand|random|srand|open|mmap|ioctl)\b"
)
FORBIDDEN_ENGINE_POSIX = re.compile(
    r"\b(?:fdatasync|fsync|pread|pwrite|fstat|ftruncate|openat|unlinkat)\b"
)


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
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default="cc")
    arguments = parser.parse_args()
    failures: list[str] = []

    for path in [*CORE_SOURCES, CORE / "c34.h", CORE / "c34_internal.h"]:
        text = path.read_text(encoding="utf-8")
        relative = path.relative_to(ROOT)
        if FORBIDDEN_CORE_INCLUDE.search(text):
            failures.append(f"platform include in portable coordinator: {relative}")
        if FORBIDDEN_CORE_TERM.search(text):
            failures.append(f"transport/file term in portable coordinator: {relative}")
        if FORBIDDEN_PRIVATE_INCLUDE.search(text):
            failures.append(f"frozen private implementation included: {relative}")

    for path in FILE_ENGINE:
        text = path.read_text(encoding="utf-8")
        relative = path.relative_to(ROOT)
        if FORBIDDEN_FILE_LOGICAL.search(text):
            failures.append(f"physical engine contains decoded mapping: {relative}")
        if FORBIDDEN_ENGINE_POSIX.search(text):
            failures.append(f"pure physical engine contains POSIX I/O: {relative}")

    posix_text = POSIX.read_text(encoding="utf-8")
    for forbidden in ("ioctl", "mmap", "discard", "BLK", "/dev/"):
        if forbidden.lower() in posix_text.lower():
            failures.append(f"POSIX adapter contains forbidden raw API: {forbidden}")
    if "fdatasync" not in posix_text or "ftruncate" not in posix_text:
        failures.append("POSIX adapter lacks explicit barrier/format operations")

    physical_header = (
        INCLUDE / "fwlab/private/c34_physical_txn.h"
    ).read_text(encoding="utf-8")
    if "physical_op_id" not in physical_header or "payload_digest" not in physical_header:
        failures.append("private physical transaction identity is incomplete")

    with tempfile.TemporaryDirectory(prefix="fwlab-c34-architecture-") as temporary:
        output_root = Path(temporary)
        flags = [
            arguments.cc,
            f"-I{INCLUDE}",
            f"-I{CORE}",
            f"-I{FILE}",
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-c",
        ]
        core_objects: list[Path] = []
        engine_objects: list[Path] = []
        for group, sources, objects in (
            ("core", CORE_SOURCES, core_objects),
            ("file", FILE_ENGINE, engine_objects),
        ):
            for index, source in enumerate(sources):
                output = output_root / f"{group}-{index}.o"
                built = run([*flags, str(source), "-o", str(output)])
                if built.returncode:
                    failures.append(
                        f"{group} object compile failed for {source.name}:\n"
                        f"{built.stdout}"
                    )
                else:
                    objects.append(output)
        if core_objects:
            symbols = run(["nm", "-u", *(str(path) for path in core_objects)])
            if symbols.returncode or FORBIDDEN_RUNTIME.search(symbols.stdout):
                failures.append(
                    "portable coordinator has forbidden runtime dependency:\n"
                    f"{symbols.stdout}"
                )
        if engine_objects:
            symbols = run(["nm", "-u", *(str(path) for path in engine_objects)])
            if symbols.returncode or FORBIDDEN_ENGINE_POSIX.search(symbols.stdout):
                failures.append(
                    "pure file engine leaks POSIX dependency:\n"
                    f"{symbols.stdout}"
                )

    if failures:
        print("C3.4 architecture isolation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("C3.4 architecture/dependency isolation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
