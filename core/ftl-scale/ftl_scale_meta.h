/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_FTL_SCALE_META_H
#define FWLAB_FTL_SCALE_META_H
enum sf_meta_phase {
    SF_M_IDLE = 0, SF_M_FORMAT_ERASE,
    SF_M_READ_ROOT_A, SF_M_READ_ROOT_B, SF_M_SELECT_ROOT,
    SF_M_READ_CP, SF_M_READ_HEADER_A, SF_M_READ_HEADER_B,
    SF_M_READ_JOURNAL_A, SF_M_READ_JOURNAL_B, SF_M_REPLAY_PAIR,
    SF_M_RECOVER_NORMALIZE,
    SF_M_CP_ERASE, SF_M_CP_BODY, SF_M_CP_HEADER_A, SF_M_CP_HEADER_B,
    SF_M_CP_ROOT, SF_M_CP_RETIRE,
    SF_M_JOURNAL_A, SF_M_JOURNAL_B, SF_M_JOURNAL_APPLY,
    SF_M_CLEAN_SELECT, SF_M_CLEAN_ERASE, SF_M_CLEAN_DONE
};
enum sf_meta_mode { SF_M_NORMAL = 0, SF_M_FORMAT, SF_M_RECOVER };
enum sf_meta_read_class { SF_READ_INVALID = 0, SF_READ_VALID, SF_READ_ERASED, SF_READ_ECC };
/* Fixed scratch only: streamed CP pages stay in the shared NFC staging frames. */
struct sf_meta {
    uint32_t phase;
    enum fwlab_spine_result_v0 result;
    struct sf_root roots[2];
    struct sf_root candidate;
    struct sf_record record;
    struct sf_record comparison;
    uint64_t digest;
    uint32_t ordinal;
    uint32_t erase_index;
    uint32_t return_phase;
    uint32_t cleanup_block;
    uint16_t cleanup_generation;
    uint8_t cleanup_health;
    uint8_t mode;
    uint8_t pending;
    uint8_t root_class[2];
    uint8_t rail_class[2];
    uint8_t tail;
    uint8_t degraded;
};
#endif
