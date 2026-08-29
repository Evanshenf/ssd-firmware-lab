/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C34_FILE_INTERNAL_H
#define FWLAB_C34_FILE_INTERNAL_H

#include "c34_file_media.h"

#include <stdbool.h>

#define C34F_MAGIC UINT64_C(0x43333446494c4531)
#define C34F_BLOCKS 6u
#define C34F_PAGES_PER_BLOCK 4u
#define C34F_PAGES 24u
#define C34F_MAIN_BYTES 96u
#define C34F_OOB_BYTES 64u
#define C34F_SUPER_BYTES 4096u
#define C34F_CHECKPOINT_BYTES 8192u
#define C34F_WAL_SEGMENT_BYTES 4096u
#define C34F_WAL_HEADER_BYTES 256u
#define C34F_WAL_RECORD_BYTES 256u
#define C34F_WAL_RECORDS 15u
#define C34F_WAL_SEGMENTS 3u
#define C34F_PAGE_SLOT_BYTES 512u
#define C34F_HEALTH_SLOT_BYTES 256u

#define C34F_SB0_OFFSET UINT32_C(0x0000)
#define C34F_SB1_OFFSET UINT32_C(0x1000)
#define C34F_CP0_OFFSET UINT32_C(0x2000)
#define C34F_CP1_OFFSET UINT32_C(0x4000)
#define C34F_WAL0_OFFSET UINT32_C(0x6000)
#define C34F_PAGE_OFFSET UINT32_C(0x9000)
#define C34F_HEALTH_OFFSET UINT32_C(0xf000)

#define C34F_MARKER UINT32_C(0xc34fc0de)
#define C34F_SB_MAGIC UINT32_C(0x42533446)
#define C34F_CP_MAGIC UINT32_C(0x50433446)
#define C34F_WAL_MAGIC UINT32_C(0x4c573446)
#define C34F_REC_MAGIC UINT32_C(0x52573446)
#define C34F_PAGE_MAGIC UINT32_C(0x47503446)
#define C34F_HEALTH_MAGIC UINT32_C(0x48423446)

enum c34f_wal_type {
    C34F_WAL_BEGIN = 1,
    C34F_WAL_A_APPLIED = 2,
    C34F_WAL_A_NO_EFFECT = 3,
    C34F_WAL_C_COMMIT = 4
};

struct c34f_page {
    uint8_t main[C34F_MAIN_BYTES];
    uint8_t oob[C34F_OOB_BYTES];
    struct fwlab_nand_page_info info;
};

struct c34f_delta {
    uint8_t page_count;
    uint8_t page_index[C34F_PAGES_PER_BLOCK];
    uint8_t page_slot[C34F_PAGES_PER_BLOCK];
    uint8_t health_valid;
    uint8_t health_block;
    uint8_t health_slot;
    uint8_t operation_kind;
    uint8_t physical_outcome;
    uint8_t integrity;
    uint8_t applied_region_mask;
    uint32_t applied_main_bytes;
    uint32_t applied_oob_bytes;
    uint32_t applied_pages;
    uint16_t base_erase_generation;
    uint16_t final_erase_generation;
    uint32_t page_hash[C34F_PAGES_PER_BLOCK];
    uint32_t health_hash;
};

struct c34f_wal_record {
    uint8_t type;
    uint8_t reserved[7];
    uint64_t lsn;
    uint64_t op_id;
    uint64_t commit_sequence;
    uint64_t previous_hash;
    uint64_t begin_lsn;
    uint64_t applied_lsn;
    uint64_t old_physical_generation;
    uint64_t new_physical_generation;
    struct fwlab_nfc_operation_token inner;
    struct fwlab_nfc_ppa ppa;
    uint64_t payload_digest;
    uint64_t identity_hash;
    struct c34f_delta delta;
};

struct c34_file_media {
    uint64_t magic;
    struct c34_file_substrate substrate;
    uint8_t uuid[16];
    uint64_t super_generation;
    uint64_t checkpoint_generation;
    uint64_t physical_generation;
    uint64_t covered_lsn;
    uint64_t next_lsn;
    uint64_t next_op_id;
    uint64_t next_commit_sequence;
    uint64_t previous_record_hash;
    uint32_t wal_epoch;
    uint8_t active_super;
    uint8_t active_checkpoint;
    uint8_t active_segment;
    uint8_t active_record;
    uint8_t page_slot[C34F_PAGES];
    uint8_t health_slot[C34F_BLOCKS];
    uint64_t page_slot_lsn[C34F_PAGES][2];
    uint64_t health_slot_lsn[C34F_BLOCKS][2];
    uint64_t page_slot_generation[C34F_PAGES][2];
    uint64_t health_slot_generation[C34F_BLOCKS][2];
    struct c34f_page page[C34F_PAGES];
    struct fwlab_nand_block_info block[C34F_BLOCKS];
    struct c34_physical_binding binding;
    struct c34_physical_receipt receipt;
    uint8_t binding_used;
    uint8_t receipt_used;
    uint8_t stopped;
    uint8_t reserved0[5];
    int platform_fd;
};

uint32_t c34f_crc32c(const uint8_t *bytes, size_t length);
uint64_t c34f_hash_bytes(uint64_t hash, const uint8_t *bytes, size_t length);
void c34f_put_u16(uint8_t *bytes, uint16_t value);
void c34f_put_u32(uint8_t *bytes, uint32_t value);
void c34f_put_u64(uint8_t *bytes, uint64_t value);
uint16_t c34f_get_u16(const uint8_t *bytes);
uint32_t c34f_get_u32(const uint8_t *bytes);
uint64_t c34f_get_u64(const uint8_t *bytes);

enum c34_file_result c34f_read(
    const struct c34_file_media *media,
    uint64_t offset,
    uint8_t *bytes,
    size_t length
);
enum c34_file_result c34f_write(
    struct c34_file_media *media,
    uint64_t offset,
    const uint8_t *bytes,
    size_t length
);
enum c34_file_result c34f_barrier(struct c34_file_media *media);

enum c34_file_result c34f_write_checkpoint(
    struct c34_file_media *media,
    uint8_t slot,
    uint64_t generation,
    uint64_t covered_lsn
);
enum c34_file_result c34f_load_checkpoint(
    struct c34_file_media *media,
    uint8_t slot,
    uint64_t expected_generation,
    uint32_t expected_hash
);
enum c34_file_result c34f_write_super(
    struct c34_file_media *media,
    uint8_t copy,
    uint64_t generation,
    uint8_t checkpoint_slot,
    uint64_t checkpoint_generation,
    uint32_t checkpoint_hash,
    uint64_t covered_lsn,
    uint32_t wal_epoch
);
enum c34_file_result c34f_select_super(
    struct c34_file_media *media,
    uint8_t *copy,
    uint64_t *generation,
    uint8_t *checkpoint_slot,
    uint64_t *checkpoint_generation,
    uint32_t *checkpoint_hash,
    uint64_t *covered_lsn,
    uint32_t *wal_epoch
);

enum c34_file_result c34f_recycle_wal(struct c34_file_media *media);
enum c34_file_result c34f_append_wal(
    struct c34_file_media *media,
    struct c34f_wal_record *record,
    uint64_t *record_hash
);
enum c34_file_result c34f_recover_wal(struct c34_file_media *media);
enum c34_file_result c34f_apply_commit(
    struct c34_file_media *media,
    const struct c34f_wal_record *record
);

enum c34_file_result c34f_write_page_candidate(
    struct c34_file_media *media,
    uint8_t page_index,
    uint8_t slot,
    uint64_t source_op_id,
    uint64_t begin_lsn,
    const struct c34f_page *page,
    uint32_t *candidate_hash
);
enum c34_file_result c34f_write_health_candidate(
    struct c34_file_media *media,
    uint8_t block,
    uint8_t slot,
    uint64_t source_op_id,
    uint64_t begin_lsn,
    const struct fwlab_nand_block_info *health,
    uint32_t *candidate_hash
);
enum c34_file_result c34f_load_page_candidate(
    struct c34_file_media *media,
    uint8_t page_index,
    uint8_t slot,
    uint32_t expected_hash,
    struct c34f_page *page
);
enum c34_file_result c34f_load_health_candidate(
    struct c34_file_media *media,
    uint8_t block,
    uint8_t slot,
    uint32_t expected_hash,
    struct fwlab_nand_block_info *health
);

#endif
