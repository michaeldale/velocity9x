/*
 * The Rage 128's scanout start, flip status and vertical blank, as the flip
 * needs them: pure arithmetic over register values, host-tested
 * (tests\host\test_r128_crtc.c). src\display32\engines\r128_scanout.c
 * reads and writes the registers.
 *
 * From the RAGE 128 PRO Register Reference Guide (RRG-G04500, Rev 1.01,
 * pp. 3-66 to 3-73): CRTC_OFFSET holds the display base in bytes, bits
 * 2:0 hardwired to zero. With CRTC_OFFSET_CNTL's CRTC_OFFSET_FLIP_CNTL
 * clear, a new offset is used "on vertical blank", so a flip is written at
 * any line; CRTC_GUI_TRIG_OFFSET reads 1 from that write until display
 * starts for the new address, which is what completes it. A8U4I5 read
 * CRTC_OFFSET_CNTL = 0 under both drivers (V9XR1MM, boots 519 and 520):
 * linear, latched at the blank, not locked.
 */
#ifndef VELOCITY9X_R128_CRTC_H
#define VELOCITY9X_R128_CRTC_H

#include "velocity9x/types.h"
#include "velocity9x/status.h"

#define V9X_R128_CRTC_STATUS            0x005cul
#define V9X_R128_CRTC_V_TOTAL_DISP      0x0208ul
#define V9X_R128_CRTC_VLINE_CRNT_VLINE  0x0210ul
#define V9X_R128_CRTC_OFFSET            0x0224ul
#define V9X_R128_CRTC_OFFSET_CNTL       0x0228ul

/* CRTC_STATUS: CRTC_VBLANK_CUR, the raster in the vertical blank. */
#define V9X_R128_CRTC_VBLANK_CUR        0x00000001ul
/* CRTC_OFFSET: the base, 24:0; CRTC_GUI_TRIG_OFFSET (R), 30. */
#define V9X_R128_CRTC_OFFSET_MASK       0x01fffffful
#define V9X_R128_CRTC_OFFSET_ALIGN      8ul
#define V9X_R128_CRTC_GUI_TRIG_OFFSET   0x40000000ul
/* CRTC_OFFSET_CNTL: CRTC_TILE_EN 15, CRTC_OFFSET_FLIP_CNTL 16 (1 = take
 * the offset at any horizontal blank, which tears), CRTC_OFFSET_LOCK 31. */
#define V9X_R128_CRTC_TILE_EN           0x00008000ul
#define V9X_R128_CRTC_OFFSET_FLIP_CNTL  0x00010000ul
#define V9X_R128_CRTC_OFFSET_LOCK       0x80000000ul

/* Non-zero when CRTC_OFFSET_CNTL describes the scanout these flips assume:
 * linear, the offset taken at the vertical blank, not locked. */
int v9x_r128_crtc_flips_at_blank(v9x_u32 offset_cntl);

/* The CRTC_OFFSET value that starts the scanout at byte_offset:
 * V9X_STATUS_INVALID_ARGUMENT for an offset that is not 8-byte aligned (at
 * 24 bpp, also a multiple of 3, as aty128fb and the X driver require), past
 * VRAM, or past the field. */
v9x_status v9x_r128_crtc_start(v9x_u32 byte_offset, v9x_u32 vram_bytes,
                               v9x_u32 bytes_per_pixel, v9x_u32 *value);

/* Non-zero while the last CRTC_OFFSET written is not yet displayed. */
int v9x_r128_crtc_flip_pending(v9x_u32 crtc_offset);

/* Non-zero while CRTC_STATUS says the raster is in the vertical blank. */
int v9x_r128_crtc_in_vblank(v9x_u32 crtc_status);

#endif
