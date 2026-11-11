/*
 * Gen3 (i915) fragment programs for DirectX 8 pixel shaders 1.0 and 1.1:
 * step 3 of docs\plans\gen3-ps11-pixel-shaders.md. A program read by
 * v9x_ps11_parse (velocity9x/ps11.h) becomes a _3DSTATE_PIXEL_SHADER_
 * PROGRAM packet, built with the encoder in i9xx_fs.c.
 *
 * Pure. Each program is run in the host model of the fragment program
 * (tests\host\i9xx_fs_sim.c) against the reference interpreter
 * (tests\host\test_i9xx_ps11.c). Nothing has run on the hardware.
 *
 * What the program expects:
 *
 *   T0-T3      texture coordinate sets 0 to 3
 *   T8, T9     diffuse and specular (v0, v1); T9.w the fog factor
 *   S0-S3      stage N's sampler, declared 2D, or CUBE for a cube stage,
 *              or VOLUME for a texm3x3 lookup on a stage that is not a
 *              cube (the interpreter's three-coordinate lookup)
 *   C0-C17     v9x_i9xx_ps11_constants' block (below)
 *
 * Cube lookups are sampled with the direction as computed: the
 * interpreter divides it by its largest magnitude first, which a cube
 * sampler does not need. That the Gen3 sampler is indifferent to the
 * scale is from both drivers, which pass the direction unscaled; it has
 * not been measured here.
 *
 * It writes r0 to oC unclamped (the colour buffer's conversion clamps),
 * or with fog r0 clamped to [0, 1] and its colour blended towards the
 * fog colour, (r0 - fog) f + fog, f in T9.w: hsw_ps11.c's rule.
 */
#ifndef VELOCITY9X_I9XX_PS11_H
#define VELOCITY9X_I9XX_PS11_H

#include "velocity9x/ps11.h"
#include "velocity9x/status.h"
#include "velocity9x/types.h"

/* The constant block: C0-C7 the shader's c0-c7 (its def values, else
 * the application's), clamped to [-1, 1]; C8 + N stage N's bump matrix
 * m00 m01 m10 m11; C12 + N stage N's luminance scale and offset, 0, 0;
 * C16 (0.5, 2, 4, 0), the numbers the expansions multiply by; C17 the
 * fog colour, 0 in w. */
#define V9X_I9XX_PS11_CONST_BUMP      8u
#define V9X_I9XX_PS11_CONST_LUMINANCE 12u
#define V9X_I9XX_PS11_CONST_FIXED     16u
#define V9X_I9XX_PS11_CONST_FOG       17u
#define V9X_I9XX_PS11_CONST_REGS      18u

struct v9x_i9xx_ps11_desc {
    v9x_u32 cube;           /* bit N: stage N is a cube map           */
    v9x_u32 projected;      /* bit N: stage N's u and v over w        */
    v9x_u32 unbound;        /* bit N: stage N has no texture: its     */
                            /* lookups read (0, 0, 0, 1), unsampled   */
    v9x_u32 fog;            /* fog after the shader                   */
};

/* The block for program and env as float bits, four a register;
 * fog_rgb three float bits, or null for black. */
void v9x_i9xx_ps11_constants(const V9X_PS11_PROGRAM *program,
                             const V9X_PS11_ENV *env,
                             const v9x_u32 *fog_rgb,
                             v9x_u32 out[V9X_I9XX_PS11_CONST_REGS * 4u]);

/* The program, header included, into out (capacity in dwords); *used
 * its dwords. V9X_STATUS_UNSUPPORTED for a shader the fragment program
 * cannot hold (over 64 arithmetic instructions, 4 phases, or texbem on a
 * cube stage); it is refused, not mistranslated. */
v9x_status v9x_i9xx_ps11_build(v9x_u32 *out, v9x_u32 capacity,
                               const V9X_PS11_PROGRAM *program,
                               const struct v9x_i9xx_ps11_desc *desc,
                               v9x_u32 *used);

#endif
