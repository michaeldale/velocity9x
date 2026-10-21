/*
 * Haswell display arithmetic: the primary plane's base and the vertical
 * blank, from register words read elsewhere. See
 * include\velocity9x\intel_hsw_display.h for the sources.
 */
#include "velocity9x/intel_hsw_display.h"

v9x_status v9x_intel_hsw_plane_split(v9x_u32 offset, v9x_u32 bytes_per_pixel,
                                     v9x_u32 *surf, v9x_u32 *plane_offset)
{
    v9x_u32 rest;

    if (surf == 0 || plane_offset == 0 ||
        (bytes_per_pixel != 1ul && bytes_per_pixel != 2ul &&
         bytes_per_pixel != 4ul)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* The page below the offset, and the rest in pixels along the row: i915
     * leaves y 0 for a linear surface and lets x run past the width. */
    rest = offset & (V9X_INTEL_HSW_SURF_ALIGN - 1ul);
    if (rest % bytes_per_pixel != 0ul ||
        rest / bytes_per_pixel > V9X_INTEL_HSW_OFFSET_X_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *surf = offset - rest;
    *plane_offset = rest / bytes_per_pixel;
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_ivb_plane_split(v9x_u32 offset, v9x_u32 bytes_per_pixel,
                                     v9x_u32 *surf, v9x_u32 *linoff)
{
    v9x_u32 rest;

    if (surf == 0 || linoff == 0 ||
        (bytes_per_pixel != 1ul && bytes_per_pixel != 2ul &&
         bytes_per_pixel != 4ul)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* The page below the offset in DSPSURF and the rest, in bytes, in
     * PRI_LINOFF: i915's linear case for pre-Haswell Gen4+ planes. */
    rest = offset & (V9X_INTEL_HSW_SURF_ALIGN - 1ul);
    if (rest % bytes_per_pixel != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *surf = offset - rest;
    *linoff = rest;
    return V9X_STATUS_OK;
}

int v9x_intel_hsw_scanout_live(v9x_u32 pipeconf, v9x_u32 dspcntr)
{
    if ((pipeconf & V9X_INTEL_HSW_PIPECONF_ENABLE) == 0ul ||
        (pipeconf & V9X_INTEL_HSW_PIPECONF_STATE) == 0ul) {
        return 0;
    }
    return (dspcntr & V9X_INTEL_HSW_DSPCNTR_ENABLE) != 0ul;
}

/*
 * PIPEADSL reads one line behind the beam: i915 adds a scanline_offset of
 * 1 on DDI parts other than HDMI, and the /flip probe (boot 16) saw the
 * plane latch, which the PRM puts at the start of the vertical blank, at
 * DSL 767 of a 768-line mode. So the beam's line is DSL + 1.
 */
#define V9X_INTEL_HSW_DSL_BEHIND 1ul

int v9x_intel_hsw_in_vblank(v9x_u32 dsl, v9x_u32 vtotal)
{
    v9x_u32 line = (dsl & V9X_INTEL_HSW_DSL_LINE_MASK) +
                   V9X_INTEL_HSW_DSL_BEHIND;
    v9x_u32 vactive = (vtotal & V9X_INTEL_HSW_TIMING_MASK) + 1ul;
    v9x_u32 lines = ((vtotal >> 16) & V9X_INTEL_HSW_TIMING_MASK) + 1ul;

    /* DSL vtotal - 1 is the beam back on line 0. */
    return line >= vactive && line < lines;
}
