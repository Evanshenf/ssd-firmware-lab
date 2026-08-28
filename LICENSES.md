<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# License policy

| Content | Default SPDX identifier |
|---|---|
| Original user-space source, shared ABI headers, schemas, scripts, build files, CI and tests | `BSD-3-Clause` |
| Linux kernel source under `kernel/` | `GPL-2.0-only` |
| Markdown architecture, governance and user documentation | `CC-BY-4.0` |
| Third-party material | Its original license; explicit provenance required |

The root [LICENSE](LICENSE) is BSD-3-Clause so GitHub displays the default source-code license. It does not override a file's SPDX identifier.

Compilable examples follow their target: user-space examples are BSD-3-Clause and kernel examples are GPL-2.0-only. CC-BY-4.0 is for documentation, not copied into source code.

Full license texts are in `LICENSES/`. Every non-license file must carry `SPDX-FileCopyrightText` and `SPDX-License-Identifier` headers in a syntax accepted by that file type.
