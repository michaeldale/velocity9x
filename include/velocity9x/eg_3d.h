/*
 * AMD Evergreen (Cedar) 3D command streams: PM4 packets for the command
 * processor, the GPU's configuration for Cedar, the clear-state preamble,
 * and the state and draw of one Direct3D batch.
 *
 * Pure policy, no I/O; host-tested (tests\host\test_eg_3d.c). Every
 * sequence here ran on M-BXMFA8's HD 5450 through V9XEG3D first:
 * evergreen_gpu_init's writes, ME_INITIALIZE and the preamble
 * (docs\decisions\2026-10-26-cedar-cp-runs-amd-microcode.md), Linux 3.9's
 * set_default_state and its render target, scissor, shader and resource
 * packets (docs\decisions\2026-10-27-cedar-first-3d-draw.md), the linear
 * colour buffer's NON_DISP_TILING_ORDER
 * (docs\decisions\2026-10-27-cedar-linear-colour-buffer.md), and the Z16
 * depth buffer and second interpolant
 * (docs\decisions\2026-10-27-cedar-gouraud-depth-modulate.md). Register
 * offsets and fields are Mesa r600's evergreend.h.
 *
 * A packet's count is derived from the number of values it is given, never
 * written by hand: a hand-written count of 9 for 8 values sent the CP into
 * a stray register write on that card (the last record above).
 */
#ifndef VELOCITY9X_EG_3D_H
#define VELOCITY9X_EG_3D_H

#include "velocity9x/status.h"

/* Config registers (MMIO and SET_CONFIG_REG). */
#define V9X_EG_GRBM_CNTL            0x8000ul
#define V9X_EG_GRBM_STATUS          0x8010ul
#define V9X_EG_GRBM_SOFT_RESET      0x8020ul
#define V9X_EG_SCRATCH_REG0         0x8500ul
#define V9X_EG_SCRATCH_UMSK         0x8540ul
#define V9X_EG_SCRATCH_ADDR         0x8544ul
#define V9X_EG_CP_SEM_WAIT_TIMER    0x85bcul
#define V9X_EG_CP_SEM_INCOMPLETE    0x85c8ul
#define V9X_EG_CP_STAT              0x8680ul
#define V9X_EG_CP_ME_CNTL           0x86d8ul
#define V9X_EG_CP_RB_RPTR           0x8700ul
#define V9X_EG_CP_RB_WPTR_DELAY     0x8704ul
#define V9X_EG_VGT_PRIMITIVE_TYPE   0x8958ul
#define V9X_EG_SQ_CONFIG            0x8c00ul
#define V9X_EG_SQ_DYN_GPR_FLUSH     0x8d8cul
#define V9X_EG_SQ_LDS_RESOURCE_MGMT 0x8e2cul
#define V9X_EG_CP_RB_BASE           0xc100ul
#define V9X_EG_CP_RB_CNTL           0xc104ul
#define V9X_EG_CP_RB_RPTR_WR        0xc108ul
#define V9X_EG_CP_RB_RPTR_ADDR      0xc10cul
#define V9X_EG_CP_RB_RPTR_ADDR_HI   0xc110ul
#define V9X_EG_CP_RB_WPTR           0xc114ul
#define V9X_EG_CP_PFP_UCODE_ADDR    0xc150ul
#define V9X_EG_CP_PFP_UCODE_DATA    0xc154ul
#define V9X_EG_CP_ME_RAM_RADDR      0xc158ul
#define V9X_EG_CP_ME_RAM_WADDR      0xc15cul
#define V9X_EG_CP_ME_RAM_DATA       0xc160ul
#define V9X_EG_CP_DEBUG             0xc1fcul

/* CP control values, as V9XEGCP and V9XEG3D measured them. */
#define V9X_EG_CP_HALT              0x14000000ul
#define V9X_EG_CP_RUN               0x000000fful
#define V9X_EG_RB_NO_UPDATE         0x08000000ul
#define V9X_EG_RB_RPTR_WR_ENA       0x80000000ul
#define V9X_EG_GRBM_RESET_CP_BLOCKS 0x00004721ul
#define V9X_EG_CP_DEBUG_VALUE       0x18000000ul
#define V9X_EG_RB_CNTL_LOADING      0x08000f03ul
#define V9X_EG_GUI_ACTIVE           0x80000000ul
/* RB_BUFSZ 11 (2^11 quadwords, 16 KiB) and RB_BLKSZ 9, the probe's. */
#define V9X_EG_CP_RING_BYTES        0x4000ul
#define V9X_EG_CP_RING_DWORDS       (V9X_EG_CP_RING_BYTES / 4ul)
#define V9X_EG_CP_RB_CNTL_VALUE     ((9ul << 8) | 11ul)
/* The CP fetches the ring in 16-dword blocks; each submission is padded
 * to one with type-2 packets, as Linux's ring commit pads. */
#define V9X_EG_CP_ALIGN_DWORDS      16ul
#define V9X_EG_PFP_DWORDS           1120ul
#define V9X_EG_ME_DWORDS            1376ul

/*
 * The driver's VRAM window at BAR0 + 240 MiB, which the mini-VDD maps
 * (V9X_EG_RING_BYTES in loader.asm): the DMA ring and its fence in the
 * first 64 KiB (eg_dma.h), then the CP's ring, its write-back slots and
 * the shaders, and the vertex arena in the top half. The probes' scratch
 * window at + 1 MiB lies between, unused here.
 */
#define V9X_EG_WINDOW_BYTES         0x00400000ul
#define V9X_EG_WINDOW_CP_RING       0x00010000ul
#define V9X_EG_WINDOW_CP_WRITEBACK  0x00014000ul
#define V9X_EG_WINDOW_SHADERS       0x00015000ul
#define V9X_EG_WINDOW_SHADER_BYTES  0x00000100ul
#define V9X_EG_WINDOW_VERTICES      0x00200000ul
#define V9X_EG_WINDOW_VERTEX_BYTES  0x00200000ul

/* PM4 (evergreend.h). */
#define V9X_EG_PACKET2              0x80000000ul
#define V9X_EG_P3_CLEAR_STATE       0x12ul
#define V9X_EG_P3_MODE_CONTROL      0x18ul
#define V9X_EG_P3_CONTEXT_CONTROL   0x28ul
#define V9X_EG_P3_INDEX_TYPE        0x2aul
#define V9X_EG_P3_DRAW_INDEX_AUTO   0x2dul
#define V9X_EG_P3_NUM_INSTANCES     0x2ful
#define V9X_EG_P3_SURFACE_SYNC      0x43ul
#define V9X_EG_P3_ME_INITIALIZE     0x44ul
#define V9X_EG_P3_PREAMBLE_CNTL     0x4aul
#define V9X_EG_P3_SET_CONFIG_REG    0x68ul
#define V9X_EG_P3_SET_CONTEXT_REG   0x69ul
#define V9X_EG_P3_SET_RESOURCE      0x6dul
#define V9X_EG_P3_SET_SAMPLER       0x6eul
#define V9X_EG_P3_SET_CTL_CONST     0x6ful
#define V9X_EG_SET_CONFIG_START     0x00008000ul
#define V9X_EG_SET_CONFIG_END       0x0000ac00ul
#define V9X_EG_SET_CONTEXT_START    0x00028000ul
#define V9X_EG_SET_CONTEXT_END      0x00029000ul
/* A type-3 packet carries at most this many dwords after its header. */
#define V9X_EG_PACKET3_BODY_MAX     0x3ffful

/* SURFACE_SYNC actions (cp_set_surface_sync). */
#define V9X_EG_SYNC_CB0_DEST_BASE   0x00000040ul
#define V9X_EG_SYNC_DB_DEST_BASE    0x00004000ul
#define V9X_EG_SYNC_TC_ACTION       0x00800000ul
#define V9X_EG_SYNC_CB_ACTION       0x02000000ul
#define V9X_EG_SYNC_DB_ACTION       0x04000000ul
#define V9X_EG_SYNC_SH_ACTION       0x08000000ul

/* Colour buffer and texture formats (V_028C70_COLOR_*; the texture's
 * FMT_ numbers are the same). */
#define V9X_EG_COLOR_5_6_5          0x08ul
#define V9X_EG_COLOR_8_8_8_8        0x1aul

/* Array modes. */
#define V9X_EG_ARRAY_LINEAR_GENERAL 0ul
#define V9X_EG_ARRAY_LINEAR_ALIGNED 1ul
#define V9X_EG_ARRAY_1D_TILED_THIN1 2ul

/* Comparison functions, shared by DB_DEPTH_CONTROL's ZFUNC and
 * SX_ALPHA_TEST_CONTROL's ALPHA_FUNC: NEVER 0 ... ALWAYS 7, Direct3D's
 * D3DCMP_* less one. */
#define V9X_EG_FUNC_NEVER           0ul
#define V9X_EG_FUNC_LESS            1ul
#define V9X_EG_FUNC_ALWAYS          7ul

/* CB_BLEND0_CONTROL factors (V_028780_BLEND_*). */
#define V9X_EG_BLEND_ZERO                0ul
#define V9X_EG_BLEND_ONE                 1ul
#define V9X_EG_BLEND_SRC_COLOR           2ul
#define V9X_EG_BLEND_ONE_MINUS_SRC_COLOR 3ul
#define V9X_EG_BLEND_SRC_ALPHA           4ul
#define V9X_EG_BLEND_ONE_MINUS_SRC_ALPHA 5ul
#define V9X_EG_BLEND_DST_ALPHA           6ul
#define V9X_EG_BLEND_ONE_MINUS_DST_ALPHA 7ul
#define V9X_EG_BLEND_DST_COLOR           8ul
#define V9X_EG_BLEND_ONE_MINUS_DST_COLOR 9ul
#define V9X_EG_BLEND_SRC_ALPHA_SATURATE  10ul

/* Sampler addressing (V_03C000_SQ_TEX_*). */
#define V9X_EG_TEX_WRAP             0ul
#define V9X_EG_TEX_MIRROR           1ul
#define V9X_EG_TEX_CLAMP_LAST_TEXEL 2ul

/* A PM4 stream being built: dwords up to capacity; past it, nothing is
 * written and overflow is set, so a caller checks once at the end. */
struct v9x_eg_pm4 {
    v9x_u32 *dwords;
    v9x_u32 count;
    v9x_u32 capacity;
    v9x_u32 overflow;
};

void v9x_eg_pm4_begin(struct v9x_eg_pm4 *stream, v9x_u32 *buffer,
                      v9x_u32 capacity);
void v9x_eg_pm4_put(struct v9x_eg_pm4 *stream, v9x_u32 value);
/* SET_CONFIG_REG or SET_CONTEXT_REG of count consecutive registers from
 * offset. A register outside the packet's range, or no values, sets
 * overflow and writes nothing. */
void v9x_eg_pm4_set_config(struct v9x_eg_pm4 *stream, v9x_u32 offset,
                           const v9x_u32 *values, v9x_u32 count);
void v9x_eg_pm4_set_context(struct v9x_eg_pm4 *stream, v9x_u32 offset,
                            const v9x_u32 *values, v9x_u32 count);
void v9x_eg_pm4_set_config1(struct v9x_eg_pm4 *stream, v9x_u32 offset,
                            v9x_u32 value);
void v9x_eg_pm4_set_context1(struct v9x_eg_pm4 *stream, v9x_u32 offset,
                             v9x_u32 value);
/* cp_set_surface_sync, Evergreen form. size 0xffffffff syncs everything
 * (CP_COHER_SIZE all ones, base 0); otherwise size bytes from address. */
void v9x_eg_pm4_surface_sync(struct v9x_eg_pm4 *stream, v9x_u32 actions,
                             v9x_u32 size, v9x_u32 address);
/* Type-2 padding to the next V9X_EG_CP_ALIGN_DWORDS boundary. */
void v9x_eg_pm4_pad(struct v9x_eg_pm4 *stream);

/*
 * evergreen_gpu_init's register writes for Cedar, in order: each register
 * is written (value read & keep) | set, where keep is zero for a plain
 * write. The CP must be halted. GB_BACKEND_MAP is 0 (one render backend);
 * DMIF_, HDP_ and DMA_TILING_CONFIG keep the BIOS's values, which the
 * display, the host path and the running DMA engine use.
 */
struct v9x_eg_reg_update {
    v9x_u32 offset;
    v9x_u32 keep;
    v9x_u32 set;
};
#define V9X_EG_CEDAR_GPU_INIT_COUNT 45u
extern const struct v9x_eg_reg_update
    v9x_eg_cedar_gpu_init[V9X_EG_CEDAR_GPU_INIT_COUNT];

/* ME_INITIALIZE for four hardware contexts: 7 dwords. */
#define V9X_EG_ME_INITIALIZE_DWORDS 7ul
void v9x_eg_3d_me_initialize(struct v9x_eg_pm4 *stream);
/* evergreen_cp_start after ME_INITIALIZE: the clear-state preamble. */
void v9x_eg_3d_preamble(struct v9x_eg_pm4 *stream);
/* Linux 3.9's set_default_state for Cedar, the default context state
 * inline: CLEAR_STATE, the SQ resource split, CONTEXT_CONTROL, sampler 0
 * and Evergreen's default context. */
void v9x_eg_3d_default_state(struct v9x_eg_pm4 *stream);

/*
 * One batch's state. GPU addresses are 256-byte aligned; pitches are in
 * elements and multiples of 8; the scissor's right and bottom are
 * exclusive. Zero depth_gpu draws without a depth buffer; zero
 * texture_gpu without a texture. The vertex buffer is resource 0 of the
 * fetch shader slots.
 */
struct v9x_eg_3d_state {
    v9x_u32 target_gpu;
    v9x_u32 target_pitch;
    v9x_u32 target_height;
    v9x_u32 target_format;       /* V9X_EG_COLOR_5_6_5 or _8_8_8_8 */
    v9x_u32 scissor_left;
    v9x_u32 scissor_top;
    v9x_u32 scissor_right;
    v9x_u32 scissor_bottom;
    v9x_u32 depth_gpu;           /* Z16, 1D tiled */
    v9x_u32 depth_pitch;
    v9x_u32 depth_height;        /* a multiple of 8 */
    v9x_u32 depth_control;       /* v9x_eg_3d_depth_control */
    v9x_u32 blend_control;       /* v9x_eg_3d_blend */
    v9x_u32 alpha_test_control;  /* v9x_eg_3d_alpha_test */
    v9x_u32 alpha_ref;           /* a float's bits */
    v9x_u32 perspective;         /* nonzero: rhw in w, perspective-correct */
    v9x_u32 pixel_centre_half;   /* nonzero: centres at +0.5 */
    v9x_u32 vs_gpu;
    v9x_u32 vs_gprs;
    v9x_u32 ps_gpu;
    v9x_u32 ps_gprs;
    v9x_u32 interpolants;        /* 1 or 2 */
    v9x_u32 texture_gpu;
    v9x_u32 texture_width;
    v9x_u32 texture_height;
    v9x_u32 texture_pitch;
    v9x_u32 texture_format;      /* V9X_EG_COLOR_5_6_5 or _8_8_8_8 */
    v9x_u32 sampler_word0;       /* v9x_eg_3d_sampler */
    v9x_u32 vertex_gpu;
    v9x_u32 vertex_stride;
    v9x_u32 vertex_bytes;
};

/* Register words. func is V9X_EG_FUNC_*; factors V9X_EG_BLEND_*. */
v9x_u32 v9x_eg_3d_depth_control(v9x_u32 enable, v9x_u32 write,
                                v9x_u32 func);
v9x_u32 v9x_eg_3d_blend(v9x_u32 enable, v9x_u32 src, v9x_u32 dst);
v9x_u32 v9x_eg_3d_alpha_test(v9x_u32 enable, v9x_u32 func);
v9x_u32 v9x_eg_3d_sampler(v9x_u32 bilinear, v9x_u32 clamp_u,
                          v9x_u32 clamp_v);

/* The batch's state after v9x_eg_3d_default_state. INVALID_ARGUMENT for a
 * state the hardware cannot take as given, with nothing written. */
v9x_status v9x_eg_3d_build_state(const struct v9x_eg_3d_state *state,
                                 struct v9x_eg_pm4 *stream);
/* A triangle list of vertex_count vertices from the bound buffer. */
void v9x_eg_3d_draw(struct v9x_eg_pm4 *stream, v9x_u32 vertex_count);
/* The colour and depth caches flushed to memory, and the texture cache
 * invalidated, over all of it. */
void v9x_eg_3d_flush(struct v9x_eg_pm4 *stream);
/* SCRATCH_REG0 written through the ring once what precedes it is done. */
void v9x_eg_3d_fence(struct v9x_eg_pm4 *stream, v9x_u32 value);

#endif
