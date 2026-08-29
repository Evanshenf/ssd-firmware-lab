<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Cycle 02 sanitized evidence manifest and review disposition

- Date: 2026-08-29
- Cycle 02 result: five narrow sub-gates passed
- Web architecture-review disposition: `APPROVE_WITH_CHANGES`
- Public C2.5 result/freeze commit: `ac33119a6443e21b5ee5d6000ac5b7c0cf5c229c`
- Evidence profile for privileged results: `Profile-Nested`

This manifest is the public, content-addressed Cycle 02 handoff. It distinguishes
source-confirmed facts from project-recorded runtime results and records known
evidence gaps rather than rerunning frozen privileged experiments to manufacture
new logs.

The raw web-review transcript is private working material. Its recommendations
were checked against the public source, immutable result records and the exact
test outputs that remain available.

## Canonical terminology correction

The only accepted pinning statement for V1 is:

> No project-driver-retained VFIO pin lease, no project-visible pinned-page
> handle, and no retained IOVA translation or Host pointer.

Historical frozen comments or records that use shorthand such as `no pin` or
`no pinning` mean that the project driver does not call a pin API or retain such
a resource. They do **not** claim that the tested kernel's synchronous
`vfio_dma_rw()` implementation never acquires a page transiently. Frozen source
and old result records are not rewritten; this manifest is the governing
clarification.

## Exact kernel and toolchain tuple

```text
running release: 7.0.0-30-generic
kernel image/headers package: 7.0.0-30.30
Ubuntu source tag: Ubuntu-7.0.0-30.30
Ubuntu source commit: d974a4063f5c03c13b4f241a9ab511750e0b9f12
module compiler package: gcc-15 15.2.0-16ubuntu1
module compiler: gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
module vermagic: 7.0.0-30-generic SMP preempt mod_unload modversions
kernel config SHA-256: b07d3cb0d53236b021d73038e315018801fa6b843529d53129ad94a2a5233bf6
Module.symvers SHA-256: 88ec24bc876cce4c2d7947424f964e5b2a76011801288209bd96539647bee4ba
vfio_dma_rw modversion: 0xaa22e02a
```

The source revision, config and `Module.symvers` tuple was frozen in the Cycle
01 evidence manifest and reused by C2.2–C2.5.

## Gate-by-gate identity and evidence availability

| Gate | Implementation/result identity | Runtime evidence available publicly |
|---|---|---|
| C2.1 | implementation `3ca449f017cba1a9cd9dbbe3ef1e2c23a4fd16f8`; result `499d7e117e494ce93d6f456b20f29aade7d0b8d9` | Complete unprivileged source/tests and recorded 9-test, sanitizer, TSan and 10k-fuzz outcomes |
| C2.2 | implementation `95e7a052ffc8320d13b1ec23ea82f0de21afe830`; result `84696ca42e8e1c4fe5ffd3eb698850fd258846a4` | Sanitized result matrix and filtered-journal hash; original privileged stdout was not retained as a separate public artifact |
| C2.3 | oracle `bf99d04ba9d1670a382d9a985fe6a47a7f494504`; result `6e8aeed1bd7e3aeaf1d435cd87713eb0a67ef5e4` | Sanitized negative/side-effect matrix and filtered-journal hash; original privileged stdout was not retained as a separate public artifact |
| C2.4 | oracle `eb5f351a2fc39e7a59c348d38fbc0e037a73d7ec`; result `823322c9f816481a63838ed7362936f7584feed9` | Sanitized lifecycle/race matrix, exact observed distribution and filtered-journal hash; original privileged stdout was not retained as a separate public artifact |
| C2.5 | final implementation `e3e518e15c5eb600d6e1f757deb214096d907bbb`; result/freeze `ac33119a6443e21b5ee5d6000ac5b7c0cf5c229c` | Sanitized stdout/dmesg transcript below, artifact hashes, two clean-boot identities and independent H0 transcript |

Retained filtered-journal identities:

```text
C2.2: 913d7c445a04f1312c19c22d45f76d41ffa58e39f5af25a435d956ed50338438
C2.3: e1531ee600a2e2f000b33fb0646c65fb32c2e199c8baf9c204a64db21f386ea7
C2.4: 1810152db5613d003ac25be2c15d16bf324a3039eaf2cb6b0439fa07ea191282
```

These hashes identify retained internal journals but do not make their contents
publicly inspectable. The corresponding committed result documents remain
project-recorded evidence, not an independent reproduction.

## C2.2–C2.5 artifact identity

```text
frozen V1 module:
SHA-256 8c9f1418dcdce7e0ebbd18d97adcc9e11a7a9f7e2bfe5b565f18f8db8714b072
srcversion FFE4E0F87FA9FA275C67192

C2.2 userspace oracle:
SHA-256 430583501e1cc13950e6c1b247f233698c15d9b0978b9010395f901df0cf3872
C2.2 privileged runner:
SHA-256 6636cc8f9211b43a9698d501ecb0f369cb8d84b3050f7614217109fb1b271931

C2.3 GCC15 oracle:
SHA-256 70f53eb01b23e2db2ef14e0ada1e99474e1a5abdb63b1833c4a74fdcee3afdf5
C2.3 privileged runner:
SHA-256 21dabcc8dedd27b3c8a8687ea90babb0f25523f5a842ed0088e42380b0ee0f98

C2.4 GCC15 oracle:
SHA-256 ddd13708e5f7486fb539ebc493272a2b777fd1c937acec985f441aec2a2f9800
C2.4 privileged runner:
SHA-256 12bc26d54cfc0b203df47aa5480b603a70b17c16d5addb36af4c5c201f28d2db

C2.5 peer fixture module:
SHA-256 514cb0cdb31c855b581927f4fa3fb16e2bf777c731853866e4892ce839d0c7df
srcversion 2AEB33A9E7688A0E2131FF0
C2.5 userspace oracle:
SHA-256 b21da6de6dea77488377c6389bdd19a2efd29cdb321b620adef56d27c8a26ae5
C2.5 privileged runner:
SHA-256 612892b74ede04906a1fea7d7ba90871b44546de483bdcee8acb8a4834edf310
```

The C2.5 exact commit was expanded into a root-only source directory and all
113 tracked files were checked byte for byte. V1, the peer fixture and H0
independently passed `W=1` build and modpost. The peer fixture passed strict
checkpatch, declared no module dependency and had only Linux platform/device
undefined symbols. GCC/Clang, ASan/UBSan, TSan, static analyzers, shell syntax,
ShellCheck, the repository checker and hosted CI passed where applicable.

The C2.5 runner recorded and compared the peer artifact hash before and after
load, but did not contain that hash as a compile-time constant. Any future
reproduction must validate the peer hash above before invoking the frozen
runner; the completed run is not repeated merely to change this P2 hardening.

## Sanitized C2.5 execution transcript

```text
artifact identity: exact GA release/vermagic, frozen V1 hash/srcversion/CRC,
peer hash/srcversion, tool hash and runner hash matched
pure wire/layout/observation selftest: PASS
unsafe no-IOMMU mode: disabled
base cdev and peer cdev: distinct
one iommufd owner, distinct IOAS IDs 2/4, same IOVA 0x100000000: PASS
distinct full-page/data patterns at equal IOVA: PASS
bidirectional data/sequence isolation: PASS
base actor / peer observer reset-unmap-detach isolation: PASS
peer actor / base observer reset-unmap-detach isolation: PASS
bounded parallel two-cdev copy: PASS (24 rounds each)
peer close and IOAS destroy with base survivor: PASS
two-instance real-provider oracle and ordered teardown: PASS
peer removed while base remained live: PASS
new base copy after peer remove: PASS
peer then V1 normal unload: PASS
taint before/after: 0/12288 (only expected out-of-tree/unsigned bits)
raw-media write counters before/after: 0:0 / 0:0
```

The bounded dmesg delta contained only VFIO initialization, expected unsigned
out-of-tree taint messages, two IOMMU-group adds, two V1 registrations, peer
fixture registration/removal and two IOMMU-group removals. It contained no
project lifecycle warning, BUG, kernel WARNING, Oops, sanitizer, lockdep,
refcount, use-after-free, hung-task, lockup or RCU-stall diagnostic.

```text
C2.5 sanitized run-log SHA-256:
dbed14bfd0284437c6dd7220a47d1ff32c2ecbff90c8047614c8ad2c41a53d95
```

The runner resolved each cdev from its platform sysfs path, required different
character-device identities and compared open fds by `dev_t`. The numeric
`dev_t` values were not printed into the retained log. This is an explicit
reproduction-evidence gap; values are not reconstructed after teardown.

## Independent clean-boot H0 transcript

After a normal reboot restored taint zero, and with V1/peer absent for the
entire run, the byte-identical frozen H0 module produced:

```text
one vendor-class function fffa:0001, revision 01
I/O decode disabled
memory decode disabled
bus mastering disabled
no PCI resource window
registered one synthetic domain/function with no BAR/DMA/IRQ
normal removal: PASS
taint before/after: 0/12288
raw-media write counters before/after: 0:0 / 0:0
```

```text
frozen H0 module SHA-256:
039019933f66cb25d4b7bc025e599a0132c0f3df2ad45d9956fab0c507af6418
H0 privileged runner SHA-256:
547c9f227b121c36dd9c6aab3d0d5996e62c24cb121f9fd4f7a9246e6a3275ea
H0 clean-boot run-log SHA-256:
0eda7493c0d15e49cd04bcf8db240bb5f76a151812e51f4ca827c3d7f032757f
```

A second normal reboot again produced taint zero, a healthy system and no
project module, platform object, VFIO cdev or H0 PCI function. This proves only
that H0 ran alone on the recorded clean boot and that current H0/V1 source/build
dependencies remain separate. There is no portable implementation whose
runtime or link isolation could be graduated.

## Signal and cleanup boundary

The final C2.5 runner shares the frozen V1 gate lock and records
HUP/INT/QUIT/TERM as pending while foreground module load completes. It commits
ownership from the load result before executing deferred cleanup. Synthetic
`dash` and `bash` signal-window tests passed, and the static checker requires
this ordering.

This is not broad crash safety. SIGKILL, host power loss or kernel failure can
prevent shell cleanup. A later gate must detect residue before load, and the
recorded successful run still relies on normal module and process teardown.

## Review disposition and next boundary

The reviewed disposition is `APPROVE_WITH_CHANGES` for one narrow graduation:
the frozen, kernel-profile-specific, synchronous CPU-mediated IOAS-copy
mechanism and its bounded two-instance evidence may be archived as complete.
V1 must not become a container for IRQ, BAR/PAT, PCI, QEMU or NVMe work.

The recommended next cycle begins with portable, headless work rather than more
Host/VFIO plumbing:

1. portable HIF/command-lifecycle core with fake providers;
2. executable persistence lattice;
3. programmable NFC/NAND model;
4. minimal crash-consistent mapping and file-backed media prototype;
5. integrated headless portability/provider-replaceability graduation.

Before implementation, separate ADRs must freeze the portable command,
capability, completion, reset-epoch, abort and status contracts, and the
persistence policy for volatile/durable success, flush/FUA, atomicity and
recovery.

Driver-retained pinning/zero-copy, device DMA, IRQ, BAR/PAT, native binding,
PCI, QEMU, NVMe protocol, raw media, real NAND/FPGA and Isolated-B* containment
remain STOP.
