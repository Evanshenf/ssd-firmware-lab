<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Persistent physical media

Future BSD-3-Clause source here provides file and raw-block containers for page/OOB, erase generations, wear/bad-block state, physical-operation WAL and checkpoints.

The API accepts physical operations only. It cannot contain an authoritative decoded logical map or silently repair firmware state. Raw initialization and runtime opening follow [ADR-0002](../docs/adr/0002-power-domains-and-persistence.md).
