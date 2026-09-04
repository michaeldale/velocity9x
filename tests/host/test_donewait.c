/*
 * Tests for the 3D-done wait's give-up rule.
 *
 * The engine reports what each idle wait saw and asks whether the next one
 * should spin for SUBSYS_STAT bit 1; every rule about that lives in
 * src\common\donewait.c and is decided here. The two machines this was written
 * for are both in the table below: the emulated ViRGE/DX answers every wait,
 * and the S3 Trio3D/2X answered none of 117 probe cells' worth.
 *
 * The property that matters is that the rule runs one way. Giving up on a part
 * that does have the bit would put back the read-too-early gap the wait exists
 * to close, so one sighting has to be enough to keep spinning forever after.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/donewait.h"

static unsigned int donewait_failures = 0u;

#define DWCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++donewait_failures; \
    } \
} while (0)

/* A fresh state spins, has seen nothing and has given up on nothing. */
static void test_starts_willing(void)
{
    struct v9x_done_wait state;

    memset(&state, 0xff, sizeof(state));
    v9x_done_wait_reset(&state);
    DWCHECK(v9x_done_wait_should_spin(&state));
    DWCHECK(state.misses == (v9x_u32)0ul);
    DWCHECK(state.ever_seen == (v9x_u16)0u);
    DWCHECK(state.given_up == (v9x_u16)0u);
}

/*
 * The Trio3D/2X: never a sighting. The rule holds until the limit and then
 * stops asking - one wait before the limit it must still be spinning, because
 * a rule that fires early would cost the emulator its correctness.
 */
static void test_gives_up_only_at_the_limit(void)
{
    struct v9x_done_wait state;
    v9x_u32 i;

    v9x_done_wait_reset(&state);
    for (i = (v9x_u32)0ul; i < V9X_DONE_WAIT_GIVE_UP - (v9x_u32)1ul; ++i) {
        v9x_done_wait_missed(&state);
        DWCHECK(v9x_done_wait_should_spin(&state));
    }
    v9x_done_wait_missed(&state);
    DWCHECK(!v9x_done_wait_should_spin(&state));
    DWCHECK(state.given_up != (v9x_u16)0u);
    DWCHECK(state.ever_seen == (v9x_u16)0u);

    /* And it stays given up: no amount of further misses undoes it. */
    for (i = (v9x_u32)0ul; i < (v9x_u32)1000ul; ++i) {
        v9x_done_wait_missed(&state);
    }
    DWCHECK(!v9x_done_wait_should_spin(&state));
}

/*
 * The emulated ViRGE/DX: the bit arrives. Misses after that are the gap the
 * spin was written for, and they must never accumulate into a give-up, however
 * many of them there are.
 */
static void test_one_sighting_is_forever(void)
{
    struct v9x_done_wait state;
    v9x_u32 i;

    v9x_done_wait_reset(&state);
    v9x_done_wait_seen(&state);
    DWCHECK(state.ever_seen != (v9x_u16)0u);
    for (i = (v9x_u32)0ul; i < V9X_DONE_WAIT_GIVE_UP * (v9x_u32)10ul; ++i) {
        v9x_done_wait_missed(&state);
        DWCHECK(v9x_done_wait_should_spin(&state));
    }
    DWCHECK(state.given_up == (v9x_u16)0u);
}

/*
 * A part that misses nearly enough to decide, then answers, is a part with the
 * bit. The streak resets and the count starts again from nothing.
 */
static void test_a_sighting_clears_the_streak(void)
{
    struct v9x_done_wait state;
    v9x_u32 i;

    v9x_done_wait_reset(&state);
    for (i = (v9x_u32)0ul; i < V9X_DONE_WAIT_GIVE_UP - (v9x_u32)1ul; ++i) {
        v9x_done_wait_missed(&state);
    }
    v9x_done_wait_seen(&state);
    DWCHECK(state.misses == (v9x_u32)0ul);
    DWCHECK(v9x_done_wait_should_spin(&state));
    for (i = (v9x_u32)0ul; i < V9X_DONE_WAIT_GIVE_UP; ++i) {
        v9x_done_wait_missed(&state);
        DWCHECK(v9x_done_wait_should_spin(&state));
    }
}

/*
 * Reset is what a mode set or a new adapter gets: the question is asked again
 * from the beginning, even on a state that had given up.
 */
static void test_reset_asks_again(void)
{
    struct v9x_done_wait state;
    v9x_u32 i;

    v9x_done_wait_reset(&state);
    for (i = (v9x_u32)0ul; i < V9X_DONE_WAIT_GIVE_UP; ++i) {
        v9x_done_wait_missed(&state);
    }
    DWCHECK(!v9x_done_wait_should_spin(&state));
    v9x_done_wait_reset(&state);
    DWCHECK(v9x_done_wait_should_spin(&state));
}

/* Every entry point tolerates a null state and none of them decides on one. */
static void test_null_is_harmless(void)
{
    v9x_done_wait_reset(0);
    v9x_done_wait_seen(0);
    v9x_done_wait_missed(0);
    DWCHECK(v9x_done_wait_should_spin(0));
}

/*
 * The counter is read as evidence, so it must not wrap on a long-running
 * desktop: at the ceiling it stops rather than returning to zero, which would
 * make a part that never answers look like one that had just started.
 */
static void test_the_count_saturates(void)
{
    struct v9x_done_wait state;

    v9x_done_wait_reset(&state);
    state.misses = (v9x_u32)0xfffffffful;
    state.ever_seen = (v9x_u16)1u;
    v9x_done_wait_missed(&state);
    DWCHECK(state.misses == (v9x_u32)0xfffffffful);
}

unsigned int v9x_run_donewait_tests(void)
{
    test_starts_willing();
    test_gives_up_only_at_the_limit();
    test_one_sighting_is_forever();
    test_a_sighting_clears_the_streak();
    test_reset_asks_again();
    test_null_is_harmless();
    test_the_count_saturates();
    return donewait_failures;
}
