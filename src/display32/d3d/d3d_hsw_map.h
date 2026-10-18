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
