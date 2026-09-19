/*
 * The Gen3 display FIFO watermark, as arithmetic.
 *
 * The scanout fetches into a FIFO ahead of the beam and refills when it
 * falls to the watermark. Too low and it empties part way down a frame,
 * which loses the rest of the picture: intel90 and intel91 measured
 * PIPESTAT bit 31 setting on the live pipe of the netbook, and the
 * recordings show frames whose top band is right and whose remainder is
 * black.
 *
 * This driver sets modes through the VBE BIOS and, until 2026-09-20, never
 * programmed a watermark. What it found there, intel91 and intel93 read as
 * plane A 6, plane B 6, overlay 6, self-refresh 127, unchanged across the
 * transitions captured - though both those modes share a pixel rate, a
 * pixel format and a FIFO partition, so that is a reading of the value and
 * not of its provenance. i915 computes them per mode in i9xx_update_wm,
 * and for the netbook's live plane the same formula gives 20 against the
 * 6 found there.
 *
 * The formula lives here, away from the registers, because it is pure
 * arithmetic over a handful of numbers and the project's rule is that such
 * things are host-tested rather than reasoned about - which matters
 * doubly when the machine it runs on has no network and every trial costs
 * a walk to it.
 *
 * Nothing here writes a register. v9x_i9xx_wm_plane returns what the
 * watermark for one plane should be; the caller reads DSPARB and the pipe
 * timings and decides what to do with the answer.
 */
#ifndef VELOCITY9X_I9XX_WM_H
#define VELOCITY9X_I9XX_WM_H

#include "velocity9x/types.h"

/*
 * i945_wm_info, from i915. The FIFO size here is the whole FIFO; the
 * per-plane size comes from DSPARB and is passed in separately.
 */
#define V9X_I9XX_WM_FIFO_TOTAL    ((v9x_u32)127ul)
#define V9X_I9XX_WM_MAX           ((v9x_u32)0x3ful)
#define V9X_I9XX_WM_DEFAULT       ((v9x_u32)1ul)
#define V9X_I9XX_WM_GUARD         ((v9x_u32)2ul)
#define V9X_I9XX_WM_CACHELINE     ((v9x_u32)64ul)

/* i915's pessimal_latency_ns, used when no better figure is known. */
#define V9X_I9XX_WM_LATENCY_NS    ((v9x_u32)5000ul)

/*
 * The floor intel_calculate_wm applies last, after the maximum and after
 * the default: a burst is eight cachelines, so a watermark below eight is
 * one the fetch cannot honour whatever the arithmetic says.
 *
 * This file's first cut let an active plane fall to V9X_I9XX_WM_DEFAULT and
 * the host tests expected it, which is not what the implementation it cites
 * does (review of 05d47b5). It changes nothing at the netbook's 20; it
 * changes what this helper would hand a mode with less room.
 */
#define V9X_I9XX_WM_MIN_BURST     ((v9x_u32)8ul)

/*
 * The watermark for one plane.
 *
 * pixel_rate_khz is htotal * vtotal * refresh / 1000, cpp the bytes per
 * pixel, fifo_size the plane's share of the FIFO from DSPARB, latency_ns
 * the memory latency to assume. Returns the value to program, clamped to
 * V9X_I9XX_WM_MAX above and to V9X_I9XX_WM_MIN_BURST below - a watermark of
 * zero is one no fetch ever satisfies, and one below a burst is one the
 * fetch cannot honour.
 *
 * Zero pixel_rate_khz or cpp means the plane is not driving anything, and
 * the answer is the whole FIFO less the guard, which is what i915 gives an
 * inactive plane. The burst floor does NOT apply there: an idle plane does
 * not go through intel_calculate_wm at all.
 */
v9x_u32 v9x_i9xx_wm_plane(v9x_u32 pixel_rate_khz, v9x_u32 cpp,
                          v9x_u32 fifo_size, v9x_u32 latency_ns);

/*
 * FW_BLC as i915 composes it: plane A in bits 5:0, plane B in 21:16, and
 * the two burst-length bits at 8 and 24 that say a fetch is eight
 * cachelines.
 */
v9x_u32 v9x_i9xx_wm_fw_blc(v9x_u32 plane_a_wm, v9x_u32 plane_b_wm);

/*
 * The per-plane FIFO sizes DSPARB describes. Plane A runs from zero to the
 * B start; plane B from there to the C start. Returns V9X_FALSE and leaves
 * the outputs alone if the register describes no room for a plane, which
 * is not a partition this driver should compute against.
 */
v9x_u16 v9x_i9xx_wm_fifo_split(v9x_u32 dsparb, v9x_u32 *plane_a,
                               v9x_u32 *plane_b);

/*
 * The bits of FW_BLC this driver claims: the two watermarks and the two
 * burst lengths. Everything else in the register is left as found.
 *
 * i915 writes the whole register, treating the rest as zero. This part's
 * BIOS leaves bit 25 set, which i915 never writes and nothing here
 * explains, and overwriting a bit whose meaning is unknown is not a thing
 * to do on a machine that has to be walked to. So the value programmed is
 * the existing one with these fields replaced.
 */
#define V9X_I9XX_WM_FW_BLC_MANAGED \
    ((v9x_u32)0x3ful | ((v9x_u32)1ul << 8) | \
     ((v9x_u32)0x3ful << 16) | ((v9x_u32)1ul << 24))

/*
 * FW_BLC as it should be written: `existing` with the managed fields
 * replaced by these watermarks. Returns the value to write.
 */
v9x_u32 v9x_i9xx_wm_fw_blc_merge(v9x_u32 existing, v9x_u32 plane_a_wm,
                                 v9x_u32 plane_b_wm);

/*
 * Bytes per pixel from DSPCNTR's format field, or zero if the field names
 * a format this does not know.
 *
 * The hardware's own answer, and it has to be: intel95 computed a watermark
 * from v9x_hal->fb.bits_per_pixel reading 32 while DSPCNTR said format 5
 * and the stride said two bytes a pixel, and programmed the live plane down
 * to 12 where 20 was right. The plane's control register is what the
 * scanout actually fetches by.
 *
 * Zero for an unknown format is deliberate. A caller that cannot name the
 * format must not compute a watermark for it, in the way
 * v9x_i9xx_wm_fifo_split declines a partition it cannot believe.
 */
v9x_u32 v9x_i9xx_wm_cpp_from_dspcntr(v9x_u32 dspcntr);

#endif /* VELOCITY9X_I9XX_WM_H */
