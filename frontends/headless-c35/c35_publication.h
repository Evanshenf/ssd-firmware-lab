/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_PUBLICATION_H
#define FWLAB_C35_PUBLICATION_H

#include "c35_binding.h"

#define C35_PUBLICATION_VERSION 2u

enum c35_publication_kind {
    C35_PUBLICATION_DMA = 1,
    C35_PUBLICATION_COMMAND = 2,
    C35_PUBLICATION_RESET = 3,
    C35_PUBLICATION_STALE = 4,
    C35_PUBLICATION_RECOVERY = 5,
    C35_PUBLICATION_TEARDOWN = 6,
    C35_PUBLICATION_CLEANUP = 7
};

struct c35_publication {
    uint16_t version;
    uint16_t size;
    uint8_t kind;
    uint8_t actor;
    uint8_t request_kind;
    uint8_t api_result;
    uint8_t terminal;
    uint8_t completion_result;
    uint8_t effect_class;
    uint8_t witness_class;
    uint8_t witness_reason;
    uint8_t status;
    uint8_t atom_mask;
    uint8_t present_mask;
    uint32_t epoch;
    uint32_t commit_state;
    uint64_t publication_uid;
    uint32_t reserved[2];
    struct c35_semantic_result semantic;
};

#endif
