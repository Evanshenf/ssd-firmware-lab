<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Transport and harness adapters

| Role | Directory / source | What it supplies |
|---|---|---|
| Current native adapter | [linux-m4](linux-m4/README.md) | Worker loop, Host data mover and owner-control binding; explicit legacy/scaled/Large/MQ2 builds |
| Shared production composition | [headless-j0](headless-j0/j0_construction.c), especially `j0_construction.c`, `j0_action_drivers.c`, `j0_controller_buffer.c` | Real profile/lifecycle/Block binding and controller buffers; also linked into native workers |
| Reference memory Host binding | [headless-j0/j0_host_data.c](headless-j0/j0_host_data.c) | Headless Host-byte/authority implementation; native Host transfers select `linux-m4/native_host.c` instead |
| Shared production storage factory | [headless-scale/scale_storage.c](headless-scale/scale_storage.c) | Explicit scalable FTL + C3 or PAGE2 construction; also linked into scaled native workers |
| Adjacent current tests | `headless-j0/tests`, `headless-scale/test_*.c`, `linux-m4/tests` | Headless/software-boundary drivers; their fake Host is not native DMA/IRQ evidence |
| Historical integrated reference | [headless-c35](headless-c35/README.md) | Reviewed fixed C3 lifecycle/persistence/NFC/mapping composition |
| Historical protocol reference | [headless-c4](headless-c4/README.md) | Bounded C4/C43 oracle, not the native command executor |

The `headless-*` names describe origins, not a test-only source boundary.
Production and test files coexist there; authoritative Makefile source lists
and explicit constructors determine what executes. See the
[source map](../docs/source-map.md) before moving or copying shared code.

`vfio-user` remains an optional future differential adapter. The implemented
Host/Guest ownership route uses one synthetic PCI/HIF function under
`kernel/m4-native` with **upstream** `vfio-pci`, IOMMUFD and QEMU, not a custom
VFIO replacement. Historical early VFIO fixtures are separate evidence and
are not the current route. A real endpoint remains a future platform.

Adapters translate transport mechanics into the same canonical command/capability ABI. They do not own protocol/FTL policy.
