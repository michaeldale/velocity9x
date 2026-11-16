/*
 * The GT218's row blocks, and the placement of tiled surfaces in them.
 * Pure arithmetic, shared by the HAL (d3d_nv50.c, eng_nv50.c) and the
 * host tests.
 *
 * A tiled storage type ("kind": 6Ch Z16, 70h tiled colour, 28h Z24S8)
 * does not keep a surface's bytes at the addresses a pitch (kind 0)
 * mapping of the same memory shows them at: the memory controller spreads
 * them, in 16 KiB pieces, over the row block ("rblock") they fall in.
 * MEASURED on W9QI0DAD (V9XN5CH /z:map, 2026-11-16): a 256x600 Z16 drawn
 * band by band put its first 32 rows 32 KiB below its base when the base
 * was 64 KiB into a row block, and every band stayed inside the 192 KiB
 * row blocks the Z touched, at every base tried. Quake 3 at 800x600 then
 * lost the top rows of its Z to the back buffer below it.
 *
 * So, as nouveau's VRAM allocator does (nvkm_mm block_size, from
 * nv50_fb_vram_rblock in nvkm/subdev/fb/ramnv50.c), a tiled surface gets
 * whole row blocks of its own, and surfaces of two different kinds never
 * share one. Row blocks are counted from video memory offset 0.
 */
#ifndef VELOCITY9X_NV50_RBLOCK_H
#define VELOCITY9X_NV50_RBLOCK_H

#include "velocity9x/types.h"

/* The registers nv50_fb_vram_rblock reads: the unit enables (partitions
 * in bits 23:16), PFB's memory configuration (column bits 15:12, banks
 * 25:24) and 100250h (bit 0: three row blocks a block). */
#define V9X_NV50_RBLOCK_UNITS_REG    0x00001540ul
#define V9X_NV50_RBLOCK_CFG1_REG     0x00100204ul
#define V9X_NV50_RBLOCK_TRIPLE_REG   0x00100250ul

/* The large page the alias windows map (nv50_chan.h): a unit is always a
 * whole number of them as well. */
#define V9X_NV50_RBLOCK_PAGE_BYTES   0x00010000ul

/*
 * The row block's size in bytes, as nv50_fb_vram_rblock computes it:
 * partitions * banks * 2^column bits * 8, three times that when 100250h
 * bit 0 is set. 0 when the values cannot be a configuration (no
 * partition, or a size past 16 MiB).
 */
v9x_u32 v9x_nv50_rblock_bytes(v9x_u32 units, v9x_u32 cfg1, v9x_u32 triple);

/*
 * The unit tiled surfaces are placed and sized in: the least common
 * multiple of the row block and the 64 KiB page, so a unit boundary is
 * both. 0 for a row block of 0 or a unit past 16 MiB.
 */
v9x_u32 v9x_nv50_rblock_unit(v9x_u32 rblock);

/* bytes rounded up to a whole number of units (unit not 0). */
v9x_u32 v9x_nv50_rblock_round(v9x_u32 bytes, v9x_u32 unit);

/*
 * A Z buffer and its colour shadow, each in units of their own, inside a
 * heap block at video memory offset `block`, `block_bytes` long: the Z
 * at the first unit boundary at or past the block, the colour at the
 * unit boundary after the Z's last unit. Returns 1 with both offsets, or
 * 0 when they do not fit (or the unit is 0). v9x_nv50_rblock_span is
 * what to ask the heap for so that they always fit.
 */
int v9x_nv50_rblock_place(v9x_u32 block, v9x_u32 block_bytes, v9x_u32 unit,
                          v9x_u32 z_bytes, v9x_u32 colour_bytes,
                          v9x_u32 *z_offset, v9x_u32 *colour_offset);

/* The heap block that always holds the pair, from a start on `granule`
 * (the heap's own alignment, at least 4 bytes): both rounded to units,
 * plus a unit less one granule for the rounding of the start. 0 on
 * overflow or a unit of 0. */
v9x_u32 v9x_nv50_rblock_span(v9x_u32 unit, v9x_u32 granule, v9x_u32 z_bytes,
                             v9x_u32 colour_bytes);

#endif
