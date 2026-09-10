<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Scalable storage and serial-credit MQ2: development evidence index

This is a publication of adopted development results, **not a new frozen
release**. The latest existing tag remains `v0.1.0-spine-preview.1`; its
[fixed-profile record](2026-09-05-vertical-spine-preview.md) is unchanged.
No performance experiment was rerun merely to prepare these documents.

## Source identity and retained history

- Adopted source tip: `a6ee009bbca5932857d51c3a5f265e0b60183a76`.
- Source tree: `a5fc616787d91fc9bcb44f1febe25541907eb19f`.
- Prior public baseline: `efe304b09d496e4421f1749ce631eaea9c4afc4b`.
- The 29 intervening commits are preserved, not squashed. Subsequent ADR,
  evidence, guide and CI changes are separate documentation/entry commits.
- The rejected combined-PUMP/STATUS experiment `7a2e258` is **not** an ancestor
  of the selected source. Its negative result is recorded in the performance
  report, not merged as a product feature.

| Adopted checkpoint | Commit | Scope |
|---|---|---|
| Compact physical NAND | `d8ae5e1` | Second physical-media binding, not a logical-file backend |
| Ready-volume descriptor | `6bb918b` | Identify and bounds consume recovered FTL capacity |
| Scalable FTL | `0559c9c` | Resident map, streamed checkpoint/replay, GC; 64/256-MiB full disk cases |
| ARM64 64-GiB campaign | `cec2c5d` | Explicit tmpfs full functional workload and bounded progress |
| Retained Block parent | `21a9e19` | Up to 1 MiB below Block, maintenance at resolved boundaries |
| Physical NAND v2 | `ccc4503` | Reservation-protected direct physical homes and recovery |
| FTL windows / PAGE2 | `1e7c5c7` | Real FTL-to-NFC batches with one aggregate Block result |
| Mapped/exclusive profiles | `777c0d1`, `fc303a2` | Explicit bounded tmpfs and opt-in host validation profile |
| Media/NFC/validator cost reductions | `f27b590`, `6f4b53f`, `8d8a1a1`, `80023c6` | Source-equivalent checks and owned buffer alignment; no format or sync removal |
| Scaled native / PUMP / large profile | `110b607`, `046d7bb`, `0223f77` | Explicit attachment identity and one authoritative executor |
| Two queues, three vectors, truthful progress | `a6ee009` | Serial-credit MQ2, captured queue incarnations and exact deletion retry |

The [current construction matrix](../current-status.md) owns current capability
wording. An older checkpoint's whole-capacity test does not automatically
qualify every later constructor or format at that capacity.

## Executed evidence: do not collapse these scopes

| Evidence | Actual scope and result | Limit |
|---|---|---|
| SCALE-B2 disk full tests | 64/256-MiB fill, interleaved half-volume overwrite, restart/full readback; fixed interruption and active-close cases passed | Exact earlier FTL/media version, not current physical-v2 disk qualification |
| SCALE-B2 tmpfs full tests | Same logical workloads passed; maximum media use 355606528 bytes, process RSS 5852 KiB | Functional/process restart, not persistent disk or host power loss |
| ARM64 64-GiB campaign | At `cec2c5d`: 64-GiB fill, 32-GiB overwrite, 64-GiB recovered read; exit 0 after 11h01m23s, recorded GC count 143447, checkpoints 213, RSS 347116 KiB | Old exact-source functional campaign; not a current MQ2/performance/ARM PCI test |
| Current retained-parent and PAGE2 paths | Named aligned/unaligned/RMW, GC/CP, cancellation, uncertain DATA/MAP tails and recovery checks passed at their recorded checkpoints | Fixed cases, not an unbounded crash matrix |
| MQ2 local userspace | GCC, selected Clang ASan/UBSan, profile/attachment/progress and old-reference regressions passed | Fake Host plus real storage; no kernel IRQ or owner-switch claim from this executable |
| MQ2 native intake | Actual two Linux hardware contexts and three IRQ vectors; Q1 writes/Q2 reads, immediate unsupported AER, Identify, cut-4 reset/rebind and one L1→L2→L1 data journey | Tested native VM and bounded workload only |
| MQ2 per-vector PBA | Mask vector 1: Q1 waits and PBA is 2 while Q2 completes; unmask gives exact Q1 completion and PBA zero | Named routing case, not exhaustive IRQ races |
| MQ2 literal VFIO Host | 55 exact CQEs; full Q1 CQ bypassed for two Q2 reads, eight alternating grants with repeated CIDs; accepted Delete lost-reply/exact-retry, old I/O retires before Delete, new CQ IOVA/reused CID and unchanged old CQ | Same epoch and bounded trace; test-only reply-loss worker is not a performance binary |
| Native performance | One five-pair 128-KiB/QD1 comparison; all ten jobs and twenty before/after data checks passed | Warm 1-MiB extent, 64-MiB bytes per sample, not sustained full-device or multi-queue parallel throughput |

The timing and sample data are in the [performance report](2026-09-10-throughput.md).
Do not call its ARM physical-media/NFC measurements native NVMe bandwidth.

## Exact archived identities

These hashes identify privately retained evidence, not downloadable public
attachments or independent reproduction. Infrastructure logs, images, account
details and raw model transcripts are deliberately not published.

| Artifact | SHA-256 |
|---|---|
| MQ2 29-path source manifest | `bfd181a7db65c6fa58931ba1aa774cc5b4c9ede93a142e91a107babe1c02fff5` |
| MQ2 native intake archive | `70c3759af8fc5ef744d5ec14ec561ddf166eaba117dc6c83f6c86ade88a1fd6a` |
| MQ2 vector/PBA archive | `75624da36b208139946b19de82353f9b484fb358933d56df628d3da45378dfce` |
| MQ2 literal Host archive | `db2a7206eda461f285a0368fd08fbbae2624a7652999c67267bed9b8813b08a7` |
| MQ2 paired-cost archive | `faf393136fd719fef2b092ae24c453b250d5f8613bda890e67f287621e8d4658` |
| MQ2 cost summary | `2713a734e8cf55e148cbf18023307c95c70932bebd30f7b5df532f09fbce7643` |
| Native intake firmware ELF | `23b584c56c2b960e44d9af42c15981276a721eadb35b327ed504e5450d213014` |
| Native PCI module | `7be36cfdcf95ba2066eb33de43362e7cff56a3b7acadaf8a8461db7246ac4eea` |
| Native IOMMU module | `403df069fb3ccd4371a2029713ec200eb1f79f241a4932d5bf5dbd8218605a22` |
| ARM64 64-GiB source manifest | `6b649d8db07818d2e26086356fa904f0d98bb176f31d612ca753a62006858406` |
| ARM64 64-GiB executable | `9ba12df8143400da43f219c0c5656c541da1b437b69490e48b920421beb74f6c` |

## Review and publication boundary

Publication-entry checks were executed at `284cd87` in an isolated no-hardlink
checkout as an ordinary Linux user. The expanded GCC current-spine entry exited
0 in 2m37.68s with maximum process RSS 99096 KiB; it included the existing
reference checks and selected real scaled/PAGE2/MQ2 software fixtures. The
new ARM64 PAGE2/physical-media fixture commands also exited 0 under QEMU user
mode. Neither ran native PCI or repeated the completed full-capacity campaigns.
Later publication commits change documentation/metadata only, not the tested
C sources or this entry. Policy, relative links, SPDX and REUSE checks are
reported separately from runtime evidence.

The MQ2 checkpoint received one independent source-review lane in two finite
scopes, then a file-based ChatGPT Pro cadence review of the supplied production
diff with no required corrections. That is **not two independent reviewers**,
an independent test rerun, human maintainer verification or whole-repository
release approval. Historical completed reviews are not reopened without a
related source change or reachable defect.

This documentation/CI preparation does not mark a new tag frozen. Hosted CI
results must be read at the actual publication commit; a workflow file alone
does not establish that it ran. P01 physical-generation ownership and N01
concurrent FLR remain disclosed. Raw block, larger native capacities, full
NVMe, RTOS/silicon ports and parallel FTL are later work, not hidden graduation
conditions for publishing this development history.

AI-assisted: yes. This report was assembled with Codex from project source,
existing test outputs and prior review records in September 2026. Maintainer:
Evanshenf. No additional human source-review attestation is inferred from the
documentation preparation or the permission to publish.
