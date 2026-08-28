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

PR CI is intentionally unprivileged. It must not load kernel modules, use KVM, run as root, open raw block devices or access persistent lab hosts.

Behavior observed from a third-party implementation may inform a test oracle. It must not be used to disguise copied code, translated code or a line-by-line AI rewrite as original BSD source.
