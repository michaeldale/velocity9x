/*
 * Haswell (Gen7.5) kernels for DirectX 8 pixel shaders 1.0 and 1.1:
 * step 4 of docs\plans\haswell-ddi8-pixel-shaders.md. A program read by
 * v9x_ps11_parse (velocity9x/ps11.h) becomes a SIMD8 pixel shader kernel,
 * built from the encoder in hsw_eu.c the way the fixed-function kernels
 * are: Align1, one register per colour channel, eight pixels.
 *
 * Pure. Each kernel is run in the host EU model against the reference
 * interpreter (tests\host\test_intel_hsw_ps11.c). Six ran on the 4570
 * through V9XHSWMM /ps11 and matched it there (docs\decisions\
 * 2026-10-30-haswell-ps11-step5a-probe.md).
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
 * With fog, r0 is clamped to [0, 1] and its colour blended towards the
 * fog colour by the specular alpha, (r0 - fog) f + fog: the factor the
 * runtime's vertex fog, or the core's table fog, put there. That is
 * Direct3D's fog after a pixel shader, and hsw_eu.c's fixed-function rule.
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
    v9x_u8 fog;             /* fog after the shader                 */
    v9x_u32 fog_rgb[3];     /* its colour, float bits; read with fog */
};

/* The push block's dwords: V9X_HSW_PS11_CONST_GRFS registers of eight. */
#define V9X_HSW_PS11_CONST_DWORDS (8u * 7u)

/* The push block for env, laid out as above: c0-c7 clamped to [-1, 1],
 * the bump matrices and luminance terms as given, every other dword 0. */
void v9x_intel_hsw_ps11_constants(const V9X_PS11_ENV *env,
                                  v9x_u32 out[V9X_HSW_PS11_CONST_DWORDS]);

/* The kernel for program into out (capacity in dwords); *used dwords. */
v9x_status v9x_intel_hsw_ps11_build(v9x_u32 *out, v9x_u32 capacity,
                                    const V9X_PS11_PROGRAM *program,
                                    const struct v9x_hsw_ps11_desc *desc,
                                    v9x_u32 *used);

#endif
