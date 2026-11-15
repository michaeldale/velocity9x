/*
 * Direct3D draws on the GT218's Tesla 3D engine: the programs, the
 * per-draw state and the vertices, built for the HAL (d3d_nv50.c) and the
 * channel probe. Pure arithmetic, no I/O. Step 5a of
 * docs\plans\nvidia-gt218-3d-z.md.
 *
 * Every draw takes the same vertex program, which copies its four
 * attributes to its results: position (attribute 0, x y z w), diffuse
 * (1), specular (2) and the texture coordinates (8, u v). The fragment
 * program is one of V9X_NV50_D3D_FP_VARIANTS, chosen by the texture
 * blend, specular and fog, all interpolated perspective-correct from the
 * position's w.
 *
 * The position is sent as (sx, sy, sz, rhw). EXA's setup leaves the
 * viewport transform off, and with it off the engine takes x, y and z as
 * they are and does not divide by w: the probe's /3d:d3d stage sent
 * (sx w, sy w, sz w, w) at w 2 and the rectangle landed at twice its
 * coordinates (M-BXMFA8). The fourth component is the weight of the
 * perspective-correct interpolation the fragment programs ask for, and
 * the engine weights by it as given: sent 1 / rhw, a texture's
 * coordinates followed weights of w (rectangle D of the same stage).
 */
#ifndef VELOCITY9X_NV50_D3D_H
#define VELOCITY9X_NV50_D3D_H

#include "velocity9x/nv50_3d.h"

/* Where the programs go in the 3D scratch: after EXA's and the earlier
 * phases' (VP 0, 200h, 300h; FP 0, 100h). */
#define V9X_NV50_D3D_VP_START        0x0400ul
#define V9X_NV50_D3D_FP_FIRST        0x0200ul
#define V9X_NV50_D3D_FP_STRIDE       0x0100ul
#define V9X_NV50_D3D_VP_WORDS_MAX    32ul
#define V9X_NV50_D3D_FP_WORDS_MAX    64ul

/* The fragment program variants: the texture blend times specular times
 * fog. */
#define V9X_NV50_D3D_OP_NONE         0ul
#define V9X_NV50_D3D_OP_REPLACE      1ul
#define V9X_NV50_D3D_OP_MODULATE     2ul
#define V9X_NV50_D3D_OPS             3ul
#define V9X_NV50_D3D_FP_VARIANTS     12ul
v9x_u32 v9x_nv50_d3d_fp_variant(v9x_u32 op, v9x_u32 specular, v9x_u32 fog);
v9x_u32 v9x_nv50_d3d_fp_start(v9x_u32 variant);

v9x_u32 v9x_nv50_d3d_vp(v9x_u32 *words, v9x_u32 max);
/* The variant's program; *registers is the count of $r it uses
 * (FP_REG_ALLOC_TEMP). 0 when it does not fit. */
v9x_u32 v9x_nv50_d3d_fp(v9x_u32 variant, v9x_u32 *words, v9x_u32 max,
                        v9x_u32 *registers);

/*
 * Two texture units (step 5 of docs\plans\nvidia-gt218-ps11.md): unit 0
 * combines its texel with the vertex colour, unit 1 its texel with unit
 * 0's result, each by a colour op (the render interface's REPLACE,
 * MODULATE, DECALALPHA and ADD, the last saturated as Direct3D's stages
 * are) and an alpha op (FRAGMENT, REPLACE, MODULATE); then specular and
 * fog as the one-unit variants. Unit 1's coordinates are attribute 9,
 * u v, which the vertex program passes as results 14 and 15.
 *
 * There are too many combinations to place all, so the HAL builds them
 * on demand into slots past the scratch's own area (gt218_reserve.h),
 * keyed by v9x_nv50_d3d_combine_key; FP_START_ID is
 * v9x_nv50_d3d_fp2_start(slot), an offset from the same code base.
 */
#define V9X_NV50_D3D_COLOR_REPLACE   0ul  /* Ct                 */
#define V9X_NV50_D3D_COLOR_MODULATE  1ul  /* Cin Ct             */
#define V9X_NV50_D3D_COLOR_DECAL     2ul  /* Cin + At (Ct - Cin) */
#define V9X_NV50_D3D_COLOR_ADD       3ul  /* sat(Cin + Ct)      */
#define V9X_NV50_D3D_COLOR_OPS       4ul
#define V9X_NV50_D3D_ALPHA_FRAGMENT  0ul  /* Ain                */
#define V9X_NV50_D3D_ALPHA_REPLACE   1ul  /* At                 */
#define V9X_NV50_D3D_ALPHA_MODULATE  2ul  /* Ain At             */
#define V9X_NV50_D3D_ALPHA_OPS       3ul
struct v9x_nv50_d3d_combine {
    v9x_u32 color[2];
    v9x_u32 alpha[2];
    v9x_u32 specular;
    v9x_u32 fog;
};
#define V9X_NV50_D3D_COMBINE_NONE    0xfffffffful
#define V9X_NV50_D3D_FP2_FIRST       0x7000ul
#define V9X_NV50_D3D_FP2_STRIDE      0x0200ul
#define V9X_NV50_D3D_FP2_SLOTS       64ul
#define V9X_NV50_D3D_FP2_WORDS_MAX   128ul
/* A number for the combination, or COMBINE_NONE for an op this does not
 * draw. */
v9x_u32 v9x_nv50_d3d_combine_key(const struct v9x_nv50_d3d_combine *c);
v9x_u32 v9x_nv50_d3d_fp2_start(v9x_u32 slot);
/* The combination's program, as v9x_nv50_d3d_fp. */
v9x_u32 v9x_nv50_d3d_fp2(const struct v9x_nv50_d3d_combine *c,
                         v9x_u32 *words, v9x_u32 max, v9x_u32 *registers);

/*
 * Pixel shader draws (step 4 of docs\plans\nvidia-gt218-ps11.md): the
 * wide vertex (r3d.h, V9X_R3D_SHADER_FLOATS) as attributes 0-6 - the
 * position, diffuse, specular and coordinate sets 0-3, four floats each
 * - which the shader vertex program copies to results 0-27, read as
 * nv50_ps11.h lays out v[]. Translated programs (nv50_ps11.c) go into
 * slots past the two-unit ones (gt218_reserve.h), FP start offsets from
 * the same code base.
 */
#define V9X_NV50_D3D_SHADER_VP_START 0x0600ul
#define V9X_NV50_D3D_SHADER_VP_WORDS_MAX 64ul
#define V9X_NV50_D3D_SHADER_RESULTS  28ul
#define V9X_NV50_D3D_SHADER_UNITS    4ul
#define V9X_NV50_D3D_PS_FIRST        0xf000ul
#define V9X_NV50_D3D_PS_STRIDE       0x1000ul
#define V9X_NV50_D3D_PS_SLOTS        16ul
#define V9X_NV50_D3D_PS_WORDS_MAX    1024ul
v9x_u32 v9x_nv50_d3d_shader_vp(v9x_u32 *words, v9x_u32 max);
v9x_u32 v9x_nv50_d3d_ps_start(v9x_u32 slot);

/* The fragment program's constants (buffer 2), sent with each fogged
 * draw: the fog colour in 0 to 2, its negation in 4 to 6. */
#define V9X_NV50_D3D_FP_CONSTANTS    8ul

/* Compare functions and blend factors, the GL-style codes the 3D class
 * takes (Mesa nvgl_comparison_op; nv_defs.xml's blend factors are GL's
 * enums with 4000h set). */
#define V9X_NV50_D3D_CMP_NEVER       0x0200ul
#define V9X_NV50_D3D_CMP_ALWAYS      0x0207ul
#define V9X_NV50_D3D_BLEND_ZERO               0x4000ul
#define V9X_NV50_D3D_BLEND_ONE                0x4001ul
#define V9X_NV50_D3D_BLEND_SRC_COLOR          0x4300ul
#define V9X_NV50_D3D_BLEND_ONE_MINUS_SRC_COLOR 0x4301ul
#define V9X_NV50_D3D_BLEND_SRC_ALPHA          0x4302ul
#define V9X_NV50_D3D_BLEND_ONE_MINUS_SRC_ALPHA 0x4303ul
#define V9X_NV50_D3D_BLEND_DST_COLOR          0x4306ul
#define V9X_NV50_D3D_BLEND_ONE_MINUS_DST_COLOR 0x4307ul
#define V9X_NV50_D3D_BLEND_SRC_ALPHA_SATURATE 0x4308ul

/* Texture image formats (TIC word 0's component sizes) the sampler takes
 * here, each with blue in the low bits as DirectDraw's are. */
#define V9X_NV50_D3D_TEX_B5G6R5      1ul
#define V9X_NV50_D3D_TEX_B5G5R5A1    2ul
#define V9X_NV50_D3D_TEX_B4G4R4A4    3ul
#define V9X_NV50_D3D_TEX_B8G8R8A8    4ul
#define V9X_NV50_D3D_TEX_B8G8R8X8    5ul

/* Sampler address modes and filters (G80_TSC_WRAP_*, _FILTER_*). */
#define V9X_NV50_D3D_WRAP_REPEAT     0ul
#define V9X_NV50_D3D_WRAP_MIRROR     1ul
#define V9X_NV50_D3D_WRAP_CLAMP      2ul

/* A pitch-linear texture's TIC and TSC words. 0 for a format this does
 * not know. */
int v9x_nv50_d3d_texture(v9x_u32 format, v9x_u32 address, v9x_u32 pitch,
                         v9x_u32 width, v9x_u32 height, v9x_u32 wrap,
                         v9x_u32 mag_linear, v9x_u32 min_linear,
                         v9x_u32 *tic, v9x_u32 *tsc);

/*
 * One draw's state. The target is pitch-linear (or tiled, through its
 * alias window) in format V9X_NV50_3D_RT_*; the scissor is half-open in
 * target pixels. Depth, when has_zeta, is the tiled buffer in zeta.
 * Codes are the V9X_NV50_D3D_CMP_* and _BLEND_* ones; alpha_ref is the
 * reference's float bits on 0 to 1.
 */
struct v9x_nv50_d3d_state {
    struct v9x_nv50_surface target;
    v9x_u32 target_format;
    v9x_u32 scissor_left;
    v9x_u32 scissor_top;
    v9x_u32 scissor_right;
    v9x_u32 scissor_bottom;
    v9x_u32 color_mask;
    v9x_u32 has_zeta;
    struct v9x_nv50_surface zeta;
    v9x_u32 zeta_format;
    v9x_u32 depth_test;
    v9x_u32 depth_write;
    v9x_u32 depth_func;
    v9x_u32 blend;
    v9x_u32 blend_src;
    v9x_u32 blend_dst;
    v9x_u32 alpha_test;
    v9x_u32 alpha_func;
    v9x_u32 alpha_ref;
    v9x_u32 fp_variant;
    v9x_u32 fp_registers;
    v9x_u32 textured;
    v9x_u32 fog_color;
    /* A two-unit draw: its program at fp_start (v9x_nv50_d3d_fp2_start)
     * in place of fp_variant's, fog constants when fog is set, and
     * CODE_CB_FLUSH first when code_flush says the program was just
     * written. textured then names both units' tables. */
    v9x_u32 two_units;
    v9x_u32 fp_start;
    v9x_u32 fog;
    v9x_u32 code_flush;
    /* The two units' combine, from which the HAL finds the program. */
    struct v9x_nv50_d3d_combine combine;
    /* A pixel shader draw: the shader vertex program and interface, the
     * program at fp_start, all four texture units bound, USES_KIL when
     * kills is set; the caller sends the constant block
     * (v9x_nv50_push_d3d_constants). Other draws write FP_CONTROL 0. */
    v9x_u32 shader;
    v9x_u32 kills;
};

/* The colour mask: red, green, blue, alpha writes, a nibble each. */
#define V9X_NV50_D3D_MASK_RGB        0x00000111ul
#define V9X_NV50_D3D_MASK_RGBA       0x00001111ul

/* The state's methods; textured draws flush and bind TIC/TSC 0. */
void v9x_nv50_push_d3d_state(struct v9x_nv50_push *push,
                             const struct v9x_nv50_d3d_state *state);
#define V9X_NV50_D3D_STATE_WORDS_MAX 160ul

/* One vertex, in Direct3D's layout: window position, rhw, diffuse and
 * specular D3DCOLORs, texture coordinates. centre is added to sx and sy
 * (half a pixel: the rasterizer samples at pixel centres, Direct3D's
 * vertices name the pixel corners' grid). Specular and the coordinates
 * are sent only when the draw reads them. */
void v9x_nv50_push_d3d_vertex(struct v9x_nv50_push *push, float sx, float sy,
                              float sz, float rhw, v9x_u32 diffuse,
                              v9x_u32 specular, float tu, float tv,
                              float centre, v9x_u32 with_specular,
                              v9x_u32 with_texture);
#define V9X_NV50_D3D_VERTEX_WORDS_MAX 18ul

/* Unit 1's coordinates for the next vertex (attribute 9), sent before
 * v9x_nv50_push_d3d_vertex, which emits it. */
void v9x_nv50_push_d3d_uv1(struct v9x_nv50_push *push, float tu1, float tv1);
#define V9X_NV50_D3D_UV1_WORDS       3ul

/* count words of the fragment constant buffer from entry 0. */
void v9x_nv50_push_d3d_constants(struct v9x_nv50_push *push,
                                 const v9x_u32 *words, v9x_u32 count);
/* One wide vertex (28 floats), centre added to x and y, the position last;
 * rhw as v9x_nv50_push_d3d_vertex takes it. */
void v9x_nv50_push_d3d_wide(struct v9x_nv50_push *push, const float *vertex,
                            float centre);
#define V9X_NV50_D3D_WIDE_WORDS      35ul

/* VERTEX_BEGIN_GL triangles, and its end. */
void v9x_nv50_push_d3d_begin(struct v9x_nv50_push *push);
void v9x_nv50_push_d3d_end(struct v9x_nv50_push *push);

#endif
