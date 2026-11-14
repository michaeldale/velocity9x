/*
 * The vbe screen shadow's rectangle arithmetic (docs\plans\vbe-shadow-
 * framebuffer.md).
 *
 * The DIB Engine draws the desktop into a copy in system memory, and the
 * driver copies what changed to the frame buffer. Reads then stay in memory:
 * on the vbe family the frame buffer is write-combined, so writing to it is
 * fast and reading it runs at 5-6 MB/s on the AMD machines measured.
 *
 * This header and src\common\shadow.c decide what to copy. The 16-bit
 * driver (src\display16\shadow16.c) feeds them the rectangles the DIB Engine
 * passes to deBeginAccess and the cursor's position, and does the copy.
 *
 * Rectangles here are right and bottom exclusive. The DIB Engine's
 * deBeginAccess rectangle is not documented either way (DIBENG.INC and the
 * s3v sample's ACCESS.ASM only pass it on), so it is taken as inclusive: one
 * extra row and column copied when it is in fact exclusive, never one missed
 * when it is not.
 */
#ifndef VELOCITY9X_SHADOW_H
#define VELOCITY9X_SHADOW_H

#include "velocity9x/types.h"

/*
 * A copy is widened to whole dwords at both ends: the copy loop moves dwords,
 * and a write-combining buffer flushes whole dwords anyway. Pitches are
 * dword multiples on every VBE mode this family sets, so the widening never
 * leaves the row.
 */
#define V9X_SHADOW_ALIGN_BYTES 4u

struct v9x_shadow_rect {
    v9x_s16 left;
    v9x_s16 top;
    v9x_s16 right;
    v9x_s16 bottom;
};

/* What has been drawn since the last copy. */
struct v9x_shadow_dirty {
    struct v9x_shadow_rect rect;
    v9x_u16 pending;        /* V9X_TRUE when rect holds something */
    v9x_u16 depth;          /* deBeginAccess calls not yet ended  */
};

/* One copy: rows of row_bytes each, starting offset bytes into both the
 * shadow and the frame buffer, which share the pitch. */
struct v9x_shadow_span {
    v9x_u32 offset;
    v9x_u16 row_bytes;
    v9x_u16 rows;
};

/*
 * One entry of the shadow's event log (V9X_DD_SHARED.shadow_log): what
 * happened, a detail word (a bracket's flags, with the nesting depth
 * after it in the high byte for an end; an Output style) and a rectangle
 * (a cursor event's x, y in left, top).
 */
struct v9x_shadow_event {
    v9x_u16 kind;
    v9x_u16 detail;
    struct v9x_shadow_rect rect;
};

/* A power of two. 128 fits the shared block's 12 KiB with 236 bytes to
 * spare (2026-11-12: 10,516 bytes before the log). */
#define V9X_SHADOW_LOG_EVENTS       128u

#define V9X_SHADOW_EVENT_BEGIN      1u  /* deBeginAccess, as passed   */
#define V9X_SHADOW_EVENT_END        2u  /* deEndAccess, pending rect  */
#define V9X_SHADOW_EVENT_TAKE       3u  /* stale read back, all       */
#define V9X_SHADOW_EVENT_OVERLAP    4u  /* stale read back, cursor's  */
#define V9X_SHADOW_EVENT_COPY       5u  /* copied out to the screen   */
#define V9X_SHADOW_EVENT_OUTPUT     6u  /* Output, its bound          */
#define V9X_SHADOW_EVENT_CURSOR     7u  /* cursor set (0) or moved (1) */
#define V9X_SHADOW_EVENT_FLUSH_ALL  8u  /* mode restore               */
#define V9X_SHADOW_EVENT_STALE      9u  /* HAL marked stale           */

void v9x_shadow_dirty_clear(struct v9x_shadow_dirty *dirty);

/*
 * Add a deBeginAccess rectangle, as the DIB Engine passed it, to what is
 * pending. Clipped to the screen. A rectangle whose right is left of its left
 * or whose bottom is above its top is taken as the whole screen: an argument
 * that cannot bound the change costs a full copy, never a missed one.
 */
void v9x_shadow_dirty_add(struct v9x_shadow_dirty *dirty,
                          v9x_s16 left, v9x_s16 top,
                          v9x_s16 right, v9x_s16 bottom,
                          v9x_u16 width, v9x_u16 height);

/* Add an exclusive rectangle (the cursor's), clipped to the screen. An empty
 * one adds nothing. */
void v9x_shadow_dirty_add_rect(struct v9x_shadow_dirty *dirty,
                               const struct v9x_shadow_rect *rect,
                               v9x_u16 width, v9x_u16 height);

/*
 * The same, cut to `bound` first when bound is not null: inside a call whose
 * own arguments bound what it draws (v9x_shadow_output_bound). An inverted
 * access rectangle, the whole screen on its own, becomes the bound.
 */
void v9x_shadow_dirty_add_within(struct v9x_shadow_dirty *dirty,
                                 v9x_s16 left, v9x_s16 top,
                                 v9x_s16 right, v9x_s16 bottom,
                                 const struct v9x_shadow_rect *bound,
                                 v9x_u16 width, v9x_u16 height);

/*
 * Output (ordinal 8) styles whose points bound what they draw, from
 * 98DDK\inc\win98\inc16\GDIDEFS.INC:1416-1435. The DIB Engine's deBeginAccess
 * rectangle does not bound an Output call: measured on the i5-4570
 * (2026-11-11), every line cost a whole-screen copy.
 *
 * Only these reach the driver to draw on this family: the DIB Engine's
 * GDIINFO claims LINECAPS 0x23 (thin, styled polylines) and POLYGONALCAPS
 * 0x08 (scanlines), with no wide lines and no curves, so GDI turns every
 * other shape into these.
 */
#define V9X_SHADOW_OS_SCANLINES     4u
#define V9X_SHADOW_OS_POLYSCANLINE  5u
#define V9X_SHADOW_OS_POLYLINE      18u
/* The start and end of a series of OS_SCANLINES calls. They draw nothing
 * (the s3v sample's BeginScan_S3 only prepares the engine,
 * SCANLINE.ASM:819-833), and the DIB Engine brackets the series with the
 * whole screen: a polygon or ellipse cost one whole-screen copy on the
 * i5-4570 until they were given an empty bound. */
#define V9X_SHADOW_OS_BEGINNSCAN    80u
#define V9X_SHADOW_OS_ENDNSCAN      81u

/*
 * The rectangle an Output call can draw in, from its points (x, y pairs).
 * OS_POLYLINE: the points' box, inclusive. OS_SCANLINES: one row at the
 * first point's y, spanning the x intervals of the points after it (each an
 * x0, x1 pair, 98DDK\src\display\mini\s3v\SCANLINE.ASM:217-232), inclusive.
 * OS_POLYSCANLINE: `count` SCAN records (GDIDEFS.INC:1368-1374, walked as
 * SCANLINE.ASM:339-412 does): x count, top, bottom (exclusive), the x pairs,
 * the count again; the box of all of them. OS_BEGINNSCAN, OS_ENDNSCAN: an
 * empty bound, points not read. V9X_FALSE, and no bound, for any other
 * style, too few points, or a record whose x count is zero or odd.
 */
v9x_u16 v9x_shadow_output_bound(v9x_u16 style, v9x_u16 count,
                                const v9x_s16 *points,
                                struct v9x_shadow_rect *bound);

/* Mark the whole screen pending. */
void v9x_shadow_dirty_add_all(struct v9x_shadow_dirty *dirty,
                              v9x_u16 width, v9x_u16 height);

/*
 * The cursor's rectangle when its hot spot is at (x, y): the shape is drawn
 * with its hot spot pixel there, so it starts hot_x left and hot_y above.
 * Empty when the shape has no width or height (no cursor).
 */
void v9x_shadow_cursor_rect(v9x_s16 x, v9x_s16 y,
                            v9x_s16 hot_x, v9x_s16 hot_y,
                            v9x_u16 shape_width, v9x_u16 shape_height,
                            struct v9x_shadow_rect *rect);

/*
 * The byte span to copy for an exclusive rectangle already clipped to the
 * screen. V9X_FALSE, and nothing to copy, when the rectangle is empty or the
 * depth is not one this family draws (8, 16, 24, 32).
 */
v9x_u16 v9x_shadow_span(const struct v9x_shadow_rect *rect,
                        v9x_u16 bits_per_pixel, v9x_u32 pitch,
                        struct v9x_shadow_span *span);

/*
 * The deferred read-back (docs\decisions\2026-11-12-ati-screen-shadow.md).
 * On a chip with an engine, the HAL's engine copies to the visible screen
 * from video memory - windowed presents - write the frame buffer and not the
 * shadow. Reading each one back cost 340 ms a frame on Carrizo, so the HAL
 * adds its rectangle to `stale` instead, and the frame buffer stays the
 * truth there: GDI's own drawing is copied out before its call returns.
 *
 * Before GDI touches the screen, this says what to read back. When `access`
 * (exclusive, clipped) touches `stale` at all, fetch is the whole of
 * `stale`, `stale` is cleared, and the answer is V9X_TRUE. All of it, not
 * the overlap: GDI's accesses nest, and a later access in the same call
 * must not read back over pixels an earlier one drew and has not copied
 * out yet. With the whole rectangle read back at the first touch, nothing
 * is stale for the rest of the call. The same call before the copy out of
 * a pending union: what of the union is still stale, GDI did not draw.
 *
 * The rectangle is read once, before the clear, so a HAL adding to it
 * meanwhile at worst marks again what is about to be read back.
 */
v9x_u16 v9x_shadow_stale_take(struct v9x_shadow_dirty *stale,
                              const struct v9x_shadow_rect *access,
                              struct v9x_shadow_rect *fetch);

/*
 * The cursor's form, from the mouse interrupt, where a whole window's
 * read-back would be too long: fetch is the overlap of `stale` and `access`
 * only, and `stale` is left as it was. V9X_TRUE when they overlap. The DIB
 * Engine does not draw the cursor inside an access bracket, so this never
 * reads back over drawing still to be copied out.
 */
v9x_u16 v9x_shadow_stale_overlap(const struct v9x_shadow_dirty *stale,
                                 const struct v9x_shadow_rect *access,
                                 struct v9x_shadow_rect *fetch);

#endif
