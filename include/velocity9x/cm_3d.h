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

/*
 * The CP's streams. Cayman's packets are Evergreen's (eg_3d.h's pm4
 * helpers); what differs is below. Sources: Linux ni.c cayman_cp_start,
 * and Linux 3.9's cayman_blit_shaders.c and evergreen_blit_kms.c (the
 * CHIP_CAYMAN branches), build\external\evergreen\linux-radeon\v3.9\.
 */
/* cayman_default_state: the clear state, as PM4 (Linux 3.9). */
#define V9X_CM_DEFAULT_CONTEXT_DWORDS 245u
extern const v9x_u32 v9x_cm_default_context[V9X_CM_DEFAULT_CONTEXT_DWORDS];

/* ME_INITIALIZE with max_hw_contexts - 1 = 7 (cayman_cp_start): 7 dwords. */
void v9x_cm_3d_me_initialize(struct v9x_eg_pm4 *stream);
/* cayman_cp_start after ME_INITIALIZE: Evergreen's preamble around
 * cayman_default_state. */
void v9x_cm_3d_preamble(struct v9x_eg_pm4 *stream);
/* Linux 3.9 set_default_state on Cayman: CLEAR_STATE, no SQ block (the
 * GPR, thread and stack splits are Evergreen's), then Evergreen's tail
 * with cayman_default_state. */
void v9x_cm_3d_default_state(struct v9x_eg_pm4 *stream);
/* CP_COHER_CNTL2 = 0, which Cayman needs before each SURFACE_SYNC sent
 * on the ring rather than from an IB (cp_set_surface_sync): 3 dwords. */
#define V9X_CM_CP_COHER_CNTL2       0x85e8ul
void v9x_cm_3d_coher_cntl2(struct v9x_eg_pm4 *stream);

/*
 * The CP's other two rings, which cayman_cp_resume programs with the gfx
 * ring because writing any ring's base resets them all: idle (both
 * pointers 0, nothing submitted), 16 KiB each, in the driver's window
 * (eg_3d.h, V9X_EG_WINDOW_*) after the shader slots and below the probes'
 * scratch at 1 MiB. Their registers (nid.h) and CP_DEBUG and CP_ME_CNTL
 * as Cayman writes them.
 */
#define V9X_CM_WINDOW_CP_RING1      0x00018000ul
#define V9X_CM_WINDOW_CP_RING2      0x0001c000ul
#define V9X_CM_CP_RB2_RPTR          0x86f8ul
#define V9X_CM_CP_RB1_RPTR          0x86fcul
#define V9X_CM_CP_RB1_BASE          0xc180ul
#define V9X_CM_CP_RB1_CNTL          0xc184ul
#define V9X_CM_CP_RB1_RPTR_ADDR     0xc188ul
#define V9X_CM_CP_RB1_RPTR_ADDR_HI  0xc18cul
#define V9X_CM_CP_RB1_WPTR          0xc190ul
#define V9X_CM_CP_RB2_BASE          0xc194ul
#define V9X_CM_CP_RB2_CNTL          0xc198ul
#define V9X_CM_CP_RB2_RPTR_ADDR     0xc19cul
#define V9X_CM_CP_RB2_RPTR_ADDR_HI  0xc1a0ul
#define V9X_CM_CP_RB2_WPTR          0xc1a4ul
#define V9X_CM_CP_DEBUG_VALUE       0x08000000ul /* (1 << 27) */
#define V9X_CM_CP_RUN               0x00000000ul /* cayman_cp_enable */

#endif
