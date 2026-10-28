/*
 * The AMD Carrizo graphics engine microcode, generated into
 * src\chipsets\ati\vi\vi_gfx_microcode.c by
 * scripts\gen-carrizo-gfx-microcode.ps1 (the licence is in that file and
 * NOTICES.TXT). Each image goes to its engine's data port from address 0,
 * then its version to the address port (Linux v4.9 gfx_v8_0.c,
 * smc_load_fw=0).
 */
#ifndef VELOCITY9X_VI_GFX_MICROCODE_H
#define VELOCITY9X_VI_GFX_MICROCODE_H

#include "velocity9x/status.h"

#define V9X_VI_RLC_MICROCODE_DWORDS 3072ul
#define V9X_VI_PFP_MICROCODE_DWORDS 4192ul
#define V9X_VI_CE_MICROCODE_DWORDS  2144ul
#define V9X_VI_ME_MICROCODE_DWORDS  4192ul

extern const v9x_u32 v9x_vi_rlc_microcode_version;
extern const v9x_u32 v9x_vi_rlc_microcode[V9X_VI_RLC_MICROCODE_DWORDS];
extern const v9x_u32 v9x_vi_pfp_microcode_version;
extern const v9x_u32 v9x_vi_pfp_microcode[V9X_VI_PFP_MICROCODE_DWORDS];
extern const v9x_u32 v9x_vi_ce_microcode_version;
extern const v9x_u32 v9x_vi_ce_microcode[V9X_VI_CE_MICROCODE_DWORDS];
extern const v9x_u32 v9x_vi_me_microcode_version;
extern const v9x_u32 v9x_vi_me_microcode[V9X_VI_ME_MICROCODE_DWORDS];

#endif
