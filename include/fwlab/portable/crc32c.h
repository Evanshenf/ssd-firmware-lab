/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PORTABLE_CRC32C_H
#define FWLAB_PORTABLE_CRC32C_H

#include <stddef.h>
#include <stdint.h>

/* Reflected CRC32C (0x82f63b78), initial/final complement. The optional
 * four-byte field contributes zero bytes, including any in-range tail;
 * it is never skipped. SIZE_MAX or an offset >= length means no field.
 * Byte accesses support unaligned input. NULL is allowed only for length zero.
 */
static inline uint32_t fwlab_crc32c_zero_field(
    const uint8_t *bytes, size_t length, size_t field_offset)
{
    /* Immutable byte table: eight reflected polynomial steps per index. */
    static const uint32_t table[256] = {
        UINT32_C(0x00000000), UINT32_C(0xf26b8303), UINT32_C(0xe13b70f7), UINT32_C(0x1350f3f4),
        UINT32_C(0xc79a971f), UINT32_C(0x35f1141c), UINT32_C(0x26a1e7e8), UINT32_C(0xd4ca64eb),
        UINT32_C(0x8ad958cf), UINT32_C(0x78b2dbcc), UINT32_C(0x6be22838), UINT32_C(0x9989ab3b),
        UINT32_C(0x4d43cfd0), UINT32_C(0xbf284cd3), UINT32_C(0xac78bf27), UINT32_C(0x5e133c24),
        UINT32_C(0x105ec76f), UINT32_C(0xe235446c), UINT32_C(0xf165b798), UINT32_C(0x030e349b),
        UINT32_C(0xd7c45070), UINT32_C(0x25afd373), UINT32_C(0x36ff2087), UINT32_C(0xc494a384),
        UINT32_C(0x9a879fa0), UINT32_C(0x68ec1ca3), UINT32_C(0x7bbcef57), UINT32_C(0x89d76c54),
        UINT32_C(0x5d1d08bf), UINT32_C(0xaf768bbc), UINT32_C(0xbc267848), UINT32_C(0x4e4dfb4b),
        UINT32_C(0x20bd8ede), UINT32_C(0xd2d60ddd), UINT32_C(0xc186fe29), UINT32_C(0x33ed7d2a),
        UINT32_C(0xe72719c1), UINT32_C(0x154c9ac2), UINT32_C(0x061c6936), UINT32_C(0xf477ea35),
        UINT32_C(0xaa64d611), UINT32_C(0x580f5512), UINT32_C(0x4b5fa6e6), UINT32_C(0xb93425e5),
        UINT32_C(0x6dfe410e), UINT32_C(0x9f95c20d), UINT32_C(0x8cc531f9), UINT32_C(0x7eaeb2fa),
        UINT32_C(0x30e349b1), UINT32_C(0xc288cab2), UINT32_C(0xd1d83946), UINT32_C(0x23b3ba45),
        UINT32_C(0xf779deae), UINT32_C(0x05125dad), UINT32_C(0x1642ae59), UINT32_C(0xe4292d5a),
        UINT32_C(0xba3a117e), UINT32_C(0x4851927d), UINT32_C(0x5b016189), UINT32_C(0xa96ae28a),
        UINT32_C(0x7da08661), UINT32_C(0x8fcb0562), UINT32_C(0x9c9bf696), UINT32_C(0x6ef07595),
        UINT32_C(0x417b1dbc), UINT32_C(0xb3109ebf), UINT32_C(0xa0406d4b), UINT32_C(0x522bee48),
        UINT32_C(0x86e18aa3), UINT32_C(0x748a09a0), UINT32_C(0x67dafa54), UINT32_C(0x95b17957),
        UINT32_C(0xcba24573), UINT32_C(0x39c9c670), UINT32_C(0x2a993584), UINT32_C(0xd8f2b687),
        UINT32_C(0x0c38d26c), UINT32_C(0xfe53516f), UINT32_C(0xed03a29b), UINT32_C(0x1f682198),
        UINT32_C(0x5125dad3), UINT32_C(0xa34e59d0), UINT32_C(0xb01eaa24), UINT32_C(0x42752927),
        UINT32_C(0x96bf4dcc), UINT32_C(0x64d4cecf), UINT32_C(0x77843d3b), UINT32_C(0x85efbe38),
        UINT32_C(0xdbfc821c), UINT32_C(0x2997011f), UINT32_C(0x3ac7f2eb), UINT32_C(0xc8ac71e8),
        UINT32_C(0x1c661503), UINT32_C(0xee0d9600), UINT32_C(0xfd5d65f4), UINT32_C(0x0f36e6f7),
        UINT32_C(0x61c69362), UINT32_C(0x93ad1061), UINT32_C(0x80fde395), UINT32_C(0x72966096),
        UINT32_C(0xa65c047d), UINT32_C(0x5437877e), UINT32_C(0x4767748a), UINT32_C(0xb50cf789),
        UINT32_C(0xeb1fcbad), UINT32_C(0x197448ae), UINT32_C(0x0a24bb5a), UINT32_C(0xf84f3859),
        UINT32_C(0x2c855cb2), UINT32_C(0xdeeedfb1), UINT32_C(0xcdbe2c45), UINT32_C(0x3fd5af46),
        UINT32_C(0x7198540d), UINT32_C(0x83f3d70e), UINT32_C(0x90a324fa), UINT32_C(0x62c8a7f9),
        UINT32_C(0xb602c312), UINT32_C(0x44694011), UINT32_C(0x5739b3e5), UINT32_C(0xa55230e6),
        UINT32_C(0xfb410cc2), UINT32_C(0x092a8fc1), UINT32_C(0x1a7a7c35), UINT32_C(0xe811ff36),
        UINT32_C(0x3cdb9bdd), UINT32_C(0xceb018de), UINT32_C(0xdde0eb2a), UINT32_C(0x2f8b6829),
        UINT32_C(0x82f63b78), UINT32_C(0x709db87b), UINT32_C(0x63cd4b8f), UINT32_C(0x91a6c88c),
        UINT32_C(0x456cac67), UINT32_C(0xb7072f64), UINT32_C(0xa457dc90), UINT32_C(0x563c5f93),
        UINT32_C(0x082f63b7), UINT32_C(0xfa44e0b4), UINT32_C(0xe9141340), UINT32_C(0x1b7f9043),
        UINT32_C(0xcfb5f4a8), UINT32_C(0x3dde77ab), UINT32_C(0x2e8e845f), UINT32_C(0xdce5075c),
        UINT32_C(0x92a8fc17), UINT32_C(0x60c37f14), UINT32_C(0x73938ce0), UINT32_C(0x81f80fe3),
        UINT32_C(0x55326b08), UINT32_C(0xa759e80b), UINT32_C(0xb4091bff), UINT32_C(0x466298fc),
        UINT32_C(0x1871a4d8), UINT32_C(0xea1a27db), UINT32_C(0xf94ad42f), UINT32_C(0x0b21572c),
        UINT32_C(0xdfeb33c7), UINT32_C(0x2d80b0c4), UINT32_C(0x3ed04330), UINT32_C(0xccbbc033),
        UINT32_C(0xa24bb5a6), UINT32_C(0x502036a5), UINT32_C(0x4370c551), UINT32_C(0xb11b4652),
        UINT32_C(0x65d122b9), UINT32_C(0x97baa1ba), UINT32_C(0x84ea524e), UINT32_C(0x7681d14d),
        UINT32_C(0x2892ed69), UINT32_C(0xdaf96e6a), UINT32_C(0xc9a99d9e), UINT32_C(0x3bc21e9d),
        UINT32_C(0xef087a76), UINT32_C(0x1d63f975), UINT32_C(0x0e330a81), UINT32_C(0xfc588982),
        UINT32_C(0xb21572c9), UINT32_C(0x407ef1ca), UINT32_C(0x532e023e), UINT32_C(0xa145813d),
        UINT32_C(0x758fe5d6), UINT32_C(0x87e466d5), UINT32_C(0x94b49521), UINT32_C(0x66df1622),
        UINT32_C(0x38cc2a06), UINT32_C(0xcaa7a905), UINT32_C(0xd9f75af1), UINT32_C(0x2b9cd9f2),
        UINT32_C(0xff56bd19), UINT32_C(0x0d3d3e1a), UINT32_C(0x1e6dcdee), UINT32_C(0xec064eed),
        UINT32_C(0xc38d26c4), UINT32_C(0x31e6a5c7), UINT32_C(0x22b65633), UINT32_C(0xd0ddd530),
        UINT32_C(0x0417b1db), UINT32_C(0xf67c32d8), UINT32_C(0xe52cc12c), UINT32_C(0x1747422f),
        UINT32_C(0x49547e0b), UINT32_C(0xbb3ffd08), UINT32_C(0xa86f0efc), UINT32_C(0x5a048dff),
        UINT32_C(0x8ecee914), UINT32_C(0x7ca56a17), UINT32_C(0x6ff599e3), UINT32_C(0x9d9e1ae0),
        UINT32_C(0xd3d3e1ab), UINT32_C(0x21b862a8), UINT32_C(0x32e8915c), UINT32_C(0xc083125f),
        UINT32_C(0x144976b4), UINT32_C(0xe622f5b7), UINT32_C(0xf5720643), UINT32_C(0x07198540),
        UINT32_C(0x590ab964), UINT32_C(0xab613a67), UINT32_C(0xb831c993), UINT32_C(0x4a5a4a90),
        UINT32_C(0x9e902e7b), UINT32_C(0x6cfbad78), UINT32_C(0x7fab5e8c), UINT32_C(0x8dc0dd8f),
        UINT32_C(0xe330a81a), UINT32_C(0x115b2b19), UINT32_C(0x020bd8ed), UINT32_C(0xf0605bee),
        UINT32_C(0x24aa3f05), UINT32_C(0xd6c1bc06), UINT32_C(0xc5914ff2), UINT32_C(0x37faccf1),
        UINT32_C(0x69e9f0d5), UINT32_C(0x9b8273d6), UINT32_C(0x88d28022), UINT32_C(0x7ab90321),
        UINT32_C(0xae7367ca), UINT32_C(0x5c18e4c9), UINT32_C(0x4f48173d), UINT32_C(0xbd23943e),
        UINT32_C(0xf36e6f75), UINT32_C(0x0105ec76), UINT32_C(0x12551f82), UINT32_C(0xe03e9c81),
        UINT32_C(0x34f4f86a), UINT32_C(0xc69f7b69), UINT32_C(0xd5cf889d), UINT32_C(0x27a40b9e),
        UINT32_C(0x79b737ba), UINT32_C(0x8bdcb4b9), UINT32_C(0x988c474d), UINT32_C(0x6ae7c44e),
        UINT32_C(0xbe2da0a5), UINT32_C(0x4c4623a6), UINT32_C(0x5f16d052), UINT32_C(0xad7d5351)
    };
    uint32_t value = UINT32_MAX;
    size_t prefix = field_offset < length ? field_offset : length;
    size_t index = 0;
    size_t zeroed = 0;

    for (; index < prefix; ++index)
        value = (value >> 8) ^ table[(value ^ bytes[index]) & UINT32_C(0xff)];
    /* Do not form field_offset + 4: an absent field may be SIZE_MAX. */
    for (; index < length && zeroed < 4u; ++index, ++zeroed)
        value = (value >> 8) ^ table[value & UINT32_C(0xff)];
    for (; index < length; ++index)
        value = (value >> 8) ^ table[(value ^ bytes[index]) & UINT32_C(0xff)];
    return ~value;
}

static inline uint32_t fwlab_crc32c(const uint8_t *bytes, size_t length)
{
    return fwlab_crc32c_zero_field(bytes, length, SIZE_MAX);
}

#endif
