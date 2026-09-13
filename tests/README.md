<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Test hierarchy

Future BSD-3-Clause tests are grouped by unit/property, fuzz, deterministic replay, power-cut, Guest, Host, endpoint and performance evidence. Every result names its trust/evidence profile and immutable environment.

Pull-request tests remain unprivileged. KVM, kernel modules, raw devices and endpoint hardware run only on disposable, explicitly provisioned runners.

Current component and integration checks live beside their production modules
under `core/`, `media/` and `frontends/`. The current software entry is
[check_current_spine.sh](../scripts/check_current_spine.sh); the remaining
[C4 portable workflow](../.github/workflows/c4-portable.yml) retains its own
historical scope. This first retirement does not remove either workflow.

The historical [C2.1 A-prime state and fake-provider suite](https://github.com/Evanshenf/ssd-firmware-lab/blob/4b2a56272e567a5c8071819f506e4a7bd0acac24/tests/unit/vfio-c21/README.md) compiled the V1 test contract as ordinary C11 without a kernel module or the portable SSD stack.

The C2.5 privileged gate combined a test-only second platform-device fixture
with a two-cdev userspace oracle. Its real IOAS and device-remove evidence
remains scoped to the recorded `Profile-Nested` runner. H0/C2 tests and their
dedicated selftest and architecture checks have been retired from current PR
checks; their exact code and runners are in the [legacy experiment archive](../docs/legacy-experiments.md).
