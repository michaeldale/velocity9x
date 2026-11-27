/*
 * The AMD Southern Islands (GCN 1) graphics engine microcode, generated
 * into src\chipsets\ati\si\si_gfx_microcode.c by
 * scripts\gen-oland-gfx-microcode.ps1 (the licence is in that file and
 * NOTICES.TXT). Each image goes to its engine's data port from address 0,
 * then 0 to the address port (Linux gfx_v6_0.c).
 *
 * The unprefixed images are the Oland's, which ran on W9QI0DAD. Cape
 * Verde, Pitcairn and Tahiti have their own RLC each; Cape Verde its own
 * PFP and ME; Pitcairn a PFP that is Tahiti's too. Every other image of
 * theirs is byte for byte the Oland's (every CE; Pitcairn's and Tahiti's
 * ME), which the generator asserts, so they use the Oland's arrays. None
 * of these three chips' images has run on a card.
 */
#ifndef VELOCITY9X_SI_GFX_MICROCODE_H
#define VELOCITY9X_SI_GFX_MICROCODE_H

#include "velocity9x/status.h"

#define V9X_SI_RLC_MICROCODE_DWORDS 2048ul
#define V9X_SI_PFP_MICROCODE_DWORDS 2144ul
#define V9X_SI_CE_MICROCODE_DWORDS  2144ul
#define V9X_SI_ME_MICROCODE_DWORDS  2144ul

extern const v9x_u32 v9x_si_rlc_microcode_version;
extern const v9x_u32 v9x_si_rlc_microcode[V9X_SI_RLC_MICROCODE_DWORDS];
extern const v9x_u32 v9x_si_pfp_microcode_version;
extern const v9x_u32 v9x_si_pfp_microcode[V9X_SI_PFP_MICROCODE_DWORDS];
extern const v9x_u32 v9x_si_ce_microcode_version;
extern const v9x_u32 v9x_si_ce_microcode[V9X_SI_CE_MICROCODE_DWORDS];
extern const v9x_u32 v9x_si_me_microcode_version;
extern const v9x_u32 v9x_si_me_microcode[V9X_SI_ME_MICROCODE_DWORDS];

extern const v9x_u32 v9x_si_verde_rlc_microcode_version;
extern const v9x_u32 v9x_si_verde_rlc_microcode[V9X_SI_RLC_MICROCODE_DWORDS];
extern const v9x_u32 v9x_si_verde_pfp_microcode_version;
extern const v9x_u32 v9x_si_verde_pfp_microcode[V9X_SI_PFP_MICROCODE_DWORDS];
extern const v9x_u32 v9x_si_verde_me_microcode_version;
extern const v9x_u32 v9x_si_verde_me_microcode[V9X_SI_ME_MICROCODE_DWORDS];
extern const v9x_u32 v9x_si_pitcairn_rlc_microcode_version;
extern const v9x_u32
    v9x_si_pitcairn_rlc_microcode[V9X_SI_RLC_MICROCODE_DWORDS];
extern const v9x_u32 v9x_si_pitcairn_pfp_microcode_version;
extern const v9x_u32
    v9x_si_pitcairn_pfp_microcode[V9X_SI_PFP_MICROCODE_DWORDS];
extern const v9x_u32 v9x_si_tahiti_rlc_microcode_version;
extern const v9x_u32 v9x_si_tahiti_rlc_microcode[V9X_SI_RLC_MICROCODE_DWORDS];

#endif
