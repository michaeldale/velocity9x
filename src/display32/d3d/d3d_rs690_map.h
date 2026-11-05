/*
 * The neutral draw description to RS690 3D builder state.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_rs690_map.c holds it to its rules. What depends on
 * DirectDraw surfaces - a texture's format, size, pitch and offset - arrives
 * already resolved by d3d_rs690.c, which owns them. d3d_r128_map.h is the
 * model.
 *
 * Accepted is what the builder (src\chipsets\ati\rs690\rs690_3d.c) expresses.
 * The probes drew most of it right on M-GB8KRE: Gouraud TL vertices,
 * textures nearest or bilinear, wrapped or clamped, REPLACE or MODULATE,
 * Z16, blend, the alpha test (docs\decisions\2026-11-05-rs690-texture-
 * probe.md, 2026-11-05-rs690-z-blend-probe.md). Flat shading and specular
 * are Mesa's encodings, not yet probed. Everything else is a refusal with a
 * reason, Direct3D's skip-and-count, and the core's software engine draws
 * it. The vertices need no translation: V9X_R3D_VERTEX is the builder's TL
 * vertex dword for dword.
 *
 * Plan: docs\plans\ati-rs690-x1200.md, phase 5.
 */
#ifndef VELOCITY9X_D3D_RS690_MAP_H
#define VELOCITY9X_D3D_RS690_MAP_H

#include "velocity9x/types.h"
#include "velocity9x/rs690_3d.h"
#include "../r3d/r3d.h"

/* A texture format no RS690 path here samples. */
#define V9X_D3D_RS690_TEXTURE_UNKNOWN 0xfffffffful

/* The bound texture as the engine resolved it: format is V9X_RS690_TEXFMT_*
 * or V9X_D3D_RS690_TEXTURE_UNKNOWN; offset is from the framebuffer mapping,
 * as every surface offset in a draw is. Level 0 only: mip chains are placed
 * but not sampled. */
typedef struct v9x_d3d_rs690_texture {
    v9x_u32 format;
    int has_alpha;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 pitch_bytes;
    v9x_u32 offset;
} V9X_D3D_RS690_TEXTURE;

#define V9X_D3D_RS690_REFUSE_NONE            0ul
#define V9X_D3D_RS690_REFUSE_TARGET          1ul  /* not RGB565/XRGB8888 */
#define V9X_D3D_RS690_REFUSE_EXPLICIT        2ul  /* render-interface draw,
                                                     CPU levels or a second
                                                     unit */
#define V9X_D3D_RS690_REFUSE_DEPTH_FUNC      3ul
#define V9X_D3D_RS690_REFUSE_BLEND           4ul
#define V9X_D3D_RS690_REFUSE_ALPHA_FUNC      5ul
#define V9X_D3D_RS690_REFUSE_DEPTH_SURFACE   6ul  /* not Z16 */
#define V9X_D3D_RS690_REFUSE_SHADE           7ul  /* neither flat nor
                                                     Gouraud */
#define V9X_D3D_RS690_REFUSE_COLOR_KEY       8ul
#define V9X_D3D_RS690_REFUSE_TEXTURE_FORMAT  9ul
#define V9X_D3D_RS690_REFUSE_TEXTURE_SHAPE   10ul
#define V9X_D3D_RS690_REFUSE_TEXTURE_OP      11ul
#define V9X_D3D_RS690_REFUSE_TEXTURE_ADDRESS 12ul
#define V9X_D3D_RS690_REFUSE_TEXTURE_FILTER  13ul
#define V9X_D3D_RS690_REFUSE_SPECULAR        14ul /* no longer given: the
                                                     shader adds it; kept
                                                     so V9XTRACE's numbers
                                                     stay put */
#define V9X_D3D_RS690_REFUSE_FOG             15ul /* not built */
#define V9X_D3D_RS690_REFUSE_STATE           16ul /* the builder refused */

/*
 * One draw's builder state. fb_gpu_base is the frame buffer's MC address
 * (HDP_FB_LOCATION); vram_base is where the framebuffer mapping starts in
 * the frame buffer (the desktop's offset), and every offset in the draw is
 * moved by it; vram_bytes is the memory the draw may reach from vram_base.
 * depth_bytes is the bound depth surface's bytes per pixel, 0 when none is
 * bound. resolved may be null for an untextured draw. Returns a
 * V9X_D3D_RS690_REFUSE_* reason; on NONE, state is ready for
 * v9x_rs690_build_3d_state, which makes the remaining range checks.
 */
v9x_u32 v9x_d3d_rs690_map_draw(const V9X_R3D_DRAW *draw,
                               const V9X_D3D_RS690_TEXTURE *resolved,
                               v9x_u32 fb_gpu_base,
                               v9x_u32 vram_base,
                               v9x_u32 vram_bytes,
                               v9x_u32 depth_bytes,
                               struct v9x_rs690_3d_state *state);

#endif
