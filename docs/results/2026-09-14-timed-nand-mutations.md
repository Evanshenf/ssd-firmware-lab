<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# N2a: resource-timed NAND PROGRAM/ERASE on the existing storage path

Source: `08e852045517acc42ae0071345e2999f60bb1d9d`, following core commit
`832100e` and baseline `7c4217671798721180880adec2ae439589e833de`.
Profile: **LAB-RW-R2**, explicit homogeneous synthetic timing. This is a bounded
development result, not a new release freeze or native/physical-NAND graduation.
Decision and next boundary: [ADR-0019](../adr/0019-timed-nand-mutations.md).

## Implemented path

The same four-slot NFC event engine now has an always-timed READ/PROGRAM/ERASE
constructor. Real PROGRAM effects execute one physical page at a time, followed
by shared-channel STATUS. The accepted group owns its snapshot and LUN
reservation. Array intervals, channel occupancy, retained results and actual
main/OOB effects have separate accounting. No wall-clock sleeps are introduced.

`scale_storage_mutation_lab_factory_init()` connects the existing Linux-profile
and lifecycle to ordinary serial format2 FTL, then LAB-RW-R2 and physical-v2.
FTL source, on-media formats, media transactions/CRC/barriers and native
construction semantics are unchanged. There is no raw-disk or Host-LBA shortcut.
The native worker still selects ordinary R0; this LAB is not native throttling.

## Finite obligations and observations

| Group | Observed evidence |
| --- | --- |
| 1: snapshot/effect/status | Three real page programs preserve accepted main/OOB despite later caller-buffer changes. The first physical effect exists while result consumption still returns pending. DONE cancellation is a no-op. Actual erase increments generation; same-format reopen observes erased bytes. |
| 2: resources | Different LUN arrays overlap; independent channels start work together; shared-channel transfers/status remain exclusive. A same-LUN erase waits for the entire preceding program group. STATUS progresses without self-LUN blocking. |
| 3: cancel/reset | Fixed cuts before LOAD/input/first confirm, immediately after confirm, after first effect and after DONE distinguish whole-group NONE from complete accepted-group drain. ERASE before/after dispatch follows the same explicit commitment boundary. |
| 4: failure facts | Known completed prefix survives later typed NONE/TORN, API uncertainty and preflight error. Unattempted suffix is FACT_EFFECT/NONE. An issued peer with no physical callback yet produces no new effect after quarantine. Real-media cases include persisted bad-block failure and successful second-page effect followed by lost API certainty. |
| 5: serial FTL journey | Existing Linux-profile/J0 RMW across partial head/full page/partial tail, ordinary SELF, FUA, Flush and same-format reopen pass. Closing after DATA confirm reconciles DATA/MAP. A full two-page group retains internal FULL/SELF; an 8-KiB parent deliberately spanning a physical-block boundary completes only its first page, preserves base frontier and leaves the second page unchanged. Existing all-zero close certificates pass. |

The adjacent core test uses the existing controlled NAND fixture. The separate
real-media test drives actual physical-v2 main/OOB storage. The J0 test uses
the existing protocol/lifecycle/FTL implementation, not another command engine.
Read-main bytes and FTL OOB identities are checked through that real J0 path;
the lower fixture also inspects exact physical bytes, including after reopen.

GCC 13.3 and Clang 18.1 ASan/UBSan passed all three core suites (R0, READ-R1,
RW-R2) and the new real-media/J0 fixtures. AArch64, RISC-V64 and big-endian
s390x cross-builds passed ELF identity checks; all three core suites and both
new real fixtures also executed successfully under QEMU user mode 8.2.2.
This is local cross execution, not hosted CI or native PCI/kernel execution.

The private LAB getter rename received one affected existing N1 J0 regression,
without changing its assertions. The ordinary MQ2 userspace worker compiled and
linked, but was not started. No previous 64-GiB/native campaign was replayed.

## Media and measured quantities

All new real fixtures ran serially as an ordinary user on a separately
provisioned, capped 1-GiB local tmpfs. There is no slow-disk fallback. Each lower
fixture creates a 2,245,120-byte physical image; each J0 fixture creates an
8,931,328-byte physical image with a 1-MiB namespace. These sizes are fixture
choices, not a change to the existing native 64-GiB construction.

| Synthetic real-media/J0 setting | Value |
| --- | --- |
| Main / OOB per page | 4096 / 128 bytes |
| Read command / PROGRAM load | 1000 ns |
| Read array per LUN | 10000 ns |
| PROGRAM confirm / array | 1000 / 100000 ns |
| ERASE command / array | 1000 / 1000000 ns |
| STATUS command / response | 1000 ns / 1 byte |
| Each shared channel | 1000000000 bytes/s including main/OOB and status response |

These are **explicit test values, not global defaults or vendor specifications**.
An uncontended page takes 15,224 modeled ns for READ and 107,225 ns for PROGRAM:
approximately 269.049 and 38.200 decimal MB/s of main payload per LUN. Four
active slots, shared channels, serial FTL, metadata and GC can reduce aggregate
Host throughput. Extra capacity or package/die labels alone cannot increase it.
The core fake deliberately uses a different smaller synthetic time scale.

The mixed six-program/one-erase lower episode consumes 1,325,678 modeled ns.
The J0 write/RMW/Flush observation consumes 2,067,723 modeled ns. Neither is a
native bandwidth result. Actual whole-fixture elapsed times were approximately
0.01/0.03 seconds for GCC lower/J0 and 0.01/0.06 seconds under Clang sanitizers;
these include setup and validation, not a simulator throughput benchmark.
Respective peak process RSS was 4,096/5,668 and 13,696/42,496 KiB, excluding
tmpfs page-cache storage. Synchronization and exclusive-holder checks remain.

## Reproduction and identities

Use the [NFC guide](../../core/nfc-page-v2/README.md) and existing tmpfs
provisioning requirements. Run the media entries serially:

```sh
make -C core/nfc-page-v2 check-mutation
make -C media/file-nand-v2 check-nfc-mutation
make -C frontends/headless-scale -f ftl.mk check-mutation-j0
```

The complete changed-source file-hash manifest has SHA-256
`9f18b7cc68ebfd4ba52920bd14964b303aed6bfcbdfba3851a09d8543ef2a93d`.
Selected archived real-fixture logs:

| Lane / fixture | SHA-256 |
| --- | --- |
| GCC lower | `c3cbe444ea44f1a976a23b6ef83aee4c574db0823d7d94467cda758892743cb4` |
| GCC J0 | `a9c186d754dbb7ce1b9ec2525fa2cf5148f81895025d08a0b9f4f62f14de1ba8` |
| Clang lower | `eb63088d87eab67860417f4042b52313fe2bd3e6e4f525df0b82e4bfceb64e97` |
| Clang J0 | `009a97a1aebfb68cae40063e1545d485c47260e30e3401e51fdff3e467bdcb1c` |
| AArch64 J0 | `02927579b8c564009b77c52ae06b92a10654827887aa0e94928dc37a6b43ff96` |
| RISC-V64 J0 | `cad99927c0c81a41cb974cb4de087d78ed32d533d534c7ba3657bd1e80d98a9b` |
| s390x J0 | `3958f74c012e89a2d782c3ed381767918c5ae58bf94c8e2560461475f5e2212b` |

An initial J0 harness asserted WRONG_STATE for the serial FTL read-only request;
the existing API returns INVALID because this constructor has no read pool.
Only that assertion was corrected. The original failed log/image is retained;
no product repair is attributed to it. Successful disposable images were removed
after close and holder-identity checks. Existing images and raw media were not
modified.

## Review and STOP

The preceding bounded design consultation allowed implementation with no new
required correction; it did not approve future code. One independent exact-source
confirmation of `08e8520` returned NoRequired/STOP for the five groups above.
It inspected source and supplied logs, not independent test reexecution or the
whole repository. No further discovery/clean-streak campaign follows this gate.

N2a ends here. N2b addresses multi-head placement/striping and coordinated
metadata/recovery representation. Modern NAND geometry, TLC/pSLC, 4-TB scale,
vendor calibration, native timing and real host-power-loss survival are not
established. P01 still prevents a claim of backend-only migration to silicon.
