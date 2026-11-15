/*
 * The Radeon 9250's (RV280) scanout start, flip status and vertical blank:
 * pure arithmetic over register values, host-tested
 * (tests\host\test_rv280_crtc.c). src\display32\engines\rv280_scanout.c
 * reads and writes the registers.
 *
 * The legacy Radeon CRTC, the Rage 128's by descent at the same offsets
 * (r128_crtc.h). Offsets and bits: xf86-video-ati 6.14.6 radeon_reg.h. The
 * flip sequence and its pending test are Linux r100_page_flip and
 * r100_page_flip_pending (r100.c:164-213): CRTC_OFFSET written with
 * OFFSET_LOCK set, GUI_TRIG_OFFSET awaited, then written again unlocked so
 * the CRTC takes it at the vertical blank; the flip pends while
 * GUI_TRIG_OFFSET reads 1.
 *
 * M-HNBJNV's 9250 read CRTC_OFFSET_CNTL = 10000000h under VBE at 8, 16 and
 * 32 bpp (docs\decisions\2026-11-15-rv280-register-probe.md): bit 28,
 * CRTC_GUI_TRIG_OFFSET_LEFT_EN, which is what makes GUI_TRIG_OFFSET report
 * a pending flip; no tiling, no per-line flip, no stereo, not locked.
 */
#ifndef VELOCITY9X_RV280_CRTC_H
#define VELOCITY9X_RV280_CRTC_H

#include "velocity9x/types.h"
#include "velocity9x/status.h"

#define V9X_RV280_CRTC_STATUS           0x005cul
#define V9X_RV280_CRTC_OFFSET           0x0224ul
#define V9X_RV280_CRTC_OFFSET_CNTL      0x0228ul

/* CRTC_STATUS: CRTC_VBLANK_CUR, the raster in the vertical blank. */
#define V9X_RV280_CRTC_VBLANK_CUR       0x00000001ul
/* CRTC_OFFSET: the base, 26:0; GUI_TRIG_OFFSET (read), 30; OFFSET_LOCK,
 * 31. */
#define V9X_RV280_CRTC_OFFSET_MASK      0x07fffffful
#define V9X_RV280_CRTC_OFFSET_ALIGN     8ul
#define V9X_RV280_CRTC_GUI_TRIG_OFFSET  0x40000000ul
#define V9X_RV280_CRTC_OFFSET_LOCK      0x80000000ul
/* CRTC_OFFSET_CNTL: TILE_EN_RIGHT 14, TILE_EN 15, OFFSET_FLIP_CNTL 16 (take
 * the offset at any horizontal blank, which tears), STEREO_OFFSET_EN 17,
 * GUI_TRIG_OFFSET_LEFT_EN 28, the lock 31. */
#define V9X_RV280_CRTC_TILE_EN_RIGHT    0x00004000ul
#define V9X_RV280_CRTC_TILE_EN          0x00008000ul
#define V9X_RV280_CRTC_OFFSET_FLIP_CNTL 0x00010000ul
#define V9X_RV280_CRTC_STEREO_OFFSET_EN 0x00020000ul
#define V9X_RV280_CRTC_GUI_TRIG_LEFT_EN 0x10000000ul
#define V9X_RV280_CRTC_CNTL_LOCK        0x80000000ul

/* Non-zero when CRTC_OFFSET_CNTL describes the scanout these flips assume:
 * linear, mono, the offset taken at the vertical blank, not locked, and the
 * pending bit enabled, so a flip can be seen to complete. */
int v9x_rv280_crtc_flips_at_blank(v9x_u32 offset_cntl);

/* The CRTC_OFFSET value that starts the scanout at byte_offset into a
 * frame buffer of fb_bytes whose GPU address is fb_gpu_base, without the
 * lock: V9X_STATUS_INVALID_ARGUMENT for an offset that is not 8-byte
 * aligned (at 24 bpp, also a whole pixel), past fb_bytes, or an address
 * past the field. */
v9x_status v9x_rv280_crtc_start(v9x_u32 fb_gpu_base, v9x_u32 fb_bytes,
                                v9x_u32 byte_offset,
                                v9x_u32 bytes_per_pixel, v9x_u32 *value);

/* Non-zero while the last CRTC_OFFSET written is not yet displayed. */
int v9x_rv280_crtc_flip_pending(v9x_u32 crtc_offset);

/* Non-zero while CRTC_STATUS says the raster is in the vertical blank. */
int v9x_rv280_crtc_in_vblank(v9x_u32 crtc_status);

#endif
