/*
 * The Tesla fragment-instruction model (nv50_sh_sim.c), shared by the
 * GT218 translator and Direct3D program tests.
 */
#ifndef V9X_TEST_NV50_SH_SIM_H
#define V9X_TEST_NV50_SH_SIM_H

#include <string.h>

#include "velocity9x/nv50_shader.h"
#include "velocity9x/types.h"

#define V9X_NV50_SIM_REGS    64ul
#define V9X_NV50_SIM_BUCKET  127ul
#define V9X_NV50_SIM_VARYINGS 32ul
#define V9X_NV50_SIM_CONSTS  64ul

/* One fragment's machine: registers (as bits), flags, the varyings by
 * byte offset / 4 as interp reads them, one constant buffer slot. */
struct v9x_nv50_sim {
    v9x_u32 r[V9X_NV50_SIM_REGS];
    v9x_u32 flags[V9X_NV50_SH_FLAGS_MAX];
    float v[V9X_NV50_SIM_VARYINGS];
    float c[V9X_NV50_SIM_CONSTS];
    v9x_u32 slot;
    int killed;
    v9x_u32 error_at;       /* the word that stopped the model */
    const char *error;
};

/* A texture fetch: unit, coordinate count and cube flag, the coordinates;
 * rgba out. */
typedef void (*v9x_nv50_sim_sample_fn)(v9x_u32 unit, v9x_u32 argc,
                                       v9x_u32 cube, const float *coords,
                                       float rgba[4]);

/* Run code from word 0 to its exit. 0 on a word the model does not know,
 * with error and error_at set. */
int v9x_nv50_sim_run(struct v9x_nv50_sim *sim, const v9x_u32 *code,
                     v9x_u32 count, v9x_nv50_sim_sample_fn sample);

float v9x_nv50_sim_reg(const struct v9x_nv50_sim *sim, v9x_u32 n);

#endif
