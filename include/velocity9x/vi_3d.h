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

/*
 * A triangle list of Direct3D TL vertices (vi_shader.h's TL VS, 32 bytes
 * each) from a buffer at vertex_low/high, Gouraud-shaded by the PS at
 * ps_low: the same target, screen-space and depth-off state as the
 * rectangle, the cache sync first and the fence last.
 */
struct v9x_vi_tl_draw {
    v9x_u32 target_low;
    v9x_u32 target_high;
    v9x_u32 pitch_pixels;       /* a multiple of 8 */
    v9x_u32 height;
    v9x_u32 format;             /* V9X_VI_CB_FORMAT_* */
    v9x_u32 tile_mode_index;
    v9x_u32 vs_low;
    v9x_u32 ps_low;
    v9x_u32 shader_high;
    v9x_u32 vertex_low;         /* dword-aligned */
    v9x_u32 vertex_high;
    v9x_u32 vertex_count;       /* a multiple of 3 */
    v9x_u32 fence_low;
    v9x_u32 fence_high;
    v9x_u32 fence_value;
    /* A Z16 depth buffer the target's size in tile mode
     * V9X_VI_TILE_DEPTH_1D, 256-byte aligned; 0 for none. */
    v9x_u32 depth_low;
    v9x_u32 depth_high;
    v9x_u32 depth_func;         /* V9X_VI_ZFUNC_* */
    v9x_u32 depth_write;        /* nonzero to write */
    /* An A8R8G8B8 texture, 256-byte aligned, for vi_shader.h's textured
     * pixel shaders: its T# and a clamping S# become the PS's twelve user
     * SGPRs. 0 for none, and the PS then has no user SGPRs. */
    v9x_u32 texture_low;
    v9x_u32 texture_high;
    v9x_u32 texture_width;      /* 1..16384 */
    v9x_u32 texture_height;     /* 1..16384 */
    v9x_u32 texture_pitch;      /* texels, at least the width */
    v9x_u32 texture_tile_mode_index;
    v9x_u32 texture_bilinear;   /* nonzero for bilinear, else point */
    v9x_u32 ps_vgprs;           /* the PS's VGPRs; 0 for eight */
    /* APPENDED for the HAL (d3d_vi_map.c); zero keeps the probes'
     * meaning. */
    v9x_u32 texture_format;     /* V9X_VI_IMG_FORMAT_*; 0 for 8_8_8_8 */
    v9x_u32 target_mask;        /* CB_TARGET_MASK RGBA bits; 0 for all */
    v9x_u32 d3d_pixel_centres;  /* nonzero: centres on whole coordinates */
    v9x_u32 depth_pitch;        /* pixels, a multiple of 8; 0: the target's */
    v9x_u32 depth_height;       /* rows, a multiple of 8; 0: the target's */
    v9x_u32 texture_address;    /* V9X_VI_TEX_ADDRESS_*, both axes */
    v9x_u32 blend_control;      /* CB_BLEND0_CONTROL; 0 is no blending */
    v9x_u32 ps_kill;            /* nonzero: the PS kills (KILL_ENABLE) */
    v9x_u32 target_mask_none;   /* nonzero: no colour channel written */
    /* A mip chain: levels base..last of the texture (0..15, 0 and 0 for
     * one level), and the filter between them, V9X_VI_MIP_FILTER_*. */
    v9x_u32 texture_base_level;
    v9x_u32 texture_last_level;
    v9x_u32 texture_mip_filter;
    /* Nonzero: the texel's W is the constant 1, for a format whose fourth
     * byte is not alpha (XRGB8888). */
    v9x_u32 texture_opaque;
    /* Nonzero: the depth buffer linear-aligned through tile index
     * V9X_VI_TILE_DEPTH_LINEAR, rows of the depth pitch; GB_TILE_MODE7
     * must hold V9X_VI_TILE_MODE_LINEAR_ALIGNED. */
    v9x_u32 depth_linear;
};

/* The S#'s MIP_FILTER (SQ_TEX_MIP_FILTER_*). */
#define V9X_VI_MIP_FILTER_NONE      0ul
#define V9X_VI_MIP_FILTER_POINT     1ul
#define V9X_VI_MIP_FILTER_LINEAR    2ul

/*
 * Where a linear-aligned chain's levels sit, as the sampler reads them:
 * each level's rows a multiple of 64 bytes and widened 64 bytes at a time
 * until the level is a whole number of 256-byte blocks, the levels end to
 * end from offset 0. Measured on M-XP7053 (V9XCZ3D /mip:0-6, boot 27): a
 * 64x16 A8R8G8B8 chain at 0, 4096, 5120, 5376, 5632, 5888, 6144, rows of
 * 256, 128, 64 and 128 bytes (docs/decisions/
 * 2026-10-28-carrizo-d3d-blend-and-shader-fixed-function.md). Power-of-two
 * sides only: POW2_PAD's padding of other sizes was not measured. Per
 * level, offsets[] in bytes and pitches[] in texels; nonzero on success.
 */
#define V9X_VI_MIP_LEVELS_MAX       12ul
int v9x_vi_3d_mip_layout(v9x_u32 bytes_per_texel, v9x_u32 width,
                         v9x_u32 height, v9x_u32 levels, v9x_u32 *offsets,
                         v9x_u32 *pitches, v9x_u32 *total_bytes);

/* CB_BLEND0_CONTROL's factors (Mesa's sid.h V_028780_BLEND_*), and
 * v9x_vi_3d_blend's word: the colour and alpha alike, DST_PLUS_SRC,
 * ENABLE. */
#define V9X_VI_BLEND_ZERO                 0x00ul
#define V9X_VI_BLEND_ONE                  0x01ul
#define V9X_VI_BLEND_SRC_COLOR            0x02ul
#define V9X_VI_BLEND_ONE_MINUS_SRC_COLOR  0x03ul
#define V9X_VI_BLEND_SRC_ALPHA            0x04ul
#define V9X_VI_BLEND_ONE_MINUS_SRC_ALPHA  0x05ul
#define V9X_VI_BLEND_DST_COLOR            0x08ul
#define V9X_VI_BLEND_ONE_MINUS_DST_COLOR  0x09ul
#define V9X_VI_BLEND_SRC_ALPHA_SATURATE   0x0aul
v9x_u32 v9x_vi_3d_blend(v9x_u32 source, v9x_u32 destination);

/* The S#'s CLAMP_X/Y/Z: clamp to the last texel (the default), wrap or
 * mirror (SQ_TEX_CLAMP_LAST_TEXEL 2, SQ_TEX_WRAP 0, SQ_TEX_MIRROR 1). */
#define V9X_VI_TEX_ADDRESS_CLAMP    0ul
#define V9X_VI_TEX_ADDRESS_WRAP     1ul
#define V9X_VI_TEX_ADDRESS_MIRROR   2ul

/* The T# data formats the builder describes (Mesa's sid.h
 * IMG_DATA_FORMAT_*), each sampled with its components in memory order:
 * A8R8G8B8's B, G, R, A and RGB565's B, G, R as X, Y, Z (W reads 1). */
#define V9X_VI_IMG_FORMAT_8_8_8_8   0x0aul
#define V9X_VI_IMG_FORMAT_5_6_5     0x10ul
/* ARGB1555 and ARGB4444 the same way: Mesa takes B5G5R5A1 and B4G4R4A4
 * with x the low field, so X, Y, Z, W are B, G, R, A in memory order. */
#define V9X_VI_IMG_FORMAT_1_5_5_5   0x11ul
#define V9X_VI_IMG_FORMAT_4_4_4_4   0x13ul

/* DB_DEPTH_CONTROL ZFUNC, gfx_8_0_enum.h's FRAG_*: Direct3D's D3DCMPFUNC
 * less one. */
#define V9X_VI_ZFUNC_NEVER      0ul
#define V9X_VI_ZFUNC_LESS       1ul
#define V9X_VI_ZFUNC_EQUAL      2ul
#define V9X_VI_ZFUNC_LEQUAL     3ul
#define V9X_VI_ZFUNC_GREATER    4ul
#define V9X_VI_ZFUNC_NOTEQUAL   5ul
#define V9X_VI_ZFUNC_GEQUAL     6ul
#define V9X_VI_ZFUNC_ALWAYS     7ul
/* Carrizo's GB_TILE_MODE5 (Linux's table): ARRAY_1D_TILED_THIN1, P2,
 * depth micro-tiling, tile split 5. Each 8x8 tile is contiguous, the
 * tiles in rows. */
#define V9X_VI_TILE_DEPTH_1D    5ul
/* DB_Z_INFO's TILE_MODE_INDEX is three bits, and Linux's Carrizo table
 * holds no linear mode in 0-7; it leaves 7 unwritten. 7 written as
 * ARRAY_LINEAR_ALIGNED, P2 (GB_TILE_MODE8's value) names a linear
 * depth buffer. */
#define V9X_VI_TILE_DEPTH_LINEAR        7ul
#define V9X_VI_TILE_MODE_LINEAR_ALIGNED 0x00000004ul

/* V9X_STATUS_OK with the packets appended; INVALID_ARGUMENT for a bad
 * target, an unaligned address, a vertex count not whole triangles or a
 * depth function past ALWAYS. */
v9x_status v9x_vi_3d_tl_draw(struct v9x_vi_pm4 *stream,
                             const struct v9x_vi_tl_draw *draw);

#endif
