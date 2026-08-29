/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_oracle.h"

#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    uint64_t aggregate = UINT64_C(1469598103934665603);
    unsigned int broken;
    uint32_t depth_sum = 0;

    for (broken = C34O_BM_UNIQUE; broken <= C34O_BM_INSTANCE; ++broken) {
        uint32_t depth;
        uint64_t hash;
        unsigned int byte;

        if (!c34o_run_broken((enum c34o_broken)broken, &depth, &hash)) {
            return 1;
        }
        depth_sum += depth;
        for (byte = 0; byte < 8; ++byte) {
            aggregate ^= (uint8_t)(hash >> (byte * 8u));
            aggregate *= UINT64_C(1099511628211);
        }
    }
    puts("C3.4 broken integration variants: PASS");
    printf("  shortest-counterexamples=%u depth-sum=%" PRIu32
           " hash=%016" PRIx64 "\n",
           C34O_INVARIANTS, depth_sum, aggregate);
    return 0;
}
