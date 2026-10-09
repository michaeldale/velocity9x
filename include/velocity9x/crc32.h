/*
 * CRC-32 as zip uses it (PKWARE APPNOTE.TXT 4.4.7): the IEEE 802.3
 * polynomial 0x04C11DB7, bit-reflected (0xEDB88320), register preset to all
 * ones and the result complemented. The check value of the nine ASCII bytes
 * "123456789" is 0xCBF43926.
 *
 * Incremental use: crc = V9X_CRC32_INITIAL; crc = v9x_crc32_update(crc, ...)
 * as often as needed; v9x_crc32_final(crc) is the value a zip header holds.
 */
#ifndef VELOCITY9X_CRC32_H
#define VELOCITY9X_CRC32_H

#include "velocity9x/types.h"

#define V9X_CRC32_INITIAL ((v9x_u32)0xFFFFFFFFul)

v9x_u32 v9x_crc32_update(v9x_u32 crc, const v9x_u8 *data, v9x_u32 length);
v9x_u32 v9x_crc32_final(v9x_u32 crc);

/* The whole computation over one buffer. */
v9x_u32 v9x_crc32(const v9x_u8 *data, v9x_u32 length);

#endif /* VELOCITY9X_CRC32_H */
