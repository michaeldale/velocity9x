/*
 * AMD Carrizo (GFX8) PM4 streams for the graphics CP: the start preamble
 * with the full clear state, and a solid rectangle through the 3D engine.
 *
 * Pure policy, no I/O. Packets are Linux amdgpu vid.h's PACKET3 (type 31:30,
 * count 29:16, opcode 15:8); register offsets gfx_8_0_d.h dword indices;
 * the sequences gfx_v8_0.c's cp_gfx_start and ring_emit_fence_gfx
 * (build\external\carrizo\). Nothing here has drawn on the chip until a
 * record says so.
 */
#ifndef VELOCITY9X_VI_3D_H
#define VELOCITY9X_VI_3D_H

#include "velocity9x/status.h"

#define V9X_VI_PACKET3(op, n) \
    ((3ul << 30) | (((v9x_u32)(n) & 0x3ffful) << 16) | \
     (((v9x_u32)(op) & 0xfful) << 8))
#define V9X_VI_P3_NOP               0x10ul
#define V9X_VI_P3_SET_BASE          0x11ul
#define V9X_VI_P3_CLEAR_STATE       0x12ul
#define V9X_VI_P3_CONTEXT_CONTROL   0x28ul
#define V9X_VI_P3_DRAW_INDEX_AUTO   0x2dul
#define V9X_VI_P3_NUM_INSTANCES     0x2ful
#define V9X_VI_P3_SURFACE_SYNC      0x43ul
#define V9X_VI_P3_EVENT_WRITE_EOP   0x47ul
#define V9X_VI_P3_PREAMBLE_CNTL     0x4aul
#define V9X_VI_P3_SET_CONTEXT_REG   0x69ul
#define V9X_VI_P3_SET_SH_REG        0x76ul
#define V9X_VI_P3_SET_UCONFIG_REG   0x79ul
#define V9X_VI_CONTEXT_REG_START    0xa000ul
#define V9X_VI_SH_REG_START         0x2c00ul
#define V9X_VI_UCONFIG_REG_START    0xc000ul

/* v4.9's gfx ring NOP, PACKET3(NOP, 3FFFh): one dword. */
#define V9X_VI_RING_NOP             V9X_VI_PACKET3(V9X_VI_P3_NOP, 0x3ffful)
#define V9X_VI_RING_ALIGN_DWORDS    16ul

/* gfx_v8_0_get_csb_size for vi_cs_data, plus cp_gfx_start's CE partitions:
 * the start stream's length. */
#define V9X_VI_START_DWORDS         (2ul + 3ul + 7ul * 2ul + 885ul + 4ul + \
                                     2ul + 2ul + 4ul)

struct v9x_vi_pm4 {
    v9x_u32 *dwords;
    v9x_u32 count;
    v9x_u32 capacity;
    v9x_u32 overflow;
};

void v9x_vi_pm4_init(struct v9x_vi_pm4 *stream, v9x_u32 *dwords,
                     v9x_u32 capacity);
void v9x_vi_pm4_put(struct v9x_vi_pm4 *stream, v9x_u32 value);
/* One SET_*_REG run of count registers starting at a dword index. */
void v9x_vi_pm4_set_context(struct v9x_vi_pm4 *stream, v9x_u32 reg,
                            const v9x_u32 *values, v9x_u32 count);
void v9x_vi_pm4_set_sh(struct v9x_vi_pm4 *stream, v9x_u32 reg,
                       const v9x_u32 *values, v9x_u32 count);
void v9x_vi_pm4_set_uconfig(struct v9x_vi_pm4 *stream, v9x_u32 reg,
                            v9x_u32 value);
/* NOPs to the ring's 16-dword boundary. */
void v9x_vi_pm4_pad(struct v9x_vi_pm4 *stream);

/* cp_gfx_start's stream: the clear-state preamble with vi_cs_data and
 * Carrizo's PA_SC_RASTER_CONFIG, CLEAR_STATE, and the CE partitions. */
void v9x_vi_3d_start(struct v9x_vi_pm4 *stream);

/*
 * A solid rectangle into a linear colour buffer, then an end-of-pipe
 * event that flushes the colour caches and writes fence_value to the
 * fence once the draw is done. Addresses are 40-bit GPU addresses as
 * bits 39:32 (high) and 31:0 (low); shaders and the target 256-byte
 * aligned. The colour is four floats' bits, R, G, B, A.
 */
struct v9x_vi_solid_rect {
    v9x_u32 target_low;
    v9x_u32 target_high;
    v9x_u32 pitch_pixels;       /* a multiple of 8 */
    v9x_u32 height;
    v9x_u32 format;             /* V9X_VI_CB_FORMAT_* */
    v9x_u32 tile_mode_index;    /* GB_TILE_MODE index: 8 is linear-aligned */
    v9x_u32 vs_low;
    v9x_u32 ps_low;
    v9x_u32 shader_high;
    v9x_u32 left;
    v9x_u32 top;
    v9x_u32 right;
    v9x_u32 bottom;
    v9x_u32 color[4];
    v9x_u32 fence_low;
    v9x_u32 fence_high;
    v9x_u32 fence_value;
    /* For the probe shaders (vi_shader.h): each stage's marker address,
     * dword-aligned, as its user SGPRs s4 and s5. 0 for the solid ones. */
    v9x_u32 probe_vs_low;
    v9x_u32 probe_ps_low;
    v9x_u32 probe_high;
};

/* gfx_8_0_enum.h ColorFormat. */
#define V9X_VI_CB_FORMAT_8_8_8_8    0x0aul
#define V9X_VI_CB_FORMAT_5_6_5      0x10ul
#define V9X_VI_TILE_LINEAR_ALIGNED  8ul

/* V9X_STATUS_OK with the packets appended; INVALID_ARGUMENT for a rectangle
 * outside the target, a pitch not a multiple of 8, or an unaligned
 * address. */
v9x_status v9x_vi_3d_solid_rect(struct v9x_vi_pm4 *stream,
                                const struct v9x_vi_solid_rect *rect);

#endif
