<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0009: Upstream VFIO route and M2/M4/M5 milestones

- Status: Accepted route; implementations remain experimental
- Date: 2026-09-01
- Supersedes: the v0.x-required `vfio-user` clause and project-owned custom-VFIO implementation clause in ADR-0001
- Preserves: ADR-0001 owner revocation, zero-reference, destructive-reset, quarantine and graduation requirements
- Depends on and preserves: ADR-0003, ADR-0004 and ADR-0008

## Context

The project needs one synthetic PCI function that a Host native driver can use
and that, after a destructive exclusive transition, QEMU can assign to a Guest.
The earlier baseline made `vfio-user` a v0.x prerequisite and assumed that M5
would implement a project VFIO device ABI. An internal bounded nested spike
provided functional-feasibility input that a synthetic endpoint can enter the
upstream assignment stack. It is not published by this ADR, contributes no
milestone-gate credit and did not establish a complete interrupt/PBA model,
owner-epoch fence, defined full BAR aperture, portable firmware path,
stale-authority rejection or bare-metal profile.

## Decision

### M2 is optional

`vfio-user` leaves the critical path and v0.x release promise. It remains an
optional, deferred Guest adapter and differential oracle. If implemented, it
must consume the same portable contracts as the Host path and must not own a
second protocol, command graph, FTL or media implementation.

### M4 and M5 share one endpoint, not one claim

M4 and M5 form one Host-assignable transport engineering epic:

```text
                         Host native nvme owner
                                  │
                                  ▼
portable firmware ← trusted HIF ← synthetic PCI function
                                  │
                                  ▼
                         upstream vfio-pci
                         VFIO cdev + IOMMUFD
                                  │
                                  ▼
                                QEMU
                                  │
                                  ▼
                         Guest native nvme owner
```

The project-owned side must implement the synthetic endpoint, deterministic
IOMMU association, trusted HIF, DMA/IRQ/reset mechanisms and owner-lifecycle
fences. Linux and QEMU provide `vfio-pci`, VFIO cdev, IOMMUFD/IOAS and
assignment. QEMU does not implement this project's NVMe protocol, FTL, NFC or
media path.

- M4 graduates Host-native operation only after the native driver executes the
  portable firmware and future scalable 512-byte-LBA block/NFC/media provider
  (`M3-P`) path on a supported bare-metal profile.
- M5 starts from a graduated M4 and adds exclusive Host-to-Guest-to-Host
  assignment through the upstream stack. M5 graduates only after P7 proves
  stale-authority rejection on bare metal.

Nested profiles are mechanism admission and fault-injection environments. They
cannot graduate bare-metal BAR containment, Host mapping/revocation,
interrupt-remapping or owner-switch claims. Real PCIe requester DMA remains an
M6 endpoint claim, not an M4 synthetic-function claim.

### Owner switching remains destructive and exclusive

Every transition atomically closes admission/enqueue, enters `QUIESCING` or
`NO_OWNER` and advances `owner_epoch`, making old effect authority non-current.
Reset-begin independently advances `controller_epoch`; unmap and queue teardown
retire their mapping/ring generations. The transition then masks sources,
cancels and drains old work, revokes mappings, synchronizes completion/IRQ work,
proves old references zero, clears interrupt routes/pending/PBA and performs a
destructive reset. Only then may it install and publish a fresh owner/route.
Making old authority non-current and publishing the new owner are distinct
linearization points. Failure to prove the fence enters `QUARANTINED`.

Upstream VFIO does not manufacture these project-local facts. In particular,
driver unbind, `pci_clear_master()`, IOAS detach or eventfd close does not by
itself prove that project work, completion leases or interrupt work are gone.

## Milestone gates

| Gate | Meaning | Claim after PASS |
|---|---|---|
| M4-N | bounded nested Host-native admission | exact `Profile-Nested` experiment only |
| M4-B | portable data path plus bare-metal Host-native evidence | M4 on the published support profile |
| M5-N | bounded nested upstream assignment plus P7 canaries | exact `Profile-Nested` assignment experiment only |
| M5-B | M4 plus bare-metal P7 and owner-cycle fault injection | M5 on the published support profile |

P7 deliberately reuses IOVA, QID, CID, CQ slots and interrupt routes across
owners. Any effectful old-token DMA, CQE, IRQ or media use must synchronously
reach a bounded terminal `STALE`, `REVOKED` or `SUPERSEDED` outcome while
new-owner data, CQ state, event counters, PBA and media state remain unchanged.
Idempotent cleanup may only release old resources and reduce the old-owner
ledger; it must not use a reused IOVA/QID/CID/vector to touch a new object.
Repetition/soak supplements but does not replace this matrix.

Stable graduation continues to require the ADR-0001 support matrix and two
release cycles of relevant correctness/security evidence.

## Profile and evidence separation

The fixed Cycle 04 software-oracle design profile remains unchanged. C4.2a
bytes, claims and obligation denominator are sealed; C4.3–C4.5 remain pending
under that fixed design. Linux queue depth, Admin-command subset, PRP-list, BAR,
DMA, MSI-X, VFIO and P7 requirements belong to a separately versioned
`Linux-profile-v1` and separate claim/evidence system. New adapter requirements
are not by themselves grounds to reopen C4.2a.

## Consequences

- The project avoids a security-sensitive custom VFIO ABI and permanent QEMU
  fork.
- Host-native and Guest-assignment mechanisms reuse one endpoint and HIF.
- M2 no longer blocks the main route, but remains available as a differential
  adapter.
- M5 implementation work is smaller than the earlier custom-VFIO plan, while
  owner-safety evidence remains substantial.
- Historical custom-VFIO experiments remain valid only for their exact stated
  contracts; they are not the current implementation route.

## Stop conditions

Stop or quarantine the current implementation on stale DMA/CQE/IRQ, an
untracked mapping or pin, an undefined or unsanitized BAR range, ambiguous
device/media identity, a second uncontrolled IOMMU provider, a need to disable
the system IOMMU, or any transition that cannot prove old references zero.

This ADR accepts a route. It does not assert that M4, M5, P7, portable
integration or any bare-metal profile has passed.

## Public history and publication gate

Any private feasibility branch based on a superseded main revision is a donor
archive only. It must not be pushed directly, force-rewritten or presented as a
public milestone branch. A public candidate must start from the then-current
public main, preserve the donor commit/archive identity in a provenance
manifest and transplant only reviewed paths.

Before a transplant branch is opened for public review it must, at minimum:

1. correct raw-SQE/canonical authority and all milestone/non-claim wording;
2. close stale DMA, mapping-reference, completion-publication and IRQ-work
   owner fences, or compile the dangerous privileged path out of the candidate;
3. define and reset/scrub every exposed BAR byte, or reduce the BAR to the
   implemented aperture;
4. make privileged Host/L2/media scripts default-safe and fail closed on any
   ambiguous device, namespace, owner, mount, holder or protected-media
   identity;
5. include public author/committer identity, DCO/SPDX, external-source
   provenance, AI-assisted/human verification and exact source/evidence
   manifests;
6. publish only the exact nested profile and explicit non-claims.

Such a branch may be public for source review before cross-platform stability,
bare-metal evidence or M4/M5 graduation. The `experimental` label does not
waive Host-corruption, confidentiality, stale-owner or provenance blockers.
Merging an isolated mechanism fixture into main is a later review decision and
does not itself graduate M4 or M5.
