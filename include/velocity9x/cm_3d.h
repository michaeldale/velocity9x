/*
 * AMD Cayman-generation (Trinity, HD 7660D) 3D engine configuration: the
 * writes Linux's cayman_gpu_init makes for 1002:9901, and the render
 * backend map it computes.
 *
 * Pure policy, no I/O. Sources: Linux ni.c cayman_gpu_init and nid.h, and
 * r600.c r6xx_remap_render_backend
 * (build\external\evergreen\linux-radeon\). The fuses this configuration
 * assumes (six SIMDs, both backends enabled) were read on M-D7TFW4
 * (docs\decisions\2026-11-03-trinity-tier0-and-registers.md).
 */
#ifndef VELOCITY9X_CM_3D_H
#define VELOCITY9X_CM_3D_H

#include "velocity9x/eg_3d.h"

/*
 * GB_BACKEND_MAP as r6xx_remap_render_backend computes it for Evergreen
 * and later (4-bit fields): tiling_pipe_log2 is GB_ADDR_CONFIG's NUM_PIPES
 * field, max_rb the chip's backends, total_max_rb the generation's
 * (CAYMAN_MAX_BACKENDS and EVERGREEN_MAX_BACKENDS are both 8),
 * disabled_rb_mask the fused-off ones. 0 when the arguments are out of
 * range (Linux's BUG_ON: fewer pipes than backends) or leave no backend.
 */
v9x_u32 v9x_cm_backend_map(v9x_u32 tiling_pipe_log2, v9x_u32 max_rb,
                           v9x_u32 total_max_rb, v9x_u32 disabled_rb_mask);

/*
 * cayman_gpu_init's register writes for 9901, in order, in Evergreen's
 * form (value read & keep) | set; the CP must be halted. Left out, as the
 * Evergreen tables leave them: the HDP surface loop, the SRBM interrupt
 * enable and acknowledge, GRBM/RLC_GFX_INDEX's broadcast (the BIOS leaves
 * it broadcast; the fuse reads that index by SE are not needed with one
 * SE), the address-config copies the display, host path, DMA engines and
 * UVD use, HDP_HOST_PATH_CNTL's write of its own value, and Trinity's
 * clock-gating values behind CG_IND. Also left out, and not on Evergreen:
 * CGTS_SM_CTRL_REG's sixteen OVERRIDE writes and its restore ("reprogram
 * the shader complex"), which a table cannot express because the restore
 * is the value read first; V9XEG3D does it in its op list.
 */
#define V9X_CM_TRINITY_GPU_INIT_COUNT 41u
extern const struct v9x_eg_reg_update
    v9x_cm_trinity_gpu_init[V9X_CM_TRINITY_GPU_INIT_COUNT];

/* The golden address configuration and the backend map in that table. */
#define V9X_CM_ARUBA_GB_ADDR_CONFIG 0x12010001ul
#define V9X_CM_TRINITY_BACKEND_MAP  0x00000010ul

/* CGTS_SM_CTRL_REG and its OVERRIDE bit, for the sequence above. */
#define V9X_CM_CGTS_SM_CTRL_REG     0x9150ul
#define V9X_CM_CGTS_SM_OVERRIDE     0x00200000ul
#define V9X_CM_CGTS_SM_OVERRIDE_WRITES 16ul

#endif
