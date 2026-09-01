#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Keep the M4 transport, executor fixture and media fixture separated."""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
KERNEL = ROOT / "kernel" / "m4-synthetic-pci-poc"


def read(name: str) -> str:
    return (KERNEL / name).read_text(encoding="utf-8")


def reject(name: str, text: str, patterns: tuple[str, ...], failures: list[str]) -> None:
    for pattern in patterns:
        if re.search(pattern, text):
            failures.append(f"{name} contains forbidden dependency {pattern!r}")


def require(name: str, text: str, patterns: tuple[str, ...], failures: list[str]) -> None:
    for pattern in patterns:
        if not re.search(pattern, text):
            failures.append(f"{name} is missing boundary marker {pattern!r}")


def main() -> int:
    failures: list[str] = []
    pci = read("m4_pci.c")
    nvme = read("m4_nvme.c")
    media = read("m4_media_fixture.c")
    frontend = read("m4_frontend.h") + read("m4_frontend.c")
    makefile = read("Makefile")

    reject(
        "m4_pci.c",
        pci,
        (
            r"\bfilp_open\b",
            r"\bkernel_(?:read|write)\b",
            r"\bvfs_fsync\b",
            r"\bbackend_path\b",
            r"struct fwlab_m4_nvme",
        ),
        failures,
    )
    reject(
        "m4_nvme.c",
        nvme,
        (
            r'"m4_internal\.h"',
            r"struct pci_dev",
            r"->bar_mapping",
            r"\b(?:readl|readq|writel|writeq|memset_io)\b",
            r"\bfwlab_m4_dma_transfer\b",
            r"\b(?:filp_open|kernel_read|kernel_write|vfs_fsync)\b",
        ),
        failures,
    )
    reject(
        "m4_media_fixture.c",
        media,
        (
            r"struct pci_dev",
            r"\b(?:readl|writel|msi_get_virq)\b",
            r"\bfwlab_m4_dma_transfer\b",
            r"\bNVME_",
        ),
        failures,
    )
    require(
        "frontend seam",
        frontend,
        (
            r"FWLAB_M4_FRONTEND_SERVICES_VERSION",
            r"struct fwlab_m4_frontend_services",
            r"struct fwlab_m4_frontend_ops",
            r"\(\*stop\)",
            r"\(\*quiescent\)",
            r"WARN_ON_ONCE\(!binding->ops->quiescent",
        ),
        failures,
    )
    require(
        "NVMe fixture",
        nvme,
        (
            r"fwlab_m4_nvme_fixture_ops",
            r"fwlab_m4_media_(?:read|write|flush)",
            r"services->dma",
            r"services->notify",
        ),
        failures,
    )
    require(
        "kernel Makefile",
        makefile,
        (r"m4_frontend\.o", r"m4_media_fixture\.o"),
        failures,
    )
    require(
        "BAR service bounds",
        pci,
        (
            r"length <= FWLAB_M4_BAR_MAP_SIZE",
            r"offset <= FWLAB_M4_BAR_MAP_SIZE - length",
        ),
        failures,
    )

    if failures:
        print("M4 frontend architecture: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("M4 frontend architecture: PASS (transport/executor/media split)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
