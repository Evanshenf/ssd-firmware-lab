<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Test hierarchy

Future BSD-3-Clause tests are grouped by unit/property, fuzz, deterministic replay, power-cut, Guest, Host, endpoint and performance evidence. Every result names its trust/evidence profile and immutable environment.

Pull-request tests remain unprivileged. KVM, kernel modules, raw devices and endpoint hardware run only on disposable, explicitly provisioned runners.

The first independent unit runner is the [C2.1 A-prime state and fake-provider suite](unit/vfio-c21/README.md). It compiles the V1 test contract as ordinary C11 and does not require a kernel module or the portable SSD stack.

The C2.5 privileged gate combines a test-only second platform-device fixture
with a two-cdev userspace oracle. Its unprivileged selftest and architecture
dependency checker run in ordinary PR checks; real IOAS and device-remove
evidence remains restricted to the provisioned `Profile-Nested` runner.
