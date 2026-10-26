/*
 * The OpenGL 1.1 vertex pipeline up to the render interface
 * (docs\plans\opengl-1.1-icd.md, Phase 4): current attributes, Begin/End
 * assembly, transform, homogeneous clipping, face culling, the viewport and
 * depth-range mapping, and batches of render-interface vertices. Pure, like
 * gl_state.c; the ICD supplies the sink a full batch goes to.
 *
 * What reaches the sink is what the CPU rasterizer contract
 * (docs\specifications\cpu-rasterizer-contract.md) describes: surface
 * coordinates with y down, sz the window depth, rhw q/w, colour packed
 * ARGB, specular alpha the fog factor (255: unfogged).
 */
#ifndef VELOCITY9X_GL_PRIM_H
#define VELOCITY9X_GL_PRIM_H

#include "velocity9x/r3d_abi.h"
#include "gl_state.h"

#define V9X_GL_POINTS         0x0000u
#define V9X_GL_LINES          0x0001u
#define V9X_GL_LINE_LOOP      0x0002u
#define V9X_GL_LINE_STRIP     0x0003u
#define V9X_GL_TRIANGLES      0x0004u
#define V9X_GL_TRIANGLE_STRIP 0x0005u
#define V9X_GL_TRIANGLE_FAN   0x0006u
#define V9X_GL_QUADS          0x0007u
#define V9X_GL_QUAD_STRIP     0x0008u
#define V9X_GL_POLYGON        0x0009u

#define V9X_GL_FLAT           0x1D00u
#define V9X_GL_SMOOTH         0x1D01u
#define V9X_GL_FRONT          0x0404u
#define V9X_GL_BACK           0x0405u
#define V9X_GL_FRONT_AND_BACK 0x0408u
#define V9X_GL_CW             0x0900u
#define V9X_GL_CCW            0x0901u
#define V9X_GL_CULL_FACE      0x0B44u
#define V9X_GL_BLEND          0x0BE2u
#define V9X_GL_ALPHA_TEST     0x0BC0u
#define V9X_GL_POLYGON_OFFSET_FILL 0x8037u

#define V9X_GL_FOG            0x0B60u
#define V9X_GL_FOG_INDEX      0x0B61u
#define V9X_GL_FOG_DENSITY    0x0B62u
#define V9X_GL_FOG_START      0x0B63u
#define V9X_GL_FOG_END        0x0B64u
#define V9X_GL_FOG_MODE       0x0B65u
#define V9X_GL_FOG_COLOR      0x0B66u
#define V9X_GL_EXP            0x0800u
#define V9X_GL_EXP2           0x0801u
#define V9X_GL_LINEAR         0x2601u

#define V9X_GL_NEVER          0x0200u
#define V9X_GL_LESS           0x0201u
#define V9X_GL_LEQUAL         0x0203u
#define V9X_GL_ALWAYS         0x0207u

#define V9X_GL_ZERO                0x0000u
#define V9X_GL_ONE                 0x0001u
#define V9X_GL_SRC_ALPHA           0x0302u
#define V9X_GL_ONE_MINUS_SRC_ALPHA 0x0303u
#define V9X_GL_SRC_ALPHA_SATURATE  0x0308u

/* A vertex as the pipeline carries it: clip coordinates and the
 * attributes it interpolates in clip space. */
typedef struct v9x_gl_vertex {
    GLfloat clip[4];
    GLfloat color[4];
    GLfloat tex[4];
    /* Unit 1's s and t (GL_SGIS_multitexture), interpolated as tex is. */
    GLfloat tex1[2];
    /* The fog coordinate c (3.9): the eye-space distance, approximated by
     * |z_e| as the specification allows, interpolated as the colour is so
     * a clipped vertex is fogged at its own distance. */
    GLfloat fog_c;
    /*
     * Set when the vertex is inside all ten clip planes, with its window
     * position (x, y, z, rhw) and the interface vertex it emits. A triangle
     * of three such vertices clips to itself, so it takes these as they
     * are instead of clipping, windowing and emitting each corner again:
     * Quake 2's polygons are fans, where every vertex was done up to three
     * times (2026-09-29, 4,000 cycles a vertex on the Gateway's PIII).
     */
    int inside;
    GLfloat window[4];
    V9X_R3D_ABI_VERTEX abi;
    /* Hardware T&L (docs\plans\hardware-tnl.md, step 3): the vertex as
     * draw_tnl takes it, when the primitive is captured - object x y z w
     * where sx sy sz rhw are, the packed colour, specular 0xFF000000 (no
     * fog), s and t - and where it went: its index in the T&L batch whose
     * serial it carries. A triangle names its corners by that index while
     * the serial is the batch's; one from a batch already flushed, or not
     * yet sent (serial 0), is sent then. A captured vertex has only clip
     * among the CPU path's fields, for the cull below, and outcode: bit k
     * set when it is outside frustum plane k (v9x_gl_prim_plane's 0 to 5).
     * tnl_element: the vertex array element it came from, plus one, in
     * the element cache (V9X_GL_TNL_ELEMENT); zero for any other. */
    V9X_R3D_ABI_VERTEX object;
    v9x_u32 tnl_serial;
    v9x_u32 tnl_index;
    v9x_u32 outcode;
    v9x_u32 tnl_element;
} V9X_GL_VERTEX;

/*
 * The T&L sink: a batch of untransformed triangles, indexed - each vertex
 * once, then a list of indices into them. Non-zero when the sink took it;
 * the front end holds it, and draws it through v9x_gl_prim_tnl_screen if
 * the engine later refuses it. Zero loses the batch, counted as a refused
 * screen batch is (sink_failures): the CPU transformed none of it (step
 * 3b), and a sink declines only when there is no device to draw with.
 */
typedef int (*V9X_GL_TNL_SINK_FN)(void *user, const V9X_R3D_ABI_VERTEX *object,
                                  v9x_u32 vertex_count,
                                  const v9x_u16 *indices,
                                  v9x_u32 index_count);

/* One captured batch's room: vertices, and indices (three a triangle). */
#define V9X_GL_TNL_VERTICES 256u
#define V9X_GL_TNL_INDICES  384u

/*
 * The element cache (step 3b): glDrawElements and glDrawArrays name a
 * vertex by its array element, and within one call an element always
 * fetches the same attributes, so a captured element is fetched and
 * transformed once a call however many indices name it. Slot e mod
 * V9X_GL_TNL_ELEMENTS holds the last element there: its clip position
 * and outcode, its object vertex, and the batch serial and index it was
 * sent at (serial 0: not yet). A slot from another call, or another
 * element, misses, and the element is fetched again.
 */
#define V9X_GL_TNL_ELEMENTS 512u

typedef struct v9x_gl_tnl_element {
    v9x_u32 element;            /* the element plus one; 0: empty */
    v9x_u32 call;
    v9x_u32 serial;
    v9x_u32 index;
    GLfloat clip[4];
    v9x_u32 outcode;
    V9X_R3D_ABI_VERTEX object;
} V9X_GL_TNL_ELEMENT;

/* Where a full batch goes, and the state the batch was made with. The sink
 * answers non-zero when it took the batch; the pipeline then empties it.
 * texcoords1 is unit 1's s and t, two floats a vertex in the vertices'
 * order, when the pipeline carries two units, and null otherwise. */
typedef int (*V9X_GL_SINK_FN)(void *user, const V9X_R3D_ABI_VERTEX *vertices,
                              const GLfloat *texcoords1,
                              v9x_u32 triangle_count);

typedef struct v9x_gl_pipeline {
    GLfloat color[4];
    GLfloat tex[4];
    /* Unit 1's current s and t (glMTexCoord2fSGIS), initially 0, 0. */
    GLfloat tex1[2];
    /* Units whose coordinates the batch carries: 2 sends texcoords1 to the
     * sink. Set by the front end outside Begin/End. */
    v9x_u32 units;
    /* The current normal (2.7). Held for queries and vertex arrays; no
     * lighting reads it yet. */
    GLfloat normal[3];
    /* The current edge flag and colour index (2.7), held for queries:
     * polygon modes LINE and POINT are not drawn, and there is no
     * colour-index pixel format. */
    GLboolean edge_flag;
    GLfloat index;
    GLenum shade_model;
    GLenum cull_face;
    GLenum front_face;
    GLenum depth_func;
    GLenum blend_src;
    GLenum blend_dst;
    GLenum alpha_func;
    GLfloat alpha_ref;
    GLdouble depth_near;
    GLdouble depth_far;
    /* glPolygonOffset's factor and units (3.5.5), applied to filled
     * triangles while POLYGON_OFFSET_FILL is enabled. */
    GLfloat offset_factor;
    GLfloat offset_units;
    /* r, one step of the drawable's depth buffer, as it was at Begin. */
    GLfloat offset_step;
    /* POLYGON_OFFSET_FILL as it was at Begin; it cannot change before
     * End, and the triangles do not look it up each. */
    int offset_on;
    /* glFog's state (3.9, table 6.8): the mode, density, linear start
     * and end, colour (clamped when given) and index. */
    GLenum fog_mode;
    GLfloat fog_density;
    GLfloat fog_start;
    GLfloat fog_end;
    GLfloat fog_color[4];
    GLfloat fog_index;
    /* FOG as it was at Begin, as offset_on is: each emitted vertex then
     * carries its fog factor in the specular alpha. */
    int fog_on;
    /* The primitive being assembled: its mode, how many vertices so far,
     * and the ones the next triangle may need. */
    GLenum mode;
    v9x_u32 count;
    V9X_GL_VERTEX first;
    /* The last three vertices and the one being added, as a ring: the
     * former previous[k], k = 0..2 oldest first, is ring[(ring_head + k)
     * & 3], and the vertex being added is built in the fourth slot
     * (v9x_gl_prim_vertex). */
    V9X_GL_VERTEX ring[4];
    unsigned int ring_head;
    /* The draw rectangle's clip planes for this primitive (the viewport
     * and scissor cannot change inside Begin/End), and whether it has any
     * area; set at Begin. */
    GLfloat clip_edge[4];
    int clip_ready;
    /*
     * The same primitive's window mapping, set at Begin rather than per
     * vertex, which is where it was computed until 2026-10-01: the draw
     * rectangle the window position is clamped to, the viewport as floats,
     * and the depth range as zw = scale zd + bias. A window resize seen by
     * a flush inside Begin/End is applied from the next primitive.
     */
    GLfloat window_rect[4];
    GLfloat viewport_f[4];
    GLdouble depth_scale;
    GLdouble depth_bias;
    /* What the four above and clip_edge/clip_ready were taken from, so a
     * Begin with the same inputs keeps them rather than taking them again
     * (2026-10-01: Quake 2 begins a primitive per polygon, 1.65 us each). */
    int begin_valid;
    GLint begin_viewport[4];
    GLint begin_scissor[4];
    int begin_scissor_on;
    v9x_u32 begin_drawable[2];
    GLdouble begin_depth[2];
    /* The last colour packed into a vertex and the floats it came from:
     * the colour changes per glColor, not per vertex, and packing it is
     * four float-to-integer conversions. */
    GLfloat argb_from[4];
    v9x_u32 argb;
    int argb_valid;
    /* The batch, and unit 1's coordinates for it. */
    V9X_R3D_ABI_VERTEX batch[3u * V9X_R3D_ABI_BATCH_MAX];
    GLfloat batch_tex1[2u * 3u * V9X_R3D_ABI_BATCH_MAX];
    v9x_u32 batch_triangles;
    V9X_GL_SINK_FN sink;
    void *sink_user;
    /* Batches the sink refused; the triangles in them are lost. */
    v9x_u32 sink_failures;
    /*
     * Hardware T&L capture (step 3). tnl_allowed: set by the front end when
     * the engine runs T&L and nothing turns it off. tnl_capture: this
     * primitive is captured - a filled primitive with no fog, polygon
     * offset, flat shading, second unit or FRONT_AND_BACK cull, all of
     * which the step 3 kernel and key leave to the CPU, and q 1 at Begin.
     * A vertex with q other than 1, which the kernel does not divide,
     * ends the capture for the rest of the primitive (v9x_gl_prim_vertex).
     *
     * Step 3b: the CPU no longer transforms a captured primitive for the
     * screen. It takes each vertex to clip space through tnl_mvp, P x MV
     * as tnl_mvp_from[0..15] (MV) and [16..31] (P) gave it, to cull: a
     * triangle every corner of which is outside one frustum plane, or
     * facing away under CULL_FACE, is not sent. Only the triangles left
     * name vertices, and only the vertices they name are sent.
     */
    V9X_GL_TNL_SINK_FN tnl_sink;
    int tnl_allowed;
    int tnl_capture;
    v9x_u32 tnl_serial;
    v9x_u32 tnl_vertex_count;
    v9x_u32 tnl_index_count;
    GLfloat tnl_mvp[16];
    GLfloat tnl_mvp_from[32];
    int tnl_mvp_valid;
    /* The front end's storage for one captured batch, V9X_GL_TNL_VERTICES
     * and V9X_GL_TNL_INDICES long, and the element cache,
     * V9X_GL_TNL_ELEMENTS long; null captures nothing (the element cache:
     * no caching). Not arrays here: the ICD's contexts are static, and
     * 9 KB each in the DLL's file put the S3 floppy's first disk over its
     * size. */
    V9X_R3D_ABI_VERTEX *tnl_vertices;
    v9x_u16 *tnl_indices;
    V9X_GL_TNL_ELEMENT *tnl_elements;
    /* The vertex array call the cache is filled under, and the element
     * the next v9x_gl_prim_vertex is (plus one; 0: none). */
    v9x_u32 tnl_call;
    v9x_u32 tnl_element_next;
    /* Batches the engine took and ones the sink turned back; triangles
     * the cull above kept from it; primitives whose capture a q other
     * than 1 ended; vertex array elements taken from the cache. */
    v9x_u32 tnl_taken;
    v9x_u32 tnl_declined;
    v9x_u32 tnl_culled;
    v9x_u32 tnl_abandoned;
    v9x_u32 tnl_cached;
    /*
     * Where a vertex's time goes (2026-10-01), when the front end supplies
     * the array - null, the initial value, measures nothing: TSC cycles as
     * lo/hi pairs per V9X_GL_PRIM_PROF_* stage, then the triangle counts
     * at V9X_GL_PRIM_PROF_COUNTS. Measurement only.
     */
    v9x_u32 *profile;
} V9X_GL_PIPELINE;

#define V9X_GL_PRIM_PROF_TRANSFORM 0u  /* both matrices, colour, texcoord */
#define V9X_GL_PRIM_PROF_INSIDE    1u  /* the six-plane inside test      */
#define V9X_GL_PRIM_PROF_WINDOW    2u  /* window mapping and emit        */
#define V9X_GL_PRIM_PROF_ASSEMBLE  3u  /* triangles, clip, cull, batch   */
#define V9X_GL_PRIM_PROF_HISTORY   4u  /* first/previous vertex copies   */
#define V9X_GL_PRIM_PROF_STAGES    5u
/* Triangle counts follow the pairs: inside fast path, clipped, culled. */
#define V9X_GL_PRIM_PROF_COUNTS    (V9X_GL_PRIM_PROF_STAGES * 2u)
#define V9X_GL_PRIM_PROF_FAST      (V9X_GL_PRIM_PROF_COUNTS + 0u)
#define V9X_GL_PRIM_PROF_CLIPPED   (V9X_GL_PRIM_PROF_COUNTS + 1u)
#define V9X_GL_PRIM_PROF_CULLED    (V9X_GL_PRIM_PROF_COUNTS + 2u)
#define V9X_GL_PRIM_PROF_DWORDS    (V9X_GL_PRIM_PROF_COUNTS + 3u)

/* The initial values of the pipeline's state (colour 1,1,1,1, texture
 * coordinate 0,0,0,1, normal 0,0,1, SMOOTH, BACK, CCW, LESS, ONE/ZERO, ALWAYS/0, depth
 * range 0..1, edge flag TRUE, index 1, fog EXP with density 1, start 0,
 * end 1, colour 0,0,0,0 and index 0) and no sink. */
void v9x_gl_pipeline_init(V9X_GL_PIPELINE *pipeline);
void v9x_gl_pipeline_sink(V9X_GL_PIPELINE *pipeline, V9X_GL_SINK_FN sink,
                          void *user);
/* The T&L sink beside it (same user), whether captures may be made, and
 * the capture's storage (V9X_GL_TNL_VERTICES and V9X_GL_TNL_INDICES long;
 * either null allows nothing) and element cache (V9X_GL_TNL_ELEMENTS
 * long; null caches nothing). */
void v9x_gl_pipeline_tnl(V9X_GL_PIPELINE *pipeline, V9X_GL_TNL_SINK_FN sink,
                         int allowed, V9X_R3D_ABI_VERTEX *vertices,
                         v9x_u16 *indices, V9X_GL_TNL_ELEMENT *elements);

/*
 * Vertex array elements through the element cache (V9X_GL_TNL_ELEMENT).
 * v9x_gl_prim_tnl_call starts a glDrawElements or glDrawArrays call: no
 * element cached before it is used after. v9x_gl_prim_element, inside
 * Begin/End, issues element `element` from the cache and answers non-zero
 * when it is there; zero, the caller fetches the element and issues it
 * with v9x_gl_prim_vertex, which caches it. Outside a capture it answers
 * zero and caches nothing.
 */
void v9x_gl_prim_tnl_call(V9X_GL_PIPELINE *pipeline);
int v9x_gl_prim_element(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                        v9x_u32 element);

/*
 * What a captured batch is drawn under (render interface ABI 7), from the
 * state it was captured in. Pure, and host-tested against the CPU path's
 * own window positions.
 *
 * v9x_gl_prim_tnl_constants: the key for an unlit OpenGL draw and the
 * constant block whose model-view-projection is projection x model-view
 * with OpenGL's clip z folded into Direct3D's: z' = (z + w) / 2, so the
 * engine's clipper, which keeps 0 <= z' <= w, keeps -w <= z <= w.
 *
 * v9x_gl_prim_tnl_viewport: the viewport on the target (y down from the
 * drawable's top) and the depth range, so the engine's window position is
 * the CPU path's: x as 2.10.1 maps it, y the drawable's height less it.
 *
 * v9x_gl_prim_tnl_cull: the winding on the target to discard
 * (V9X_R3D_ABI_CULL_*). The window and the target are the same picture
 * with y counted the other way, and both windings are the one seen: a
 * triangle counter-clockwise in window coordinates (y up) is
 * counter-clockwise on the target too, though its signed area there (y
 * down) is negative. So OpenGL's default front, CCW, culled BACK, discards
 * what is clockwise on the target, Direct3D's own CW.
 */
void v9x_gl_prim_tnl_constants(const V9X_GL_STATE *state,
                               const V9X_GL_PIPELINE *pipeline,
                               V9X_R3D_TNL_KEY *key,
                               V9X_R3D_TNL_CONSTANTS *constants);
void v9x_gl_prim_tnl_viewport(const V9X_GL_STATE *state,
                              const V9X_GL_PIPELINE *pipeline,
                              V9X_R3D_ABI_TNL_DRAW *tnl);
v9x_u32 v9x_gl_prim_tnl_cull(const V9X_GL_STATE *state,
                             const V9X_GL_PIPELINE *pipeline);

/*
 * v9x_gl_prim_tnl_scissor: the state's scissor as a captured batch needs
 * it. The CPU path cuts its geometry to the draw rectangle (the drawable,
 * and the scissor box when the test is on) and asks the engine for the
 * whole drawable; the engine's own clipper cuts only to the viewport, so a
 * captured batch carries the draw rectangle, on the target, as its
 * scissor. Left and top inclusive, right and bottom exclusive.
 */
void v9x_gl_prim_tnl_scissor(const V9X_GL_STATE *state,
                             V9X_R3D_ABI_STATE *out);

/*
 * The CPU's draw of a captured batch the engine refused (step 3b): the
 * triangles index[*next], index[*next + 1], ... of `indices` taken through
 * the constants' model-view-projection to clip space, clipped to the view
 * volume and to the scissor rectangle of `scissor`, windowed by `frame`'s
 * viewport and depth range, culled by its cull, and written to `out` as a
 * list of screen triangles, as the CPU path emits them. Stops when the
 * indices end or `room` triangles could not take the next one's pieces
 * (V9X_GL_PRIM_TNL_PIECES); *next is then the first index not done.
 * Returns the triangles written.
 */
#define V9X_GL_PRIM_TNL_PIECES 11u

v9x_u32 v9x_gl_prim_tnl_screen(const V9X_R3D_TNL_CONSTANTS *constants,
                               const V9X_R3D_ABI_TNL_DRAW *frame,
                               const V9X_R3D_ABI_STATE *scissor,
                               const V9X_R3D_ABI_VERTEX *object,
                               const v9x_u16 *indices, v9x_u32 index_count,
                               v9x_u32 *next, V9X_R3D_ABI_VERTEX *out,
                               v9x_u32 room);
/* 1 or 2: whether the next primitives carry unit 1's coordinate. Outside
 * Begin/End only; the batch is empty then (glEnd flushes it). */
void v9x_gl_pipeline_units(V9X_GL_PIPELINE *pipeline, v9x_u32 units);

/* Current attributes. Legal inside and outside Begin/End. */
void v9x_gl_prim_color(V9X_GL_PIPELINE *pipeline, GLfloat r, GLfloat g,
                       GLfloat b, GLfloat a);
void v9x_gl_prim_texcoord(V9X_GL_PIPELINE *pipeline, GLfloat s, GLfloat t,
                          GLfloat r, GLfloat q);
/* Unit 1's s and t (glMTexCoord2fSGIS with TEXTURE1_SGIS). */
void v9x_gl_prim_texcoord1(V9X_GL_PIPELINE *pipeline, GLfloat s, GLfloat t);
void v9x_gl_prim_normal(V9X_GL_PIPELINE *pipeline, GLfloat x, GLfloat y,
                        GLfloat z);
void v9x_gl_prim_edge_flag(V9X_GL_PIPELINE *pipeline, GLboolean flag);
void v9x_gl_prim_index(V9X_GL_PIPELINE *pipeline, GLfloat index);

/* State the pipeline or the fragment stage reads; each is
 * INVALID_OPERATION inside Begin/End and INVALID_ENUM for a bad value. */
void v9x_gl_prim_shade_model(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                             GLenum mode);
void v9x_gl_prim_cull_face(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                           GLenum mode);
void v9x_gl_prim_front_face(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                            GLenum mode);
void v9x_gl_prim_depth_func(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                            GLenum func);
void v9x_gl_prim_blend_func(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                            GLenum src, GLenum dst);
void v9x_gl_prim_alpha_func(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                            GLenum func, GLclampf ref);
void v9x_gl_prim_depth_range(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                             GLclampd near_value, GLclampd far_value);
void v9x_gl_prim_polygon_offset(V9X_GL_STATE *state,
                                V9X_GL_PIPELINE *pipeline,
                                GLfloat factor, GLfloat units);

/*
 * glFog (3.9). `params` holds one value, or four for FOG_COLOR, which only
 * the vector forms (`vector` non-zero) accept. FOG_MODE's value is the
 * enum as a float. INVALID_ENUM for an unknown name, a colour through a
 * scalar form or an unknown mode; INVALID_VALUE for a negative density.
 * The integer forms convert before calling: a colour by table 2.6, the
 * rest directly.
 */
void v9x_gl_prim_fog(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                     GLenum pname, const GLfloat *params, int vector);

/*
 * The fog factor f for fog coordinate c (3.9), in [0, 1]: LINEAR
 * (end - c) / (end - start), one when start equals end; EXP e^(-dc); EXP2
 * e^(-(dc)^2). It is computed per vertex and interpolated, which the
 * specification allows.
 */
GLfloat v9x_gl_prim_fog_factor(const V9X_GL_PIPELINE *pipeline, GLfloat c);

/* glBegin, glVertex, glEnd. A vertex outside Begin/End is ignored, which is
 * what the specification leaves open (2.6.3). */
void v9x_gl_prim_begin(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                       GLenum mode);
void v9x_gl_prim_vertex(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                        GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void v9x_gl_prim_end(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline);

/* glRect (2.9): Begin(POLYGON), (x1,y1), (x2,y1), (x2,y2), (x1,y2) at
 * z 0, End. Inside Begin/End it is INVALID_OPERATION and adds nothing to
 * the primitive being assembled. */
void v9x_gl_prim_rect(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                      GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);

/* Hand what is batched to the sink now (glFlush, glFinish, a state change
 * the batch was not made with). */
void v9x_gl_prim_flush(V9X_GL_PIPELINE *pipeline);

/*
 * Whether two batches may be drawn as one: the same fragment state and the
 * same texture description - storage, format, the same level images (by
 * their first level's pixels and the count), filters, mip, address, the
 * combine and the environment colour. Vertices carry everything else. The
 * caller keeps a batch's texture levels alive and unchanged until it is
 * drawn, which is what makes the pixels pointer a fair identity.
 */
int v9x_gl_prim_same_draw(const V9X_R3D_ABI_TEXTURE *texture_a,
                          const V9X_R3D_ABI_STATE *state_a,
                          const V9X_R3D_ABI_TEXTURE *texture_b,
                          const V9X_R3D_ABI_STATE *state_b);

/* Non-zero when a fragment's alpha can change what is written: the alpha
 * test is on, or blending uses a source-alpha factor. There is no alpha
 * plane, so destination-alpha factors read one whatever the source. */
int v9x_gl_prim_fragment_alpha_used(const V9X_GL_STATE *state,
                                    const V9X_GL_PIPELINE *pipeline);

/*
 * Non-zero when the batch's alpha test is on and can discard nothing: the
 * alpha it tests is the vertices' (no texture, or a 565 one that passes the
 * fragment's through) or one (a 565 REPLACE), and every vertex passes with
 * a step to spare, so no interpolated or rounded value between them fails.
 * EQUAL and NOTEQUAL count only for one alpha across the batch. The ICD
 * then sends it without the test, which an engine that tests only texel
 * alpha (the Mach64) would otherwise refuse.
 */
/* Non-zero when the ABI state blends with a factor that reads alpha. */
int v9x_gl_prim_blend_reads_alpha(const V9X_R3D_ABI_STATE *state);

int v9x_gl_prim_alpha_test_passes(const V9X_R3D_ABI_STATE *state,
                                  const V9X_R3D_ABI_TEXTURE *texture,
                                  const V9X_R3D_ABI_VERTEX *vertices,
                                  v9x_u32 vertex_count);

/*
 * A two-unit draw no engine took, drawn as single-unit passes instead -
 * what the application would have drawn itself had GL_SGIS_multitexture
 * not been offered. Pass 0 is unit 0's draw as it stands; the passes
 * after it draw unit 1's texture at unit 1's coordinates over the pixels
 * pass 0 wrote, with a blend that applies unit 1's combine (GL 1.1 table
 * 3.18, RGB; the target has no alpha plane):
 *
 *   MODULATE    C = Cd * Ct          (DESTCOLOR, ZERO)
 *   REPLACE     C = Ct               no blend
 *   DECALALPHA  C = Cd(1-At) + Ct At (SRCALPHA, INVSRCALPHA)
 *   BLEND       C = Cd(1-Ct)         (ZERO, INVSRCCOLOR), then
 *               C += Cc Ct           (ONE, ONE), the vertex colour Cc,
 *                                    unless Cc is black (GL's default)
 *
 * Over the same pixels only: with depth writes the later passes test
 * EQUAL against the depth pass 0 wrote; without them they repeat pass 0's
 * test against the unchanged buffer. Triangles of one batch that overlap
 * without depth writes take unit 1 twice where they overlap.
 *
 * Returns the number of passes, 0 when the draw cannot be split exactly:
 * the application blends (the passes need the blender), the draw is
 * fogged (fog applies to the two units' combined colour, which no pass
 * holds), or an alpha test
 * that could discard has no depth writes to confine the later passes to
 * what it kept, or unit 1 changes the alpha a test reads.
 */
#define V9X_GL_SPLIT_PASSES_MAX 3u

typedef struct v9x_gl_split_pass {
    /* 0: unit 0's texture and coordinates; 1: unit 1's. */
    v9x_u32 unit;
    /* For a unit-1 pass, its texture's combine; unit 0's pass keeps its
     * own. */
    v9x_u32 color_op;
    v9x_u32 alpha_op;
    /* Non-zero: every vertex's colour is unit 1's environment colour. */
    v9x_u32 env_colour;
    V9X_R3D_ABI_STATE state;
} V9X_GL_SPLIT_PASS;

v9x_u32 v9x_gl_prim_split(const V9X_R3D_ABI_STATE *state,
                          const V9X_R3D_ABI_TEXTURE *texture1,
                          V9X_GL_SPLIT_PASS *passes);

/* The fragment state as the render interface takes it, from the GL state
 * and the pipeline's functions. */
void v9x_gl_prim_abi_state(V9X_GL_STATE *state,
                           const V9X_GL_PIPELINE *pipeline,
                           V9X_R3D_ABI_STATE *out);

#endif /* VELOCITY9X_GL_PRIM_H */
