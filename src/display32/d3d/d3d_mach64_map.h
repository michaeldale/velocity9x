/*
 * The neutral draw description to the Mobility-M policy request and draw
 * state.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_mach64_map.c holds it to its rules. What depends on
 * DirectDraw surfaces - a texture's format, size and VRAM offset - arrives
 * already resolved by d3d_mach64.c, which owns them.
 */
#ifndef VELOCITY9X_D3D_MACH64_MAP_H
#define VELOCITY9X_D3D_MACH64_MAP_H

#include "velocity9x/types.h"
#include "velocity9x/ati_mach64_engine.h"
#include "../r3d/r3d.h"

/* A texture format no Mobility-M sampler reads: the policy refuses it. */
#define V9X_D3D_MACH64_TEXTURE_UNKNOWN 0xfffffffful

/* The bound texture as the engine resolved it. format is
 * V9X_M64_TEXTURE_FORMAT_* or V9X_D3D_MACH64_TEXTURE_UNKNOWN. levels is the
 * usable chain length, and level_offsets[n] level n's VRAM offset for the
 * first `levels` of them (level_offsets[0] is offset). */
typedef struct v9x_d3d_mach64_texture {
    v9x_u32 format;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 levels;
    v9x_u32 offset;
    v9x_u32 pitch_bytes;
    v9x_u32 level_offsets[V9X_M64_TEXTURE_LEVELS_MAX];
} V9X_D3D_MACH64_TEXTURE;

/*
 * specular_rgb is non-zero when some vertex of the batch carries specular
 * colour. Direct3D's SPECULARENABLE defaults on, and a TL vertex without
 * specular colour adds nothing, so only colour that would actually be added
 * makes the unmeasured specular path a refusal.
 */
void v9x_d3d_mach64_map_request(const V9X_R3D_DRAW *draw,
                                const V9X_D3D_MACH64_TEXTURE *texture,
                                v9x_u32 specular_rgb,
                                struct v9x_m64_draw_request *request);

/* The engine state for a request the policy accepted. */
void v9x_d3d_mach64_map_state(const V9X_R3D_DRAW *draw,
                              const struct v9x_m64_draw_request *request,
                              const V9X_D3D_MACH64_TEXTURE *texture,
                              v9x_u32 vram_bytes,
                              struct v9x_m64_draw_state *state);

/* Non-zero when any of the vertices has specular red, green or blue. */
v9x_u32 v9x_d3d_mach64_specular_rgb(const V9X_R3D_VERTEX *vertices,
                                    v9x_u32 vertex_count);

#endif
