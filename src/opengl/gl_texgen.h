/*
 * OpenGL 1.1 texture coordinate generation (2.10.4) and its queries
 * (6.1.3, glGetTexGen). Pure, like gl_light.c: the ICD holds one
 * V9X_GL_TEXGEN a context, the pipeline (gl_prim.c) asks it for a
 * vertex's coordinates, and every rule is a host test
 * (tests\host\test_gl_texgen.c). docs\plans\opengl-display-lists-and-texgen.md.
 *
 * One set of state, applied to texture unit 0: texgen per unit
 * (GL_ARB_multitexture) is not kept. The enables are gl_state.c's
 * TEXTURE_GEN_S/T/R/Q caps.
 */
#ifndef VELOCITY9X_GL_TEXGEN_H
#define VELOCITY9X_GL_TEXGEN_H

#include "gl_state.h"

#define V9X_GL_S                   0x2000u
#define V9X_GL_T                   0x2001u
#define V9X_GL_R                   0x2002u
#define V9X_GL_Q                   0x2003u
#define V9X_GL_TEXTURE_GEN_MODE    0x2500u
#define V9X_GL_OBJECT_PLANE        0x2501u
#define V9X_GL_EYE_PLANE           0x2502u
#define V9X_GL_EYE_LINEAR          0x2400u
#define V9X_GL_OBJECT_LINEAR       0x2401u
#define V9X_GL_SPHERE_MAP          0x2402u
#define V9X_GL_TEXTURE_GEN_S       0x0C60u
#define V9X_GL_TEXTURE_GEN_T       0x0C61u
#define V9X_GL_TEXTURE_GEN_R       0x0C62u
#define V9X_GL_TEXTURE_GEN_Q       0x0C63u

typedef struct v9x_gl_texgen {
    /* S, T, R, Q in that order. */
    GLenum mode[4];
    GLfloat object_plane[4][4];
    /* As given times the inverse of the model-view at the call (2.10.4). */
    GLfloat eye_plane[4][4];
} V9X_GL_TEXGEN;

/* Table 6.14's initial values: every mode EYE_LINEAR, S's planes
 * (1, 0, 0, 0), T's (0, 1, 0, 0), R's and Q's zero. */
void v9x_gl_texgen_init(V9X_GL_TEXGEN *texgen);

/*
 * glTexGen{i,f,d}{,v}, every form converted to floats by the caller:
 * TEXTURE_GEN_MODE takes params[0] as the mode, the planes four values.
 * INVALID_ENUM for another coord or pname, a mode that is not one of the
 * three, or SPHERE_MAP for R or Q; INVALID_OPERATION between Begin and
 * End. `modelview` is the model-view top, which an eye plane is taken
 * through.
 */
void v9x_gl_texgen_set(V9X_GL_STATE *state, V9X_GL_TEXGEN *texgen,
                       GLenum coord, GLenum pname, const GLfloat *params,
                       const V9X_GL_MATRIX *modelview);

/* glGetTexGen*v: the values written to out (1 or 4), or zero with
 * INVALID_ENUM set for a coord or pname it does not name. */
unsigned int v9x_gl_texgen_get(V9X_GL_STATE *state,
                               const V9X_GL_TEXGEN *texgen, GLenum coord,
                               GLenum pname, GLfloat *out);

/* Bit k set when coordinate k (S, T, R, Q) is enabled. */
v9x_u32 v9x_gl_texgen_enabled(const V9X_GL_STATE *state);

/*
 * The coordinates of one vertex: each coordinate in `mask` is generated
 * into tex[k], the others are left as they are. `object` is the vertex as
 * given, `normal` the current normal, `normalize` GL_NORMALIZE's enable.
 */
void v9x_gl_texgen_apply(const V9X_GL_TEXGEN *texgen, v9x_u32 mask,
                         const V9X_GL_MATRIX *modelview,
                         const GLfloat *object, const GLfloat *normal,
                         int normalize, GLfloat *tex);

#endif /* VELOCITY9X_GL_TEXGEN_H */
