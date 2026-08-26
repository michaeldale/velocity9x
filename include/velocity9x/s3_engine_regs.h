#ifndef VELOCITY9X_S3_ENGINE_REGS_H
#define VELOCITY9X_S3_ENGINE_REGS_H

/*
 * S3 2D drawing-engine register map, shared by both bitnesses.
 *
 * These constants used to live in src\display32\ddhal_internal.h, private to
 * the 32-bit DirectDraw HAL. GDI acceleration (docs\plans\gdi-acceleration.md)
 * needs the same registers from the 16-bit V9XDISP.DRV, which is loaded long
 * before the HAL DLL and cannot depend on it - so the numbers move here and
 * both sides include them. Constants only: no code, no pointers, no types.
 * Nothing here may grow a declaration, because the two consumers compile under
 * different memory models and only the preprocessor is common to them.
 *
 * Scope is the **2D** engine. The ViRGE S3D triangle and setup windows at
 * 0xb4xx stay in ddhal_internal.h: they belong to Direct3D, the 16-bit side has
 * no use for them, and copying them here would invite a 16-bit caller to poke
 * the 3D pipeline.
 *
 * The register numbers themselves are unchanged and remain as verified by the
 * 32-bit HAL on both chips - see docs\decisions\2026-08-14-virge-blitter.md for
 * the measured ViRGE results and the Trio64 8514/A bring-up beside it.
 */

/* VGA CRTC and input status. Byte ports, present on both chips. */
#define V9X_CRTC_INDEX              0x03d4u
#define V9X_CRTC_DATA               0x03d5u
#define V9X_INPUT_STATUS_1          0x03dau
#define V9X_STATUS_VBLANK              0x08u

/*
 * Trio32/64 enhanced 8514/A-compatible drawing engine ports.
 *
 * Word-width I/O ports, so the 16-bit side reaches these with an ordinary
 * out dx,ax and needs no selector.
 */
#define V9X_TRIO_CUR_Y                 0x82e8u
#define V9X_TRIO_CUR_X                 0x86e8u
#define V9X_TRIO_MAJ_AXIS_PCNT         0x96e8u
#define V9X_TRIO_CMD_STATUS            0x9ae8u
#define V9X_TRIO_FRGD_COLOR            0xa6e8u
#define V9X_TRIO_FRGD_MIX              0xbae8u
#define V9X_TRIO_MULTIFUNC_CNTL        0xbee8u
#define V9X_TRIO_PIXEL_CNTL_FRGD_MIX   0xa000u
#define V9X_TRIO_FRGD_MIX_NEW          0x0027u
#define V9X_TRIO_CMD_RECT_SOLID        0x40b1u
/* Screen-to-screen BitBLT on the 8514/A-compatible enhanced command set:
 * opcode 6 in bits 15:13, plus write-enable and the two direction bits.
 * FRGD_MIX 0x0067 selects a display-memory source with the SRC mix, which is
 * what makes it a copy rather than a pattern fill. The engine addresses both
 * rectangles through the display pitch from a common bank base, so source and
 * destination must both be display-pitch surfaces on a scan-line boundary. */
#define V9X_TRIO_DESTX_DIASTP          0x8ee8u
#define V9X_TRIO_DESTY_AXSTP           0x8ae8u
#define V9X_TRIO_FRGD_MIX_COPY         0x0067u
#define V9X_TRIO_CMD_BITBLT            0xc011u
#define V9X_TRIO_CMD_INC_X             0x0020u
#define V9X_TRIO_CMD_INC_Y             0x0080u
#define V9X_TRIO_STATUS_BUSY           0x0200u
#define V9X_TRIO_IDLE_SPIN_LIMIT       0x00400000ul

/*
 * ViRGE 2D register offsets inside the new-MMIO window.
 *
 * The 32-bit HAL reaches these as offsets into its flat mapping of the whole
 * 64 MiB BAR, where the MMIO window sits at BAR + 16 MiB. The 16-bit driver
 * reaches the same registers through a dedicated LDT selector based at
 * linear + 0x01000000, which is why every offset here is below 0x10000 and
 * must stay so - a register above that would not be addressable from the
 * 16-bit side at all.
 */
#define V9X_VIRGE_ENGINE_STATUS       0x00008504ul
#define V9X_VIRGE_DEST_BASE           0x0000a4d8ul
#define V9X_VIRGE_MONO_PAT_0          0x0000a4e8ul
#define V9X_VIRGE_MONO_PAT_1          0x0000a4ecul
#define V9X_VIRGE_DEST_SRC_STRIDE     0x0000a4e4ul
#define V9X_VIRGE_PATTERN_FG          0x0000a4f4ul
#define V9X_VIRGE_COMMAND             0x0000a500ul
#define V9X_VIRGE_RECT_WH             0x0000a504ul
#define V9X_VIRGE_RECT_DEST_XY        0x0000a50cul

#endif
