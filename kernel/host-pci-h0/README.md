<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# H0 synthetic PCI enumeration probe

This is the first privileged Milestone-0 feasibility probe. It asks one narrow question: can an Ubuntu GA 7.0 GPL out-of-tree module use exported APIs to register and later remove a synthetic PCI root bus and one inert function?

H0 intentionally exposes:

- one dynamically allocated emulation-only PCI domain at or above `0x10000`, outside the 16-bit physical ACPI segment range;
- bus `00`, slot `00`, function `0`;
- lab-only vendor/device IDs `fffa:0001`; `fffa` is merely absent from the pinned test environment's current `pci.ids`, is not assigned to or owned by this project, and must not be used for distributed hardware;
- vendor-specific class `ff0000`;
- read-only conventional configuration space.

H0 intentionally has **no** BAR, address decoder, DMA, IRQ, MSI/MSI-X, reset, power-management capability or matching device driver. It must never use the storage-controller class or allow the native storage driver to bind. The module performs `scan → driver_override=none → add` inside the kernel before the function becomes probe-visible. Configuration writes are acknowledged but cannot enable I/O, memory decoding or bus mastering.

## Build

```sh
make -C kernel/host-pci-h0
```

Build against the running kernel's headers. The resulting module is tied to that kernel ABI and is not a portable artifact.

## Privileged smoke test

Run only on a disposable/nested lab kernel with console recovery:

```sh
sudo tests/privileged/m0_host_bridge_h0.sh \
  kernel/host-pci-h0/ssd_fwlab_host_h0.ko
```

The script verifies identity, class, absence of BARs and driver binding, unload cleanup and absence of new fatal kernel diagnostics. It does not establish BAR/PAT, DMA, IRQ or bare-metal evidence.

## Stop conditions

Do not proceed to a BAR experiment if H0 leaves a PCI device/root bus after unload, binds any driver, exposes a non-zero resource, taints the kernel unexpectedly, or emits a new `BUG`, `WARNING`, `Oops`, KASAN or lockdep report.

The first exact-code run passed under `Profile-Nested`; see the [2026-08-28 result](../../docs/results/2026-08-28-m0-h0-nested.md). That result does not graduate BAR, DMA, IRQ, native-driver or bare-metal gates.
