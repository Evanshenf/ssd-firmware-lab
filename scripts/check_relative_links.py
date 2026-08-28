#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Check that local Markdown links resolve inside the repository."""

from __future__ import annotations

from pathlib import Path
import re
import sys
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]
LINK_RE = re.compile(r"(?<!!)\[[^\]]*\]\((?P<target><[^>]+>|[^)\s]+)(?:\s+['\"][^'\"]*['\"])?\)")
SCHEMES = ("http://", "https://", "mailto:", "data:")


def iter_targets(text: str):
    in_fence = False
    for line_number, line in enumerate(text.splitlines(), start=1):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        for match in LINK_RE.finditer(line):
            yield line_number, match.group("target").strip("<>")


def main() -> int:
    failures: list[str] = []
    for document in sorted(ROOT.rglob("*.md")):
        if ".git" in document.parts:
            continue
        for line_number, target in iter_targets(document.read_text(encoding="utf-8")):
            if target.startswith(SCHEMES) or target.startswith("#"):
                continue
            relative_target = unquote(target.split("#", 1)[0])
            if not relative_target:
                continue
            resolved = (document.parent / relative_target).resolve()
            try:
                resolved.relative_to(ROOT)
            except ValueError:
                failures.append(f"{document.relative_to(ROOT)}:{line_number}: escapes repository: {target}")
                continue
            if not resolved.exists():
                failures.append(f"{document.relative_to(ROOT)}:{line_number}: missing: {target}")

    if failures:
        print("Relative-link policy failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("Relative links: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
