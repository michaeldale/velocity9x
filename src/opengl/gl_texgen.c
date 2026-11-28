/*
 * OpenGL 1.1 texture coordinate generation (gl_texgen.h). The ICD links no
 * C runtime, so the square root is the x87's through a pragma, as
 * gl_light.c's is.
 */
#include "gl_texgen.h"

#ifdef __WATCOMC__
static double v9x_gl_texgen_sqrt(double value);
#pragma aux v9x_gl_texgen_sqrt = "fsqrt" parm [8087] value [8087];
#else
#include <math.h>
#define v9x_gl_texgen_sqrt sqrt
#endif

void v9x_gl_texgen_init(V9X_GL_TEXGEN *texgen)
{
    unsigned int coord;
    unsigned int i;

    for (coord = 0u; coord < 4u; ++coord) {
        texgen->mode[coord] = V9X_GL_EYE_LINEAR;
        for (i = 0u; i < 4u; ++i) {
            texgen->object_plane[coord][i] =
                (coord < 2u && i == coord) ? 1.0f : 0.0f;
            texgen->eye_plane[coord][i] = texgen->object_plane[coord][i];
        }
    }
}

static int v9x_gl_texgen_coord(GLenum coord)
{
    if (coord >= V9X_GL_S && coord <= V9X_GL_Q) {
        return (int)(coord - V9X_GL_S);
    }
    return -1;
}

/*
 * The inverse of a column-major 4x4 by cofactors (the adjugate over the
 * determinant), as Mesa's invert_matrix_general computes it. Non-zero when
 * it exists; a singular matrix leaves out alone and returns zero.
 */
static int v9x_gl_texgen_invert(const GLfloat *m, GLfloat *out)
{
    double inv[16];
    double det;
    unsigned int i;

    inv[0] = (double)m[5] * m[10] * m[15] - (double)m[5] * m[11] * m[14] -
             (double)m[9] * m[6] * m[15] + (double)m[9] * m[7] * m[14] +
             (double)m[13] * m[6] * m[11] - (double)m[13] * m[7] * m[10];
    inv[4] = -(double)m[4] * m[10] * m[15] + (double)m[4] * m[11] * m[14] +
             (double)m[8] * m[6] * m[15] - (double)m[8] * m[7] * m[14] -
             (double)m[12] * m[6] * m[11] + (double)m[12] * m[7] * m[10];
    inv[8] = (double)m[4] * m[9] * m[15] - (double)m[4] * m[11] * m[13] -
             (double)m[8] * m[5] * m[15] + (double)m[8] * m[7] * m[13] +
             (double)m[12] * m[5] * m[11] - (double)m[12] * m[7] * m[9];
    inv[12] = -(double)m[4] * m[9] * m[14] + (double)m[4] * m[10] * m[13] +
              (double)m[8] * m[5] * m[14] - (double)m[8] * m[6] * m[13] -
              (double)m[12] * m[5] * m[10] + (double)m[12] * m[6] * m[9];
    inv[1] = -(double)m[1] * m[10] * m[15] + (double)m[1] * m[11] * m[14] +
             (double)m[9] * m[2] * m[15] - (double)m[9] * m[3] * m[14] -
             (double)m[13] * m[2] * m[11] + (double)m[13] * m[3] * m[10];
    inv[5] = (double)m[0] * m[10] * m[15] - (double)m[0] * m[11] * m[14] -
             (double)m[8] * m[2] * m[15] + (double)m[8] * m[3] * m[14] +
             (double)m[12] * m[2] * m[11] - (double)m[12] * m[3] * m[10];
    inv[9] = -(double)m[0] * m[9] * m[15] + (double)m[0] * m[11] * m[13] +
             (double)m[8] * m[1] * m[15] - (double)m[8] * m[3] * m[13] -
             (double)m[12] * m[1] * m[11] + (double)m[12] * m[3] * m[9];
    inv[13] = (double)m[0] * m[9] * m[14] - (double)m[0] * m[10] * m[13] -
              (double)m[8] * m[1] * m[14] + (double)m[8] * m[2] * m[13] +
              (double)m[12] * m[1] * m[10] - (double)m[12] * m[2] * m[9];
    inv[2] = (double)m[1] * m[6] * m[15] - (double)m[1] * m[7] * m[14] -
             (double)m[5] * m[2] * m[15] + (double)m[5] * m[3] * m[14] +
             (double)m[13] * m[2] * m[7] - (double)m[13] * m[3] * m[6];
    inv[6] = -(double)m[0] * m[6] * m[15] + (double)m[0] * m[7] * m[14] +
             (double)m[4] * m[2] * m[15] - (double)m[4] * m[3] * m[14] -
             (double)m[12] * m[2] * m[7] + (double)m[12] * m[3] * m[6];
    inv[10] = (double)m[0] * m[5] * m[15] - (double)m[0] * m[7] * m[13] -
              (double)m[4] * m[1] * m[15] + (double)m[4] * m[3] * m[13] +
              (double)m[12] * m[1] * m[7] - (double)m[12] * m[3] * m[5];
    inv[14] = -(double)m[0] * m[5] * m[14] + (double)m[0] * m[6] * m[13] +
              (double)m[4] * m[1] * m[14] - (double)m[4] * m[2] * m[13] -
              (double)m[12] * m[1] * m[6] + (double)m[12] * m[2] * m[5];
    inv[3] = -(double)m[1] * m[6] * m[11] + (double)m[1] * m[7] * m[10] +
             (double)m[5] * m[2] * m[11] - (double)m[5] * m[3] * m[10] -
             (double)m[9] * m[2] * m[7] + (double)m[9] * m[3] * m[6];
    inv[7] = (double)m[0] * m[6] * m[11] - (double)m[0] * m[7] * m[10] -
             (double)m[4] * m[2] * m[11] + (double)m[4] * m[3] * m[10] +
             (double)m[8] * m[2] * m[7] - (double)m[8] * m[3] * m[6];
    inv[11] = -(double)m[0] * m[5] * m[11] + (double)m[0] * m[7] * m[9] +
              (double)m[4] * m[1] * m[11] - (double)m[4] * m[3] * m[9] -
              (double)m[8] * m[1] * m[7] + (double)m[8] * m[3] * m[5];
    inv[15] = (double)m[0] * m[5] * m[10] - (double)m[0] * m[6] * m[9] -
              (double)m[4] * m[1] * m[10] + (double)m[4] * m[2] * m[9] +
              (double)m[8] * m[1] * m[6] - (double)m[8] * m[2] * m[5];
    det = (double)m[0] * inv[0] + (double)m[1] * inv[4] +
          (double)m[2] * inv[8] + (double)m[3] * inv[12];
    if (det == 0.0) {
        return 0;
    }
    for (i = 0u; i < 16u; ++i) {
        out[i] = (GLfloat)(inv[i] / det);
    }
    return 1;
}

void v9x_gl_texgen_set(V9X_GL_STATE *state, V9X_GL_TEXGEN *texgen,
                       GLenum coord, GLenum pname, const GLfloat *params,
                       const V9X_GL_MATRIX *modelview)
{
    int k = v9x_gl_texgen_coord(coord);
    unsigned int i;

    if (state->in_begin) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return;
    }
    if (k < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    switch (pname) {
    case V9X_GL_TEXTURE_GEN_MODE: {
        /* Matched as a float, not converted: a float-to-integer cast
         * needs the C runtime's helper, which the ICD does not link. */
        GLenum mode;

        if (params[0] == (GLfloat)V9X_GL_EYE_LINEAR) {
            mode = V9X_GL_EYE_LINEAR;
        } else if (params[0] == (GLfloat)V9X_GL_OBJECT_LINEAR) {
            mode = V9X_GL_OBJECT_LINEAR;
        } else if (params[0] == (GLfloat)V9X_GL_SPHERE_MAP) {
            mode = V9X_GL_SPHERE_MAP;
        } else {
            v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
            return;
        }
        if (mode == V9X_GL_SPHERE_MAP && k >= 2) {
            v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
            return;
        }
        texgen->mode[k] = mode;
        return;
    }
    case V9X_GL_OBJECT_PLANE:
        for (i = 0u; i < 4u; ++i) {
            texgen->object_plane[k][i] = params[i];
        }
        return;
    case V9X_GL_EYE_PLANE: {
        /* (p1' p2' p3' p4') = (p1 p2 p3 p4) M^-1: the row vector times the
         * inverse, so each result is the plane dotted with a column of it.
         * A singular model-view (2.10.4 leaves it undefined) keeps the
         * plane as given. */
        GLfloat inverse[16];
        unsigned int col;

        if (!v9x_gl_texgen_invert(modelview->m, inverse)) {
            for (i = 0u; i < 4u; ++i) {
                texgen->eye_plane[k][i] = params[i];
            }
            return;
        }
        for (col = 0u; col < 4u; ++col) {
            texgen->eye_plane[k][col] =
                params[0] * inverse[col * 4u] +
                params[1] * inverse[col * 4u + 1u] +
                params[2] * inverse[col * 4u + 2u] +
                params[3] * inverse[col * 4u + 3u];
        }
        return;
    }
    default:
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
}

unsigned int v9x_gl_texgen_get(V9X_GL_STATE *state,
                               const V9X_GL_TEXGEN *texgen, GLenum coord,
                               GLenum pname, GLfloat *out)
{
    int k = v9x_gl_texgen_coord(coord);
    unsigned int i;

    if (k < 0) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return 0u;
    }
    switch (pname) {
    case V9X_GL_TEXTURE_GEN_MODE:
        out[0] = (GLfloat)texgen->mode[k];
        return 1u;
    case V9X_GL_OBJECT_PLANE:
        for (i = 0u; i < 4u; ++i) {
            out[i] = texgen->object_plane[k][i];
        }
        return 4u;
    case V9X_GL_EYE_PLANE:
        for (i = 0u; i < 4u; ++i) {
            out[i] = texgen->eye_plane[k][i];
        }
        return 4u;
    default:
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return 0u;
    }
}

v9x_u32 v9x_gl_texgen_enabled(const V9X_GL_STATE *state)
{
    v9x_u32 mask = 0ul;

    if (v9x_gl_state_cap(state, V9X_GL_TEXTURE_GEN_S)) {
        mask |= 0x1ul;
    }
    if (v9x_gl_state_cap(state, V9X_GL_TEXTURE_GEN_T)) {
        mask |= 0x2ul;
    }
    if (v9x_gl_state_cap(state, V9X_GL_TEXTURE_GEN_R)) {
        mask |= 0x4ul;
    }
    if (v9x_gl_state_cap(state, V9X_GL_TEXTURE_GEN_Q)) {
        mask |= 0x8ul;
    }
    return mask;
}

static GLfloat v9x_gl_texgen_dot(const GLfloat *a, const GLfloat *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

/*
 * Sphere map (2.10.4): u the unit vector from the eye to the vertex, n'
 * the eye-space normal, r = u - 2 n' (n' . u), m = 2 sqrt(rx^2 + ry^2 +
 * (rz + 1)^2), s = rx / m + 1/2 and t = ry / m + 1/2. n' is the normal
 * through the inverse transpose of the model-view's upper 3x3 (2.10.3),
 * normalised only under GL_NORMALIZE. A vertex at the eye, or r = (0, 0,
 * -1), has no direction or no m: s and t are then 1/2.
 */
static void v9x_gl_texgen_sphere(const GLfloat *m, const GLfloat *eye,
                                 const GLfloat *normal, int normalize,
                                 GLfloat *st)
{
    GLfloat inverse[16];
    GLfloat n[3];
    GLfloat u[3];
    GLfloat r[3];
    double length;
    double dot;
    double scale;
    unsigned int i;

    st[0] = 0.5f;
    st[1] = 0.5f;
    if (v9x_gl_texgen_invert(m, inverse)) {
        /* Inverse transpose: row i of the transpose is column i of the
         * inverse, so n'_i = sum_j inverse[i * 4 + j] n_j. */
        for (i = 0u; i < 3u; ++i) {
            n[i] = inverse[i * 4u] * normal[0] +
                   inverse[i * 4u + 1u] * normal[1] +
                   inverse[i * 4u + 2u] * normal[2];
        }
    } else {
        n[0] = normal[0];
        n[1] = normal[1];
        n[2] = normal[2];
    }
    if (normalize) {
        length = v9x_gl_texgen_sqrt((double)n[0] * n[0] +
                                    (double)n[1] * n[1] +
                                    (double)n[2] * n[2]);
        if (length != 0.0) {
            for (i = 0u; i < 3u; ++i) {
                n[i] = (GLfloat)(n[i] / length);
            }
        }
    }
    length = v9x_gl_texgen_sqrt((double)eye[0] * eye[0] +
                                (double)eye[1] * eye[1] +
                                (double)eye[2] * eye[2]);
    if (length == 0.0) {
        return;
    }
    for (i = 0u; i < 3u; ++i) {
        u[i] = (GLfloat)(eye[i] / length);
    }
    dot = (double)n[0] * u[0] + (double)n[1] * u[1] + (double)n[2] * u[2];
    for (i = 0u; i < 3u; ++i) {
        r[i] = (GLfloat)(u[i] - 2.0 * n[i] * dot);
    }
    scale = 2.0 * v9x_gl_texgen_sqrt((double)r[0] * r[0] +
                                     (double)r[1] * r[1] +
                                     ((double)r[2] + 1.0) * (r[2] + 1.0));
    if (scale == 0.0) {
        return;
    }
    st[0] = (GLfloat)(r[0] / scale + 0.5);
    st[1] = (GLfloat)(r[1] / scale + 0.5);
}

void v9x_gl_texgen_apply(const V9X_GL_TEXGEN *texgen, v9x_u32 mask,
                         const V9X_GL_MATRIX *modelview,
                         const GLfloat *object, const GLfloat *normal,
                         int normalize, GLfloat *tex)
{
    const GLfloat *m = modelview->m;
    GLfloat eye[4];
    GLfloat sphere[2];
    int eye_done = 0;
    int sphere_done = 0;
    unsigned int k;
    unsigned int row;

    for (k = 0u; k < 4u; ++k) {
        if ((mask & (1ul << k)) == 0ul) {
            continue;
        }
        if (texgen->mode[k] == V9X_GL_OBJECT_LINEAR) {
            tex[k] = v9x_gl_texgen_dot(texgen->object_plane[k], object);
            continue;
        }
        if (!eye_done) {
            for (row = 0u; row < 4u; ++row) {
                eye[row] = m[row] * object[0] + m[4u + row] * object[1] +
                           m[8u + row] * object[2] +
                           m[12u + row] * object[3];
            }
            eye_done = 1;
        }
        if (texgen->mode[k] == V9X_GL_EYE_LINEAR) {
            tex[k] = v9x_gl_texgen_dot(texgen->eye_plane[k], eye);
            continue;
        }
        /* SPHERE_MAP, which set refuses for R and Q. */
        if (k < 2u) {
            if (!sphere_done) {
                v9x_gl_texgen_sphere(m, eye, normal, normalize, sphere);
                sphere_done = 1;
            }
            tex[k] = sphere[k];
        }
    }
}
