/*
 * Glide 2.x API values (docs\plans\glide-2x-wrapper.md), numbered as the
 * Glide 2.4 Reference Manual (3Dfx Interactive, 1997) and its glide.h
 * number them. Facts only: no 3dfx source is copied (licence rule in
 * docs\plans\3dfx-voodoo3-prior-work.md).
 *
 * The values NFS II SE exercised agree with what the census logged
 * (docs\decisions\2026-10-08-nfs2se-glide-census.md): W-buffer 2, depth
 * functions 3 and 7, fog mode 2, combine function 1 and 3 with factor 1,
 * blend factors 1, 4 and 5, formats 5, 10, 11 and 12, aspect 3, clamp 1
 * and wrap 0, bilinear 1. The rest are carried for the mapping and are
 * checked when a title first uses them.
 *
 * Pure: no Windows headers. Shared by glide_dll.c and the host tests.
 */
#ifndef VELOCITY9X_GLIDE_API_H
#define VELOCITY9X_GLIDE_API_H

#include "velocity9x/types.h"

/* GrOriginLocation_t. */
#define V9X_GLIDE_ORIGIN_UPPER_LEFT 0ul
#define V9X_GLIDE_ORIGIN_LOWER_LEFT 1ul

/* GrCmpFnc_t: NEVER .. ALWAYS. Direct3D's D3DCMPFUNC is the same order
 * from 1, which is what the render interface takes. */
#define V9X_GLIDE_CMP_NEVER  0ul
#define V9X_GLIDE_CMP_ALWAYS 7ul

/* GrDepthBufferMode_t. */
#define V9X_GLIDE_DEPTH_DISABLE 0ul
#define V9X_GLIDE_DEPTH_ZBUFFER 1ul
#define V9X_GLIDE_DEPTH_WBUFFER 2ul

/* GrCullMode_t: which signed area is discarded. */
#define V9X_GLIDE_CULL_DISABLE  0ul
#define V9X_GLIDE_CULL_NEGATIVE 1ul
#define V9X_GLIDE_CULL_POSITIVE 2ul

/* GrFogMode_t; the low byte is the source, higher bits are modifiers. */
#define V9X_GLIDE_FOG_DISABLE        0ul
#define V9X_GLIDE_FOG_ITERATED_ALPHA 1ul
#define V9X_GLIDE_FOG_TABLE          2ul
#define V9X_GLIDE_FOG_SOURCE_MASK    0xfful
#define V9X_GLIDE_FOG_TABLE_SIZE     64u

/* GrChromakeyMode_t. */
#define V9X_GLIDE_CHROMAKEY_DISABLE 0ul
#define V9X_GLIDE_CHROMAKEY_ENABLE  1ul

/* GrAlphaBlendFnc_t. 2 and 6 name a source colour as a destination
 * factor and a destination colour as a source factor. */
#define V9X_GLIDE_BLEND_ZERO                0ul
#define V9X_GLIDE_BLEND_SRC_ALPHA           1ul
#define V9X_GLIDE_BLEND_COLOR               2ul
#define V9X_GLIDE_BLEND_DST_ALPHA           3ul
#define V9X_GLIDE_BLEND_ONE                 4ul
#define V9X_GLIDE_BLEND_ONE_MINUS_SRC_ALPHA 5ul
#define V9X_GLIDE_BLEND_ONE_MINUS_COLOR     6ul
#define V9X_GLIDE_BLEND_ONE_MINUS_DST_ALPHA 7ul
#define V9X_GLIDE_BLEND_ALPHA_SATURATE      15ul

/* GrCombineFunction_t (the ones mapped; the rest are reported). */
#define V9X_GLIDE_COMBINE_FUNCTION_ZERO        0ul
#define V9X_GLIDE_COMBINE_FUNCTION_LOCAL       1ul
#define V9X_GLIDE_COMBINE_FUNCTION_LOCAL_ALPHA 2ul
#define V9X_GLIDE_COMBINE_FUNCTION_SCALE_OTHER 3ul

/* GrCombineFactor_t (the ones mapped). */
#define V9X_GLIDE_COMBINE_FACTOR_ZERO  0ul
#define V9X_GLIDE_COMBINE_FACTOR_LOCAL 1ul
#define V9X_GLIDE_COMBINE_FACTOR_ONE   8ul

/* GrCombineLocal_t and GrCombineOther_t. */
#define V9X_GLIDE_COMBINE_LOCAL_ITERATED 0ul
#define V9X_GLIDE_COMBINE_LOCAL_CONSTANT 1ul
#define V9X_GLIDE_COMBINE_OTHER_ITERATED 0ul
#define V9X_GLIDE_COMBINE_OTHER_TEXTURE  1ul
#define V9X_GLIDE_COMBINE_OTHER_CONSTANT 2ul

/* GrTextureClampMode_t and GrTextureFilterMode_t. */
#define V9X_GLIDE_TEXTURE_WRAP     0ul
#define V9X_GLIDE_TEXTURE_CLAMP    1ul
#define V9X_GLIDE_TEXTURE_POINT    0ul
#define V9X_GLIDE_TEXTURE_BILINEAR 1ul

/* GrTextureFormat_t. Below 8 a texel is 8 bits, from 8 up 16 bits. */
#define V9X_GLIDE_TEXFMT_RGB_332            0ul
#define V9X_GLIDE_TEXFMT_YIQ_422            1ul
#define V9X_GLIDE_TEXFMT_ALPHA_8            2ul
#define V9X_GLIDE_TEXFMT_INTENSITY_8        3ul
#define V9X_GLIDE_TEXFMT_ALPHA_INTENSITY_44 4ul
#define V9X_GLIDE_TEXFMT_P_8                5ul
#define V9X_GLIDE_TEXFMT_ARGB_8332          8ul
#define V9X_GLIDE_TEXFMT_AYIQ_8422          9ul
#define V9X_GLIDE_TEXFMT_RGB_565            10ul
#define V9X_GLIDE_TEXFMT_ARGB_1555          11ul
#define V9X_GLIDE_TEXFMT_ARGB_4444          12ul
#define V9X_GLIDE_TEXFMT_ALPHA_INTENSITY_88 13ul
#define V9X_GLIDE_TEXFMT_AP_88              14ul
#define V9X_GLIDE_TEXFMT_16BIT              8ul

/* GrLOD_t: GR_LOD_256 (0) to GR_LOD_1 (8); the largest edge is
 * 256 >> lod. GrAspectRatio_t: 8x1 (0) to 1x8 (6), 1x1 at 3. */
#define V9X_GLIDE_LOD_256     0ul
#define V9X_GLIDE_LOD_1       8ul
#define V9X_GLIDE_LOD_EDGE    256ul
#define V9X_GLIDE_ASPECT_8X1  0ul
#define V9X_GLIDE_ASPECT_1X1  3ul
#define V9X_GLIDE_ASPECT_1X8  6ul

/* GrMipMapLevelMask_t: which LODs of a chain a download or source names.
 * EVEN is the even LOD numbers (256, 64, 16, ...). */
#define V9X_GLIDE_MIPMAPLEVELMASK_EVEN 1ul
#define V9X_GLIDE_MIPMAPLEVELMASK_ODD  2ul
#define V9X_GLIDE_MIPMAPLEVELMASK_BOTH 3ul

/* GrTexTable_t: the NCC tables and the 256-entry palette. */
#define V9X_GLIDE_TEXTABLE_NCC0    0ul
#define V9X_GLIDE_TEXTABLE_NCC1    1ul
#define V9X_GLIDE_TEXTABLE_PALETTE 2ul
#define V9X_GLIDE_PALETTE_ENTRIES  256u

/*
 * The snap bias: many titles add 3 << 18 to x and y so the float's low
 * mantissa bits hold the fixed-point screen coordinate the hardware wants
 * (NFS II SE does, census). A coordinate at or above 1 << 19 carries it.
 */
#define V9X_GLIDE_SNAP_BIAS      786432.0f
#define V9X_GLIDE_SNAP_THRESHOLD 524288.0f

#endif /* VELOCITY9X_GLIDE_API_H */
