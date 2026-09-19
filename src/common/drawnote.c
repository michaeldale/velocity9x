/*
 * The present trace's record rule. See include\velocity9x\drawnote.h for why
 * this is a module of its own.
 */
#include "velocity9x/drawnote.h"

void v9x_draw_note_reset(struct v9x_draw_note *state)
{
    if (state == 0) {
        return;
    }

    state->armed = (v9x_u16)0u;
}

void v9x_draw_note_flip(struct v9x_draw_note *state)
{
    if (state == 0) {
        return;
    }

    state->armed = (v9x_u16)1u;
}

int v9x_draw_note_should_record(struct v9x_draw_note *state, int submitted)
{
    if (state == 0) {
        return 0;
    }

    /*
     * Both conditions, and the backend's return value is not one of them.
     * A batch that launched nothing leaves the marker armed so the real
     * first draw of the frame is the one recorded; a batch that launched
     * and then failed has still submitted, and spends it.
     */
    if (state->armed == (v9x_u16)0u || submitted == 0) {
        return 0;
    }

    state->armed = (v9x_u16)0u;
    return 1;
}
