/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_NFC_TEST_SUPPORT_H
#define FWLAB_NFC_TEST_SUPPORT_H

#include <stdalign.h>
#include <stdint.h>

#include "../fakes/nfc_buffer.h"
#include "../fakes/nfc_memory_media.h"
#include "fwlab/portable/nfc_model.h"

#define C33_TEST_ARENA_BYTES 131072u

union c33_test_arena {
    max_align_t alignment;
    uint8_t bytes[C33_TEST_ARENA_BYTES];
};

struct c33_test_environment {
    union c33_test_arena model_arena;
    union c33_test_arena media_arena;
    struct c33_fake_buffer buffer;
    struct c33_memory_media *memory;
    struct fwlab_nfc_model *model;
    struct fwlab_nfc_provider provider;
    uint64_t next_uid;
    uint64_t instance_nonce;
    uint32_t current_epoch;
};

struct fwlab_nfc_model_config c33_test_config(void);

int c33_test_init(
    struct c33_test_environment *environment,
    const struct fwlab_nfc_model_config *config,
    const struct fwlab_nfc_factory_bad *factory_bad,
    size_t factory_bad_count,
    uint64_t instance_nonce
);

struct fwlab_nfc_request c33_test_request(
    struct c33_test_environment *environment,
    uint8_t kind,
    struct fwlab_nfc_ppa ppa
);

int c33_test_run_event(
    struct c33_test_environment *environment,
    const struct fwlab_nfc_request *request,
    struct fwlab_nfc_completion *completion
);

int c33_test_program(
    struct c33_test_environment *environment,
    struct fwlab_nfc_ppa ppa,
    const uint8_t main[2],
    uint8_t oob,
    struct fwlab_nfc_completion *completion
);

int c33_test_read(
    struct c33_test_environment *environment,
    struct fwlab_nfc_ppa ppa,
    uint8_t retry_step,
    uint8_t main[2],
    uint8_t *oob,
    struct fwlab_nfc_completion *completion
);

int c33_test_erase(
    struct c33_test_environment *environment,
    struct fwlab_nfc_ppa ppa,
    struct fwlab_nfc_completion *completion
);

#endif
