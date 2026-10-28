/*
 * The neutral draw description to Carrizo (GFX8) batch state.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_vi_map.c holds it to its rules. What depends on
 * DirectDraw surfaces - a texture's format, size, pitch and offset -
 * arrives resolved by d3d_vi.c, which owns them.
 *
 * Accepted is what vi_3d.c's TL-vertex draw and vi_shader.c's shaders
 * express, all of which ran on M-XP7053's R7E through V9XCZ3D
 * (docs\decisions\2026-10-28-carrizo-gouraud.md, -depth.md and
 * -texture-modulate.md): 16 and 32 bpp targets, Z16 with any function,
 * RGB565 and ARGB8888 textures, point or bilinear, clamp, wrap or mirror,
 * replace, modulate and decal-alpha, and specular added after them, which DirectX 5
 * and 6 enable by default. Everything else is a refusal with a reason,
 * Direct3D's skip-and-count. Plan: docs\plans\ati-carrizo-gfx8-3d.md,
 * Phase 5 step 1.
 *
 * The vertices need no translation: the VS fetches V9X_R3D_VERTEX as it
 * is (vi_shader.c, v9x_vi_shader_tl_vs).
 */
#ifndef VELOCITY9X_D3D_VI_MAP_H
#define VELOCITY9X_D3D_VI_MAP_H

#include "velocity9x/types.h"
#include "velocity9x/vi_3d.h"
#include "velocity9x/vi_shader.h"
#include "../r3d/r3d.h"

/* A texture format no Carrizo path here samples. */
#define V9X_D3D_VI_TEXTURE_UNKNOWN 0xfffffffful

/* The bound texture as the engine resolved it: format V9X_VI_IMG_FORMAT_*
 * or UNKNOWN, has_alpha for a format whose texels carry it, level 0's
 * pitch in texels, its offset from the framebuffer mapping as every
 * surface offset in a draw is, and whether it has a source colour key.
 * levels: 1, or a chain's count laid out by v9x_vi_3d_mip_layout from
 * offset, sampled across under a mip filter. */
typedef struct v9x_d3d_vi_texture {
    v9x_u32 levels;
    v9x_u32 format;
    v9x_u32 has_alpha;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 pitch_texels;
    v9x_u32 offset;
    v9x_u32 has_key;
    v9x_u32 key;                /* the source key, in the texture's format */
} V9X_D3D_VI_TEXTURE;

#define V9X_D3D_VI_REFUSE_NONE            0ul
#define V9X_D3D_VI_REFUSE_TARGET          1ul  /* format, pitch or alignment */
#define V9X_D3D_VI_REFUSE_EXPLICIT        2ul  /* a render-interface draw */
#define V9X_D3D_VI_REFUSE_DEPTH_FUNC      3ul
#define V9X_D3D_VI_REFUSE_BLEND           4ul  /* not built yet */
#define V9X_D3D_VI_REFUSE_ALPHA_TEST      5ul  /* not built yet */
#define V9X_D3D_VI_REFUSE_DEPTH_SURFACE   6ul  /* not Z16 as placed */
#define V9X_D3D_VI_REFUSE_FOG             7ul  /* not built yet */
#define V9X_D3D_VI_REFUSE_COLOR_KEY       8ul  /* not built yet */
#define V9X_D3D_VI_REFUSE_TEXTURE_FORMAT  9ul
#define V9X_D3D_VI_REFUSE_TEXTURE_SHAPE   10ul
#define V9X_D3D_VI_REFUSE_TEXTURE_OP      11ul
#define V9X_D3D_VI_REFUSE_TEXTURE_ADDRESS 12ul
#define V9X_D3D_VI_REFUSE_TEXTURE_FILTER  13ul
#define V9X_D3D_VI_REFUSE_STATE           15ul /* the builder refused */
#define V9X_D3D_VI_REFUSE_ADDRESS         16ul /* the heap not located */
#define V9X_D3D_VI_REFUSE_OVERFLOW        17ul /* the stream outgrew its buffer */

/* The pixel shader a draw takes (vi_shader.h, v9x_vi_shader_d3d_ps): each
 * op, then each op with Direct3D's specular added. */
#define V9X_D3D_VI_PS_GOURAUD           0ul
#define V9X_D3D_VI_PS_TEXTURE           1ul
#define V9X_D3D_VI_PS_MODULATE          2ul
#define V9X_D3D_VI_PS_GOURAUD_SPECULAR  3ul
#define V9X_D3D_VI_PS_TEXTURE_SPECULAR  4ul
#define V9X_D3D_VI_PS_MODULATE_SPECULAR 5ul
#define V9X_D3D_VI_PS_COUNT             6ul
#define V9X_D3D_VI_PS_SPECULAR          3ul /* added to an op's number */
/* The alpha test, fog or a colour key: a PS built for the draw from
 * *built (vi_shader.h, v9x_vi_shader_d3d_ps_built), its constants in its
 * code, which the engine sends with the batch. */
#define V9X_D3D_VI_PS_BUILT             6ul
/* DECALALPHA, which has no slot: only ever on the way to _BUILT. */
#define V9X_D3D_VI_PS_DECALALPHA        7ul

/*
 * One draw's batch state, without what the engine alone knows: the shader,
 * vertex and fence addresses and the vertex count, which it fills. Every
 * surface offset is moved by the heap's GPU address, heap_high:heap_low;
 * a surface the low half would carry out of is refused. depth_bytes is the
 * bound depth surface's bytes per pixel, 0 when none is bound. Fills out
 * and *ps (V9X_D3D_VI_PS_*), and *built when *ps is V9X_D3D_VI_PS_BUILT;
 * returns a V9X_D3D_VI_REFUSE_* reason.
 */
v9x_u32 v9x_d3d_vi_map_draw(const V9X_R3D_DRAW *draw,
                            const V9X_D3D_VI_TEXTURE *texture,
                            v9x_u32 heap_high,
                            v9x_u32 heap_low,
                            v9x_u32 depth_bytes,
                            struct v9x_vi_tl_draw *out,
                            v9x_u32 *ps,
                            struct v9x_vi_d3d_ps *built);

/* Placement d3d_vi.c gives what it binds: every surface on 256 bytes;
 * colour targets and textures in rows of whole 64 bytes (linear aligned,
 * tile index 8); a Z16's pitch whole 8-pixel tiles and its rows padded to
 * the 8-row tile the DB reads (tile index 5, 1D). */
#define V9X_D3D_VI_SURFACE_ALIGN    256ul
#define V9X_D3D_VI_ROW_ALIGN        64ul
#define V9X_D3D_VI_DEPTH_TILE_ROWS  8ul
#define V9X_D3D_VI_DEPTH_PITCH_UNIT 16ul /* 8 pixels of Z16 */
#define V9X_D3D_VI_TEXTURE_SIDE_MAX 2048ul

#endif
