/*
 * The neutral draw description to the GT218's Tesla 3D state.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_nv50_map.c holds it to its rules. What depends on
 * DirectDraw surfaces - a texture's format, size, pitch and offset -
 * arrives resolved by d3d_nv50.c, which owns them. Step 5a of
 * docs\plans\nvidia-gt218-3d-z.md: linear targets, no depth buffer yet.
 *
 * Accepted is what nv50_d3d.c's programs and state express. Everything
 * else is a refusal with a reason, Direct3D's skip-and-count.
 *
 * Addresses: the channel's VM is the identity over video memory and BAR1
 * maps it from 0, so a surface's offset from the framebuffer mapping is
 * its VM address (eng_nv50.c).
 */
#ifndef VELOCITY9X_D3D_NV50_MAP_H
#define VELOCITY9X_D3D_NV50_MAP_H

#include "velocity9x/types.h"
#include "velocity9x/nv50_d3d.h"
#include "../r3d/r3d.h"

/* The render interface's write-mask bits (r3d_abi.h V9X_R3D_ABI_WRITE_*). */
#define V9X_D3D_NV50_WRITE_RED   1ul
#define V9X_D3D_NV50_WRITE_GREEN 2ul
#define V9X_D3D_NV50_WRITE_BLUE  4ul

/* A texture format nothing here samples. */
#define V9X_D3D_NV50_TEXTURE_UNKNOWN 0ul

/* The bound texture as d3d_nv50.c resolved it: format V9X_NV50_D3D_TEX_*
 * or UNKNOWN, level 0's size, its row pitch in bytes and its offset, and
 * the source colour key when the surface has one. */
typedef struct v9x_d3d_nv50_texture {
    v9x_u32 format;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 pitch;
    v9x_u32 offset;
    v9x_u32 has_key;
    v9x_u32 key;
} V9X_D3D_NV50_TEXTURE;

#define V9X_D3D_NV50_REFUSE_NONE            0ul
#define V9X_D3D_NV50_REFUSE_TARGET          1ul  /* format, pitch or base */
#define V9X_D3D_NV50_REFUSE_EXPLICIT        2ul  /* a render-interface draw */
#define V9X_D3D_NV50_REFUSE_DEPTH_FUNC      3ul
#define V9X_D3D_NV50_REFUSE_BLEND           4ul
#define V9X_D3D_NV50_REFUSE_ALPHA_FUNC      5ul
#define V9X_D3D_NV50_REFUSE_DEPTH_SURFACE   6ul  /* no shadow target yet */
#define V9X_D3D_NV50_REFUSE_COLOR_KEY       8ul  /* no kill in the programs */
#define V9X_D3D_NV50_REFUSE_TEXTURE_FORMAT  9ul
#define V9X_D3D_NV50_REFUSE_TEXTURE_SHAPE   10ul
#define V9X_D3D_NV50_REFUSE_TEXTURE_OP      11ul
#define V9X_D3D_NV50_REFUSE_TEXTURE_ADDRESS 12ul
#define V9X_D3D_NV50_REFUSE_TEXTURE_FILTER  13ul
#define V9X_D3D_NV50_REFUSE_STATE           15ul /* the engine refused */
#define V9X_D3D_NV50_REFUSE_OVERFLOW        17ul /* the push outgrew its area */

/*
 * Placement rules every surface the engine binds keeps: rows on 64 bytes
 * and bases on 256, which is what every surface measured so far had. The
 * hardware's own minimum is not measured; a surface off these is refused
 * rather than drawn on a guess.
 */
#define V9X_D3D_NV50_PITCH_ALIGN     64ul
#define V9X_D3D_NV50_SURFACE_ALIGN   256ul
#define V9X_D3D_NV50_TEXTURE_SIDE_MAX 2048ul

/*
 * One draw: the state (the fragment program's register count from
 * fp_registers, indexed by variant), the texture's TIC and TSC words when
 * it is textured, and whether the vertices carry specular. Returns a
 * V9X_D3D_NV50_REFUSE_* reason; nothing is meaningful on a refusal.
 */
v9x_u32 v9x_d3d_nv50_map_draw(const V9X_R3D_DRAW *draw,
                              const V9X_D3D_NV50_TEXTURE *texture,
                              const v9x_u32 *fp_registers,
                              struct v9x_nv50_d3d_state *state,
                              v9x_u32 *tic, v9x_u32 *tsc,
                              v9x_u32 *with_specular);

#endif
