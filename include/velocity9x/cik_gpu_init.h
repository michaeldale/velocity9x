/*
 * The AMD Mullins (Radeon R3E, GCN 2) graphics engine configuration: what
 * Linux writes before the engine draws, as one list of register entries.
 *
 * The golden tables and tile tables are generated into
 * src\chipsets\ati\cik\cik_gpu_init_tables.c by
 * scripts\gen-mullins-gpu-init.ps1 from Linux cik.c and gfx_v7_0.c (MIT,
 * AMD; notice in that file). v9x_cik_gpu_init_ops lays them out with
 * gfx_v7_0_constants_init's own writes, in Linux's order, as
 * v9x_vi_gpu_init_ops does for Carrizo. The entries use Carrizo's shape
 * (struct v9x_vi_reg_seq): a byte offset, a clear mask and a value; a
 * clear mask of FFFFFFFFh is a plain write.
 */
#ifndef VELOCITY9X_CIK_GPU_INIT_H
#define VELOCITY9X_CIK_GPU_INIT_H

#include "velocity9x/vi_gpu_init.h"

#define V9X_CIK_KALINDI_MGCG_CGCG_COUNT     55u
#define V9X_CIK_GODAVARI_GOLDEN_COUNT       31u
#define V9X_CIK_KALINDI_GOLDEN_COMMON_COUNT 4u
#define V9X_CIK_KALINDI_GOLDEN_SPM_COUNT    1u
#define V9X_CIK_TILE_MODES                  32u
#define V9X_CIK_MACROTILE_MODES             16u

extern const struct v9x_vi_reg_seq
    v9x_cik_kalindi_mgcg_cgcg_init[V9X_CIK_KALINDI_MGCG_CGCG_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_cik_godavari_golden[V9X_CIK_GODAVARI_GOLDEN_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_cik_kalindi_golden_common[V9X_CIK_KALINDI_GOLDEN_COMMON_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_cik_kalindi_golden_spm[V9X_CIK_KALINDI_GOLDEN_SPM_COUNT];
extern const v9x_u32 v9x_cik_mullins_tile_modes[V9X_CIK_TILE_MODES];
extern const v9x_u32 v9x_cik_mullins_macrotile_modes[V9X_CIK_MACROTILE_MODES];

/*
 * The whole list into ops, at most capacity entries. Returns the count,
 * or 0 if capacity is too small or ops is null.
 */
v9x_u32 v9x_cik_gpu_init_ops(struct v9x_vi_reg_seq *ops, v9x_u32 capacity);

#endif
