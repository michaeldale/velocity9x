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
 */
#ifndef VELOCITY9X_TEST_HSW_EU_SIM_H
#define VELOCITY9X_TEST_HSW_EU_SIM_H

#include "velocity9x/types.h"

#define V9X_HSW_SIM_GRFS 128u

struct v9x_hsw_sim {
    v9x_u32 grf[V9X_HSW_SIM_GRFS][8];
    v9x_u32 flag[4];            /* f0.0, f0.1, f1.0, f1.1: 16 bits each */
    v9x_u32 message[16][8];     /* the URB write's payload, header first */
    v9x_u32 message_length;
    v9x_u32 executed;
    int ended;                  /* an EOT send ran */
    int error;                  /* an instruction outside the subset */
};

/* Run from the first word until EOT, an error, or `dwords` run out.
 * Returns 1 for a clean EOT. */
int v9x_hsw_sim_run(struct v9x_hsw_sim *sim, const v9x_u32 *words,
                    v9x_u32 dwords);

float v9x_hsw_sim_float(v9x_u32 bits);
v9x_u32 v9x_hsw_sim_bits(float value);

#endif
