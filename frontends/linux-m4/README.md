<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Native Linux firmware binding

This work area connects the synthetic PCI/HIF to the portable firmware,
selected FTL/NFC and physical file-NAND path. The native worker and owner-control
binding are implemented. The integration is experimental; implementation and
lab execution do not themselves establish reviewed architecture graduation.

Current adopted scaled/large/MQ2 constructions and their exact evidence are
listed in the [development matrix](../../docs/current-status.md). The ordinary
`worker` and `check-runtime` remain the old tagged reference. Native
format/recover/stop steps are in the [scaled operator guide](../../docs/native-scaled-usage.md).

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

Run the retained tiny-reference runtime matrix with:

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
and runs the runtime matrix. It additionally runs selected PAGE2/physical-v2,
retained-parent and MQ2 progress fixtures on an explicit bounded tmpfs as an
ordinary user. Hosted `current-spine` runs those same semantics.
Historical frozen leaf/changed-path checks retain their old scope and are not
silently relaxed to approve the evolved integration.

The tiny-reference native binding selects one finite lab budget: 65536 NFC identifiers and
record sequences, 4096 Host mutation sequences, compatible lifecycle ranges,
and 65536 file-media transactions. These are ceilings, not capacity or endurance
claims. NFC diagnostic trace windows are retired only when no operation/event
is live; sequence numbers, cache, epochs and media are not reset. The small
reference binding retains full traces. Exhausted admission returns a terminal
resource error; recovery does not erase persistent record history. These limits
describe the retained M3P/C3 binding, not the scalable FTL/PAGE2 constructor's
capacity or resource configuration.

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
existing scaled FTL, PAGE2 R0 and physical-v2 NAND model. It defaults to 64 MiB;
the same fixture accepts `--namespace-mib 256`. Requests remain
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
After the default check, run the same built ELF serially at 256 MiB:

```sh
FWLAB_TEST_MEDIA_DIR=/run/fwlab-test-media \
  flock -n /run/fwlab-test-media/.run.lock \
  frontends/linux-m4/build/scaled-offline/native_scaled_offline --namespace-mib 256
```

The finite journey checks Identify capacity, nonzero Write/SELF near the last
valid LBA, Flush, Read, complete runtime/media close, fresh-epoch recovery,
readback and continued writes. The data mover must actually transfer Host
bytes. Format is explicit and new-file-only; recovery neither creates nor
formats, and a second format cannot replace an image. A different supported
capacity on recovery is rejected without changing the existing image's inode,
size or modification time; correct-capacity recovery then checks the data.
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

## Selected scaled worker

`make -C frontends/linux-m4 scaled-worker` builds
`build/scaled-offline/fwlab_native_scaled_worker`. It selects the same
physical-v2 construction at compile time, while sharing the ordinary
worker's execution loop, host mover and owner-control code. No per-I/O storage
fallback or second executor is added. The default `worker` remains legacy.

Scaled, PUMP, Large and MQ2 workers accept `--namespace-mib 64|256|65536`,
defaulting to 64 MiB. They use the same capacity/geometry presets as the
headless scaled test. The tiny legacy worker rejects this option. Namespace
size is owned by the formatted/recovered FTL volume: the Linux protocol adapter
already uses that volume for Identify NSZE/NCAP/NUSE and command LBA bounds.
Neither PCI BAR size nor Host transfer size grows with namespace capacity.

This selects a **fresh volume's capacity**, not online expansion or conversion
of an existing image. On later startup supply the same capacity and UUID,
without `--format`; mismatches fail rather than resizing or formatting.
Fresh format checks actual tmpfs space and available RAM before allocation.
The 64 GiB preset explicitly permits an image up to 90 GiB, including NAND
overprovisioning/OOB/metadata; small presets retain the 600 MiB mapping limit.
Recovery does not demand a second image's RAM for already allocated tmpfs data.
The normal synchronization calls, exclusive holder, FTL format and PAGE2
semantics remain unchanged.

Large-volume startup/drain have finite capacity-aware iteration allowances and
30-second progress output. These software allowances do not extend the
controller's advertised readiness timeout. Native 64 GiB bind/reset/rebind and
owner-switch readiness still need actual Linux/HIF qualification; a headless
64 GiB PASS or the offline 256 MiB fixture does not prove those timings.

The scaled entry requires the explicit `FWLAB_M4_ATTACH_IDENTITY` ioctl from
the matching kernel. It records UUID, media format and the supplied
binding digest once. The attachment schema version, ordinary I/O wire version
and media-format identity are distinct. Unsupported kernels fail startup;
there is no fallback to legacy ATTACH or automatic image conversion. Exact
same-descriptor retries are bounded and do not publish readiness. The digest
is supplied build provenance, not cryptographic attestation of a running image.

The CLI retains explicit new-only `--format` versus recovery without that flag.
Use fresh disposable media for a separately scoped online qualification; this
build is not authorization to replace a running worker or existing NAND file.
Native qualification was still pending when this entry was introduced.
Subsequent selected-profile native/reset/owner evidence is indexed in the
[development results](../../docs/results/2026-09-10-scaled-storage-mq2.md),
under its actual source and profile identities. That is not a rerun or approval
of every earlier BAR/scaled combination. No performance or multi-queue claim
follows from building this target alone.

The matching `native-io-scaled` / `native-io-scaled-static` clients retain exact
device/BDF/identity and exclusive-open checks, but require exactly64MiB and add
an8KiB case ending at the last LBA. Default clients still require1MiB. Both
provide `profile-plan` as a no-device-open description, not a test PASS. The
existing initramfs builder accepts the chosen static client as its second
argument; owner subprocesses execute that same client binary.

## Selected singleton-pump worker

`scaled-pump-worker` builds the separate `fwlab_native_scaled_pump_worker` in
the same build directory. Match it with kernel `FWLAB_M4_PRODUCER=2`; existing
BAR workers and the default kernel remain separate compatibility references.
The explicit attachment-v2 mode request never falls back to a BAR attachment,
and media format/UUID/binding identity are not used as producer-mode flags.

The pump executes synchronously at the top of the actual firmware loop, before
STATUS and owner/runtime blocking. It advances the existing kernel HIF, not a
second protocol executor, and visits at most two queues for one fresh capture.
It does not move FTL/NFC/media work into the kernel or change their semantics.
The response distinguishes admission failure from a HIF service fault. A valid
service fault still reaches STATUS, owner polling and drain/reset, without new
business admission that turn. Lost replies cause at most three same-fd ticks;
retained NEXT delivery and keyed DMA/publication remain authoritative.

`attach-check` covers the actual shared mode/identity validators and userspace
helpers with fake syscall responses. `check-scaled-pump-runtime` compiles the
same selected worker loop and crosses unchanged real scaled storage. Its finite
fake Host injects a service fault/reset and loses one post-capture reply; only
PUMP may capture and NEXT must return the retained command without recapture.
The BAR offline target retains the legacy-constructor smoke. Both require the
existing capped tmpfs and serial policy above. No second test framework or
kernel fault/locking claim is created by these offline checks.

The native client adds `cut4` to the existing one-shot cut/reset/readback
journey, matching the kernel's single service-capture fault point. Actual kernel
mode compatibility, native I/O/reset, pending-mask/unmask and owner switching
were separate qualification requirements, not consequences of the offline
check. The later selected Large/MQ2 episodes have their own
[recorded evidence](../../docs/results/2026-09-10-scaled-storage-mq2.md);
those results do not cover arbitrary producer/profile combinations. Removing
the BAR producer does not remove deferred IRQ work or establish whole-system
single-thread bandwidth.

## Large serialized Host profile (adopted development)

`large-worker` / `check-large-runtime` select 1 MiB maximum I/O on the default
64 MiB namespace and unchanged scaled FTL/PAGE2/physical-v2 path. The worker's
namespace capacity is selectable independently via `--namespace-mib`. Namespace
capacity and Host command size are independent. Match this worker with a PUMP
kernel built using `FWLAB_M4_HOST_PROFILE=2`. The separate 160-byte attachment-v3
binds exact Host limits before identity pinning; old 112/128-byte messages remain
SMALL-only and do not silently attach a large worker to a small implementation.

The first profile permits one captured I/O command (including Flush) and one
Admin command through transport retirement. Ring depth and metadata capacity
remain 32, not 32 resident 1 MiB payloads. Kernel, native mover and J0 each own
their own 1 MiB I/O frame and 4 KiB control frame; small I/O cannot borrow the
control reserve. Existing inline small-reference arrays are not enlarged.
The native path requires referenced input, not the old inline snapshot mode.

Large requests snapshot a complete bounded PRP graph before data DMA, including
up to 257 data-page references and two offset-capable list pages. Data mappings
remain individually checked 4 KiB segments. Buffer, Host-DMA and publication
identities remain distinct; no loan or direct-LBA backend is introduced.
Unknown SHAPE results retain the same origin/frame for bounded readback; lasting
uncertainty fails closed instead of ordinary rollback and re-admission.

The profile keeps AER as immediate Unsupported (SCT 0 / SC 1 / DNR 1), not a
long-lived event request occupying the only Admin credit. Serial Q1 ingress
establishes Q1 Write-before-Flush order; it does not claim future MQ ordering.
Frames return at their existing owner-specific drain/retirement boundaries,
and retained cancelled kernel requests are cleaned before the zero certificate.

`profile-check` executes the actual bounded PRP parser and controller-buffer
owner with fake adjacent inputs. `attach-check` includes exact v3 reply/retry
and profile-pin checks. The existing offline worker journey also has a selected
1 MiB variant; fake syscall results do not establish kernel/IOMMU execution.
`native-io-large` / `native-io-large-static` require the same exact 64 MiB/BDF
guard plus MDTS 8, and add aligned and offset 1 MiB cases. Their `aer` command
checks Unsupported followed by Identify; the scoped lab runner adds reset.

The LARGE client distinguishes a logical workload from a wire command. On L1
it reads the guarded namespace's `max_hw_sectors_kb` before I/O and explicitly
splits a larger workload at that limit, recording each transfer's wire size and
completed command count. Linux's IOMMU mapping recommendation can make that
limit 128 KiB despite MDTS 8. There is no retry-as-smaller fallback after an I/O
error. The L2 guest requires room for an unsplit 1 MiB command and fails if the
limit is smaller; its owner journey requires the explicit large-wire success
marker. Split L1 commands do not prove single-command 1 MiB SELF or atomicity.
The strict LARGE guest prepares a 2 MiB-aligned, synchronously collapsed THP
payload buffer and fails if that preparation is unavailable. This keeps the
1 MiB-plus-offset buffer within one folio for Linux's bio/SG limits while NVMe
still uses 257 controller-page PRP references. It does not change the NAND
image, controller frame sizes, DMA checks, or submitted command lengths.

Subsequent native and performance evidence is now indexed in the
[development results](../../docs/results/2026-09-10-scaled-storage-mq2.md).
It is separate from these offline checks; no 1-MiB atomicity, new NAND algorithm
or 10-GB/s claim follows from a successful build.

### MQ2 serialized profile (adopted development)

`mq2-worker` selects private Host profile 3: two paired I/O queues, depth 32,
Admin vector 0 and I/O vectors 1/2. It retains one **global** I/O credit/frame
and one Admin reserve, not one I/O frame per queue. NoQ negotiates the minimum
of requested SQ count, requested CQ count and two. Its captured result remains
stable on retry. Queue deletion drains captured holders before reuse; CQE
publication uses the captured SQ/CQ incarnations and association.

The selected loop uses private per-call `advanced` / `runnable` facts from the
native, J0 and PAGE2 storage owners. Polling budget and occupied slots are not
progress. The old loop entry remains unchanged for reference builds. Neither
these facts nor a buffer lease grants Host DMA or durability authority.

`profile-check` includes the MQ2 policy's NoQ/paired-queue/retry cases with fake
queue effects. `check-progress-runtime` selects actual MQ2 profile 3, checks its
constructed limits and reuses the real 1-MiB storage journey with a controlled
waiting executor and observed, still-executed `nanosleep`. Its fake Host drives
Q1 only: it tests profile binding and progress versus idle, not a two-queue
kernel or IRQ delivery. Actual Linux two-queue,
three-vector, deletion/reset/owner and cost episodes subsequently passed in
their [recorded scope](../../docs/results/2026-09-10-scaled-storage-mq2.md).
This offline entry alone is not that native evidence or a release approval.
