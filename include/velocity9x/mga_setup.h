/*
 * Matrox MGA-2164W triangle setup: one screen-space triangle into at most
 * two trapezoids for mga_3d.c, split at the middle vertex.
 *
 * Phase 4 of docs\plans\matrox-mga2164w-hardware-3d.md. The coverage
 * contract is the one the software rasterizer and the Rage IIC setup keep:
 * a pixel is drawn when its centre, (x + 1/2, y + 1/2), is inside, and a
 * centre exactly on an edge belongs to a left or top edge only. Each edge's
 * column on every row is computed exactly and handed to the engine as the
 * Bresenham terms whose walk lands on it (the walk measured in
 * docs\decisions\2026-10-10-mga2164w-trapezoids.md).
 *
 * Colour, depth and the texture's s, t, q are planes over the triangle,
 * given to the engine as their value at the trapezoid's first pixel and
 * their change per pixel and per row; the engine adds the left edge's
 * column steps itself (same record). Perspective texturing pre-shifts s
 * and t by the 1/8 texel the engine adds before its floor
 * (docs\decisions\2026-10-10-mga2164w-textures.md).
 *
 * Pure policy, no I/O. Doubles for the planes, integers for coverage; no
 * float-to-integer cast (the HAL links no runtime that would serve one).
 */
#ifndef VELOCITY9X_MGA_SETUP_H
#define VELOCITY9X_MGA_SETUP_H

#include "velocity9x/mga_3d.h"

/* Vertex positions in sixteenths of a pixel, on [0, 2047] pixels: every
 * product in the edge setup stays inside 31 bits. */
#define V9X_MGA_SETUP_SUBPIXEL  16L
#define V9X_MGA_SETUP_COORD_MAX (2047L * V9X_MGA_SETUP_SUBPIXEL)
#define V9X_MGA_SETUP_TRAPS     2u

/*
 * One vertex. z is in the 16-bit Z buffer's units (0 to 65535); red, green
 * and blue 0 to 255; u and v the texture coordinates, 1.0 spanning the
 * texture; q is 1/w at any positive scale (only ratios between the three
 * vertices matter).
 */
struct v9x_mga_setup_vertex {
    v9x_s32 x;
    v9x_s32 y;
    double z;
    double red;
    double green;
    double blue;
    double u;
    double v;
    double q;
};

/*
 * The trapezoids for one triangle. Everything in `base` - target, depth,
 * texture, shading, FCOL, stipple - is copied to each; the setup supplies
 * top, length, the two edges, the colour planes (Gouraud), the Z plane
 * (16-bit depth) and TMR0-TMR8 (texture). *count is 0 for a triangle that
 * covers no pixel centre. INVALID_ARGUMENT for a vertex out of range;
 * UNSUPPORTED for one whose planes overflow the engine's fields (a sliver
 * whose depth changes by tens of thousands of units a pixel, for example).
 * 32-bit depth is not set up.
 */
v9x_status v9x_mga_setup_triangle(const struct v9x_mga3d_trap *base,
                                  const struct v9x_mga_setup_vertex *vertices,
                                  struct v9x_mga3d_trap *traps,
                                  v9x_u32 *count);

#endif
