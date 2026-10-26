/*
 * OpenGL 1.1 lighting state (2.13) and what it gives a T&L draw
 * (docs\plans\hardware-tnl.md, step 4). Pure, like gl_state.c.
 *
 * The ICD keeps this state for every context, and only an engine that runs
 * T&L keys draws it: on every other chip, and for anything the key cannot
 * express, primitives stay unlit as they always were.
 *
 * Light positions and spot directions are stored in eye coordinates, as the
 * specification requires: transformed by the model-view matrix current when
 * glLight is called (2.13.2).
 */
#ifndef VELOCITY9X_GL_LIGHT_H
#define VELOCITY9X_GL_LIGHT_H

#include "velocity9x/r3d_tnl.h"
#include "gl_state.h"

#define V9X_GL_LIGHTS 8u

#define V9X_GL_LIGHT0                 0x4000u
#define V9X_GL_AMBIENT                0x1200u
#define V9X_GL_DIFFUSE                0x1201u
#define V9X_GL_SPECULAR               0x1202u
#define V9X_GL_POSITION               0x1203u
#define V9X_GL_SPOT_DIRECTION         0x1204u
#define V9X_GL_SPOT_EXPONENT          0x1205u
#define V9X_GL_SPOT_CUTOFF            0x1206u
#define V9X_GL_CONSTANT_ATTENUATION   0x1207u
#define V9X_GL_LINEAR_ATTENUATION     0x1208u
#define V9X_GL_QUADRATIC_ATTENUATION  0x1209u
#define V9X_GL_EMISSION               0x1600u
#define V9X_GL_SHININESS              0x1601u
#define V9X_GL_AMBIENT_AND_DIFFUSE    0x1602u
#define V9X_GL_COLOR_INDEXES          0x1603u
#define V9X_GL_LIGHT_MODEL_LOCAL_VIEWER 0x0B51u
#define V9X_GL_LIGHT_MODEL_TWO_SIDE   0x0B52u
#define V9X_GL_LIGHT_MODEL_AMBIENT    0x0B53u
#define V9X_GL_LIGHTING_CAP           0x0B50u
#define V9X_GL_COLOR_MATERIAL         0x0B57u
#define V9X_GL_NORMALIZE              0x0BA1u

typedef struct v9x_gl_light {
    GLfloat ambient[4];
    GLfloat diffuse[4];
    GLfloat specular[4];
    GLfloat position[4];        /* eye coordinates */
    GLfloat spot_direction[3];  /* eye coordinates */
    GLfloat spot_exponent;
    GLfloat spot_cutoff;        /* degrees; 180 is no spot */
    GLfloat attenuation[3];     /* constant, linear, quadratic */
} V9X_GL_LIGHT;

typedef struct v9x_gl_material {
    GLfloat ambient[4];
    GLfloat diffuse[4];
    GLfloat specular[4];
    GLfloat emission[4];
    GLfloat shininess;
} V9X_GL_MATERIAL;

typedef struct v9x_gl_lighting {
    V9X_GL_LIGHT light[V9X_GL_LIGHTS];
    V9X_GL_MATERIAL material[2];        /* front, back */
    GLfloat model_ambient[4];
    GLboolean local_viewer;
    GLboolean two_side;
    GLenum color_material_face;
    GLenum color_material_mode;
} V9X_GL_LIGHTING;

/* Table 6.9's initial values: light 0 white diffuse and specular, the rest
 * black; every light at (0, 0, 1, 0) pointing down -z with no spot. */
void v9x_gl_lighting_init(V9X_GL_LIGHTING *lighting);

/*
 * glLight (2.13.2). params holds one value, or four (three for
 * SPOT_DIRECTION), which only the vector forms (`vector` non-zero) accept.
 * POSITION and SPOT_DIRECTION are taken through `modelview` (column-major).
 * INVALID_ENUM for an unknown light or name, or a vector name through a
 * scalar form; INVALID_VALUE for a spot exponent outside [0, 128], a cutoff
 * outside [0, 90] other than 180, or a negative attenuation.
 */
void v9x_gl_light(V9X_GL_STATE *state, V9X_GL_LIGHTING *lighting,
                  GLenum light, GLenum pname, const GLfloat *params,
                  int vector, const GLfloat *modelview);

/* glLightModel: AMBIENT (vector forms only), LOCAL_VIEWER and TWO_SIDE. */
void v9x_gl_light_model(V9X_GL_STATE *state, V9X_GL_LIGHTING *lighting,
                        GLenum pname, const GLfloat *params, int vector);

/*
 * glMaterial: FRONT, BACK or FRONT_AND_BACK; AMBIENT, DIFFUSE, SPECULAR,
 * EMISSION, AMBIENT_AND_DIFFUSE and COLOR_INDEXES (accepted, there being no
 * colour-index mode) through the vector forms, SHININESS (0 to 128) through
 * either.
 */
void v9x_gl_material(V9X_GL_STATE *state, V9X_GL_LIGHTING *lighting,
                     GLenum face, GLenum pname, const GLfloat *params,
                     int vector);

/* glColorMaterial: which face and which terms follow the current colour
 * while COLOR_MATERIAL is enabled. */
void v9x_gl_color_material(V9X_GL_STATE *state, V9X_GL_LIGHTING *lighting,
                           GLenum face, GLenum mode);

/* glGetLight and glGetMaterial: the values written to out (up to four),
 * or 0 with INVALID_ENUM. A material query names FRONT or BACK. */
unsigned int v9x_gl_get_light(V9X_GL_STATE *state,
                              const V9X_GL_LIGHTING *lighting, GLenum light,
                              GLenum pname, GLfloat *out);
unsigned int v9x_gl_get_material(V9X_GL_STATE *state,
                                 const V9X_GL_LIGHTING *lighting, GLenum face,
                                 GLenum pname, GLfloat *out);

/*
 * Whether lit primitives can be drawn through a T&L key: lighting off, or
 * every term the key carries expressible - not two-sided (the engine has no
 * back colours yet), and a colour material that applies to the front face.
 */
int v9x_gl_lighting_expressible(const V9X_GL_STATE *state,
                                const V9X_GL_LIGHTING *lighting);

/*
 * The lighting half of a T&L draw: into a key the front end has already
 * filled for an unlit draw (model, inputs, textures) and its constants
 * (matrices included), the flags, lights, material sources and normal input,
 * and the light, material and normal-matrix constants. Lights are taken in
 * the order LIGHT0 to LIGHT7, enabled ones only. Does nothing with LIGHTING
 * disabled.
 */
void v9x_gl_lighting_tnl(const V9X_GL_STATE *state,
                         const V9X_GL_LIGHTING *lighting,
                         V9X_R3D_TNL_KEY *key,
                         V9X_R3D_TNL_CONSTANTS *constants);

#endif /* VELOCITY9X_GL_LIGHT_H */
