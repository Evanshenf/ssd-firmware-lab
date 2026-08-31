#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Exercise C4.2 authority-lock, C3.5 closure, and receipt negatives."""

from __future__ import annotations

import argparse
from pathlib import Path
import tempfile

from c42_authority import (
    AuthorityError,
    AuthorityReceipt,
    DEFAULT_AUTHORITY_LOCK,
    DEFAULT_C35_MANIFEST,
    ROOT,
    clean_environment,
    load_c35_manifest,
    materialize_c35_reference,
    verify_authority_lock,
    verify_c35_manifest,
    verify_materialized_c35_reference,
)


FRONTEND = ROOT / "frontends/headless-c4"
MAKEFILE = FRONTEND / "Makefile"
C42_BINARIES = (
    "c42_queue_unit", "c42_publication_unit", "c42_identity_unit",
    "c42_reset_delete_unit", "c42_remediation_unit", "c42_provider_matrix",
    "c42_phase_cuts", "c42_dut_replay", "c42_public_abi", "c42_model",
    "c42_broken", "c42_fuzz", "c42_headless_fake_link",
)


def expect_failure(action, label: str) -> None:
    try:
        action()
    except (AuthorityError, OSError):
        return
    raise AuthorityError(f"authority negative survived: {label}")


def replace_unique(path: Path, before: str, after: str) -> None:
    text = path.read_text(encoding="utf-8")
    if text.count(before) != 1:
        raise AuthorityError(f"authority negative anchor differs: {path}: {before}")
    path.write_text(text.replace(before, after, 1), encoding="utf-8")


def receipt_digest(run_root: Path, argument: str) -> str:
    run_root.mkdir(parents=True, exist_ok=True)
    home = run_root / "home"
    home.mkdir()
    receipt = AuthorityReceipt(ROOT, run_root)
    result = receipt.run(
        "miniature",
        ["printf", "%s", argument],
        cwd=ROOT,
        environment=clean_environment(home),
        timeout=30,
    )
    if result.returncode != 0 or result.stdout != argument:
        raise AuthorityError("miniature authority fixture failed")
    digest, count = receipt.finalize(("miniature",))
    if count != 1:
        raise AuthorityError("miniature receipt count differs")
    return digest


def c42_build_receipt_digest(run_root: Path) -> str:
    home = run_root / "home"
    home.mkdir(parents=True)
    build = run_root / "build"
    environment = clean_environment(home)
    receipt = AuthorityReceipt(ROOT, run_root)
    outputs = tuple(build / name for name in C42_BINARIES) + (
        build / "c42_execution.receipt",
    )
    result = receipt.run(
        "c42-build-miniature",
        [
            "make", "-f", str(MAKEFILE), "-C", str(FRONTEND),
            "CC=cc", f"BUILD_DIR={build}",
            f"FWLAB_FAKE_OUTPUT={build / 'c42_headless_fake_link'}",
            "check-c42-build-tests",
        ],
        cwd=ROOT,
        environment=environment,
        expected_outputs=outputs,
        timeout=600,
    )
    if result.returncode != 0:
        raise AuthorityError(f"C4.2 build receipt fixture failed:\n{result.stdout}")
    digest, count = receipt.finalize(("c42-build-miniature",))
    if count != 1:
        raise AuthorityError("C4.2 build receipt node count differs")
    return digest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-policy", action="store_true")
    arguments = parser.parse_args()
    negatives = 0
    try:
        verify_authority_lock(
            ROOT, DEFAULT_AUTHORITY_LOCK,
            require_policy=not arguments.no_policy,
        )
        verify_c35_manifest(ROOT, DEFAULT_C35_MANIFEST, "cc")

        with tempfile.TemporaryDirectory(prefix="c42-authority-lock-") as name:
            altered = Path(name) / "authority.lock.toml"
            altered.write_bytes(DEFAULT_AUTHORITY_LOCK.read_bytes())
            replace_unique(
                altered, "topology_sha256 = \"", "topology_sha256 = \"0"
            )
            expect_failure(
                lambda: verify_authority_lock(ROOT, altered, require_policy=False),
                "authority lock digest",
            )
            negatives += 1

        with tempfile.TemporaryDirectory(prefix="c42-c35-lock-") as name:
            altered = Path(name) / "c35-reference.toml"
            altered.write_bytes(DEFAULT_C35_MANIFEST.read_bytes())
            replace_unique(altered, "path_count = 105", "path_count = 104")
            expect_failure(
                lambda: verify_c35_manifest(ROOT, altered, "cc"),
                "C3.5 manifest digest",
            )
            negatives += 1

        manifest = load_c35_manifest(DEFAULT_C35_MANIFEST)
        for label, relative, before, after in (
            (
                "forged recipe",
                "frontends/headless-c35/Makefile",
                "check-architecture: lanes",
                "check-architecture:\n\t@echo 'C3.5c architecture: PASS "
                "archive=b1f95f2787fe9a3d585f467d4ba72e6de32938c51273d4ad7f411dc1e644cf6f'",
            ),
            (
                "suppressed checker",
                "scripts/check_c35_architecture.py",
                "def main() -> int:",
                "def main() -> int:\n    return 0\n\ndef suppressed_main() -> int:",
            ),
        ):
            with tempfile.TemporaryDirectory(prefix="c42-c35-substitute-") as name:
                isolated = Path(name) / "repo"
                materialize_c35_reference(ROOT, manifest, isolated)
                replace_unique(isolated / relative, before, after)
                expect_failure(
                    lambda: verify_materialized_c35_reference(manifest, isolated),
                    label,
                )
                negatives += 1

        with tempfile.TemporaryDirectory(prefix="c42-receipt-left-") as left_name, \
                tempfile.TemporaryDirectory(prefix="c42-receipt-right-") as right_name:
            left = receipt_digest(Path(left_name), "stable")
            right = receipt_digest(Path(right_name), "stable")
            changed = receipt_digest(Path(right_name) / "changed", "changed")
            if left != right or left == changed:
                raise AuthorityError("normalized authority receipt determinism differs")
            negatives += 1

        with tempfile.TemporaryDirectory(prefix="c42-build-left-") as left_name, \
                tempfile.TemporaryDirectory(prefix="c42-build-right-") as right_name:
            left = c42_build_receipt_digest(Path(left_name))
            right = c42_build_receipt_digest(Path(right_name))
            if left != right:
                raise AuthorityError("C4.2 normalized build receipt differs")
            negatives += 1

        with tempfile.TemporaryDirectory(prefix="c42-receipt-duplicate-") as name:
            root = Path(name)
            home = root / "home"
            home.mkdir()
            receipt = AuthorityReceipt(ROOT, root)
            receipt.run(
                "duplicate", ["printf", "%s", "one"], cwd=ROOT,
                environment=clean_environment(home), timeout=30,
            )
            expect_failure(
                lambda: receipt.run(
                    "duplicate", ["printf", "%s", "two"], cwd=ROOT,
                    environment=clean_environment(home), timeout=30,
                ),
                "duplicate receipt node",
            )
            negatives += 1
            expect_failure(
                lambda: receipt.finalize(("duplicate", "missing")),
                "missing receipt node",
            )
            negatives += 1
    except (AuthorityError, OSError) as error:
        print(f"C4.2 authority integrity: FAIL: {error}")
        return 1

    print(
        "C4.2 authority integrity: PASS "
        f"negatives={negatives} lock=exact c35=finite receipt=normalized"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
