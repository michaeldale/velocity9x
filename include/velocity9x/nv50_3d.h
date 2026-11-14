/*
 * The GT218's Tesla 3D engine (class 8597h): the setup push and the
 * programs for a first triangle, as
 * docs\specifications\gt218-tesla-3d-first-triangle.md lists them from
 * xf86-video-nouveau's EXA (NVAccelInitNV50TCL). Pure arithmetic, no
 * I/O; the 32-bit HAL and the channel probe use it, not the 16-bit
 * driver. Phase 1 of docs\plans\nvidia-gt218-3d.md. Unmeasured on a card
 * until a record says otherwise.
 */
#ifndef VELOCITY9X_NV50_3D_H
#define VELOCITY9X_NV50_3D_H

#include "velocity9x/types.h"
#include "velocity9x/nv50_chan.h"

/* Class 8597h methods (Mesa nv50_3d.xml.h). */
#define V9X_8597_DMA_ZETA            0x0184ul
#define V9X_8597_DMA_ZETA_COUNT      11ul
#define V9X_8597_DMA_COLOR           0x01c0ul
#define V9X_8597_DMA_COLOR_COUNT     8ul
#define V9X_8597_VIEWPORT_HORIZ0     0x0c00ul
#define V9X_8597_SCISSOR_ENABLE0     0x0e00ul
#define V9X_8597_SCISSOR_HORIZ0      0x0e04ul
#define V9X_8597_VP_ADDRESS_HIGH     0x0f7cul
#define V9X_8597_COLOR_MASK_COMMON   0x0f90ul
#define V9X_8597_FP_ADDRESS_HIGH     0x0fa4ul
#define V9X_8597_SCREEN_SCISSOR_HORIZ 0x0ff4ul
#define V9X_8597_RT_CONTROL          0x121cul
#define V9X_8597_LINKED_TSC          0x1234ul
#define V9X_8597_CB_DEF_ADDRESS_HIGH 0x1280ul
#define V9X_8597_TEX_LIMITS2         0x13bcul
#define V9X_8597_VP_START_ID         0x140cul
#define V9X_8597_POINT_SPRITE_ENABLE 0x1520ul
#define V9X_8597_ZETA_ENABLE         0x1538ul
#define V9X_8597_COND_MODE           0x1558ul
#define V9X_8597_TSC_ADDRESS_HIGH    0x155cul
#define V9X_8597_TIC_ADDRESS_HIGH    0x1574ul
#define V9X_8597_VP_ATTR_EN0         0x1650ul
#define V9X_8597_SET_PROGRAM_CB      0x1694ul
#define V9X_8597_VP_RESULT_MAP_SIZE  0x16acul
#define V9X_8597_VP_REG_ALLOC_RESULT 0x16b8ul
#define V9X_8597_VP_RESULT_MAP0      0x16bcul
#define V9X_8597_VIEWPORT_TRANSFORM_EN 0x192cul
#define V9X_8597_FP_INTERPOLANT_CTRL 0x1988ul

/* COND_MODE: always draw. */
#define V9X_8597_COND_MODE_ALWAYS    1ul
/* The constant buffers EXA binds: 1 for the vertex program, 2 for the
 * fragment program, each 256 entries (nv50_accel.h CB_PVP, CB_PFP). */
#define V9X_8597_CB_VP               1ul
#define V9X_8597_CB_FP               2ul
#define V9X_8597_CB_DEF_BUFFER_SHIFT 16u
#define V9X_8597_CB_ENTRIES          256ul
/* The window bounds EXA sets for viewport and scissors. */
#define V9X_8597_BOUND               (8192ul << 16)

/*
 * Where the programs, texture tables and constants go, EXA's scratch
 * layout (nv50_accel.h PVP_OFFSET .. PFP_DATA) from a 4 KiB aligned
 * video memory address, which is also its VM address.
 */
struct v9x_nv50_3d_scratch {
    v9x_u32 vp;
    v9x_u32 fp;
    v9x_u32 tic;
    v9x_u32 tsc;
    v9x_u32 vp_constants;
    v9x_u32 fp_constants;
};

#define V9X_NV50_3D_SCRATCH_VP       0x0000ul
#define V9X_NV50_3D_SCRATCH_FP       0x1000ul
#define V9X_NV50_3D_SCRATCH_TIC      0x2000ul
#define V9X_NV50_3D_SCRATCH_TSC      0x3000ul
#define V9X_NV50_3D_SCRATCH_VP_DATA  0x4000ul
#define V9X_NV50_3D_SCRATCH_FP_DATA  0x4100ul
#define V9X_NV50_3D_SCRATCH_BYTES    0x5000ul

void v9x_nv50_3d_scratch_at(v9x_u32 base, struct v9x_nv50_3d_scratch *out);

/* Bind the 3D object to its subchannel. */
void v9x_nv50_push_bind_3d(struct v9x_nv50_push *push);

/* EXA's setup, without DMA_NOTIFY (EXA binds a null object we do not
 * build). The 3D object must already be bound. */
void v9x_nv50_push_3d_setup(struct v9x_nv50_push *push,
                            const struct v9x_nv50_3d_scratch *scratch);

/* Room for the bind and the setup. */
#define V9X_NV50_3D_SETUP_WORDS_MAX  96ul

/* EXA's vertex program and its PFP_S fragment program, word for word
 * (nv50_accel.c), to be written at scratch.vp and scratch.fp. */
#define V9X_NV50_3D_EXA_VP_WORDS     60ul
#define V9X_NV50_3D_EXA_FP_S_WORDS   6ul
extern const v9x_u32 v9x_nv50_3d_exa_vp[V9X_NV50_3D_EXA_VP_WORDS];
extern const v9x_u32 v9x_nv50_3d_exa_fp_s[V9X_NV50_3D_EXA_FP_S_WORDS];

#endif
