/*
 * The neutral draw description to MGA-2164W trapezoid state.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_mga_map.c holds it to its rules. What depends on
 * DirectDraw surfaces - the texture's format, shape, VRAM offset and colour
 * key - arrives already resolved by d3d_mga.c.
 *
 * Everything accepted is measured on A8U4I5 (docs\decisions\
 * 2026-10-10-mga2164w-*.md); the rest is a refusal with a reason,
 * Direct3D's skip-and-count. The chip has no alpha blending, no fog, no
 * specular, no filtering and no mipmaps: blending by source alpha is drawn
 * as the engine's screen-door stipple, one density a triangle, which is
 * what Matrox's own HAL publishes (ALPHAFLATSTIPPLED); every filter is
 * drawn as the nearest texel of the top level.
 */
#ifndef VELOCITY9X_D3D_MGA_MAP_H
#define VELOCITY9X_D3D_MGA_MAP_H

#include "velocity9x/mga_3d.h"
#include "../r3d/r3d.h"

/* The bound texture as d3d_mga.c resolved it. valid is zero for a
 * surface the sampler cannot read; the mapping then refuses the draw. */
typedef struct v9x_d3d_mga_texture {
    v9x_u32 valid;
    v9x_u32 format;             /* V9X_MGA3D_TEX_TW16 or _TW15 */
    v9x_u32 offset;
    v9x_u32 log2_width;
    v9x_u32 log2_height;
    v9x_u32 log2_pitch;
    v9x_u32 has_color_key;
    v9x_u32 color_key;          /* the surface's source key, low value */
} V9X_D3D_MGA_TEXTURE;

/*
 * A mapped draw. base is the trapezoid template v9x_mga_setup_triangle
 * fills per triangle (target, depth, texture, shading, dither). skip is
 * set when nothing would pass (a NEVER depth test), stipple when each
 * triangle takes a screen-door density from its alpha, flat when every
 * triangle takes its first vertex's colour.
 */
typedef struct v9x_d3d_mga_mapped {
    struct v9x_mga3d_trap base;
    v9x_u32 skip;
    v9x_u32 stipple;
    v9x_u32 flat;
} V9X_D3D_MGA_MAPPED;

#define V9X_D3D_MGA_REFUSE_NONE           0ul
#define V9X_D3D_MGA_REFUSE_TARGET         1ul  /* not 565 or 1555 */
#define V9X_D3D_MGA_REFUSE_EXPLICIT       2ul  /* render-interface draw */
#define V9X_D3D_MGA_REFUSE_TARGET_SHAPE   3ul  /* pitch or origin */
#define V9X_D3D_MGA_REFUSE_BLEND          4ul
#define V9X_D3D_MGA_REFUSE_ALPHA_TEST     5ul
#define V9X_D3D_MGA_REFUSE_FOG            6ul
#define V9X_D3D_MGA_REFUSE_SPECULAR       7ul
#define V9X_D3D_MGA_REFUSE_DEPTH          8ul  /* pitch, ZORG */
#define V9X_D3D_MGA_REFUSE_TEXTURE        9ul  /* format, shape, place */
#define V9X_D3D_MGA_REFUSE_TEXTURE_OP     10ul
#define V9X_D3D_MGA_REFUSE_ADDRESS        11ul
#define V9X_D3D_MGA_REFUSE_KEYS           12ul /* colour key and alpha
                                                  test both want TEXTRANS */
#define V9X_D3D_MGA_REFUSE_SETUP          13ul /* a triangle's planes
                                                  overflow (counted per
                                                  triangle by d3d_mga.c) */

/*
 * One draw. specular_rgb is non-zero when some vertex of the batch carries
 * specular colour: SPECULARENABLE defaults on, and specular that adds
 * nothing is no reason to refuse. Returns a V9X_D3D_MGA_REFUSE_* reason;
 * on NONE, out is ready for the setup.
 */
v9x_u32 v9x_d3d_mga_map_draw(const V9X_R3D_DRAW *draw,
                             const V9X_D3D_MGA_TEXTURE *texture,
                             v9x_u32 vram_bytes, v9x_u32 specular_rgb,
                             V9X_D3D_MGA_MAPPED *out);

/*
 * The DWGCTL.trans pattern for a source alpha, 0 to 255: the nearest of
 * the engine's densities - all, 8, 4 or 2 of every 16 pixels - or
 * V9X_D3D_MGA_STIPPLE_NONE when the triangle is too faint to draw any
 * (the patterns are decoded in docs\specifications\mga2164w-3d-engine.md).
 */
#define V9X_D3D_MGA_STIPPLE_NONE 15ul
/* 8 of every 16 pixels (pattern 0001). */
#define V9X_D3D_MGA_STIPPLE_HALF 1ul
v9x_u32 v9x_d3d_mga_stipple(v9x_u32 alpha);

#endif
