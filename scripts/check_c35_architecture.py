#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Audit the C3.5c portable-core archive, final links, and projections."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "frontends" / "headless-c35"
BUILD = COMPONENT / "build"
ARCHIVE = BUILD / "libfwlab_portable_core_c31_c34.a"
EXPECTED_ARCHIVE_SHA256 = (
    "b1f95f2787fe9a3d585f467d4ba72e6de32938c51273d4ad7f411dc1e644cf6f"
)

ARCHIVE_MEMBERS = [
    "c31.o",
    "c31_codec.o",
    "c32_policy.o",
    "c34_codec.o",
    "c34_recovery.o",
    "c34_mapping.o",
    "c34_journal.o",
    "c34_checkpoint.o",
    "c34_nfc_graph.o",
    "c34_coordinator.o",
    "c34_drive.o",
    "c34_provider.o",
]

ARCHIVE_SOURCES = [
    ROOT / "core" / "c31.c",
    ROOT / "core" / "c31_codec.c",
    ROOT / "core" / "c32" / "c32_policy.c",
    ROOT / "core" / "c34" / "c34_codec.c",
    ROOT / "core" / "c34" / "c34_recovery.c",
    ROOT / "core" / "c34" / "c34_mapping.c",
    ROOT / "core" / "c34" / "c34_journal.c",
    ROOT / "core" / "c34" / "c34_checkpoint.c",
    ROOT / "core" / "c34" / "c34_nfc_graph.c",
    ROOT / "core" / "c34" / "c34_coordinator.c",
    ROOT / "core" / "c34" / "c34_drive.c",
    ROOT / "core" / "c34" / "c34_provider.c",
]

EXPECTED_ARCHIVE_HEADERS = {
    "core/c31_internal.h",
    "core/c34/c34.h",
    "core/c34/c34_internal.h",
    "include/fwlab/contracts/c31_provider.h",
    "include/fwlab/contracts/nand_media.h",
    "include/fwlab/contracts/nfc_provider.h",
    "include/fwlab/contracts/persistence_facts.h",
    "include/fwlab/portable/c31.h",
    "include/fwlab/portable/c31_codec.h",
    "include/fwlab/portable/c31_types.h",
    "include/fwlab/portable/nfc_types.h",
    "include/fwlab/portable/persistence_policy.h",
    "include/fwlab/private/c34_physical_txn.h",
}

FORBIDDEN_PORTABILITY_WORDS = re.compile(
    r"\b(vfio|qemu|nvme|pcie?|bar|irq|iova|linux|errno)\b|a-prime",
    re.IGNORECASE,
)

POSIX_SYMBOLS = {
    "__errno_location",
    "fdatasync",
    "fstat",
    "ftruncate",
    "pread",
    "pwrite",
    "open",
    "close",
    "unlink",
    "ioctl",
    "mmap",
}


def fail(message: str) -> None:
    raise RuntimeError(message)


def run(*args: str, cwd: Path | None = None) -> bytes:
    return subprocess.check_output(args, cwd=cwd or ROOT)


def symbols(path: Path, undefined: bool) -> set[str]:
    flag = "-u" if undefined else "--defined-only"
    output = run("nm", "-A", flag, str(path)).decode("utf-8", "replace")
    found: set[str] = set()
    for line in output.splitlines():
        fields = line.split()
        if fields:
            found.add(fields[-1].split("@", 1)[0])
    return found


def dependency_headers(dep: Path, component: Path, root: Path) -> set[Path]:
    text = dep.read_text(encoding="utf-8").replace("\\\n", " ")
    try:
        dependencies = text.split(":", 1)[1].split()
    except IndexError as error:
        raise RuntimeError(f"malformed dependency file: {dep}") from error
    headers: set[Path] = set()
    resolved_root = root.resolve()
    for token in dependencies:
        if not token.endswith(".h"):
            continue
        path = (component / token).resolve()
        try:
            path.relative_to(resolved_root)
        except ValueError as error:
            raise RuntimeError(
                f"archive dependency escapes repository: {token!r} in {dep}"
            ) from error
        if not path.is_file() or path.is_symlink():
            fail(f"archive dependency is unavailable or indirect: {path}")
        headers.add(path)
    return headers


def portability_token(path: Path) -> re.Match[str] | None:
    return FORBIDDEN_PORTABILITY_WORDS.search(
        path.read_text(encoding="utf-8")
    )


def audit_dependency_scanner() -> None:
    with tempfile.TemporaryDirectory(prefix="c35-dependency-mutation-") as directory:
        root = Path(directory) / "repo"
        component = root / "frontends" / "headless-c35"
        header = root / "include" / "fwlab" / "portable" / "mutation.h"
        dep = component / "build" / "fw" / "mutation.d"
        header.parent.mkdir(parents=True)
        dep.parent.mkdir(parents=True)
        header.write_text("/* nvme transport mutation */\n", encoding="utf-8")
        dep.write_text(
            "build/fw/mutation.o: ../../core/mutation.c "
            "../../include/fwlab/portable/mutation.h\n",
            encoding="utf-8",
        )
        parsed = dependency_headers(dep, component, root)
        if parsed != {header.resolve()}:
            fail("dependency scanner did not resolve the injected header")
        if portability_token(header) is None:
            fail("dependency scanner mutation did not detect transport content")


def audit_archive() -> str:
    if not ARCHIVE.is_file():
        fail(f"missing archive: {ARCHIVE}")
    members = run("ar", "t", str(ARCHIVE)).decode().splitlines()
    if members != ARCHIVE_MEMBERS:
        fail(f"archive member mismatch: {members!r}")

    dep_names = sorted(path.name for path in (BUILD / "fw").glob("*.d"))
    expected_dep_names = sorted(name.replace(".o", ".d")
                                for name in ARCHIVE_MEMBERS)
    if dep_names != expected_dep_names:
        fail(f"dependency file mismatch: {dep_names!r}")
    archive_headers: set[Path] = set()
    for member, source in zip(ARCHIVE_MEMBERS, ARCHIVE_SOURCES):
        dep = BUILD / "fw" / member.replace(".o", ".d")
        text = dep.read_text(encoding="utf-8").replace("\\\n", " ")
        if member not in text.split(":", 1)[0] or source.name not in text:
            fail(f"dependency provenance mismatch: {dep}")
        archive_headers.update(dependency_headers(dep, COMPONENT, ROOT))
    actual_headers = {
        path.relative_to(ROOT.resolve()).as_posix()
        for path in archive_headers
    }
    if actual_headers != EXPECTED_ARCHIVE_HEADERS:
        fail(f"archive header dependency mismatch: {sorted(actual_headers)!r}")

    nm_output = run("nm", "-A", "--defined-only", str(ARCHIVE)).decode()
    writable = []
    for line in nm_output.splitlines():
        match = re.search(r"\s([BCDGSV])\s+([^\s]+)$", line)
        if match:
            writable.append((match.group(1), match.group(2)))
    if writable:
        fail(f"writable archive symbols: {writable!r}")

    defined = symbols(ARCHIVE, False)
    external = symbols(ARCHIVE, True) - defined
    allowed_external = {
        "__stack_chk_fail",
        "memcpy",
        "memmove",
        "memcmp",
        "memset",
    }
    if external - allowed_external:
        fail(f"unexpected archive undefined symbols: {sorted(external)}")
    forbidden_symbols = {
        "malloc", "calloc", "realloc", "free", "pthread_create",
        "pthread_join", "clock_gettime", "nanosleep", "sleep", "rand",
        "random", "srand", "open", "close", "pread", "pwrite",
        "fdatasync", "ftruncate", "ioctl", "mmap",
    }
    if (defined | external) & forbidden_symbols:
        fail("forbidden runtime symbol entered firmware archive")

    audit_dependency_scanner()
    scan_paths = ARCHIVE_SOURCES + sorted(archive_headers)
    for path in scan_paths:
        match = portability_token(path)
        if match:
            fail(f"host/transport token {match.group(0)!r} in {path}")
    archive_strings = run("strings", "-a", str(ARCHIVE)).decode(
        "utf-8", "replace")
    match = FORBIDDEN_PORTABILITY_WORDS.search(archive_strings)
    if match:
        fail(f"host/transport string {match.group(0)!r} in archive")

    with tempfile.TemporaryDirectory(prefix="c35-archive-") as directory:
        rebuilt = Path(directory) / "rebuilt.a"
        objects = [BUILD / "fw" / member for member in ARCHIVE_MEMBERS]
        subprocess.check_call(
            [os.environ.get("AR", "ar"), "rcsD", str(rebuilt),
             *(str(path) for path in objects)],
            cwd=ROOT,
        )
        if rebuilt.read_bytes() != ARCHIVE.read_bytes():
            fail("deterministic archive reconstruction differs")
    digest = hashlib.sha256(ARCHIVE.read_bytes()).hexdigest()
    if digest != EXPECTED_ARCHIVE_SHA256:
        fail(f"portable-core archive changed: {digest}")
    return digest


def audit_headless_boundary() -> None:
    source = (COMPONENT / "c35_headless.c").read_text(encoding="utf-8")
    header = (COMPONENT / "c35_headless.h").read_text(encoding="utf-8")
    finalizer = (COMPONENT / "c35_finalizer.c").read_text(encoding="utf-8")
    makefile = (COMPONENT / "Makefile").read_text(encoding="utf-8")
    includes = re.findall(r'^#include\s+"([^"]+)"', source, re.MULTILINE)
    if includes != ["c35_headless.h"]:
        fail(f"generic headless private include leak: {includes!r}")
    forbidden = re.compile(r"\b(c32|c33|c34|file|posix|scripted|model)\b",
                           re.IGNORECASE)
    match = forbidden.search(source)
    if match:
        fail(f"provider branch token in generic headless: {match.group(0)}")
    for token in (
            "c35_headless_compat_query", "c35_headless_compat_transfer",
            "c35_headless_submit_status", "c35_headless_complete_status",
            "c35_headless_reset_status", "c35_headless_teardown_status"):
        if token not in header or token not in source:
            fail(f"missing wrapper recovery API: {token}")
    if "C35_FINALIZER_PENDING_RETIRE" not in finalizer or \
            "c35_headless_compat_query" not in finalizer or \
            "c35_headless_compat_transfer" not in finalizer:
        fail("runtime finalizer lacks wrapper-token adoption")
    if "test_wrapper_recovery.c" not in makefile or \
            "$(WRAPPER_BIN)" not in makefile:
        fail("wrapper recovery test is outside the standard check gate")
    for token in ("next_teardown_uid", "teardown_uid_limit"):
        if token not in header or token not in source:
            fail(f"missing protected teardown token domain: {token}")
    teardown_start = source.index("enum c35_result c35_teardown_start")
    teardown_end = source.index("static void fault_if_needed", teardown_start)
    teardown_body = source[teardown_start:teardown_end]
    proof = teardown_body.find("teardown_token_prepare")
    mutations = [position for position in (
        teardown_body.find("headless->previous_control ="),
        teardown_body.find("memset(&headless->control"),
    ) if position >= 0]
    if proof < 0 or not mutations or proof > min(mutations):
        fail("teardown mutates active control before capacity proof")
    if "control_allocate(headless, C35_OPERATION_TEARDOWN" in source:
        fail("reset and teardown still share the control-token allocator")

    with tempfile.TemporaryDirectory(prefix="c35a-headless-") as directory:
        for name in ("c35_headless", "c35_finalizer"):
            obj = Path(directory) / f"{name}.o"
            subprocess.check_call([
                os.environ.get("CC", "cc"), "-std=c11", "-fno-common",
                "-I", str(ROOT / "include"),
                "-I", str(COMPONENT),
                "-c", str(COMPONENT / f"{name}.c"), "-o", str(obj),
            ])
            trace_symbols = {
                symbol for symbol in symbols(obj, True)
                if symbol.startswith("c35_trace_")
            }
            if trace_symbols:
                fail(f"observer entered authoritative {name} object: "
                     f"{sorted(trace_symbols)}")

    for path in COMPONENT.rglob("*.c"):
        text = path.read_text(encoding="utf-8")
        if "(void)c35_operation_retire" in text:
            fail(f"suppressed C35 retirement result: {path}")
        if "pthread_" in text and \
                path.name != "test_threads.c":
            fail(f"pthread outside dedicated isolation driver: {path}")
        if path.name != "c35_lifecycle_port.c" and re.search(
                r"\bfwlab_c31_(?:submit|step|completion_(?:acquire|release|"
                r"consume)|abort_(?:request|ack)|reset_(?:begin|ack)|"
                r"teardown_(?:begin|ack))\s*\(", text):
            fail(f"direct C31 mutation bypasses lifecycle port: {path}")


def lane_output(lane: str, mode: str) -> bytes:
    binary = BUILD / f"c35_lane_{lane}"
    if not binary.is_file():
        fail(f"missing lane binary: {binary}")
    return run(str(binary), mode)


def audit_lane_links(archive_hash: str) -> dict[str, str]:
    makefile = (COMPONENT / "Makefile").read_text(encoding="utf-8")
    required_allowlist_tokens = [
        "LANE_BASE_SOURCES", "LANE_C34_SOURCES", "LANE_FILE_SOURCES",
        "$(LANE_S_BIN)", "$(LANE_M_BIN)", "$(LANE_B_BIN)",
        "$(LANE_P_BIN)", "$(FW_ARCHIVE)",
    ]
    for token in required_allowlist_tokens:
        if token not in makefile:
            fail(f"missing explicit link allowlist token: {token}")

    for lane in "smbp":
        binary = BUILD / f"c35_lane_{lane}"
        link_map = BUILD / f"c35_lane_{lane}.map"
        if not binary.is_file() or not link_map.is_file():
            fail(f"missing {lane} lane link artifact")
        map_text = link_map.read_text(encoding="utf-8", errors="replace")
        if "libfwlab_portable_core_c31_c34.a" not in map_text:
            fail(f"lane {lane} did not consume frozen firmware archive")
        pulled = set(re.findall(
            r"libfwlab_portable_core_c31_c34\.a\(([^)]+)\)", map_text))
        expected_pulled = ({"c31.o", "c31_codec.o"} if lane == "s"
                           else set(ARCHIVE_MEMBERS))
        if pulled != expected_pulled:
            fail(f"lane {lane} archive provenance mismatch: {sorted(pulled)}")
        if lane == "s" and re.search(r"\bc34_|\bfwlab_nfc_model_", map_text):
            fail("scripted lane linked storage/NFC implementation")
        if lane == "m" and re.search(r"\bc34_file_", map_text):
            fail("memory lane linked file engine")
        if lane == "b" and re.search(r"\bc34_file_posix_", map_text):
            fail("byte lane linked POSIX adapter")
        if lane == "p" and "c34_file_posix_format" not in map_text:
            fail("POSIX lane omitted its isolated fd adapter")
        if "c35_fault_lifecycle" in map_text or "c35_fault_binding" in map_text:
            fail(f"test fault decorator entered final lane {lane}")
        undefined = symbols(binary, True)
        if lane != "p" and undefined & POSIX_SYMBOLS:
            fail(f"POSIX symbol leaked into lane {lane}: "
                 f"{sorted(undefined & POSIX_SYMBOLS)}")
        if lane == "p" and undefined & {"open", "ioctl", "mmap"}:
            fail(f"forbidden P-lane syscall: {sorted(undefined)}")

    with tempfile.TemporaryDirectory(prefix="c35-posix-object-") as directory:
        obj = Path(directory) / "c34_file_posix.o"
        subprocess.check_call([
            os.environ.get("CC", "cc"), "-std=c11", "-fno-common",
            "-I", str(ROOT / "include"),
            "-I", str(ROOT / "media" / "c34-file"),
            "-c", str(ROOT / "media" / "c34-file" / "c34_file_posix.c"),
            "-o", str(obj),
        ])
        undefined = symbols(obj, True)
        adapter_allowed = {
            "__errno_location", "fdatasync", "fstat", "ftruncate",
            "pread", "pwrite", "memset", "__stack_chk_fail",
            "c34_file_format", "c34_file_restart",
        }
        if undefined - adapter_allowed:
            fail(f"P adapter undefined-symbol leak: {sorted(undefined)}")
        if undefined & {"open", "close", "unlink", "ioctl", "mmap"}:
            fail("P adapter owns fd lifecycle or mapping")

    life = {lane: lane_output(lane, "life") for lane in "smbp"}
    if any(life[lane] != life["s"] for lane in "mbp"):
        fail("E_life S/M/B/P byte projection mismatch")
    semantic = {lane: lane_output(lane, "semantic") for lane in "mbp"}
    if semantic["m"] != semantic["b"] or semantic["m"] != semantic["p"]:
        fail("E_sem/E_raw M/B/P byte projection mismatch")
    container = {lane: lane_output(lane, "container") for lane in "bp"}
    if container["b"] != container["p"]:
        fail("E_container B/P byte projection mismatch")
    two_atom = {lane: lane_output(lane, "two-atom") for lane in "mbp"}
    if two_atom["m"] != two_atom["b"] or two_atom["m"] != two_atom["p"]:
        fail("two-atom M/B/P canonical projection mismatch")

    return {
        "archive": archive_hash,
        "life": hashlib.sha256(life["s"]).hexdigest(),
        "semantic_raw": hashlib.sha256(semantic["m"]).hexdigest(),
        "container": hashlib.sha256(container["b"]).hexdigest(),
        "two_atom": hashlib.sha256(two_atom["m"]).hexdigest(),
    }


def main() -> int:
    try:
        archive_hash = audit_archive()
        audit_headless_boundary()
        hashes = audit_lane_links(archive_hash)
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"C3.5c architecture: FAIL: {error}", file=sys.stderr)
        return 1
    print("C3.5c architecture: PASS " + " ".join(
        f"{name}={value}" for name, value in hashes.items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
