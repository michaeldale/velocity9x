/*
 * ATI Radeon X1200 IGP (RS690) 2D engine: register values for solid fill
 * and screen copy.
 *
 * Pure policy, no I/O. The caller writes what these builders return, every
 * dword in order, to the register BAR (BAR2) + offset, with the CP's queues
 * off (CP_CSQ_CNTL mode 0), which is how VBE leaves the chip
 * (docs\decisions\2026-11-04-rs690-register-probe.md). The last write of a
 * fill or copy is DST_HEIGHT_WIDTH, whose write starts the operation.
 *
 * Offsets and fields are xf86-video-ati 6.14.6's radeon_reg.h; the
 * sequences are its MMIO EXA path, radeon_exa_funcs.c (Emit2DState,
 * RADEONSolid, RADEONCopy), radeon_exa.c (RADEONGetOffsetPitch) and
 * radeon_accel.c (RADEONEngineRestore, the R300 branch of
 * RADEONEngineFlush). The 2D engine is the Radeon one the RS690 keeps
 * (Linux radeon_asic.c gives rs690_asic r100_copy_blit). None of it is
 * checked against an AMD register guide.
 *
 * Every surface is named by its offset into the frame buffer, as the LFB
 * sees it; the engine wants a GPU address, which is the frame buffer's
 * GPU base (HDP_FB_LOCATION) plus that offset.
 */
#ifndef VELOCITY9X_RS690_2D_H
#define VELOCITY9X_RS690_2D_H

#include "velocity9x/status.h"

/* Non-GUI registers. */
#define V9X_RS690_CONFIG_MEMSIZE        0x00f8ul
#define V9X_RS690_HDP_FB_LOCATION       0x0134ul
#define V9X_RS690_CP_CSQ_CNTL           0x0740ul
#define V9X_RS690_RBBM_STATUS           0x0e40ul

/* 2D engine registers (FIFOed). */
#define V9X_RS690_SRC_PITCH_OFFSET      0x1428ul
#define V9X_RS690_DST_PITCH_OFFSET      0x142cul
#define V9X_RS690_SRC_Y_X               0x1434ul
#define V9X_RS690_DST_Y_X               0x1438ul
#define V9X_RS690_DST_HEIGHT_WIDTH      0x143cul
#define V9X_RS690_DP_GUI_MASTER_CNTL    0x146cul
#define V9X_RS690_DP_BRUSH_BKGD_CLR     0x1478ul
#define V9X_RS690_DP_BRUSH_FRGD_CLR     0x147cul
#define V9X_RS690_DP_SRC_FRGD_CLR       0x15d8ul
#define V9X_RS690_DP_SRC_BKGD_CLR       0x15dcul
#define V9X_RS690_DP_CNTL               0x16c0ul
#define V9X_RS690_DP_DATATYPE           0x16c4ul
#define V9X_RS690_DP_WRITE_MASK         0x16ccul
#define V9X_RS690_DEFAULT_SC_BOTTOM_RIGHT 0x16e8ul
#define V9X_RS690_DSTCACHE_CTLSTAT      0x1714ul

/* DP_GUI_MASTER_CNTL fields. */
#define V9X_RS690_GMC_SRC_PITCH_OFFSET_CNTL 0x00000001ul
#define V9X_RS690_GMC_DST_PITCH_OFFSET_CNTL 0x00000002ul
#define V9X_RS690_GMC_BRUSH_SOLID_COLOR 0x000000d0ul  /* 13 << 4 */
#define V9X_RS690_GMC_BRUSH_NONE        0x000000f0ul  /* 15 << 4 */
#define V9X_RS690_GMC_DST_DATATYPE_SHIFT 8
#define V9X_RS690_GMC_SRC_DATATYPE_COLOR 0x00003000ul /* 3 << 12 */
#define V9X_RS690_GMC_ROP3_SHIFT        16
#define V9X_RS690_DP_SRC_SOURCE_MEMORY  0x02000000ul  /* 2 << 24 */
#define V9X_RS690_GMC_CLR_CMP_CNTL_DIS  0x10000000ul

/* ROP3 codes: pattern copy for fills, source copy for copies. */
#define V9X_RS690_ROP3_P                0xf0ul
#define V9X_RS690_ROP3_S                0xccul

/* DP_CNTL direction bits. */
#define V9X_RS690_DST_X_LEFT_TO_RIGHT   0x00000001ul
#define V9X_RS690_DST_Y_TOP_TO_BOTTOM   0x00000002ul

/* DP_DATATYPE bit 29: host data big-endian, cleared on a PC. */
#define V9X_RS690_HOST_BIG_ENDIAN_EN    0x20000000ul

/* Destination datatypes (GMC 11:8): CI8, RGB565, ARGB8888 (radeon_reg.h
 * ATI_DATATYPE_*). A 5:5:5 desktop is the same 16 bits to a fill or copy.
 * 24 bpp is not offered. */
#define V9X_RS690_DATATYPE_8BPP         2ul
#define V9X_RS690_DATATYPE_16BPP        4ul
#define V9X_RS690_DATATYPE_32BPP        6ul

/* *_PITCH_OFFSET: pitch in 64-byte units in 29:22, the GPU address in
 * 1 KiB units in 21:0 (RADEONGetOffsetPitch; it refuses a pitch above
 * 16320 bytes, the field's 255 units). */
#define V9X_RS690_PITCH_SHIFT           22
#define V9X_RS690_PITCH_UNIT            64ul
#define V9X_RS690_PITCH_UNITS_MAX       0xfful
#define V9X_RS690_OFFSET_SHIFT          10
#define V9X_RS690_OFFSET_ALIGN          1024ul

/* Coordinates, widths and heights, and the default scissor's maxima: 13
 * bits (RADEON_DEFAULT_SC_RIGHT_MAX / BOTTOM_MAX, 1FFFh). */
#define V9X_RS690_COORD_MAX             0x1ffful
#define V9X_RS690_SC_MAX                0x1fff1ffful

/* RBBM_STATUS: free command FIFO entries in 6:0 (64), engine active in 31
 * (RADEON_RBBM_FIFOCNT_MASK, RADEON_RBBM_ACTIVE). */
#define V9X_RS690_RBBM_FIFO_MASK        0x0000007ful
#define V9X_RS690_RBBM_FIFO_SLOTS       64ul
#define V9X_RS690_RBBM_ACTIVE           0x80000000ul

/* DSTCACHE_CTLSTAT: flush and free the 2D destination cache (bits 0 and
 * 2), busy in 31 (R300_RB2D_DC_FLUSH_ALL, R300_RB2D_DC_BUSY). */
#define V9X_RS690_DC_FLUSH_ALL          0x00000005ul
#define V9X_RS690_DC_BUSY               0x80000000ul

/* HDP_FB_LOCATION 15:0: the frame buffer's GPU address >> 16. */
#define V9X_RS690_FB_LOCATION_MASK      0x0000fffful
#define V9X_RS690_FB_LOCATION_SHIFT     16

/* CP_CSQ_CNTL 31:28, the CP's mode; 0 is both queues disabled. */
#define V9X_RS690_CSQ_MODE_SHIFT        28
#define V9X_RS690_CSQ_MODE_MASK         0xful

/* The init list writes 3 registers; a fill 11; a copy 13. */
#define V9X_RS690_BLT_WRITES            16u

struct v9x_rs690_blt {
    v9x_u32 offsets[V9X_RS690_BLT_WRITES];
    v9x_u32 values[V9X_RS690_BLT_WRITES];
    v9x_u32 count;
};

struct v9x_rs690_fill {
    v9x_u32 fb_gpu_base;
    v9x_u32 fb_bytes;
    v9x_u32 target_offset;
    v9x_u32 pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 left;
    v9x_u32 top;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 color;
};

struct v9x_rs690_copy {
    v9x_u32 fb_gpu_base;
    v9x_u32 fb_bytes;
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
 * The engine state the operations rely on and do not write themselves:
 * little-endian host data, the default scissor opened to its maxima, and
 * all planes writable (RADEONEngineRestore). Once per mode set, after the
 * engine is idle. dp_datatype_read is DP_DATATYPE as read, whose other
 * bits are kept.
 */
v9x_status v9x_rs690_build_init(v9x_u32 dp_datatype_read,
                                struct v9x_rs690_blt *blt);

/* V9X_STATUS_OK with the writes in blt; INVALID_ARGUMENT for a rectangle
 * that does not fit the surface or the frame buffer; UNSUPPORTED for a
 * pixel size, pitch or position the engine cannot express. */
v9x_status v9x_rs690_build_fill(const struct v9x_rs690_fill *fill,
                                struct v9x_rs690_blt *blt);
v9x_status v9x_rs690_build_copy(const struct v9x_rs690_copy *copy,
                                struct v9x_rs690_blt *blt);

/* Non-zero when RBBM_STATUS shows all 64 FIFO entries free and the engine
 * not active. */
v9x_u32 v9x_rs690_gui_idle(v9x_u32 rbbm_status);

/* The frame buffer's GPU address from HDP_FB_LOCATION. */
v9x_u32 v9x_rs690_fb_gpu_base(v9x_u32 hdp_fb_location);

/* Non-zero when CP_CSQ_CNTL leaves the engine to direct writes. */
v9x_u32 v9x_rs690_cp_off(v9x_u32 cp_csq_cntl);

/* The display controller's address for a surface at offset into the frame
 * buffer: fb_gpu_base + offset, refused unless the offset lies inside
 * fb_bytes, the sum does not wrap, and the address is 256-byte aligned.
 * The alignment is Evergreen's GRPH_PRIMARY_SURFACE_ADDRESS rule
 * (eg_dma.h), taken for AVIVO's D1GRPH without an AVIVO register guide to
 * confirm it. */
#define V9X_RS690_SCANOUT_ALIGN 256ul
v9x_status v9x_rs690_scanout_address(v9x_u32 fb_gpu_base, v9x_u32 fb_bytes,
                                     v9x_u32 offset, v9x_u32 *address);

#endif
