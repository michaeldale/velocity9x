/*
 * The Mullins start of stream; see include\velocity9x\cik_3d.h.
 */
#include "velocity9x/cik_3d.h"
#include "velocity9x/cik_clear_state.h"

/* cikd.h: PACKET3_PREAMBLE_BEGIN/END_CLEAR_STATE, CE_PARTITION_BASE. */
#define CIK_PREAMBLE_BEGIN_CLEAR    0x20000000ul
#define CIK_PREAMBLE_END_CLEAR      0x30000000ul
#define CIK_CONTEXT_CONTROL_LOAD    0x80000000ul
#define CIK_CE_PARTITION_BASE       3ul
#define CIK_CE_PARTITION_SIZE       0x8000ul
/* PA_SC_RASTER_CONFIG and _1: gfx_v7_0_raster_config's CHIP_MULLINS. */
#define CIK_PA_SC_RASTER_CONFIG     0xa0d4ul
#define CIK_RASTER_CONFIG           0x00000000ul
#define CIK_RASTER_CONFIG_1         0x00000000ul
/* VGT_VERTEX_REUSE_BLOCK_CNTL (A316h) and VGT_OUT_DEALLOC_CNTL, the pair
 * cp_gfx_start writes after CLEAR_STATE. */
#define CIK_VGT_VERTEX_REUSE        0xa316ul
#define CIK_VGT_VERTEX_REUSE_VALUE  0x0000000eul
#define CIK_VGT_OUT_DEALLOC_VALUE   0x00000010ul

void v9x_cik_3d_start(struct v9x_vi_pm4 *stream)
{
    v9x_u32 run;
    v9x_u32 first = 0ul;
    v9x_u32 pair[2];

    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_SET_BASE, 2ul));
    v9x_vi_pm4_put(stream, CIK_CE_PARTITION_BASE);
    v9x_vi_pm4_put(stream, CIK_CE_PARTITION_SIZE);
    v9x_vi_pm4_put(stream, CIK_CE_PARTITION_SIZE);

    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_PREAMBLE_CNTL, 0ul));
    v9x_vi_pm4_put(stream, CIK_PREAMBLE_BEGIN_CLEAR);
    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_CONTEXT_CONTROL, 1ul));
    v9x_vi_pm4_put(stream, CIK_CONTEXT_CONTROL_LOAD);
    v9x_vi_pm4_put(stream, CIK_CONTEXT_CONTROL_LOAD);
    for (run = 0ul; run < V9X_CIK_CLEAR_STATE_RUNS; ++run) {
        v9x_vi_pm4_set_context(stream, v9x_cik_clear_state_runs[run].first,
                               v9x_cik_clear_state_values + first,
                               v9x_cik_clear_state_runs[run].count);
        first += v9x_cik_clear_state_runs[run].count;
    }
    pair[0] = CIK_RASTER_CONFIG;
    pair[1] = CIK_RASTER_CONFIG_1;
    v9x_vi_pm4_set_context(stream, CIK_PA_SC_RASTER_CONFIG, pair, 2ul);
    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_PREAMBLE_CNTL, 0ul));
    v9x_vi_pm4_put(stream, CIK_PREAMBLE_END_CLEAR);

    v9x_vi_pm4_put(stream, V9X_VI_PACKET3(V9X_VI_P3_CLEAR_STATE, 0ul));
    v9x_vi_pm4_put(stream, 0ul);
    pair[0] = CIK_VGT_VERTEX_REUSE_VALUE;
    pair[1] = CIK_VGT_OUT_DEALLOC_VALUE;
    v9x_vi_pm4_set_context(stream, CIK_VGT_VERTEX_REUSE, pair, 2ul);
}
