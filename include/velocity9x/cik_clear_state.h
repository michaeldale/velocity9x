/*
 * The GFX7 clear-state context registers for Mullins, generated into
 * src\chipsets\ati\cik\cik_clear_state.c by scripts\gen-mullins-clear-state.ps1
 * from Linux amdgpu's clearstate_ci.h (MIT; the notice is in that file).
 * The same seven runs as Carrizo's (vi_clear_state.h), so the run struct
 * is Carrizo's.
 */
#ifndef VELOCITY9X_CIK_CLEAR_STATE_H
#define VELOCITY9X_CIK_CLEAR_STATE_H

#include "velocity9x/vi_clear_state.h"

#define V9X_CIK_CLEAR_STATE_RUNS   7u
#define V9X_CIK_CLEAR_STATE_DWORDS 885u

extern const v9x_u32 v9x_cik_clear_state_values[V9X_CIK_CLEAR_STATE_DWORDS];
extern const struct v9x_vi_clear_state_run
    v9x_cik_clear_state_runs[V9X_CIK_CLEAR_STATE_RUNS];

#endif
