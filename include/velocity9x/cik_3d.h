/*
 * The Mullins (GFX7) parts of the 3D stream that differ from Carrizo's.
 *
 * Every context and SH register Carrizo's stream builder (vi_3d.c) writes
 * has the same index and name in Linux's gfx_7_2_d.h, so the draws are
 * Carrizo's. What is not is the start of the stream, which gfx_v7_0's
 * cp_gfx_start orders differently and fills from CI's clear state.
 */
#ifndef VELOCITY9X_CIK_3D_H
#define VELOCITY9X_CIK_3D_H

#include "velocity9x/vi_3d.h"

/*
 * gfx_v7_0_cp_gfx_start's stream, in its order: SET_BASE for the CE
 * partitions, PREAMBLE_CNTL begin, CONTEXT_CONTROL, ci_cs_data's context
 * registers (cik_clear_state.c), PA_SC_RASTER_CONFIG 0, 0 (Mullins),
 * PREAMBLE_CNTL end, CLEAR_STATE, and VGT_VERTEX_REUSE_BLOCK_CNTL /
 * VGT_OUT_DEALLOC_CNTL. The counterpart of v9x_vi_3d_start.
 */
void v9x_cik_3d_start(struct v9x_vi_pm4 *stream);

#endif
