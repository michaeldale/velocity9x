/*
 * The GFX8 clear-state context registers, generated into
 * src\chipsets\ati\vi\vi_clear_state.c by scripts\gen-carrizo-clear-state.ps1
 * from Linux amdgpu's clearstate_vi.h (MIT; the notice is in that file).
 */
#ifndef VELOCITY9X_VI_CLEAR_STATE_H
#define VELOCITY9X_VI_CLEAR_STATE_H

#include "velocity9x/status.h"

/* vi_SECT_CONTEXT_defs: seven runs, 885 registers. */
#define V9X_VI_CLEAR_STATE_RUNS   7u
#define V9X_VI_CLEAR_STATE_DWORDS 885u

/* One run of consecutive context registers: the dword index of the first
 * (0xa000 and up) and how many. */
struct v9x_vi_clear_state_run {
    v9x_u32 first;
    v9x_u32 count;
};

/* Every run's values, one after another, in table order. */
extern const v9x_u32 v9x_vi_clear_state_values[V9X_VI_CLEAR_STATE_DWORDS];
extern const struct v9x_vi_clear_state_run
    v9x_vi_clear_state_runs[V9X_VI_CLEAR_STATE_RUNS];

#endif
