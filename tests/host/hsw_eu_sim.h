/*
 * A host model of the Haswell EU, for the subset the vertex kernels use
 * (src\chipsets\intel\hsw_vs.c). Host tests only.
 *
 * It decodes each instruction with v9x_intel_hsw_eu_decode and executes it
 * as this tree reads the ISA: Align16 SIMD4x2 (vertex v in channels 4v to
 * 4v + 3, a source's vec4 at subnr + v * vstride elements, swizzled), and
 * Align1 for the URB header's MOV and OR. Math is IEEE double rounded to
 * float, POW as exp(y log x); the EU's own precision is not modelled, and
 * agreement here shows the program's logic, not the hardware's numbers.
 *
 * Supported: MOV, OR, ADD, MUL, DP3, DP4, SEL (by predicate or condition),
 * CMP into the flags, MATH INV/RSQ/SQRT/EXP/LOG/POW, and a URB SEND, which
 * is captured; EOT ends the run. Anything else stops the run as an error.
 *
 * For the pixel kernels (src\chipsets\intel\hsw_ps11.c), Align1 SIMD8 as
 * well: PLN against the barycentrics in the register pair src1 names; a
 * sampler SEND, its parameters one register per coordinate, answered per
 * channel by the test's sample callback into four response registers; and
 * a render target write (data port render cache), whose payload is
 * captured as the URB's is, and whose enabled channels - all eight, or the
 * predicate's - are kept in rt_mask, so a kill is seen.
 */
#ifndef VELOCITY9X_TEST_HSW_EU_SIM_H
#define VELOCITY9X_TEST_HSW_EU_SIM_H

#include "velocity9x/types.h"

#define V9X_HSW_SIM_GRFS 128u

/* A sampler answer: the coordinates of one channel (count of them, as the
 * message carried) for sampler and binding table entry bti, into rgba. */
typedef void (*v9x_hsw_sim_sample_fn)(void *user, v9x_u32 sampler,
                                      v9x_u32 bti, v9x_u32 count,
                                      const float *coords, float rgba[4]);

struct v9x_hsw_sim {
    v9x_u32 grf[V9X_HSW_SIM_GRFS][8];
    v9x_u32 flag[4];            /* f0.0, f0.1, f1.0, f1.1: 16 bits each */
    v9x_u32 message[16][8];     /* the URB write's payload, header first */
    v9x_u32 message_length;
    v9x_u32 executed;
    int ended;                  /* an EOT send ran */
    int error;                  /* an instruction outside the subset */
    v9x_hsw_sim_sample_fn sample;   /* null: a sampler send is an error */
    void *sample_user;
    v9x_u32 rt_mask;            /* channels the render target write took */
    v9x_u32 samples;            /* sampler sends run                     */
};

/* Run from the first word until EOT, an error, or `dwords` run out.
 * Returns 1 for a clean EOT. */
int v9x_hsw_sim_run(struct v9x_hsw_sim *sim, const v9x_u32 *words,
                    v9x_u32 dwords);

float v9x_hsw_sim_float(v9x_u32 bits);
v9x_u32 v9x_hsw_sim_bits(float value);

#endif
