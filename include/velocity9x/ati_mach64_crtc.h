/*
 * The Mach64 family's scanout start and vertical position, as the flip
 * needs them: pure arithmetic over register values, host-tested
 * (tests\host\test_mach64_crtc.c). src\display32\engines\m64_scanout.c
 * reads and writes the registers.
 *
 * Measured on A8U4I5's Rage IIC in its VBE 1024x768x16 mode (ATIRX /crtc,
 * boot 141, docs\decisions\2026-10-03-rage-iic-scanout-start.md):
 * CRTC_OFF_PITCH's offset field moves the picture cleanly, and a write
 * made mid-frame showed mid-frame - a band of the new buffer between the
 * lines it was written and rewritten - so the start is not held for the
 * next frame. A flip is therefore written in the vertical blank.
 */
#ifndef VELOCITY9X_ATI_MACH64_CRTC_H
#define VELOCITY9X_ATI_MACH64_CRTC_H

#include "velocity9x/types.h"
#include "velocity9x/status.h"

/* Block 0 of the register window, at +0x400 (RRG p.4-27..4-31). */
#define V9X_M64_CRTC_V_TOTAL_DISP   0x00000408ul
#define V9X_M64_CRTC_VLINE          0x00000410ul
#define V9X_M64_CRTC_OFF_PITCH      0x00000414ul

/* CRTC_OFF_PITCH: the offset in 8-byte units in 19:0; the pitch and the
 * bits above the offset are kept as they are. */
#define V9X_M64_CRTC_OFFSET_MASK    0x000ffffful
#define V9X_M64_CRTC_KEEP_MASK      0xfff00000ul

/* Lines before the blank's end a flip may still be written: it must land
 * in the blank, never in the first active line. */
#define V9X_M64_CRTC_FLIP_GUARD     4ul

/* The register value that starts the scanout at byte_offset, keeping the
 * pitch: V9X_STATUS_INVALID_ARGUMENT for an offset that is not 8-byte
 * aligned, past VRAM, or past the field. */
v9x_status v9x_m64_crtc_start(v9x_u32 off_pitch, v9x_u32 byte_offset,
                              v9x_u32 vram_bytes, v9x_u32 *value);

/* Non-zero while CRTC_VLINE's current line is in the vertical blank:
 * between the last displayed line and the total. */
int v9x_m64_crtc_in_blank(v9x_u32 vline, v9x_u32 v_total_disp);

/* Non-zero while a flip may be written: in the blank, and at least
 * V9X_M64_CRTC_FLIP_GUARD lines before it ends. */
int v9x_m64_crtc_flip_window(v9x_u32 vline, v9x_u32 v_total_disp);

#endif
