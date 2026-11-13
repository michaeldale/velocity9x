/*
 * ATI R6xx (Radeon HD 2600 XT, RV630) 3D command streams: PM4 packets for
 * the command processor, r600_gpu_init's configuration for the RV630, the
 * default state, and one solid rectangle drawn through the shader core.
 *
 * Pure policy, no I/O; host-tested (tests\host\test_r6_3d.c). Not yet run
 * on a card: V9XR63D (tools\diag\rv630_3d_win32.c) is the first to send
 * these streams. What runs under them was measured on M-NZHWBR's HD 2600 XT:
 * AMD's microcode on the CP and Linux's ring test, including a
 * SET_CONFIG_REG through the ring
 * (docs\decisions\2026-11-13-rv630-cp-ring-test.md), and the register
 * values the configuration reads
 * (docs\decisions\2026-11-13-rv630-register-probe.md).
 *
 * Sources, local and outside version control:
 *   build\external\evergreen\linux-radeon\r600.c, r600d.h
 *       r600_gpu_init (the RV630's numbers), r600_cp_start;
 *   build\external\rs690\xf86-video-ati-6.14.6\src\
 *       r6xx_accel.c   r600_set_default_state (pre-RV770 branches),
 *                      r600_sq_setup, r600_set_render_target (with the
 *                      rv6xx SURFACE_BASE_UPDATE workaround), r600_vs_setup,
 *                      r600_ps_setup, r600_fs_setup, r600_set_spi,
 *                      r600_set_alu_consts, r600_set_vtx_resource, the
 *                      scissors, r600_draw_auto, r600_wait_3d_idle_clean,
 *                      r600_cp_set_surface_sync, r600_finish_op;
 *       r600_exa.c     R600PrepareSolid, R600Solid, R600DoneSolid;
 *       r600_state.h   PACK0's choice of packet by register range;
 *       r600_reg.h, r600_reg_auto_r6xx.h, r600_reg_r6xx.h  offsets, fields.
 * R6xx's context and shader registers are not Evergreen's (the CB block
 * starts at 28040h, not 28C60h; SQ_PGM_START_VS is 28858h), so nothing in
 * eg_3d.h applies here except the packet header format.
 *
 * A packet's count is derived from the number of values it is given, never
 * written by hand (eg_3d.h records why).
 */
#ifndef VELOCITY9X_R6_3D_H
#define VELOCITY9X_R6_3D_H

#include "velocity9x/status.h"

/* Config registers (r600d.h, r600_reg_r6xx.h). */
#define V9X_R6_GRBM_CNTL                0x8000ul
#define V9X_R6_GRBM_STATUS              0x8010ul
#define V9X_R6_GRBM_SOFT_RESET          0x8020ul
#define V9X_R6_WAIT_UNTIL               0x8040ul
#define V9X_R6_SCRATCH_REG0             0x8500ul
#define V9X_R6_CP_STAT                  0x8680ul
#define V9X_R6_CP_ME_CNTL               0x86d8ul
#define V9X_R6_CP_RB_RPTR               0x8700ul
#define V9X_R6_CP_QUEUE_THRESHOLDS      0x8760ul
#define V9X_R6_CP_MEQ_THRESHOLDS        0x8764ul
#define V9X_R6_VGT_CACHE_INVALIDATION   0x88c4ul
#define V9X_R6_VGT_GS_PER_ES            0x88c8ul
#define V9X_R6_VGT_ES_PER_GS            0x88ccul
#define V9X_R6_VGT_GS_VERTEX_REUSE      0x88d4ul
#define V9X_R6_VGT_GS_PER_VS            0x88e8ul
#define V9X_R6_CC_GC_SHADER_PIPE_CONFIG 0x8950ul
#define V9X_R6_VGT_PRIMITIVE_TYPE       0x8958ul
#define V9X_R6_VGT_NUM_INSTANCES        0x8974ul
#define V9X_R6_PA_CL_ENHANCE            0x8a14ul
#define V9X_R6_PA_SC_LINE_STIPPLE_STATE 0x8b10ul
#define V9X_R6_PA_SC_MULTI_CHIP_CNTL    0x8b20ul
#define V9X_R6_PA_SC_AA_SAMPLE_LOCS_2S  0x8b40ul
#define V9X_R6_PA_SC_AA_SAMPLE_LOCS_4S  0x8b44ul
#define V9X_R6_PA_SC_AA_SAMPLE_LOCS_8S_WD0 0x8b48ul
#define V9X_R6_PA_SC_AA_SAMPLE_LOCS_8S_WD1 0x8b4cul
#define V9X_R6_PA_SC_ENHANCE            0x8bf0ul
#define V9X_R6_SQ_CONFIG                0x8c00ul
#define V9X_R6_SQ_GPR_RESOURCE_MGMT_1   0x8c04ul
#define V9X_R6_SQ_GPR_RESOURCE_MGMT_2   0x8c08ul
#define V9X_R6_SQ_THREAD_RESOURCE_MGMT  0x8c0cul
#define V9X_R6_SQ_STACK_RESOURCE_MGMT_1 0x8c10ul
#define V9X_R6_SQ_STACK_RESOURCE_MGMT_2 0x8c14ul
#define V9X_R6_SQ_MS_FIFO_SIZES         0x8cf0ul
/* R7xx_SQ_DYN_GPR_CNTL_PS_FLUSH_REQ in xf86's r600_reg_r7xx.h; xf86's
 * default state writes it 0 on every R6xx too. r600d.h has no R6xx name
 * for it. */
#define V9X_R6_SQ_DYN_GPR_CNTL_PS_FLUSH 0x8d8cul
#define V9X_R6_SX_DEBUG_1               0x9054ul
#define V9X_R6_SPI_CONFIG_CNTL          0x9100ul
#define V9X_R6_SPI_CONFIG_CNTL_1        0x913cul
#define V9X_R6_TA_CNTL_AUX              0x9508ul
#define V9X_R6_TC_CNTL                  0x9608ul
#define V9X_R6_VC_ENHANCE               0x9714ul
#define V9X_R6_DB_DEBUG                 0x9830ul
#define V9X_R6_DB_WATERMARKS            0x9838ul
#define V9X_R6_GB_TILING_CONFIG         0x98f0ul
#define V9X_R6_CC_RB_BACKEND_DISABLE    0x98f4ul
/* Outside the SET_CONFIG_REG range: read by MMIO only. */
#define V9X_R6_RAMCFG                   0x2408ul

/* PM4 (r600_reg.h IT_*, r600d.h PACKET3_*). */
#define V9X_R6_PACKET2                  0x80000000ul
#define V9X_R6_P3_START_3D_CMDBUF       0x24ul
#define V9X_R6_P3_CONTEXT_CONTROL       0x28ul
#define V9X_R6_P3_INDEX_TYPE            0x2aul
#define V9X_R6_P3_DRAW_INDEX_AUTO       0x2dul
#define V9X_R6_P3_NUM_INSTANCES         0x2ful
#define V9X_R6_P3_SURFACE_SYNC          0x43ul
#define V9X_R6_P3_ME_INITIALIZE         0x44ul
#define V9X_R6_P3_EVENT_WRITE           0x46ul
#define V9X_R6_P3_SET_CONFIG_REG        0x68ul
#define V9X_R6_P3_SET_CONTEXT_REG       0x69ul
#define V9X_R6_P3_SET_ALU_CONST         0x6aul
#define V9X_R6_P3_SET_BOOL_CONST        0x6bul
#define V9X_R6_P3_SET_LOOP_CONST        0x6cul
#define V9X_R6_P3_SET_RESOURCE          0x6dul
#define V9X_R6_P3_SET_SAMPLER           0x6eul
#define V9X_R6_P3_SET_CTL_CONST         0x6ful
#define V9X_R6_P3_SURFACE_BASE_UPDATE   0x73ul
/* A type-3 packet carries at most this many dwords after its header. */
#define V9X_R6_PACKET3_BODY_MAX         0x3ffful

/* The register ranges each SET_* packet addresses (r600_reg.h), the
 * packet's first body dword being (register - start) / 4. */
#define V9X_R6_SET_CONFIG_START         0x00008000ul
#define V9X_R6_SET_CONFIG_END           0x0000ac00ul
#define V9X_R6_SET_CONTEXT_START        0x00028000ul
#define V9X_R6_SET_CONTEXT_END          0x00029000ul
#define V9X_R6_SET_ALU_CONST_START      0x00030000ul
#define V9X_R6_SET_ALU_CONST_END        0x00032000ul
#define V9X_R6_SET_RESOURCE_START       0x00038000ul
#define V9X_R6_SET_RESOURCE_END         0x0003c000ul
#define V9X_R6_SET_SAMPLER_START        0x0003c000ul
#define V9X_R6_SET_SAMPLER_END          0x0003cff0ul
#define V9X_R6_SET_CTL_CONST_START      0x0003cff0ul
#define V9X_R6_SET_CTL_CONST_END        0x0003e200ul
#define V9X_R6_SET_LOOP_CONST_START     0x0003e200ul
#define V9X_R6_SET_LOOP_CONST_END       0x0003e380ul
#define V9X_R6_SET_BOOL_CONST_START     0x0003e380ul
#define V9X_R6_SET_BOOL_CONST_END       0x0003e38cul

/* SURFACE_SYNC actions (CP_COHER_CNTL, r600_reg_r6xx.h). */
#define V9X_R6_SYNC_CB0_DEST_BASE       0x00000040ul
#define V9X_R6_SYNC_TC_ACTION           0x00800000ul
#define V9X_R6_SYNC_VC_ACTION           0x01000000ul
#define V9X_R6_SYNC_CB_ACTION           0x02000000ul
#define V9X_R6_SYNC_SH_ACTION           0x08000000ul
/* CP_COHER_SIZE of all ones: everything, base ignored. */
#define V9X_R6_SYNC_ALL                 0xfffffffful

/* The CP fetches the ring in 16-dword blocks; each submission is padded
 * to one with type-2 packets, as R600CPFlushIndirect pads. */
#define V9X_R6_CP_ALIGN_DWORDS          16ul

/* A PM4 stream being built: dwords up to capacity; past it, nothing is
 * written and overflow is set, so a caller checks once at the end. A
 * register outside every SET_* range also sets overflow. */
struct v9x_r6_pm4 {
    v9x_u32 *dwords;
    v9x_u32 count;
    v9x_u32 capacity;
    v9x_u32 overflow;
};

void v9x_r6_pm4_begin(struct v9x_r6_pm4 *stream, v9x_u32 *buffer,
                      v9x_u32 capacity);
void v9x_r6_pm4_put(struct v9x_r6_pm4 *stream, v9x_u32 value);
/* A type-3 header for op with body_dwords after it (PACK3's num). */
v9x_u32 v9x_r6_pm4_header(v9x_u32 op, v9x_u32 body_dwords);
/* PACK0: count consecutive registers from offset, through whichever of
 * SET_CONFIG_REG, SET_CONTEXT_REG, SET_ALU_CONST, SET_RESOURCE,
 * SET_SAMPLER, SET_CTL_CONST, SET_LOOP_CONST or SET_BOOL_CONST covers the
 * whole run. xf86 falls back to a type-0 packet outside them; nothing here
 * needs one, so that, a misaligned offset or no values sets overflow and
 * writes nothing. */
void v9x_r6_pm4_set(struct v9x_r6_pm4 *stream, v9x_u32 offset,
                    const v9x_u32 *values, v9x_u32 count);
/* EREG: one register. */
void v9x_r6_pm4_set1(struct v9x_r6_pm4 *stream, v9x_u32 offset,
                     v9x_u32 value);
/* r600_cp_set_surface_sync: CP_COHER_SIZE (size + 255) / 256, or all ones
 * for V9X_R6_SYNC_ALL, base address / 256, poll interval 10. R6xx's GPU
 * addresses are 32 bits. */
void v9x_r6_pm4_surface_sync(struct v9x_r6_pm4 *stream, v9x_u32 actions,
                             v9x_u32 size, v9x_u32 address);
/* Type-2 padding to the next V9X_R6_CP_ALIGN_DWORDS boundary. */
void v9x_r6_pm4_pad(struct v9x_r6_pm4 *stream);

/* r600_cp_start's ME_INITIALIZE: word 2 3 (R6xx), the RV630's 8 hardware
 * contexts less one, DEVICE_ID(1). 7 dwords. */
#define V9X_R6_ME_INITIALIZE_DWORDS     7ul
void v9x_r6_3d_me_initialize(struct v9x_r6_pm4 *stream);

/*
 * r600_gpu_init's register writes for the RV630 that fall in the
 * SET_CONFIG_REG range, in Linux's order: each register is written
 * (value read & keep) | set, keep zero for a plain write. Linux reads
 * first for SX_DEBUG_1, SQ_MS_FIFO_SIZES and SQ_CONFIG.
 *
 * GB_TILING_CONFIG's set comes from RAMCFG (v9x_r6_rv630_tiling_config),
 * not from the table, whose entry for it is a placeholder.
 *
 * Left alone, for this first draw, which is of linear surfaces only: the
 * HDP surface registers 2C14h-2F10h Linux clears, DCP_, HDP_ and
 * DMA_TILING_CONFIG (the display, the host path and the DMA engine use
 * the BIOS's), HDP_HOST_PATH_CNTL and ARB_POP (memory-controller side, and
 * outside the ring's reach). PA_SC_MULTI_CHIP_CNTL, which Linux writes
 * twice, is written once. The context registers gpu_init writes are in
 * v9x_r6_3d_default_state.
 */
struct v9x_r6_reg_update {
    v9x_u32 offset;
    v9x_u32 keep;
    v9x_u32 set;
};
#define V9X_R6_RV630_GPU_INIT_COUNT 33u
extern const struct v9x_r6_reg_update
    v9x_r6_rv630_gpu_init[V9X_R6_RV630_GPU_INIT_COUNT];

/* GB_TILING_CONFIG as r600_gpu_init composes it for 2 tile pipes from
 * RAMCFG: PIPE_TILING(1), BANK_TILING and GROUP_SIZE from its bank count
 * and burst length, ROW_TILING and SAMPLE_SPLIT from its row count (at
 * most 3), BANK_SWAPS(1), and BACKEND_MAP 0: r6xx_remap_render_backend for
 * the RV630's one render backend gives 0 whatever CC_RB_BACKEND_DISABLE
 * says. M-NZHWBR's RAMCFG A9h gives 8A12h, the value its BIOS left. */
v9x_u32 v9x_r6_rv630_tiling_config(v9x_u32 ramcfg);

/* The table written through the ring as SET_CONFIG_REG, one packet per
 * register. read_values holds the value read of each table entry, in table
 * order; tiling_config replaces GB_TILING_CONFIG's. */
void v9x_r6_3d_gpu_init(struct v9x_r6_pm4 *stream,
                        const v9x_u32 *read_values, v9x_u32 tiling_config);

/*
 * xf86's r600_set_default_state for the RV630, inline: START_3D_CMDBUF and
 * CONTEXT_CONTROL (r600_start_3d), then the context registers
 * r600_gpu_init writes (VGT_OUT_DEALLOC_CNTL and VGT_VERTEX_REUSE_BLOCK_CNTL
 * from CC_GC_SHADER_PIPE_CONFIG's inactive pipes, and the zeros), then
 * r600_sq_setup with xf86's RV630 numbers, the default context, the four
 * clip rectangles, the sixteen viewport scissors, and the fetch shader
 * cleared, pointed at fs_gpu (256-byte aligned) as a KMS relocation of 0
 * into xf86's shader buffer points it at the solid VS.
 */
void v9x_r6_3d_default_state(struct v9x_r6_pm4 *stream,
                             v9x_u32 cc_gc_shader_pipe_config,
                             v9x_u32 fs_gpu);

/*
 * One solid rectangle, R600PrepareSolid then R600DoneSolid for a 32 bpp
 * linear target with GXcopy and every plane: the scissors at the target,
 * the solid VS and PS, the render target, SPI with no interpolants, the
 * colour as PS constant 0, the vertex resource, a RECTLIST of three
 * vertices from vertex_gpu, then r600_finish_op's end: CACHE_FLUSH_AND_INV,
 * WAIT_UNTIL 3D idle clean, and the CB synced over the target.
 *
 * GPU addresses are 256-byte aligned. The target is ARGB8888 (COLOR_8_8_8_8
 * with COMP_SWAP 1, as xf86 sets for 32 bpp), linear general (array mode
 * 0), target_pitch pixels a row, a multiple of 8, at most 8192.
 */
#define V9X_R6_3D_PITCH_MAX            8192ul
#define V9X_R6_3D_VERTEX_DWORDS        6ul
#define V9X_R6_3D_VERTEX_STRIDE        8ul
#define V9X_R6_3D_SHADER_SYNC_BYTES    512ul  /* xf86's vs_size, ps_size */
struct v9x_r6_3d_solid {
    v9x_u32 target_gpu;
    v9x_u32 target_pitch;
    v9x_u32 target_height;
    v9x_u32 vs_gpu;
    v9x_u32 ps_gpu;
    v9x_u32 vertex_gpu;
    v9x_u32 colour;              /* ARGB8888 */
};
/* INVALID_ARGUMENT, with nothing written, for a state the hardware cannot
 * take as given; INSUFFICIENT_MEMORY when the stream overflowed. */
v9x_status v9x_r6_3d_solid(const struct v9x_r6_3d_solid *solid,
                           struct v9x_r6_pm4 *stream);

/* R600Solid's three vertices of the rectangle [x1, x2) x [y1, y2): (x1,
 * y1), (x1, y2), (x2, y2), each two floats' bits. Coordinates up to
 * 16777216 convert exactly. */
void v9x_r6_3d_rect_vertices(v9x_u32 x1, v9x_u32 y1, v9x_u32 x2, v9x_u32 y2,
                             v9x_u32 *out);

/* A non-negative integer's single-precision bits, exact to 2^24. */
v9x_u32 v9x_r6_3d_float_bits(v9x_u32 value);

/* SCRATCH_REG0 written through the ring when the CP reaches it; after
 * v9x_r6_3d_solid's WAIT_UNTIL that is once the 3D pipe is idle and
 * clean. */
void v9x_r6_3d_fence(struct v9x_r6_pm4 *stream, v9x_u32 value);

#endif
