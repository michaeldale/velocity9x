/*
 * The neutral render core: what every 3D front end - the DX5 Direct3D HAL in
 * src\display32\d3d today, the OpenGL ICD the plan adds - hands the engines,
 * and the screen-space work that is the same for all of them.
 *
 * This header names no API. Its vertex is the screen-space vertex every
 * engine consumes, its clipper cuts against a rectangle, and its list
 * builder knows nothing about where a batch goes: the front end supplies a
 * batch sink and a cull decision as callbacks. Nothing in src\display32\r3d
 * includes a DDHAL or OS header, so all of it compiles on the host and is
 * held to tests (docs\plans\opengl-1.1-icd.md, Phase 1).
 *
 * Phase 1a moved the clipper and list builder here out of d3d_core.c
 * unchanged in behaviour. The D3D core casts its V9X_D3DTLVERTEX to this
 * vertex: the two are declared field for field the same, the size is
 * asserted below and the offsets in tests\host\test_r3d_clip.c.
 */
#ifndef VELOCITY9X_R3D_H
#define VELOCITY9X_R3D_H

#include "velocity9x/types.h"

/*
 * A screen-space vertex: D3DTLVERTEX's layout, which is also what the
 * engines' converters read. sx/sy are pixels, sz is 0..1 depth, rhw is
 * 1/w, colour and specular are packed ARGB, tu/tv are texture coordinates.
 */
typedef struct v9x_r3d_vertex {
    float sx;
    float sy;
    float sz;
    float rhw;
    v9x_u32 color;
    v9x_u32 specular;
    float tu;
    float tv;
} V9X_R3D_VERTEX;

typedef char v9x_r3d_assert_vertex_32[sizeof(V9X_R3D_VERTEX) == 32 ? 1 : -1];

/*
 * A triangle clipped against four edges has at most seven vertices; eight is
 * the buffer both stages of the clipper use. A fan over eight vertices is
 * six triangles.
 */
#define V9X_R3D_CLIP_MAX_VERTICES 8u
#define V9X_R3D_MAX_FAN_TRIANGLES 6u

/*
 * Clip one triangle to the rectangle [0, width) x [0, height), Sutherland-
 * Hodgman against the four edges in the order left, right, top, bottom,
 * with perspective-correct texture coordinates at each cut.
 *
 * A vertex whose sx or sy is not finite, or lies outside +/- guard_limit,
 * refuses the whole triangle with -1: the guard band is the executing
 * engine's fixed-point range, and a coordinate past it would wrap in the
 * engine's converter rather than clip. Otherwise the result holds the
 * clipped polygon and the count (0 when nothing is left) is returned.
 */
int v9x_r3d_clip_triangle(const V9X_R3D_VERTEX *triangle,
                          float guard_limit,
                          float width,
                          float height,
                          V9X_R3D_VERTEX *result);

/*
 * Non-zero when all three vertices lie on [0, width] x [0, height], edges
 * included, and every coordinate is finite. A NaN answers no so it reaches
 * the clipper, which refuses it with a count.
 */
int v9x_r3d_triangle_on_target(const V9X_R3D_VERTEX *triangle,
                               float width,
                               float height);

/*
 * The batch sink and cull decision a list builder needs, supplied by the
 * front end. batch returns non-zero when every triangle was emitted; culled
 * returns non-zero for a triangle the application asked to remove.
 */
typedef int (*V9X_R3D_BATCH_FN)(void *user,
                                const V9X_R3D_VERTEX *vertices,
                                v9x_u32 triangle_count);
typedef int (*V9X_R3D_CULLED_FN)(void *user,
                                 const V9X_R3D_VERTEX *triangle);

typedef struct v9x_r3d_list {
    float guard_limit;
    float width;
    float height;
    /* Non-zero when a triangle off the target must be cut before the sink
     * sees it; zero for an engine with a measured hardware guard band. */
    v9x_u32 clip_in_core;
    V9X_R3D_BATCH_FN batch;
    V9X_R3D_CULLED_FN culled;
    void *user;
} V9X_R3D_LIST;

/*
 * A triangle list, drawn through the sink in runs: triangles already on the
 * target go through as windows on the caller's array, a triangle that
 * crosses an edge is cut and its fan sent on its own, a culled triangle ends
 * the run and is not sent. Returns non-zero when every triangle was drawn;
 * a refused one (past the guard band, or declined by the sink) makes it zero
 * and the rest of the list is still drawn.
 */
int v9x_r3d_draw_list(const V9X_R3D_LIST *list,
                      const V9X_R3D_VERTEX *vertices,
                      v9x_u32 triangle_count);

#endif
