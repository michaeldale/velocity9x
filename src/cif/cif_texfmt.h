/*
 * ATI3DCIF texel formats to the render interface's 16-bit formats
 * (docs\plans\ati-3dcif-wrapper.md, Phase 2): the formats the census saw
 * (docs\decisions\2026-10-14-3dcif-demo-census.md) - CI8 through a
 * palette, RGB1555, RGB332, RGB4444 and VQ - plus RGB565, RGB8888 and CI4,
 * which the guide lists beside them and cost a line each. Y8 and YUV422
 * are not converted.
 *
 * 1555 is taken in the hardware's sense, alpha set = opaque: ATI's DLL
 * hands textures to the chip unconverted, and the chip reads ARGB1555 the
 * way Direct3D's surfaces give it, which our Mach64 path already feeds it.
 *
 * Chroma key (C3D_ETEXOP_CHROMAKEY) is done here: a texel whose colour
 * equals the key gets alpha 0, in a format with alpha, and the state
 * module adds the alpha test that discards it. The comparison is in the
 * texel's own precision - the key's top bits against the texel's field -
 * except for palette formats, whose 8-bit entries are compared whole.
 * Which the chip does is not documented; Phase 3 measures it.
 *
 * Textures in the Rage PRO's tiled layout (eTexTiling ON) are not
 * converted. Only the Knight demos tile, and they do so because of their
 * WIN.INI [AtiDemo] TexAddrTiling key, not the caps.
 *
 * Pure: no Windows headers.
 */
#ifndef VELOCITY9X_CIF_TEXFMT_H
#define VELOCITY9X_CIF_TEXFMT_H

#include "velocity9x/types.h"
#include "velocity9x/r3d_abi.h"
#include "cif_api.h"

/* Bits a texel in `format` occupies in the source, 0 for one this module
 * does not convert. CI4 is a byte (its index in one nibble); VQ is one
 * index byte per 2x2 block: 2 bits a texel. */
v9x_u32 v9x_cif_texfmt_bits(v9x_u32 format);

/* The interface format a conversion produces, 0 when not converted.
 * `keyed` asks for an alpha the key can clear. */
v9x_u32 v9x_cif_texfmt_abi_format(v9x_u32 format, v9x_u32 keyed);

/*
 * Converts one row of `count` texels. `palette` is 256 C3D_PALETTENTRY
 * (red in the low byte) and is read for CI4 and CI8 only; `nibble` is
 * V9X_C3D_ECI_TMAP_4BIT_LOW or _HI for CI4 (two textures share a byte,
 * one per nibble). `key` is a C3D_COLOR. V9X_FALSE for a format not
 * converted, a palette format without a palette, or VQ (see below).
 */
v9x_u16 v9x_cif_texfmt_convert_row(v9x_u32 format, const void *src,
                                   v9x_u32 count, const v9x_u32 *palette,
                                   v9x_u32 nibble, v9x_u32 keyed,
                                   v9x_u32 key, v9x_u16 *dst);

/*
 * VQ: one index byte per 2x2 block, `width` / 2 bytes a row of blocks at
 * `index_pitch`, into a codebook of 256 entries of four 16-bit texels in
 * the order upper-left, upper-right, lower-left, lower-right (guide,
 * "Vector Quantization (VQ) Compression"). Writes `width` x `height`
 * 16-bit texels at `dst_pitch` bytes, as the codebook holds them; which
 * 16-bit format the codebook is in is the caller's (the guide does not
 * say). Width and height are even; V9X_FALSE otherwise.
 */
v9x_u16 v9x_cif_texfmt_vq_decode(const v9x_u8 *indices, v9x_u32 index_pitch,
                                 const v9x_u16 *codebook, v9x_u32 width,
                                 v9x_u32 height, v9x_u16 *dst,
                                 v9x_u32 dst_pitch);

/*
 * Rewrites `count` texels of a V9X_R3D_ABI_FORMAT_* layout in place as
 * RGB565, dropping alpha: the copy for a draw that does not read the
 * texture's alpha (cif_state.h's texel_alpha). V9X_FALSE for a layout
 * this does not know.
 */
v9x_u16 v9x_cif_texfmt_to_565(v9x_u32 abi_format, v9x_u16 *texels,
                              v9x_u32 count);

/*
 * Levels the interface gets for a texture of 2^xlg2 by 2^ylg2: one
 * without mipmaps, else down to 1x1, at most V9X_R3D_ABI_LEVELS_MAX. A
 * 1024-texel edge has eleven levels; the smallest is dropped.
 */
v9x_u32 v9x_cif_texfmt_levels(v9x_u32 xlg2, v9x_u32 ylg2, v9x_u32 mipmapped);

#endif /* VELOCITY9X_CIF_TEXFMT_H */
