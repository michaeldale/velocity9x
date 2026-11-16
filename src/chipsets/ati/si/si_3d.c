/*
 * The Oland start of stream; see include\velocity9x\si_3d.h.
 */
#include "velocity9x/si_3d.h"
#include "velocity9x/si_clear_state.h"

/* sid.h: PACKET3_PREAMBLE_BEGIN/END_CLEAR_STATE. */
#define SI_PREAMBLE_BEGIN_CLEAR     0x20000000ul
#define SI_PREAMBLE_END_CLEAR       0x30000000ul
/* VGT_VERTEX_REUSE_BLOCK_CNTL (A316h) and VGT_OUT_DEALLOC_CNTL, the pair
 * cp_gfx_start writes after CLEAR_STATE. */
#define SI_VGT_VERTEX_REUSE         0xa316ul
#define SI_VGT_VERTEX_REUSE_VALUE   0x0000000eul
#define SI_VGT_OUT_DEALLOC_VALUE    0x00000010ul

void v9x_si_3d_start(struct v9x_vi_pm4 *stream)
{
    v9x_u32 run;
    v9x_u32 first = 0ul;
    v9x_u32 pair[2];

    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_PREAMBLE_CNTL, 0ul));
    v9x_vi_pm4_put(stream, SI_PREAMBLE_BEGIN_CLEAR);
    for (run = 0ul; run < V9X_SI_CLEAR_STATE_RUNS; ++run) {
        v9x_vi_pm4_set_context(stream, v9x_si_clear_state_runs[run].first,
                               v9x_si_clear_state_values + first,
                               v9x_si_clear_state_runs[run].count);
        first += v9x_si_clear_state_runs[run].count;
    }
    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_PREAMBLE_CNTL, 0ul));
    v9x_vi_pm4_put(stream, SI_PREAMBLE_END_CLEAR);

    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_CLEAR_STATE, 0ul));
    v9x_vi_pm4_put(stream, 0ul);
    pair[0] = SI_VGT_VERTEX_REUSE_VALUE;
    pair[1] = SI_VGT_OUT_DEALLOC_VALUE;
    v9x_vi_pm4_set_context(stream, SI_VGT_VERTEX_REUSE, pair, 2ul);
}
