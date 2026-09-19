/*
 * When the present trace should record a draw, as pure logic.
 *
 * The trace keeps one record per frame for the first draw batch after an
 * accepted flip, because the ring is short and a frame is hundreds of
 * batches. Deciding which batch that is turned out to be three separate
 * mistakes in one day (2026-09-19), each of which made the instrument
 * report something that had not happened:
 *
 *   - recorded before the backend ran, so a batch that correctly WAITED
 *     for a pending flip was written between the flip and its completion,
 *     which is the pattern the trace exists to call premature reuse;
 *   - recorded whatever the backend returned, so a refused batch claimed a
 *     submission and consumed the marker, hiding the real one;
 *   - recorded on the backend's SUCCESS, which is not the same as having
 *     submitted anything. The ViRGE path returns success for a degenerate
 *     triangle, a thin one, and a blend its unit cannot express, so a whole
 *     batch can succeed having launched no command at all; and it returns
 *     failure from the middle of a batch whose earlier triangles DID
 *     launch.
 *
 * So the rule takes neither the return value nor the ordering on trust: a
 * draw is recorded only when the marker is armed AND the backend actually
 * launched something, which the caller establishes by watching a
 * submission count across the call. Success and failure do not enter into
 * it. This lives here, and is tested here, because the wrapper it governs
 * sits in the HAL among DDHAL types and MMIO and cannot be tested at all.
 */
#ifndef VELOCITY9X_DRAWNOTE_H
#define VELOCITY9X_DRAWNOTE_H

#include "velocity9x/types.h"

struct v9x_draw_note {
    v9x_u16 armed;
};

/* No flip seen yet: nothing to record. */
void v9x_draw_note_reset(struct v9x_draw_note *state);

/* An accepted flip arms the marker for the draw that follows it. */
void v9x_draw_note_flip(struct v9x_draw_note *state);

/*
 * Whether this batch is the one to record, given whether the backend
 * launched anything. Non-zero also CONSUMES the marker, so the next batch
 * is not recorded until another flip arms it; zero leaves the marker where
 * it was, which is what keeps a refused or empty batch from spending it.
 */
int v9x_draw_note_should_record(struct v9x_draw_note *state, int submitted);

#endif /* VELOCITY9X_DRAWNOTE_H */
