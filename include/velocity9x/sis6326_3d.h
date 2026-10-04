/*
 * SiS 6326 3D engine: register values for one triangle.
 *
 * Pure policy, no I/O, integer-only so it builds into tools with no float
 * runtime. The vertices arrive as IEEE single bit patterns (what Direct3D's
 * TLVERTEX holds); the builder sorts them by Y and names the order in the
 * primitive word, because the setup engine does not sort (datasheet 7.14.4;
 * docs\decisions\2026-10-04-sis6326-3d-state-under-sis-hal.md).
 *
 * Register meanings and citations: docs\specifications\sis6326-registers.md
 * sections 6-8.
 */
#ifndef VELOCITY9X_SIS6326_3D_H
#define VELOCITY9X_SIS6326_3D_H

#include "velocity9x/status.h"

/* Vertex registers: A at 8800h, B at 8820h, C at 8840h, 32 bytes each. */
#define V9X_SIS3D_VERTEX_A       0x8800ul
#define V9X_SIS3D_VERTEX_STRIDE  0x0020ul
#define V9X_SIS3D_VERTEX_FS      0x00ul
#define V9X_SIS3D_VERTEX_Z       0x04ul
#define V9X_SIS3D_VERTEX_X       0x08ul
#define V9X_SIS3D_VERTEX_Y       0x0cul
#define V9X_SIS3D_VERTEX_ARGB    0x10ul
#define V9X_SIS3D_VERTEX_U       0x14ul
#define V9X_SIS3D_VERTEX_V       0x18ul
#define V9X_SIS3D_VERTEX_W       0x1cul

#define V9X_SIS3D_PRIMITIVE      0x89f8ul
#define V9X_SIS3D_STATUS         0x89fcul
#define V9X_SIS3D_ENABLE         0x8a00ul
#define V9X_SIS3D_Z_SET          0x8a04ul
#define V9X_SIS3D_ALPHA_SET      0x8a0cul
#define V9X_SIS3D_DST_SET        0x8a14ul
#define V9X_SIS3D_DST_BASE       0x8a18ul
#define V9X_SIS3D_FOG            0x8a20ul
#define V9X_SIS3D_BLEND          0x8a28ul
#define V9X_SIS3D_CLIP_TB        0x8a30ul
#define V9X_SIS3D_CLIP_LR        0x8a34ul

/* 89FCh read: D1 engine idle and 3D queue empty, D0 engine idle. */
#define V9X_SIS3D_STATUS_IDLE_EMPTY 0x00000002ul
#define V9X_SIS3D_STATUS_IDLE       0x00000001ul

/* 8A00h enable bits (datasheet 7.14.6). */
#define V9X_SIS3D_ENABLE_PRIM_SETUP 0x00000800ul

/* 89F8h fields (datasheet 7.14.4). */
#define V9X_SIS3D_DRAW_TRIANGLE     0x00000002ul
#define V9X_SIS3D_DIRECTION_BIT     0x00000080ul
#define V9X_SIS3D_FIRE_SHIFT        8
#define V9X_SIS3D_FIRE_ON_TSWC      6ul
#define V9X_SIS3D_BOTTOM_SHIFT      12
#define V9X_SIS3D_MIDDLE_SHIFT      14
#define V9X_SIS3D_TOP_SHIFT         16
#define V9X_SIS3D_SHADE_SHIFT       18
#define V9X_SIS3D_SHADE_FLAT_TOP    1ul
#define V9X_SIS3D_SHADE_FLAT_MIDDLE 2ul
#define V9X_SIS3D_SHADE_FLAT_BOTTOM 3ul
#define V9X_SIS3D_SHADE_GOURAUD     4ul

/* Destination formats, 8A14h D[22:16]: 16 bpp class, RGB565 (7.14.9). */
#define V9X_SIS3D_DST_RGB565        0x11ul
/* 8A14h D[27:24]: ROP2 COPY_PEN. */
#define V9X_SIS3D_ROP_COPY          0x0cul
#define V9X_SIS3D_DST_PITCH_MAX     0x00003ffful
/* 22-bit local addresses in the 2D engine; the 3D base registers take 23
 * (7.14.7-9), but no Rev. Ax/Bx board carries more than 4 MiB. */
#define V9X_SIS3D_ADDRESS_LIMIT     0x00400000ul
/* 13-bit sign-magnitude clip fields with 12 integer bits (7.14.12). */
#define V9X_SIS3D_CLIP_MAX          0x00000ffful

/* Dwords a triangle's state writes, and its vertex writes. */
#define V9X_SIS3D_STATE_DWORDS      9u
#define V9X_SIS3D_VERTEX_DWORDS     24u

struct v9x_sis3d_vertex {
    v9x_u32 x;      /* IEEE single bits, pixels */
    v9x_u32 y;
    v9x_u32 z;
    v9x_u32 argb;
    v9x_u32 u;
    v9x_u32 v;
    v9x_u32 w;
    v9x_u32 fog_specular;
};

struct v9x_sis3d_target {
    v9x_u32 vram_bytes;
    v9x_u32 offset;
    v9x_u32 pitch_bytes;
    v9x_u32 width;
    v9x_u32 height;
};

struct v9x_sis3d_writes {
    v9x_u32 offsets[V9X_SIS3D_VERTEX_DWORDS];
    v9x_u32 values[V9X_SIS3D_VERTEX_DWORDS];
    v9x_u32 count;
};

/* q / 16 as IEEE single bits, exact for |q| below 2^24. */
v9x_u32 v9x_sis3d_float_q4(v9x_s32 q);

/* Indices of the top, middle and bottom vertex by Y; ties keep input
 * order. */
void v9x_sis3d_order(const struct v9x_sis3d_vertex *vertices,
                     v9x_u32 *top, v9x_u32 *middle, v9x_u32 *bottom);

/* The 89F8h word for a triangle, firing on the write of vertex C's W. */
v9x_u32 v9x_sis3d_primitive(const struct v9x_sis3d_vertex *vertices,
                            v9x_u32 shade, int direction);

/* Flat untextured state into an RGB565 target: no Z, no alpha test, no
 * blend beyond ONE/ZERO, no fog, clip to the target. */
v9x_status v9x_sis3d_build_flat_state(const struct v9x_sis3d_target *target,
                                      struct v9x_sis3d_writes *writes);

/* The 24 vertex dwords, A then B then C, vertex C's W last (the fire). */
void v9x_sis3d_build_vertices(const struct v9x_sis3d_vertex *vertices,
                              struct v9x_sis3d_writes *writes);

#endif
