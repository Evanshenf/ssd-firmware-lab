#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Fail closed on unsafe or out-of-scope public-repository content."""

from __future__ import annotations

from pathlib import Path
import re
import subprocess
import sys
import tomllib


ROOT = Path(__file__).resolve().parents[1]
IGNORED_PARTS = {".git"}
FORBIDDEN_SUFFIXES = {
    ".7z", ".bin", ".doc", ".docx", ".dump", ".elf", ".fw", ".gz",
    ".img", ".iso", ".key", ".p12", ".patch", ".pcap", ".pdf", ".pem",
    ".pfx", ".ppt", ".pptx", ".qcow2", ".rar", ".raw", ".tar", ".tgz",
    ".wal", ".xlsx", ".zip",
}
FORBIDDEN_ROOTS = {
    Path("vendor"), Path("third_party/gpl"), Path("private"), Path("internal")
}
MAX_FILE_BYTES = 1_000_000
SPDX_LICENSE_TAG = "SPDX-License-" + "Identifier:"
ALLOWED_LICENSES = {"BSD-3-Clause", "CC-BY-4.0", "GPL-2.0-only"}
SECRET_PATTERNS = {
    "GitHub token": re.compile(r"(?:gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,})"),
    "private key": re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |DSA |PGP )?PRIVATE KEY-----"),
    "credential URL": re.compile(r"https?://[^/@\s]+:[^/@\s]+@"),
    "OpenAI-style secret": re.compile(r"\bsk-(?:proj-)?[A-Za-z0-9_-]{20,}"),
}
PRIVATE_IP = re.compile(
    r"(?<![0-9])(?:10(?:\.[0-9]{1,3}){3}|192\.168(?:\.[0-9]{1,3}){2}|"
    r"172\.(?:1[6-9]|2[0-9]|3[01])(?:\.[0-9]{1,3}){2})(?![0-9])"
)
RAW_TRANSCRIPT_NAME = re.compile(
    r"(?:^|/)(?:reviews?|prompts?|answers?|transcripts?|raw[-_]?chat)(?:/|$)", re.IGNORECASE
)
MUTABLE_ACTION = re.compile(
    r"^\s*(?:-\s*)?uses:\s*['\"]?(?!\./)([^@\s'\"]+)@([^\s#'\"]+)['\"]?",
    re.MULTILINE,
)


def project_files():
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT,
        check=False,
        capture_output=True,
    )
    if result.returncode == 0:
        relatives = sorted(
            Path(raw.decode("utf-8")) for raw in result.stdout.split(b"\0") if raw
        )
        for relative in relatives:
            path = ROOT / relative
            if path.exists() or path.is_symlink():
                yield path, relative
        return

    for path in sorted(ROOT.rglob("*")):
        if path.is_file() and not IGNORED_PARTS.intersection(path.parts):
            yield path, path.relative_to(ROOT)


def spdx_license_from(header: str) -> str | None:
    for line in header.splitlines():
        if SPDX_LICENSE_TAG not in line:
            continue
        value = line.split(SPDX_LICENSE_TAG, 1)[1]
        value = value.split("-->", 1)[0]
        return value.strip(" \t#/*<>!-")
    return None


def load_policy(relative: str, failures: list[str]):
    path = ROOT / relative
    try:
        with path.open("rb") as stream:
            return tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as error:
        failures.append(f"invalid or missing policy {relative}: {error}")
        return {}


def main() -> int:
    failures: list[str] = []
    for forbidden in FORBIDDEN_ROOTS:
        if (ROOT / forbidden).exists():
            failures.append(f"forbidden source root exists: {forbidden}")
    if (ROOT / ".gitmodules").exists():
        failures.append("third-party submodules are forbidden in the baseline")

    for path, relative in project_files():
        relative_text = relative.as_posix()
        if path.is_symlink():
            failures.append(f"symbolic link is forbidden: {relative}")
            continue
        if path.suffix.lower() in FORBIDDEN_SUFFIXES:
            failures.append(f"forbidden artifact type: {relative}")
        if path.stat().st_size > MAX_FILE_BYTES:
            failures.append(f"file exceeds {MAX_FILE_BYTES} bytes: {relative}")
        if RAW_TRANSCRIPT_NAME.search(relative_text):
            failures.append(f"raw model/review material path is forbidden: {relative}")

        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            failures.append(f"binary content is forbidden: {relative}")
            continue

        for label, pattern in SECRET_PATTERNS.items():
            if pattern.search(text):
                failures.append(f"possible {label} in {relative}; value suppressed")
        if PRIVATE_IP.search(text):
            failures.append(f"private IP address in {relative}; value suppressed")

        if relative != Path("LICENSE") and relative.parts[0] != "LICENSES":
            header = "\n".join(text.splitlines()[:16])
            license_id = spdx_license_from(header)
            if license_id not in ALLOWED_LICENSES:
                failures.append(f"missing or unapproved SPDX expression in {relative}")
            elif relative.suffix == ".md" and license_id != "CC-BY-4.0":
                failures.append(f"Markdown must use CC-BY-4.0: {relative}")
            elif relative.parts[0] == "kernel" and relative.suffix != ".md":
                if license_id != "GPL-2.0-only":
                    failures.append(f"kernel source/policy must use GPL-2.0-only: {relative}")
            elif relative.suffix != ".md" and license_id != "BSD-3-Clause":
                failures.append(f"non-kernel project file must use BSD-3-Clause: {relative}")

        if relative.parts[:2] == (".github", "workflows"):
            if "pull_request_target:" in text:
                failures.append(f"pull_request_target is forbidden: {relative}")
            for action, revision in MUTABLE_ACTION.findall(text):
                if not re.fullmatch(r"[0-9a-f]{40}", revision):
                    failures.append(f"action is not pinned to a full commit: {action}@{revision}")

    provenance = ROOT / "docs/provenance/sources.yaml"
    if provenance.exists():
        text = provenance.read_text(encoding="utf-8")
        for moving in re.finditer(r"revision:\s*['\"]?(?:main|master|HEAD)['\"]?", text):
            failures.append(f"moving provenance revision near byte {moving.start()}")

    gates = load_policy("policy/release-gates.toml", failures)
    gate = gates.get("gate", {})
    if gate.get("official_recognition", {}).get("required") is not False:
        failures.append("official recognition must remain separate and not required")
    if gate.get("source_and_protocol_boundary", {}).get("status") != "enforced":
        failures.append("source/protocol boundary must remain enforced")
    required_forbidden = {
        "official-specification-pdf",
        "official-logo-or-certification-mark",
        "near-verbatim-register-opcode-bitfield-table",
        "raw-model-transcript",
    }
    actual_forbidden = set(
        gate.get("source_and_protocol_boundary", {}).get("forbidden_artifacts", [])
    )
    if actual_forbidden != required_forbidden:
        failures.append("source/protocol forbidden-artifact set changed or is incomplete")
    implementation = gate.get("protocol_implementation_basis", {})
    if implementation.get("status") != "review-required":
        failures.append("protocol implementation basis must remain review-required")
    if implementation.get("blocks_initial_design_repository") is not False:
        failures.append("protocol review must not block the initial design repository")

    boundaries = load_policy("policy/source-boundaries.toml", failures)
    expected_licenses = {
        "user_space": "BSD-3-Clause",
        "kernel_source": "GPL-2.0-only",
        "documentation": "CC-BY-4.0",
    }
    if boundaries.get("licenses") != expected_licenses:
        failures.append("source-boundaries license matrix changed or is incomplete")
    if boundaries.get("paths", {}).get("gpl_source_roots") != ["kernel"]:
        failures.append("GPL source roots must remain restricted to kernel/")
    provenance_policy = boundaries.get("provenance", {})
    if provenance_policy.get("raw_model_transcripts") is not False:
        failures.append("raw model transcripts must remain forbidden")
    if provenance_policy.get("third_party_submodules") is not False:
        failures.append("third-party submodules must remain forbidden")
    if provenance_policy.get("moving_revisions_for_code") is not False:
        failures.append("moving provenance revisions for code must remain forbidden")

    if failures:
        print("Repository policy failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("Repository policy: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
