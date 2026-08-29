#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Check the narrow C2.5 V1/H0 and portable-source architecture boundary."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
H0_ROOT = Path("kernel/host-pci-h0")
V1_ROOT = Path("kernel/vfio-cdev-v1")
FIXTURE_ROOT = Path("kernel/vfio-cdev-v1-peer-fixture")
A1_HEADER = V1_ROOT / "uapi/unstable/fwlab_c21_a1.h"
PORTABLE_ROOTS = (
    Path("core"),
    Path("media"),
    Path("nfc"),
    Path("include/fwlab/contracts"),
    Path("include/fwlab/portable"),
    Path("include/fwlab/private"),
)
IMPLEMENTATION_ROOTS = PORTABLE_ROOTS[:3]
SOURCE_SUFFIXES = {
    ".asm", ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp",
    ".hxx", ".inc", ".inl", ".ipp", ".s", ".tcc",
}
IMPLEMENTATION_SUFFIXES = {".asm", ".c", ".cc", ".cpp", ".cxx", ".s"}
BUILD_NAMES = {"Kbuild", "Makefile", "meson.build", "CMakeLists.txt"}
IGNORED_IMPLEMENTATION_PARTS = {"build", "fakes", "out", "tests"}

H0_FORBIDDEN = re.compile(
    r"(?:vfio-cdev-v1|ssd[-_]fwlab[-_]vfio[-_]v1|fwlab_c21|FWLAB_V1_)",
    re.IGNORECASE,
)
V1_FORBIDDEN = re.compile(
    r"(?:host-pci-h0|ssd_fwlab_host_h0|fwlab_h0_|FWLAB_H0_)",
    re.IGNORECASE,
)
PORTABLE_FORBIDDEN = re.compile(
    r"(?:\bfwlab_c21\b|\bFWLAB_C21_|fwlab_c21_a1|\bFWA1\b|"
    r"\bIOVA\b|\bVFIO\b|\bIOMMUFD\b|A[-_ ]prime|A\u2032)",
    re.IGNORECASE,
)


def project_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT,
        check=False,
        capture_output=True,
    )
    if result.returncode:
        raise RuntimeError("git ls-files failed")
    return sorted(
        Path(raw.decode("utf-8"))
        for raw in result.stdout.split(b"\0")
        if raw
    )


def is_under(path: Path, root: Path) -> bool:
    return path == root or root in path.parents


def is_checked_source(path: Path) -> bool:
    return path.suffix.lower() in SOURCE_SUFFIXES or path.name in BUILD_NAMES


def read_text(relative: Path, failures: list[str]) -> str | None:
    path = ROOT / relative
    if not path.is_file() or path.is_symlink():
        failures.append(f"missing, non-regular or symbolic architecture input: {relative}")
        return None
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        failures.append(f"cannot read architecture input {relative}: {error}")
        return None


def check_cross_dependencies(files: list[Path], failures: list[str]) -> None:
    for relative in files:
        if not is_checked_source(relative):
            continue
        if is_under(relative, H0_ROOT):
            pattern = H0_FORBIDDEN
            other = "V1"
        elif is_under(relative, V1_ROOT):
            pattern = V1_FORBIDDEN
            other = "H0"
        else:
            continue
        text = read_text(relative, failures)
        if text is not None and pattern.search(text):
            failures.append(f"{relative}: direct {other} dependency marker is forbidden")


def check_fixture(files: list[Path], failures: list[str]) -> None:
    source = FIXTURE_ROOT / "ssd_fwlab_v1_peer_fixture.c"
    makefile = FIXTURE_ROOT / "Makefile"
    fixture_sources = [
        path for path in files
        if is_under(path, FIXTURE_ROOT) and path.suffix.lower() in SOURCE_SUFFIXES
    ]
    if fixture_sources != [source]:
        failures.append(
            "peer fixture must contain exactly one source file: "
            f"expected {source}, got {fixture_sources}"
        )

    source_text = read_text(source, failures)
    make_text = read_text(makefile, failures)
    if source_text is None or make_text is None:
        return

    includes = re.findall(r"^\s*#\s*include\s+([^\n]+)$", source_text, re.MULTILINE)
    allowed_includes = {
        "<linux/device.h>",
        "<linux/err.h>",
        "<linux/errno.h>",
        "<linux/module.h>",
        "<linux/platform_device.h>",
        "<linux/string.h>",
    }
    if set(includes) != allowed_includes or len(includes) != len(allowed_includes):
        failures.append(f"{source}: include set is not the fixed Linux-only fixture set")

    forbidden = re.compile(
        r"(?:MODULE_SOFTDEP|EXPORT_SYMBOL|host-pci-h0|ssd_fwlab_host_h0|"
        r"fwlab_h0_|fwlab_c21|FWLAB_C21_|ssd_fwlab_vfio_v1|"
        r"\bfwlab_v1_(?!peer_)|\bIOVA\b|\bvfio_|\biommufd_|"
        r"(?:^|[\s/])(?:core|media|nfc|frontends)/)",
        re.IGNORECASE | re.MULTILINE,
    )
    if forbidden.search(source_text) or forbidden.search(make_text):
        failures.append("peer fixture contains a forbidden H0/V1-state/portable dependency")

    required_fragments = (
        '#define FWLAB_V1_PEER_DRIVER_NAME "ssd-fwlab-vfio-v1"',
        "#define FWLAB_V1_PEER_DEVICE_ID 0",
        "platform_device_register_simple(FWLAB_V1_PEER_DRIVER_NAME,",
        "pdev->dev.driver",
        "platform_get_drvdata(pdev)",
        "platform_device_unregister(fwlab_v1_peer_pdev)",
    )
    for fragment in required_fragments:
        if fragment not in source_text:
            failures.append(f"{source}: missing required fixture fragment: {fragment}")
    if "obj-m += ssd_fwlab_v1_peer_fixture.o" not in make_text:
        failures.append(f"{makefile}: unexpected fixture module target")
    if re.search(r"(?:(?:^|\s)-I\S*|ccflags|vfio-cdev-v1|host-pci-h0|"
                 r"core/|media/|nfc/)",
                 make_text, re.IGNORECASE):
        failures.append(f"{makefile}: cross-directory build dependency is forbidden")


def check_unstable_boundary(files: list[Path], failures: list[str]) -> None:
    copies = [path for path in files if path.name == A1_HEADER.name]
    if copies != [A1_HEADER]:
        failures.append(
            "A-prime header must exist only at its unstable kernel-test path: "
            f"expected {[A1_HEADER]}, got {copies}"
        )

    portable_sources: list[Path] = []
    implementation_sources: list[Path] = []
    for relative in files:
        if not any(is_under(relative, root) for root in PORTABLE_ROOTS):
            continue
        if relative.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        portable_sources.append(relative)
        if (
            any(is_under(relative, root) for root in IMPLEMENTATION_ROOTS)
            and relative.suffix.lower() in IMPLEMENTATION_SUFFIXES
            and not IGNORED_IMPLEMENTATION_PARTS.intersection(relative.parts)
        ):
            implementation_sources.append(relative)
        text = read_text(relative, failures)
        if text is not None and PORTABLE_FORBIDDEN.search(text):
            failures.append(
                f"{relative}: A-prime/IOVA/VFIO/iommufd is forbidden in portable source"
            )

    print(f"C2.5 portable source files checked: {len(portable_sources)}")
    print(
        "C2.5 portable implementation source count: "
        f"{len(implementation_sources)}"
    )
    if implementation_sources:
        print(
            "C2.5 portable implementation sources: "
            + ", ".join(str(path) for path in implementation_sources)
        )
    else:
        print("C2.5 portable implementation sources: none (design-only)")


def main() -> int:
    failures: list[str] = []
    try:
        files = project_files()
    except (OSError, RuntimeError, UnicodeDecodeError) as error:
        print(f"C2.5 architecture check cannot enumerate project files: {error}",
              file=sys.stderr)
        return 1

    check_cross_dependencies(files, failures)
    check_fixture(files, failures)
    check_unstable_boundary(files, failures)

    if failures:
        print("C2.5 architecture isolation check failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("C2.5 H0/V1 dependency direction: PASS")
    print("C2.5 peer fixture boundary: PASS")
    print("C2.5 unstable/portable boundary: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
