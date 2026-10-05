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
 * ARGB, specular alpha the fog factor (255: no fog yet).
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
} V9X_GL_VERTEX;

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
    /* POLYGON_OFFSET_FILL as it was at Begin; it cannot change before
     * End, and the triangles do not look it up each. */
    int offset_on;
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
 * range 0..1) and no sink. */
void v9x_gl_pipeline_init(V9X_GL_PIPELINE *pipeline);
void v9x_gl_pipeline_sink(V9X_GL_PIPELINE *pipeline, V9X_GL_SINK_FN sink,
                          void *user);
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

/* glBegin, glVertex, glEnd. A vertex outside Begin/End is ignored, which is
 * what the specification leaves open (2.6.3). */
void v9x_gl_prim_begin(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                       GLenum mode);
void v9x_gl_prim_vertex(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                        GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void v9x_gl_prim_end(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline);

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

/* The fragment state as the render interface takes it, from the GL state
 * and the pipeline's functions. */
void v9x_gl_prim_abi_state(V9X_GL_STATE *state,
                           const V9X_GL_PIPELINE *pipeline,
                           V9X_R3D_ABI_STATE *out);

#endif /* VELOCITY9X_GL_PRIM_H */
