<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Contributing

The repository is design-first. Open an issue or ADR before changing a public ABI, persistence format, ownership transition, trust boundary or authenticity claim.

Each pull request must:

1. stay within the license and source boundaries in [REFERENCE_POLICY.md](REFERENCE_POLICY.md);
2. add per-file SPDX headers and provenance for external references;
3. state `AI-assisted: yes` or `AI-assisted: no`;
4. name the affected files, human owner and tests actually run;
5. avoid credentials, private infrastructure, restricted documents and raw model transcripts;
6. include a Developer Certificate of Origin sign-off (`git commit -s`).

For AI-assisted changes, describe the tool/model and date, the categories of input material, and the human verification performed. Private prompts are not required. A digest may be recorded locally, but do not publish confidential inputs.

Run:

```sh
make check
```

PR test execution is intentionally unprivileged. Tests must not load kernel
modules, use KVM, run as root, open raw block devices or access persistent lab
hosts. Privileged CI setup is limited to installing toolchains and mounting or
unmounting the job's dedicated, disposable 1 GiB tmpfs on an ephemeral hosted
runner. This is not permission to change a persistent lab mount or device.

Run the current-spine entry as an ordinary user with an existing, writable tmpfs
of at most 1 GiB and at least 256 MiB free:

```sh
FWLAB_TEST_MEDIA_DIR=/run/fwlab-test-media sh scripts/check_current_spine.sh
```

The entry checks the mount and fails rather than falling back to disk. It runs
media tests serially, retains synchronization and locking, and reports stage,
elapsed time, media usage and available memory; CI also captures peak RSS with
`/usr/bin/time -v`. Executables and logs stay off tmpfs. Historical J0 reference
fixtures retain their existing temporary byte stores; the new scaled/POSIX
entries use the explicit tmpfs mount. The selected storage
smokes do not repeat the completed full-capacity campaigns or claim disk
durability, host-power-loss recovery or SSD performance. The ARM64 additions
exercise only the current PAGE2/physical-media in-process fixtures under
QEMU user mode; they are not ARM native M4/M5 qualification.

Behavior observed from a third-party implementation may inform a test oracle. It must not be used to disguise copied code, translated code or a line-by-line AI rewrite as original BSD source.
