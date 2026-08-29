/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C34_ORACLE_H
#define FWLAB_C34_ORACLE_H

#include <stddef.h>
#include <stdint.h>

#define C34O_FAMILIES 13u
#define C34O_INVARIANTS 18u

enum c34o_action {
    C34O_CAPTURE = UINT32_C(1) << 0,
    C34O_DATA_C = UINT32_C(1) << 1,
    C34O_MAP_C = UINT32_C(1) << 2,
    C34O_VOLATILE_SUCCESS = UINT32_C(1) << 3,
    C34O_DURABLE_SUCCESS = UINT32_C(1) << 4,
    C34O_TRIM_C = UINT32_C(1) << 5,
    C34O_GC_COPY = UINT32_C(1) << 6,
    C34O_RELOCATION_C = UINT32_C(1) << 7,
    C34O_RELEASE_LEASE = UINT32_C(1) << 8,
    C34O_ERASE_SOURCE = UINT32_C(1) << 9,
    C34O_CHECKPOINT_IMAGE = UINT32_C(1) << 10,
    C34O_CHECKPOINT_ANCHOR = UINT32_C(1) << 11,
    C34O_FENCE_SUCCESS = UINT32_C(1) << 12,
    C34O_RESET = UINT32_C(1) << 13,
    C34O_OLD_EVENT = UINT32_C(1) << 14,
    C34O_HOST_CACHE = UINT32_C(1) << 15,
    C34O_SYNC = UINT32_C(1) << 16,
    C34O_NFC_EVENT = UINT32_C(1) << 17,
    C34O_TORN_DATA = UINT32_C(1) << 18,
    C34O_TORN_MAP = UINT32_C(1) << 19,
    C34O_SECOND_INSTANCE = UINT32_C(1) << 20
};

enum c34o_broken {
    C34O_BROKEN_NONE = 0,
    C34O_BM_UNIQUE = 1,
    C34O_BM_TORN = 2,
    C34O_BM_DEPEND = 3,
    C34O_BM_DURABLE = 4,
    C34O_BM_VOLATILE = 5,
    C34O_BM_TRIM = 6,
    C34O_BM_GC = 7,
    C34O_BM_CHECKPOINT = 8,
    C34O_BM_EPOCH = 9,
    C34O_BM_FENCE = 10,
    C34O_BM_PLP = 11,
    C34O_BM_CONSERVE = 12,
    C34O_BM_HOST = 13,
    C34O_BM_WITNESS = 14,
    C34O_BM_PHYS_FACT = 15,
    C34O_BM_OWNERSHIP = 16,
    C34O_BM_REPLACE = 17,
    C34O_BM_INSTANCE = 18
};

struct c34o_metrics {
    uint32_t families;
    uint32_t states;
    uint32_t cuts;
    uint32_t terminals;
    uint32_t invariant_mask;
    uint64_t hash;
};

int c34o_run_positive(struct c34o_metrics *metrics);
int c34o_run_broken(
    enum c34o_broken broken,
    uint32_t *minimal_depth,
    uint64_t *counterexample_hash
);

#endif
