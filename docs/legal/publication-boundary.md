<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Third-party publication boundary

This is an engineering source-hygiene policy, not legal advice.

`ssd-firmware-lab` is open source and is not currently intended for sale. It does not need recognition, endorsement or certification from NVM Express to publish independently authored work.

Open-source or non-commercial status does not, by itself, replace the license terms of a third-party document, codebase, logo or trademark. The repository therefore applies a narrow publication boundary:

- independently authored architecture, generic HIF/capability contracts, NAND/NFC/media models, persistence algorithms and test infrastructure may be published;
- official specification PDFs, logos, certification marks and near-verbatim tables/text are not committed;
- implementation contributions state their provenance and license and receive human source-boundary review;
- the project does not claim official certification, endorsement or recognition;
- interoperability behavior and public operating-system interfaces may be tested without presenting a third-party implementation as project source.

This boundary is not a blanket ban on implementing an interoperable device. It is a review rule that keeps original code, third-party material and marketing claims distinguishable. The absence of an official-recognition requirement does not itself decide the document-license, implementation-rights or patent basis of a particular contribution; that scoped question remains review-required.

The first protocol-code boundary review is recorded in
[the C4.1 source-boundary review](c4-1-source-boundary-review.md).
