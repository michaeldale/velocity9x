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
 * The vocabulary of a draw, numbered as Direct3D numbers the same concepts
 * (d3dtypes.h: D3DCMP_*, D3DBLEND_*, D3DFILTER_*, D3DTADDRESS_*,
 * D3DTBLEND_*), so the D3D front end passes its render state through
 * untranslated and src\display32\d3d\d3d_state.c asserts the equality at
 * compile time - the arrangement d3d_raster.h already uses. A front end with
 * another vocabulary maps to these. A concept Direct3D lacks takes a value
 * outside its range when one is added.
 */
#define V9X_R3D_CMP_NEVER          1ul
#define V9X_R3D_CMP_LESS           2ul
#define V9X_R3D_CMP_EQUAL          3ul
#define V9X_R3D_CMP_LESSEQUAL      4ul
#define V9X_R3D_CMP_GREATER        5ul
#define V9X_R3D_CMP_NOTEQUAL       6ul
#define V9X_R3D_CMP_GREATEREQUAL   7ul
#define V9X_R3D_CMP_ALWAYS         8ul

#define V9X_R3D_BLEND_ZERO         1ul
#define V9X_R3D_BLEND_ONE          2ul
#define V9X_R3D_BLEND_SRCCOLOR     3ul
#define V9X_R3D_BLEND_INVSRCCOLOR  4ul
#define V9X_R3D_BLEND_SRCALPHA     5ul
#define V9X_R3D_BLEND_INVSRCALPHA  6ul
#define V9X_R3D_BLEND_DESTALPHA    7ul
#define V9X_R3D_BLEND_INVDESTALPHA 8ul
#define V9X_R3D_BLEND_DESTCOLOR    9ul
#define V9X_R3D_BLEND_INVDESTCOLOR 10ul
#define V9X_R3D_BLEND_SRCALPHASAT  11ul

#define V9X_R3D_FILTER_NEAREST          1ul
#define V9X_R3D_FILTER_LINEAR           2ul
#define V9X_R3D_FILTER_MIPNEAREST       3ul
#define V9X_R3D_FILTER_MIPLINEAR        4ul
#define V9X_R3D_FILTER_LINEARMIPNEAREST 5ul
#define V9X_R3D_FILTER_LINEARMIPLINEAR  6ul

#define V9X_R3D_ADDRESS_WRAP   1ul
#define V9X_R3D_ADDRESS_MIRROR 2ul
#define V9X_R3D_ADDRESS_CLAMP  3ul
#define V9X_R3D_ADDRESS_BORDER 4ul

/* How the texel combines with the fragment colour: D3DTBLEND_*'s numbers.
 * The split into a colour op and an alpha op that the plan describes comes
 * when the software rasterizer grows the ops (Phase 2); until then the
 * engines translate these themselves, as they always have. */
#define V9X_R3D_TEXOP_DECAL         1ul
#define V9X_R3D_TEXOP_MODULATE      2ul
#define V9X_R3D_TEXOP_DECALALPHA    3ul
#define V9X_R3D_TEXOP_MODULATEALPHA 4ul
#define V9X_R3D_TEXOP_DECALMASK     5ul
#define V9X_R3D_TEXOP_MODULATEMASK  6ul
#define V9X_R3D_TEXOP_COPY          7ul
#define V9X_R3D_TEXOP_ADD           8ul

#define V9X_R3D_SHADE_FLAT    1ul
#define V9X_R3D_SHADE_GOURAUD 2ul

/* Render-target layouts: the two 16 bpp ones the engines write, numbered as
 * V9X_D3D_TARGET_FORMAT_* and V9X_D3D_RASTER_PIXFMT_* already are. */
#define V9X_R3D_FORMAT_RGB565   1ul
#define V9X_R3D_FORMAT_XRGB1555 2ul

/*
 * A surface as a draw names it: where it is in video memory, its shape, and
 * the front end's own object for it (the D3D core's surface LCL), which the
 * core carries but never reads. An engine that needs more than the offset
 * - the ViRGE's colour-key rewrite, Gen3's mip-tree layout - takes it from
 * the object through the front end's services.
 */
typedef struct v9x_r3d_surface {
    v9x_u32 offset;
    v9x_u32 pitch;
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 format;
    void *object;
} V9X_R3D_SURFACE;

/*
 * The bound texture: its object, null when the draw is untextured, and the
 * sampling state. wrap_either is the ViRGE-shaped "either axis wraps" that
 * D3DRENDERSTATE_WRAPU/WRAPV were folded into before Gen3 needed each axis.
 */
typedef struct v9x_r3d_texture {
    void *object;
    v9x_u32 min_filter;
    v9x_u32 mag_filter;
    v9x_u32 op;
    v9x_u32 address;
    v9x_u32 border;
    v9x_u32 wrap_u;
    v9x_u32 wrap_v;
    v9x_u32 wrap_either;
} V9X_R3D_TEXTURE;

/*
 * One batch's whole description. Nothing initialises this positionally, so
 * a field may be inserted where it reads best; the append-only rule of the
 * ops table does not apply here.
 *
 * depth.object is null when no depth surface is bound; depth_enable is the
 * application's state on top of that. The three-part test the engines make
 * (enabled, bound, and a non-zero pitch) is v9x_d3d_state_depth_active.
 *
 * color_key_enable and alpha_force are the D3D front end's knobs: an engine
 * that serves them reads them, and every other front end leaves them zero.
 */
typedef struct v9x_r3d_draw {
    V9X_R3D_SURFACE target;
    V9X_R3D_SURFACE depth;
    v9x_u32 depth_enable;
    v9x_u32 depth_write;
    v9x_u32 depth_func;
    V9X_R3D_TEXTURE texture;
    v9x_u32 blend_enable;
    v9x_u32 src_blend;
    v9x_u32 dst_blend;
    v9x_u32 alpha_test_enable;
    v9x_u32 alpha_func;
    v9x_u32 alpha_ref;
    v9x_u32 shade_mode;
    v9x_u32 specular_enable;
    v9x_u32 fog_enable;
    v9x_u32 fog_color;
    v9x_u32 color_key_enable;
    v9x_u32 alpha_force;
} V9X_R3D_DRAW;

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
