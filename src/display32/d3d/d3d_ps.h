/*
 * Pixel shaders 1.0/1.1 in the Direct3D core: step 5b of
 * docs\plans\haswell-ddi8-pixel-shaders.md. What a DDI 8 context keeps for
 * them and what a draw under one hands an engine:
 *
 *   - the shader table: CREATEPIXELSHADER's programs, parsed by
 *     v9x_ps11_parse (velocity9x/ps11.h), found by owner and handle, each
 *     with a serial that names its kernel to an engine's cache;
 *   - the constants c0-c7 as SETPIXELSHADERCONST wrote them;
 *   - per texture stage 0-3, what a shader reads besides the texture: the
 *     bump matrix and luminance terms, the coordinate set (TEXCOORDINDEX),
 *     and the filters and address modes the sampler takes;
 *   - the wide vertex an engine draws: sx sy sz rhw, diffuse and specular
 *     as four floats, then stage 0-3's coordinates, four floats each.
 *
 * Pure, host-tested in tests\host\test_d3d_ps.c. The texture stage state
 * numbers are d3d8types.h's, kept here rather than taken from
 * win9x_ddraw_abi.h so the host build needs no Windows types.
 */
#ifndef VELOCITY9X_D3D_PS_H
#define VELOCITY9X_D3D_PS_H

#include "velocity9x/ps11.h"
#include "velocity9x/types.h"
#include "d3d_dp2.h"
#include "d3d_state.h"

#define V9X_D3D_PS_STAGES          4u
#define V9X_D3D_PS_WIDE_FLOATS     28u

/* D3DTEXTURESTAGESTATETYPE (d3d8types.h); TEXTUREMAP 0 is the DDI's. */
#define V9X_D3D_PS_TSS_TEXTUREMAP      0u
#define V9X_D3D_PS_TSS_BUMPENVMAT00    7u
#define V9X_D3D_PS_TSS_BUMPENVMAT01    8u
#define V9X_D3D_PS_TSS_BUMPENVMAT10    9u
#define V9X_D3D_PS_TSS_BUMPENVMAT11    10u
#define V9X_D3D_PS_TSS_TEXCOORDINDEX   11u
#define V9X_D3D_PS_TSS_ADDRESS         12u
#define V9X_D3D_PS_TSS_ADDRESSU        13u
#define V9X_D3D_PS_TSS_ADDRESSV        14u
#define V9X_D3D_PS_TSS_MAGFILTER       16u
#define V9X_D3D_PS_TSS_MINFILTER       17u
#define V9X_D3D_PS_TSS_MIPFILTER       18u
#define V9X_D3D_PS_TSS_BUMPENVLSCALE   22u
#define V9X_D3D_PS_TSS_BUMPENVLOFFSET  23u
#define V9X_D3D_PS_TSS_TRANSFORMFLAGS  24u

/* D3DTTFF_PROJECTED: the stage divides its coordinates by the last. */
#define V9X_D3D_PS_TTFF_PROJECTED      0x100u

typedef struct v9x_d3d_ps_stage {
    v9x_u32 texture;        /* TEXTUREMAP's handle, 0 for none        */
    v9x_u32 min_filter;     /* D3DTEXTUREFILTERTYPE values            */
    v9x_u32 mag_filter;
    v9x_u32 mip_filter;
    v9x_u32 address_u;      /* D3DTEXTUREADDRESS values               */
    v9x_u32 address_v;
    float bump[4];          /* m00 m01 m10 m11                        */
    float lscale;
    float loffset;
    v9x_u32 texcoord;       /* TEXCOORDINDEX as written               */
    v9x_u32 transform;      /* TEXTURETRANSFORMFLAGS as written       */
} V9X_D3D_PS_STAGE;

/* A stage as Direct3D defaults it: stage N reads set N, wrap, point. */
void v9x_d3d_ps_stage_reset(V9X_D3D_PS_STAGE *stage, v9x_u32 index);

/* One texture stage state; 1 when it is one a shader draw reads. */
int v9x_d3d_ps_stage_state(V9X_D3D_PS_STAGE *stage, v9x_u32 state,
                           v9x_u32 value);

/* SETPIXELSHADERCONST: count registers from first, four float bits each.
 * Registers past c7 are not kept (1.1 has eight). */
void v9x_d3d_ps_set_constants(float c[8][4], v9x_u32 first, v9x_u32 count,
                              const v9x_u32 *bits);

/* What the translator and interpreter are given for a draw: the
 * constants as set, each stage's bump terms, and which stages are cube
 * maps or projected. */
void v9x_d3d_ps_env(const float c[8][4],
                    const V9X_D3D_PS_STAGE stage[V9X_D3D_PS_STAGES],
                    v9x_u32 cube, V9X_PS11_ENV *env);

/*
 * One XYZRHW vertex of layout as the wide vertex. Diffuse absent is
 * opaque white and specular absent black with alpha 1, as d3d_dp2.c gives
 * the fixed-function pipeline them; the alpha is the fog factor, so no
 * fog. Stage N's coordinates are its TEXCOORDINDEX set, and a set
 * the vertex lacks, or a missing component, is 0 with w 1.
 */
void v9x_d3d_ps_wide_vertex(const V9X_DP2_FVF *layout,
                            const V9X_D3D_PS_STAGE stage[V9X_D3D_PS_STAGES],
                            const v9x_u8 *source,
                            float out[V9X_D3D_PS_WIDE_FLOATS]);

/*
 * The shader table. owner is the context a shader was created on, so two
 * contexts' handles never meet; serial is unique while the HAL is loaded
 * and never 0. A full table, or a program the parser refuses, creates
 * nothing.
 */
typedef struct v9x_d3d_ps_shader {
    const void *owner;
    v9x_u32 handle;
    v9x_u32 serial;
    V9X_PS11_PROGRAM program;
} V9X_D3D_PS_SHADER;

#define V9X_D3D_PS_FULL 16u   /* beside the V9X_PS11_* statuses */

v9x_u32 v9x_d3d_ps_create(V9X_D3D_PS_SHADER *table, v9x_u32 slots,
                          v9x_u32 *serial, const void *owner,
                          v9x_u32 handle, const v9x_u32 *tokens,
                          v9x_u32 dwords);

const V9X_D3D_PS_SHADER *v9x_d3d_ps_find(const V9X_D3D_PS_SHADER *table,
                                         v9x_u32 slots, const void *owner,
                                         v9x_u32 handle);

void v9x_d3d_ps_delete(V9X_D3D_PS_SHADER *table, v9x_u32 slots,
                       const void *owner, v9x_u32 handle);

/* Every shader of a context that is going away. */
void v9x_d3d_ps_forget(V9X_D3D_PS_SHADER *table, v9x_u32 slots,
                       const void *owner);

/*
 * Fixed-function texture stages 0 to 2 as a ps.1.1 program, so an engine
 * whose fixed-function kernels cannot sample a stage - a cube map - draws
 * it through its shader path, which can. D3D's stage arithmetic: stage 0's
 * CURRENT is the diffuse colour, each stage's result saturated, the
 * cascade ending at the first disabled stage or at count, a disabled
 * stage 0 colour the diffuse colour and
 * alpha with nothing sampled, an alpha DISABLE under an enabled colour the
 * input's alpha, and the specular colour added last when specular. Ops
 * SELECTARG1, SELECTARG2, MODULATE, MODULATE2X, MODULATE4X and ADD over
 * DIFFUSE, CURRENT, TEXTURE, TFACTOR and SPECULAR, unmodified. Returns the
 * token count, or 0 for anything else, nothing to be drawn so. Host-tested
 * against the reference interpreter (tests\host\test_d3d_ps.c).
 *
 * And the two ops no fixed-function kernel here draws:
 *
 *   - DOTPRODUCT3 as a colour op: dp3_sat of both arguments read signed
 *     (_bx2, 2x - 1), into the colour channels only. The DirectX 8 SDK's
 *     D3DTEXTUREOP page says a colour DOTPRODUCT3 leaves alpha alone in
 *     fixed-function stages; that is recalled, not read here. As an alpha
 *     op it is refused: 1.1's dp3 cannot write alpha alone.
 *   - BUMPENVMAP on stage N with stage N+1 drawn: stage N+1 is sampled
 *     by texbem from stage N's texel, and stage N itself passes CURRENT,
 *     colour and alpha (its own alpha op is not run). That a bump stage
 *     passes CURRENT is recalled from the reference rasterizer, not
 *     measured. 3DMark 2001 SE's bump test has the base map on stage 0,
 *     the bump map on 1 and the environment on 2 (the 4570, boot 247,
 *     d3d8 proxy). Two bump stages in a row are refused, and so is
 *     BUMPENVMAPLUMINANCE, no luminance bump format being published
 *     (V8U8 and Q8W8V8U8 only).
 *
 * The texture factor is c0, and c1 holds it signed for DOTPRODUCT3, so no
 * constant takes a modifier; v9x_d3d_ps_fixed_env fills both.
 */
#define V9X_D3D_PS_FIXED_DWORDS 48u
#define V9X_D3D_PS_FIXED_STAGES 3u

/* stages[0..count-1], count 1 to V9X_D3D_PS_FIXED_STAGES: the stages the
 * caller has state for. */
v9x_u32 v9x_d3d_ps_fixed(const V9X_D3D_STATE_STAGE *stages, v9x_u32 count,
                         int specular,
                         v9x_u32 tokens[V9X_D3D_PS_FIXED_DWORDS]);

/* 1 when the stages that are drawn hold an op only the program above
 * draws: DOTPRODUCT3 or a bump op as a colour op. */
int v9x_d3d_ps_fixed_needs(const V9X_D3D_STATE_STAGE *stages, v9x_u32 count);

/*
 * What a draw under a v9x_d3d_ps_fixed program adds to v9x_d3d_ps_env's
 * environment: c0 the texture factor (D3DCOLOR, alpha in the top byte) as
 * r g b a in [0, 1], c1 the same signed; and for each texbem, the bump
 * matrix and luminance terms of the stage it reads its (du, dv) from,
 * placed on the stage it samples. Direct3D keeps a fixed-function bump
 * matrix on the bump map's stage; texbem reads the sampled stage's.
 */
void v9x_d3d_ps_fixed_env(const V9X_PS11_PROGRAM *program,
                          const V9X_D3D_PS_STAGE stage[V9X_D3D_PS_STAGES],
                          v9x_u32 factor, V9X_PS11_ENV *env);

#endif
