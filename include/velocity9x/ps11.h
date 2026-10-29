/*
 * DirectX 8 pixel shaders, versions 1.0 and 1.1, read into a list a
 * translator can walk (step 4 of docs\plans\haswell-ddi8-pixel-shaders.md).
 *
 * The token format and the instructions' meaning are
 * docs\specifications\ddi8-on-win9x.md section 6. What is accepted is what
 * a translator here can run: the arithmetic instructions, and of the
 * texture instructions tex, texcoord, texkill, texbem, texbeml and the
 * texm3x3 family (pad, tex, spec, vspec). texreg2ar, texreg2gb and the
 * texm3x2 pair are refused, as is anything from 1.2 on: no shader 3DMark
 * 2001 SE uses needs them (docs\decisions\
 * 2026-10-29-haswell-ddi8-step3-what-nature-asks.md), and a refused shader
 * fails CreatePixelShader rather than drawing wrong.
 *
 * Pure: the parser is src\display32\d3d\d3d_ps11.c, host-tested in
 * tests\host\test_d3d_ps11.c. The reference interpreter (v9x_ps11_run,
 * src\display32\d3d\d3d_ps11_ref.c) is host-only, as r3d_tnl_ref.c is:
 * the Gen7.5 translator's kernels are run in the host EU model against it
 * (tests\host\test_intel_hsw_ps11.c).
 */
#ifndef VELOCITY9X_PS11_H
#define VELOCITY9X_PS11_H

#include "velocity9x/types.h"

/* Operations. The arithmetic ones first, then the texture ones. */
#define V9X_PS11_NOP          0u
#define V9X_PS11_MOV          1u
#define V9X_PS11_ADD          2u
#define V9X_PS11_SUB          3u
#define V9X_PS11_MUL          4u
#define V9X_PS11_MAD          5u
#define V9X_PS11_LRP          6u
#define V9X_PS11_DP3          7u
#define V9X_PS11_CND          8u
#define V9X_PS11_TEX          16u
#define V9X_PS11_TEXCOORD     17u
#define V9X_PS11_TEXKILL      18u
#define V9X_PS11_TEXBEM       19u
#define V9X_PS11_TEXBEML      20u
#define V9X_PS11_TEXM3X3PAD   21u
#define V9X_PS11_TEXM3X3TEX   22u
#define V9X_PS11_TEXM3X3SPEC  23u
#define V9X_PS11_TEXM3X3VSPEC 24u
#define V9X_PS11_IS_TEXTURE(op) ((op) >= V9X_PS11_TEX)

/* Register files, as the tokens number them. */
#define V9X_PS11_FILE_TEMP    0u    /* r0-r1, r0 the result */
#define V9X_PS11_FILE_INPUT   1u    /* v0 diffuse, v1 specular */
#define V9X_PS11_FILE_CONST   2u    /* c0-c7 */
#define V9X_PS11_FILE_TEXTURE 3u    /* t0-t3 */

/* Source modifiers, the token's values. */
#define V9X_PS11_SRC_NONE     0u
#define V9X_PS11_SRC_NEG      1u
#define V9X_PS11_SRC_BIAS     2u
#define V9X_PS11_SRC_BIASNEG  3u
#define V9X_PS11_SRC_SIGN     4u    /* _bx2 */
#define V9X_PS11_SRC_SIGNNEG  5u
#define V9X_PS11_SRC_COMP     6u    /* 1 - x */

#define V9X_PS11_MAX_INSTRUCTIONS 24u
#define V9X_PS11_MAX_TEXTURE      4u
#define V9X_PS11_MAX_ARITHMETIC   8u

typedef struct v9x_ps11_src {
    v9x_u8 file;
    v9x_u8 reg;
    v9x_u8 swizzle;     /* two bits per output channel, x in 1:0 */
    v9x_u8 mod;         /* V9X_PS11_SRC_* */
} V9X_PS11_SRC;

typedef struct v9x_ps11_inst {
    v9x_u8 op;          /* V9X_PS11_* */
    v9x_u8 coissue;     /* runs beside the instruction before it */
    v9x_u8 dst_file;
    v9x_u8 dst_reg;
    v9x_u8 dst_mask;    /* bit c: channel c written */
    v9x_u8 saturate;
    v9x_s32 shift;      /* -1 _d2, 0, 1 _x2, 2 _x4 */
    v9x_u8 sources;
    V9X_PS11_SRC src[3];
} V9X_PS11_INST;

typedef struct v9x_ps11_program {
    v9x_u32 version;            /* 0x0100 or 0x0101 */
    v9x_u32 count;
    V9X_PS11_INST inst[V9X_PS11_MAX_INSTRUCTIONS];
    /* def: constants the shader sets itself, as IEEE float bits, and
     * which (bit k for c_k). */
    v9x_u32 def[8][4];
    v9x_u32 def_mask;
    /* Bit N: texture stage N is sampled; read as coordinates. */
    v9x_u32 samples;
    v9x_u32 coords;
    /* v0 and v1 are read (bit 0, bit 1). */
    v9x_u32 inputs;
    v9x_u32 has_kill;
} V9X_PS11_PROGRAM;

/* How a parse ended. */
#define V9X_PS11_OK          0u
#define V9X_PS11_MALFORMED   1u  /* tokens past the length, no end token */
#define V9X_PS11_VERSION     2u  /* not ps.1.0 or ps.1.1                 */
#define V9X_PS11_UNSUPPORTED 3u  /* an instruction this cannot run       */
#define V9X_PS11_INVALID     4u  /* a register, mask, modifier or order
                                    1.1 does not allow                  */

/* Read dwords tokens into program. On any status but V9X_PS11_OK the
 * program is not to be used; *where is the token index that stopped it. */
v9x_u32 v9x_ps11_parse(const v9x_u32 *tokens, v9x_u32 dwords,
                       V9X_PS11_PROGRAM *program, v9x_u32 *where);

/*
 * What a draw adds to a shader: the texture stages' bump matrices and
 * luminance terms (texbem, texbeml), which stages are cube maps, and
 * which have projected coordinates. Constants c0-c7 are the
 * application's, before the shader's own def values; both are clamped to
 * [-1, 1] where they are read (ddi8-on-win9x.md section 6).
 */
typedef struct v9x_ps11_env {
    float c[8][4];
    float bump[4][4];       /* stage: m00 m01 m10 m11 */
    float luminance[4][2];  /* stage: scale, offset   */
    v9x_u32 cube;           /* bit N: stage N is a cube map */
    v9x_u32 projected;      /* bit N: stage N's u and v over w */
} V9X_PS11_ENV;

/* One pixel's inputs: diffuse and specular, and texture coordinate sets
 * 0 to 3 (x y z w). */
typedef struct v9x_ps11_pixel {
    float v[2][4];
    float tc[4][4];
} V9X_PS11_PIXEL;

/* A texture lookup: stage, then count coordinates - u v for a 2D stage,
 * and for a cube stage or a texm3x3 lookup three, a cube's already
 * divided by its largest magnitude, as the sampler wants them. */
typedef void (*v9x_ps11_sample_fn)(void *user, v9x_u32 stage, v9x_u32 count,
                                   const float *coords, float rgba[4]);

/*
 * The reference interpreter (host only): run program on one pixel. out
 * is r0 at the end, unclamped beyond the shader's own rules. Returns 0
 * when a texkill killed the pixel, else 1.
 */
int v9x_ps11_run(const V9X_PS11_PROGRAM *program, const V9X_PS11_ENV *env,
                 const V9X_PS11_PIXEL *pixel, v9x_ps11_sample_fn sample,
                 void *user, float out[4]);

#endif
