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
#define V9X_D3D_NV50_REFUSE_PROGRAMS        18ul /* the two-unit cache is full */
/* Not a refusal: one triangle dropped as past the guard band or not
 * finite (d3d_nv50.c), counted with the refusals. */
#define V9X_D3D_NV50_DROP_GUARD             19ul

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
 * Step 5b's shadow targets (docs\plans\nvidia-gt218-3d-z.md, option A):
 * a pitch-linear target cannot have a Z buffer, so a Z-tested draw goes
 * to a tiled colour shadow of its target, which d3d_nv50.c places beside
 * each Z buffer and copies to and from the linear target. Both are tiled
 * (V9X_D3D_NV50_TILE_MODE), the colour addressed through the kind-70h
 * window and the Z through its format's, and both are the Z buffer's
 * size, at least the target's.
 */
#define V9X_D3D_NV50_TILE_MODE       0x10ul  /* two GOBs, 8 rows a block */
#define V9X_D3D_NV50_TILE_ROWS       8ul
#define V9X_D3D_NV50_TILE_ROW_BYTES  64ul

typedef struct v9x_d3d_nv50_shadow_view {
    struct v9x_nv50_surface colour;
    struct v9x_nv50_surface zeta;
    v9x_u32 zeta_format;
    /* Zero when the shadow holds the target but this draw has no Z
     * buffer of its own: it draws into the shadow, untested. */
    v9x_u32 with_zeta;
} V9X_D3D_NV50_SHADOW_VIEW;

/* A tiled surface's row bytes and rows, as placed: rows of 64 bytes and
 * whole 8-row blocks, for tile mode V9X_D3D_NV50_TILE_MODE. */
void v9x_d3d_nv50_tiled_layout(v9x_u32 width, v9x_u32 height, v9x_u32 bytes,
                               v9x_u32 *pitch, v9x_u32 *rows);

/*
 * One draw: the state (the fragment program's register count from
 * fp_registers, indexed by variant), the texture's TIC and TSC words when
 * it is textured, and whether the vertices carry specular. shadow is null
 * for a draw straight into the linear target. Returns a
 * V9X_D3D_NV50_REFUSE_* reason; nothing is meaningful on a refusal.
 *
 * A two-unit draw (texcoords1 set; step 5 of nvidia-gt218-ps11.md) reads
 * texture[1] as unit 1's and writes its tables after unit 0's, so texture
 * names two and tic and tsc have room for two entries each. It sets
 * state->two_units and state->combine; the program's start and register
 * count are the caller's (its program cache).
 */
v9x_u32 v9x_d3d_nv50_map_draw(const V9X_R3D_DRAW *draw,
                              const V9X_D3D_NV50_TEXTURE *texture,
                              const V9X_D3D_NV50_SHADOW_VIEW *shadow,
                              const v9x_u32 *fp_registers,
                              struct v9x_nv50_d3d_state *state,
                              v9x_u32 *tic, v9x_u32 *tsc,
                              v9x_u32 *with_specular);

/*
 * A depth fill of [left, right) by [top, bottom) of the shadow view's Z
 * buffer at value (the 16-bit full scale), as a draw: two triangles at
 * the value, depth test ALWAYS with writes, every colour channel masked,
 * the shadow's colour as the target it does not write. Fills state and
 * six vertices; zero for a rectangle that is empty or past the buffer.
 */
int v9x_d3d_nv50_depth_fill_draw(const V9X_D3D_NV50_SHADOW_VIEW *shadow,
                                 v9x_u32 left, v9x_u32 top, v9x_u32 right,
                                 v9x_u32 bottom, v9x_u32 value,
                                 const v9x_u32 *fp_registers,
                                 struct v9x_nv50_d3d_state *state,
                                 V9X_R3D_VERTEX *vertices);

#endif
