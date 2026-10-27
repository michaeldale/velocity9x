/*
 * Frame grabs: the presented buffer, whole, at chosen flips.
 *
 * The agent's screenshot cannot answer what the monitor showed. It takes
 * 40 to 70 s during 3D, so an intermittent frame - the Ironlake's early
 * 3DMark 2000 Game 1 frame (docs\decisions\2026-10-27-0.32.0-release-
 * checks-ati-ironlake.md) - cannot be caught twice. And it reads memory
 * after drawing finishes, so the HD 5450's every-second-frame flicker
 * looked clean in every screenshot (docs\decisions\2026-10-27-cedar-page-
 * flips.md). Intel's frame cover (i9xx_cover.h) writes one downsampled
 * image a session, and only on Intel.
 *
 * A grab is taken in the core's flip path, on any engine with page flips:
 * armed by the V9X_DDGRABFRAMES escape (V9XTRACE -grab:<interval>x<count>),
 * it writes the surface each Nth accepted flip presents, at full size, as
 * a 24-bit BMP. The engine is drained first, so the frame is finished.
 *
 * This module is the arithmetic: the bounds check for a direct read of
 * video memory, the pixel decode from the display's own masks, the BMP
 * layout and the schedule. The HAL reads and writes.
 */
#ifndef VELOCITY9X_FRAME_GRAB_H
#define VELOCITY9X_FRAME_GRAB_H

#include "velocity9x/types.h"

/* Files V9XGRAB0.BMP to V9XGRAB9.BMP: one digit in the name. */
#define V9X_FRAME_GRAB_FILES     10u
/* The row buffer's width, in pixels. Wider modes are refused. */
#define V9X_FRAME_GRAB_WIDTH_MAX 2048u
/* BITMAPFILEHEADER (14) and BITMAPINFOHEADER (40). */
#define V9X_FRAME_GRAB_BMP_HEADER_BYTES 54u
/* A BMP row is padded to four bytes. */
#define V9X_FRAME_GRAB_ROW_BYTES_MAX \
    ((V9X_FRAME_GRAB_WIDTH_MAX * 3u + 3u) & ~3u)

/* A validated read plan: every pixel the grab reads lies inside the
 * surface and inside video memory. */
struct v9x_frame_grab_plan {
    v9x_u32 offset;             /* surface start in video memory          */
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 pitch;              /* source bytes per row                   */
    v9x_u32 bytes_per_pixel;    /* 2 or 4                                 */
    v9x_u32 red_mask;
    v9x_u32 green_mask;
    v9x_u32 blue_mask;
    v9x_u32 row_bytes;          /* BMP bytes per row, padded              */
    v9x_u32 file_bytes;         /* header and every row                   */
};

/* V9X_TRUE and a plan, or V9X_FALSE and *plan untouched: 16 or 32 bits
 * per pixel only (8 bpp needs a palette), every mask non-zero, the width
 * at most V9X_FRAME_GRAB_WIDTH_MAX, a row inside the pitch and the last
 * row inside vram_bytes, all without overflowing a 32-bit product. */
v9x_u16 v9x_frame_grab_plan(v9x_u32 offset, v9x_u32 width, v9x_u32 height,
                            v9x_u32 pitch, v9x_u32 bits_per_pixel,
                            v9x_u32 vram_bytes, v9x_u32 red_mask,
                            v9x_u32 green_mask, v9x_u32 blue_mask,
                            struct v9x_frame_grab_plan *plan);

/* One channel of a pixel, by its mask, widened to 8 bits with its top bits
 * replicated: 5-bit 1Fh is FFh, not F8h. 0 for a zero mask. */
v9x_u8 v9x_frame_grab_channel(v9x_u32 pixel, v9x_u32 mask);

/* The 54-byte header of a bottom-up 24-bit BMP for the plan. */
void v9x_frame_grab_bmp_header(const struct v9x_frame_grab_plan *plan,
                               v9x_u8 *out);

/* One source row to the plan's row_bytes of BMP: blue, green, red, then
 * zero padding. */
void v9x_frame_grab_row(const struct v9x_frame_grab_plan *plan,
                        const v9x_u8 *line, v9x_u8 *out);

/*
 * The schedule, run once per accepted flip. V9X_TRUE when this flip is to
 * be grabbed: every interval-th flip after arming, while remaining is
 * non-zero. An interval of 0 is taken as 1. *countdown is the flips left
 * before the next grab, which arming sets to the interval.
 */
v9x_u16 v9x_frame_grab_due(v9x_u32 *countdown, v9x_u32 interval,
                           v9x_u32 remaining);

#endif
