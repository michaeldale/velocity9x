/*
 * A host model of the Gen3 (i915) fragment program, for one pixel. Host
 * tests only.
 *
 * It decodes the dwords of a _3DSTATE_PIXEL_SHADER_PROGRAM packet with
 * its own field positions, written out as numbers from Mesa gallium's
 * i915_reg.h rather than from the encoder's names, so that a field the
 * encoder misplaces is not misplaced the same way here. Then it runs
 * them as this tree reads the sources:
 *
 *   - registers are four floats; a source is swizzled per channel (X to
 *     W, ZERO, ONE) and negated per channel;
 *   - the result of each arithmetic instruction is computed on all four
 *     channels and written under the mask, clamped to [0, 1] if
 *     saturated;
 *   - RCP, RSQ, EXP and LOG are taken as scalar operations on src0.x
 *     replicated. Whether the hardware reads x or each channel is not
 *     known, so a source that is not one channel replicated is an error
 *     here rather than a guess;
 *   - TEXLD, TEXLDP (x, y, z divided by w first) and TEXLDB call the
 *     sample callback, which answers the four channels;
 *   - TEXKILL kills when any of the four channels of its address is
 *     negative (TEXKILL_4D, which both drivers select).
 *
 * It refuses, as an error, what the encoder should never emit: a T
 * register read before its declaration, an R or U register read before
 * any write, a sampler not declared, two different constant registers in
 * one instruction, an opcode outside the list. Precision is IEEE single;
 * the hardware's is not modelled.
 */
#ifndef VELOCITY9X_TEST_I9XX_FS_SIM_H
#define VELOCITY9X_TEST_I9XX_FS_SIM_H

#include "velocity9x/types.h"

/* A texel for sampler `sampler`, declared with sample type `sample_type`
 * (D0 bits 22-23 as a number: 0 2D, 1 cube, 2 volume), at `coord` (after
 * TEXLDP's divide); `opcode` is the T0 opcode number (0x15 to 0x17). */
typedef void (*v9x_i9xx_fs_sim_sample_fn)(void *user, v9x_u32 sampler,
                                          v9x_u32 sample_type,
                                          v9x_u32 opcode,
                                          const float coord[4],
                                          float rgba[4]);

struct v9x_i9xx_fs_sim {
    float t[11][4];          /* inputs: T0-T7, T8 diffuse, T9 specular  */
    float c[32][4];          /* inputs: the constants                   */
    float r[16][4];
    float u[4][4];
    float oc[4];             /* the output colour                       */
    float od[4];
    v9x_u32 r_written;
    v9x_u32 u_written;
    v9x_u32 oc_written;      /* the oC channels written                 */
    v9x_u32 t_declared;
    v9x_u32 s_declared;
    v9x_u32 s_type[16];
    v9x_u32 executed;        /* instructions run, declarations included */
    v9x_u32 samples;
    int killed;
    int error;               /* set with the dword index of the fault   */
    v9x_u32 error_at;
    v9x_i9xx_fs_sim_sample_fn sample;   /* null: a texture load errs   */
    void *sample_user;
};

/* Clear everything but the inputs (t, c) and the callback. */
void v9x_i9xx_fs_sim_reset(struct v9x_i9xx_fs_sim *sim);

/* Run the program, header included, `dwords` long. Returns 1 when it
 * ran to its end without an error (a kill is not an error). */
int v9x_i9xx_fs_sim_run(struct v9x_i9xx_fs_sim *sim, const v9x_u32 *words,
                        v9x_u32 dwords);

#endif
