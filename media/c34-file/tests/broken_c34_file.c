/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_file_test_support.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const uint8_t uuid[16] = {
    0x34, 0x04, 0x42, 0x52, 0x4f, 0x4b, 0x45, 0x4e,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
};

static uint64_t hash_value(uint64_t hash, uint64_t value)
{
    unsigned int byte;

    for (byte = 0; byte < 8; ++byte) {
        hash ^= (uint8_t)(value >> (byte * 8u));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void mutate(
    struct c34f_memory_substrate *image,
    unsigned int variant
)
{
    const size_t record = C34F_WAL0_OFFSET + C34F_WAL_HEADER_BYTES;
    size_t offset;

    switch (variant) {
    case 1:
        image->size = C34_FILE_IMAGE_BYTES - 1u;
        return;
    case 2:
        offset = C34F_SB0_OFFSET + 120;
        break;
    case 3:
        offset = C34F_CP0_OFFSET + 300;
        break;
    case 4:
        offset = record + 240;
        break;
    case 5:
        offset = record + C34F_WAL_RECORD_BYTES + 40;
        break;
    case 6:
        offset = record + 2u * C34F_WAL_RECORD_BYTES + 48;
        break;
    case 7:
        offset = C34F_PAGE_OFFSET + C34F_PAGE_SLOT_BYTES + 64;
        break;
    case 8:
        offset = C34F_HEALTH_OFFSET + C34F_HEALTH_SLOT_BYTES + 64;
        break;
    case 9:
        offset = record + 244;
        break;
    case 10:
        offset = record + 2u * C34F_WAL_RECORD_BYTES + 8;
        break;
    case 11:
        offset = C34F_SB0_OFFSET + 64;
        break;
    case 12:
        image->size = C34_FILE_IMAGE_BYTES + 1u;
        return;
    case 13:
        offset = C34F_WAL0_OFFSET + 48;
        break;
    case 14:
        offset = C34F_PAGE_OFFSET + C34F_PAGE_SLOT_BYTES + 48;
        break;
    case 15:
        memcpy(&image->working[record + 3u * C34F_WAL_RECORD_BYTES],
               &image->working[record + 2u * C34F_WAL_RECORD_BYTES],
               C34F_WAL_RECORD_BYTES);
        memcpy(&image->stable[record + 3u * C34F_WAL_RECORD_BYTES],
               &image->stable[record + 2u * C34F_WAL_RECORD_BYTES],
               C34F_WAL_RECORD_BYTES);
        return;
    default:
        offset = C34F_SB0_OFFSET + 32;
        break;
    }
    image->working[offset] ^= 1;
    image->stable[offset] ^= 1;
}

int main(void)
{
    struct c34f_memory_substrate source;
    struct c34_file_substrate provider;
    union c34f_test_arena source_arena;
    union c34f_test_arena restart_arena;
    struct c34_file_media *source_media;
    struct c34_file_media *restart;
    struct c34_physical_receipt receipt;
    uint64_t aggregate = UINT64_C(1469598103934665603);
    unsigned int variant;

    c34f_memory_substrate_init(&source);
    provider = c34f_memory_substrate_provider(&source);
    if (c34_file_format(
            source_arena.bytes, sizeof(source_arena.bytes), &provider, uuid,
            &source_media) != C34_FILE_OK ||
        !c34f_test_program_page(source_media, 1, 0x6d, &receipt)) {
        return 1;
    }
    for (variant = 1; variant <= 16; ++variant) {
        struct c34f_memory_substrate image;
        enum c34_file_result result;

        c34f_memory_substrate_restart_image(&source, &image);
        mutate(&image, variant);
        provider = c34f_memory_substrate_provider(&image);
        result = c34_file_restart(
            restart_arena.bytes, sizeof(restart_arena.bytes), &provider,
            uuid, &restart);
        if (result == C34_FILE_OK) {
            fprintf(stderr, "broken variant %u was accepted\n", variant);
            return 1;
        }
        aggregate = hash_value(hash_value(aggregate, variant), result);
    }
    puts("C3.4 broken file variants: PASS");
    printf("  shortest-counterexamples=16 depth-sum=16 hash=%016" PRIx64
           "\n",
           aggregate);
    return 0;
}
