#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Check the C3.2 public boundary and recovery type isolation."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
INTERNAL = ROOT / "core/c32/c32_internal.h"
RECOVERY = ROOT / "core/c32/c32_recovery.c"
PUBLIC_HEADERS = [
    ROOT / "include/fwlab/contracts/persistence_facts.h",
    ROOT / "include/fwlab/portable/persistence_policy.h",
]
FORBIDDEN_PROJECTION_TERMS = {
    "c32_model_state",
    "inflight",
    "ledger",
    "firmware_ram",
    "host_cache",
    "c32_plp_envelope",
    "mutation",
    "fence",
    "gc",
}
FORBIDDEN_PUBLIC_TERMS = {
    "C32_",
    "c32_",
    "erase_generation",
    "physical_op",
    "persistent_record",
    "checkpoint_hash",
    "host_cache",
    "firmware_ram",
    "broken_variant",
}


def block(text: str, start: str, end: str) -> str:
    begin = text.find(start)
    if begin < 0:
        raise ValueError(f"missing block start: {start}")
    finish = text.find(end, begin)
    if finish < 0:
        raise ValueError(f"missing block end after: {start}")
    return text[begin:finish + len(end)]


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

    internal = INTERNAL.read_text(encoding="utf-8")
    recovery = RECOVERY.read_text(encoding="utf-8")
    try:
        projection = block(
            internal, "struct c32_logical_image {", "};"
        )
        prototype = block(
            internal, "int c32_logical_recover(", ");"
        )
        implementation = recovery[recovery.index("int c32_logical_recover("):]
    except ValueError as error:
        failures.append(str(error))
        projection = ""
        prototype = ""
        implementation = ""

    for term in sorted(FORBIDDEN_PROJECTION_TERMS):
        if re.search(rf"\b{re.escape(term)}\b", projection):
            failures.append(f"logical-image projection exposes {term}")
        if re.search(rf"\b{re.escape(term)}\b", prototype):
            failures.append(f"logical-recovery signature exposes {term}")
        if re.search(rf"\b{re.escape(term)}\b", implementation):
            failures.append(f"logical-recovery implementation reads {term}")

    expected_signature = re.compile(
        r"int\s+c32_logical_recover\s*\(\s*"
        r"const\s+struct\s+c32_logical_image\s*\*\s*image\s*,\s*"
        r"enum\s+c32_broken_variant\s+broken\s*,\s*"
        r"struct\s+c32_recovery_result\s*\*\s*result\s*\)\s*;",
        re.DOTALL,
    )
    if not expected_signature.fullmatch(prototype.strip()):
        failures.append("logical-recovery signature is not the frozen image-only form")

    for header in PUBLIC_HEADERS:
        text = header.read_text(encoding="utf-8")
        for term in sorted(FORBIDDEN_PUBLIC_TERMS):
            if term in text:
                failures.append(f"public header exposes private term {term}: {header.name}")

    probe = """\
/* SPDX-License-Identifier: BSD-3-Clause */
#include <stddef.h>
#include "fwlab/portable/persistence_policy.h"

int public_consumer_probe(const struct fwlab_persist_profile *profile)
{
    return fwlab_persist_profile_validate(profile) == FWLAB_PERSIST_OK;
}
"""
    with tempfile.TemporaryDirectory(prefix="fwlab-c32-architecture-") as temporary:
        temporary_root = Path(temporary)
        source = temporary_root / "public_probe.c"
        public_object = temporary_root / "public_probe.o"
        recovery_object = temporary_root / "recovery.o"
        source.write_text(probe, encoding="utf-8")
        flags = [
            arguments.cc,
            f"-I{ROOT / 'include'}",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wpedantic",
            "-c",
        ]
        public_build = run([*flags, str(source), "-o", str(public_object)])
        if public_build.returncode:
            failures.append(f"public consumer probe failed:\n{public_build.stdout}")
        recovery_build = run(
            [*flags, str(RECOVERY), "-o", str(recovery_object)]
        )
        if recovery_build.returncode:
            failures.append(f"recovery isolation compile failed:\n{recovery_build.stdout}")
        elif recovery_object.exists():
            symbols = run(["nm", "-u", str(recovery_object)])
            forbidden_symbols = re.compile(
                r"\b(?:malloc|calloc|realloc|free|pthread_create|"
                r"clock_gettime|open|read|write|mmap)\b"
            )
            if symbols.returncode or forbidden_symbols.search(symbols.stdout):
                failures.append(
                    "logical recovery has a forbidden runtime dependency:\n"
                    f"{symbols.stdout}"
                )

    if failures:
        print("C3.2 architecture isolation failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("C3.2 architecture/type-isolation gate: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
