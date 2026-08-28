<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C2.4 lifecycle and real-race oracle

This directory contains an independent userspace oracle for the frozen C2.2
VFIO cdev module. It does not link the C2.1 engine, reuse its transition code,
or alter the disposable A-prime wire contract.

The bounded lifecycle matrix proves distinct IOAS A/B ownership across a
successful replacement, preservation of B after a deliberately misaligned
IOAS C replacement fails, unwind after a REPLACE result cannot be copied back
to a read-only userspace argument, duplicate-detach stability, and a serial
close while attached followed by IOAS cleanup and a new open generation.

The concurrency matrix is bounded real-iommufd stress using
`pthread_barrier_t` starts. Submit races an
exact IOAS UNMAP repeatedly; the transfer result may only be success or
`-ENOENT`, and every new request after UNMAP returns must report `-ENOENT`.
Separate bounded loops race submit with RESET, IOAS replacement, and DETACH.
The accepted outcomes are defined by the frozen device mutex: submit either
returns the full 64-byte write before the transition, or it observes the exact
post-transition state rejection. Every completed transition must advance one
generation, clear RESULT and the full data region, and leave exactly the
expected IOAS as owner. The test never closes a device fd concurrently; the
close-with-attached case is intentionally serial.

The pthread barrier coordinates userspace release; it does not prove that both
syscalls overlapped inside the kernel. Outcome counters are reported exactly
and may contain only one linearization class. Deterministic mutex/transition
serialization evidence comes from the frozen C2.1
[deterministic injected-fake contention handshakes](../../docs/results/2026-08-28-c2-1-a-prime-fake-provider.md);
this tool adds repeated real-iommufd stress and rejects every outcome outside
the documented classes. It runs exactly 24 UNMAP rounds and 12 rounds for each
transition kind, so it is bounded stress rather than an exhaustive proof of
all scheduler or in-kernel overlap interleavings.

`--hold-open DEVICE` is a wrapper-only mode. It opens and binds the cdev,
prints `READY`, and waits for stdin EOF. The privileged script uses it to prove
that an ordinary, non-forced `rmmod` is rejected while a userspace owner holds
the device and succeeds only after that owner closes.

The frozen adapter emits one bounded driver-level diagnostic for the deliberate
duplicate DETACH. The wrapper requires that exact message once and continues to
reject kernel `WARN_ON`, Oops, lockdep, refcount, UAF and hung-task diagnostics.

Build and run the pure endian/record helper selftest without VFIO:

```sh
make -C tools/vfio-cdev-v1-c24
tools/vfio-cdev-v1-c24/build/vfio_cdev_v1_c24 --selftest
```

Privileged execution is time-bounded by
`tests/privileged/c2_4_vfio_cdev_v1.sh`. Neither the oracle nor its wrapper
opens the raw-media block device.
