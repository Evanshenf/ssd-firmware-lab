<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# AI-assisted development policy

AI may help explore designs, draft small changes, generate tests and review diffs. It is never the authority for protocol meaning, licensing, security or release readiness.

An AI-assisted PR records:

- tool/model family and date;
- input-material categories and, when useful, a non-secret digest;
- files materially affected;
- the responsible human reviewer;
- tests and source checks actually performed.

Do not provide a model with credentials, private source, NDA material, unpublished vulnerabilities or a restricted specification. Do not ask a model to transform GPL or otherwise incompatible source into a BSD “equivalent.” Raw prompts and answers are private working material unless deliberately redacted, reviewed and relicensed.

Generated output is treated as untrusted input: inspect it, establish provenance, run tests and take human responsibility before committing it.
