#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Write or byte-check the deterministic C42A-P1 obligation lock."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

from check_c42_claim_models import (
    DEFAULT_MODEL_DIR,
    LOCK_NAME,
    ModelError,
    build_model,
    render_lock,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-dir", type=Path, default=DEFAULT_MODEL_DIR)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    try:
        model = build_model(arguments.model_dir)
        rendered = render_lock(model)
        path = arguments.model_dir / LOCK_NAME
        if arguments.write:
            path.write_text(rendered, encoding="utf-8")
        else:
            if not path.is_file() or path.read_text(encoding="utf-8") != rendered:
                raise ModelError(f"{LOCK_NAME} differs from deterministic generation")
    except (ModelError, OSError, UnicodeError) as error:
        print(f"C4.2 obligation generator: FAIL: {error}", file=sys.stderr)
        return 1
    counts = model["counts"]
    print(
        "C4.2 obligation generator: PASS "
        f"obligations={counts['d_fault']} lanes={counts['d_exec']} "
        f"input={model['input_digest']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
