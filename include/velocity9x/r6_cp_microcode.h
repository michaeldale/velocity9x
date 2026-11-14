/*
 * ATI R6xx (HD 2600 XT, RV630) command processor microcode, generated into
 * src\chipsets\ati\r600\r6_cp_microcode.c by scripts\gen-r600-microcode.ps1
 * from linux-firmware, under AMD's LICENSE.radeon (reproduced in that
 * file). The images V9XEGCP and V9XR63D loaded from PFP.BIN and ME.BIN on
 * M-NZHWBR (docs\decisions\2026-11-13-rv630-cp-ring-test.md).
 */
#ifndef VELOCITY9X_R6_CP_MICROCODE_H
#define VELOCITY9X_R6_CP_MICROCODE_H

#include "velocity9x/status.h"

/* radeon_ucode.h R600_PFP_UCODE_SIZE, and R600_PM4_UCODE_SIZE three times
 * (r600_cp_load_microcode writes the ME image's 3 * 1792 dwords). */
#define V9X_R6_PFP_MICROCODE_DWORDS 576u
#define V9X_R6_ME_MICROCODE_DWORDS  5376u

extern const v9x_u32 v9x_r6_rv630_pfp_microcode[V9X_R6_PFP_MICROCODE_DWORDS];
extern const v9x_u32 v9x_r6_rv630_me_microcode[V9X_R6_ME_MICROCODE_DWORDS];

#endif
