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
#include "velocity9x/r3d_tnl.h"

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

/* Render-target layouts: the two 16 bpp ones the engines write and the
 * 32 bpp one, numbered as V9X_D3D_TARGET_FORMAT_* and
 * V9X_D3D_RASTER_PIXFMT_* already are. 3 and 4 are texture layouts in the
 * ABI's one numbering, so XRGB8888 is 5. */
#define V9X_R3D_FORMAT_RGB565   1ul
#define V9X_R3D_FORMAT_XRGB1555 2ul
#define V9X_R3D_FORMAT_XRGB8888 5ul

/* A draw's depth.format: zero is the engine's 16-bit depth, as every draw
 * had; Z24X8 a 32-bit cell with 24 bits of depth, which only an engine
 * with depth32_bits is given (d3d_core.c). */
#define V9X_R3D_DEPTH_Z16   0ul
#define V9X_R3D_DEPTH_Z24X8 1ul

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
    /*
     * A CPU-resident texture, for an explicit draw (below) whose front end
     * keeps its own sampler-ready levels - the OpenGL ICD's: `object` is
     * then null and `levels` names `level_count` of them, level 0 first,
     * each validated by the render interface before it gets here. The
     * format and the three combine fields take the render interface's
     * numbers (V9X_R3D_ABI_FORMAT_*, _MIP_*, _COLOROP_*, _ALPHAOP_*).
     */
    const struct v9x_r3d_level *levels;
    v9x_u32 level_count;
    v9x_u32 format;
    v9x_u32 mip;
    v9x_u32 color_op;
    v9x_u32 alpha_op;
    v9x_u32 env_color;
} V9X_R3D_TEXTURE;

/* A colour combine past the render interface's, which only the Direct3D
 * front end states: C = Cin + Ct (D3DTOP_ADD). The interface's validation
 * stops at V9X_R3D_ABI_COLOROP_BLEND, so no ICD draw carries it, and an
 * engine that does not know it refuses the draw. */
#define V9X_R3D_COLOROP_ADD 5ul

/* One CPU texture level: storage the front end owns for the call. */
typedef struct v9x_r3d_level {
    const void *pixels;
    v9x_u32 pitch;
    v9x_u32 width;
    v9x_u32 height;
} V9X_R3D_LEVEL;

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
    /*
     * Non-zero when the front end states everything below and the engine
     * must honour it exactly or refuse the draw in accepts(): the render
     * interface sets it; the Direct3D front end never does, so no Direct3D
     * draw changes meaning. An explicit draw is perspective-correct, takes
     * any factor pair and the alpha test, and carries a scissor in surface
     * rows (half-open, inside the target) and a write mask
     * (V9X_R3D_ABI_WRITE_*).
     */
    v9x_u32 explicit_state;
    v9x_u32 scissor_left;
    v9x_u32 scissor_top;
    v9x_u32 scissor_right;
    v9x_u32 scissor_bottom;
    v9x_u32 write_mask;
    /* Non-zero when the caller has seen every vertex of the draw with
     * alpha 255; zero when unknown. An engine's accepts() has no vertices,
     * so this is how one that must know (the Mach64's MODULATEALPHA) can. */
    v9x_u32 vertex_alpha_opaque;
    /*
     * What the engine adds to every vertex's sx and sy as it copies it:
     * half a pixel for a Direct3D draw on an engine whose limits say
     * centre_in_engine (d3d_core.c, v9x_d3d_centre), zero from every other
     * caller, whose vertices are already where they are drawn.
     */
    float centre_offset;
    /*
     * A second texture unit, from the render interface only: texcoords1 is
     * non-null exactly when there is one, and then holds two floats a
     * vertex - tu1, tv1 - in the order of the vertices ops->draw receives.
     * texture1 is described as `texture` is, and on such a draw both carry
     * their combine in color_op/alpha_op/env_color whatever their storage;
     * `op` is not set. Only an engine whose limits say two units is handed
     * one, and the core does not clip it (docs\plans\gen3-sgis-
     * multitexture.md). Direct3D leaves texcoords1 null.
     */
    V9X_R3D_TEXTURE texture1;
    const float *texcoords1;
} V9X_R3D_DRAW;

/*
 * What a draw the engine transforms adds to V9X_R3D_DRAW (render interface
 * ABI 7, r3d_abi.h's V9X_R3D_ABI_TNL_DRAW, validated): the key and constant
 * block, the viewport in target pixels, the depth range, the cull as the
 * core numbers it (V9X_R3D_CULL_*, r3d_cull.h) and the provoking vertex.
 */
typedef struct v9x_r3d_tnl {
    const V9X_R3D_TNL_KEY *key;
    const V9X_R3D_TNL_CONSTANTS *constants;
    float viewport_x;
    float viewport_y;
    float viewport_width;
    float viewport_height;
    float depth_near;
    float depth_far;
    v9x_u32 cull;
    v9x_u32 provoking;
    /* Indexed (non-null): vertex_count vertices, index_count indices, a
     * list; the draw's triangle count means nothing then. */
    const v9x_u16 *indices;
    v9x_u32 index_count;
    v9x_u32 vertex_count;
    /* Segments (ABI 7, step 3b): each a described draw for its fragment
     * state, its constants and its index range, partitioning the indices
     * in order from 0. Null and 0 for one draw under `constants`. */
    const struct v9x_r3d_tnl_segment *segments;
    v9x_u32 segment_count;
} V9X_R3D_TNL;

typedef struct v9x_r3d_tnl_segment {
    V9X_R3D_DRAW draw;
    const V9X_R3D_TNL_CONSTANTS *constants;
    v9x_u32 first_index;
    v9x_u32 index_count;
} V9X_R3D_TNL_SEGMENT;

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

typedef struct v9x_r3d_list_stats {
    v9x_u32 triangles_in;
    v9x_u32 triangles_culled;
    v9x_u32 triangles_clipped;
    v9x_u32 sink_batches;
} V9X_R3D_LIST_STATS;

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
    /* Optional caller-owned storage. A zero capacity preserves the split-run
     * behaviour for callers that have not opted into in-call merging. */
    V9X_R3D_VERTEX *staging;
    v9x_u32 staging_triangles;
    /* Optional per-call accounting, overwritten by v9x_r3d_draw_list. */
    V9X_R3D_LIST_STATS *stats;
    /* Added to every vertex's sx and sy before the triangle is tested and
     * clipped, in a copy: Direct3D's pixel centres moved onto the core's
     * (d3d_core.c). Non-zero needs staging, which then carries every
     * triangle; the caller's array is never written. Culling sees the
     * caller's triangle, which a translation does not change. */
    float pixel_offset;
} V9X_R3D_LIST;

/*
 * A triangle list, drawn through the sink in order. With caller-owned staging,
 * the first cull or clip moves the pending window into that storage and later
 * survivors and clipped fans share capacity-sized batches. An unbroken list
 * remains one window on the caller's array. With zero staging capacity, clips
 * and culls retain the historical split-run behaviour. Returns non-zero when
 * every triangle was drawn; a refused one (past the guard band, or declined
 * by the sink) makes it zero and the rest of the list is still drawn.
 */
int v9x_r3d_draw_list(const V9X_R3D_LIST *list,
                      const V9X_R3D_VERTEX *vertices,
                      v9x_u32 triangle_count);

/* Unit-width aliased point/line coverage. A line owns its first rasterized
 * pixel and excludes its final one, so consecutive strip segments neither
 * double-blend nor leave a gap. The sink receives the covered pixel centre
 * and attributes interpolated along the original segment. -1 is invalid or
 * declined; otherwise the return is the number of fragments delivered. */
typedef int (*V9X_R3D_FRAGMENT_FN)(void *user, v9x_s32 x, v9x_s32 y,
                                   const V9X_R3D_VERTEX *vertex);
int v9x_r3d_draw_point(const V9X_R3D_VERTEX *vertex,
                       v9x_u32 width, v9x_u32 height,
                       V9X_R3D_FRAGMENT_FN fragment, void *user);
int v9x_r3d_draw_line(const V9X_R3D_VERTEX *first,
                      const V9X_R3D_VERTEX *last,
                      v9x_u32 width, v9x_u32 height,
                      V9X_R3D_FRAGMENT_FN fragment, void *user);

typedef struct v9x_r3d_clear_rect {
    v9x_u32 left, top, right, bottom;
} V9X_R3D_CLEAR_RECT;

/* CPU clear description. Rectangles are the drawable/scissor intersection.
 * Everything is validated before the first write, preventing partial clears
 * from a malformed list. */
typedef struct v9x_r3d_clear {
    void *color;
    v9x_u32 color_pitch;
    void *depth;
    v9x_u32 depth_pitch;
    v9x_u32 width, height, format;
    v9x_u32 clear_color, clear_depth;
    v9x_u32 color_value; /* 0x00RRGGBB */
    v9x_u32 depth_value; /* low 16 bits; low 24 for a 32-bit cell */
    v9x_u32 write_red, write_green, write_blue, write_depth;
    const V9X_R3D_CLEAR_RECT *rects;
    v9x_u32 rect_count;
    /* 4 for a 32-bit depth cell, written whole; 0 or 2 for 16-bit. */
    v9x_u32 depth_bytes;
} V9X_R3D_CLEAR;

int v9x_r3d_clear(const V9X_R3D_CLEAR *clear);

#endif
