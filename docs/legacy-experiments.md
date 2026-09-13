<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Retired mechanism experiments

The H0, V0 and C2 mechanism experiments have been retired from `main`, together
with their dedicated tools, tests and current PR check targets. Their historical
results remain in the [results index](results/README.md).

All retired H0/C2 files are recoverable from the unchanged
`v0.1.0-spine-preview.1` tag at commit
`4b2a56272e567a5c8071819f506e4a7bd0acac24`. The links below use that exact commit.

| Experiment | Retained source and test entry |
| --- | --- |
| H0 synthetic PCI enumeration | [Kernel probe](https://github.com/Evanshenf/ssd-firmware-lab/tree/4b2a56272e567a5c8071819f506e4a7bd0acac24/kernel/host-pci-h0), [privileged runner](https://github.com/Evanshenf/ssd-firmware-lab/blob/4b2a56272e567a5c8071819f506e4a7bd0acac24/tests/privileged/m0_host_bridge_h0.sh) |
| V0 emulated VFIO contract | [Kernel harness](https://github.com/Evanshenf/ssd-firmware-lab/tree/4b2a56272e567a5c8071819f506e4a7bd0acac24/kernel/vfio-cdev-v0), [userspace tool](https://github.com/Evanshenf/ssd-firmware-lab/tree/4b2a56272e567a5c8071819f506e4a7bd0acac24/tools/vfio-cdev-v0), [privileged runner](https://github.com/Evanshenf/ssd-firmware-lab/blob/4b2a56272e567a5c8071819f506e4a7bd0acac24/tests/privileged/m0_vfio_cdev_v0.sh) |
| C2.1 A-prime contract | [V1 contract and driver](https://github.com/Evanshenf/ssd-firmware-lab/tree/4b2a56272e567a5c8071819f506e4a7bd0acac24/kernel/vfio-cdev-v1), [unit and fake-provider suite](https://github.com/Evanshenf/ssd-firmware-lab/tree/4b2a56272e567a5c8071819f506e4a7bd0acac24/tests/unit/vfio-c21) |
| C2.2–C2.5 IOAS and lifecycle gates | [Historical userspace tools](https://github.com/Evanshenf/ssd-firmware-lab/tree/4b2a56272e567a5c8071819f506e4a7bd0acac24/tools), [M0/C2 privileged runners](https://github.com/Evanshenf/ssd-firmware-lab/tree/4b2a56272e567a5c8071819f506e4a7bd0acac24/tests/privileged), [C2.5 peer fixture](https://github.com/Evanshenf/ssd-firmware-lab/tree/4b2a56272e567a5c8071819f506e4a7bd0acac24/kernel/vfio-cdev-v1-peer-fixture), [C2.5 dependency checker](https://github.com/Evanshenf/ssd-firmware-lab/blob/4b2a56272e567a5c8071819f506e4a7bd0acac24/scripts/check_c25_architecture.py) |

Use a separate checkout of the recorded commit to inspect these experiments.
Their [Cycle 01](results/2026-08-28-cycle-01-evidence-manifest.md) and
[Cycle 02](results/2026-08-29-cycle-02-evidence-manifest.md) manifests retain the
original source identities, environment requirements and evidence limits.

The separate M4/M5 PoC is preserved by `archive/m4-m5-profile-nested` at commit
[`f3438fee61f82cf305f29bc232aa2c0fa9e1b166`](https://github.com/Evanshenf/ssd-firmware-lab/tree/f3438fee61f82cf305f29bc232aa2c0fa9e1b166).
This archive retains the PoC's original experimental scope.

Only the independent H0/C2 mechanism packages and their dedicated checks are
retired here. Current native M4/M5 uses upstream `vfio-pci` and IOMMUFD, not
those old custom VFIO harnesses. Its production sources, shared J0 composition,
FTL/NFC/media stack and current cross-architecture CI remain unchanged.

The retired Cycle 01/C2 freeze rules remain available in the tag's
[source-boundary policy](https://github.com/Evanshenf/ssd-firmware-lab/blob/4b2a56272e567a5c8071819f506e4a7bd0acac24/policy/source-boundaries.toml).
They are no longer current-checkout requirements. Remaining C3/C4 freeze rules,
the general license/import checks and historical result contents are unchanged.
