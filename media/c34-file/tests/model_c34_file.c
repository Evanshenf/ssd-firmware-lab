/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_file_test_support.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const uint8_t uuid[16] = {
    0x34, 0x04, 0x4d, 0x4f, 0x44, 0x45, 0x4c, 0x01,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
};

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int byte;

    for (byte = 0; byte < 8; ++byte) {
        hash ^= (uint8_t)(value >> (byte * 8u));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int page_valid(struct c34_file_media *media)
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
           page.state == FWLAB_NAND_PAGE_VALID;
}

int main(void)
{
    struct c34f_memory_substrate source;
    struct c34_file_substrate provider;
    union c34f_test_arena source_arena;
    struct c34_file_media *source_media;
    struct c34_physical_receipt receipt;
    uint64_t aggregate = UINT64_C(1469598103934665603);
    const size_t record = C34F_WAL0_OFFSET + C34F_WAL_HEADER_BYTES;
    unsigned int prefix;
    uint32_t restarts = 0;

    c34f_memory_substrate_init(&source);
    provider = c34f_memory_substrate_provider(&source);
    if (c34_file_format(
            source_arena.bytes, sizeof(source_arena.bytes), &provider, uuid,
            &source_media) != C34_FILE_OK ||
        !c34f_test_program_page(source_media, 1, 0x5c, &receipt)) {
        return 1;
    }
    for (prefix = 0; prefix <= C34F_WAL_RECORD_BYTES; ++prefix) {
        struct c34f_memory_substrate image;
        struct c34_file_substrate image_provider;
        union c34f_test_arena format_arena;
        union c34f_test_arena restart_arena;
        struct c34_file_media *formatted;
        struct c34_file_media *restart;
        enum c34_file_result result;
        int expected_valid = prefix == C34F_WAL_RECORD_BYTES;

        c34f_memory_substrate_init(&image);
        image_provider = c34f_memory_substrate_provider(&image);
        if (c34_file_format(
                format_arena.bytes, sizeof(format_arena.bytes),
                &image_provider, uuid, &formatted) != C34_FILE_OK) {
            return 1;
        }
        memcpy(&image.working[C34F_PAGE_OFFSET],
               &source.stable[C34F_PAGE_OFFSET],
               C34F_HEALTH_OFFSET + C34F_BLOCKS * 2u *
                       C34F_HEALTH_SLOT_BYTES -
                   C34F_PAGE_OFFSET);
        memcpy(&image.stable[C34F_PAGE_OFFSET],
               &source.stable[C34F_PAGE_OFFSET],
               C34F_HEALTH_OFFSET + C34F_BLOCKS * 2u *
                       C34F_HEALTH_SLOT_BYTES -
                   C34F_PAGE_OFFSET);
        memcpy(&image.working[record], &source.stable[record], prefix);
        memcpy(&image.stable[record], &source.stable[record], prefix);
        result = c34_file_restart(
            restart_arena.bytes, sizeof(restart_arena.bytes),
            &image_provider, uuid, &restart);
        if (result != C34_FILE_OK || page_valid(restart) != expected_valid) {
            return 1;
        }
        aggregate = hash_u64(
            hash_u64(aggregate, prefix), c34_file_physical_hash(restart));
        ++restarts;
        (void)formatted;
    }
    puts("C3.4 file persistent-prefix model: PASS");
    printf("  families=4 prefixes=%u restarts=%" PRIu32 " cuts=%" PRIu32
           "\n",
           C34F_WAL_RECORD_BYTES + 1u, restarts, restarts * 3u);
    printf("  hash=%016" PRIx64 "\n", aggregate);
    return 0;
}
