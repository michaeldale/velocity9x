/*
 * ATI3DCIF vertices and primitives to render-interface triangles
 * (docs\plans\ati-3dcif-wrapper.md, Phase 2).
 *
 * Only C3D_EV_VTCF: no demo in the census sets C3D_EV_TLVERTEX
 * (docs\decisions\2026-10-14-3dcif-demo-census.md). The conventions are
 * the census's decoded vertices checked against the guide and the 3D RAGE
 * white paper:
 *   - x, y are pixels from the viewport's top-left, which is the draw
 *     surface's top-left unless C3D_ERS_SURF_VPORT moves it (white paper,
 *     "ATI3DCIF Viewport"). No pixel-centre offset is applied: the guide
 *     does not give one, and Phase 3's probe measures it.
 *   - z is in Z-buffer units, 0 to 2^16 - 1 (guide, "Z Buffers"), so it is
 *     scaled to the interface's 0..1 and clamped.
 *   - w is 1/w, and s and t arrive multiplied by it ("homogeneous (s, t,
 *     and inverse w)"; ATi-3D's corners decode to s/w of 0 and 1). The
 *     interface takes u, v and rhw, so s and t are divided by w.
 *   - r, g, b, a are 0 to 255.
 *   - Fog takes its factor from the vertex alpha (guide, "Applying Fog"):
 *     the Rage's fog runs through the alpha interpolator with the fog
 *     colour in place of the destination, so 255 is no fog, as the
 *     interface's specular alpha counts it.
 *
 * Pure: no Windows headers.
 */
#ifndef VELOCITY9X_CIF_VERTEX_H
#define VELOCITY9X_CIF_VERTEX_H

#include "velocity9x/types.h"
#include "velocity9x/r3d_abi.h"
#include "cif_api.h"

/* The 16-bit Z buffer's largest value, which z = 1.0 in the interface. */
#define V9X_CIF_Z_MAX 65535.0f

typedef struct v9x_cif_vertex_setup {
    float origin_x;         /* the viewport's left, in surface pixels */
    float origin_y;         /* and top */
    v9x_u32 shade;          /* V9X_C3D_ESH_* */
    v9x_u32 solid_argb;     /* C3D_ERS_SOLID_CLR as 0xAARRGGBB */
    v9x_u32 fog;            /* fog on: vertex alpha is the fog factor */
    v9x_u32 white;          /* colour white, alpha kept (cif_state.h) */
} V9X_CIF_VERTEX_SETUP;

/* One VTCF vertex (ten floats) to the interface's. Solid shading takes
 * the solid colour; flat shading is applied per triangle afterwards by
 * v9x_cif_triangle_flat. With fog on, the colour's alpha is 255 and the
 * vertex alpha goes to specular alpha; otherwise specular alpha is 255. */
void v9x_cif_vtcf_convert(const V9X_CIF_VERTEX_SETUP *setup, const float *in,
                          V9X_R3D_ABI_VERTEX *out);

/* Flat shading: every corner takes the last vertex's colour (white paper,
 * "Shading Modes"). Specular, which carries fog, stays per vertex. */
void v9x_cif_triangle_flat(V9X_R3D_ABI_VERTEX *triangle);

/* How a primitive call's vertices are walked. */
#define V9X_CIF_FORM_STRIP 0ul      /* RenderPrimStrip: contiguous, shared */
#define V9X_CIF_FORM_LIST  1ul      /* RenderPrimList: separate primitives */

/*
 * Triangles a call of `count` vertices of primitive `prim` produces, 0 for
 * a primitive this module does not expand (lines, points, rectangles: no
 * demo in the census draws them). Triangles: a list takes three vertices
 * each, a strip shares two between neighbours. Quadrangles: a list takes
 * four vertices each, drawn as the fan 0-1-2, 0-2-3; a quadrangle strip
 * is not expanded, because the guide does not give its vertex order and
 * no demo draws one.
 */
v9x_u32 v9x_cif_prim_triangles(v9x_u32 prim, v9x_u32 form, v9x_u32 count);

/*
 * Which of the call's vertices is corner `corner` (0..2) of triangle
 * `triangle`. A strip keeps every triangle's winding as the first's, as
 * Direct3D's strips do; CIF has no cull state, so winding matters only to
 * keep the order predictable.
 */
v9x_u32 v9x_cif_prim_vertex(v9x_u32 prim, v9x_u32 form, v9x_u32 triangle,
                            v9x_u32 corner);

#endif /* VELOCITY9X_CIF_VERTEX_H */
