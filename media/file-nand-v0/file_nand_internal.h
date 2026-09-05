/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_FILE_NAND_V0_INTERNAL_H
#define FWLAB_FILE_NAND_V0_INTERNAL_H

#include "file_nand.h"

#include <stddef.h>
#include <stdint.h>

#define FILE_NAND_MAGIC UINT64_C(0x464e56304d454449)
#define FILE_NAND_SECTOR_BYTES UINT64_C(4096)
#define FILE_NAND_SUPER_BYTES UINT64_C(4096)
#define FILE_NAND_PHYSICAL_CP_BYTES UINT64_C(4096)
#define FILE_NAND_WAL_RECORD_BYTES UINT64_C(512)
#define FILE_NAND_WAL_SEGMENT_BYTES UINT64_C(8192)
#define FILE_NAND_WAL_RECORDS_PER_SEGMENT 15u
#define FILE_NAND_WAL_SEGMENTS_PER_BANK 4u
#define FILE_NAND_WAL_BANKS 2u
#define FILE_NAND_TXNS_BEFORE_RECYCLE 16u
#define FILE_NAND_PAGE_SLOT_BYTES UINT64_C(8192)
#define FILE_NAND_HEALTH_SLOT_BYTES UINT64_C(256)
/* Finite lab envelope; transaction IDs retain their existing on-media width. */
#define FILE_NAND_MAX_PHYSICAL_TXNS UINT64_C(65536)
#define FILE_NAND_PAGE_COUNT 512u
#define FILE_NAND_BLOCK_COUNT 16u
#define FILE_NAND_PAGES_PER_BLOCK 32u
#define FILE_NAND_MAIN_BYTES 4096u
#define FILE_NAND_OOB_BYTES 128u

#define FILE_NAND_SUPER0 UINT64_C(0x0000)
#define FILE_NAND_SUPER1 UINT64_C(0x1000)
#define FILE_NAND_CP0 UINT64_C(0x2000)
#define FILE_NAND_CP1 UINT64_C(0x3000)
#define FILE_NAND_WAL0 UINT64_C(0x4000)
#define FILE_NAND_WAL_BANK_BYTES UINT64_C(0x8000)
#define FILE_NAND_WAL1 UINT64_C(0xc000)
#define FILE_NAND_PAGE_CANDIDATES UINT64_C(0x14000)
#define FILE_NAND_HEALTH_CANDIDATES UINT64_C(0x814000)

#define FILE_NAND_PHASE_MARKER UINT32_C(0xc04d17ed)
#define FILE_NAND_SUPER_MAGIC UINT32_C(0x464e5331)
#define FILE_NAND_CP_MAGIC UINT32_C(0x464e4331)
#define FILE_NAND_PAGE_MAGIC UINT32_C(0x464e5031)
#define FILE_NAND_HEALTH_MAGIC UINT32_C(0x464e4831)
#define FILE_NAND_WAL_SEGMENT_MAGIC UINT32_C(0x464e5731)
#define FILE_NAND_WAL_RECORD_MAGIC UINT32_C(0x464e5231)

enum file_nand_wal_kind {
    FILE_NAND_WAL_BEGIN = 1,
    FILE_NAND_WAL_APPLIED = 2,
    FILE_NAND_WAL_NO_EFFECT = 3,
    FILE_NAND_WAL_COMMIT = 4,
    FILE_NAND_WAL_ROLLBACK = 5
};

enum file_nand_mutation_kind {
    FILE_NAND_MUTATION_PROGRAM = 1,
    FILE_NAND_MUTATION_ERASE = 2,
    FILE_NAND_MUTATION_MARK_BAD = 3
};

struct file_nand_identity {
    uint64_t device;
    uint64_t inode;
};

typedef enum fwlab_nfc_api_result (*file_nand_size_fn)(
    void *context, uint64_t *size);
typedef enum fwlab_nfc_api_result (*file_nand_resize_fn)(
    void *context, uint64_t size);
typedef enum fwlab_nfc_api_result (*file_nand_read_fn)(
    void *context, uint64_t offset, void *buffer, size_t size);
typedef enum fwlab_nfc_api_result (*file_nand_write_fn)(
    void *context, uint64_t offset, const void *buffer, size_t size);
typedef enum fwlab_nfc_api_result (*file_nand_barrier_fn)(void *context);
typedef enum fwlab_nfc_api_result (*file_nand_identity_fn)(
    void *context, struct file_nand_identity *identity);
typedef enum fwlab_nfc_api_result (*file_nand_close_fn)(void *context);

struct file_nand_substrate_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    file_nand_size_fn size_get;
    file_nand_resize_fn resize;
    file_nand_read_fn read;
    file_nand_write_fn write;
    file_nand_barrier_fn barrier;
    file_nand_identity_fn identity;
    file_nand_close_fn close;
};

struct file_nand_substrate {
    const struct file_nand_substrate_ops *ops;
    void *context;
};

struct file_nand_candidate_desc {
    uint16_t linear_page;
    uint8_t slot;
    uint8_t kind;
    uint32_t generation;
};

struct file_nand_transaction {
    uint8_t operation_kind;
    uint8_t physical_outcome;
    uint8_t integrity;
    uint8_t health_present;
    uint32_t applied_main_bytes;
    uint32_t applied_oob_bytes;
    uint16_t applied_pages;
    uint16_t candidate_count;
    uint16_t base_erase_generation;
    uint16_t final_erase_generation;
    struct fwlab_nfc_ppa ppa;
    uint64_t transaction_uid;
    uint64_t old_generation;
    uint64_t new_generation;
    uint64_t payload_digest;
    struct file_nand_candidate_desc candidate[32];
    uint8_t health_block;
    uint8_t health_slot;
    uint16_t reserved0;
    uint32_t health_generation;
    uint32_t health_crc;
    uint64_t candidate_set_hash;
};

struct fwlab_file_nand_v0 {
    uint64_t magic;
    struct file_nand_substrate substrate;
    _Alignas(max_align_t) uint8_t substrate_storage[128];
    struct file_nand_identity holder;
    uint8_t media_uuid[16];
    uint8_t selected_page_slot[FILE_NAND_PAGE_COUNT];
    uint8_t selected_health_slot[FILE_NAND_BLOCK_COUNT];
    uint8_t checkpoint_page_slot[FILE_NAND_PAGE_COUNT];
    uint8_t checkpoint_health_slot[FILE_NAND_BLOCK_COUNT];
    uint64_t page_generation[FILE_NAND_PAGE_COUNT];
    uint64_t health_generation[FILE_NAND_BLOCK_COUNT];
    uint64_t physical_generation;
    uint64_t next_transaction_uid;
    uint64_t next_lsn;
    uint64_t previous_record_hash;
    uint64_t super_generation;
    uint64_t checkpoint_generation;
    uint64_t covered_lsn;
    uint32_t wal_epoch;
    uint16_t transactions_in_bank;
    uint16_t wal_record_count;
    uint8_t active_wal_bank;
    uint8_t selected_checkpoint_copy;
    uint8_t selected_super_copy;
    uint8_t initialized;
    uint8_t quarantined;
    uint8_t closed;
    uint8_t cut_state;
    struct fwlab_file_nand_cut_key_v0 cut_key;
    struct fwlab_file_nand_cut_status_v0 cut_status;
};

uint16_t file_nand_get_le16(const uint8_t *bytes);
uint32_t file_nand_get_le32(const uint8_t *bytes);
uint64_t file_nand_get_le64(const uint8_t *bytes);
void file_nand_put_le16(uint8_t *bytes, uint16_t value);
void file_nand_put_le32(uint8_t *bytes, uint32_t value);
void file_nand_put_le64(uint8_t *bytes, uint64_t value);
uint32_t file_nand_crc32c(const uint8_t *bytes, size_t size);
uint64_t file_nand_hash64(const uint8_t *bytes, size_t size);
int file_nand_bytes_zero(const void *value, size_t size);
int file_nand_geometry_valid(const struct fwlab_nfc_geometry *geometry);
int file_nand_ppa_valid(const struct fwlab_nfc_ppa *ppa);
uint16_t file_nand_linear_page(const struct fwlab_nfc_ppa *ppa);
uint64_t file_nand_page_slot_offset(uint16_t linear_page, uint8_t slot);
uint64_t file_nand_health_slot_offset(uint8_t block, uint8_t slot);

enum fwlab_nfc_api_result file_nand_encode_page_candidate(
    uint8_t bytes[8192],
    uint64_t generation,
    uint64_t transaction_uid,
    const struct fwlab_nfc_ppa *ppa,
    uint8_t slot,
    uint8_t state,
    uint8_t program_count,
    uint16_t erase_generation,
    const uint8_t main[4096],
    const uint8_t oob[128],
    uint32_t *candidate_crc
);
enum fwlab_nfc_api_result file_nand_decode_page_candidate(
    const uint8_t bytes[8192],
    uint16_t expected_linear_page,
    uint8_t expected_slot,
    struct fwlab_nand_page_info *page,
    uint8_t main[4096],
    uint8_t oob[128],
    uint64_t *generation,
    uint64_t *transaction_uid,
    uint32_t *candidate_crc
);
enum fwlab_nfc_api_result file_nand_encode_health_candidate(
    uint8_t bytes[256],
    uint64_t generation,
    uint64_t transaction_uid,
    uint8_t block,
    uint8_t slot,
    const struct fwlab_nand_block_info *info,
    uint32_t *candidate_crc
);
enum fwlab_nfc_api_result file_nand_decode_health_candidate(
    const uint8_t bytes[256],
    uint8_t expected_block,
    uint8_t expected_slot,
    struct fwlab_nand_block_info *info,
    uint64_t *generation,
    uint64_t *transaction_uid,
    uint32_t *candidate_crc
);

enum fwlab_nfc_api_result file_nand_read_selected_page(
    struct fwlab_file_nand_v0 *media,
    const struct fwlab_nfc_ppa *ppa,
    uint8_t main[4096],
    uint8_t oob[128],
    struct fwlab_nand_page_info *page,
    struct fwlab_nand_block_info *block
);
enum fwlab_nfc_api_result file_nand_read_selected_health(
    struct fwlab_file_nand_v0 *media,
    uint8_t block,
    struct fwlab_nand_block_info *info
);
enum fwlab_nfc_api_result file_nand_commit_transaction(
    struct fwlab_file_nand_v0 *media,
    struct file_nand_transaction *transaction,
    struct fwlab_nand_media_result *result
);
enum fwlab_nfc_api_result file_nand_prepare_mutation(
    struct fwlab_file_nand_v0 *media,
    const uint16_t *linear_pages,
    uint16_t page_count,
    int health_block
);
enum fwlab_nfc_api_result file_nand_engine_format(
    void *arena,
    size_t arena_size,
    const struct file_nand_substrate *substrate,
    const uint8_t media_uuid[16],
    struct fwlab_file_nand_v0 **media
);
enum fwlab_nfc_api_result file_nand_engine_restart(
    void *arena,
    size_t arena_size,
    const struct file_nand_substrate *substrate,
    const struct fwlab_file_nand_holder_v0 *holder,
    struct fwlab_file_nand_v0 **media
);
int file_nand_validate_live(struct fwlab_file_nand_v0 *media);

#endif /* FWLAB_FILE_NAND_V0_INTERNAL_H */
