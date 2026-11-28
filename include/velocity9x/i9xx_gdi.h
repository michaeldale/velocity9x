/*
 * GDI screen-to-screen copies on the Gen3 blitter: the band plan and the
 * stream, as pure C (src\chipsets\intel\i9xx_gdi.c). The 16-bit driver's
 * gdi_gen3.c submits what this builds. docs\plans\gen3-gdi-blitter.md.
 */
#ifndef VELOCITY9X_I9XX_GDI_H
#define VELOCITY9X_I9XX_GDI_H

#include "velocity9x/intel_gma.h"

/*
 * The GDI path's breadcrumb: byte 0x100 of the reserve's status page. The
 * HAL's words are at 0x80 to 0x93 (breadcrumb, sequence allocator, magic
 * pair; d3d_i9xx.c), and this dword is clear of them so the HAL's proving
 * and allocator rules do not change (the plan's decision 3).
 */
#define V9X_I9XX_GDI_BREADCRUMB_BYTE   ((v9x_u32)0x00000100ul)

/*
 * At most this many bands per stream. A copy that needs more is built as
 * several streams, submitted in order, each from the band the last one
 * ended at (v9x_i9xx_gdi_build_copy's first_band). The stream is built in
 * DGROUP, 1,163 dwords at this bound.
 */
#define V9X_I9XX_GDI_MAX_BANDS         128ul

/*
 * At most this many bands per copy, every row of the tallest screen moved
 * by one. A copy past it goes to the DIB Engine.
 */
#define V9X_I9XX_GDI_MAX_COPY_BANDS    2048ul

/*
 * A copy that needs more than one stream goes to the blitter only when its
 * bands average this many pixels. On the GMA 950 netbook a band cost the
 * blitter about 15 to 40 us (V9XGDIB at 800x600x32: a 400x300 drag by 5 rows,
 * 60 bands, 1,447 us; 504x384 right by 8, 63 column bands, 2,452 us), and
 * the DIB Engine copied about 6 pixels a microsecond: below about 256
 * pixels a band, the CPU is the faster. The bound is that estimate, not a
 * measurement of the crossover. Internet Explorer's scrolls by one to three
 * rows of an 800-pixel window are the case this serves
 * (docs/decisions/2026-11-24-ddi-census-ie-on-the-netbook.md).
 */
#define V9X_I9XX_GDI_MIN_BAND_PIXELS   256ul

/* The longest stream: the leading MI_FLUSH, every band's copy and its
 * MI_FLUSH, the breadcrumb, and one MI_NOOP of padding. */
#define V9X_I9XX_GDI_STREAM_DWORDS \
    (1ul + V9X_I9XX_GDI_MAX_BANDS * (V9X_I9XX_BLT_COPY_DWORDS + 1ul) + \
     V9X_I9XX_BREADCRUMB_STREAM_DWORDS + 1ul)

/*
 * One screen-to-screen copy in GDI's terms: both rectangles on one surface
 * at base (a framebuffer byte offset, which is its graphics address) with
 * pitch, the destination at (x, y), the source at (source_x, source_y),
 * both width by height pixels.
 */
struct v9x_i9xx_gdi_copy {
    v9x_u32 bytes_per_pixel;
    v9x_u32 vram_bytes;
    v9x_u32 base;
    v9x_u32 pitch;
    v9x_u32 x;
    v9x_u32 y;
    v9x_u32 source_x;
    v9x_u32 source_y;
    v9x_u32 width;
    v9x_u32 height;
};

/*
 * One band: a part of the copy that does not overlap its own source.
 * Offsets are relative to the copy's rectangles.
 */
struct v9x_i9xx_gdi_band {
    v9x_u32 left;
    v9x_u32 top;
    v9x_u32 width;
    v9x_u32 height;
};

/*
 * How many bands a copy needs, so the blitter can run them in index order.
 *
 * Gen3's XY_SRC_COPY_BLT has no scan-direction control, so a copy whose
 * rectangles overlap is cut into bands no taller than the vertical move
 * (or, for a purely horizontal move, no wider than the horizontal one),
 * ordered away from the overlap: a band never reads pixels an earlier band
 * wrote, and no band overlaps itself. Rectangles that do not overlap are
 * one band. UNSUPPORTED past V9X_I9XX_GDI_MAX_COPY_BANDS, or past
 * V9X_I9XX_GDI_MAX_BANDS with bands under V9X_I9XX_GDI_MIN_BAND_PIXELS on
 * average; INVALID_ARGUMENT for
 * an empty copy.
 */
v9x_status v9x_i9xx_gdi_band_count(const struct v9x_i9xx_gdi_copy *copy,
                                   v9x_u32 *count);

/*
 * Band `index` of the order above, computed rather than stored: the 16-bit
 * driver runs on GDI's stack and keeps no array of them. index must be
 * below what v9x_i9xx_gdi_band_count gave.
 */
void v9x_i9xx_gdi_band_at(const struct v9x_i9xx_gdi_copy *copy,
                          v9x_u32 index, struct v9x_i9xx_gdi_band *band);

/*
 * One stream of a copy, bands first_band up to V9X_I9XX_GDI_MAX_BANDS of
 * them: MI_FLUSH, so what the 3D engine left in its render cache is in
 * memory before the blitter reads it; each band's XY_SRC_COPY_BLT and
 * MI_FLUSH; the breadcrumb storing sequence at breadcrumb_offset; MI_NOOP
 * to an even length. Every band goes through v9x_i9xx_build_copy_blt and
 * the whole stream through v9x_i9xx_decode_blt_stream before this returns
 * OK. bands_out, when not null, gets the copy's band count; the caller
 * builds the next stream from first_band + V9X_I9XX_GDI_MAX_BANDS while
 * that is below it. A first_band other than 0 at or past the count is
 * INVALID_ARGUMENT.
 */
v9x_status v9x_i9xx_gdi_build_copy(const struct v9x_i9xx_gdi_copy *copy,
                                   v9x_u32 first_band,
                                   v9x_u32 breadcrumb_offset,
                                   v9x_u32 sequence,
                                   v9x_u32 *stream, v9x_u32 capacity,
                                   v9x_u32 *written, v9x_u32 *bands_out);

#endif /* VELOCITY9X_I9XX_GDI_H */
