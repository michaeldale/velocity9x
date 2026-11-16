/*
 * The AMD Oland (GCN 1) graphics engine configuration: what Linux writes
 * before the engine draws, as one list of register entries.
 *
 * The golden tables and the tile table are generated into
 * src\chipsets\ati\si\si_gpu_init_tables.c by scripts\gen-oland-gpu-init.ps1
 * from amdgpu si.c and gfx_v6_0.c (MIT, AMD; notice in that file).
 * v9x_si_gpu_init_ops lays them out with gfx_v6_0_constants_init's own
 * writes, in Linux's order, as v9x_cik_gpu_init_ops does for Mullins. The
 * entries use Carrizo's shape (struct v9x_vi_reg_seq): a byte offset, a
 * clear mask and a value; a clear mask of FFFFFFFFh is a plain write.
 */
#ifndef VELOCITY9X_SI_GPU_INIT_H
#define VELOCITY9X_SI_GPU_INIT_H

#include "velocity9x/vi_gpu_init.h"

#define V9X_SI_OLAND_GOLDEN_COUNT       16u
#define V9X_SI_OLAND_GOLDEN_RLC_COUNT   6u
#define V9X_SI_OLAND_MGCG_CGCG_COUNT    60u
#define V9X_SI_TILE_MODES               32u

extern const struct v9x_vi_reg_seq
    v9x_si_oland_golden[V9X_SI_OLAND_GOLDEN_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_si_oland_golden_rlc[V9X_SI_OLAND_GOLDEN_RLC_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_si_oland_mgcg_cgcg_init[V9X_SI_OLAND_MGCG_CGCG_COUNT];
extern const v9x_u32 v9x_si_oland_tile_modes[V9X_SI_TILE_MODES];

/*
 * The whole list into ops, at most capacity entries. Returns the count,
 * or 0 if capacity is too small or ops is null.
 */
v9x_u32 v9x_si_gpu_init_ops(struct v9x_vi_reg_seq *ops, v9x_u32 capacity);

#endif
