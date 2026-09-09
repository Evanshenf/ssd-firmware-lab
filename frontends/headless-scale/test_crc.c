/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "ftl_scale_codec.h"
#include "fwlab/portable/crc32c.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool zero_reference(const uint8_t *p, size_t n)
{
    if (!p && n) return false;
    for (size_t i = 0; i < n; ++i) if (p[i]) return false;
    return true;
}

static void zero_span(size_t offset, size_t length, bool exhaustive, uint64_t *cases)
{
    size_t allocation = offset + length;
    uint8_t *base = malloc(allocation ? allocation : 1u);
    CHECK(base);
    memset(base, 0xa5, allocation ? allocation : 1u);
    uint8_t *p = base + offset;
    memset(p, 0, length);
    CHECK(sf_bytes_zero(p, length) == zero_reference(p, length)); ++*cases;
    const size_t positions[] = {0, 7, 8, length / 2, length ? length - 1u : 0};
    size_t count = exhaustive ? length : sizeof(positions) / sizeof(positions[0]);
    for (size_t k = 0; k < count; ++k) {
        size_t bad = exhaustive ? k : positions[k];
        if (bad >= length) continue;
        for (unsigned bit = 0; bit < 8; ++bit) {
            p[bad] = (uint8_t)(1u << bit);
            CHECK(sf_bytes_zero(p, length) == zero_reference(p, length)); ++*cases;
            p[bad] = 0;
        }
    }
    free(base); /* The span ends at the allocation boundary for ASan checks. */
}

static void zero_equivalence(void)
{
    uint64_t cases = 0;
    CHECK(sf_bytes_zero(NULL, 0));
    CHECK(!sf_bytes_zero(NULL, 1) && !sf_bytes_zero(NULL, 8) && !sf_bytes_zero(NULL, 4096));
    const size_t lengths[] = {124, 128, 252, 512, 1024, 1984, 2016, 2048, 3928, 4096, 8192};
    for (size_t offset = 0; offset < 8; ++offset) {
        for (size_t n = 0; n <= 65; ++n) zero_span(offset, n, true, &cases);
        for (size_t n = 0; n < sizeof(lengths) / sizeof(lengths[0]); ++n)
            zero_span(offset, lengths[n], false, &cases);
    }
    printf("SCALE_ZERO_SCAN_PASS|cases=%" PRIu64 "|byte_reference=1|offsets0to7=1|short_all_positions_all_bits=1|record_lengths_through8192=1|null_semantics=1|exact_heap_end=1\n", cases);
}

int main(void)
{
    zero_equivalence();
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
