/*
 * Whether to keep waiting for a 3D-done bit. See include\velocity9x\donewait.h
 * for why this is a module of its own and why the rule only ever runs one way.
 */
#include "velocity9x/donewait.h"

void v9x_done_wait_reset(struct v9x_done_wait *state)
{
    if (state == 0) {
        return;
    }

    state->misses = (v9x_u32)0ul;
    state->ever_seen = (v9x_u16)0u;
    state->given_up = (v9x_u16)0u;
}

int v9x_done_wait_should_spin(const struct v9x_done_wait *state)
{
    if (state == 0) {
        return 1;
    }

    return state->given_up == (v9x_u16)0u;
}

void v9x_done_wait_seen(struct v9x_done_wait *state)
{
    if (state == 0) {
        return;
    }

    /*
     * One sighting is proof the bit exists on this part, and it retires the
     * question for good: a later miss is the gap the spin was written for, not
     * evidence of a chip without the bit.
     */
    state->ever_seen = (v9x_u16)1u;
    state->given_up = (v9x_u16)0u;
    state->misses = (v9x_u32)0ul;
}

void v9x_done_wait_missed(struct v9x_done_wait *state)
{
    if (state == 0) {
        return;
    }

    /*
     * Misses are counted on a part that has answered before, because the
     * count is worth reading, but they decide nothing there.
     */
    if (state->misses < (v9x_u32)0xfffffffful) {
        ++state->misses;
    }
    if (state->ever_seen != (v9x_u16)0u) {
        return;
    }
    if (state->misses >= V9X_DONE_WAIT_GIVE_UP) {
        state->given_up = (v9x_u16)1u;
    }
}
