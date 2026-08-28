#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Require explicit SPDX metadata on every project-authored file."""

from __future__ import annotations

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
EXEMPT = {Path("LICENSE")}
EXEMPT_PREFIXES = (Path("LICENSES"), Path(".git"))
IGNORED_PARTS = {".git", ".cache", ".venv", "__pycache__", "build", "out"}
SPDX_LICENSE_TAG = "SPDX-License-" + "Identifier:"


def is_exempt(relative: Path) -> bool:
    return relative in EXEMPT or any(
        relative == prefix or prefix in relative.parents for prefix in EXEMPT_PREFIXES
    )


def main() -> int:
    failures: list[str] = []
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file():
            continue
        if IGNORED_PARTS.intersection(path.parts):
            continue
        relative = path.relative_to(ROOT)
        if is_exempt(relative):
            continue
        try:
            head = "\n".join(path.read_text(encoding="utf-8").splitlines()[:16])
        except UnicodeDecodeError:
            failures.append(f"binary project file is not allowed: {relative}")
            continue
        if "SPDX-FileCopyrightText:" not in head:
            failures.append(f"missing SPDX copyright: {relative}")
        if SPDX_LICENSE_TAG not in head:
            failures.append(f"missing SPDX license: {relative}")

    if failures:
        print("SPDX policy failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("SPDX policy: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
