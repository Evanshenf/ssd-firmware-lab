<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3.5 integrated headless firmware graduation

This directory composes the frozen C3.1 command lifecycle, C3.2 persistence
policy, C3.3 programmable NAND controller model and C3.4 crash-consistent
mapping/file media behind one transport-free headless interface. It adds no
NVMe command, PCI function, BAR, DMA engine, interrupt or QEMU device.

The headless interface owns command admission, completion publication, reset
ordering and teardown. It does not own an L2P table, NAND state or physical
file format. A private binding maps a C3.5 semantic request either to the
lifecycle-only scripted provider or to the frozen C3.4 command graph.

```text
headless HIF
  -> frozen C3.1 lifecycle + fake DMA
       -> S: scripted lifecycle-only binding
       -> frozen C3.4 mapping coordinator
            -> frozen C3.2 persistence witness
            -> frozen C3.3 NFC model
            -> coherent media/physical-receipt bundle
                 -> M: memory media
                 -> B: 64-KiB byte-image file engine
                 -> P: anonymous POSIX regular fd
```

## Evidence lanes

| Lane | Link contents | Comparable evidence |
| --- | --- | --- |
| S | C3.1 plus scripted lifecycle binding | Lifecycle only |
| M | C3.4, C3.3 and direct memory media | Lifecycle, semantic result and raw NAND projection |
| B | C3.4, C3.3 and byte-image file engine | Lifecycle, semantic/raw and deterministic container |
| P | C3.4, C3.3 and anonymous-fd adapter | Lifecycle, semantic/raw, container and target Linux ABI |

The comparisons are deliberately tiered:

```text
E_life:      S == M == B == P
E_sem/raw:       M == B == P
E_container:         B == P
```

S is not a storage provider and has no durability or restart claim. M has no
file-container claim. The full C3.4 stack has one fixed geometry: one channel,
one LUN, one plane, six blocks, four pages per block, and 96+64 bytes per page.
A separate test runs two genuine C3.3 geometries, but that is not a claim that
C3.4 supports multiple FTL profiles.

## Ownership and ordering

The generic frontend permanently enforces these transactions:

```text
submit -> register binding -> first step

completion acquire -> copy sidecar -> encode canonical record
  -> consume C3.1 lease -> acknowledge binding result

close admission -> begin reset -> drain to RESET_ACK
  -> recover binding while closed -> prove quiescent -> ACK -> reopen

close admission -> teardown drain/ACK -> prove bundle quiescent
  -> release bundle and exact fd/arenas
```

The raw-media provider and physical-receipt provider are an indivisible bundle
with one live claimant. Cross-instance request/command/lease identities are
matched in full; slot-only or UID-only matches are rejected.

Fake DMA capture and the later semantic media request are two separate
transport-neutral test commands. They are not presented as a completed
single-command hardware DMA-to-FTL path.

## Build and test

From this directory:

```sh
make check-gcc
make check-clang
make check-sanitize
make check-thread
make check-analysis
make check-architecture
make check-determinism
make check-cross
make check-all
```

`libfwlab_firmware_c35.a` is built once per compiler/ABI from an exact source
list. C3.3, media providers, bindings, the headless adapter and tests stay
outside that archive. Four separate `c35_lane_{s,m,b,p}` links consume the same
archive. The architecture gate checks archive members/dependencies/symbols,
lane syscall containment, link maps and full byte projections before reporting
hashes.

The finite evidence includes:

- 14 directed S/M/B/P graduation and malformed-contract cases;
- M/B reset at every measured C3.1 unit cut, plus lease and sidecar-ACK cuts;
- full-handle, generation, epoch and cross-instance stale probes;
- 13 closed composition families and 16 shortest broken counterexamples;
- all 5,544 six-by-six macro schedules and 20,586 distinct prefixes, with 36
  selected schedules also executed by two live firmware instances;
- 192 barrier-started pthread twin-instance runs over MM, BB and MB;
- standalone dual-geometry C3.3 isolation and explicit non-profile C3.4
  rejection;
- native GCC/Clang, ASan+UBSan, Clang TSan, two static analyzers, and exact
  native/AArch64/RISC-V/s390x byte comparison. The s390x executable is verified
  as big-endian before execution.

The macro scheduler exhausts the frozen six-by-six composition grammar, not
arbitrary C call/thread schedules. The pthread test proves independent-instance
isolation only; all calls on one firmware instance remain caller-serialized.
The user-mode P lane exercises a target Linux libc/syscall ABI on the same Host
kernel and filesystem. It does not prove a QEMU NVMe device, filesystem-
independent durability, physical power loss or real NAND behavior.

Tests use only caller-owned memory and newly created, immediately unlinked
regular files. No raw block device, privileged lab host, KVM or physical SSD is
used by this gate.
