/*
 * CRC-32b by Andrey Dyldin
 *
 * Based on "File Verification Using CRC" by Mark R. Nelson in
 * Dr. Dobb's Journal, May 1992, pp. 64-67
 */

#include <astra.h>

static uint32_t crc32_table[256] = { 0 };

static void _init_crc32(void)
{
    int i, j;
    uint32_t c;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#   define POLY 0x04c11db7
#   define STEP { c = c & 0x80000000 ? (c << 1) ^ POLY : (c << 1); }
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#   define POLY 0xedb88320
#   define STEP { c = (c&1) ? ((c >> 1) ^ POLY) : (c >> 1); }
#endif

    for(i = 0; i < 256; ++i)
    {
        for(c = i << 24, j = 8; j > 0; --j)
            STEP;
        crc32_table[i] = c;
    }

#undef STEP
#undef POLY
}

uint32_t crc32b(const uint8_t *buffer, int size)
{
    if(!crc32_table[0])
        _init_crc32();

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#   define ESTEP (crc << 8) ^ crc32_table[(crc >> 24) ^ *buffer]
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#   define ESTEP (crc >> 8) ^ crc32_table[(crc ^ *buffer) & 0xFF]
#endif

#define STEP { crc = ESTEP; ++buffer; --size; }

    register uint32_t crc = 0xffffffff;

    int n = (size + 7) / 8;
    const int m = size % 8;
    switch(m)
    {
        case 0: do { STEP;
        case 7:      STEP;
        case 6:      STEP;
        case 5:      STEP;
        case 4:      STEP;
        case 3:      STEP;
        case 2:      STEP;
        case 1:      STEP;
                   } while (--n > 0);
    }

#undef STEP
#undef ESTEP

    return crc;
}
