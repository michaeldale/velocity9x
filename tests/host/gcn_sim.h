/*
 * A model of the GCN 3 instructions vi_shader.c emits, for one pixel
 * (lane 0 of a wave), so pixel shaders can be run on the host: step 1 of
 * docs\plans\ati-gcn-ps11.md. What each instruction does is the GCN 3 ISA
 * guide's, cross-checked against Mesa (aco, ac_llvm_build.c) where noted
 * in gcn_sim.c. GCN 1 and 2 words are not run: those are the GCN 3 words
 * rewritten by cik_shader.c, which its own test holds to LLVM's bytes.
 */
#ifndef V9X_TEST_GCN_SIM_H
#define V9X_TEST_GCN_SIM_H

#include "velocity9x/types.h"

#define V9X_GCN_SIM_VGPRS   256ul
#define V9X_GCN_SIM_SGPRS   104ul
#define V9X_GCN_SIM_ATTRS   32ul
#define V9X_GCN_SIM_MEMORY  256ul

/* An image_sample: the T# and S# (eight and four dwords as the SGPRs
 * hold them), the four VGPRs from vaddr as floats, the dmask; rgba out. */
typedef void (*v9x_gcn_sim_sample_fn)(const v9x_u32 *t_sharp,
                                      const v9x_u32 *s_sharp,
                                      const float *address, v9x_u32 dmask,
                                      float rgba[4]);

struct v9x_gcn_sim {
    v9x_u32 v[V9X_GCN_SIM_VGPRS];       /* lane 0 of each VGPR, bits */
    v9x_u32 s[V9X_GCN_SIM_SGPRS];
    v9x_u32 vcc[2];
    v9x_u32 exec[2];
    v9x_u32 m0;
    /* What v_interp yields for attribute a, channel c: the value at the
     * pixel (the model does not interpolate). */
    float attr[V9X_GCN_SIM_ATTRS][4];
    /* What s_load reads: memory_dwords dwords at the 64-bit GPU address
     * memory_low/high. */
    const v9x_u32 *memory;
    v9x_u32 memory_low;
    v9x_u32 memory_high;
    v9x_u32 memory_dwords;
    v9x_gcn_sim_sample_fn sample;
    /* Results. */
    float mrt0[4];
    int exported;           /* MRT0 was exported */
    int done;               /* an export with done was seen */
    int killed;             /* lane 0 left EXEC */
    v9x_u32 executed;       /* instructions run */
    v9x_u32 error_at;       /* the word that stopped the model */
    const char *error;
};

/* Clear sim, EXEC lane 0 on; the caller then fills attributes, user
 * SGPRs, memory and sample. */
void v9x_gcn_sim_reset(struct v9x_gcn_sim *sim);

/* Run code from word 0 to s_endpgm. 1 when it ended there, 0 on a word
 * the model does not know or a fault, with error and error_at set. */
int v9x_gcn_sim_run(struct v9x_gcn_sim *sim, const v9x_u32 *code,
                    v9x_u32 count);

float v9x_gcn_sim_vgpr(const struct v9x_gcn_sim *sim, v9x_u32 n);

#endif
