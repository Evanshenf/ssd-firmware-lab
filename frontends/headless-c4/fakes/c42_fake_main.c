/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>

#include "tests/c42_support.h"

int main(void)
{
    struct c42_test_fixture fixture;
    struct c42_snapshot snapshot = {0};

    if (!c42_test_fixture_init(&fixture, 4, 1) ||
        !c42_test_submit(&fixture, 1, 0, 1, 0x1234) ||
        !c42_test_run(&fixture, 32, 4) ||
        c42_snapshot_read(fixture.controller, &snapshot) != C42_OK) {
        return 1;
    }
    printf("c42-fake-v1 phase=%u epoch=%u sq=%u/%u cq=%u/%u/%u "
           "active=%u notify=%u capture=%u acquire=%u\n",
           snapshot.phase, snapshot.controller_epoch,
           snapshot.sq[1].device_index,
           snapshot.sq[1].pending_or_unacked,
           snapshot.cq[1].device_index,
           snapshot.cq[1].pending_or_unacked,
           snapshot.cq[1].phase,
           snapshot.active_commands,
           snapshot.pending_notifications,
           fixture.memory.capture_count,
           fixture.command.acquire_count);
    return 0;
}
