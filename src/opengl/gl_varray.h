/*
 * Vertex arrays (OpenGL 1.1 section 2.8): the six client arrays, their
 * pointers and enables, glArrayElement, glDrawArrays, glDrawElements and
 * glInterleavedArrays. Pure: an element is fetched from client memory,
 * converted (table 2.6 for colours and normals), and fed to the vertex
 * pipeline (gl_prim.c) exactly as the immediate-mode commands would be,
 * so the arrays and glBegin/glEnd draw the same triangles.
 *
 * The index array is held but never read: there is no colour-index mode.
 * The edge-flag array is held but not read either, until polygon mode
 * LINE and POINT are drawn.
 */
#ifndef VELOCITY9X_GL_VARRAY_H
#define VELOCITY9X_GL_VARRAY_H

#include "gl_prim.h"
#include "gl_texture.h"          /* V9X_GL_UNSIGNED_BYTE */

#define V9X_GL_ARRAY_VERTEX     0u
#define V9X_GL_ARRAY_NORMAL     1u
#define V9X_GL_ARRAY_COLOR      2u
#define V9X_GL_ARRAY_INDEX      3u
#define V9X_GL_ARRAY_TEXCOORD   4u
#define V9X_GL_ARRAY_EDGE_FLAG  5u
#define V9X_GL_ARRAY_COUNT      6u

/* The client-state names, glEnableClientState's caps. */
#define V9X_GL_VERTEX_ARRAY         0x8074u
#define V9X_GL_NORMAL_ARRAY         0x8075u
#define V9X_GL_COLOR_ARRAY          0x8076u
#define V9X_GL_INDEX_ARRAY          0x8077u
#define V9X_GL_TEXTURE_COORD_ARRAY  0x8078u
#define V9X_GL_EDGE_FLAG_ARRAY      0x8079u

#define V9X_GL_BYTE                 0x1400u
#define V9X_GL_SHORT                0x1402u
#define V9X_GL_UNSIGNED_SHORT       0x1403u
#define V9X_GL_INT                  0x1404u
#define V9X_GL_UNSIGNED_INT         0x1405u
#define V9X_GL_FLOAT                0x1406u
#define V9X_GL_DOUBLE               0x140Au

typedef struct v9x_gl_array {
    int enabled;
    GLint size;
    GLenum type;
    GLsizei stride;             /* as given; zero means tightly packed */
    const void *pointer;
} V9X_GL_ARRAY;

typedef struct v9x_gl_arrays {
    V9X_GL_ARRAY array[V9X_GL_ARRAY_COUNT];
} V9X_GL_ARRAYS;

/* Table 6.6's initial values: every array disabled, null, stride 0, size
 * 4 (3 for normals, 1 for indices and edge flags), type FLOAT (BOOLEAN
 * for edge flags, which have no type). */
void v9x_gl_arrays_init(V9X_GL_ARRAYS *arrays);

/* glEnableClientState / glDisableClientState. INVALID_ENUM for any other
 * name. */
void v9x_gl_arrays_client_state(V9X_GL_STATE *state, V9X_GL_ARRAYS *arrays,
                                GLenum cap, int enable);
/* For glIsEnabled: non-zero when `cap` names a client array, with its
 * enable in *enabled; zero for any other cap (the caller asks gl_state). */
int v9x_gl_arrays_is_enabled(const V9X_GL_ARRAYS *arrays, GLenum cap,
                             GLboolean *enabled);

/* gl{Vertex,Normal,Color,Index,TexCoord}Pointer with 2.8's size, type and
 * stride errors; `which` is V9X_GL_ARRAY_*. Normals and indices have no
 * size argument: pass 3 and 1. */
void v9x_gl_arrays_pointer(V9X_GL_STATE *state, V9X_GL_ARRAYS *arrays,
                           unsigned int which, GLint size, GLenum type,
                           GLsizei stride, const void *pointer);
void v9x_gl_arrays_edge_flag_pointer(V9X_GL_STATE *state,
                                     V9X_GL_ARRAYS *arrays, GLsizei stride,
                                     const void *pointer);
/* glGetPointerv. The feedback and selection buffer pointers answer null,
 * their initial value, until those modes exist. */
void v9x_gl_arrays_get_pointer(V9X_GL_STATE *state,
                               const V9X_GL_ARRAYS *arrays, GLenum pname,
                               void **out);

void v9x_gl_arrays_element(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                           const V9X_GL_ARRAYS *arrays, GLint index);
void v9x_gl_arrays_draw(V9X_GL_STATE *state, V9X_GL_PIPELINE *pipeline,
                        const V9X_GL_ARRAYS *arrays, GLenum mode,
                        GLint first, GLsizei count);
void v9x_gl_arrays_draw_elements(V9X_GL_STATE *state,
                                 V9X_GL_PIPELINE *pipeline,
                                 const V9X_GL_ARRAYS *arrays, GLenum mode,
                                 GLsizei count, GLenum type,
                                 const void *indices);
void v9x_gl_arrays_interleaved(V9X_GL_STATE *state, V9X_GL_ARRAYS *arrays,
                               GLenum format, GLsizei stride,
                               const void *pointer);

#endif /* VELOCITY9X_GL_VARRAY_H */
