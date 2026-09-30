/*
 * The batches a merge of consecutive DrawPrimitives records would build.
 *
 * Half-Life sends its world as DrawPrimitives buffers of small records -
 * about two triangles each, one list call per record - and in-call merging
 * inside one list call cannot touch them
 * (docs/decisions/2026-09-30-halflife-batches-are-small-records-not-culling.md).
 * Merging consecutive records is the remaining in-call lever, and it is only
 * sound where no state change separates them. This counts how many batches
 * such a merge would leave, before anything is built.
 *
 * Header-only and static, so the D3D core and tests\host\test_r3d_runs.c
 * each compile their own copy and no new external symbol is created. It
 * includes nothing from the DDHAL side.
 */
#ifndef VELOCITY9X_R3D_RUNS_H
#define VELOCITY9X_R3D_RUNS_H

#include "velocity9x/types.h"

/*
 * Account one record against the run being built. `run_triangles` is the
 * caller's run state: zero at the start of each buffer and after a record
 * that was not drawn. Returns V9X_TRUE when the record would start a new
 * batch.
 *
 * A record joins the run only when it carries no state changes (its pairs
 * are applied before its vertices, so any pair ends the run) and the joined
 * run stays within `capacity`, the engine's triangle bound.
 */
static v9x_u16 v9x_r3d_record_run_step(v9x_u32 *run_triangles,
                                       v9x_u32 state_changes,
                                       v9x_u32 triangles,
                                       v9x_u32 capacity)
{
    /* `*run_triangles < capacity` first: a record already over the bound
     * stands alone, and the subtraction below must not wrap. */
    if (state_changes == 0ul && *run_triangles != 0ul &&
        *run_triangles < capacity &&
        triangles <= capacity - *run_triangles) {
        *run_triangles += triangles;
        return V9X_FALSE;
    }

    *run_triangles = triangles;
    return V9X_TRUE;
}

#endif
