/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_FILE_NAND_V0_H
#define FWLAB_FILE_NAND_V0_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/contracts/nand_media.h"

#define FWLAB_FILE_NAND_V0_VERSION 1u
#define FWLAB_FILE_NAND_V0_IMAGE_BYTES UINT64_C(0x816000)

struct fwlab_file_nand_v0;

struct fwlab_file_nand_holder_v0 {
    uint64_t device;
    uint64_t inode;
    uint8_t media_uuid[16];
};

enum fwlab_file_nand_cut_phase_v0 {
    FWLAB_FILE_NAND_CUT_BEFORE_EFFECT_V0 = 1,
    FWLAB_FILE_NAND_CUT_AFTER_EFFECT_V0 = 2
};

enum fwlab_file_nand_cut_state_v0 {
    FWLAB_FILE_NAND_CUT_DISARMED_V0 = 0,
    FWLAB_FILE_NAND_CUT_ARMED_V0 = 1,
    FWLAB_FILE_NAND_CUT_FIRED_V0 = 2
};

struct fwlab_file_nand_cut_key_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint64_t expected_transaction_uid;
    uint8_t phase;
    uint8_t one_shot;
    uint8_t reserved1[6];
};

struct fwlab_file_nand_cut_status_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_file_nand_cut_key_v0 key;
    struct fwlab_nfc_ppa ppa;
    uint8_t state;
    uint8_t reserved1[3];
    uint64_t old_physical_generation;
    uint64_t new_physical_generation;
    uint64_t begin_lsn;
    uint64_t applied_lsn;
    uint32_t reserved2[4];
};

struct fwlab_file_nand_receipt_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint8_t media_uuid[16];
    uint64_t physical_generation;
    uint64_t next_transaction_uid;
    uint64_t next_lsn;
    uint32_t wal_epoch;
    uint16_t transactions_in_bank;
    uint8_t active_wal_bank;
    uint8_t selected_checkpoint_copy;
    uint8_t quarantined;
    uint8_t reserved1[7];
    uint32_t reserved2[4];
};

size_t fwlab_file_nand_v0_arena_alignment(void);
size_t fwlab_file_nand_v0_arena_size(void);

struct fwlab_nfc_geometry fwlab_file_nand_v0_geometry(void);

enum fwlab_nfc_api_result fwlab_file_nand_v0_posix_format(
    void *arena,
    size_t arena_size,
    int directory_fd,
    const char *name,
    const uint8_t media_uuid[16],
    struct fwlab_file_nand_v0 **media,
    struct fwlab_file_nand_holder_v0 *holder
);

enum fwlab_nfc_api_result fwlab_file_nand_v0_posix_restart(
    void *arena,
    size_t arena_size,
    int directory_fd,
    const char *name,
    const struct fwlab_file_nand_holder_v0 *holder,
    struct fwlab_file_nand_v0 **media
);

struct fwlab_nand_media fwlab_file_nand_v0_media(
    struct fwlab_file_nand_v0 *media
);

enum fwlab_nfc_api_result fwlab_file_nand_v0_cut_arm(
    struct fwlab_file_nand_v0 *media,
    const struct fwlab_file_nand_cut_key_v0 *key
);

enum fwlab_nfc_api_result fwlab_file_nand_v0_cut_query(
    const struct fwlab_file_nand_v0 *media,
    struct fwlab_file_nand_cut_status_v0 *status
);

enum fwlab_nfc_api_result fwlab_file_nand_v0_receipt(
    const struct fwlab_file_nand_v0 *media,
    struct fwlab_file_nand_receipt_v0 *receipt
);

enum fwlab_nfc_api_result fwlab_file_nand_v0_close(
    struct fwlab_file_nand_v0 *media
);

#endif /* FWLAB_FILE_NAND_V0_H */
