/*
 * The Oland (GFX6) parts of the 3D stream that differ from Carrizo's.
 *
 * Every context register Carrizo's stream builder (vi_3d.c) writes has the
 * same index and name in Linux's gfx_6_0_d.h (checked 2026-11-16). The
 * builder's GFX6 differences - no RSRC3, VGT_PRIMITIVE_TYPE in config
 * space, SI's fence - are switched by struct v9x_vi_pm4's gfx6 flag. What
 * is here is the start of the stream, which gfx_v6_0_cp_gfx_start orders
 * its own way and fills from SI's clear state.
 */
#ifndef VELOCITY9X_SI_3D_H
#define VELOCITY9X_SI_3D_H

#include "velocity9x/vi_3d.h"

/* The second half of gfx_v6_0_cp_gfx_start, in its order: PREAMBLE_CNTL
 * begin, si_cs_data's context registers (si_clear_state.c), PREAMBLE_CNTL
 * end, CLEAR_STATE, and VGT_VERTEX_REUSE_BLOCK_CNTL / VGT_OUT_DEALLOC_CNTL.
 * No CONTEXT_CONTROL and no raster configuration: Linux's SI start has
 * neither, and gpu_init writes PA_SC_RASTER_CONFIG, which the clear
 * state's runs do not cover. The first half, ME_INITIALIZE and the CE
 * partitions, must already have run. */
#define V9X_SI_START_DWORDS         (2ul + 7ul * 2ul + 882ul + 2ul + 2ul + 4ul)

void v9x_si_3d_start(struct v9x_vi_pm4 *stream);

#endif
