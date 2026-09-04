/*
 * Whether to keep waiting for a 3D-done bit: the decision half.
 *
 * The S3D engine sets SUBSYS_STAT bit 1 when its queued triangles are
 * finished, and `v9x_wait_idle` requires that bit before it reports idle,
 * because 86Box's ViRGE hands a launched triangle to a render thread and its
 * idle bit reads true in the gap before that thread wakes
 * (docs\decisions\2026-09-03-the-probe-matrix-and-the-3d-done-bit.md). When
 * the bit does not come the wait spins to a bound and then accepts idle
 * anyway - never a reset, because an engine that says it is idle is not one to
 * reset.
 *
 * On the S3 Trio3D/2X that bound is paid on every single wait and never
 * repaid: 117 of 117 probe cells wrote a `_Dmiss` delta, so the bit is not
 * something that part does
 * (docs\decisions\2026-09-04-the-trio3d-runs-the-matrix.md). Spinning 4,096
 * status reads per draw for a bit that will never arrive is a tax on every
 * triangle the card draws.
 *
 * So the wait learns. This header and src\common\donewait.c hold the rule and
 * nothing else: no I/O, no registers, no OS. The engine reports what each wait
 * saw and asks whether the next one should spin; every rule is host-tested,
 * which is the same split as include\velocity9x\mtrr.h.
 *
 * The rule is deliberately one-way. A part that has produced the bit even once
 * is never given up on - one sighting proves the bit is real there, and a
 * later miss is the emulator's gap or a genuine timeout, both of which the
 * spin exists to cover. Only a part that has missed every wait from the first,
 * for a long run of them, is decided against.
 */
#ifndef VELOCITY9X_DONEWAIT_H
#define VELOCITY9X_DONEWAIT_H

#include "velocity9x/types.h"

/*
 * Consecutive misses, with the bit never once seen, before the wait stops
 * asking for it. The probe's matrix issues around 240 waits and the emulated
 * ViRGE/DX answered every one of them, so a part that has missed this many in
 * a row while never answering is not being unlucky.
 */
#define V9X_DONE_WAIT_GIVE_UP ((v9x_u32)64ul)

struct v9x_done_wait {
    v9x_u32 misses;     /* consecutive waits that ended without the bit    */
    v9x_u16 ever_seen;  /* the bit has arrived at least once on this part  */
    v9x_u16 given_up;   /* stop spinning for it until the state is reset   */
};

/* Start of day: spin for the bit, having seen and missed nothing. */
void v9x_done_wait_reset(struct v9x_done_wait *state);

/* Non-zero while the next wait should spin for the bit. */
int v9x_done_wait_should_spin(const struct v9x_done_wait *state);

/* The bit arrived. This part has it; never give up on it again. */
void v9x_done_wait_seen(struct v9x_done_wait *state);

/* A wait ended without the bit. Enough of these from the start decides. */
void v9x_done_wait_missed(struct v9x_done_wait *state);

#endif
