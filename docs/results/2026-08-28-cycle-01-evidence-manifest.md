<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Cycle 01 evidence manifest — Profile-Nested

- Cycle date: 2026-08-28
- Evidence profile: `Profile-Nested`
- Public source commit: `0f8ece41c863b19ddc28d8dea52e67a1905e7425`
- Public root tree: `edb939b44a23c52bf5f219bb308d978f92ca41f2`
- Review verdict: `APPROVE_WITH_CHANGES`
- V1 load status: `STOP`

This manifest binds the Cycle 01 H0 and V0 source, tests, result records and locally tested artifacts to one immutable public commit. Generated binaries are not distributed; their digests identify the exact locally tested builds. This evidence is limited to nested functional tests and does not graduate bare-metal, BAR/PAT, DMA, IRQ, PCI composition, QEMU assignment or storage-protocol claims.

## Frozen public inputs and results

All paths below are relative to the repository root at the public source commit. SHA-256 values were independently recalculated from the frozen public files. The source, build-recipe and test inputs match the retained laboratory copies; the result-record digests bind the later sanitized records in the immutable public commit.

### H0 synthetic PCI enumeration

| Item | Path | SHA-256 |
|---|---|---|
| Kernel source | `kernel/host-pci-h0/ssd_fwlab_host_h0.c` | `f282c72422f2aa4e9c501787a6485a7794917d76f8f6e53cec351027685288fc` |
| Kernel build recipe | `kernel/host-pci-h0/Makefile` | `7e1301e467cf2f44df0e7add77f6e5c142f92ffd26c1eaaa1cb2f303f45e9a5f` |
| Privileged test | `tests/privileged/m0_host_bridge_h0.sh` | `547c9f227b121c36dd9c6aab3d0d5996e62c24cb121f9fd4f7a9246e6a3275ea` |
| Public result | `docs/results/2026-08-28-m0-h0-nested.md` | `4f4e4cd0b0c5762dd480a818c78bd75a14da9f9f7a39aa2a29abefa1fa8c1309` |

The H0 files were introduced by the parent commit and are byte-for-byte unchanged at the Cycle 01 public source commit.

### V0 emulated VFIO cdev contract

| Item | Path | SHA-256 |
|---|---|---|
| Kernel source | `kernel/vfio-cdev-v0/ssd_fwlab_vfio_v0.c` | `a91e0c3d8ca209691bc679d8ff59d62ef2e6bad36c0248b188ea1e4ff2201a2b` |
| Kernel build recipe | `kernel/vfio-cdev-v0/Makefile` | `87be2cf0d5f4fda348f48cbb4184570b646d5b303c5cfba8ed2c59e41e504140` |
| Userspace source | `tools/vfio-cdev-v0/vfio_cdev_v0_smoke.c` | `6f9882b09ec5dba783982199acf16e25f257b5139a31e8b39315d0734d1299fd` |
| Userspace build recipe | `tools/vfio-cdev-v0/Makefile` | `b7cd64e80de107cc28b844bdb955d41e3b0e807517ea03506056cede678fabac` |
| Privileged test | `tests/privileged/m0_vfio_cdev_v0.sh` | `0d1cfcb405e48d2da01c1a00a69044cfd2eed7c1729724173937b5cce7d1bf70` |
| Public result | `docs/results/2026-08-28-m0-vfio-cdev-v0-nested.md` | `e264b3401ae600e8aba7dae76a037878ff94e0247a19acdfe7146b4c07dbb96e` |

## Exact kernel and build tuple

```text
distribution: Ubuntu 26.04 LTS
running release: 7.0.0-30-generic
kernel image/headers package: 7.0.0-30.30
Ubuntu source tag: Ubuntu-7.0.0-30.30
Ubuntu source commit: d974a4063f5c03c13b4f241a9ab511750e0b9f12
module compiler package: gcc-15 15.2.0-16ubuntu1
module compiler: gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
module vermagic: 7.0.0-30-generic SMP preempt mod_unload modversions
kernel config SHA-256: b07d3cb0d53236b021d73038e315018801fa6b843529d53129ad94a2a5233bf6
Module.symvers SHA-256: 88ec24bc876cce4c2d7947424f964e5b2a76011801288209bd96539647bee4ba
```

The recorded configuration enables `CONFIG_MODVERSIONS`, `CONFIG_PCI_DOMAINS`, `CONFIG_VFIO`, `CONFIG_VFIO_DEVICE_CDEV`, `CONFIG_IOMMUFD` and `CONFIG_PCI_ENDPOINT`. H0 and V0 were built with the exact running-kernel headers and passed their recorded warning, modpost and policy gates.

The installed ABI records the future V1 provider as:

```text
CRC: 0xaa22e02a
symbol: vfio_dma_rw
provider: drivers/vfio/vfio
export: EXPORT_SYMBOL
```

This proves that the installed kernel package exports `vfio_dma_rw`; it does not yet prove that a project module successfully links it.

## Tested artifact identity

| Artifact | SHA-256 | `srcversion` |
|---|---|---|
| H0 kernel module | `039019933f66cb25d4b7bc025e599a0132c0f3df2ad45d9956fab0c507af6418` | `FE6B104A9C2C552E9F0AD5A` |
| V0 kernel module | `aa5c65f15910ea20aaf0a3f313c18e06b16605b42f152ecd2af20808aa21eeba` | `755BDD519F5679749E9F0ED` |
| V0 userspace smoke | `186e5d925212b7ff3a60d9513cfbb0470e9716f07d9a54e6729c1c996fc7041e` | not applicable |

Both modules reported the vermagic in the exact kernel tuple above. Their embedded modversion dependencies matched the installed `Module.symvers`, including the H0 PCI-host APIs and the V0 emulated-VFIO bind, attach, detach, registration and unregistration APIs.

## Result and cleanup evidence

The public H0 and V0 result records are the authoritative sanitized result summaries. Retained kernel journals independently corroborate the kernel-side load, publication and removal sequences. The original privileged-script standard output was not preserved as a separate immutable artifact, so this manifest does not claim that a complete raw console transcript can be reconstructed.

The post-cycle recovery check observed:

- kernel taint value `0` after a normal reboot;
- neither project module loaded;
- no H0 root, PCI function or bus residue;
- no V0 platform device or VFIO cdev residue;
- `/dev/sdb`, the dedicated future media device, remained kernel read-only (`RO=1`) and unmounted;
- the operating system reported a normal running state.

The result records and retained journals are evidence for the narrow H0/V0 claims only. They do not establish that private media contents were cryptographically measured, nor do they replace a future raw-transcript requirement for higher-risk experiments.

## Review disposition and accepted changes

The independent Pro consultation returned `APPROVE_WITH_CHANGES`: the stable architecture and V1 mechanism question remain valid, but the first V1 module load is not approved until all applicable P0 gates are closed. No raw model answer or private consultation material is part of the public evidence set.

The project accepts the following Cycle 01 recommendations:

1. Use a strictly test-only, non-mappable device-specific control region, called A-prime (`A′`), rather than a project-private `VFIO_DEVICE_FEATURE` index.
2. Treat `vfio_dma_rw` as synchronous CPU-mediated IOAS copy with no driver-retained VFIO pin lease, not as real device DMA or an absolute claim that the kernel never transiently pins memory.
3. Treat an error as potentially having partial side effects. Do not publish a supposedly truthful completed-byte count on an error when the provider does not supply one.
4. Define explicit request/result states and locking across copy, reset, close, attach/replace and owner-side lifecycle serialization before load.
5. Add an injected fake copy-provider seam and run the changed state machine without real iommufd before enabling the real provider.
6. Keep V1 independent of H0, firmware, NFC, media, IRQ, BAR, PCI, QEMU and the final portable HIF, and retain the `Profile-Nested` evidence label.

## Open blocker before V1 load

No H0 or V0 project module references `vfio_dma_rw`. Therefore the installed export evidence above is not a successful project-module link test. Before the first V1 load, a GPL V1 or dedicated link-probe module must:

1. compile against the exact kernel tuple in this manifest;
2. complete modpost without unresolved symbols or namespace errors;
3. contain a `vfio_dma_rw` modversion dependency with CRC `0xaa22e02a`;
4. record its immutable source, build and artifact identities.

Until that compile/modpost evidence and the remaining accepted P0 design gates are recorded, the first V1 load remains `STOP`.
