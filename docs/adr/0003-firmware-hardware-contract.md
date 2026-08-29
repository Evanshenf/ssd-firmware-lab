<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0003: Firmware-visible hardware contract

- Status: Accepted semantic baseline; binary layouts await prototype
- Date: 2026-08-28

## Goal

Software HIF, an ISS SoC model and future endpoint HIF logic present the same policy-facing semantics to firmware. Firmware does not absorb queue walking, Host address resolution, physical completion writing or interrupt-generation mechanics.

## Canonical command boundary

ADR-0006 refines how full identity crosses this boundary: HIF retains and
interprets owner/queue/ring/CID fields and binds them into an opaque origin
token. Portable firmware does not parse those transport fields; it interprets
only its portable instance, controller epoch and command UID.

Raw command bytes, transport address graphs, Host/guest/IO virtual addresses, page-frame identities, pins, IOAS objects and raw traces stay in trusted HIF. Raw trace is disabled by default, sensitive when enabled and excluded from public test artifacts.

Firmware receives a versioned, address-free canonical descriptor containing:

- ABI/version/feature metadata;
- full command identity and safety-state generation;
- protocol-semantic fields needed for independent firmware interpretation;
- descriptor kind and facts that an address was present, but never its value;
- a normalized transport fault;
- opaque data/metadata capabilities and bounded ranges;
- a random trace cookie not derived from raw submission bytes.

There is no raw-submission hash across the trust boundary because it would permit address correlation. Derived hints have a validity mask and are not a second protocol truth.

## One safety schema

One machine-readable schema defines only the maximum transport-safety envelope. It generates kernel and user validators plus a versioned runtime safety snapshot. Runtime state may narrow, never expand, authorization without a new reviewed schema/config generation.

The initial repository publishes the schema/generator interface and toy fixtures only when implementation begins. Real protocol tables or near-verbatim specification-derived definitions require provenance and source-boundary review.

HIF rejects unsafe graphs/ranges or reports a normalized transport fault. Firmware alone decides protocol legality, actual transfer and completion status from the canonical command plus its Feature/Namespace state.

## Capability DMA

A DMA request names an opaque command-scoped capability, capability offset, controller-memory offset, length, direction, ordering flags and cookie. A capability binds exact range/direction and the full command identity. Completion returns the cookie, byte count, normalized fault and identity generation.

Reset, owner change, unmap and queue recreation revoke capabilities. The software baseline uses bounded bounce buffers; a real HIF may use an address-walker context without changing firmware semantics.

## Lifecycle and completion

The lifecycle and full identity are defined in [the architecture](../architecture.md#command-identity-and-lifecycle). Abort is a cancel/query/ack protocol. Held asynchronous commands, queue-delete barriers, reset begin/ack and daemon forced cancellation use the same state machine.

Firmware produces only result/status policy as a completion intent. HIF owns captured command/queue identity, queue head/tail and phase, physical completion ordering and interrupt mechanics. It guarantees data visibility before completion visibility before interrupt visibility.

## NFC contract

The NFC descriptor names physical geometry, staged read/program/erase operation, controller-buffer offsets, ECC/retry profile, physical status, scheduling group, cookie and completion identity. A software model may internally coalesce stages, but the ABI must not prevent a future RTL NFC from exposing them separately.

## Reset retention

Ownership reset clears all Host-derived volatile queues, capabilities, DMA contexts, interrupt pending state, active commands and timers. Only persistent media and explicitly declared retention regions survive. A new owner may negotiate different transport capabilities without changing portable firmware policy.

## Forbidden firmware dependencies

BAR polling, synthetic PCI, VFIO, Host address/pin resolution, GPA/HPA/IOVA, QEMU or `libvfio-user` internals, eventfd, filesystem paths and simulator-private WAL must not enter firmware target includes, link dependencies or persistent formats.
