/*
 * What the Haswell engine refuses of a render-interface draw.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_hsw_map.c holds it to its rules. Step 1 of
 * docs\plans\intel-haswell-glide-3dcif-opengl.md: an explicit draw is
 * honoured exactly or refused (r3d.h, explicit_state), and before this
 * d3d_hsw.c read neither the CPU levels nor the scissor nor the mask, so
 * it drew a CPU-level texture untextured and wrote outside a scissor.
 */
#ifndef VELOCITY9X_D3D_HSW_MAP_H
#define VELOCITY9X_D3D_HSW_MAP_H

#include "velocity9x/types.h"
#include "velocity9x/intel_hsw_eu.h"
#include "velocity9x/intel_hsw_3d.h"
#include "velocity9x/r3d_abi.h"
#include "../r3d/r3d.h"

/* Refusal reasons, continuing d3d_hsw.c's V9X_D3D_HSW_REFUSE_* (0-8) in the
 * same counters (V9XTRACE M64PolicyNN). */
#define V9X_D3D_HSW_REFUSE_CPU_TEXTURE 9ul   /* texture.levels: no sampler */
#define V9X_D3D_HSW_REFUSE_COMBINE     10ul  /* a two-unit combine with no
                                                exact kernel stage       */

/* d3d_internal.h's V9X_D3D_PLACE_ALLOC, "the heap had no room", which that
 * DDHAL header keeps from the host build; d3d_hsw.c asserts the two equal. */
#define V9X_D3D_HSW_PLACE_ALLOC 4ul

/*
 * 1 when a texture tree the engine could not place is answered
 * DDERR_OUTOFVIDEOMEMORY rather than left to DirectDraw: the heap had no
 * room (place_reason V9X_D3D_HSW_PLACE_ALLOC) and the format is not RGB -
 * DXT or signed, with no bit count DirectDraw could size a surface by.
 * The runtime then evicts managed textures and tries again. 0 otherwise.
 */
int v9x_d3d_hsw_unplaced_out_of_memory(v9x_u32 place_reason, int rgb);

/* The RGB write mask, V9X_R3D_ABI_WRITE_RED | GREEN | BLUE. */
#define V9X_D3D_HSW_WRITE_RGB 7ul

/*
 * Zero when nothing the render interface states stops this engine drawing
 * the draw exactly, or the reason: a CPU-level texture, on any draw
 * (Direct3D never sets one). The scissor and mask are drawn since step 2,
 * through the helpers below.
 */
v9x_u32 v9x_d3d_hsw_explicit_refusal(const V9X_R3D_DRAW *draw);

/* BLEND_STATE's write-disable nibble (A R G B, 27:24) for the render
 * interface's RGB mask; alpha always written. Step 2. */
v9x_u8 v9x_d3d_hsw_write_disable(v9x_u32 write_mask);

/*
 * The render target's SURFACE_STATE format for a render-interface layout,
 * and the write-disable bits it needs ORed into BLEND_STATE: 565 and 1555
 * as before, and XRGB8888 (a 32 bpp desktop,
 * docs\plans\32bpp-acceleration-and-3d.md) as B8G8R8A8_UNORM - the format
 * the Phase D and E scenes rendered on this chip - with alpha writes off,
 * so the X byte keeps what the clear wrote, as the Gen3 and software
 * engines do. 0 for any other layout, nothing written.
 */
int v9x_d3d_hsw_target(v9x_u32 r3d_format, v9x_u16 *format,
                       v9x_u8 *write_disable);

/*
 * A texture's pixel layout as the sampler's SURFACE_STATE format, whether
 * that format carries alpha, and its bytes a texel. Given the
 * DDPIXELFORMAT's bit count, whether it says it has alpha, and its four
 * masks. Five layouts: RGB565, ARGB1555 and ARGB4444 as before, and since
 * 2026-10-20 ARGB8888 as B8G8R8A8_UNORM and XRGB8888 as B8G8R8X8_UNORM,
 * whose alpha the sampler reads as 1 whatever the X byte holds. 0 for any
 * other layout, nothing written.
 */
int v9x_d3d_hsw_texel_format(v9x_u32 bits, int alpha_pixels, v9x_u32 red,
                             v9x_u32 green, v9x_u32 blue, v9x_u32 alpha,
                             v9x_u16 *format, int *has_alpha,
                             v9x_u32 *bytes);

/*
 * A signed (bump) texture, step 5d of docs\plans\haswell-ddi8-pixel-
 * shaders.md: 3DMark 2001 SE's Nature needs Q8W8V8U8 or W11V11U10 for
 * its "bump normal maps", and V8U8 or V16U16 for environment bump maps
 * (its D3D8 renderer's CheckDeviceFormat pairs, read from the binary).
 * Given the DDPIXELFORMAT's flags, FourCC, bit count and first three
 * masks: DDPF_D3DFORMAT (or DDPF_FOURCC) with D3DFMT_Q8W8V8U8 (63) is
 * R8G8B8A8_SNORM, 4 bytes, with alpha (Q); with D3DFMT_V8U8 (60), or
 * DirectX 7's DDPF_BUMPDUDV of 16 bits, du FFh, dv FF00h and no
 * luminance, is R8G8_SNORM, 2 bytes. Bytes run U V W Q from the lowest,
 * which is R G B A. 0 for anything else, nothing written.
 */
#define V9X_D3D_HSW_DDPF_FOURCC    0x00000004ul
#define V9X_D3D_HSW_DDPF_BUMPDUDV  0x00080000ul
#define V9X_D3D_HSW_DDPF_D3DFORMAT 0x00200000ul
#define V9X_D3D_HSW_D3DFMT_V8U8     60ul
#define V9X_D3D_HSW_D3DFMT_Q8W8V8U8 63ul

int v9x_d3d_hsw_signed_format(v9x_u32 flags, v9x_u32 fourcc, v9x_u32 bits,
                              v9x_u32 first, v9x_u32 second, v9x_u32 third,
                              v9x_u16 *format, int *has_alpha,
                              v9x_u32 *bytes);

/*
 * A DXT texture's pixel format (flags, FourCC) as the BC format the
 * sampler reads, and its block's bytes: DXT1 BC1 (8), DXT3 BC2 (16), DXT5
 * BC3 (16), named by FourCC or by D3DFORMAT, whose value is the same code.
 * DXT1's punch-through alpha counts as alpha. The premultiplied DXT2 and
 * DXT4 are not published and refused. 0 for any other format, nothing
 * written. Nature in 3DMark 2001 SE draws its distant terrain and
 * mountains with DXT1 and DXT3 (the 4570, boot 154).
 */
#define V9X_D3D_HSW_FOURCC_DXT1 0x31545844ul
#define V9X_D3D_HSW_FOURCC_DXT3 0x33545844ul
#define V9X_D3D_HSW_FOURCC_DXT5 0x35545844ul

int v9x_d3d_hsw_bc_format(v9x_u32 flags, v9x_u32 fourcc, v9x_u16 *format,
                          int *has_alpha, v9x_u32 *block_bytes);

/*
 * The Z buffer's 3DSTATE_DEPTH_BUFFER (and 3DSTATE_SF) format for its cell:
 * 2 bytes D16_UNORM, as F2b built it, or 4 bytes D24_UNORM_X8_UINT, 24 bits
 * of depth beside 16 (docs\plans\32bpp-acceleration-and-3d.md, decision 1).
 * 0 for any other cell, nothing written.
 */
int v9x_d3d_hsw_depth_format(v9x_u32 cell_bytes, v9x_u8 *format);

/* A depth fill's value as the z the depth-only draw writes: the value's
 * low 16 bits over 65535 for a 2-byte cell, its low 24 over 16777215 for a
 * 4-byte one, which is what each format stores (round(z x max)). */
float v9x_d3d_hsw_fill_z(v9x_u32 value, v9x_u32 cell_bytes);

/*
 * The draw's scissor as the inclusive rectangle 3DSTATE_DRAWING_RECTANGLE
 * takes, clamped to the target, in rect[0..3] = x0, y0, x1, y1: the whole
 * target for a draw that states none. 0 when it is empty, and nothing is
 * to be drawn. Step 2.
 */
int v9x_d3d_hsw_scissor(const V9X_R3D_DRAW *draw, v9x_u32 rect[4]);

/*
 * A two-unit draw's combines (V9X_R3D_ABI_COLOROP_* / _ALPHAOP_*, unit 1's
 * "fragment" being unit 0's result, OpenGL 1.2.1 table 3.18) as the
 * kernel generator's stages (intel_hsw_eu.h), exactly or not at all. Step
 * 4. has_alpha0/1 say whether the texture's format carries alpha: without
 * it the sampler returns alpha 1, so combines that differ only in what they
 * do with the texture's alpha agree. Zero, with *stage0 and *stage1 set, or
 * V9X_D3D_HSW_REFUSE_COMBINE.
 */
v9x_u32 v9x_d3d_hsw_two_unit_stages(v9x_u32 color0, v9x_u32 alpha0,
                                    int has_alpha0, v9x_u32 color1,
                                    v9x_u32 alpha1, int has_alpha1,
                                    v9x_u8 *stage0, v9x_u8 *stage1);

#endif
