/*
 * The neutral draw description to VSA-100 3D builder state and vertices.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_tdfx_map.c holds it to its rules. What depends on
 * DirectDraw surfaces - a texture's format, size, pitch and VRAM offset -
 * arrives already resolved by d3d_tdfx.c, which owns them.
 *
 * Accepted is what TDFX3D measured on the Voodoo4 4500 (docs\decisions\
 * 2026-10-12-voodoo4-3d-engine-draws-pixel-exact.md) and what the builder
 * expresses from the Napalm spec beside it; everything else is a refusal
 * with a reason, Direct3D's skip-and-count. Plan: docs\plans\3dfx-vsa100-
 * d3d.md, step 3.
 */
#ifndef VELOCITY9X_D3D_TDFX_MAP_H
#define VELOCITY9X_D3D_TDFX_MAP_H

#include "velocity9x/types.h"
#include "velocity9x/tdfx_3d.h"
#include "../r3d/r3d.h"

/* A texture format no VSA-100 path here samples. */
#define V9X_D3D_TDFX_TEXTURE_UNKNOWN 0xfffffffful

/* The bound texture as the engine resolved it. format is V9X_TDFX_TEX_* or
 * V9X_D3D_TDFX_TEXTURE_UNKNOWN; offset is from the framebuffer mapping, as
 * every surface offset in a draw is. levels counts the mip levels found
 * packed after level 0, level 0 included; 0 or 1 is no chain. */
typedef struct v9x_d3d_tdfx_texture {
    v9x_u32 format;
    int has_alpha;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 offset;
    v9x_u32 pitch_bytes;
    v9x_u32 levels;
} V9X_D3D_TDFX_TEXTURE;

#define V9X_D3D_TDFX_REFUSE_NONE            0ul
#define V9X_D3D_TDFX_REFUSE_TARGET          1ul  /* not RGB565 */
#define V9X_D3D_TDFX_REFUSE_EXPLICIT        2ul  /* render-interface draw
                                                    the builder cannot
                                                    honour exactly */
#define V9X_D3D_TDFX_REFUSE_DEPTH_FUNC      3ul
#define V9X_D3D_TDFX_REFUSE_BLEND           4ul
#define V9X_D3D_TDFX_REFUSE_ALPHA_FUNC      5ul
#define V9X_D3D_TDFX_REFUSE_FOG             6ul  /* not built */
#define V9X_D3D_TDFX_REFUSE_SPECULAR        7ul  /* not built */
#define V9X_D3D_TDFX_REFUSE_COLOR_KEY       8ul  /* not built */
#define V9X_D3D_TDFX_REFUSE_TEXTURE_FORMAT  9ul
#define V9X_D3D_TDFX_REFUSE_TEXTURE_SHAPE   10ul
#define V9X_D3D_TDFX_REFUSE_TEXTURE_OP      11ul
#define V9X_D3D_TDFX_REFUSE_TEXTURE_ADDRESS 12ul
#define V9X_D3D_TDFX_REFUSE_TEXTURE_FILTER  13ul
#define V9X_D3D_TDFX_REFUSE_SHADE           14ul

/*
 * One draw's builder state. vram_base is where the framebuffer mapping
 * starts in video memory (the desktop's offset; d3d_tdfx.c derives it), and
 * every offset in the draw is moved by it, because the engine addresses
 * video memory from its first byte. vram_bytes is the memory the draw may
 * reach from vram_base. specular_rgb is non-zero when some vertex of the
 * batch carries specular colour. Returns a V9X_D3D_TDFX_REFUSE_* reason; on
 * NONE, state is ready for v9x_tdfx_3d_build_state, which makes the
 * remaining range checks.
 */
v9x_u32 v9x_d3d_tdfx_map_draw(const V9X_R3D_DRAW *draw,
                              const V9X_D3D_TDFX_TEXTURE *resolved,
                              v9x_u32 vram_base,
                              v9x_u32 vram_bytes,
                              v9x_u32 specular_rgb,
                              struct v9x_tdfx_3d_state *state);

/*
 * One triangle's vertices in the builder's units: colour channels 0-255
 * (vertex 0's throughout when flat), Z in depth counts (sz x 65535), 1/W
 * from RHW, and S/W, T/W as 256ths of the texture's width times RHW (the
 * unit TDFX3D measured). Zero when it draws nothing - zero area, a
 * coordinate that is not finite, or (textured) an RHW at or below zero -
 * and the caller skips it.
 */
int v9x_d3d_tdfx_triangle(const V9X_R3D_VERTEX *triangle,
                          v9x_u32 shade_mode,
                          int textured,
                          struct v9x_tdfx_3d_vertex *out);

#endif
