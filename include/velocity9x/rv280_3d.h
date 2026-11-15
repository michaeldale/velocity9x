/*
 * ATI Radeon 9250 (RV280, an R200-class part) 3D engine register values,
 * written directly over the register BAR with the CP off: phase 5 of
 * docs\plans\ati-rv280-radeon-9250.md. Pure builders, host-tested
 * (tests\host\test_rv280_3d.c); src\chipsets\ati\rv280\rv280_3d.c.
 *
 * Sources: xf86-video-ati 6.14.6 (build\external\rs690\xf86-video-ati-
 * 6.14.6\src\), its ACCEL_MMIO build:
 *   - RADEONInit3DEngineMMIO's R200 branch, radeon_commonfuncs.c:777-803;
 *   - R200PrepareComposite and RadeonCompositeTile, radeon_exa_render.c:
 *     995-1132 and 2455-2509: colour buffer, texture environment, SE_VF_CNTL
 *     without RADEON_MODE, vertex dwords to SE_PORT_DATA0;
 *   - RadeonFinishComposite's WAIT_UNTIL 3D idle-clean, :2213-2216.
 * Offsets and fields: radeon_reg.h there. The order of a vertex's dwords
 * follows the register's bit order, as Linux r200.c r200_get_vtx_size_0
 * counts them (build\external\rv280\): X, Y, then colour 0.
 *
 * Not in those files, and so a hypothesis the draw probe measures:
 *   - diffuse-only output, TXCBLEND_0 = ARG_C diffuse with A = B = 0 and the
 *     stage's output in R0, after the single-argument pass-through at
 *     radeon_render.c:925-930, with no texture unit enabled;
 *   - the byte order of a packed colour (PK_RGBA), with and without
 *     SE_VF_CNTL's COLOR_ORDER_RGBA.
 */
#ifndef VELOCITY9X_RV280_3D_H
#define VELOCITY9X_RV280_3D_H

#include "velocity9x/types.h"
#include "velocity9x/status.h"

/* Registers (radeon_reg.h). */
#define V9X_RV280_RB3D_BLENDCNTL        0x1c20ul
#define V9X_RV280_PP_CNTL               0x1c38ul
#define V9X_RV280_RB3D_CNTL             0x1c3cul
#define V9X_RV280_RB3D_COLOROFFSET      0x1c40ul
#define V9X_RV280_RE_WIDTH_HEIGHT       0x1c44ul
#define V9X_RV280_RB3D_COLORPITCH       0x1c48ul
#define V9X_RV280_SE_CNTL               0x1c4cul
#define V9X_RV280_RE_CNTL               0x1c50ul
#define V9X_RV280_RB3D_PLANEMASK        0x1d84ul
#define V9X_RV280_WAIT_UNTIL            0x1720ul
#define V9X_RV280_SE_PORT_DATA0         0x2000ul
#define V9X_RV280_SE_VAP_CNTL           0x2080ul
#define V9X_RV280_SE_VF_CNTL            0x2084ul
#define V9X_RV280_SE_VTX_FMT_0          0x2088ul
#define V9X_RV280_SE_VTX_FMT_1          0x208cul
#define V9X_RV280_SE_VTE_CNTL           0x20b0ul
#define V9X_RV280_SE_VAP_CNTL_STATUS    0x2140ul
#define V9X_RV280_SE_VTX_STATE_CNTL     0x2180ul
#define V9X_RV280_RE_TOP_LEFT           0x26c0ul
#define V9X_RV280_RE_AUX_SCISSOR_CNTL   0x26f0ul
#define V9X_RV280_PP_TXMULTI_CTL_0      0x2c1cul
#define V9X_RV280_PP_CNTL_X             0x2cc4ul
#define V9X_RV280_PP_TXCBLEND_0         0x2f00ul
#define V9X_RV280_PP_TXCBLEND2_0        0x2f04ul
#define V9X_RV280_PP_TXABLEND_0         0x2f08ul
#define V9X_RV280_PP_TXABLEND2_0        0x2f0cul

/* SE_VAP_CNTL: FORCE_W_TO_ONE 1<<16, VF_MAX_VTX_NUM 9<<18; TCL_ENABLE
 * (bit 0) clear, so the vertices bypass TCL. */
#define V9X_RV280_VAP_CNTL_BYPASS       0x00250000ul
/* SE_CNTL: SOLID front and back faces, OGL pixel centre, ROUND_MODE_ROUND,
 * ROUND_PREC_4TH_PIX; the diffuse shade model in bits 9:8. The init's
 * value is the Gouraud one. */
#define V9X_RV280_SE_CNTL_BASE          0x9800001eul
#define V9X_RV280_SE_DIFFUSE_FLAT       0x00000100ul
#define V9X_RV280_SE_DIFFUSE_GOURAUD    0x00000200ul
/* RB3D_CNTL COLOR_FORMAT, bits 13:10. */
#define V9X_RV280_COLOR_FORMAT_RGB565   0x00001000ul
#define V9X_RV280_COLOR_FORMAT_ARGB8888 0x00001800ul
/* RB3D_BLENDCNTL: source GL_ONE, destination GL_ZERO (no blending). */
#define V9X_RV280_BLEND_ONE_ZERO        0x20210000ul
/* PP_CNTL: TEX_BLEND_0_ENABLE, no texture unit. */
#define V9X_RV280_PP_BLEND_0_ONLY       0x00001000ul
/* TXCBLEND_0 / TXABLEND_0: ARG_C = diffuse colour / alpha (selector 4 in
 * bits 14:10), A = B = 0, OP MADD: out = 0 * 0 + diffuse. */
#define V9X_RV280_TXCBLEND_DIFFUSE      0x00001000ul
#define V9X_RV280_TXABLEND_DIFFUSE      0x00001000ul
/* TXCBLEND2_0 / TXABLEND2_0: CLAMP_0_1, OUTPUT_REG_R0. */
#define V9X_RV280_TXBLEND2_TO_R0        0x00011000ul
/* SE_VTX_FMT_0: XY always; COLOR_0 packed RGBA (1 << 11). */
#define V9X_RV280_VTX_FMT_PK_COLOR0     0x00000800ul
/* SE_VF_CNTL: TRIANGLE_LIST 4, WALK_DATA 3<<4, COLOR_ORDER_RGBA 1<<6,
 * vertex count from bit 16. No RADEON_MODE on R200. */
#define V9X_RV280_VF_TRIANGLE_LIST      0x00000004ul
#define V9X_RV280_VF_WALK_DATA          0x00000030ul
#define V9X_RV280_VF_COLOR_ORDER_RGBA   0x00000040ul
#define V9X_RV280_VF_NUM_SHIFT          16u
/* WAIT_UNTIL: WAIT_3D_IDLECLEAN. */
#define V9X_RV280_WAIT_3D_IDLECLEAN     0x00020000ul

/* RB3D_COLOROFFSET's alignment, RB3D_COLORPITCH's unit and field. */
#define V9X_RV280_COLOR_OFFSET_ALIGN    16ul
#define V9X_RV280_COLOR_PITCH_UNIT      8ul
#define V9X_RV280_COLOR_PITCH_MAX       0x1ff8ul
/* RE_WIDTH_HEIGHT holds width and height in 16 bits each; X uses up to
 * 2047. */
#define V9X_RV280_TARGET_MAX            2047ul

/* Vertices per SE_VF_CNTL in one triangle list here, and dwords per vertex
 * (X, Y, packed colour). */
#define V9X_RV280_3D_MAX_VERTICES       12u
#define V9X_RV280_3D_VERTEX_DWORDS      3u

#define V9X_RV280_3D_LIST_MAX           64u

struct v9x_rv280_3d_list {
    v9x_u32 offsets[V9X_RV280_3D_LIST_MAX];
    v9x_u32 values[V9X_RV280_3D_LIST_MAX];
    v9x_u32 count;
};

/* The colour buffer: a surface in the frame buffer at a GPU address. */
struct v9x_rv280_3d_target {
    v9x_u32 fb_gpu_base;
    v9x_u32 fb_bytes;
    v9x_u32 offset;
    v9x_u32 pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 width;
    v9x_u32 height;
};

/* A screen-space vertex: x and y as IEEE single bits, and a packed colour
 * sent as it is. */
struct v9x_rv280_3d_vertex {
    v9x_u32 x_bits;
    v9x_u32 y_bits;
    v9x_u32 colour;
};

/* RB3D_CNTL = 0 (RADEONEngineInit), then RADEONInit3DEngine's R200 branch:
 * fourteen-or-fewer writes, once per mode on an idle engine. */
v9x_status v9x_rv280_build_3d_init(struct v9x_rv280_3d_list *list);

/* The colour buffer, the diffuse-only texture environment and the shade
 * model: V9X_STATUS_UNSUPPORTED for a depth the colour buffer has no format
 * for here (only 16 and 32 bpp), INVALID_ARGUMENT for a surface outside the
 * frame buffer, misaligned, or too large. */
v9x_status v9x_rv280_build_3d_target(const struct v9x_rv280_3d_target *target,
                                     int gouraud,
                                     struct v9x_rv280_3d_list *list);

/* A triangle list: SE_VF_CNTL then each vertex's dwords to SE_PORT_DATA0.
 * count a multiple of 3, 3..V9X_RV280_3D_MAX_VERTICES. */
v9x_status v9x_rv280_build_3d_triangles(
    const struct v9x_rv280_3d_vertex *vertices, v9x_u32 count,
    int colour_order_rgba, struct v9x_rv280_3d_list *list);

/* WAIT_UNTIL 3D idle-clean, as RadeonFinishComposite ends a draw. */
v9x_status v9x_rv280_build_3d_finish(struct v9x_rv280_3d_list *list);

#endif
