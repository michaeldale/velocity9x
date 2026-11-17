/*
 * GCN pixel shaders for DirectX 8 pixel shaders 1.0 and 1.1: step 2 of
 * docs\plans\ati-gcn-ps11.md. A program read by v9x_ps11_parse
 * (velocity9x/ps11.h) becomes GCN 3 words, built with the encoder in
 * vi_shader.c; GCN 2 and GCN 1 run them after v9x_cik_shader_from_vi and
 * v9x_si_shader_from_vi (cik_shader.h), which keep their length.
 *
 * Pure. Each program is run in a host model of the GCN 3 instructions
 * against the reference interpreter (tests\host\test_vi_ps11.c); the card
 * runs them from step 3 (V9XSICP /ps11).
 *
 * What the program expects:
 *
 *   v0:v1          PERSP_CENTER's i and j (SPI_PS_INPUT_ENA)
 *   parameter 0    diffuse (v0), x y z w
 *   parameter 1    specular (v1); its w is the fog factor with desc->fog
 *   parameter 2+N  texture coordinate set N, x y z w, N 0 to 3
 *   s0:s1          the 64-bit address of the table below (two user
 *                  SGPRs), so PRIM_MASK, which v_interp reads through M0,
 *                  is s2
 *   the table      stage N's T# (eight dwords; its TYPE 2D, 3D for a
 *                  texm3x3 lookup on a stage that is not a cube, or CUBE)
 *                  and S#, the constants v9x_vi_ps11_constants writes,
 *                  and the fog colour, which the caller writes
 *
 * It exports r0 to MRT0, compressed (FP16 pairs, SPI_SHADER_COL_FORMAT
 * FP16_ABGR, as every Direct3D PS in vi_shader.c), unclamped beyond the
 * shader's own rules; or with desc->fog, r0 clamped to [0, 1] and its
 * colour blended towards the fog colour as (r0 - fog) f + fog, f the
 * specular alpha: Gen3's and Haswell's rule (i9xx_ps11.h). A program that
 * kills needs DB_SHADER_CONTROL KILL_ENABLE.
 *
 * Cube lookups take the direction to a face and coordinates the way
 * Mesa's ac_prepare_cube_coords does: v_cubeid, v_cubesc, v_cubetc,
 * v_cubema, then sc / |ma| + 1.5 and tc / |ma| + 1.5 (v_cubema returns
 * twice the major axis), the face the third coordinate, sampled with DA.
 * The interpreter divides the direction by its largest magnitude first,
 * which this does not need. Not measured on a card yet (step 6).
 */
#ifndef VELOCITY9X_VI_PS11_H
#define VELOCITY9X_VI_PS11_H

#include "velocity9x/ps11.h"
#include "velocity9x/status.h"
#include "velocity9x/types.h"

#define V9X_VI_PS11_USER_SGPRS   2ul
/* Diffuse, specular and four coordinate sets. */
#define V9X_VI_PS11_PARAMS       6ul
#define V9X_VI_PS11_PARAM_DIFFUSE  0ul
#define V9X_VI_PS11_PARAM_SPECULAR 1ul
#define V9X_VI_PS11_PARAM_COORDS   2ul

/*
 * The table, in dwords from the address in s0:s1. Each block is read by
 * one s_load, so every offset is below the 1020 bytes an SMRD can reach
 * on GCN 1 and 2 (cik_shader.h).
 *
 *   T_SHARP + 8 N     stage N's image descriptor
 *   S_SHARP + 4 N     stage N's sampler
 *   CONST + 4 k       c_k x y z w: the shader's def, else the
 *                     application's, clamped to [-1, 1]
 *   BUMP + 4 N        stage N's bump matrix m00 m01 m10 m11
 *   LUMINANCE + 2 N   stage N's luminance scale and offset
 *   FOG               the fog colour r g b, then the alpha test's
 *                     reference (a float on [0, 1]); both the caller's
 */
#define V9X_VI_PS11_T_SHARP      0ul
#define V9X_VI_PS11_S_SHARP      32ul
#define V9X_VI_PS11_CONST        48ul
#define V9X_VI_PS11_BUMP         80ul
#define V9X_VI_PS11_LUMINANCE    96ul
#define V9X_VI_PS11_FOG          104ul
#define V9X_VI_PS11_ALPHA_REF    107ul
#define V9X_VI_PS11_TABLE_DWORDS 108ul

/* VGPRs every program allocates (SPI_SHADER_PGM_RSRC1 VGPRS). */
#define V9X_VI_PS11_VGPRS        72ul

struct v9x_vi_ps11_desc {
    v9x_u32 cube;           /* bit N: stage N is a cube map           */
    v9x_u32 projected;      /* bit N: stage N's u and v over w        */
    v9x_u32 unbound;        /* bit N: stage N has no texture: its     */
                            /* lookups read (0, 0, 0, 1), unsampled   */
    v9x_u32 fog;            /* fog after the shader                   */
    /* The alpha test after it, which GCN has no fixed function for: the
     * pixel kept where clamp(r0.a) FUNC ref holds, alpha_func a
     * V9X_VI_VOPC_CMPX_* (vi_shader.h), ref the table's ALPHA_REF. */
    v9x_u32 alpha_test;
    v9x_u32 alpha_func;
};

/* What a built program needs from the draw beside its code. */
struct v9x_vi_ps11_info {
    v9x_u32 words;          /* code dwords                            */
    v9x_u32 vgprs;          /* V9X_VI_PS11_VGPRS                      */
    v9x_u32 sgprs;          /* the highest SGPR read or written, + 1  */
    v9x_u32 kills;          /* it kills: DB_SHADER_CONTROL KILL_ENABLE */
    v9x_u32 coords;         /* bit N: coordinate set N is read        */
};

/* The table's constant, bump and luminance dwords for program and env, as
 * float bits; the descriptors and the fog colour are left as they are. */
void v9x_vi_ps11_constants(const V9X_PS11_PROGRAM *program,
                           const V9X_PS11_ENV *env,
                           v9x_u32 table[V9X_VI_PS11_TABLE_DWORDS]);

/*
 * The program into words (capacity in dwords). V9X_STATUS_UNSUPPORTED
 * for a shader this does not translate (texbem on a cube stage, which the
 * interpreter gives no meaning, or one whose descriptors and constants do
 * not fit the SGPRs), V9X_STATUS_INSUFFICIENT_MEMORY when it does not
 * fit; it is refused, not mistranslated.
 */
v9x_status v9x_vi_ps11_build(v9x_u32 *words, v9x_u32 capacity,
                             const V9X_PS11_PROGRAM *program,
                             const struct v9x_vi_ps11_desc *desc,
                             struct v9x_vi_ps11_info *info);

#endif
