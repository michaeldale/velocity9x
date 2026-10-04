/*
 * The neutral draw description to SiS 6326 state, texture and triangle
 * words.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_sis6326_map.c holds it to its rules. What depends on
 * DirectDraw surfaces - a texture's format, size and VRAM offsets - arrives
 * already resolved by d3d_sis6326.c, which owns them.
 *
 * Everything accepted here was measured on A8U4I5 (docs\decisions\
 * 2026-10-05-sis6326-3d-*.md) or follows from a measured encoding; the rest
 * is a refusal with a reason, Direct3D's skip-and-count.
 */
#ifndef VELOCITY9X_D3D_SIS6326_MAP_H
#define VELOCITY9X_D3D_SIS6326_MAP_H

#include "velocity9x/types.h"
#include "velocity9x/sis6326_3d.h"
#include "../r3d/r3d.h"

/* A texture format no 6326 sampler reads. */
#define V9X_D3D_SIS_TEXTURE_UNKNOWN 0xfffffffful
/* Level 0 and the nine levels after it. */
#define V9X_D3D_SIS_TEXTURE_LEVELS  (V9X_SIS3D_MIP_LEVELS_MAX + 1u)

/* The bound texture as the engine resolved it. format is V9X_SIS3D_TEXEL_*
 * or V9X_D3D_SIS_TEXTURE_UNKNOWN; levels is the usable chain length, and
 * level_offsets[n] level n's VRAM offset (level_offsets[0] is offset). */
typedef struct v9x_d3d_sis_texture {
    v9x_u32 format;
    int has_alpha;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 levels;
    v9x_u32 offset;
    v9x_u32 pitch_bytes;
    v9x_u32 level_offsets[V9X_D3D_SIS_TEXTURE_LEVELS];
} V9X_D3D_SIS_TEXTURE;

#define V9X_D3D_SIS_REFUSE_NONE            0ul
#define V9X_D3D_SIS_REFUSE_TARGET          1ul  /* not RGB565 */
#define V9X_D3D_SIS_REFUSE_EXPLICIT        2ul  /* render-interface draw */
#define V9X_D3D_SIS_REFUSE_DEPTH_FUNC      3ul
#define V9X_D3D_SIS_REFUSE_BLEND           4ul
#define V9X_D3D_SIS_REFUSE_ALPHA_FUNC      5ul
#define V9X_D3D_SIS_REFUSE_FOG             6ul  /* not measured */
#define V9X_D3D_SIS_REFUSE_SPECULAR        7ul  /* not measured */
#define V9X_D3D_SIS_REFUSE_COLOR_KEY       8ul  /* not measured */
#define V9X_D3D_SIS_REFUSE_TEXTURE_FORMAT  9ul
#define V9X_D3D_SIS_REFUSE_TEXTURE_SHAPE   10ul
#define V9X_D3D_SIS_REFUSE_TEXTURE_OP      11ul
#define V9X_D3D_SIS_REFUSE_TEXTURE_ADDRESS 12ul
#define V9X_D3D_SIS_REFUSE_TEXTURE_FILTER  13ul
#define V9X_D3D_SIS_REFUSE_SHADE           14ul

/*
 * One draw's state and, when textured, its texture words' description.
 * specular_rgb is non-zero when some vertex of the batch carries specular
 * colour: SPECULARENABLE defaults on, and specular that adds nothing is no
 * reason to refuse. Returns a V9X_D3D_SIS_REFUSE_* reason; on NONE, state
 * and texture are ready for v9x_sis3d_build_state and
 * v9x_sis3d_build_texture, which make the remaining range checks. Every
 * draw has texture words: an untextured one samples a dummy texel and takes
 * its colour from the vertex, because an untextured batch before a textured
 * one stalls the engine. *textured says whether the vertices' U, V and W
 * are used.
 */
v9x_u32 v9x_d3d_sis_map_draw(const V9X_R3D_DRAW *draw,
                             const V9X_D3D_SIS_TEXTURE *resolved,
                             v9x_u32 vram_bytes,
                             v9x_u32 specular_rgb,
                             struct v9x_sis3d_state *state,
                             struct v9x_sis3d_texture *texture,
                             int *textured);

/*
 * One triangle's vertex registers and primitive word. Zero when it draws
 * nothing - zero area, a coordinate that is not finite, or (textured) an
 * RHW at or below zero - and the caller skips it.
 */
int v9x_d3d_sis_triangle(const V9X_R3D_VERTEX *triangle,
                         v9x_u32 shade_mode,
                         int textured,
                         struct v9x_sis3d_vertex *out,
                         v9x_u32 *primitive);

#endif
