/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_PORTABLE_CRC32C_FAST_H
#define FWLAB_PORTABLE_CRC32C_FAST_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "fwlab/portable/crc32c.h"

/* Compile-time profiles only. A hardware-profile executable requires that
 * ISA on its execution CPU; this header does not perform runtime dispatch.
 * Unsupported targets (including big-endian ARM) use the existing table.
 * API bytes, complement convention and zero-field semantics are unchanged. */
#if defined(__x86_64__) && defined(__SSE4_2__)
#include <nmmintrin.h>
#define FWLAB_CRC32C_FAST_BACKEND 1
#elif defined(__aarch64__) && defined(__ARM_FEATURE_CRC32) && \
      defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
      __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#include <arm_acle.h>
#define FWLAB_CRC32C_FAST_BACKEND 2
#else
#define FWLAB_CRC32C_FAST_BACKEND 0
#endif

#if FWLAB_CRC32C_FAST_BACKEND != 0
#include "fwlab/portable/crc32c_shift1024.h"

static inline uint32_t fwlab_crc32c_fast_word(uint32_t state, const uint8_t *bytes)
{
    uint64_t word;
    memcpy(&word, bytes, sizeof(word)); /* No alignment or aliasing assumption. */
#if FWLAB_CRC32C_FAST_BACKEND == 1
    return (uint32_t)_mm_crc32_u64(state, word);
#else
    return __crc32cd(state, word);
#endif
}

static inline uint32_t fwlab_crc32c_fast_byte(uint32_t state, uint8_t byte)
{
#if FWLAB_CRC32C_FAST_BACKEND == 1
    return _mm_crc32_u8(state, byte);
#else
    return __crc32cb(state, byte);
#endif
}

static inline uint32_t fwlab_crc32c_fast_shift1024(uint32_t state)
{
    return fwlab_crc32c_shift1024_table[0][state & UINT32_C(0xff)] ^
           fwlab_crc32c_shift1024_table[1][(state >> 8) & UINT32_C(0xff)] ^
           fwlab_crc32c_shift1024_table[2][(state >> 16) & UINT32_C(0xff)] ^
           fwlab_crc32c_shift1024_table[3][state >> 24];
}

/* Internal raw-state continuation. Only the outer API complements the state.
 * For concatenation A||B: raw(s,A||B)=shift_len(B)(raw(s,A)) XOR raw(0,B).
 * Four independent 1024-byte lanes break the instruction dependency chain;
 * the first carries s and the remaining lanes start at raw zero. */
static inline uint32_t fwlab_crc32c_fast_raw(
    uint32_t state, const uint8_t *bytes, size_t length)
{
    while (length >= 4096u) {
        uint32_t a = state, b = 0, c = 0, d = 0;
        for (size_t offset = 0; offset < 1024u; offset += 8u) {
            a = fwlab_crc32c_fast_word(a, bytes + offset);
            b = fwlab_crc32c_fast_word(b, bytes + 1024u + offset);
            c = fwlab_crc32c_fast_word(c, bytes + 2048u + offset);
            d = fwlab_crc32c_fast_word(d, bytes + 3072u + offset);
        }
        state = fwlab_crc32c_fast_shift1024(a) ^ b;
        state = fwlab_crc32c_fast_shift1024(state) ^ c;
        state = fwlab_crc32c_fast_shift1024(state) ^ d;
        bytes += 4096u;
        length -= 4096u;
    }
    while (length >= 8u) {
        state = fwlab_crc32c_fast_word(state, bytes);
        bytes += 8u;
        length -= 8u;
    }
    while (length != 0) {
        state = fwlab_crc32c_fast_byte(state, *bytes++);
        --length;
    }
    return state;
}
#endif

/* NULL is valid only for zero length; then no pointer arithmetic/load occurs. */
static inline uint32_t fwlab_crc32c_fast(const uint8_t *bytes, size_t length)
{
#if FWLAB_CRC32C_FAST_BACKEND == 0
    return fwlab_crc32c(bytes, length);
#else
    return ~fwlab_crc32c_fast_raw(UINT32_MAX, bytes, length);
#endif
}

/* Four in-range bytes contribute zeros, not omission. The range may end in
 * the middle of the field. An absent/SIZE_MAX field never forms offset+4. */
static inline uint32_t fwlab_crc32c_fast_zero_field(
    const uint8_t *bytes, size_t length, size_t field_offset)
{
#if FWLAB_CRC32C_FAST_BACKEND == 0
    return fwlab_crc32c_zero_field(bytes, length, field_offset);
#else
    size_t prefix = field_offset < length ? field_offset : length;
    size_t zeros = length - prefix;
    uint32_t state = fwlab_crc32c_fast_raw(UINT32_MAX, bytes, prefix);
    if (zeros > 4u) zeros = 4u;
    for (size_t i = 0; i < zeros; ++i)
        state = fwlab_crc32c_fast_byte(state, 0);
    if (length - prefix > zeros)
        state = fwlab_crc32c_fast_raw(state, bytes + prefix + zeros, length - prefix - zeros);
    return ~state;
#endif
}

#endif
