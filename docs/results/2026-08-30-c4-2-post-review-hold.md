<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C4.2 post-review hold and C4.2a requirement

- Date: 2026-08-30
- Reviewed source: `905a01e9e140a7bda2810db92118f5693b196ac1`
- Reviewed evidence HEAD: `5df035d31deb5f39ed112154b51f897e43136188`
- Disposition: **CRITICAL REMAINS / REVIEW HOLD / C4.2a REQUIRED**

The original C4.2 build, sanitizer, analyzer, model, cross-ISA and repository
suites completed successfully, and their recorded output remains reproducible.
An immutable post-source adversarial review subsequently found counterexamples
that those suites did not cover. Therefore the original PASS disposition is
superseded and C4.3 is blocked.

## Confirmed ownership defects

1. An out-of-range command-port admission state can fall through as committed,
   store an invalid ticket and advance the SQ head without graph ownership.
2. After reset advances the controller epoch, old candidate/control APIs and
   notification acquisition can still mutate or expose old state before global
   provider quiescence.
3. A prepared SQ candidate does not pin its associated CQ generation. CQ delete
   can race candidate commit and leave a live SQ bound to an absent CQ.
4. CQ scrub reaches logical READY without an explicit provider terminal-retire
   handshake. The bundled fake retains the old scrub operation and rejects a
   valid same-QID recreation.

Required corrections also move SQHD sampling to the actual CQ-slot reservation
transaction and prevent a permanently delayed consume-prepare record from
starving unrelated READY commands.

## Evidence corrections

The original bounded model was a narrow abstract model, not a path-by-path DUT
replay. Its 20 broken variants used common artificial setup steps rather than
family-specific production mutations. The eight advertised temporary-source
architecture mutations tested detectors against isolated strings instead of
compile-valid mutations of complete source copies.

C4.2a must use an immutable independent reference, replay every discovered
baseline path against the real DUT/fakes, compile 20 exact temporary
production-source mutants, and run eight complete-source architecture
mutations through the same checker.

## Required remediation boundary

C4.2a will version the component and queue-memory port, add strict provider
result validation, reset/teardown supersede states, exact prepared-SQ
association, scrub retire start/query, reserve-time SQHD and fair per-record
progress. It will publish a new source/evidence chain and update the current
freeze only after full local and public CI success.

This hold does not add protocol commands, PRP/data DMA, storage, NAND, PCI,
BAR/MMIO, interrupts, QEMU/vfio-user or Linux-driver interoperability. It does
not invalidate C4.1 or the frozen Cycle 03 components.
