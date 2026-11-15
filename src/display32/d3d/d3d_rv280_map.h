/*
 * The neutral draw description to the Radeon 9250's (RV280, R200) 3D
 * builder state.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_rv280_map.c holds it to its rules. What depends on
 * DirectDraw surfaces - a texture's format, size, pitch and offset - arrives
 * already resolved by d3d_rv280.c, which owns them. d3d_rs690_map.h is the
 * model.
 *
 * Accepted is what the builder (src\chipsets\ati\rv280\rv280_3d.c)
 * expresses and the V9XR23D probes drew right on M-HNBJNV at 16 and 32 bpp:
 * flat and Gouraud TL vertices with 1/W, Z16 with every function (LESS
 * measured), SRCALPHA / INVSRCALPHA blending and the other factors from the
 * same register field, the alpha test, textures (RGB565 and ARGB8888
 * measured; ARGB1555, ARGB4444 and XRGB8888 from r200_reg.h) nearest
 * or bilinear, wrapped or clamped, perspective-correct, decal or modulate,
 * vertex fog from the specular alpha, specular
 * (docs\decisions\2026-11-15-rv280-*-probe.md). Everything else is a refusal
 * with a reason: Direct3D's skip-and-count, the render interface's CPU
 * fallback. The vertices need no translation: V9X_R3D_VERTEX is D3DTLVERTEX
 * dword for dword, the builder's X, Y, Z, W, colour, specular, S, T.
 *
 * Plan: docs\plans\ati-rv280-radeon-9250.md, phase 5.
 */
#ifndef VELOCITY9X_D3D_RV280_MAP_H
#define VELOCITY9X_D3D_RV280_MAP_H

#include "velocity9x/types.h"
#include "velocity9x/rv280_3d.h"
#include "../r3d/r3d.h"

/* A texture format no RV280 path here samples. */
#define V9X_D3D_RV280_TEXTURE_UNKNOWN 0xfffffffful

/* The bound texture as the engine resolved it: bytes_per_pixel 2 or 4, or
 * V9X_D3D_RV280_TEXTURE_UNKNOWN, and its V9X_RV280_TEXFMT_* (DEFAULT for
 * RGB565 and ARGB8888); offset is from the framebuffer mapping, as every
 * surface offset in a draw is. Level 0 only. */
typedef struct v9x_d3d_rv280_texture {
    v9x_u32 bytes_per_pixel;
    v9x_u32 format;
    int has_alpha;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 pitch_bytes;
    v9x_u32 offset;
    /* The surface has a source colour key (DDRAWISURF_HASCKEYSRCBLT). */
    int has_key;
} V9X_D3D_RV280_TEXTURE;

#define V9X_D3D_RV280_REFUSE_NONE            0ul
#define V9X_D3D_RV280_REFUSE_TARGET          1ul  /* not RGB565/XRGB8888 */
#define V9X_D3D_RV280_REFUSE_EXPLICIT        2ul  /* render-interface draw,
                                                     CPU levels or a second
                                                     unit */
#define V9X_D3D_RV280_REFUSE_DEPTH_FUNC      3ul
#define V9X_D3D_RV280_REFUSE_BLEND           4ul
#define V9X_D3D_RV280_REFUSE_ALPHA_FUNC      5ul
#define V9X_D3D_RV280_REFUSE_DEPTH_SURFACE   6ul  /* not Z16 */
#define V9X_D3D_RV280_REFUSE_SHADE           7ul  /* neither flat nor
                                                     Gouraud */
#define V9X_D3D_RV280_REFUSE_COLOR_KEY       8ul  /* not built yet */
#define V9X_D3D_RV280_REFUSE_TEXTURE_FORMAT  9ul
#define V9X_D3D_RV280_REFUSE_TEXTURE_SHAPE   10ul
#define V9X_D3D_RV280_REFUSE_TEXTURE_OP      11ul /* DECALALPHA, ADD */
#define V9X_D3D_RV280_REFUSE_TEXTURE_ADDRESS 12ul
#define V9X_D3D_RV280_REFUSE_TEXTURE_FILTER  13ul
#define V9X_D3D_RV280_REFUSE_STATE           16ul /* the builder refused */

/* One draw's builder state. target.raster points at raster in the same
 * struct: copy the struct and set it again. textured says whether texture
 * and combine apply; layout is the vertex layout to send. */
typedef struct v9x_d3d_rv280_state {
    struct v9x_rv280_3d_target target;
    struct v9x_rv280_3d_raster raster;
    struct v9x_rv280_3d_texture texture;
    v9x_u32 combine;
    v9x_u32 textured;
    v9x_u32 gouraud;
    v9x_u32 layout;
} V9X_D3D_RV280_STATE;

/*
 * One draw's builder state. fb_gpu_base is the frame buffer's GPU address
 * (MC_FB_LOCATION); vram_base is where the framebuffer mapping starts in the
 * frame buffer (the desktop's offset), and every offset in the draw is moved
 * by it; vram_bytes is the memory the draw may reach from vram_base.
 * depth_bytes is the bound depth surface's bytes per pixel, 0 when none is
 * bound. resolved may be null for an untextured draw. Returns a
 * V9X_D3D_RV280_REFUSE_* reason; on NONE, state is ready for
 * v9x_rv280_build_3d_target or _textured_target, which make the remaining
 * range checks.
 */
v9x_u32 v9x_d3d_rv280_map_draw(const V9X_R3D_DRAW *draw,
                               const V9X_D3D_RV280_TEXTURE *resolved,
                               v9x_u32 fb_gpu_base,
                               v9x_u32 vram_base,
                               v9x_u32 vram_bytes,
                               v9x_u32 depth_bytes,
                               V9X_D3D_RV280_STATE *state);

#endif
