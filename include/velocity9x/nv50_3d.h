/*
 * The GT218's Tesla 3D engine (class 8597h): the setup push and the
 * programs for a first triangle, as
 * docs\specifications\gt218-tesla-3d-first-triangle.md lists them from
 * xf86-video-nouveau's EXA (NVAccelInitNV50TCL). Pure arithmetic, no
 * I/O; the 32-bit HAL and the channel probe use it, not the 16-bit
 * driver. Phase 1 of docs\plans\nvidia-gt218-3d.md. Unmeasured on a card
 * until a record says otherwise.
 */
#ifndef VELOCITY9X_NV50_3D_H
#define VELOCITY9X_NV50_3D_H

#include "velocity9x/types.h"
#include "velocity9x/nv50_chan.h"

/* Class 8597h methods (Mesa nv50_3d.xml.h). */
#define V9X_8597_DMA_ZETA            0x0184ul
#define V9X_8597_DMA_ZETA_COUNT      11ul
#define V9X_8597_DMA_COLOR           0x01c0ul
#define V9X_8597_DMA_COLOR_COUNT     8ul
#define V9X_8597_VIEWPORT_HORIZ0     0x0c00ul
#define V9X_8597_SCISSOR_ENABLE0     0x0e00ul
#define V9X_8597_SCISSOR_HORIZ0      0x0e04ul
#define V9X_8597_VP_ADDRESS_HIGH     0x0f7cul
#define V9X_8597_COLOR_MASK_COMMON   0x0f90ul
#define V9X_8597_FP_ADDRESS_HIGH     0x0fa4ul
#define V9X_8597_SCREEN_SCISSOR_HORIZ 0x0ff4ul
#define V9X_8597_RT_CONTROL          0x121cul
#define V9X_8597_LINKED_TSC          0x1234ul
#define V9X_8597_CB_DEF_ADDRESS_HIGH 0x1280ul
#define V9X_8597_TEX_LIMITS2         0x13bcul
#define V9X_8597_VP_START_ID         0x140cul
#define V9X_8597_POINT_SPRITE_ENABLE 0x1520ul
#define V9X_8597_ZETA_ENABLE         0x1538ul
#define V9X_8597_COND_MODE           0x1558ul
#define V9X_8597_TSC_ADDRESS_HIGH    0x155cul
#define V9X_8597_TIC_ADDRESS_HIGH    0x1574ul
#define V9X_8597_VP_ATTR_EN0         0x1650ul
#define V9X_8597_SET_PROGRAM_CB      0x1694ul
#define V9X_8597_VP_RESULT_MAP_SIZE  0x16acul
#define V9X_8597_VP_REG_ALLOC_RESULT 0x16b8ul
#define V9X_8597_VP_RESULT_MAP0      0x16bcul
#define V9X_8597_VIEWPORT_TRANSFORM_EN 0x192cul
#define V9X_8597_FP_INTERPOLANT_CTRL 0x1988ul

/* The draw (nv50_exa.c NV50EXAPrepareComposite, NV50EXAComposite). */
#define V9X_8597_RT_ADDRESS_HIGH0    0x0200ul
#define V9X_8597_VTX_ATTR_2I0        0x0680ul
#define V9X_8597_VTX_ATTR_2I8        0x06a0ul
#define V9X_8597_RT_ARRAY_MODE       0x1224ul
#define V9X_8597_RT_HORIZ0           0x1240ul
#define V9X_8597_TIC_FLUSH           0x1334ul
#define V9X_8597_BLEND_ENABLE0       0x1360ul
#define V9X_8597_FP_START_ID         0x1414ul
#define V9X_8597_BIND_TIC2           0x1458ul
#define V9X_8597_VERTEX_BEGIN_GL     0x15dcul
#define V9X_8597_VERTEX_END_GL       0x15e0ul
/* RT_HORIZ: a pitch-linear target is this bit with the pitch, and its
 * height follows (Mesa nv50_validate_fb); it can have no Z buffer. */
#define V9X_8597_RT_HORIZ_LINEAR     0x80000000ul
#define V9X_8597_PRIMITIVE_TRIANGLES 4ul
/* BIND_TIC(2): EXA writes 1, then texture 0 with sampler 0, valid. */
#define V9X_8597_BIND_TIC_FIRST      0x00000001ul
#define V9X_8597_BIND_TIC_UNIT0      0x00000203ul

/* Render target formats (nv50_defs.xml.h NV50_SURFACE_FORMAT_*), the
 * same enum as the 2D engine's surfaces, where E6h and E8h were
 * measured (docs\decisions\2026-11-14-gt218-502d-engine-measurements.md). */
#define V9X_NV50_3D_RT_BGRX8         0xe6ul
#define V9X_NV50_3D_RT_B5G6R5        0xe8ul

/* COND_MODE: always draw. */
#define V9X_8597_COND_MODE_ALWAYS    1ul
/* The constant buffers EXA binds: 1 for the vertex program, 2 for the
 * fragment program, each 256 entries (nv50_accel.h CB_PVP, CB_PFP). */
#define V9X_8597_CB_VP               1ul
#define V9X_8597_CB_FP               2ul
#define V9X_8597_CB_DEF_BUFFER_SHIFT 16u
#define V9X_8597_CB_ENTRIES          256ul
/* The window bounds EXA sets for viewport and scissors. */
#define V9X_8597_BOUND               (8192ul << 16)

/*
 * Where the programs, texture tables and constants go, EXA's scratch
 * layout (nv50_accel.h PVP_OFFSET .. PFP_DATA) from a 4 KiB aligned
 * video memory address, which is also its VM address.
 */
struct v9x_nv50_3d_scratch {
    v9x_u32 vp;
    v9x_u32 fp;
    v9x_u32 tic;
    v9x_u32 tsc;
    v9x_u32 vp_constants;
    v9x_u32 fp_constants;
    /* The 1x1 texture a solid colour is drawn from. */
    v9x_u32 texel;
};

#define V9X_NV50_3D_SCRATCH_VP       0x0000ul
#define V9X_NV50_3D_SCRATCH_FP       0x1000ul
#define V9X_NV50_3D_SCRATCH_TIC      0x2000ul
#define V9X_NV50_3D_SCRATCH_TSC      0x3000ul
#define V9X_NV50_3D_SCRATCH_VP_DATA  0x4000ul
#define V9X_NV50_3D_SCRATCH_FP_DATA  0x4100ul
#define V9X_NV50_3D_SCRATCH_TEXEL    0x4800ul
#define V9X_NV50_3D_SCRATCH_BYTES    0x5000ul

void v9x_nv50_3d_scratch_at(v9x_u32 base, struct v9x_nv50_3d_scratch *out);

/* Bind the 3D object to its subchannel. */
void v9x_nv50_push_bind_3d(struct v9x_nv50_push *push);

/* EXA's setup, without DMA_NOTIFY (EXA binds a null object we do not
 * build). The 3D object must already be bound. */
void v9x_nv50_push_3d_setup(struct v9x_nv50_push *push,
                            const struct v9x_nv50_3d_scratch *scratch);

/* Room for the bind and the setup. */
#define V9X_NV50_3D_SETUP_WORDS_MAX  96ul

/*
 * What a solid colour needs in memory before a draw (NV50EXAPictSolid):
 * the texel, TIC entry 0 and TSC entry 0, written at scratch.texel,
 * .tic and .tsc; and the vertex program's constants for units 0 and 1,
 * eleven words each (a 3x3 transform, then 1/width, 1/height), written at
 * scratch.vp_constants. The colour is A8R8G8B8.
 */
#define V9X_NV50_3D_TIC_WORDS        8ul
#define V9X_NV50_3D_TSC_WORDS        8ul
#define V9X_NV50_3D_VP_CONSTANT_WORDS 22ul
struct v9x_nv50_3d_solid {
    v9x_u32 texel;
    v9x_u32 tic[V9X_NV50_3D_TIC_WORDS];
    v9x_u32 tsc[V9X_NV50_3D_TSC_WORDS];
    v9x_u32 vp_constants[V9X_NV50_3D_VP_CONSTANT_WORDS];
};

void v9x_nv50_3d_solid_tables(const struct v9x_nv50_3d_scratch *scratch,
                              v9x_u32 argb, struct v9x_nv50_3d_solid *out);

/* One solid rectangle (x, y, w, h) into a pitch-linear render target at
 * VM address target, after the setup and the solid tables: EXA's single
 * triangle twice the rectangle's size, cut by the scissor. */
void v9x_nv50_push_3d_solid_rect(struct v9x_nv50_push *push, v9x_u32 target,
                                 v9x_u32 pitch, v9x_u32 height,
                                 v9x_u32 format, v9x_u32 x, v9x_u32 y,
                                 v9x_u32 w, v9x_u32 h);
#define V9X_NV50_3D_RECT_WORDS_MAX   48ul

/*
 * Gouraud shading, the first programs of our own, built with the encoder
 * (velocity9x\nv50_shader.h). The vertex program copies position (a[0],
 * a[4]) and a four-component colour (attribute 1, a[8] to a[14]) to the
 * outputs EXA's result map already routes, o[0] to o[14]; the fragment
 * program interpolates the colour, perspective-correct, into $r0..$r3.
 * They go at entry offsets in the same code areas as EXA's: the vertex
 * program at scratch.vp + 200h, the fragment program at scratch.fp +
 * 100h. Each returns its word count, 0 if max is too small.
 */
#define V9X_NV50_3D_GOURAUD_VP_START 0x200ul
#define V9X_NV50_3D_GOURAUD_FP_START 0x100ul
#define V9X_NV50_3D_GOURAUD_VP_MAX   16ul
#define V9X_NV50_3D_GOURAUD_FP_MAX   16ul
v9x_u32 v9x_nv50_3d_gouraud_vp(v9x_u32 *words, v9x_u32 max);
v9x_u32 v9x_nv50_3d_gouraud_fp(v9x_u32 *words, v9x_u32 max);

/* VP_ATTR_EN(0) for attribute 0's x and y and attribute 1's four
 * components, a nibble each. */
#define V9X_8597_VTX_ATTR_4F_X1      0x0510ul
#define V9X_NV50_3D_GOURAUD_ATTR_EN0 0x000000f3ul

/* One Gouraud rectangle (x, y, w, h), its programs already written:
 * red ramps from 0 at the left edge to 1 at the right, green from 0 at
 * the top to 1 at the bottom, blue is 0.5 and alpha 1. EXA's one triangle
 * twice the rectangle carries the colours: at (x, y + 2h) green 2.0, at
 * (x + 2w, y) red 2.0, at (x, y) neither. */
void v9x_nv50_push_3d_gouraud_rect(struct v9x_nv50_push *push,
                                   v9x_u32 target, v9x_u32 pitch,
                                   v9x_u32 height, v9x_u32 format,
                                   v9x_u32 x, v9x_u32 y, v9x_u32 w,
                                   v9x_u32 h);

/* EXA's vertex program and its PFP_S fragment program, word for word
 * (nv50_accel.c), to be written at scratch.vp and scratch.fp. */
#define V9X_NV50_3D_EXA_VP_WORDS     60ul
#define V9X_NV50_3D_EXA_FP_S_WORDS   6ul
extern const v9x_u32 v9x_nv50_3d_exa_vp[V9X_NV50_3D_EXA_VP_WORDS];
extern const v9x_u32 v9x_nv50_3d_exa_fp_s[V9X_NV50_3D_EXA_FP_S_WORDS];

#endif
