/*
 * The GT218's row blocks and tiled placement: see
 * include\velocity9x\nv50_rblock.h. No I/O; linked into the HAL and the
 * host tests.
 */
#include "velocity9x/nv50_rblock.h"

/* No row block or unit is allowed past this: the heap is 128 MiB, and a
 * larger value means the registers were not what nouveau reads. */
#define V9X_NV50_RBLOCK_MAX_BYTES 0x01000000ul

v9x_u32 v9x_nv50_rblock_bytes(v9x_u32 units, v9x_u32 cfg1, v9x_u32 triple)
{
    v9x_u32 partitions = 0ul;
    v9x_u32 mask = (units >> 16) & 0xfful;
    v9x_u32 colbits = (cfg1 >> 12) & 0xful;
    v9x_u32 banks = 1ul << (((cfg1 >> 24) & 0x3ul) + 2ul);
    v9x_u32 rowsize;

    while (mask != 0ul) {
        partitions += mask & 1ul;
        mask >>= 1;
    }
    if (partitions == 0ul) {
        return 0ul;
    }

    /* At most 8 partitions, 32 banks, 2^15 columns: 2^26 before the
     * check, so no overflow. */
    rowsize = partitions * banks * (1ul << colbits) * 8ul;
    if ((triple & 1ul) != 0ul) {
        rowsize *= 3ul;
    }
    if (rowsize > V9X_NV50_RBLOCK_MAX_BYTES) {
        return 0ul;
    }
    return rowsize;
}

v9x_u32 v9x_nv50_rblock_unit(v9x_u32 rblock)
{
    v9x_u32 a = rblock;
    v9x_u32 b = V9X_NV50_RBLOCK_PAGE_BYTES;
    v9x_u32 unit;

    if (rblock == 0ul || rblock > V9X_NV50_RBLOCK_MAX_BYTES) {
        return 0ul;
    }
    /* The least common multiple, through the greatest common divisor. */
    while (b != 0ul) {
        v9x_u32 rest = a % b;

        a = b;
        b = rest;
    }
    unit = (rblock / a) * V9X_NV50_RBLOCK_PAGE_BYTES;
    if (unit > V9X_NV50_RBLOCK_MAX_BYTES) {
        return 0ul;
    }
    return unit;
}

v9x_u32 v9x_nv50_rblock_round(v9x_u32 bytes, v9x_u32 unit)
{
    return ((bytes + unit - 1ul) / unit) * unit;
}

int v9x_nv50_rblock_place(v9x_u32 block, v9x_u32 block_bytes, v9x_u32 unit,
                          v9x_u32 z_bytes, v9x_u32 colour_bytes,
                          v9x_u32 *z_offset, v9x_u32 *colour_offset)
{
    v9x_u32 z;
    v9x_u32 colour;
    v9x_u32 end;

    if (unit == 0ul || z_bytes == 0ul || colour_bytes == 0ul) {
        return 0;
    }
    z = v9x_nv50_rblock_round(block, unit);
    colour = z + v9x_nv50_rblock_round(z_bytes, unit);
    end = colour + v9x_nv50_rblock_round(colour_bytes, unit);
    if (z < block || colour <= z || end <= colour ||
        end - block > block_bytes) {
        return 0;
    }
    *z_offset = z;
    *colour_offset = colour;
    return 1;
}

v9x_u32 v9x_nv50_rblock_span(v9x_u32 unit, v9x_u32 granule, v9x_u32 z_bytes,
                             v9x_u32 colour_bytes)
{
    v9x_u32 z;
    v9x_u32 colour;

    if (unit == 0ul || granule == 0ul || granule > unit ||
        z_bytes > V9X_NV50_RBLOCK_MAX_BYTES * 8ul ||
        colour_bytes > V9X_NV50_RBLOCK_MAX_BYTES * 8ul) {
        return 0ul;
    }
    z = v9x_nv50_rblock_round(z_bytes, unit);
    colour = v9x_nv50_rblock_round(colour_bytes, unit);
    return z + colour + unit - granule;
}
