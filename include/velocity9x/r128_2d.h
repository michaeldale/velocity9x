/*
 * ATI Rage 128 2D engine: register values for solid fill and screen copy.
 *
 * Pure policy, no I/O. The caller writes what these builders return, every
 * dword in order, to the register BAR (BAR2) + offset, with the engine in
 * NONPM4 (PM4_BUFFER_CNTL mode 0), which is how the card is left after POST
 * (docs\decisions\2026-10-22-rage128-register-probe.md). The last write of a
 * fill or copy is the size register, whose write starts the operation.
 *
 * Offsets and fields are xf86-video-r128's r128_reg.h; the sequences are its
 * r128_accel.c (EngineInit, solid fill, screen-to-screen copy) and
 * r128_exa.c (per-operation pitch/offset). Rage 128 PRO Register Reference
 * Guide (RRG-G04500) chapter 3 describes the same registers.
 */
#ifndef VELOCITY9X_R128_2D_H
#define VELOCITY9X_R128_2D_H

#include "velocity9x/status.h"

/* Non-GUI registers. */
#define V9X_R128_PM4_BUFFER_CNTL        0x0704ul
#define V9X_R128_PC_NGUI_CTLSTAT        0x0184ul
#define V9X_R128_CONFIG_MEMSIZE         0x00f8ul

/* 2D engine registers (FIFOed). */
#define V9X_R128_SRC_PITCH_OFFSET       0x1428ul
#define V9X_R128_DST_PITCH_OFFSET       0x142cul
#define V9X_R128_SRC_Y_X                0x1434ul
#define V9X_R128_DST_Y_X                0x1438ul
#define V9X_R128_DST_HEIGHT_WIDTH       0x143cul
#define V9X_R128_DP_GUI_MASTER_CNTL     0x146cul
#define V9X_R128_DP_BRUSH_FRGD_CLR      0x147cul
#define V9X_R128_DST_WIDTH_HEIGHT       0x1598ul
#define V9X_R128_AUX_SC_CNTL            0x1660ul
#define V9X_R128_DP_CNTL                0x16c0ul
#define V9X_R128_DP_DATATYPE            0x16c4ul
#define V9X_R128_DP_WRITE_MASK          0x16ccul
#define V9X_R128_DEFAULT_OFFSET         0x16e0ul
#define V9X_R128_DEFAULT_PITCH          0x16e4ul
#define V9X_R128_DEFAULT_SC_BOTTOM_RIGHT 0x16e8ul
#define V9X_R128_SC_TOP_LEFT            0x16ecul
#define V9X_R128_SC_BOTTOM_RIGHT        0x16f0ul
#define V9X_R128_GUI_STAT               0x1740ul
#define V9X_R128_SCALE_3D_CNTL          0x1a00ul

/* DP_GUI_MASTER_CNTL fields. */
#define V9X_R128_GMC_SRC_PITCH_OFFSET_CNTL 0x00000001ul
#define V9X_R128_GMC_DST_PITCH_OFFSET_CNTL 0x00000002ul
#define V9X_R128_GMC_BRUSH_SOLID_COLOR  0x000000d0ul  /* 13 << 4 */
#define V9X_R128_GMC_BRUSH_NONE         0x000000f0ul  /* 15 << 4 */
#define V9X_R128_GMC_DST_DATATYPE_SHIFT 8
#define V9X_R128_GMC_SRC_DATATYPE_COLOR 0x00003000ul  /* 3 << 12 */
#define V9X_R128_GMC_ROP3_SHIFT         16
#define V9X_R128_DP_SRC_SOURCE_MEMORY   0x02000000ul  /* 2 << 24 */
#define V9X_R128_GMC_CLR_CMP_CNTL_DIS   0x10000000ul
#define V9X_R128_GMC_AUX_CLIP_DIS       0x20000000ul

/* ROP3 codes: pattern copy for fills, source copy for copies. */
#define V9X_R128_ROP3_P                 0xf0ul
#define V9X_R128_ROP3_S                 0xccul

/* DP_CNTL direction bits. */
#define V9X_R128_DST_X_LEFT_TO_RIGHT    0x00000001ul
#define V9X_R128_DST_Y_TOP_TO_BOTTOM    0x00000002ul

/* DP_DATATYPE bit 29: host data big-endian, cleared on a PC. */
#define V9X_R128_HOST_BIG_ENDIAN_EN     0x20000000ul

/* Destination datatypes (GMC 11:8): 8 bpp CI, 16 bpp, 32 bpp. 15 bpp (3)
 * is the same 16 bits to a fill or copy; 24 bpp (5) is not offered. */
#define V9X_R128_DATATYPE_8BPP          2ul
#define V9X_R128_DATATYPE_16BPP         4ul
#define V9X_R128_DATATYPE_32BPP         6ul

/* *_PITCH_OFFSET: pitch in 8-pixel units in 31:21, offset in 32-byte
 * units in 20:0 (r128_exa.c). */
#define V9X_R128_PITCH_SHIFT            21
#define V9X_R128_PITCH_UNITS_MAX        0x7fful
#define V9X_R128_PITCH_PIXEL_UNIT       8ul
#define V9X_R128_OFFSET_ALIGN           32ul
#define V9X_R128_OFFSET_FIELD_MAX       0x001ffffful

/* Coordinates, widths and heights, and the scissor's maxima: 13 bits
 * (R128_DEFAULT_SC_RIGHT_MAX / BOTTOM_MAX, 1FFFh). */
#define V9X_R128_COORD_MAX              0x1ffful
#define V9X_R128_SC_MAX                 0x1fff1ffful

/* GUI_STAT: free FIFO entries in 11:0 (64 slots), engine active in 31. */
#define V9X_R128_GUI_FIFO_MASK          0x00000ffful
#define V9X_R128_GUI_FIFO_SLOTS         64ul
#define V9X_R128_GUI_ACTIVE             0x80000000ul

/* PC_NGUI_CTLSTAT: flush everything in 7:0, busy in 31. */
#define V9X_R128_PC_FLUSH_ALL           0x000000fful
#define V9X_R128_PC_BUSY                0x80000000ul

/* PM4_BUFFER_CNTL mode, 31:28; 0 is NONPM4, direct register access. */
#define V9X_R128_PM4_MODE_SHIFT         28
#define V9X_R128_PM4_MODE_MASK          0xful

/* The init list writes 9 registers; a fill 6; a copy 7. */
#define V9X_R128_BLT_WRITES             12u

struct v9x_r128_blt {
    v9x_u32 offsets[V9X_R128_BLT_WRITES];
    v9x_u32 values[V9X_R128_BLT_WRITES];
    v9x_u32 count;
};

struct v9x_r128_fill {
    v9x_u32 vram_bytes;
    v9x_u32 target_offset;
    v9x_u32 pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 left;
    v9x_u32 top;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 color;
};

struct v9x_r128_copy {
    v9x_u32 vram_bytes;
    v9x_u32 source_offset;
    v9x_u32 source_pitch_bytes;
    v9x_u32 destination_offset;
    v9x_u32 destination_pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 source_left;
    v9x_u32 source_top;
    v9x_u32 destination_left;
    v9x_u32 destination_top;
    v9x_u32 width;
    v9x_u32 height;
};

/*
 * The engine state every operation relies on and does not write itself:
 * the scissors opened to their maxima (POST leaves the default scissor at
 * the mode's size), no auxiliary clip, all planes writable, no 3D scaler,
 * little-endian host data, and default pitch/offset for the primary. Once
 * per mode set, after the engine is idle. UNSUPPORTED for a pixel size the
 * engine is not used for.
 */
v9x_status v9x_r128_build_init(v9x_u32 bytes_per_pixel,
                               v9x_u32 primary_pitch_bytes,
                               v9x_u32 dp_datatype_read,
                               struct v9x_r128_blt *blt);

/* V9X_STATUS_OK with the writes in blt; INVALID_ARGUMENT for a rectangle
 * that does not fit the surface or memory; UNSUPPORTED for a pixel size,
 * pitch or position the engine cannot express. */
v9x_status v9x_r128_build_fill(const struct v9x_r128_fill *fill,
                               struct v9x_r128_blt *blt);
v9x_status v9x_r128_build_copy(const struct v9x_r128_copy *copy,
                               struct v9x_r128_blt *blt);

/* Non-zero when GUI_STAT shows all 64 FIFO slots free and the engine not
 * active. */
v9x_u32 v9x_r128_gui_idle(v9x_u32 gui_stat);

/* Free FIFO slots in a GUI_STAT read. */
v9x_u32 v9x_r128_gui_fifo_free(v9x_u32 gui_stat);

/* Non-zero when PM4_BUFFER_CNTL leaves the engine to direct writes. */
v9x_u32 v9x_r128_pm4_direct(v9x_u32 pm4_buffer_cntl);

#endif
