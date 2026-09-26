/*
 * glGetBooleanv, glGetIntegerv, glGetFloatv and glGetDoublev (6.1.1-6.1.2)
 * over the ICD's state. Pure. Each query computes its values once, with a
 * class that says how they convert: a Boolean request answers "not zero";
 * an integer request rounds, except that colours and depths map [0,1]
 * linearly onto [0, 2^31 - 1] as 6.1.2 requires; a float or double request
 * takes the value as it is.
 */
#ifndef VELOCITY9X_GL_GET_H
#define VELOCITY9X_GL_GET_H

#include "gl_prim.h"
#include "gl_texture.h"

#define V9X_GL_GET_BOOLEAN 0
#define V9X_GL_GET_INTEGER 1
#define V9X_GL_GET_FLOAT   2
#define V9X_GL_GET_DOUBLE  3

/* `out` is a GLboolean, GLint, GLfloat or GLdouble array by `kind`, with
 * room for the query's values (16 for a matrix). An unknown pname is
 * INVALID_ENUM and writes nothing; inside Begin/End every query is
 * INVALID_OPERATION. */
void v9x_gl_get(V9X_GL_STATE *state, const V9X_GL_PIPELINE *pipeline,
                const V9X_GL_TEXTURES *textures, GLenum pname, int kind,
                void *out);

#endif /* VELOCITY9X_GL_GET_H */
