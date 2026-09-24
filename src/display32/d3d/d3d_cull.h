/*
 * Back-face culling, decided in the core before any engine sees a triangle.
 *
 * A leaf translation unit like d3d_zfixed.h: it includes nothing from the
 * DDHAL side, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_cull.c holds it to its cases. It sits under
 * src\display32\d3d rather than src\common because D3DRENDERSTATE_CULLMODE is
 * Direct3D vocabulary, which belongs to the core half of the core/engine
 * split (docs\decisions\2026-08-29-d3d-core-engine-split.md).
 *
 * WHY IN SOFTWARE, and not the Gen3 S4 cull field that the Intel engine
 * already programs to NONE. Three reasons, each sufficient:
 *
 * - The hardware's CW/CCW naming is unmeasured on this part. Mesa's i915
 *   driver flips the S4 value with the framebuffer's y orientation, so which
 *   Direct3D winding each encoding removes is a thing to capture before it is
 *   a thing to rely on - and the allowlist decoder pins S4 to NONE exactly.
 * - The decision is the sign of one cross product on vertices the core
 *   already holds in screen space, so it is exact, and a triangle culled here
 *   costs the GPU nothing rather than a setup it then discards.
 * - It is the same decision for every engine, so an engine gains culling by
 *   advertising the caps, not by learning a register.
 */
#ifndef VELOCITY9X_D3D_CULL_H
#define VELOCITY9X_D3D_CULL_H

/*
 * D3DRENDERSTATE_CULLMODE's values, d3dtypes.h. Direct3D's default is CCW:
 * back faces are the ones whose vertices run counterclockwise on screen.
 */
#define V9X_D3DCULL_NONE 1ul
#define V9X_D3DCULL_CW   2ul
#define V9X_D3DCULL_CCW  3ul

/*
 * Non-zero when a triangle with these screen-space vertices is removed under
 * the given cull mode.
 *
 * Screen space has y growing downward, so a positive
 * (b - a) x (c - a) is CLOCKWISE as seen on the monitor - the opposite of the
 * y-up convention. Zero area, NaN and any mode other than CW or CCW draw the
 * triangle: a degenerate one covers nothing either way, and a NaN vertex must
 * reach the clipper, which refuses it with a count.
 */
int v9x_d3d_cull_triangle(unsigned long mode,
                          float ax, float ay,
                          float bx, float by,
                          float cx, float cy);

/*
 * The cull mode the core will actually apply: the application's request if
 * the engine advertises it (D3DPMISCCAPS_CULLCW / CULLCCW), NONE otherwise.
 *
 * Gated on the caps rather than applied everywhere because an engine that
 * claims CULLNONE alone has been drawing every triangle, and applications
 * have been setting D3D's default CCW at it all along - turning culling on
 * for it would change its output with nothing it published saying so.
 */
unsigned long v9x_d3d_cull_honoured(unsigned long mode,
                                    int claims_cw, int claims_ccw);

#endif
