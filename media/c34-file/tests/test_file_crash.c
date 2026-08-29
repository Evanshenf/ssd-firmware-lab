/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_file_test_support.h"
#include "c34_file_oracle.h"

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
    0x34, 0x04, 0xcc, 0x01, 0x08, 0x29, 0x10, 0x11,
    0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
};

static int issue_program(
    struct c34_file_media *media,
    uint64_t identity,
    int expect_success
)
{
    struct fwlab_nand_media nand = c34_file_nand_media(media);
    struct c34_physical_txn_provider physical =
        c34_file_txn_provider(media);
    struct fwlab_nfc_ppa ppa;
    struct fwlab_nfc_operation_token inner;
    struct fwlab_nand_media_result result;
    struct c34_physical_receipt receipt;
    uint8_t main[C34F_MAIN_BYTES];
    uint8_t oob[C34F_OOB_BYTES];
    enum fwlab_nfc_api_result api;

    memset(&ppa, 0, sizeof(ppa));
    memset(main, 0x7a, sizeof(main));
    memset(oob, 0xa7, sizeof(oob));
    CHECK(c34f_test_bind_program(media, identity, ppa, main, oob, &inner));
    api = nand.ops->program(
        nand.context, &ppa, main, sizeof(main), oob, sizeof(oob),
        sizeof(main), sizeof(oob), FWLAB_NFC_INTEGRITY_COMPLETE, &result);
    if (!expect_success) {
        return api != FWLAB_NFC_API_OK;
    }
    CHECK(api == FWLAB_NFC_API_OK &&
          physical.ops->receipt(physical.context, &inner, &receipt) ==
              C34_PHYSICAL_TXN_OK &&
          receipt.committed == 1);
    return 1;
}

static int first_page_valid(struct c34_file_media *media)
{
    struct fwlab_nand_media nand = c34_file_nand_media(media);
    struct fwlab_nfc_ppa ppa;
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
    uint8_t main[C34F_MAIN_BYTES];
    uint8_t oob[C34F_OOB_BYTES];

    memset(&ppa, 0, sizeof(ppa));
    return nand.ops->read_page(
               nand.context, &ppa, main, sizeof(main), oob, sizeof(oob),
               &page, &block) == FWLAB_NFC_API_OK &&
           page.state == FWLAB_NAND_PAGE_VALID && main[0] == 0x7a &&
           oob[0] == 0xa7;
}

static int test_bac_cuts(void)
{
    unsigned int stage;

    for (stage = 1; stage <= 4; ++stage) {
        struct c34f_memory_substrate substrate;
        struct c34f_memory_substrate restart_substrate;
        struct c34f_memory_substrate second_substrate;
        struct c34_file_substrate provider;
        union c34f_test_arena arena;
        union c34f_test_arena restart_arena;
        union c34f_test_arena second_arena;
        struct c34_file_media *media;
        struct c34_file_media *restart;
        struct c34_file_media *second;
        uint32_t base;
        uint64_t first_hash;
        struct c34fo_page0 oracle;

        c34f_memory_substrate_init(&substrate);
        provider = c34f_memory_substrate_provider(&substrate);
        CHECK(c34_file_format(
                  arena.bytes, sizeof(arena.bytes), &provider, uuid,
                  &media) == C34_FILE_OK);
        base = substrate.barriers;
        substrate.cut_after_barrier = base + stage;
        CHECK(issue_program(media, 1, 0));
        CHECK(c34fo_recover_page0(substrate.stable, uuid, &oracle));
        CHECK((oracle.state == FWLAB_NAND_PAGE_VALID) == (stage >= 2));
        c34f_memory_substrate_restart_image(&substrate, &restart_substrate);
        provider = c34f_memory_substrate_provider(&restart_substrate);
        CHECK(c34_file_restart(
                  restart_arena.bytes, sizeof(restart_arena.bytes), &provider,
                  uuid, &restart) == C34_FILE_OK);
        CHECK(first_page_valid(restart) == (stage >= 2));
        first_hash = c34_file_physical_hash(restart);
        c34f_memory_substrate_restart_image(
            &restart_substrate, &second_substrate);
        provider = c34f_memory_substrate_provider(&second_substrate);
        CHECK(c34_file_restart(
                  second_arena.bytes, sizeof(second_arena.bytes), &provider,
                  uuid, &second) == C34_FILE_OK);
        CHECK(c34_file_physical_hash(second) == first_hash);
    }
    return 1;
}

static int test_checkpoint_cuts(void)
{
    unsigned int stage;

    for (stage = 1; stage <= 3; ++stage) {
        struct c34f_memory_substrate substrate;
        struct c34f_memory_substrate restart_substrate;
        struct c34f_memory_substrate second_substrate;
        struct c34_file_substrate provider;
        union c34f_test_arena arena;
        union c34f_test_arena restart_arena;
        union c34f_test_arena second_arena;
        struct c34_file_media *media;
        struct c34_file_media *restart;
        struct c34_file_media *second;
        uint32_t base;
        struct c34fo_page0 oracle;

        c34f_memory_substrate_init(&substrate);
        provider = c34f_memory_substrate_provider(&substrate);
        CHECK(c34_file_format(
                  arena.bytes, sizeof(arena.bytes), &provider, uuid,
                  &media) == C34_FILE_OK);
        CHECK(issue_program(media, 1, 1));
        base = substrate.barriers;
        substrate.cut_after_barrier = base + stage;
        CHECK(c34_file_physical_checkpoint(media) == C34_FILE_CUT);
        CHECK(c34fo_recover_page0(substrate.stable, uuid, &oracle));
        CHECK(oracle.state == FWLAB_NAND_PAGE_VALID);
        c34f_memory_substrate_restart_image(&substrate, &restart_substrate);
        provider = c34f_memory_substrate_provider(&restart_substrate);
        CHECK(c34_file_restart(
                  restart_arena.bytes, sizeof(restart_arena.bytes), &provider,
                  uuid, &restart) == C34_FILE_OK);
        CHECK(first_page_valid(restart));
        c34f_memory_substrate_restart_image(
            &restart_substrate, &second_substrate);
        provider = c34f_memory_substrate_provider(&second_substrate);
        CHECK(c34_file_restart(
                  second_arena.bytes, sizeof(second_arena.bytes), &provider,
                  uuid, &second) == C34_FILE_OK);
        CHECK(first_page_valid(second));
    }
    return 1;
}

static int test_tail_and_corruption(void)
{
    struct c34f_memory_substrate source;
    struct c34f_memory_substrate partial;
    struct c34f_memory_substrate corrupt;
    struct c34_file_substrate provider;
    union c34f_test_arena source_arena;
    union c34f_test_arena restart_arena;
    struct c34_file_media *media;
    struct c34_file_media *restart;
    const size_t record = C34F_WAL0_OFFSET + C34F_WAL_HEADER_BYTES;

    c34f_memory_substrate_init(&source);
    provider = c34f_memory_substrate_provider(&source);
    CHECK(c34_file_format(
              source_arena.bytes, sizeof(source_arena.bytes), &provider, uuid,
              &media) == C34_FILE_OK);
    CHECK(issue_program(media, 1, 1));

    c34f_memory_substrate_init(&partial);
    provider = c34f_memory_substrate_provider(&partial);
    CHECK(c34_file_format(
              restart_arena.bytes, sizeof(restart_arena.bytes), &provider,
              uuid, &restart) == C34_FILE_OK);
    memcpy(&partial.working[record], &source.stable[record], 100);
    memcpy(&partial.stable[record], &source.stable[record], 100);
    provider = c34f_memory_substrate_provider(&partial);
    CHECK(c34_file_restart(
              restart_arena.bytes, sizeof(restart_arena.bytes), &provider,
              uuid, &restart) == C34_FILE_OK);
    CHECK(!first_page_valid(restart));

    c34f_memory_substrate_restart_image(&source, &corrupt);
    corrupt.working[record + 20] ^= 1;
    corrupt.stable[record + 20] ^= 1;
    provider = c34f_memory_substrate_provider(&corrupt);
    CHECK(c34_file_restart(
              restart_arena.bytes, sizeof(restart_arena.bytes), &provider,
              uuid, &restart) == C34_FILE_CORRUPT);

    c34f_memory_substrate_restart_image(&source, &corrupt);
    corrupt.working[C34F_PAGE_OFFSET + C34F_PAGE_SLOT_BYTES + 64] ^= 1;
    corrupt.stable[C34F_PAGE_OFFSET + C34F_PAGE_SLOT_BYTES + 64] ^= 1;
    provider = c34f_memory_substrate_provider(&corrupt);
    CHECK(c34_file_restart(
              restart_arena.bytes, sizeof(restart_arena.bytes), &provider,
              uuid, &restart) == C34_FILE_CORRUPT);
    return 1;
}

static int test_latest_super_corruption_fails_closed(void)
{
    struct c34f_memory_substrate substrate;
    struct c34f_memory_substrate restart_substrate;
    struct c34_file_substrate provider;
    union c34f_test_arena arena;
    union c34f_test_arena restart_arena;
    struct c34_file_media *media;
    struct c34_file_media *restart;

    c34f_memory_substrate_init(&substrate);
    provider = c34f_memory_substrate_provider(&substrate);
    CHECK(c34_file_format(
              arena.bytes, sizeof(arena.bytes), &provider, uuid, &media) ==
          C34_FILE_OK);
    CHECK(issue_program(media, 1, 1));
    CHECK(c34_file_physical_checkpoint(media) == C34_FILE_OK);
    c34f_memory_substrate_restart_image(&substrate, &restart_substrate);
    restart_substrate.working[C34F_SB1_OFFSET + 120] ^= 1;
    restart_substrate.stable[C34F_SB1_OFFSET + 120] ^= 1;
    provider = c34f_memory_substrate_provider(&restart_substrate);
    CHECK(c34_file_restart(
              restart_arena.bytes, sizeof(restart_arena.bytes), &provider,
              uuid, &restart) == C34_FILE_CORRUPT);
    return 1;
}

int main(void)
{
    CHECK(test_bac_cuts());
    CHECK(test_checkpoint_cuts());
    CHECK(test_tail_and_corruption());
    CHECK(test_latest_super_corruption_fails_closed());
    puts("C3.4 file crash/recovery: PASS (11 cases)");
    return 0;
}
