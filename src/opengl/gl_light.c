/*
 * OpenGL 1.1 lighting state and its T&L key (gl_light.h). The ICD links no
 * C runtime, so the cosine and square root are the x87's through pragmas,
 * as gl_matrix.c's are.
 */
#include "gl_light.h"

#ifdef __WATCOMC__
static double v9x_gl_light_cos(double value);
#pragma aux v9x_gl_light_cos = "fcos" parm [8087] value [8087];
static double v9x_gl_light_sqrt(double value);
#pragma aux v9x_gl_light_sqrt = "fsqrt" parm [8087] value [8087];
#else
#include <math.h>
#define v9x_gl_light_cos cos
#define v9x_gl_light_sqrt sqrt
#endif

#define V9X_GL_LIGHT_FRONT          0x0404u
#define V9X_GL_LIGHT_BACK           0x0405u
#define V9X_GL_LIGHT_FRONT_AND_BACK 0x0408u
#define V9X_GL_LIGHT_DEGREES_TO_RADIANS 0.017453292519943295
/* No OpenGL light has a range: the largest float, so the kernel's range
 * test never fires (r3d_tnl.h). */
#define V9X_GL_LIGHT_NO_RANGE 3.4e38f

static void v9x_gl_light_set4(GLfloat *out, GLfloat x, GLfloat y, GLfloat z,
                              GLfloat w)
{
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
}

static void v9x_gl_light_copy4(GLfloat *out, const GLfloat *in)
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
}

void v9x_gl_lighting_init(V9X_GL_LIGHTING *lighting)
{
    unsigned int i;

    for (i = 0u; i < V9X_GL_LIGHTS; ++i) {
        V9X_GL_LIGHT *light = &lighting->light[i];
        GLfloat on = i == 0u ? 1.0f : 0.0f;

        v9x_gl_light_set4(light->ambient, 0.0f, 0.0f, 0.0f, 1.0f);
        v9x_gl_light_set4(light->diffuse, on, on, on, 1.0f);
        v9x_gl_light_set4(light->specular, on, on, on, 1.0f);
        v9x_gl_light_set4(light->position, 0.0f, 0.0f, 1.0f, 0.0f);
        light->spot_direction[0] = 0.0f;
        light->spot_direction[1] = 0.0f;
        light->spot_direction[2] = -1.0f;
        light->spot_exponent = 0.0f;
        light->spot_cutoff = 180.0f;
        light->attenuation[0] = 1.0f;
        light->attenuation[1] = 0.0f;
        light->attenuation[2] = 0.0f;
    }
    for (i = 0u; i < 2u; ++i) {
        V9X_GL_MATERIAL *m = &lighting->material[i];

        v9x_gl_light_set4(m->ambient, 0.2f, 0.2f, 0.2f, 1.0f);
        v9x_gl_light_set4(m->diffuse, 0.8f, 0.8f, 0.8f, 1.0f);
        v9x_gl_light_set4(m->specular, 0.0f, 0.0f, 0.0f, 1.0f);
        v9x_gl_light_set4(m->emission, 0.0f, 0.0f, 0.0f, 1.0f);
        m->shininess = 0.0f;
    }
    v9x_gl_light_set4(lighting->model_ambient, 0.2f, 0.2f, 0.2f, 1.0f);
    lighting->local_viewer = 0u;
    lighting->two_side = 0u;
    lighting->color_material_face = V9X_GL_LIGHT_FRONT_AND_BACK;
    lighting->color_material_mode = V9X_GL_AMBIENT_AND_DIFFUSE;
}

/* The light's index, or -1. */
static int v9x_gl_light_index(GLenum light)
{
    if (light < V9X_GL_LIGHT0 || light >= V9X_GL_LIGHT0 + V9X_GL_LIGHTS) {
        return -1;
    }
    return (int)(light - V9X_GL_LIGHT0);
}

/* m (column-major) times (x, y, z, w). */
static void v9x_gl_light_transform(const GLfloat *m, const GLfloat *in,
                                   GLfloat w, GLfloat *out, unsigned int n)
{
    unsigned int row;

    for (row = 0u; row < n; ++row) {
        out[row] = m[row] * in[0] + m[4u + row] * in[1] +
                   m[8u + row] * in[2] + m[12u + row] * w;
    }
}

void v9x_gl_light(V9X_GL_STATE *state, V9X_GL_LIGHTING *lighting,
                  GLenum light, GLenum pname, const GLfloat *params,
                  int vector, const GLfloat *modelview)
{
    int index = v9x_gl_light_index(light);
    V9X_GL_LIGHT *l;

    if (index < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    l = &lighting->light[index];
    switch (pname) {
    case V9X_GL_AMBIENT:
    case V9X_GL_DIFFUSE:
    case V9X_GL_SPECULAR:
    case V9X_GL_POSITION:
    case V9X_GL_SPOT_DIRECTION:
        if (!vector) {
            v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
            return;
        }
        break;
    default:
        break;
    }
    switch (pname) {
    case V9X_GL_AMBIENT:
        v9x_gl_light_copy4(l->ambient, params);
        return;
    case V9X_GL_DIFFUSE:
        v9x_gl_light_copy4(l->diffuse, params);
        return;
    case V9X_GL_SPECULAR:
        v9x_gl_light_copy4(l->specular, params);
        return;
    case V9X_GL_POSITION:
        v9x_gl_light_transform(modelview, params, params[3], l->position,
                               4u);
        return;
    case V9X_GL_SPOT_DIRECTION:
        /* The upper 3x3 only: a direction has no translation. */
        v9x_gl_light_transform(modelview, params, 0.0f, l->spot_direction,
                               3u);
        return;
    case V9X_GL_SPOT_EXPONENT:
        if (params[0] < 0.0f || params[0] > 128.0f) {
            v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
            return;
        }
        l->spot_exponent = params[0];
        return;
    case V9X_GL_SPOT_CUTOFF:
        if ((params[0] < 0.0f || params[0] > 90.0f) &&
            params[0] != 180.0f) {
            v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
            return;
        }
        l->spot_cutoff = params[0];
        return;
    case V9X_GL_CONSTANT_ATTENUATION:
    case V9X_GL_LINEAR_ATTENUATION:
    case V9X_GL_QUADRATIC_ATTENUATION:
        if (params[0] < 0.0f) {
            v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
            return;
        }
        l->attenuation[pname - V9X_GL_CONSTANT_ATTENUATION] = params[0];
        return;
    default:
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
}

void v9x_gl_light_model(V9X_GL_STATE *state, V9X_GL_LIGHTING *lighting,
                        GLenum pname, const GLfloat *params, int vector)
{
    switch (pname) {
    case V9X_GL_LIGHT_MODEL_AMBIENT:
        if (!vector) {
            v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
            return;
        }
        v9x_gl_light_copy4(lighting->model_ambient, params);
        return;
    case V9X_GL_LIGHT_MODEL_LOCAL_VIEWER:
        lighting->local_viewer = params[0] != 0.0f ? 1u : 0u;
        return;
    case V9X_GL_LIGHT_MODEL_TWO_SIDE:
        lighting->two_side = params[0] != 0.0f ? 1u : 0u;
        return;
    default:
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
}

static int v9x_gl_light_face_ok(GLenum face)
{
    return face == V9X_GL_LIGHT_FRONT || face == V9X_GL_LIGHT_BACK ||
           face == V9X_GL_LIGHT_FRONT_AND_BACK;
}

void v9x_gl_material(V9X_GL_STATE *state, V9X_GL_LIGHTING *lighting,
                     GLenum face, GLenum pname, const GLfloat *params,
                     int vector)
{
    unsigned int f;

    if (!v9x_gl_light_face_ok(face)) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    if (pname == V9X_GL_SHININESS) {
        if (params[0] < 0.0f || params[0] > 128.0f) {
            v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
            return;
        }
    } else if (!vector ||
               (pname != V9X_GL_AMBIENT && pname != V9X_GL_DIFFUSE &&
                pname != V9X_GL_SPECULAR && pname != V9X_GL_EMISSION &&
                pname != V9X_GL_AMBIENT_AND_DIFFUSE &&
                pname != V9X_GL_COLOR_INDEXES)) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    for (f = 0u; f < 2u; ++f) {
        V9X_GL_MATERIAL *m = &lighting->material[f];

        if ((f == 0u && face == V9X_GL_LIGHT_BACK) ||
            (f == 1u && face == V9X_GL_LIGHT_FRONT)) {
            continue;
        }
        switch (pname) {
        case V9X_GL_AMBIENT:
            v9x_gl_light_copy4(m->ambient, params);
            break;
        case V9X_GL_DIFFUSE:
            v9x_gl_light_copy4(m->diffuse, params);
            break;
        case V9X_GL_AMBIENT_AND_DIFFUSE:
            v9x_gl_light_copy4(m->ambient, params);
            v9x_gl_light_copy4(m->diffuse, params);
            break;
        case V9X_GL_SPECULAR:
            v9x_gl_light_copy4(m->specular, params);
            break;
        case V9X_GL_EMISSION:
            v9x_gl_light_copy4(m->emission, params);
            break;
        case V9X_GL_SHININESS:
            m->shininess = params[0];
            break;
        default:
            break;  /* COLOR_INDEXES: no colour-index mode */
        }
    }
}

void v9x_gl_color_material(V9X_GL_STATE *state, V9X_GL_LIGHTING *lighting,
                           GLenum face, GLenum mode)
{
    if (!v9x_gl_light_face_ok(face) ||
        (mode != V9X_GL_EMISSION && mode != V9X_GL_AMBIENT &&
         mode != V9X_GL_DIFFUSE && mode != V9X_GL_SPECULAR &&
         mode != V9X_GL_AMBIENT_AND_DIFFUSE)) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    lighting->color_material_face = face;
    lighting->color_material_mode = mode;
}

unsigned int v9x_gl_get_light(V9X_GL_STATE *state,
                              const V9X_GL_LIGHTING *lighting, GLenum light,
                              GLenum pname, GLfloat *out)
{
    int index = v9x_gl_light_index(light);
    const V9X_GL_LIGHT *l;

    if (index < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return 0u;
    }
    l = &lighting->light[index];
    switch (pname) {
    case V9X_GL_AMBIENT:
        v9x_gl_light_copy4(out, l->ambient);
        return 4u;
    case V9X_GL_DIFFUSE:
        v9x_gl_light_copy4(out, l->diffuse);
        return 4u;
    case V9X_GL_SPECULAR:
        v9x_gl_light_copy4(out, l->specular);
        return 4u;
    case V9X_GL_POSITION:
        v9x_gl_light_copy4(out, l->position);
        return 4u;
    case V9X_GL_SPOT_DIRECTION:
        out[0] = l->spot_direction[0];
        out[1] = l->spot_direction[1];
        out[2] = l->spot_direction[2];
        return 3u;
    case V9X_GL_SPOT_EXPONENT:
        out[0] = l->spot_exponent;
        return 1u;
    case V9X_GL_SPOT_CUTOFF:
        out[0] = l->spot_cutoff;
        return 1u;
    case V9X_GL_CONSTANT_ATTENUATION:
    case V9X_GL_LINEAR_ATTENUATION:
    case V9X_GL_QUADRATIC_ATTENUATION:
        out[0] = l->attenuation[pname - V9X_GL_CONSTANT_ATTENUATION];
        return 1u;
    default:
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return 0u;
    }
}

unsigned int v9x_gl_get_material(V9X_GL_STATE *state,
                                 const V9X_GL_LIGHTING *lighting, GLenum face,
                                 GLenum pname, GLfloat *out)
{
    const V9X_GL_MATERIAL *m;

    if (face != V9X_GL_LIGHT_FRONT && face != V9X_GL_LIGHT_BACK) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return 0u;
    }
    m = &lighting->material[face == V9X_GL_LIGHT_BACK ? 1u : 0u];
    switch (pname) {
    case V9X_GL_AMBIENT:
        v9x_gl_light_copy4(out, m->ambient);
        return 4u;
    case V9X_GL_DIFFUSE:
        v9x_gl_light_copy4(out, m->diffuse);
        return 4u;
    case V9X_GL_SPECULAR:
        v9x_gl_light_copy4(out, m->specular);
        return 4u;
    case V9X_GL_EMISSION:
        v9x_gl_light_copy4(out, m->emission);
        return 4u;
    case V9X_GL_SHININESS:
        out[0] = m->shininess;
        return 1u;
    case V9X_GL_COLOR_INDEXES:
        out[0] = 0.0f;
        out[1] = 1.0f;
        out[2] = 1.0f;
        return 3u;
    default:
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return 0u;
    }
}

int v9x_gl_lighting_expressible(const V9X_GL_STATE *state,
                                const V9X_GL_LIGHTING *lighting)
{
    if (!v9x_gl_state_cap(state, V9X_GL_LIGHTING_CAP)) {
        return 1;
    }
    return !lighting->two_side;
}

/* Bits of the key's material sources a colour material sets: the terms
 * GL_COLOR_MATERIAL's mode names, from colour 0. */
static v9x_u32 v9x_gl_light_sources(GLenum mode)
{
    v9x_u32 c0 = V9X_R3D_TNL_SOURCE_COLOR0;

    switch (mode) {
    case V9X_GL_AMBIENT:
        return c0 << (2u * V9X_R3D_TNL_TERM_AMBIENT);
    case V9X_GL_DIFFUSE:
        return c0 << (2u * V9X_R3D_TNL_TERM_DIFFUSE);
    case V9X_GL_SPECULAR:
        return c0 << (2u * V9X_R3D_TNL_TERM_SPECULAR);
    case V9X_GL_EMISSION:
        return c0 << (2u * V9X_R3D_TNL_TERM_EMISSIVE);
    default:
        return (c0 << (2u * V9X_R3D_TNL_TERM_AMBIENT)) |
               (c0 << (2u * V9X_R3D_TNL_TERM_DIFFUSE));
    }
}

static int v9x_gl_light_rgb_zero(const GLfloat *c)
{
    return c[0] == 0.0f && c[1] == 0.0f && c[2] == 0.0f;
}

/* The rows of the inverse transpose of a 3x3 (row r, column c in m[r][c]):
 * the cofactors over the determinant. A singular matrix keeps itself. */
static void v9x_gl_light_normal_matrix(GLfloat m[3][4])
{
    GLfloat a[3][3];
    GLfloat det;
    unsigned int r;
    unsigned int c;

    for (r = 0u; r < 3u; ++r) {
        for (c = 0u; c < 3u; ++c) {
            a[r][c] = m[r][c];
        }
    }
    m[0][0] = a[1][1] * a[2][2] - a[1][2] * a[2][1];
    m[0][1] = a[1][2] * a[2][0] - a[1][0] * a[2][2];
    m[0][2] = a[1][0] * a[2][1] - a[1][1] * a[2][0];
    m[1][0] = a[0][2] * a[2][1] - a[0][1] * a[2][2];
    m[1][1] = a[0][0] * a[2][2] - a[0][2] * a[2][0];
    m[1][2] = a[0][1] * a[2][0] - a[0][0] * a[2][1];
    m[2][0] = a[0][1] * a[1][2] - a[0][2] * a[1][1];
    m[2][1] = a[0][2] * a[1][0] - a[0][0] * a[1][2];
    m[2][2] = a[0][0] * a[1][1] - a[0][1] * a[1][0];
    det = a[0][0] * m[0][0] + a[0][1] * m[0][1] + a[0][2] * m[0][2];
    if (det == 0.0f) {
        for (r = 0u; r < 3u; ++r) {
            for (c = 0u; c < 3u; ++c) {
                m[r][c] = a[r][c];
            }
        }
        return;
    }
    for (r = 0u; r < 3u; ++r) {
        for (c = 0u; c < 3u; ++c) {
            m[r][c] /= det;
        }
    }
}

void v9x_gl_lighting_tnl(const V9X_GL_STATE *state,
                         const V9X_GL_LIGHTING *lighting,
                         V9X_R3D_TNL_KEY *key,
                         V9X_R3D_TNL_CONSTANTS *constants)
{
    unsigned int n = 0u;
    unsigned int i;
    unsigned int f;
    int specular_light = 0;
    int specular_material;
    v9x_u32 sources = 0ul;

    if (!v9x_gl_state_cap(state, V9X_GL_LIGHTING_CAP)) {
        return;
    }
    key->flags |= V9X_R3D_TNL_LIGHTING;
    key->inputs |= V9X_R3D_TNL_IN_NORMAL;
    if (lighting->local_viewer) {
        key->flags |= V9X_R3D_TNL_LOCAL_VIEWER;
    }
    if (v9x_gl_state_cap(state, V9X_GL_NORMALIZE)) {
        key->flags |= V9X_R3D_TNL_NORMALIZE;
    }

    /* The colour material, front face only: the key cannot apply a source
     * to the back alone, and a back-only one leaves the front material. */
    if (v9x_gl_state_cap(state, V9X_GL_COLOR_MATERIAL) &&
        lighting->color_material_face != V9X_GL_LIGHT_BACK) {
        sources = v9x_gl_light_sources(lighting->color_material_mode);
    }
    key->material_sources = sources;

    for (i = 0u; i < V9X_GL_LIGHTS; ++i) {
        const V9X_GL_LIGHT *l = &lighting->light[i];
        V9X_R3D_TNL_LIGHT *out = &constants->light[n];
        v9x_u32 type;

        if (!v9x_gl_state_cap(state, (GLenum)(V9X_GL_LIGHT0 + i))) {
            continue;
        }
        v9x_gl_light_copy4(out->ambient, l->ambient);
        v9x_gl_light_copy4(out->diffuse, l->diffuse);
        v9x_gl_light_copy4(out->specular, l->specular);
        if (!v9x_gl_light_rgb_zero(l->specular)) {
            specular_light = 1;
        }
        if (l->position[3] == 0.0f) {
            /* Directional: the unit vector toward the light. */
            double length = v9x_gl_light_sqrt(
                (double)l->position[0] * l->position[0] +
                (double)l->position[1] * l->position[1] +
                (double)l->position[2] * l->position[2]);

            type = V9X_R3D_TNL_LIGHT_DIRECTIONAL;
            v9x_gl_light_set4(out->position, 0.0f, 0.0f, 0.0f, 0.0f);
            if (length > 0.0) {
                out->position[0] = (GLfloat)(l->position[0] / length);
                out->position[1] = (GLfloat)(l->position[1] / length);
                out->position[2] = (GLfloat)(l->position[2] / length);
            }
        } else {
            double length = v9x_gl_light_sqrt(
                (double)l->spot_direction[0] * l->spot_direction[0] +
                (double)l->spot_direction[1] * l->spot_direction[1] +
                (double)l->spot_direction[2] * l->spot_direction[2]);

            type = l->spot_cutoff == 180.0f ? V9X_R3D_TNL_LIGHT_POINT
                                             : V9X_R3D_TNL_LIGHT_SPOT;
            v9x_gl_light_set4(out->position,
                              l->position[0] / l->position[3],
                              l->position[1] / l->position[3],
                              l->position[2] / l->position[3], 1.0f);
            v9x_gl_light_set4(out->attenuation, l->attenuation[0],
                              l->attenuation[1], l->attenuation[2],
                              V9X_GL_LIGHT_NO_RANGE);
            v9x_gl_light_set4(out->direction, 0.0f, 0.0f, 0.0f, 0.0f);
            if (length > 0.0) {
                out->direction[0] = (GLfloat)(l->spot_direction[0] / length);
                out->direction[1] = (GLfloat)(l->spot_direction[1] / length);
                out->direction[2] = (GLfloat)(l->spot_direction[2] / length);
            }
            v9x_gl_light_set4(
                out->spot,
                (GLfloat)v9x_gl_light_cos(
                    (double)l->spot_cutoff * V9X_GL_LIGHT_DEGREES_TO_RADIANS),
                l->spot_exponent, 0.0f, 0.0f);
        }
        key->light_types |= type << (2u * n);
        ++n;
    }
    key->light_count = n;

    for (f = 0u; f < 2u; ++f) {
        const V9X_GL_MATERIAL *m = &lighting->material[f];
        V9X_R3D_TNL_MATERIAL *out = &constants->material[f];

        v9x_gl_light_copy4(out->ambient, m->ambient);
        v9x_gl_light_copy4(out->diffuse, m->diffuse);
        v9x_gl_light_copy4(out->specular, m->specular);
        v9x_gl_light_copy4(out->emissive, m->emission);
        v9x_gl_light_set4(out->power, m->shininess, 0.0f, 0.0f, 0.0f);
    }
    /* Specular is worth computing only when some light has it and the
     * material does, or takes it from the colour. */
    specular_material =
        !v9x_gl_light_rgb_zero(lighting->material[0].specular) ||
        V9X_R3D_TNL_SOURCE(sources, V9X_R3D_TNL_TERM_SPECULAR) !=
            V9X_R3D_TNL_SOURCE_MATERIAL;
    if (specular_light && specular_material) {
        key->flags |= V9X_R3D_TNL_SPECULAR;
    }

    v9x_gl_light_copy4(constants->scene_ambient, lighting->model_ambient);
    v9x_gl_light_set4(constants->viewer, 0.0f, 0.0f, 1.0f, 0.0f);
    for (i = 0u; i < 3u; ++i) {
        unsigned int c;

        for (c = 0u; c < 3u; ++c) {
            constants->normal[i][c] = constants->model_view[i][c];
        }
        constants->normal[i][3] = 0.0f;
    }
    v9x_gl_light_normal_matrix(constants->normal);
}
