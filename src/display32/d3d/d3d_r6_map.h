/*
 * The neutral draw description to R6xx (HD 2600 XT, RV630) batch state.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_r6_map.c holds it to its rules. d3d_eg_map.c's
 * mapping on r6_3d.c's builder (struct v9x_r6_3d_state, in d3d_order) and
 * r6_shader.c's Direct3D shaders; the refusal reasons are Evergreen's
 * (d3d_eg_map.h, V9X_D3D_EG_REFUSE_*), so V9XTRACE's policy counters read
 * the same on both.
 *
 * What Evergreen draws that this does not, yet: specular and fog (the
 * draw goes ahead without them: the lit shaders are not ported), a source
 * colour key (refused).
 */
#ifndef VELOCITY9X_D3D_R6_MAP_H
#define VELOCITY9X_D3D_R6_MAP_H

#include "velocity9x/types.h"
#include "velocity9x/r6_3d.h"
#include "velocity9x/r6_shader.h"
#include "d3d_eg_map.h"

/* A texture format no R6xx path here samples. */
#define V9X_D3D_R6_TEXTURE_UNKNOWN 0xfffffffful

/* The bound texture as the engine resolved it: format V9X_R6_COLOR_* or
 * UNKNOWN, level 0's size and pitch in texels, and its offset from the
 * framebuffer mapping, as every surface offset in a draw is. */
typedef struct v9x_d3d_r6_texture {
    v9x_u32 format;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 pitch_texels;
    v9x_u32 offset;
    /* The surface's source colour key is set (DDRAWISURF_HASCKEYSRCBLT). */
    v9x_u32 has_key;
    /* An XRGB8888 texture: its fourth byte is not alpha. */
    v9x_u32 opaque;
    /* 1, or the levels of a chain d3d_r6.c placed by Evergreen's layout
     * (v9x_eg_3d_mip_layout), level 1 at mip_offset. */
    v9x_u32 levels;
    v9x_u32 mip_offset;
} V9X_D3D_R6_TEXTURE;

/*
 * One draw's batch state, without the addresses only the engine knows:
 * heap_gpu is where the framebuffer mapping starts in the GPU's address
 * space. depth_bytes is the bound depth surface's bytes per pixel, 0 when
 * none is bound. The shaders are named by slot (r6_shader.h,
 * V9X_R6_SLOT_*), their GPR counts and the interpolants in state; the
 * vertex buffer and shader addresses are the engine's to fill. Returns a
 * V9X_D3D_EG_REFUSE_* reason.
 */
v9x_u32 v9x_d3d_r6_map_draw(const V9X_R3D_DRAW *draw,
                            const V9X_D3D_R6_TEXTURE *texture,
                            v9x_u32 heap_gpu,
                            v9x_u32 depth_bytes,
                            struct v9x_r6_3d_state *state,
                            v9x_u32 *vs_slot,
                            v9x_u32 *ps_slot);

#endif
