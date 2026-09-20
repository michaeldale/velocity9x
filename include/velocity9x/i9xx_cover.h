/*
 * Planning a read of the presented surface, as arithmetic.
 *
 * The back-buffer sampler added on 2026-09-20 took its offset from the flip
 * and its width, height and pitch from the last Direct3D context lookup.
 * Those are not the same surface: a context can describe an offscreen
 * target, or an earlier mode, and the sampler would then walk the aperture
 * with one surface's geometry over another's memory. It also assumed every
 * surface was 16-bit, which the Gen3 plane does not require.
 *
 * So the layout comes from the SCANOUT registers that describe the buffer
 * actually being presented - the pipe source for its dimensions, DSPCNTR
 * for its pixel format, the plane's stride for its pitch - and this module
 * validates them before a single aperture read happens.
 *
 * The bounds arithmetic is here rather than inline because it is the
 * memory-safety check for a direct read of video memory, every step of it
 * has to avoid overflowing a 32-bit multiply, and the project's rule is
 * that such things are host-tested rather than reasoned about.
 */
#ifndef VELOCITY9X_I9XX_COVER_H
#define VELOCITY9X_I9XX_COVER_H

#include "velocity9x/types.h"

/*
 * A validated read plan. Every field is safe to use directly: the sampler
 * walks `rows` by `columns` at `step`, and every pixel it touches lies
 * inside the surface and inside video memory.
 */
struct v9x_i9xx_cover_plan {
    v9x_u32 width;              /* pixels across, from the pipe source     */
    v9x_u32 height;             /* pixels down                             */
    v9x_u32 pitch;              /* bytes per row, from the plane's stride  */
    v9x_u32 bytes_per_pixel;    /* 1, 2 or 4 - from DSPCNTR's format       */
    v9x_u32 step;               /* sample every step'th pixel, both axes   */
    v9x_u32 columns;            /* samples per sampled row                 */
    v9x_u32 rows;               /* sampled rows                            */
};

/*
 * Plan a sample of the surface at `byte_offset`, or refuse.
 *
 * Returns V9X_FALSE and leaves the plan alone if anything about the
 * described surface cannot be believed: a pixel format this driver cannot
 * read a pixel out of, a pitch too small for the width, a surface that
 * would run past the end of video memory, or a zero anywhere it matters.
 *
 * Refusing is the whole point. The alternative to a plan that cannot be
 * validated is not a slightly wrong statistic, it is a read of whatever
 * follows the framebuffer.
 */
v9x_u16 v9x_i9xx_cover_plan(v9x_u32 pipesrc, v9x_u32 dspcntr,
                            v9x_u32 stride, v9x_u32 byte_offset,
                            v9x_u32 vram_bytes, v9x_u32 step,
                            struct v9x_i9xx_cover_plan *plan);

#endif /* VELOCITY9X_I9XX_COVER_H */
