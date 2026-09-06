/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fwlab/portable/crc32c_fast.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(FWLAB_TEST_EXPECT_BACKEND) && \
    FWLAB_TEST_EXPECT_BACKEND != FWLAB_CRC32C_FAST_BACKEND
#error "CRC test did not compile the requested hardware/fallback profile"
#endif

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

static uint64_t checked;

/* Independent retained compact-media bitwise reference. It reads one byte at
 * a time and uses the old per-byte omitted-field predicate, not DUT chunks. */
static uint32_t reference_byte(uint32_t raw, uint8_t byte)
{
    raw ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit)
        raw = (raw >> 1) ^ (UINT32_C(0x82f63b78) & (0u - (raw & 1u)));
    return raw;
}

static uint32_t bitwise_reference(const uint8_t *bytes, size_t length, size_t omitted)
{
    uint32_t raw = UINT32_MAX;
    for (size_t i = 0; i < length; ++i)
        raw = reference_byte(raw, i >= omitted && i - omitted < 4u ? 0 : bytes[i]);
    return ~raw;
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

static void compare(const uint8_t *bytes, size_t length, size_t omitted,
                     uint32_t expected, unsigned pattern, size_t alignment)
{
    uint32_t fast = fwlab_crc32c_fast_zero_field(bytes, length, omitted);
    uint32_t table = fwlab_crc32c_zero_field(bytes, length, omitted);
    if (fast != expected || table != expected) {
        fprintf(stderr, "CRC mismatch backend=%d pattern=%u alignment=%zu length=%zu "
                "field=%zu bitwise=%08" PRIx32 " table=%08" PRIx32 " fast=%08" PRIx32 "\n",
                FWLAB_CRC32C_FAST_BACKEND, pattern, alignment, length, omitted, expected, table, fast);
        exit(1);
    }
    if (omitted >= length) {
        CHECK(fwlab_crc32c_fast(bytes, length) == expected);
        CHECK(fwlab_crc32c(bytes, length) == expected);
    }
    ++checked;
}

static void verify(const uint8_t *bytes, size_t length, size_t omitted,
                    unsigned pattern, size_t alignment)
{
    compare(bytes, length, omitted, bitwise_reference(bytes, length, omitted), pattern, alignment);
}

static unsigned verify_shift_table(void)
{
#if FWLAB_CRC32C_FAST_BACKEND != 0
    unsigned checked_entries = 0;
    /* Generator definition: T[k][v] = raw(v<<(8*k), 1024 zero bytes).
     * This independently evaluates the polynomial recurrence for all entries,
     * then checks the XOR composition on fixed arbitrary RAW states. Neither
     * initialization nor final complement belongs inside this linear shift. */
    for (unsigned k = 0; k < 4; ++k) {
        for (unsigned v = 0; v < 256; ++v) {
            uint32_t raw = (uint32_t)v << (8u * k);
            for (unsigned byte = 0; byte < 1024; ++byte) raw = reference_byte(raw, 0);
            CHECK(fwlab_crc32c_shift1024_table[k][v] == raw);
            ++checked_entries;
        }
    }
    static const uint32_t seeds[] = {0, 1, UINT32_MAX, UINT32_C(0x80000000),
                                    UINT32_C(0x12345678), UINT32_C(0x82f63b78)};
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        uint32_t raw = seeds[i];
        for (unsigned byte = 0; byte < 1024; ++byte) raw = reference_byte(raw, 0);
        CHECK(fwlab_crc32c_fast_shift1024(seeds[i]) == raw);
    }
    return checked_entries;
#else
    return 0;
#endif
}

int main(void)
{
    static const size_t lengths[] = {
        0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 28, 31, 32, 33,
        60, 63, 64, 65, 124, 127, 128, 129, 252, 255, 256, 257,
        511, 512, 513, 1023, 1024, 1025, 2047, 2048, 2049,
        3071, 3072, 3073, 4088, 4091, 4092, 4093, 4094, 4095,
        4096, 4097, 4104, 5119, 5120, 5121, 8191, 8192, 8193,
        12287, 12288, 12289, 16383, 16384, 16385, 32767, 32768
    };
    static const size_t field_lengths[] = {
        0, 1, 3, 7, 8, 9, 1023, 1024, 1025, 4092, 4096, 4099, 8192, 8193, 12288, 16385
    };
    static const size_t alignments[] = {0, 1, 3, 7, 15, 31};
    static const uint8_t known[] = "123456789";
    static const uint8_t sentence[] = "The quick brown fox jumps over the lazy dog";
    _Alignas(64) static uint8_t storage[32768 + 32];
    unsigned shift_entries = verify_shift_table();

    CHECK(fwlab_crc32c_fast(known, sizeof(known) - 1u) == UINT32_C(0xe3069283));
    verify(known, sizeof(known) - 1u, SIZE_MAX, 0, 0);
    verify(sentence, sizeof(sentence) - 1u, SIZE_MAX, 0, 0);
    verify(NULL, 0, SIZE_MAX, 0, 0); verify(NULL, 0, 0, 0, 0);
    for (unsigned v = 0; v < 256; ++v) {
        uint8_t byte = (uint8_t)v;
        verify(&byte, 1, SIZE_MAX, v, 0);
    }
    for (unsigned pattern = 0; pattern < 5; ++pattern) {
        for (size_t i = 0; i < sizeof(storage); ++i) storage[i] = pattern_byte(pattern, i);
        /* Exhaustive lengths use the independently advanced bitwise state;
         * avoid quadratic bitwise-reference work while checking every length. */
        uint32_t raw = UINT32_MAX;
        for (size_t length = 0; length <= 8192; ++length) {
            if (length) raw = reference_byte(raw, storage[length - 1u]);
            compare(storage, length, SIZE_MAX, ~raw, pattern, 0);
        }
        for (size_t a = 0; a < sizeof(alignments) / sizeof(alignments[0]); ++a) {
            const uint8_t *bytes = storage + alignments[a];
            for (size_t n = 0; n < sizeof(lengths) / sizeof(lengths[0]); ++n)
                verify(bytes, lengths[n], SIZE_MAX, pattern, alignments[a]);
            for (size_t n = 0; n < sizeof(field_lengths) / sizeof(field_lengths[0]); ++n) {
                size_t length = field_lengths[n];
                const size_t fields[] = {0, 1, length / 2u, length, length + 1u,
                                        SIZE_MAX, SIZE_MAX - 1u, SIZE_MAX - 3u};
                for (size_t field = 0; field < sizeof(fields) / sizeof(fields[0]); ++field)
                    verify(bytes, length, fields[field], pattern, alignments[a]);
                for (size_t tail = 1; tail <= 4 && tail <= length; ++tail)
                    verify(bytes, length, length - tail, pattern, alignments[a]);
            }
        }
    }
    printf("CRC32C_FAST_PASS|backend=%d|cases=%" PRIu64 "|shift_entries=%u|lengths0_8192=1|"
           "multiple4KiBblocks=1|alignments=6|patterns=5|zero_field=1|no_throughput_claim=1\n",
           FWLAB_CRC32C_FAST_BACKEND, checked, shift_entries);
    return 0;
}
