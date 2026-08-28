<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Test hierarchy

Future BSD-3-Clause tests are grouped by unit/property, fuzz, deterministic replay, power-cut, Guest, Host, endpoint and performance evidence. Every result names its trust/evidence profile and immutable environment.

Pull-request tests remain unprivileged. KVM, kernel modules, raw devices and endpoint hardware run only on disposable, explicitly provisioned runners.
