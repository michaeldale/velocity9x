/*
 * The AMD Southern Islands (GCN 1) graphics engine configuration: what
 * Linux writes before the engine draws, as one list of register entries.
 *
 * The golden tables and the tile tables are generated into
 * src\chipsets\ati\si\si_gpu_init_tables.c by scripts\gen-oland-gpu-init.ps1
 * from amdgpu si.c and gfx_v6_0.c (MIT, AMD; notice in that file).
 * v9x_si_gpu_init_ops_for lays them out with gfx_v6_0_constants_init's own
 * writes, in Linux's order, as v9x_cik_gpu_init_ops does for Mullins. The
 * entries use Carrizo's shape (struct v9x_vi_reg_seq): a byte offset, a
 * clear mask and a value; a clear mask of FFFFFFFFh is a plain write.
 *
 * Four chips: the Oland, measured on two cards, and Cape Verde, Pitcairn
 * and Tahiti, from Linux alone (docs\decisions\
 * 2026-11-28-si-cape-verde-pitcairn-tahiti-from-surveys.md). Linux reads
 * five things in constants_init and decides on them: the DRAM row
 * (MC_ARB_RAMCFG), the render backends and compute units fused or
 * user-disabled in each shader engine and array (setup_rb, setup_spi), and
 * the disabled texture channel caches (setup_tcc). The caller reads them
 * (struct v9x_si_gpu_config) and the list follows from them here, so the
 * decision is host-tested. Pitcairn and Tahiti have two shader engines,
 * where the Oland and Cape Verde have one; Cape Verde, Pitcairn and Tahiti
 * have two shader arrays (SH) in each, where the Oland has one.
 */
#ifndef VELOCITY9X_SI_GPU_INIT_H
#define VELOCITY9X_SI_GPU_INIT_H

#include "velocity9x/vi_gpu_init.h"

/* The chips, as indices: amdgpu's CHIP_OLAND, CHIP_VERDE, CHIP_PITCAIRN
 * and CHIP_TAHITI. */
#define V9X_SI_CHIP_OLAND       0ul
#define V9X_SI_CHIP_VERDE       1ul
#define V9X_SI_CHIP_PITCAIRN    2ul
#define V9X_SI_CHIP_TAHITI      3ul
#define V9X_SI_CHIP_COUNT       4ul
#define V9X_SI_CHIP_NONE        0xfffffffful

/* Shader engines and arrays per engine at most (Tahiti's and Pitcairn's
 * two and two, gfx_v6_0_constants_init). */
#define V9X_SI_MAX_SE           2ul
#define V9X_SI_MAX_SH           2ul

/* What the caller reads for v9x_si_gpu_init_ops_for, by byte offset
 * (gfx_6_0_d.h and gmc_6_0_d.h dword indices times four). The per-SE/SH
 * pair is read with GRBM_GFX_INDEX set to v9x_si_grbm_select's value, as
 * gfx_v6_0_select_se_sh selects it; GRBM_GFX_INDEX goes back to
 * V9X_SI_GRBM_BROADCAST after. */
#define V9X_SI_GRBM_GFX_INDEX           0x0802cul
#define V9X_SI_GRBM_BROADCAST           0xe0000000ul
#define V9X_SI_MC_ARB_RAMCFG            0x02760ul
#define V9X_SI_CC_RB_BACKEND_DISABLE    0x098f4ul
#define V9X_SI_GC_USER_RB_BACKEND_DISABLE 0x09b7cul
#define V9X_SI_CC_GC_SHADER_ARRAY_CONFIG  0x089bcul
#define V9X_SI_GC_USER_SHADER_ARRAY_CONFIG 0x089c0ul
#define V9X_SI_CGTS_TCC_DISABLE         0x09148ul
#define V9X_SI_CGTS_USER_TCC_DISABLE    0x0914cul

/*
 * The inputs Linux reads, each the OR of the fuse register and the user
 * one as Linux ORs them: rb_disable[se][sh] is CC_RB_BACKEND_DISABLE |
 * GC_USER_RB_BACKEND_DISABLE, cu_disable[se][sh] CC_GC_SHADER_ARRAY_CONFIG
 * | GC_USER_SHADER_ARRAY_CONFIG, both with that SE and SH selected;
 * tcc_disable CGTS_TCC_DISABLE | CGTS_USER_TCC_DISABLE. Entries past the
 * chip's engines and arrays are not read.
 */
struct v9x_si_gpu_config {
    v9x_u32 chip;
    v9x_u32 mc_arb_ramcfg;
    v9x_u32 tcc_disable;
    v9x_u32 rb_disable[V9X_SI_MAX_SE][V9X_SI_MAX_SH];
    v9x_u32 cu_disable[V9X_SI_MAX_SE][V9X_SI_MAX_SH];
};

#define V9X_SI_OLAND_GOLDEN_COUNT       16u
#define V9X_SI_OLAND_GOLDEN_RLC_COUNT   6u
#define V9X_SI_OLAND_MGCG_CGCG_COUNT    60u
#define V9X_SI_VERDE_GOLDEN_COUNT       18u
#define V9X_SI_VERDE_GOLDEN_RLC_COUNT   6u
#define V9X_SI_VERDE_MGCG_CGCG_COUNT    80u
#define V9X_SI_PITCAIRN_GOLDEN_COUNT    16u
#define V9X_SI_PITCAIRN_GOLDEN_RLC_COUNT 6u
#define V9X_SI_PITCAIRN_MGCG_CGCG_COUNT 80u
#define V9X_SI_TAHITI_GOLDEN_COUNT      19u
#define V9X_SI_TAHITI_GOLDEN_RLC_COUNT  6u
#define V9X_SI_TAHITI_MGCG_CGCG_COUNT   110u
#define V9X_SI_TILE_MODES               32u

extern const struct v9x_vi_reg_seq
    v9x_si_oland_golden[V9X_SI_OLAND_GOLDEN_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_si_oland_golden_rlc[V9X_SI_OLAND_GOLDEN_RLC_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_si_oland_mgcg_cgcg_init[V9X_SI_OLAND_MGCG_CGCG_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_si_verde_golden[V9X_SI_VERDE_GOLDEN_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_si_verde_golden_rlc[V9X_SI_VERDE_GOLDEN_RLC_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_si_verde_mgcg_cgcg_init[V9X_SI_VERDE_MGCG_CGCG_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_si_pitcairn_golden[V9X_SI_PITCAIRN_GOLDEN_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_si_pitcairn_golden_rlc[V9X_SI_PITCAIRN_GOLDEN_RLC_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_si_pitcairn_mgcg_cgcg_init[V9X_SI_PITCAIRN_MGCG_CGCG_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_si_tahiti_golden[V9X_SI_TAHITI_GOLDEN_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_si_tahiti_golden_rlc[V9X_SI_TAHITI_GOLDEN_RLC_COUNT];
extern const struct v9x_vi_reg_seq
    v9x_si_tahiti_mgcg_cgcg_init[V9X_SI_TAHITI_MGCG_CGCG_COUNT];

/* The tile tables with a 4 KiB DRAM row, and for each the modes whose
 * TILE_SPLIT is Linux's split_equal_to_row_size, one bit a mode: those
 * take the row v9x_si_gpu_init_ops_for computes. Pitcairn's table is
 * Tahiti's (one case in gfx_v6_0_tiling_mode_table_init). */
extern const v9x_u32 v9x_si_oland_tile_modes[V9X_SI_TILE_MODES];
extern const v9x_u32 v9x_si_oland_row_split_modes;
extern const v9x_u32 v9x_si_verde_tile_modes[V9X_SI_TILE_MODES];
extern const v9x_u32 v9x_si_verde_row_split_modes;
extern const v9x_u32 v9x_si_tahiti_tile_modes[V9X_SI_TILE_MODES];
extern const v9x_u32 v9x_si_tahiti_row_split_modes;

/*
 * The chip a PCI device id is: 6610 and 6611 the Oland, 682C Cape Verde,
 * 6808 Pitcairn, 6780 Tahiti. V9X_SI_CHIP_NONE for any other.
 */
v9x_u32 v9x_si_chip_for_device(v9x_u32 pci_device);

/*
 * The chip's shader engines and arrays per engine, which the caller loops
 * over to fill the per-SE/SH inputs. V9X_STATUS_INVALID_ARGUMENT for an
 * unknown chip or a null pointer.
 */
v9x_status v9x_si_chip_shape(v9x_u32 chip, v9x_u32 *shader_engines,
                             v9x_u32 *sh_per_se);

/*
 * GRBM_GFX_INDEX selecting one SE and SH with instance broadcast, as
 * gfx_v6_0_select_se_sh(se, sh, 0xffffffff) writes it.
 */
v9x_u32 v9x_si_grbm_select(v9x_u32 se, v9x_u32 sh);

/*
 * The whole list for config into ops, at most capacity entries. Returns
 * the count, or 0 if capacity is too small, ops or config is null or the
 * chip is unknown.
 */
v9x_u32 v9x_si_gpu_init_ops_for(const struct v9x_si_gpu_config *config,
                                struct v9x_vi_reg_seq *ops,
                                v9x_u32 capacity);

/*
 * The Oland's list, from the inputs V9XSIMM measured on both Olands
 * (docs\decisions\2026-11-16-oland-gpu-init.md, 2026-11-20-oland-6611-
 * survey.md): MC_ARB_RAMCFG's NOOFCOLS 2, both RBs, six CUs, no TCC off.
 * The list that ran on W9QI0DAD, unchanged.
 */
v9x_u32 v9x_si_gpu_init_ops(struct v9x_vi_reg_seq *ops, v9x_u32 capacity);

#endif
