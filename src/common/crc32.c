/*
 * The zip CRC-32. See include\velocity9x\crc32.h.
 *
 * Four bits at a time through a sixteen-entry table built on each call. A
 * 256-entry literal table is the usual choice and would be faster, but it is
 * a kilobyte of numbers nobody can review by eye; the bitwise loop it derives
 * from is reviewable but costs eight steps per byte. Building sixteen entries
 * is 64 steps per call and halves the per-byte work to two lookups, and the
 * table is local, so there is no static state and no C runtime.
 */
#include "velocity9x/crc32.h"

/* The reflected IEEE polynomial (APPNOTE.TXT 4.4.7, ISO 3309). */
#define V9X_CRC32_POLYNOMIAL ((v9x_u32)0xEDB88320ul)

v9x_u32 v9x_crc32_update(v9x_u32 crc, const v9x_u8 *data, v9x_u32 length)
{
    v9x_u32 table[16];
    v9x_u32 entry;
    v9x_u32 i;
    v9x_u16 bit;

    if (data == 0) {
        return crc;
    }

    /* table[n] is the register after shifting the nibble n through four
     * steps of the bitwise algorithm. */
    for (i = 0ul; i < 16ul; ++i) {
        entry = i;
        for (bit = 0u; bit < 4u; ++bit) {
            if ((entry & 1ul) != 0ul) {
                entry = (entry >> 1) ^ V9X_CRC32_POLYNOMIAL;
            } else {
                entry >>= 1;
            }
        }
        table[i] = entry;
    }

    /* Low nibble first: the reflected register consumes bits LSB first. */
    for (i = 0ul; i < length; ++i) {
        crc ^= (v9x_u32)data[i];
        crc = (crc >> 4) ^ table[crc & 0x0Ful];
        crc = (crc >> 4) ^ table[crc & 0x0Ful];
    }
    return crc;
}

v9x_u32 v9x_crc32_final(v9x_u32 crc)
{
    return crc ^ (v9x_u32)0xFFFFFFFFul;
}

v9x_u32 v9x_crc32(const v9x_u8 *data, v9x_u32 length)
{
    return v9x_crc32_final(v9x_crc32_update(V9X_CRC32_INITIAL, data, length));
}
