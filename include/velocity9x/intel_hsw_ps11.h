/*
 * Haswell (Gen7.5) kernels for DirectX 8 pixel shaders 1.0 and 1.1:
 * step 4 of docs\plans\haswell-ddi8-pixel-shaders.md. A program read by
 * v9x_ps11_parse (velocity9x/ps11.h) becomes a SIMD8 pixel shader kernel,
 * built from the encoder in hsw_eu.c the way the fixed-function kernels
 * are: Align1, one register per colour channel, eight pixels.
 *
 * Pure. Each kernel is run in the host EU model against the reference
 * interpreter (tests\host\test_intel_hsw_ps11.c). None has run on
 * hardware.
 *
 * What the kernel expects (ddi8-on-win9x.md section 7):
 *
 *   g2-g3                 perspective barycentrics (payload)
 *   const_grf + 0 to 3    push constants c0-c7: c_k in register k / 2,
 *                         dwords 4 (k % 2) to + 3, clamped to [-1, 1] by
 *                         whoever fills them
 *   const_grf + 4, + 5    stage s's bump matrix m00 m01 m10 m11, in
 *                         register 4 + s / 2, dwords 4 (s % 2) to + 3
 *   const_grf + 6         stage s's luminance scale and offset, dwords
 *                         2s and 2s + 1
 *   setup_grf             the setup data, two registers an attribute:
 *                         diffuse, specular, then texture coordinate sets
 *                         0 to 3, each four components (the SBE's order)
 *   binding table         entry 0 the render target, 1 + N stage N's
 *                         texture; sampler N for stage N
 *
 * It writes r0 to the render target (SIMD8 single source) and ends. A
 * shader with texkill writes only the pixels it keeps, by predicate.
 * Fixed-function fog after the shader is not here yet.
 */
#ifndef VELOCITY9X_INTEL_HSW_PS11_H
#define VELOCITY9X_INTEL_HSW_PS11_H

#include "velocity9x/ps11.h"
#include "velocity9x/status.h"
#include "velocity9x/types.h"

#define V9X_HSW_PS11_CONST_GRFS      ((v9x_u8)7u)
#define V9X_HSW_PS11_CONST_BUMP      ((v9x_u8)4u)
#define V9X_HSW_PS11_CONST_LUMINANCE ((v9x_u8)6u)
#define V9X_HSW_PS11_ATTRIBUTES      ((v9x_u8)6u)
#define V9X_HSW_PS11_ATTR_TEXCOORD   ((v9x_u8)2u)

struct v9x_hsw_ps11_desc {
    v9x_u8 const_grf;       /* first push constant register         */
    v9x_u8 setup_grf;       /* first setup register                 */
    v9x_u32 cube;           /* bit N: stage N is a cube map         */
    v9x_u32 projected;      /* bit N: stage N's u and v over w      */
};

/* The kernel for program into out (capacity in dwords); *used dwords. */
v9x_status v9x_intel_hsw_ps11_build(v9x_u32 *out, v9x_u32 capacity,
                                    const V9X_PS11_PROGRAM *program,
                                    const struct v9x_hsw_ps11_desc *desc,
                                    v9x_u32 *used);

#endif
