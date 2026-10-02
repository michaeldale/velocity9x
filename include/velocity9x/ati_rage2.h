/*
 * ATI Rage II class (264GT2C, the Rage IIC): trapezoid register encoding.
 *
 * The chip has no triangle setup engine. The 3D RAGE's drawing primitive is
 * the trapezoid: the line engine walks the leading edge, a second Bresenham
 * engine walks the trailing edge, and the pixels between the two on each
 * scanline are drawn. A write to DST_BRES_LNTH with DRAW_TRAP set starts it.
 * docs\specifications\ati-rage2-3d-engine.md has the register notes and
 * their sources; nothing in them was measured when this was written.
 *
 * This module only encodes: a described trapezoid in, an ordered
 * (offset, value) stream out, bounds refused rather than clamped. It does
 * not compute Bresenham terms from edge endpoints, because how the engine
 * counts the leading-edge length and which span pixels it includes are
 * what the Phase 2 scenes measure (docs\plans\ati-rage-iic-hardware-3d.md).
 *
 * Offsets are 4 KiB-window offsets, block 0 at +400h, as for the Mach64.
 */
#ifndef VELOCITY9X_ATI_RAGE2_H
#define VELOCITY9X_ATI_RAGE2_H

#include "velocity9x/ati_mach64_engine.h"

/* GT trapezoid registers (RRG-G02700 p.4-46, 4-59; atiregs.h "GT"). */
#define V9X_R2_DST_BRES_LNTH        0x00000520ul  /* 0_48, the trigger */
#define V9X_R2_TRAIL_BRES_ERR       0x00000538ul  /* 0_4E */
#define V9X_R2_TRAIL_BRES_INC       0x0000053cul  /* 0_4F */
#define V9X_R2_TRAIL_BRES_DEC       0x00000540ul  /* 0_50 */

/* DST_BRES_LNTH fields (RRG p.4-46). */
#define V9X_R2_LNTH_LOAD_TRAIL      0x80000000ul  /* bit 31: load TRAIL_X */
#define V9X_R2_LNTH_DRAW_TRAP       0x00008000ul  /* bit 15: draw */
#define V9X_R2_LNTH_TRAIL_X_SHIFT   16u           /* 28:16 */
#define V9X_R2_LNTH_LENGTH_MASK     0x00007ffful  /* 14:0 */

/* DST_CNTL bits a trapezoid may use (atiregs.h; RRG p.4-48). X_DIR and
 * Y_DIR are the Mach64 ones in ati_mach64_regs.h. */
#define V9X_R2_DST_Y_MAJOR          0x00000004ul
#define V9X_R2_DST_BRES_SIGN        0x00000800ul
#define V9X_R2_TRAIL_X_DIR          0x00002000ul
#define V9X_R2_TRAP_FILL_DIR        0x00004000ul
#define V9X_R2_TRAIL_BRES_SIGN      0x00008000ul
#define V9X_R2_BRES_SIGN_AUTO       0x00020000ul
#define V9X_R2_TRAP_DST_CNTL_MASK   (V9X_M64_DST_X_DIR | V9X_M64_DST_Y_DIR | \
    V9X_R2_DST_Y_MAJOR | V9X_R2_DST_BRES_SIGN | V9X_R2_TRAIL_X_DIR |     \
    V9X_R2_TRAP_FILL_DIR | V9X_R2_TRAIL_BRES_SIGN | V9X_R2_BRES_SIGN_AUTO)

/* Field limits: X 13 bits, Y 15 bits, terms signed 18 bits (RRG). The
 * length cap is this module's, not the chip's: the first scenes are small
 * on purpose, so a mis-modelled length cannot sweep the aperture. */
#define V9X_R2_X_MAX                8191ul
#define V9X_R2_BRES_MIN             (-131072l)
#define V9X_R2_BRES_MAX             131071l
#define V9X_R2_BRES_MASK            0x0003fffful
#define V9X_R2_TRAP_LENGTH_MAX      64ul

#define V9X_R2_FLAT_STATE_DWORDS    11ul
#define V9X_R2_TRAP_DWORDS          9ul

/* A 16-bpp destination surface in VRAM and the scissor over it, in the
 * surface's own pixel coordinates, inclusive. */
struct v9x_r2_target {
    v9x_u32 offset;
    v9x_u32 pitch_bytes;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 vram_bytes;
    v9x_u32 scissor_left;
    v9x_u32 scissor_top;
    v9x_u32 scissor_right;
    v9x_u32 scissor_bottom;
};

/* One flat trapezoid, as raw register terms. */
struct v9x_r2_flat_trap {
    v9x_u32 x;          /* leading edge start, DST_Y_X */
    v9x_u32 y;
    v9x_u32 length;     /* DST_BRES_LNTH[14:0] */
    v9x_u32 trail_x;    /* DST_BRES_LNTH[28:16] */
    v9x_s32 lead_err;
    v9x_s32 lead_inc;
    v9x_s32 lead_dec;
    v9x_s32 trail_err;
    v9x_s32 trail_inc;
    v9x_s32 trail_dec;
    v9x_u32 dst_cntl;   /* only V9X_R2_TRAP_DST_CNTL_MASK bits */
};

/*
 * Triangle setup (rage2_setup.c): a screen-space triangle into at most two
 * trapezoids whose engine coverage is exactly centre sampling with the
 * top-left rule. Vertices are fixed point, V9X_R2_SUBPIXEL per pixel.
 * Coordinates must lie in [0, 2047] pixels, which keeps every product in
 * the setup inside 31 bits and every edge term inside the 18-bit fields.
 *
 * The edge terms follow the walk measured on A8U4I5
 * (docs\decisions\2026-10-02-rage-iic-trapezoid-edge-model.md): per row
 * an edge steps while its error is >= 0, adding DEC, then adds INC. For an
 * edge spanning dyF by dxF sixteenths that is DEC = -dyF, INC = dxF, and
 * an ERR in [-dyF, 0) that carries the first row's rounding.
 */
#define V9X_R2_SUBPIXEL             16l
#define V9X_R2_SETUP_COORD_MAX      (2047l * V9X_R2_SUBPIXEL)
#define V9X_R2_SETUP_TRAPS          2u

struct v9x_r2_vertex {
    v9x_s32 x;
    v9x_s32 y;
};

/* The trapezoids for one flat triangle inside `target` (its whole width
 * and height, not only the scissor). *count is 0 for a triangle that
 * covers no pixel centre. Only trajectory fields are filled; the colour
 * and datapath come from v9x_r2_build_flat_state. */
v9x_status v9x_r2_setup_triangle(const struct v9x_r2_target *target,
                                 const struct v9x_r2_vertex *vertices,
                                 struct v9x_r2_flat_trap *traps,
                                 v9x_u32 *count);

/* The datapath for a flat-coloured 2D-path trapezoid: 3D pipe off,
 * DP_FRGD_CLR as the source, the target and its scissor. */
v9x_status v9x_r2_build_flat_state(const struct v9x_r2_target *target,
                                   v9x_u32 color,
                                   v9x_u32 *offsets, v9x_u32 *values,
                                   v9x_u32 capacity, v9x_u32 *written);

/* The trajectory and its trigger, which is always the last write. */
v9x_status v9x_r2_build_trap(const struct v9x_r2_target *target,
                             const struct v9x_r2_flat_trap *trap,
                             v9x_u32 *offsets, v9x_u32 *values,
                             v9x_u32 capacity, v9x_u32 *written);

#endif
