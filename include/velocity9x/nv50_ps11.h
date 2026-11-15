/*
 * Tesla (GT218) fragment programs for DirectX 8 pixel shaders 1.0 and
 * 1.1: step 2 of docs\plans\nvidia-gt218-ps11.md. A program read by
 * v9x_ps11_parse (velocity9x/ps11.h) becomes Tesla code, built with the
 * encoder in nv50_shader.c.
 *
 * Pure. Each program is run in a host model of the instructions the
 * encoder emits against the reference interpreter (tests\host\
 * test_nv50_ps11.c); V9XN5CH /3d:ps11 draws them on the card.
 *
 * What the program expects:
 *
 *   v[0x00]           the position's w term, as nv50_d3d.c's programs
 *                     read it (interpolated linearly, then 1 / w)
 *   v[0x04]-v[0x10]   diffuse (v0), x y z w, perspective-correct
 *   v[0x14]-v[0x20]   specular (v1)
 *   v[0x24 + 16 N]    texture coordinate set N, x y z w, N 0 to 3
 *   c<slot>[]         v9x_nv50_ps11_constants' block (below)
 *   texture unit and sampler N    stage N: 2D, or a cube map where the
 *                     draw says so, or 3D for a texm3x3 lookup on a
 *                     stage that is not a cube (the interpreter's
 *                     three-coordinate lookup)
 *
 * It leaves r0 in $r0-$r3, the colour result nv50_d3d.c's programs end
 * with, unclamped beyond the shader's own rules. Fog is not applied here;
 * step 4 of the plan adds it where draw_shader needs it.
 *
 * Cube lookups pass the direction as computed: the interpreter divides it
 * by its largest magnitude first, which a cube sampler does not need
 * (Mesa's nv50 cube path passes it unscaled too). Not measured here.
 */
#ifndef VELOCITY9X_NV50_PS11_H
#define VELOCITY9X_NV50_PS11_H

#include "velocity9x/ps11.h"
#include "velocity9x/status.h"
#include "velocity9x/types.h"

/* The varyings, byte offsets in v[]. */
#define V9X_NV50_PS11_V_W            0x00ul
#define V9X_NV50_PS11_V_DIFFUSE      0x04ul
#define V9X_NV50_PS11_V_SPECULAR     0x14ul
#define V9X_NV50_PS11_V_COORDS       0x24ul
#define V9X_NV50_PS11_V_COORD_STRIDE 0x10ul
/* w, two colours and four coordinate sets, one float each. */
#define V9X_NV50_PS11_INTERPOLANTS   25ul

/*
 * The constant block, as float indices (byte offset four times): c0-c7
 * (the shader's def values, else the application's) clamped to [-1, 1];
 * stage N's bump matrix m00 m01 m10 m11; stage N's luminance scale and
 * offset; then the numbers the program multiplies and clamps by.
 */
#define V9X_NV50_PS11_C_CONST        0ul
#define V9X_NV50_PS11_C_BUMP         32ul
#define V9X_NV50_PS11_C_LUMINANCE    48ul
#define V9X_NV50_PS11_C_HALF         56ul
#define V9X_NV50_PS11_C_ONE          57ul
#define V9X_NV50_PS11_C_MINUS_ONE    58ul
#define V9X_NV50_PS11_C_TWO          59ul
#define V9X_NV50_PS11_C_FOUR         60ul
#define V9X_NV50_PS11_C_FLOATS       64ul

/* Registers a program allocates ($r0-$r63), FP_REG_ALLOC's count. */
#define V9X_NV50_PS11_REGISTERS      64ul

struct v9x_nv50_ps11_desc {
    v9x_u32 cube;           /* bit N: stage N is a cube map           */
    v9x_u32 projected;      /* bit N: stage N's u and v over w        */
    v9x_u32 unbound;        /* bit N: stage N has no texture: its     */
                            /* lookups read (0, 0, 0, 1), unsampled   */
    v9x_u32 slot;           /* the constant buffer slot the block is  */
                            /* bound to                               */
};

/* What a built program needs from the draw beside its code. */
struct v9x_nv50_ps11_info {
    v9x_u32 words;          /* code dwords                            */
    v9x_u32 registers;      /* V9X_NV50_PS11_REGISTERS                */
    v9x_u32 kills;          /* it discards: FP_CONTROL USES_KIL       */
    v9x_u32 coords;         /* bit N: coordinate set N is read        */
};

/* The block for program and env as float bits. */
void v9x_nv50_ps11_constants(const V9X_PS11_PROGRAM *program,
                             const V9X_PS11_ENV *env,
                             v9x_u32 out[V9X_NV50_PS11_C_FLOATS]);

/*
 * The program into words (capacity in dwords). V9X_STATUS_UNSUPPORTED
 * for a shader this does not translate (texbem on a cube stage, which the
 * interpreter gives no meaning), V9X_STATUS_INSUFFICIENT_MEMORY when it
 * does not fit; it is refused, not mistranslated.
 */
v9x_status v9x_nv50_ps11_build(v9x_u32 *words, v9x_u32 capacity,
                               const V9X_PS11_PROGRAM *program,
                               const struct v9x_nv50_ps11_desc *desc,
                               struct v9x_nv50_ps11_info *info);

#endif
