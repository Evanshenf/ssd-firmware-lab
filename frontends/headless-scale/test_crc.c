/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "ftl_scale_codec.h"
#include "fwlab/portable/crc32c.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

/* Independent reference: the previous compact-media bit-at-a-time algorithm.
 * Keep its per-byte field predicate, rather than copying the helper's loops.
 */
static uint32_t bitwise_reference(const uint8_t *bytes, size_t length,
                                  size_t omitted)
{
    uint32_t value = UINT32_MAX;
    size_t index;
    unsigned bit;

    for (index = 0; index < length; ++index) {
        value ^= (index >= omitted && index - omitted < 4u)
                     ? 0u : bytes[index];
        for (bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^
                (UINT32_C(0x82f63b78) & (0u - (value & 1u)));
    }
    return ~value;
}

static uint64_t checked;

static void verify(const uint8_t *bytes, size_t length, size_t omitted,
                    unsigned pattern, size_t alignment)
{
    uint32_t expected = bitwise_reference(bytes, length, omitted);
    uint32_t actual = fwlab_crc32c_zero_field(bytes, length, omitted);

    if (actual != expected) {
        fprintf(stderr, "CRC mismatch pattern=%u alignment=%zu length=%zu "
                "omitted=%zu expected=%08" PRIx32 " actual=%08" PRIx32 "\n",
                pattern, alignment, length, omitted, expected, actual);
        exit(EXIT_FAILURE);
    }
    if (omitted >= length) {
        CHECK(fwlab_crc32c(bytes, length) == expected);
        CHECK(sf_crc32c(bytes, length) == expected);
    }
    ++checked;
}

static uint8_t pattern_byte(unsigned pattern, size_t index)
{
    switch (pattern) {
    case 0: return 0;
    case 1: return UINT8_MAX;
    case 2: return (uint8_t)index;
    case 3: return (index & 1u) ? UINT8_C(0x55) : UINT8_C(0xaa);
    default: return (uint8_t)((index * 37u) ^ (index >> 3) ^ (index >> 8));
    }
}

int main(void)
{
    static const size_t lengths[] = {
        0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 28, 31, 32, 33,
        60, 63, 64, 65, 124, 127, 128, 129, 252, 255, 256, 257,
        511, 512, 513, 4091, 4092, 4093, 4094, 4095, 4096, 4097,
        8191, 8192, 8193
    };
    static const size_t alignments[] = {0, 1, 3, 7};
    static const size_t omissions[] = {
        SIZE_MAX, SIZE_MAX - 1u, SIZE_MAX - 3u,
        0, 1, 2, 3, 4, 15, 28, 60, 124, 252, 4092
    };
    static const uint8_t known[] = "123456789";
    static const uint8_t zero_field[] = {'1', '2', 0, 0, 0, 0, '7', '8', '9'};
    _Alignas(16) uint8_t storage[8193 + 16];
    unsigned pattern, value;
    size_t a, n, o, tail, index;

    CHECK(fwlab_crc32c(known, sizeof(known) - 1u) == UINT32_C(0xe3069283));
    CHECK(sf_crc32c(known, sizeof(known) - 1u) == UINT32_C(0xe3069283));
    CHECK(fwlab_crc32c_zero_field(known, sizeof(known) - 1u, 2) ==
          bitwise_reference(zero_field, sizeof(zero_field), SIZE_MAX));
    verify(NULL, 0, 0, 0, 0);
    verify(NULL, 0, SIZE_MAX, 0, 0);
    /* Every possible first-byte table index, independent of longer patterns. */
    for (value = 0; value <= UINT8_MAX; ++value) {
        uint8_t byte = (uint8_t)value;
        verify(&byte, 1, SIZE_MAX, value, 0);
    }
    for (pattern = 0; pattern < 5; ++pattern) {
        for (index = 0; index < sizeof(storage); ++index)
            storage[index] = pattern_byte(pattern, index);
        for (a = 0; a < sizeof(alignments) / sizeof(alignments[0]); ++a) {
            const uint8_t *bytes = storage + alignments[a];
            for (n = 0; n < sizeof(lengths) / sizeof(lengths[0]); ++n) {
                size_t length = lengths[n];
                for (o = 0; o < sizeof(omissions) / sizeof(omissions[0]); ++o)
                    verify(bytes, length, omissions[o], pattern, alignments[a]);
                verify(bytes, length, length + 1u, pattern, alignments[a]);
                verify(bytes, length, length / 2u, pattern, alignments[a]);
                /* Field wholly absent, exactly ending, or truncated to 1--3 bytes. */
                for (tail = 0; tail <= 4 && tail <= length; ++tail)
                    verify(bytes, length, length - tail, pattern, alignments[a]);
            }
        }
    }
    printf("SCALE_CRC32C_PASS|cases=%" PRIu64 "|patterns=5|unaligned=1|zero_field=1|ftl_wrapper=1\n",
           checked);
    return 0;
}
