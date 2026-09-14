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
| Current userspace cross build | [current-spine workflow](../.github/workflows/current-spine.yml), native `mq2-worker` target | Complete current worker compiles for AArch64/RISC-V64/s390x; ELF class, byte order and machine checked. Only existing small PAGE2/media fixtures execute under emulation, not the whole worker or kernel |
| Scalable storage checks | [headless-scale/ftl.mk](../frontends/headless-scale/ftl.mk) | Existing bounded Block/FTL/NFC/physical-media journeys on explicitly provisioned tmpfs |
| Resource-timed NAND LAB | [nfc_page_v2_lab.c](../core/nfc-page-v2/nfc_page_v2_lab.c), `check-parallel-read-j0`, `check-mutation-j0`, `check-nfc-mutation` | Explicit READ-R1 or RW-R2 construction; actual physical-v2 bytes and resource timing, not native wall-clock throttling or vendor calibration; [ADR-0019](adr/0019-timed-nand-mutations.md) |
| Cooperative channel LAB | [nfc_channel_v2.c](../core/nfc-page-v2/nfc_channel_v2.c), [channel_volume.c](../media/file-nand-v2/channel_volume.c), `check-channel`, `check-channel-volume`, `check-channel-j0` | Independent real shards, sealed PAGE2 batches and existing serial FTL/J0; no threads, multi-head writes or native selection; [ADR-0020](adr/0020-cooperative-nand-channel-domains.md) |
| Multi-head format3 LAB | [ftl_scale_heads.c](../core/ftl-scale/ftl_scale_heads.c), [ftl_scale_write.c](../core/ftl-scale/ftl_scale_write.c), `check-multihead-j0`, `check-multihead-parent` | Actual multi-head DATA/ordered MAP on the same Block/NFC/media; process-reopen and bounded GC/pressure, not threads/native throughput; [ADR-0021](adr/0021-multihead-ftl-write-waves.md) |
| Channel-worker LAB | [nfc_channel_v2_actor.c](../core/nfc-page-v2/nfc_channel_v2_actor.c), [nfc_channel_workers.c](../frontends/headless-scale/nfc_channel_workers.c), `check-channel-workers`, `check-channel-workers-j0` | One shared actor interpreter with cooperative/one/four worker bindings, frame ACKs and actual joins; same real FTL3/media, not a native mode or speedup claim; [ADR-0022](adr/0022-channel-worker-execution.md) |
| Independent-plane READ LAB | [nfc_page_v2_lab.c](../core/nfc-page-v2/nfc_page_v2_lab.c), [test_plane_read_j0.c](../frontends/headless-scale/test_plane_read_j0.c), `check-nfc-plane`, `check-plane-read-j0` | Explicit plane/LUN ownership, same bus, unchanged generic format2 READ pool and real media; no native/mutable-B3 parallel READ claim; [ADR-0023](adr/0023-independent-plane-read.md) |
| Mutable format3 READ/WRITE LAB | [ftl_scale_runtime.c](../core/ftl-scale/ftl_scale_runtime.c), [scale_storage.c](../frontends/headless-scale/scale_storage.c), `check-unified-rw-parent`, `check-unified-rw-j0` | Disjoint existing pools, one writable instance, real GC/IPR and close/recovery; old defaults preserved, not native selection; [ADR-0024](adr/0024-mutable-format3-read-write.md) |
| Native channel opt-in | [native_scaled_media.c](../frontends/linux-m4/native_scaled_media.c), [test_scaled_runtime.c](../frontends/linux-m4/tests/test_scaled_runtime.c), existing `progress-runtime` with `--nand-profile channel-lab4k` | Same actual worker constructor/loop and mutable format3/IPR, process-lived volume and fresh runtimes; cooperative strict POSIX, fake Host only, no kernel/M5/threaded proof; [ADR-0025](adr/0025-native-channel-construction.md) |
| Shared capacity construction | `scale_storage_capacity_mib()` in [scale_storage.c](../frontends/headless-scale/scale_storage.c) | Headless and native use the same64/256/65536MiB preset geometry; recovered FTL volume still owns advertised capacity |
| Current 64-GiB qualification entry | `headless-scale/ftl.mk check-64g` / `--full-64g` | Selects PAGE2/window-v2/mapped-physical-v2 with an explicit large memory budget; result scope in [capacity evidence](results/2026-09-10-current-capacity.md) |
| Historical 64-GiB reference entry | `headless-scale/ftl.mk check-64g-reference-v1` | Preserves C3 NFC/compact-media-v1 and its old evidence; no implicit fallback from the current path |
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
