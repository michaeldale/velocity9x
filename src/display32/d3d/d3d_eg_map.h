/*
 * The neutral draw description to Evergreen (Cedar) batch state.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_eg_map.c holds it to its rules. What depends on
 * DirectDraw surfaces - a texture's format, size, pitch and offset -
 * arrives resolved by d3d_eg.c, which owns them.
 *
 * Accepted is what eg_3d.c's builder and eg_shader.c's Direct3D shaders
 * express, all of whose streams ran on M-BXMFA8's HD 5450 through V9XEG3D
 * (docs\decisions\2026-10-27-cedar-d3d-streams.md). Everything else is a
 * refusal with a reason, Direct3D's skip-and-count. Plan:
 * docs\plans\ati-evergreen-cedar-3d.md, item 5.
 *
 * The vertices need no translation: the VS fetches V9X_R3D_VERTEX as it
 * is (eg_shader.c, v9x_eg_shader_d3d_vs).
 */
#ifndef VELOCITY9X_D3D_EG_MAP_H
#define VELOCITY9X_D3D_EG_MAP_H

#include "velocity9x/types.h"
#include "velocity9x/eg_3d.h"
#include "velocity9x/eg_shader.h"
#include "../r3d/r3d.h"

/* The render interface's write-mask bits (r3d_abi.h V9X_R3D_ABI_WRITE_*). */
#define V9X_D3D_EG_WRITE_RED   1ul
#define V9X_D3D_EG_WRITE_GREEN 2ul
#define V9X_D3D_EG_WRITE_BLUE  4ul

/* A texture format no Evergreen path here samples. */
#define V9X_D3D_EG_TEXTURE_UNKNOWN 0xfffffffful

/* The bound texture as the engine resolved it: format V9X_EG_COLOR_* or
 * UNKNOWN, has_alpha for a format whose texels carry it, level 0's pitch
 * in texels, and its offset from the framebuffer mapping, as every
 * surface offset in a draw is. levels is 1, or the levels of a chain
 * d3d_eg.c placed by v9x_eg_3d_mip_layout, level 1 at mip_offset. */
typedef struct v9x_d3d_eg_texture {
    v9x_u32 format;
    v9x_u32 has_alpha;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 pitch_texels;
    v9x_u32 offset;
    v9x_u32 levels;
    v9x_u32 mip_offset;
    /* The surface's source colour key, in its own format, when it has
     * one (DDRAWISURF_HASCKEYSRCBLT). */
    v9x_u32 has_key;
    v9x_u32 key;
} V9X_D3D_EG_TEXTURE;

#define V9X_D3D_EG_REFUSE_NONE            0ul
#define V9X_D3D_EG_REFUSE_TARGET          1ul  /* format, pitch or alignment */
#define V9X_D3D_EG_REFUSE_EXPLICIT        2ul  /* a render-interface draw */
#define V9X_D3D_EG_REFUSE_DEPTH_FUNC      3ul
#define V9X_D3D_EG_REFUSE_BLEND           4ul
#define V9X_D3D_EG_REFUSE_ALPHA_FUNC      5ul
#define V9X_D3D_EG_REFUSE_DEPTH_SURFACE   6ul  /* not Z16 as placed */
#define V9X_D3D_EG_REFUSE_COLOR_KEY       8ul  /* no kill shader yet */
#define V9X_D3D_EG_REFUSE_TEXTURE_FORMAT  9ul
#define V9X_D3D_EG_REFUSE_TEXTURE_SHAPE   10ul
#define V9X_D3D_EG_REFUSE_TEXTURE_OP      11ul
#define V9X_D3D_EG_REFUSE_TEXTURE_ADDRESS 12ul
#define V9X_D3D_EG_REFUSE_TEXTURE_FILTER  13ul
#define V9X_D3D_EG_REFUSE_STATE           15ul /* the builder refused */
#define V9X_D3D_EG_REFUSE_ADDRESS         16ul /* the heap not located */
#define V9X_D3D_EG_REFUSE_OVERFLOW        17ul /* the stream outgrew its buffer */

/* A draw with specular, fog or a colour key takes the lit shaders
 * (eg_shader.h, v9x_eg_shader_d3d_ps_built): ps_slot is then
 * V9X_D3D_EG_PS_LIT, and the engine builds the PS from this and sends it
 * with the batch, as the fog colour and the key are in its code. */
#define V9X_D3D_EG_PS_LIT 0xfffffffful
typedef struct v9x_eg_d3d_ps V9X_D3D_EG_LIT;

/*
 * One draw's batch state, without the GPU addresses the engine alone
 * knows: heap_gpu is where the framebuffer mapping starts in the GPU's
 * address space, and every surface offset is moved by it. depth_bytes is
 * the bound depth surface's bytes per pixel, 0 when none is bound. The
 * shaders are named by slot (eg_shader.h, V9X_EG_SLOT_*) in vs_slot and
 * ps_slot, or ps_slot V9X_D3D_EG_PS_LIT and lit, with their GPR counts
 * and the interpolants in state; the vertex buffer and shader addresses
 * are the engine's to fill. Returns a V9X_D3D_EG_REFUSE_* reason.
 */
v9x_u32 v9x_d3d_eg_map_draw(const V9X_R3D_DRAW *draw,
                            const V9X_D3D_EG_TEXTURE *texture,
                            v9x_u32 heap_gpu,
                            v9x_u32 depth_bytes,
                            struct v9x_eg_3d_state *state,
                            v9x_u32 *vs_slot,
                            v9x_u32 *ps_slot,
                            V9X_D3D_EG_LIT *lit);

/*
 * A depth fill of part of a Z16, as a draw: the DMA engine can fill only
 * the whole of a 1D-tiled buffer (eng_eg.c), so a rectangle - Quake 3's
 * scissored depth clear behind its HUD models - is two triangles over it,
 * ZFUNC ALWAYS with writes on, scissored to it, every colour channel
 * masked. The colour target is the Z buffer's own memory named as RGB565,
 * which only sizes the draw: CB_TARGET_MASK 0 writes none of it. Haswell's
 * and Gen4's fills are the same draw (d3d_hsw.c, d3d_g4.c).
 *
 * offset, pitch, width and height are the surface's; [left, right) by
 * [top, bottom) the rectangle; value on the 16-bit full scale. object is
 * the surface's, which the map needs bound. Fills draw and its six
 * vertices; zero for a rectangle that is empty or past the surface.
 */
int v9x_d3d_eg_depth_fill_draw(v9x_u32 offset, v9x_u32 pitch,
                               v9x_u32 width, v9x_u32 height,
                               v9x_u32 left, v9x_u32 top,
                               v9x_u32 right, v9x_u32 bottom,
                               v9x_u32 value, void *object,
                               V9X_R3D_DRAW *draw, V9X_R3D_VERTEX *vertices);

/* Z16's placement: rows padded to the 8-row tile the DB reads, and the
 * pitch a whole number of 8-pixel tiles. */
#define V9X_D3D_EG_DEPTH_TILE_ROWS  8ul
#define V9X_D3D_EG_DEPTH_PITCH_UNIT 16ul /* 8 pixels of Z16 */
/* A Z16's rows are placed on 32 bytes, not 16: the core binds a depth
 * buffer only on d3d_eg.c's target_pitch_align, and a 360-pixel GL window's
 * 720-byte rows had every clear and draw refused (M-BXMFA8, boot 42). */
#define V9X_D3D_EG_DEPTH_ROW_ALIGN  32ul
/* Every surface the engine binds starts on 256 bytes. */
#define V9X_D3D_EG_SURFACE_ALIGN    256ul

#endif
