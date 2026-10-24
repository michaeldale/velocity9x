/*
 * The neutral draw description to Rage 128 3D builder state.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_r128_map.c holds it to its rules. What depends on
 * DirectDraw surfaces - a texture's format, size, pitch and the VRAM offset
 * of each level - arrives already resolved by d3d_r128.c, which owns them.
 *
 * Accepted is what the builder (src\chipsets\ati\rage128\r128_3d.c)
 * expresses; the CCE probe drew its Z16, texture and Gouraud streams right
 * (docs\decisions\2026-10-23-rage128-3d-builder-gouraud-z-texture.md).
 * Everything else is a refusal with a reason, Direct3D's skip-and-count.
 * Plan: docs\plans\ati-rage128.md, Phase 5.
 *
 * The vertices need no translation: V9X_R3D_VERTEX is the CCE's TL vertex
 * dword for dword (r128_3d.h).
 */
#ifndef VELOCITY9X_D3D_R128_MAP_H
#define VELOCITY9X_D3D_R128_MAP_H

#include "velocity9x/types.h"
#include "velocity9x/r128_3d.h"
#include "../r3d/r3d.h"

/* A texture format no Rage 128 path here samples. */
#define V9X_D3D_R128_TEXTURE_UNKNOWN 0xfffffffful

/* The bound texture as the engine resolved it. datatype is V9X_R128_TEX_*
 * or V9X_D3D_R128_TEXTURE_UNKNOWN; offsets are from the framebuffer
 * mapping, as every surface offset in a draw is, one per level found;
 * levels counts them, level 0 included. */
typedef struct v9x_d3d_r128_texture {
    v9x_u32 datatype;
    int has_alpha;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 bytes_per_texel;
    v9x_u32 pitch_bytes;
    v9x_u32 levels;
    v9x_u32 offsets[V9X_R128_TEXTURE_LEVELS];
} V9X_D3D_R128_TEXTURE;

#define V9X_D3D_R128_REFUSE_NONE            0ul
#define V9X_D3D_R128_REFUSE_TARGET          1ul  /* not RGB565/XRGB8888 */
#define V9X_D3D_R128_REFUSE_EXPLICIT        2ul  /* render-interface draw:
                                                    CPU levels, a second
                                                    unit, or a scissor
                                                    outside the target */
#define V9X_D3D_R128_REFUSE_DEPTH_FUNC      3ul
#define V9X_D3D_R128_REFUSE_BLEND           4ul
#define V9X_D3D_R128_REFUSE_ALPHA_FUNC      5ul
#define V9X_D3D_R128_REFUSE_DEPTH_SURFACE   6ul  /* not Z16 */
#define V9X_D3D_R128_REFUSE_COLOR_KEY       8ul  /* not built */
#define V9X_D3D_R128_REFUSE_TEXTURE_FORMAT  9ul
#define V9X_D3D_R128_REFUSE_TEXTURE_SHAPE   10ul
#define V9X_D3D_R128_REFUSE_TEXTURE_OP      11ul
#define V9X_D3D_R128_REFUSE_TEXTURE_ADDRESS 12ul
#define V9X_D3D_R128_REFUSE_TEXTURE_FILTER  13ul
#define V9X_D3D_R128_REFUSE_SHADE           14ul
#define V9X_D3D_R128_REFUSE_STATE           15ul /* the builder refused */

/*
 * One draw's builder state. vram_base is where the framebuffer mapping
 * starts in video memory, and every offset in the draw is moved by it,
 * because the engine addresses video memory from its first byte.
 * vram_bytes is the memory the draw may reach from vram_base. depth_bytes
 * is the bound depth surface's bytes per pixel, 0 when none is bound.
 * resolved1 is the second unit's texture, for a draw with texcoords1.
 * Returns a V9X_D3D_R128_REFUSE_* reason; on NONE, state is ready for
 * v9x_r128_3d_build_state, which makes the remaining range checks.
 */
v9x_u32 v9x_d3d_r128_map_draw(const V9X_R3D_DRAW *draw,
                              const V9X_D3D_R128_TEXTURE *resolved,
                              const V9X_D3D_R128_TEXTURE *resolved1,
                              v9x_u32 vram_base,
                              v9x_u32 vram_bytes,
                              v9x_u32 depth_bytes,
                              struct v9x_r128_3d_state *state);

#endif
