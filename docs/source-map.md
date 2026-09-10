<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Current source map

This is navigation for the adopted scalable/serial-credit MQ2 path, not a new
architecture or a request to move directories. Current capabilities are in
[the status matrix](current-status.md); version and ownership decisions are in
[ADR-0012](adr/0012-versioned-physical-nand-media.md),
[ADR-0013](adr/0013-scalable-ftl-and-page-windows.md) and
[ADR-0014](adr/0014-native-profile-and-serial-mq2.md).

## Production path and its owners

| Layer | Source entry | Responsibility |
|---|---|---|
| PCI/HIF | [kernel/m4-native](../kernel/m4-native/README.md), `m4_hif.c` | One SQ consumer/CQE publisher, raw addresses, PRP capture, DMA authority and IRQ routes |
| Native binding | [frontends/linux-m4](../frontends/linux-m4/README.md), `native_worker.c`, `native_host.c`, `native_owner.c` | Drive retained captures and firmware, transfer Host bytes, revoke/drain exclusive ownership |
| Shared composition | [j0_construction.c](../frontends/headless-j0/j0_construction.c), [j0_action_drivers.c](../frontends/headless-j0/j0_action_drivers.c) | Bind actual ready volume/Block service and route typed actions; no FTL map policy |
| Controller buffers | [j0_controller_buffer.c](../frontends/headless-j0/j0_controller_buffer.c) | Internal frame leases; an internal lease is not Host DMA authority. Native transfers use `native_host.c` above, not the reference memory-Host binding |
| Protocol/lifecycle | [core/command-spine](../core/command-spine/README.md) and `profiles/linux_profile_v1_adapter.c` | Address-free command semantics and finite aggregate lifecycle actions |
| Storage construction | [scale_storage.c](../frontends/headless-scale/scale_storage.c), [native_scaled_media.c](../frontends/linux-m4/native_scaled_media.c) | Select one FTL/NFC construction; keep physical media holder alive across rebuilt volatile runtimes |
| FTL | [core/ftl-scale](../core/ftl-scale/README.md) | Mapping, RMW, private page windows, GC, journal/checkpoint, recovery and durable Block result |
| NFC | [core/nfc-page-v2](../core/nfc-page-v2/README.md) | PAGE2-R0 accepted payload/result ownership and physical group execution |
| Physical NAND | [media/file-nand-v2](../media/file-nand-v2/README.md) | PPA/main/OOB/health and reservation/home/terminal recovery; POSIX byte adapter underneath |

The native [Makefile](../frontends/linux-m4/Makefile) lists `FIRMWARE_SOURCES`,
`SCALED_SOURCES` and their selected worker targets. Shared files under
`headless-j0` and `headless-scale` are real production dependencies. Adjacent
`tests/` and `test_*.c` files are test drivers, not alternative FTLs. Some
reference objects remain compiled into common source lists; the explicit
factory/profile selects the executing path, without recovery-time fallback.

For the current scaled native construction, Write goes through Host DMA-in,
Block, retained FTL parent, PAGE2 and physical NAND before result publication.
Read traverses the storage layers before Host DMA-out. Physical media code
does not receive NVMe LBAs, and GC/NAND children do not enter the shared Host
action graph. BAR is volatile memory, not the persistent namespace backend.

## References and test entry points

| Role | Entry | Scope boundary |
|---|---|---|
| Tiny tagged/reference runtime | Ordinary `worker` / `check-runtime`, [core/m3p](../core/m3p/m3p.h), [nfc](../nfc/README.md), [file-NAND-v0](../media/file-nand-v0/file_nand_engine.c) | Existing 1-MiB/8-KiB reference and old image reader/writer |
| Historical core oracles | [core](../core/README.md), [headless-c35](../frontends/headless-c35/README.md), [headless-c4](../frontends/headless-c4/README.md) | Their exact C3/C4 lifecycle, fault and cross-platform results; not blanket current-stack coverage |
| Reference memory Host | [j0_host_data.c](../frontends/headless-j0/j0_host_data.c) | Headless Host-byte/authority binding, compiled in common source lists but not selected for native Host transfers |
| Current software aggregate | [scripts/check_current_spine.sh](../scripts/check_current_spine.sh) | Named reference checks plus selected real scaled/PAGE2/progress fixtures; no native PCI in hosted CI |
| Scalable storage checks | [headless-scale/ftl.mk](../frontends/headless-scale/ftl.mk) | Existing bounded Block/FTL/NFC/physical-media journeys on explicitly provisioned tmpfs |
| Explicit CRC ISA checks | [current-spine workflow](../.github/workflows/current-spine.yml), [existing CRC oracle](../frontends/headless-scale/test_crc_fast.c) | x86 SSE4.2 under GCC/Clang and ARM CRC under QEMU user mode; required backend checked at compile time, not a performance result |
| Native offline fixture | [test_scaled_runtime.c](../frontends/linux-m4/tests/test_scaled_runtime.c) | Actual selected worker/real storage, fake syscall/Host boundary; not two-queue kernel or IRQ evidence |
| Native evidence | [development result index](results/2026-09-10-scaled-storage-mq2.md) | Named Linux/owner/vector/literal-Host episodes and exact archived identities |

`media/README.md` is retained as an exact historical frozen file. Its old
directory overview is not the current format-selection guide; use the direct
v0/v1/v2 component links and ADR-0012. Navigation maintenance must not rewrite
that file's frozen hash or broaden old C3/C4 expected outputs.

Regular-file disk results, tmpfs functional recovery, ARM user-mode/layer
measurements and native VM measurements keep separate identities. The latest
source does not inherit every old capacity/platform result. See the
[results index](results/README.md) before selecting a test or citing a rate.
