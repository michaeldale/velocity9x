/*
 * Haswell (Gen7.5) display: the primary plane's scanout arithmetic.
 *
 * Phase F3 of docs\plans\intel-haswell-direct3d.md. Pure: the register
 * words come from V9XHSWMM's flip probe and from the HAL's scanout
 * (src\display32\engines\hsw_scanout.c), which reads and writes them and
 * decides nothing.
 *
 * Sources:
 *   - Register offsets: i915_reg.h (PIPEDSL, PIPECONF, DSPCNTR, DSPSTRIDE,
 *     DSPSURF, DSPOFFSET, DSPSURFLIVE for pipe and plane A; TRANS_VTOTAL_A),
 *     and the read-only snapshots of the i5-4570 (boot 14), where each
 *     reads a value consistent with the VBE mode.
 *   - The split of a linear offset into a 4 KB-aligned DSPSURF and an x in
 *     DSPOFFSET: i915's intel_gen4_compute_page_offset (linear case) and
 *     ironlake_update_primary_plane, which writes DSPOFFSET on Haswell for
 *     linear and tiled surfaces alike.
 * None of it is measured as a write until the /flip probe has run.
 */
#ifndef VELOCITY9X_INTEL_HSW_DISPLAY_H
#define VELOCITY9X_INTEL_HSW_DISPLAY_H

#include "velocity9x/status.h"
#include "velocity9x/types.h"

/* Transcoder A's vertical timing: vactive - 1 in 12:0, vtotal - 1 in 28:16. */
#define V9X_INTEL_HSW_TRANS_VTOTAL_A ((v9x_u32)0x0006000cul)
/* Pipe A's current line, 12:0. Read only while the pipe is live: a disabled
 * pipe's counters hung the 945 (docs\decisions\2026-10-15-a-disabled-
 * pipes-counters-hang-the-945.md). */
#define V9X_INTEL_HSW_PIPEADSL       ((v9x_u32)0x00070000ul)
#define V9X_INTEL_HSW_PIPEACONF      ((v9x_u32)0x00070008ul)
#define V9X_INTEL_HSW_DSPACNTR       ((v9x_u32)0x00070180ul)
#define V9X_INTEL_HSW_DSPASTRIDE     ((v9x_u32)0x00070188ul)
#define V9X_INTEL_HSW_DSPASURF       ((v9x_u32)0x0007019cul)
#define V9X_INTEL_HSW_DSPAOFFSET     ((v9x_u32)0x000701a4ul)
#define V9X_INTEL_HSW_DSPASURFLIVE   ((v9x_u32)0x000701acul)

/* PIPECONF: enable 31, state (the pipe is running) 30. DSPCNTR: enable 31. */
#define V9X_INTEL_HSW_PIPECONF_ENABLE ((v9x_u32)0x80000000ul)
#define V9X_INTEL_HSW_PIPECONF_STATE  ((v9x_u32)0x40000000ul)
#define V9X_INTEL_HSW_DSPCNTR_ENABLE  ((v9x_u32)0x80000000ul)

#define V9X_INTEL_HSW_DSL_LINE_MASK   ((v9x_u32)0x00001ffful)
#define V9X_INTEL_HSW_TIMING_MASK     ((v9x_u32)0x00001ffful)
/* DSPSURF is a graphics address in 31:12. */
#define V9X_INTEL_HSW_SURF_ALIGN      ((v9x_u32)0x00001000ul)
/* DSPOFFSET's x, 12:0. */
#define V9X_INTEL_HSW_OFFSET_X_MAX    ((v9x_u32)0x00001ffful)

/*
 * The DSPASURF and DSPAOFFSET words that put the plane's first pixel at
 * framebuffer byte `offset`: the page below it, and the rest as an x in
 * pixels with y 0. INVALID_ARGUMENT when the rest is not a whole number of
 * pixels or bytes_per_pixel is not 1, 2 or 4; nothing is rounded.
 */
v9x_status v9x_intel_hsw_plane_split(v9x_u32 offset, v9x_u32 bytes_per_pixel,
                                     v9x_u32 *surf, v9x_u32 *plane_offset);

/*
 * Ivy Bridge (Gen7): plane A's linear offset, a byte offset added to
 * DSPASURF for a linear surface; 701A4h is the tiled offset there and is
 * ignored (IVB PRM Vol 3 Pt 3 5.3, PRI_LINOFF and PRI_TILEOFF). Every
 * other plane and pipe register above is at the same address on Ivy Bridge
 * (both PRMs; docs\decisions\2026-10-21-ivybridge-read-only-mmio.md).
 */
#define V9X_INTEL_IVB_DSPALINOFF     ((v9x_u32)0x00070184ul)

/*
 * The DSPASURF and PRI_LINOFF words for Ivy Bridge: the page below
 * `offset`, and the rest in bytes. The PRM wants the linear offset "at
 * least pixel aligned for RGB formats", so a rest that is not a whole
 * number of pixels is INVALID_ARGUMENT, as is bytes_per_pixel other than
 * 1, 2 or 4.
 */
v9x_status v9x_intel_ivb_plane_split(v9x_u32 offset, v9x_u32 bytes_per_pixel,
                                     v9x_u32 *surf, v9x_u32 *linoff);

/* Non-zero when pipe A is enabled and running and plane A is enabled: the
 * only configuration the scanout serves, and the only one in which
 * PIPEADSL is read. */
int v9x_intel_hsw_scanout_live(v9x_u32 pipeconf, v9x_u32 dspcntr);

/* Non-zero when PIPEADSL's line is in the vertical blank of the timing in
 * TRANS_VTOTAL_A: the beam, one line ahead of the counter, at or past
 * vactive. */
int v9x_intel_hsw_in_vblank(v9x_u32 dsl, v9x_u32 vtotal);

#endif
