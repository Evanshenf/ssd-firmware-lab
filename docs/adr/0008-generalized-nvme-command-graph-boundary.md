<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0008: Generalized NVMe command-graph boundary

- Status: Accepted semantic baseline; C4.1 wire/profile subset implemented
- Date: 2026-08-30

## Context

The reviewed Cycle 03 core intentionally contains no storage protocol. C31
owns a command with zero or one provider operation. C35 composes that fixed
lifecycle with a two-atom, 16-byte-per-atom storage profile; its DMA fixture and
semantic storage request are separate test operations.

An NVMe Read or Write needs a multi-action dependency graph: protocol policy,
data-shape resolution, DMA, storage/durability work and physical completion
publication. Submitting such a command to C31 as a provider-free command is
unsafe: Abort or reset can terminalize C31 while unobserved outer actions still
own resources. Reinterpreting the frozen DMA or NFC provider kind would hide
the same ownership error.

## Decision

Cycle 04 has three distinct authorities:

1. the headless memory-transport HIF retains the raw command, queue identity,
   address graph and physical completion placement;
2. portable, address-free NVMe policy owns protocol legality, exact transfer
   shape and status/result;
3. the versioned `c4_command_graph_v1` is the sole outer command-lifecycle
   authority for multi-action admission, cancellation, reset drain, immutable
   completion intent and its one-use lease.

C31 and C35 remain unchanged regression references. They are not linked into
the Cycle 04 runtime and their historical evidence is not reinterpreted.

Each typed action port transfers ownership only on explicit acceptance, later
returns exactly one terminal event through polling, and supports idempotent
cancel plus quiescence proof. Admission reserves every action record, terminal
intent, completion lease and finalizer record required to retire the command.

The HIF commits data before the physical completion value. At the publication
linearization point, HIF commits the completion and releases its active private
origin/CID mapping; the command graph consumes only its opaque completion
lease and retires its opaque handle. Host completion-queue acknowledgement
remains a separate HIF-private tombstone lifecycle.

## C4.1 boundary

C4.1 freezes only:

- one project-authored fixed software-oracle profile;
- explicit address-free command, completion-intent and profile codecs;
- private raw command capture and physical completion codec;
- source/provenance, cross-endian and fake-adjacent gates.

It does not implement command legality, the command graph, queue mechanics,
PRP walking, DMA or storage. Those are separate C4.2-C4.5 gates.

## Source boundary

The project commits no official specification PDF and no copied Linux, SPDK,
QEMU, FEMU or NVMeVirt source/table. The small interoperability field map and
literal fixtures are independently authored. External sources are recorded as
behavior, terminology and source-boundary references with `code_copied: false`.
This project review is not certification, endorsement or legal advice.

## Fixed claim boundary

The C4 profile has one eight-LBA fake namespace and a maximum 4-KiB transfer.
Passing C4.1 proves only codec/profile and architectural isolation under the
published fixtures. It establishes no native Linux-driver interoperability,
PCI register behavior, real requester DMA, MSI-X, vfio-user/QEMU integration,
persistent NAND mapping, standards conformance or performance result.
