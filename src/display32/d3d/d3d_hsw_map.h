/*
 * What the Haswell engine refuses of a render-interface draw.
 *
 * Pure: no DDHAL header, so scripts\build-host.ps1 compiles it and
 * tests\host\test_d3d_hsw_map.c holds it to its rules. Step 1 of
 * docs\plans\intel-haswell-glide-3dcif-opengl.md: an explicit draw is
 * honoured exactly or refused (r3d.h, explicit_state), and before this
 * d3d_hsw.c read neither the CPU levels nor the scissor nor the mask, so
 * it drew a CPU-level texture untextured and wrote outside a scissor.
 */
#ifndef VELOCITY9X_D3D_HSW_MAP_H
#define VELOCITY9X_D3D_HSW_MAP_H

#include "velocity9x/types.h"
#include "../r3d/r3d.h"

/* Refusal reasons, continuing d3d_hsw.c's V9X_D3D_HSW_REFUSE_* (0-8) in the
 * same counters (V9XTRACE M64PolicyNN). */
#define V9X_D3D_HSW_REFUSE_CPU_TEXTURE 9ul   /* texture.levels: no sampler */
#define V9X_D3D_HSW_REFUSE_MASK        10ul  /* write mask other than RGB  */
#define V9X_D3D_HSW_REFUSE_SCISSOR     11ul  /* a scissor short of target  */

/* The RGB write mask, V9X_R3D_ABI_WRITE_RED | GREEN | BLUE. */
#define V9X_D3D_HSW_WRITE_RGB 7ul

/*
 * Zero when nothing the render interface states stops this engine drawing
 * the draw exactly, or the reason. A CPU-level texture is refused on any
 * draw - Direct3D never sets one - and the scissor and mask only on an
 * explicit one: the engine has no scissor rectangle or partial mask yet,
 * so a scissor must cover the whole target and the mask be RGB.
 */
v9x_u32 v9x_d3d_hsw_explicit_refusal(const V9X_R3D_DRAW *draw);

#endif
