<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# NAND flash controller

Future BSD-3-Clause source here models physical NAND resources, staged operations, timing, ECC/read-retry, suspend/resume and deterministic faults. Firmware chooses FTL/GC policy; NFC chooses physical scheduling/outcome.

The first profiles are abstract transaction models and do not claim pin-level ONFI conformance or calibrated physical NAND accuracy.
