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
 * opaque white and specular absent zero, as the fixed-function pipeline
 * reads them; stage N's coordinates are its TEXCOORDINDEX set, and a set
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

#endif
