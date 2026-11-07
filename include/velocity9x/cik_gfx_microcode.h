/*
 * The AMD Mullins graphics engine microcode, generated into
 * src\chipsets\ati\cik\cik_gfx_microcode.c by
 * scripts\gen-mullins-gfx-microcode.ps1 (the licence is in that file and
 * NOTICES.TXT). Each image goes to its engine's data port from address 0,
 * then its version to the address port (Linux gfx_v7_0.c).
 */
#ifndef VELOCITY9X_CIK_GFX_MICROCODE_H
#define VELOCITY9X_CIK_GFX_MICROCODE_H

#include "velocity9x/status.h"

#define V9X_CIK_RLC_MICROCODE_DWORDS 2560ul
#define V9X_CIK_PFP_MICROCODE_DWORDS 2144ul
#define V9X_CIK_CE_MICROCODE_DWORDS  2144ul
#define V9X_CIK_ME_MICROCODE_DWORDS  2144ul

extern const v9x_u32 v9x_cik_rlc_microcode_version;
extern const v9x_u32 v9x_cik_rlc_microcode[V9X_CIK_RLC_MICROCODE_DWORDS];
extern const v9x_u32 v9x_cik_pfp_microcode_version;
extern const v9x_u32 v9x_cik_pfp_microcode[V9X_CIK_PFP_MICROCODE_DWORDS];
extern const v9x_u32 v9x_cik_ce_microcode_version;
extern const v9x_u32 v9x_cik_ce_microcode[V9X_CIK_CE_MICROCODE_DWORDS];
extern const v9x_u32 v9x_cik_me_microcode_version;
extern const v9x_u32 v9x_cik_me_microcode[V9X_CIK_ME_MICROCODE_DWORDS];

#endif
