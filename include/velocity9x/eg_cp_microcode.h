/*
 * AMD Evergreen (Cedar) command processor microcode, generated into
 * src\chipsets\ati\evergreen\eg_cp_microcode.c by
 * scripts\gen-evergreen-microcode.ps1 from linux-firmware, under AMD's
 * LICENSE.radeon (reproduced in that file).
 */
#ifndef VELOCITY9X_EG_CP_MICROCODE_H
#define VELOCITY9X_EG_CP_MICROCODE_H

#include "velocity9x/status.h"

/* radeon_ucode.h EVERGREEN_PFP_UCODE_SIZE and EVERGREEN_PM4_UCODE_SIZE. */
#define V9X_EG_PFP_MICROCODE_DWORDS 1120u
#define V9X_EG_ME_MICROCODE_DWORDS  1376u

extern const v9x_u32 v9x_eg_pfp_microcode[V9X_EG_PFP_MICROCODE_DWORDS];
extern const v9x_u32 v9x_eg_me_microcode[V9X_EG_ME_MICROCODE_DWORDS];

#endif
