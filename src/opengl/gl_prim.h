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
} V9X_GL_VERTEX;

/* Where a full batch goes, and the state the batch was made with. The sink
 * answers non-zero when it took the batch; the pipeline then empties it. */
typedef int (*V9X_GL_SINK_FN)(void *user, const V9X_R3D_ABI_VERTEX *vertices,
                              v9x_u32 triangle_count);

typedef struct v9x_gl_pipeline {
    GLfloat color[4];
    GLfloat tex[4];
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
    /* The primitive being assembled: its mode, how many vertices so far,
     * and the ones the next triangle may need. */
    GLenum mode;
    v9x_u32 count;
    V9X_GL_VERTEX first;
    V9X_GL_VERTEX previous[3];
    /* The batch. */
    V9X_R3D_ABI_VERTEX batch[3u * V9X_R3D_ABI_BATCH_MAX];
    v9x_u32 batch_triangles;
    V9X_GL_SINK_FN sink;
    void *sink_user;
    /* Batches the sink refused; the triangles in them are lost. */
    v9x_u32 sink_failures;
} V9X_GL_PIPELINE;

/* The initial values of the pipeline's state (colour 1,1,1,1, texture
 * coordinate 0,0,0,1, normal 0,0,1, SMOOTH, BACK, CCW, LESS, ONE/ZERO, ALWAYS/0, depth
 * range 0..1) and no sink. */
void v9x_gl_pipeline_init(V9X_GL_PIPELINE *pipeline);
void v9x_gl_pipeline_sink(V9X_GL_PIPELINE *pipeline, V9X_GL_SINK_FN sink,
                          void *user);

/* Current attributes. Legal inside and outside Begin/End. */
void v9x_gl_prim_color(V9X_GL_PIPELINE *pipeline, GLfloat r, GLfloat g,
                       GLfloat b, GLfloat a);
void v9x_gl_prim_texcoord(V9X_GL_PIPELINE *pipeline, GLfloat s, GLfloat t,
                          GLfloat r, GLfloat q);
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

/* The fragment state as the render interface takes it, from the GL state
 * and the pipeline's functions. */
void v9x_gl_prim_abi_state(V9X_GL_STATE *state,
                           const V9X_GL_PIPELINE *pipeline,
                           V9X_R3D_ABI_STATE *out);

#endif /* VELOCITY9X_GL_PRIM_H */
