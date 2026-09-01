<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# FTL restart POC

This is an exploratory, non-graduating smoke program for the existing C3.4
fixed-profile mapping stack.  It uses a newly created, immediately unlinked
64-KiB ordinary file and demonstrates:

1. two durable logical writes;
2. checkpoint and readback;
3. durable trim of atom 0 and overwrite of atom 1;
4. process-style close/reopen of the file-media engine;
5. reconstruction from physical page/OOB state;
6. tombstone preservation and payload recovery after restart.

It is not a production FTL, raw-device test, power-loss result, performance
benchmark, or Cycle/Milestone evidence.

```sh
make -C experiments/ftl-restart-poc check
```
