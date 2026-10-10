/*
 * Matrox MGA-2164W 3D: trapezoids for the Direct3D engine.
 *
 * Phase 1 of docs\plans\matrox-mga2164w-hardware-3d.md: flat and Gouraud
 * trapezoids, no Z, no texture. Pure policy, no I/O, as mga_engine.h: the
 * caller writes what v9x_mga3d_build_trap returns, every dword in order,
 * the last to its +100h mirror. Register meanings and page citations are
 * in docs\specifications\mga2164w-3d-engine.md (the MGA-2164W Developer
 * Specification, whose trapezoid section is 4.5.5).
 *
 * v9x_mga3d_model_trap is the other half: what this module believes the
 * engine draws for the same trapezoid. The write probe (MGA2D.EXE /tri)
 * compares the two on the card; until a decision record says they agree,
 * the model is a hypothesis taken from the specification's edge formulas
 * and 86Box's emulation of them.
 *
 * Only the 2164W gets 3D. Nothing here checks the chip; the caller does.
 */
#ifndef VELOCITY9X_MGA_3D_H
#define VELOCITY9X_MGA_3D_H

#include "velocity9x/mga_engine.h"

/* Edge and interpolant registers (spec 2-6..2-7, 3-23..3-44). AR0, AR5,
 * DWGCTL, FCOL, SGN, FXBNDRY, YDSTLEN, PITCH and YDSTORG are in
 * mga_engine.h. */
#define V9X_MGA_AR1   0x1c64ul
#define V9X_MGA_AR2   0x1c68ul
#define V9X_MGA_AR4   0x1c70ul
#define V9X_MGA_AR6   0x1c78ul
#define V9X_MGA_DR4   0x1cd0ul
#define V9X_MGA_DR6   0x1cd8ul
#define V9X_MGA_DR7   0x1cdcul
#define V9X_MGA_DR8   0x1ce0ul
#define V9X_MGA_DR10  0x1ce8ul
#define V9X_MGA_DR11  0x1cecul
#define V9X_MGA_DR12  0x1cf0ul
#define V9X_MGA_DR14  0x1cf8ul
#define V9X_MGA_DR15  0x1cfcul

/* Colour interpolants are signed 9.15 in <23:0> (spec 3-45..3-53): one
 * colour level is 1 << 15. */
#define V9X_MGA3D_COLOR_ONE 0x00008000L

#define V9X_MGA3D_MAX_WRITES 24u

struct v9x_mga3d_writes {
    v9x_u32 offsets[V9X_MGA3D_MAX_WRITES];
    v9x_u32 values[V9X_MGA3D_MAX_WRITES];
    v9x_u32 count;
};

/*
 * One edge: it starts at column x on the trapezoid's first row and moves dx
 * columns over dy rows (dy >= 1), stepped by the engine's integer
 * Bresenham. The left edge's column is drawn; the right edge's is not
 * (spec 4-34: "the bottom and right edges exist just beyond the object's
 * extents").
 */
struct v9x_mga3d_edge {
    v9x_s32 x;
    v9x_s32 dx;
    v9x_s32 dy;
};

#define V9X_MGA3D_SHADE_FLAT    0ul
#define V9X_MGA3D_SHADE_GOURAUD 1ul

/*
 * Rows top .. top + length - 1 of a destination surface. Flat: every pixel
 * is `color`. Gouraud: red, green and blue each start at [0] on the left
 * edge of the first row and change by [1] per pixel and [2] per row
 * (signed 9.15); `color` is FCOL, whose top byte is the alpha stored at
 * 32 bpp (spec 3-62). Gouraud is atype I with zmode NOZCMP: the depth unit
 * compares nothing and writes nothing (spec 3-56).
 */
struct v9x_mga3d_trap {
    v9x_u32 vram_bytes;
    v9x_u32 target_offset;
    v9x_u32 pitch_bytes;
    v9x_u32 bytes_per_pixel;
    v9x_u32 top;
    v9x_u32 length;
    struct v9x_mga3d_edge left;
    struct v9x_mga3d_edge right;
    v9x_u32 shade;
    v9x_u32 color;
    v9x_s32 red[3];
    v9x_s32 green[3];
    v9x_s32 blue[3];
};

/*
 * How the per-row colour increment is taken, the question phase 1 asks.
 * EDGE: the start value follows the left edge, so a row whose edge moved
 * dx columns also gains dx times the per-pixel step (86Box's model).
 * NONE: the start value gains only the per-row step.
 */
#define V9X_MGA3D_FOLD_NONE 0ul
#define V9X_MGA3D_FOLD_EDGE 1ul

typedef void (*v9x_mga3d_plot_fn)(void *context, v9x_s32 x, v9x_u32 row,
                                  v9x_u32 pixel);

/* The register writes for one trapezoid. OK; INVALID_ARGUMENT for one the
 * surface does not hold; UNSUPPORTED for one the engine's fields or the
 * linearizer cannot express. */
v9x_status v9x_mga3d_build_trap(const struct v9x_mga3d_trap *trap,
                                struct v9x_mga3d_writes *writes);

/* The pixels the engine is believed to draw for the trapezoid, row by row
 * from 0, left to right. Gouraud pixels are given at 32 bpp only, as
 * 8:8:8 with FCOL's alpha; other depths return UNSUPPORTED for Gouraud,
 * since 16 bpp shading is dithered (spec 3-70). */
v9x_status v9x_mga3d_model_trap(const struct v9x_mga3d_trap *trap,
                                v9x_u32 fold, v9x_mga3d_plot_fn plot,
                                void *context);

#endif
