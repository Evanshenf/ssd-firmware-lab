/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_file_test_support.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static const uint8_t uuid[16] = {
    0x34, 0x04, 0x20, 0x26, 0x08, 0x29, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
};

static int program_page(
    struct c34_file_media *media,
    uint64_t identity,
    struct fwlab_nfc_ppa ppa,
    uint8_t fill,
    struct c34_physical_receipt *receipt
)
{
    struct fwlab_nand_media nand = c34_file_nand_media(media);
    struct c34_physical_txn_provider physical =
        c34_file_txn_provider(media);
    struct fwlab_nfc_operation_token inner;
    struct fwlab_nand_media_result result;
    uint8_t main[C34F_MAIN_BYTES];
    uint8_t oob[C34F_OOB_BYTES];

    memset(main, fill, sizeof(main));
    memset(oob, (uint8_t)(fill ^ 0xffu), sizeof(oob));
    CHECK(c34f_test_bind_program(media, identity, ppa, main, oob, &inner));
    if (nand.ops->program(
            nand.context, &ppa, main, sizeof(main), oob, sizeof(oob),
            sizeof(main), sizeof(oob), FWLAB_NFC_INTEGRITY_COMPLETE,
            &result) != FWLAB_NFC_API_OK) {
        return 0;
    }
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_APPLIED &&
          result.integrity == FWLAB_NFC_INTEGRITY_COMPLETE);
    CHECK(physical.ops->receipt(physical.context, &inner, receipt) ==
          C34_PHYSICAL_TXN_OK);
    return 1;
}

static int test_format_program_restart(void)
{
    struct c34f_memory_substrate substrate;
    struct c34f_memory_substrate restart_substrate;
    struct c34_file_substrate provider;
    union c34f_test_arena arena;
    union c34f_test_arena restart_arena;
    struct c34_file_media *media;
    struct c34_file_media *restart;
    struct c34_physical_receipt receipt;
    struct fwlab_nand_media nand;
    struct fwlab_nfc_ppa ppa;
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
    uint8_t main[C34F_MAIN_BYTES];
    uint8_t oob[C34F_OOB_BYTES];
    uint64_t physical_hash;

    c34f_memory_substrate_init(&substrate);
    provider = c34f_memory_substrate_provider(&substrate);
    CHECK(c34_file_format(arena.bytes, sizeof(arena.bytes), &provider, uuid,
                          &media) == C34_FILE_OK);
    CHECK(substrate.size == C34_FILE_IMAGE_BYTES &&
          c34_file_image_hash(media) != 0);
    memset(&ppa, 0, sizeof(ppa));
    CHECK(program_page(media, 1, ppa, 0x5a, &receipt));
    CHECK(receipt.physical_generation == 1 &&
          receipt.applied_main_bytes == C34F_MAIN_BYTES);
    physical_hash = c34_file_physical_hash(media);
    c34f_memory_substrate_restart_image(&substrate, &restart_substrate);
    provider = c34f_memory_substrate_provider(&restart_substrate);
    CHECK(c34_file_restart(
              restart_arena.bytes, sizeof(restart_arena.bytes), &provider,
              uuid, &restart) == C34_FILE_OK);
    CHECK(c34_file_physical_hash(restart) == physical_hash);
    nand = c34_file_nand_media(restart);
    CHECK(nand.ops->read_page(
              nand.context, &ppa, main, sizeof(main), oob, sizeof(oob),
              &page, &block) == FWLAB_NFC_API_OK);
    CHECK(page.state == FWLAB_NAND_PAGE_VALID && page.program_count == 1 &&
          main[0] == 0x5a && main[95] == 0x5a && oob[0] == 0xa5 &&
          block.next_program_page == 1);
    return 1;
}

static int test_physical_checkpoint(void)
{
    struct c34f_memory_substrate substrate;
    struct c34f_memory_substrate restart_substrate;
    struct c34_file_substrate provider;
    union c34f_test_arena arena;
    union c34f_test_arena restart_arena;
    struct c34_file_media *media;
    struct c34_file_media *restart;
    struct c34_physical_receipt receipt;
    struct fwlab_nfc_ppa ppa;
    uint64_t hash;

    c34f_memory_substrate_init(&substrate);
    provider = c34f_memory_substrate_provider(&substrate);
    CHECK(c34_file_format(arena.bytes, sizeof(arena.bytes), &provider, uuid,
                          &media) == C34_FILE_OK);
    memset(&ppa, 0, sizeof(ppa));
    CHECK(program_page(media, 1, ppa, 0x6b, &receipt));
    CHECK(c34_file_physical_checkpoint(media) == C34_FILE_OK);
    hash = c34_file_physical_hash(media);
    c34f_memory_substrate_restart_image(&substrate, &restart_substrate);
    provider = c34f_memory_substrate_provider(&restart_substrate);
    CHECK(c34_file_restart(
              restart_arena.bytes, sizeof(restart_arena.bytes), &provider,
              uuid, &restart) == C34_FILE_OK);
    CHECK(c34_file_physical_hash(restart) == hash &&
          restart->covered_lsn == 3 && restart->wal_epoch == 2);
    return 1;
}

int main(void)
{
    CHECK(test_format_program_restart());
    CHECK(test_physical_checkpoint());
    puts("C3.4 file contract: PASS (2 cases)");
    return 0;
}
