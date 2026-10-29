/*
 * DDI 8's answers built from what the driver already tells a DDI 7
 * runtime: the DX8 forms of the DX7 cap words that changed, and the
 * DirectX 8 format list from the texture, render and depth formats
 * (docs\specifications\ddi8-on-win9x.md sections 3 and 4). d3d_core.c
 * copies the rest of D3DCAPS8 field for field.
 *
 * Pure, so the mapping is held to tests\host\test_d3d_caps8.c.
 */
#ifndef VELOCITY9X_D3D_CAPS8_H
#define VELOCITY9X_D3D_CAPS8_H

#include "velocity9x/types.h"

/* D3DFORMAT, d3d8types.h. */
#define V9X_D3DFMT_A8R8G8B8 21ul
#define V9X_D3DFMT_X8R8G8B8 22ul
#define V9X_D3DFMT_R5G6B5   23ul
#define V9X_D3DFMT_X1R5G5B5 24ul
#define V9X_D3DFMT_A1R5G5B5 25ul
#define V9X_D3DFMT_A4R4G4B4 26ul
#define V9X_D3DFMT_D16      80ul
#define V9X_D3DFMT_D24X8    77ul

/* D3DFORMAT_OP_*, the DDK's ddrawi.h. */
#define V9X_D3DFORMAT_OP_TEXTURE                  0x00000001ul
#define V9X_D3DFORMAT_OP_CUBETEXTURE              0x00000004ul
#define V9X_D3DFORMAT_OP_SAME_FORMAT_RENDERTARGET 0x00000010ul
#define V9X_D3DFORMAT_OP_ZSTENCIL                 0x00000040ul
#define V9X_D3DFORMAT_OP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH 0x00000080ul
#define V9X_D3DFORMAT_OP_DISPLAYMODE              0x00000400ul
#define V9X_D3DFORMAT_OP_3DACCELERATION           0x00000800ul

/*
 * The DX8 forms of four DX7 cap words, matched to what the runtime makes
 * of a DDI 7 driver's (V9XD32 T17 on the 4570, boot 111).
 *
 * Texture filters: DX8 reads the MINF, MIPF and MAGF bits, so the DX5
 * words a DX7 driver may report alone (NEAREST to LINEARMIPLINEAR) add
 * them; the DX5 bits stay, as the runtime keeps them.
 * Texture caps: DX8's MIPMAP where the filters name any mip filter.
 * Raster: DX8 has no ROP2, XOR, SUBPIXEL, SUBPIXELX, STIPPLE, the
 * antialias sort bits or TRANSLUCENTSORTINDEPENDENT.
 * Misc: DX8 has no MASKPLANES or CONFORMANT.
 */
v9x_u32 v9x_d3d_caps8_filter(v9x_u32 dx7);
v9x_u32 v9x_d3d_caps8_texture(v9x_u32 dx7, v9x_u32 filter_dx7);
v9x_u32 v9x_d3d_caps8_raster(v9x_u32 dx7);
v9x_u32 v9x_d3d_caps8_misc(v9x_u32 dx7);

/* D3DLINECAPS for a device that draws lines (as triangles, so with what
 * triangles have), or 0. */
v9x_u32 v9x_d3d_caps8_lines(int drawn);

/* The D3DFORMAT of an RGB DDPIXELFORMAT's flags, bit count and masks, or
 * 0 for one with no DX8 name here. */
v9x_u32 v9x_d3d_caps8_format_of(v9x_u32 flags, v9x_u32 bits, v9x_u32 red,
                                v9x_u32 green, v9x_u32 blue, v9x_u32 alpha);

typedef struct v9x_d3d_format8 {
    v9x_u32 format;              /* D3DFORMAT                      */
    v9x_u32 operations;          /* D3DFORMAT_OP_*                 */
} V9X_D3D_FORMAT8;

#define V9X_D3D_RENDERS_16 0x1ul
#define V9X_D3D_RENDERS_32 0x2ul

/*
 * The format list: every texture format (D3DFORMATs, zeros skipped), the
 * display and render formats for renders (V9X_D3D_RENDERS_*), and depth
 * formats for depth16_bits (16: D16) and depth32_bits (24: D24X8), each
 * with the colour depth free, as the DDI 7 description already allows
 * any pairing. One entry per format, its operations merged. Writes at
 * most room entries; returns how many there are.
 */
v9x_u32 v9x_d3d_caps8_formats(const v9x_u32 *textures, v9x_u32 texture_count,
                              v9x_u32 renders, v9x_u32 depth16_bits,
                              v9x_u32 depth32_bits, V9X_D3D_FORMAT8 *out,
                              v9x_u32 room);

#endif
