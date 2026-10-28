/*
 * AMD Carrizo SDMA engine microcode, generated into
 * src\chipsets\ati\vi\vi_sdma_microcode.c by scripts\gen-carrizo-microcode.ps1
 * from linux-firmware, under AMD's LICENSE.amdgpu (reproduced in that file).
 */
#ifndef VELOCITY9X_VI_SDMA_MICROCODE_H
#define VELOCITY9X_VI_SDMA_MICROCODE_H

#include "velocity9x/status.h"

/* carrizo_sdma.bin's ucode_size_bytes / 4. */
#define V9X_VI_SDMA_MICROCODE_DWORDS 2592u

/* Written to SDMA0_UCODE_ADDR after the image, as Linux does. */
extern const v9x_u32 v9x_vi_sdma_microcode_version;
extern const v9x_u32 v9x_vi_sdma_microcode[V9X_VI_SDMA_MICROCODE_DWORDS];

#endif
