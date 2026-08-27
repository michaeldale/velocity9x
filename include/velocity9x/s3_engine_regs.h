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
/* Screen-to-screen BitBLT is command 0 in bits 31:27, so the source is read
 * from display memory when neither the mono-source nor image-data-source bit
 * is set. The stride register carries the destination stride in its high word
 * and the source stride in its low word; both are masked to 0xff8 by the
 * hardware, and the surface bases to an 8-byte boundary. */
#define V9X_VIRGE_SRC_BASE            0x0000a4d4ul
#define V9X_VIRGE_RECT_SRC_XY         0x0000a508ul
#define V9X_VIRGE_STRIDE_MASK            0x00000ff8ul
#define V9X_VIRGE_COORD_MAX                    2047ul

/* SUBSYS_STAT: a FIFO free-slot count and an engine-idle bit. */
#define V9X_VIRGE_STATUS_FIFO_SHIFT             8u
#define V9X_VIRGE_STATUS_FIFO_MASK       0x00001f00ul
#define V9X_VIRGE_STATUS_IDLE            0x00002000ul

/*
 * CMD_SET fields. The ROP256 byte occupies bits 24:17, which is why both ROP
 * constants are a byte shifted left by 17 - a GDI Rop's high word carries the
 * same ROP256 code, so bits 24:17 take it unchanged.
 */
#define V9X_VIRGE_CMD_DRAW_ENABLE        0x00000020ul
#define V9X_VIRGE_CMD_MONO_PATTERN       0x00000100ul
#define V9X_VIRGE_CMD_ROP_SHIFT                  17
#define V9X_VIRGE_CMD_ROP_PATCOPY        (0x000000f0ul << 17)
#define V9X_VIRGE_CMD_ROP_SRCCOPY        (0x000000ccul << 17)
#define V9X_VIRGE_CMD_X_POSITIVE         0x02000000ul
#define V9X_VIRGE_CMD_Y_POSITIVE         0x04000000ul
/*
 * CPU-source command bits, from S3.INC:800-829. MONOSRCBLT is
 * BITBLT + bSRC_Sys + bSRC_Mono, and the mono path additionally sets
 * CPUAlign_dword and bClip_Enable - the first because the image-transfer window
 * takes dword writes, the second because the bit-alignment padding has to be
 * clipped away rather than drawn.
 */
#define V9X_VIRGE_CMD_SRC_SYS            0x00000080ul
#define V9X_VIRGE_CMD_SRC_MONO           0x00000040ul
#define V9X_VIRGE_CMD_CPU_ALIGN_DWORD    0x00000800ul
#define V9X_VIRGE_CMD_TRANSPARENT        0x00000200ul
#define V9X_VIRGE_CMD_CLIP_ENABLE        0x00000002ul

/*
 * Registers the monochrome CPU-source path needs.
 *
 * Derived, not guessed. S3.INC:702 defines BitBLTArea = D2BaseOffset + 0x400
 * and gives each register as an offset within it - CLIP_L_R 0xdc, CLIP_T_B
 * 0xe0, SRC_BG_CLR 0xf8, SRC_FG_CLR 0xfc. The base resolves to 0xa400, which
 * is confirmed three ways against offsets already verified on hardware above:
 * PAT_FG_CLR 0xf4 -> 0xa4f4, CMD_SET 0x100 -> 0xa500, RWIDTH_HEIGHT 0x104 ->
 * 0xa504.
 *
 * The clip rectangle is not optional on the mono path. A mono source starts at
 * an arbitrary bit within a byte and the engine is fed whole bytes, so the
 * destination is shifted left by that bit offset and the leading padding pixels
 * are trimmed by hardware clipping. Without the clip they would be drawn.
 */
#define V9X_VIRGE_CLIP_L_R            0x0000a4dcul
#define V9X_VIRGE_CLIP_T_B            0x0000a4e0ul
#define V9X_VIRGE_SRC_BG_COLOR        0x0000a4f8ul
#define V9X_VIRGE_SRC_FG_COLOR        0x0000a4fcul

/*
 * The image-transfer window, where the CPU hands pixel data to the engine.
 * S3.INC:688 puts it at offset 0 of the MMIO window with a 0x8000-byte maximum
 * burst - inside the 64 KiB this driver's engine selector already covers.
 */
#define V9X_VIRGE_IMAGE_XFER          0x00000000ul
#define V9X_VIRGE_IMAGE_XFER_MAX      0x00008000ul

/*
 * CR66 bit 1 is the ViRGE/DX graphics-engine reset the Windows 98 S3 sample
 * uses, and the only recovery either bitness has. The Trio64 has none, which
 * is why this is a ViRGE constant and not a family one.
 */
#define V9X_VIRGE_CR66_ENGINE_RESET            0x02u
#define V9X_VIRGE_CRTC_CR66                    0x66u

#endif
