#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Compare GCC and Clang C3.5 canonical lane bytes without hashing first."""

from __future__ import annotations

import hashlib
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "frontends" / "headless-c35"
MODES = {"life": "smbp", "semantic": "mbp", "container": "bp"}


def invoke(build: str, lane: str, mode: str) -> bytes:
    binary = COMPONENT / build / f"c35_lane_{lane}"
    return subprocess.check_output([str(binary), mode], cwd=ROOT)


def main() -> int:
    reference: dict[str, bytes] = {}
    try:
        for mode, lanes in MODES.items():
            for lane in lanes:
                gcc = invoke("build", lane, mode)
                clang = invoke("build/clang", lane, mode)
                if gcc != clang:
                    raise RuntimeError(f"GCC/Clang differ: {lane}/{mode}")
                if mode not in reference:
                    reference[mode] = gcc
    except (OSError, subprocess.CalledProcessError, RuntimeError) as error:
        print(f"C3.5 determinism: FAIL: {error}", file=sys.stderr)
        return 1
    hashes = " ".join(
        f"{mode}={hashlib.sha256(data).hexdigest()}"
        for mode, data in reference.items())
    print(f"C3.5 determinism: PASS (GCC == Clang exact bytes) {hashes}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
