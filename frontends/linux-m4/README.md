<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Native Linux firmware binding

This work area connects the synthetic PCI/HIF to the portable firmware,
M3-P, NFC and physical file-NAND path. The native worker and owner-control
binding are implemented. The integration is experimental; implementation and
lab execution do not themselves establish reviewed architecture graduation.

The shared lifecycle has a private publication extension. A completion lease
retains the immutable intent until publication has a known outcome. Profile
retirement then allows command-slot reuse. Host queue occupancy and pending
notification remain HIF responsibilities. Old lease and command identifiers
are not reused when a storage slot is recycled.

The runtime accepts a Host binding at construction. Its default headless
binding snapshots supplied input bytes. A referenced Host binding receives an
origin and exact shape with no input array; the later DMA action reads the
Host bytes through that binding. Firmware, FTL and NFC retain their existing
address-free interfaces.

Run the current prerequisite matrix with:

```sh
make -C frontends/linux-m4 check-runtime
make -C frontends/linux-m4 check-runtime CC=clang
```

It uses the real two-profile firmware/media fixture, publishes and reclaims
64 commands, discards one lease during close, and checks a referenced Host
binding whose bytes change between admission and DMA. That binding is an
adjacent test provider, not native DMA evidence. The final marker explicitly
reports `native_hif=not_connected`.

The same existing runtime ELF also checks repeated empty Flush before/after
recovery, automatic foreground reclamation, zero-live GC close/restart cuts,
free=1/2 three-page writes, two full namespace overwrites, 4096 continued reads
and same-image full readback. A small reference budget exercises terminal
resource failure with DNR and reserved final Flush/close capacity. These are
software-path tests, not native Linux or real power-loss evidence.

Run the current semantic aggregate with `sh scripts/check_current_spine.sh`
(`CC=clang` selects Clang). It rebuilds and executes the existing S0-B/J0-A/J0-B
ELFs with actual object/archive/ELF digests, then builds the worker/native client
and runs the runtime matrix. Hosted `current-spine` runs those same semantics.
Historical frozen leaf/changed-path checks retain their old scope and are not
silently relaxed to approve the evolved integration.

The native binding selects one finite lab budget: 65536 NFC identifiers and
record sequences, 4096 Host mutation sequences, compatible lifecycle ranges,
and 65536 file-media transactions. These are ceilings, not capacity or endurance
claims. NFC diagnostic trace windows are retired only when no operation/event
is live; sequence numbers, cache, epochs and media are not reset. The small
reference binding retains full traces. Exhausted admission returns a terminal
resource error; recovery does not erase persistent record history.

The native binding provides the referenced Host transfer, queue effects and
CQE publication through the private kernel interface. The kernel owns PCI/BAR,
mappings, Host transfers and IRQ; the userspace process owns portable firmware,
FTL/NFC and physical file-media policy. See the [native integration guide](../../kernel/m4-native/README.md)
for the required lab environment, media identity, startup and cleanup rules.

`worker` builds the firmware process; `native-io` builds the separate native
integration client. Its `owner-stale` mode runs exactly four J3 stale-authority
checks using two sequential VFIO/IOMMUFD attachments on the same function.
It reuses fixed IOVAs, completion slot/lease number and eventfd descriptor,
then requires both old-key rejection and successful current completion. The
private canary controls are inert unless explicitly armed by the exclusive
firmware/control owner. The producer submits Identify only and contains no
NVMe command executor, FTL or logical-media shortcut.

Its `owner-budget` mode adds a bounded, read-only-data workload to `owner-host`:
the actual Linux driver must receive SC=6/SCT=0/DNR=1 at the finite NFC budget,
without a timeout/reset, with the error buffer unchanged and final Flush still
successful. It deliberately consumes that runtime's budget and then revokes
the owner. All device identity guards remain in force. Do not run it against
an ordinary physical namespace.

## Offline scaled native construction

The ordinary `worker` target still constructs the tiny M3P/C3/file-v0 binding.
The separate `check-scaled-runtime` target is an **offline prerequisite**, not
a native PCI or owner-switch qualification.

It runs the actual native constructor, worker loop, host data mover and
completion handling against a bounded fake ioctl boundary. Storage is the
existing 64 MiB scaled FTL, PAGE2 R0 and physical-v2 NAND model. Requests remain
8 KiB; the original worker scheduling and sleeps remain. The fake owns only
Host byte buffers and assumed transport identities, never namespace storage.
No real device is opened, and unknown ioctl operations fail.

Use a separately prepared, capped 1 GiB tmpfs at `/run/fwlab-test-media` with
sufficient available RAM. Run serially; no slow-disk fallback is provided:

```sh
flock -n /run/fwlab-test-media/.run.lock \
  make -C frontends/linux-m4 check-scaled-runtime \
  FWLAB_TEST_MEDIA_DIR=/run/fwlab-test-media
```

Code and logs stay outside tmpfs. The test uses only its own newly created
directory/image and removes it on success; a failed case leaves its image for
diagnosis. It never uses an existing deployment image or raw block device.

The finite journey checks Identify capacity, nonzero Write/SELF near the last
valid LBA, Flush, Read, complete runtime/media close, fresh-epoch recovery,
readback and continued writes. The data mover must actually transfer Host
bytes. Format is explicit and new-file-only; recovery neither creates nor
formats, and a second format cannot replace an image.
One adjacent constructor/close smoke also checks the unchanged legacy
selection; it does not repeat the historical full runtime matrix on `/tmp`.

`native_scaled_media` owns the media holder, arena, factory and binding at
stable addresses until the associated native context finishes its runtime.
Keep this owner through all reset and NO_OWNER intervals: owner grant can
reconstruct a runtime through it later. The factory frees FTL/NFC state only.
Final close stops owner-server callbacks before draining runtime and media.
The media owner refuses close while a runtime exists; this last guard alone
does not authorize closing a holder during NO_OWNER. A zero-initialized owner and an outliving native context are
private construction preconditions, not a general resource-management API.

The offline check also uses the actual typed attachment helper and the small
identity-pinning routine shared with HIF, checks the owner observation, and
reconstructs once while retaining the same open media holder. Its ioctl/PCI
boundary remains fake. This check is not native PCI/M5, ARM kernel, kernel readiness timeout,
power-loss durability or throughput evidence. No WRITE-loan or READ-copy change
is included. Existing snapshot, CRC/OOB and actual synchronization semantics
remain in the storage engines.

## Selected scaled worker candidate

`make -C frontends/linux-m4 scaled-worker` builds
`build/scaled-offline/fwlab_native_scaled_worker`. It selects the same fixed
64 MiB physical-v2 construction at compile time, while sharing the ordinary
worker's execution loop, host mover and owner-control code. No per-I/O storage
fallback or second executor is added. The default `worker` remains legacy.

The scaled entry requires the explicit `FWLAB_M4_ATTACH_IDENTITY` ioctl from
the matching candidate kernel. It records UUID, media format and the supplied
binding digest once. The attachment schema version, ordinary I/O wire version
and media-format identity are distinct. Unsupported kernels fail startup;
there is no fallback to legacy ATTACH or automatic image conversion. Exact
same-descriptor retries are bounded and do not publish readiness. The digest
is supplied build provenance, not cryptographic attestation of a running image.

The CLI retains explicit new-only `--format` versus recovery without that flag.
Use fresh disposable media for a separately scoped online qualification; this
build is not authorization to replace a running worker or existing NAND file.
Actual Linux probe/reset timing, native I/O and M5 remain to be qualified for
this candidate. No performance or multi-queue claim follows from its build.
