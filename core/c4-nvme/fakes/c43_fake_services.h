/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C43_FAKE_SERVICES_H
#define FWLAB_C43_FAKE_SERVICES_H

#include "fwlab/portable/c4_command_graph.h"

#define C43_FAKE_EVENT_CAPACITY 32u

enum c43_fake_event {
    C43_FAKE_QUEUE_PREPARE_START = 1,
    C43_FAKE_QUEUE_PREPARE_QUERY = 2,
    C43_FAKE_QUEUE_FINISH_START = 3,
    C43_FAKE_QUEUE_FINISH_QUERY = 4,
    C43_FAKE_QUEUE_CANCEL = 5,
    C43_FAKE_QUEUE_RETIRE = 6,
    C43_FAKE_QUEUE_RESET_BEGIN = 7,
    C43_FAKE_QUEUE_QUIESCENT = 8,
    C43_FAKE_TARGET_SUBMIT = 9,
    C43_FAKE_TARGET_QUERY = 10,
    C43_FAKE_TARGET_CANCEL = 11,
    C43_FAKE_TARGET_RELEASE = 12,
    C43_FAKE_TARGET_RELEASE_QUERY = 13,
    C43_FAKE_TARGET_RESET_BEGIN = 14,
    C43_FAKE_TARGET_QUIESCENT = 15,
    C43_FAKE_BLOCK_SUBMIT = 16,
    C43_FAKE_BLOCK_QUERY = 17,
    C43_FAKE_BLOCK_CANCEL = 18,
    C43_FAKE_BLOCK_RETIRE = 19,
    C43_FAKE_BLOCK_RESET_BEGIN = 20,
    C43_FAKE_BLOCK_QUIESCENT = 21
};

struct c43_fake_services {
    uint32_t event_count;
    uint8_t overflow;
    uint8_t reserved[3];
    uint32_t events[C43_FAKE_EVENT_CAPACITY];
};

void c43_fake_services_init(struct c43_fake_services *services);
void c43_fake_services_providers(
    struct c43_fake_services *services,
    struct fwlab_c43_graph_providers *providers
);

#endif /* FWLAB_C43_FAKE_SERVICES_H */
