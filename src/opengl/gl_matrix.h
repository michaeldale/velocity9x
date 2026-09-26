/*
 * The three matrix stacks of an OpenGL 1.1 context and the commands on them
 * (2.10.2): pure, no OS header, no C runtime. Part of V9X_GL_STATE; the
 * commands take the state so their errors land in its flag.
 *
 * Matrices are column-major, sixteen floats, element (row r, column c) at
 * index c * 4 + r, as glLoadMatrix takes them. Every command multiplies the
 * current matrix on the right: C = C * M.
 */
#ifndef VELOCITY9X_GL_MATRIX_H
#define VELOCITY9X_GL_MATRIX_H

#include "velocity9x/gl_types.h"

#define V9X_GL_MODELVIEW   0x1700u
#define V9X_GL_PROJECTION  0x1701u
#define V9X_GL_TEXTURE     0x1702u

#define V9X_GL_STACK_OVERFLOW  0x0503u
#define V9X_GL_STACK_UNDERFLOW 0x0504u

/* The minimum depths the specification requires (table 6.x), which is what
 * this implementation offers and what glGet will report. */
#define V9X_GL_MODELVIEW_DEPTH  32u
#define V9X_GL_PROJECTION_DEPTH 2u
#define V9X_GL_TEXTURE_DEPTH    2u

typedef struct v9x_gl_matrix {
    GLfloat m[16];
} V9X_GL_MATRIX;

typedef struct v9x_gl_matrices {
    GLenum mode;
    V9X_GL_MATRIX modelview[V9X_GL_MODELVIEW_DEPTH];
    V9X_GL_MATRIX projection[V9X_GL_PROJECTION_DEPTH];
    V9X_GL_MATRIX texture[V9X_GL_TEXTURE_DEPTH];
    /* The index of each stack's top. */
    unsigned int modelview_top;
    unsigned int projection_top;
    unsigned int texture_top;
} V9X_GL_MATRICES;

/* Every stack one identity deep, mode MODELVIEW. */
void v9x_gl_matrices_init(V9X_GL_MATRICES *matrices);

/* C = A * B, column-major; `out` may alias neither input. */
void v9x_gl_matrix_multiply(V9X_GL_MATRIX *out, const V9X_GL_MATRIX *a,
                            const V9X_GL_MATRIX *b);

/* The top of a stack, read-only; `which` is one of the three modes. */
const V9X_GL_MATRIX *v9x_gl_matrix_top(const V9X_GL_MATRICES *matrices,
                                       GLenum which);

#endif /* VELOCITY9X_GL_MATRIX_H */
