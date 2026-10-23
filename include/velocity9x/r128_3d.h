/*
 * ATI Rage 128 3D engine: CCE packet streams for state and triangles.
 *
 * Pure policy, no I/O. The caller feeds the dwords these builders return to
 * the CCE in order: in PIO mode, as pairs to PM4_FIFO_DATA_EVEN then _ODD
 * (docs\decisions\2026-10-22-rage128-cce-pio-runs-packets-and-a-triangle.md),
 * padded to a whole pair with V9X_R128_PACKET2.
 *
 * Register offsets and fields are xf86-video-r128's r128_reg.h. The state
 * follows Mesa 7.11's r128 driver (r128_state.c r128DDInitState and
 * r128_texstate.c) and the Linux DRM's context emit (r128_state.c
 * r128_emit_context), which writes the context block from
 * DST_PITCH_OFFSET_C before any primitive. The 3D scissor in that block is
 * what POST leaves at zero, so a stream without it draws nothing.
 *
 * The vertex is Direct3D's TL vertex as the CCE takes it with every format
 * bit set (RHW, DIFFUSE_ARGB, SPEC_FRGB, S_T): X, Y, Z (0..1), RHW as
 * floats, diffuse ARGB, specular RGB with fog in its alpha byte, then S and
 * T as floats, which the setup engine multiplies by W itself
 * (TEXTURE_ST_MULT_W). V9X_R3D_VERTEX has that layout.
 */
#ifndef VELOCITY9X_R128_3D_H
#define VELOCITY9X_R128_3D_H

#include "velocity9x/status.h"

/* The CCE microcode, src\chipsets\ati\rage128\r128_cce_microcode.c: 256
 * instructions, high dword then low, for PM4_MICROCODE_DATAH/DATAL after
 * PM4_MICROCODE_ADDR = 0. MIT, AMD 2000, notice in that file. */
#define V9X_R128_CCE_MICROCODE_DWORDS   512u
extern const v9x_u32 v9x_r128_cce_microcode[V9X_R128_CCE_MICROCODE_DWORDS];

/* Packet headers (r128_reg.h R128_CCE_PACKET*; the DRM's CCE_PACKET0 and
 * CCE_PACKET3 macros): n is the dwords after the header, less one. */
#define V9X_R128_PACKET0(reg, n)    (((v9x_u32)(n) << 16) | ((v9x_u32)(reg) >> 2))
#define V9X_R128_PACKET2            0x80000000ul
/* WAIT_UNTIL: EVENT_CRTC_OFFSET stalls the command FIFO until
 * CRTC_GUI_TRIG_OFFSET is 0, the display showing the last CRTC_OFFSET
 * written; no stall when nothing is pending (RRG-G04500 p. 3-238). */
#define V9X_R128_WAIT_UNTIL         0x1720ul
#define V9X_R128_EVENT_CRTC_OFFSET  0x00000001ul
#define V9X_R128_WAIT_DWORDS        2u
#define V9X_R128_PACKET3(op, n)     ((v9x_u32)(op) | ((v9x_u32)(n) << 16))
#define V9X_R128_PACKET3_3D_RNDR_GEN_PRIM 0xc0002500ul
/* The count field of a packet header, bits 29:16. */
#define V9X_R128_PACKET_COUNT_MAX   0x3ffful

/* Registers the streams write. */
#define V9X_R128_PM4_VC_FPU_SETUP       0x071cul
#define V9X_R128_SCALE_3D_CNTL_REG      0x1a00ul
#define V9X_R128_SETUP_CNTL             0x1bc4ul
#define V9X_R128_WINDOW_XY_OFFSET       0x1bccul
#define V9X_R128_DST_PITCH_OFFSET_C     0x1c80ul
#define V9X_R128_TEX_CNTL_C             0x1c9cul
#define V9X_R128_PRIM_TEX_CNTL_C        0x1cb0ul
#define V9X_R128_CONSTANT_COLOR_C       0x1d34ul
#define V9X_R128_STEN_REF_MASK_C        0x1d40ul
#define V9X_R128_PLANE_3D_MASK_C        0x1d44ul

/* The context block, DST_PITCH_OFFSET_C to FOG_COLOR_C: 12 registers. */
#define V9X_R128_CONTEXT_REGS           12u
/* PRIM_TEX_CNTL_C, PRIM_TEXTURE_COMBINE_CNTL_C, TEX_SIZE_PITCH_C and the
 * eleven level offsets PRIM_TEX_0_OFFSET_C to _10: 14 registers. */
#define V9X_R128_TEXTURE_REGS           14u
#define V9X_R128_TEXTURE_LEVELS         11u

/* Z_STEN_CNTL_C and SCALE_3D_CNTL compare codes, shared by the Z test
 * (6:4) and the alpha test (26:24): never, less, lessequal, equal,
 * greaterequal, greater, notequal, always. */
#define V9X_R128_CMP_NEVER              0ul
#define V9X_R128_CMP_LESS               1ul
#define V9X_R128_CMP_LESSEQUAL          2ul
#define V9X_R128_CMP_EQUAL              3ul
#define V9X_R128_CMP_GREATEREQUAL       4ul
#define V9X_R128_CMP_GREATER            5ul
#define V9X_R128_CMP_NOTEQUAL           6ul
#define V9X_R128_CMP_ALWAYS             7ul

/* Blend factors (R128_ALPHA_BLEND_*). */
#define V9X_R128_BLEND_ZERO             0ul
#define V9X_R128_BLEND_ONE              1ul
#define V9X_R128_BLEND_SRCCOLOR         2ul
#define V9X_R128_BLEND_INVSRCCOLOR      3ul
#define V9X_R128_BLEND_SRCALPHA         4ul
#define V9X_R128_BLEND_INVSRCALPHA      5ul
#define V9X_R128_BLEND_DSTALPHA         6ul
#define V9X_R128_BLEND_INVDSTALPHA      7ul
#define V9X_R128_BLEND_DSTCOLOR         8ul
#define V9X_R128_BLEND_INVDSTCOLOR      9ul
#define V9X_R128_BLEND_SAT              10ul

/* Texture datatypes (PRIM_TEX_CNTL_C 19:16). */
#define V9X_R128_TEX_ARGB1555           3ul
#define V9X_R128_TEX_RGB565             4ul
#define V9X_R128_TEX_ARGB8888           6ul
#define V9X_R128_TEX_ARGB4444           15ul

/* Filters: min (PRIM_TEX_CNTL_C 3:1) and mag (6:4). */
#define V9X_R128_FILTER_NEAREST         0ul
#define V9X_R128_FILTER_LINEAR          1ul
#define V9X_R128_FILTER_MIPNEAREST      2ul
#define V9X_R128_FILTER_MIPLINEAR       3ul
#define V9X_R128_FILTER_LINEARMIPNEAREST 4ul
#define V9X_R128_FILTER_LINEARMIPLINEAR 5ul

/* Texture combine, as Mesa's r128UpdateTextureEnv builds it for unit 0. */
#define V9X_R128_COMBINE_REPLACE        0ul  /* C = Ct, A = At */
#define V9X_R128_COMBINE_MODULATE       1ul  /* C = CfCt, A = AfAt */
#define V9X_R128_COMBINE_DECAL          2ul  /* C = Ct, A = Af */
#define V9X_R128_COMBINE_MODULATE_RGB   3ul  /* C = CfCt, A = Af */
#define V9X_R128_COMBINE_DECAL_ALPHA    4ul  /* C = Cf(1-At)+CtAt, A = Af */
#define V9X_R128_COMBINE_MODULATE_TEXALPHA 5ul /* C = CfCt, A = At */

/* Primitive packet: header, vertex format, control. */
#define V9X_R128_PRIM_HEADER_DWORDS     3u
#define V9X_R128_VERTEX_DWORDS          8u
/* VC_CNTL's count, 31:16; kept well inside one packet's count field. */
#define V9X_R128_PRIM_TRIANGLES_MAX     64u

struct v9x_r128_3d_texture {
    v9x_u32 enable;
    v9x_u32 datatype;          /* V9X_R128_TEX_* */
    v9x_u32 width_log2;
    v9x_u32 height_log2;
    v9x_u32 levels;            /* 1 is no mip chain */
    v9x_u32 offsets[V9X_R128_TEXTURE_LEVELS];  /* bytes in video memory */
    v9x_u32 min_filter;        /* V9X_R128_FILTER_* */
    v9x_u32 mag_filter;        /* NEAREST or LINEAR */
    v9x_u32 clamp_s;           /* non-zero clamps, zero wraps */
    v9x_u32 clamp_t;
    v9x_u32 combine;           /* V9X_R128_COMBINE_* */
};

struct v9x_r128_3d_state {
    v9x_u32 vram_bytes;
    v9x_u32 target_offset;     /* bytes in video memory */
    v9x_u32 target_pitch_bytes;
    v9x_u32 target_bytes_per_pixel;  /* 2 or 4 */
    v9x_u32 depth_enable;      /* test */
    v9x_u32 depth_write;
    v9x_u32 depth_func;        /* V9X_R128_CMP_* */
    v9x_u32 depth_offset;
    v9x_u32 depth_pitch_bytes;
    v9x_u32 depth_bytes_per_pixel;   /* 2 */
    v9x_u32 blend_enable;
    v9x_u32 src_blend;         /* V9X_R128_BLEND_* */
    v9x_u32 dst_blend;
    v9x_u32 alpha_test_enable;
    v9x_u32 alpha_func;        /* V9X_R128_CMP_* */
    v9x_u32 alpha_ref;         /* 0-255 */
    v9x_u32 flat_shade;        /* non-zero: colour of the first vertex */
    v9x_u32 specular_enable;
    v9x_u32 fog_enable;
    v9x_u32 fog_color;         /* RGB */
    v9x_u32 dither_enable;
    /* The render interface's scissor, half-open, inside 8192; zero
     * scissor_enable opens it to the maxima. */
    v9x_u32 scissor_enable;
    v9x_u32 scissor_left;
    v9x_u32 scissor_top;
    v9x_u32 scissor_right;
    v9x_u32 scissor_bottom;
    v9x_u32 masked_channels;   /* V9X_R128_CHANNEL_*: kept, not written */
    struct v9x_r128_3d_texture texture;
};

/* Colour channels a draw leaves as they are, through PLANE_3D_MASK_C. */
#define V9X_R128_CHANNEL_RED            1ul
#define V9X_R128_CHANNEL_GREEN          2ul
#define V9X_R128_CHANNEL_BLUE           4ul

/*
 * The 3D engine writes through the GUI pixel cache, which PC_NGUI_CTLSTAT's
 * flush does not reach: without PC_GUI_CTLSTAT's flush its last lines stay
 * cached, a CPU read sees the memory before them, and they land later over
 * whatever the CPU wrote since (A8U4I5, 2026-10-23: each probe draw showed
 * the one before it). The Linux DRM flushes it through the stream, with
 * PC_RI_GUI as well when the CPU has written memory the engine will read
 * (r128_state.c, the texture blit).
 */
#define V9X_R128_PC_GUI_CTLSTAT         0x1748ul
#define V9X_R128_PC_FLUSH_GUI           0x00000003ul
#define V9X_R128_PC_RI_GUI              0x00000004ul
#define V9X_R128_FLUSH_DWORDS           2u

/* The state stream's length: the cache flush, the context block, the setup
 * registers, and the texture block. */
#define V9X_R128_STATE_DWORDS_MAX       50u

struct v9x_r128_3d_stream {
    v9x_u32 dwords[V9X_R128_STATE_DWORDS_MAX];
    v9x_u32 count;
};

/*
 * The whole state for one draw, as packets, after a GUI pixel-cache flush
 * that also invalidates its reads, since the CPU may have written the target
 * or a texture since the last batch. V9X_STATUS_OK with the stream;
 * INVALID_ARGUMENT for a surface outside memory or a misaligned one;
 * UNSUPPORTED for a pixel size, pitch, texture shape or code the engine
 * cannot express. Every batch carries its whole state: nothing here assumes
 * a register kept its value between batches.
 */
v9x_status v9x_r128_3d_build_state(const struct v9x_r128_3d_state *state,
                                   struct v9x_r128_3d_stream *stream);

/* The end of a batch: flush the GUI pixel cache, so that once the CCE and
 * the cache report idle the pixels are in memory for the CPU. */
void v9x_r128_3d_build_flush(v9x_u32 out[V9X_R128_FLUSH_DWORDS]);

/*
 * The three header dwords of a 3D_RNDR_GEN_PRIM triangle list of
 * triangle_count full TL vertices (V9X_R128_VERTEX_DWORDS each), which
 * follow it as they are. INVALID_ARGUMENT for none or more than
 * V9X_R128_PRIM_TRIANGLES_MAX.
 */
v9x_status v9x_r128_3d_build_prim(v9x_u32 triangle_count,
                                  v9x_u32 header[V9X_R128_PRIM_HEADER_DWORDS]);

#endif
