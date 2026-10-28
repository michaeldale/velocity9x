/*
 * AMD Carrizo (GFX8) graphics engine configuration: the register writes
 * Linux v4.9 gfx_v8_0.c makes before the engine draws, which a legacy VBE
 * boot leaves undone (docs\decisions\2026-10-28-carrizo-first-draw-
 * attempt.md: the tile table was found unprogrammed).
 *
 * Pure policy, no I/O. The tables are generated into vi_gpu_init_tables.c
 * by scripts\gen-carrizo-gpu-init.ps1 from gfx_v8_0.c (MIT; the notice is
 * in that file). v9x_vi_gpu_init_ops lays them out with gpu_init's own
 * writes as one ordered list for whoever applies them.
 */
#ifndef VELOCITY9X_VI_GPU_INIT_H
#define VELOCITY9X_VI_GPU_INIT_H

#include "velocity9x/status.h"

/* One amdgpu_program_register_sequence entry, as a byte offset: a clear
 * mask of FFFFFFFFh writes set; anything else clears those bits of what
 * the register reads and sets set. */
struct v9x_vi_reg_seq {
    v9x_u32 offset;
    v9x_u32 clear;
    v9x_u32 set;
};

#define V9X_VI_CZ_MGCG_CGCG_COUNT     75u
#define V9X_VI_CZ_GOLDEN_COUNT        12u
#define V9X_VI_CZ_GOLDEN_COMMON_COUNT 8u
#define V9X_VI_TILE_MODES             32u
#define V9X_VI_MACROTILE_MODES        16u

extern const struct v9x_vi_reg_seq
    v9x_vi_cz_mgcg_cgcg_init[V9X_VI_CZ_MGCG_CGCG_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_vi_cz_golden_settings[V9X_VI_CZ_GOLDEN_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_vi_cz_golden_common[V9X_VI_CZ_GOLDEN_COMMON_COUNT];
extern const v9x_u32 v9x_vi_cz_tile_modes[V9X_VI_TILE_MODES];
extern const v9x_u32 v9x_vi_cz_macrotile_modes[V9X_VI_MACROTILE_MODES];

/* Registers gpu_init writes itself (byte offsets). */
#define V9X_VI_GRBM_CNTL            0x08000ul
#define V9X_VI_GB_ADDR_CONFIG       0x098f8ul
#define V9X_VI_HDP_ADDR_CONFIG      0x02f48ul
#define V9X_VI_GB_TILE_MODE0        0x09910ul
#define V9X_VI_GB_MACROTILE_MODE0   0x09990ul
#define V9X_VI_SRBM_GFX_CNTL        0x00e44ul
#define V9X_VI_SH_MEM_BASES         0x08c28ul
#define V9X_VI_SH_MEM_APE1_BASE     0x08c2cul
#define V9X_VI_SH_MEM_APE1_LIMIT    0x08c30ul
#define V9X_VI_SH_MEM_CONFIG        0x08c34ul
#define V9X_VI_PA_SC_FIFO_SIZE      0x08bccul
#define V9X_VI_GRBM_GFX_INDEX       0x30800ul

/* The whole list, in Linux's order:
 *   1. cz_mgcg_cgcg_init, cz_golden_settings_a11, cz_golden_common_all;
 *   2. gpu_init: GRBM_CNTL's READ_TIMEOUT FFh, GB_ADDR_CONFIG and
 *      HDP_ADDR_CONFIG 22010001h, the tile and macrotile tables (Carrizo
 *      skips modes 7, 12, 17 and 23 and macrotile mode 7), SH_MEM_CONFIG,
 *      APE1 and BASES for VMIDs 0-15 through SRBM_GFX_CNTL's VMID select
 *      (back to 0 after), GRBM_GFX_INDEX broadcast, then PA_SC_FIFO_SIZE.
 * DMIF_ADDR_CALC, which gpu_init also writes, is a display register and is
 * left out: the scanout is linear and running. setup_rb's
 * PA_SC_RASTER_CONFIG is golden_common's.
 * Writes count entries to ops and returns the count; 0 if capacity is
 * short. */
#define V9X_VI_GPU_INIT_OPS_MAX     256u
v9x_u32 v9x_vi_gpu_init_ops(struct v9x_vi_reg_seq *ops, v9x_u32 capacity);

#endif
