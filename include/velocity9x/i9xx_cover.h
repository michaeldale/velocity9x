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
    /*
     * DSPCNTR's format field itself, bits 29:26, kept because the byte
     * width does not determine the layout: two bytes is 555 or 565 and
     * four is 8888 or 1010102, and a writer that guessed from the width
     * produced wrong colours for every format but the netbook's.
     */
    v9x_u32 format;
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

/*
 * WHEN a capture happens, kept apart from the results it produces.
 *
 * Two defects made this worth separating. Arming cleared the records and the
 * image status directly, and the sampler ran BEFORE the arming path on the
 * same flip - so the first flip after a mode change wrote its image and its
 * statistics and then had them erased by the arm that followed. And the
 * desktop-restoration flip goes through the same arming path, so leaving a
 * benchmark cleared the evidence it had just collected while capturing
 * nothing to replace it.
 *
 * So arming only SCHEDULES. The results are replaced when a new capture is
 * actually taken, which is at the top of the sampler, and the image's status
 * and identity survive until a new image is successfully written. Applying
 * the schedule inside the sampler is also what makes the ordering
 * irrelevant: it cannot run after the thing it gates.
 */
struct v9x_i9xx_cover_state {
    v9x_u32 rearm;          /* a capture has been asked for, not yet begun */
    v9x_u32 image_wanted;   /* and it should replace the image too         */
    v9x_u32 records;        /* records written since the last begin        */
    v9x_u32 attempts;       /* image attempts since the last begin         */
};

/* Ask for a new capture. Nothing is discarded here. */
void v9x_i9xx_cover_request(struct v9x_i9xx_cover_state *state);

/*
 * Whether a finished image attempt should replace the retained identity.
 *
 * True only when the write succeeded. A failed replacement leaves the
 * previous image's status, sequence and session exactly as they were, which
 * is the whole point: metadata that survives a failure must describe a file
 * that survived it too.
 */
v9x_u16 v9x_i9xx_cover_commit_image(struct v9x_i9xx_cover_state *state,
                                    v9x_u32 status);

/*
 * Do two plans describe the same bytes?
 *
 * The second read of a buffer must sample exactly what the first did. A mode
 * change between them alters the dimensions, the pitch or the pixel format
 * without any DriverInit, so a plan rebuilt from the registers at recheck
 * time can walk different memory and report a different count - which would
 * read as the engine having written late when nothing of the sort happened.
 *
 * The step is compared too: it decides which pixels are visited, so two
 * plans that agree on the surface and differ on the step do not agree on
 * the sample.
 */
v9x_u16 v9x_i9xx_cover_plan_same(const struct v9x_i9xx_cover_plan *first,
                                 const struct v9x_i9xx_cover_plan *second);

/*
 * Begin a sample. Applies any pending request, which is the point at which
 * the previous run's records are dropped - and only then. Returns V9X_TRUE
 * if this sample should be recorded, which is false once the slots are
 * full and no new capture has been asked for.
 */
v9x_u16 v9x_i9xx_cover_begin(struct v9x_i9xx_cover_state *state,
                             v9x_u32 slots);

#endif /* VELOCITY9X_I9XX_COVER_H */
