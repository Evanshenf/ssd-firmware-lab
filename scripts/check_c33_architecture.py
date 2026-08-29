#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Enforce the C3.3 portable NFC, media and adapter dependency boundary."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
NFC = ROOT / "nfc"
PUBLIC = [
    ROOT / "include/fwlab/portable/nfc_types.h",
    ROOT / "include/fwlab/portable/nfc_model.h",
    ROOT / "include/fwlab/portable/nfc_codec.h",
    ROOT / "include/fwlab/contracts/nfc_provider.h",
    ROOT / "include/fwlab/contracts/nand_media.h",
]
MODEL_SOURCES = [
    NFC / "nfc_model.c",
    NFC / "nfc_scheduler.c",
    NFC / "nfc_fault.c",
    NFC / "nfc_media.c",
    NFC / "nfc_codec.c",
]
ADAPTER = NFC / "nfc_adapter.c"
FORBIDDEN_INCLUDES = re.compile(
    r"^\s*#\s*include\s*[<\"](?:asm/|linux/|sys/|pthread\.h|"
    r"libvfio-user|vfio|qemu|hw/|sysemu/)",
    re.MULTILINE | re.IGNORECASE,
)
FORBIDDEN_TERMS = re.compile(
    r"\b(?:LBA|LPN|L2P|P2L|NVMe|IOVA|eventfd|BAR|MSI-X|"
    r"host_cache|decoded_map|fsync|mmap)\b",
    re.IGNORECASE,
)
FORBIDDEN_RUNTIME = re.compile(
    r"\b(?:malloc|calloc|realloc|free|pthread_create|pthread_join|"
    r"clock_gettime|nanosleep|sleep|rand|random|srand|open|fsync|mmap)\b"
)


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=60,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default="cc")
    arguments = parser.parse_args()
    failures: list[str] = []

    for path in [*MODEL_SOURCES, NFC / "nfc_internal.h", *PUBLIC]:
        text = path.read_text(encoding="utf-8")
        if FORBIDDEN_INCLUDES.search(text):
            failures.append(f"forbidden platform include: {path.relative_to(ROOT)}")
        if FORBIDDEN_TERMS.search(text):
            failures.append(f"forbidden policy/transport term: {path.relative_to(ROOT)}")
        if "c31_provider.h" in text:
            failures.append(f"model/public layer depends on C3.1: {path.relative_to(ROOT)}")
        if "persistence_facts" in text or "core/c32" in text:
            failures.append(f"NFC depends on persistence implementation: {path.relative_to(ROOT)}")

    adapter_text = ADAPTER.read_text(encoding="utf-8")
    if "adapters/nfc_c31_adapter.h" not in adapter_text:
        failures.append("C3.1 adapter does not use its isolated private boundary")
    adapter_header = (NFC / "adapters/nfc_c31_adapter.h").read_text(
        encoding="utf-8"
    )
    if adapter_header.count("fwlab/contracts/c31_provider.h") != 1:
        failures.append("C3.1 public provider dependency is not adapter-only")

    probe = """\
/* SPDX-License-Identifier: BSD-3-Clause */
#include "fwlab/contracts/nand_media.h"
#include "fwlab/contracts/nfc_provider.h"
#include "fwlab/portable/nfc_codec.h"
#include "fwlab/portable/nfc_model.h"
#include "fwlab/portable/nfc_types.h"

int public_nfc_consumer(const struct fwlab_nfc_model_config *config)
{
    return fwlab_nfc_model_config_validate(config) == FWLAB_NFC_API_OK;
}
"""
    with tempfile.TemporaryDirectory(prefix="fwlab-c33-architecture-") as temporary:
        temporary_root = Path(temporary)
        probe_source = temporary_root / "probe.c"
        probe_object = temporary_root / "probe.o"
        probe_source.write_text(probe, encoding="utf-8")
        flags = [
            arguments.cc,
            f"-I{ROOT / 'include'}",
            f"-I{NFC}",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wpedantic",
            "-c",
        ]
        built = run([*flags, str(probe_source), "-o", str(probe_object)])
        if built.returncode:
            failures.append(f"public consumer compile failed:\n{built.stdout}")
        objects: list[Path] = []
        for source in [*MODEL_SOURCES, ADAPTER]:
            output = temporary_root / f"{source.stem}.o"
            built = run([*flags, str(source), "-o", str(output)])
            if built.returncode:
                failures.append(
                    f"object compile failed for {source.name}:\n{built.stdout}"
                )
                continue
            objects.append(output)
        if objects:
            symbols = run(["nm", "-u", *(str(path) for path in objects)])
            if symbols.returncode or FORBIDDEN_RUNTIME.search(symbols.stdout):
                failures.append(
                    "portable NFC object has forbidden runtime dependency:\n"
                    f"{symbols.stdout}"
                )

    if failures:
        print("C3.3 architecture isolation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("C3.3 architecture/dependency isolation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
