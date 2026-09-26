/*
 * The matrix stacks (gl_matrix.h) and the matrix commands of V9X_GL_STATE
 * (2.10.2). Pure; the ICD links no C runtime, so the trigonometry glRotate
 * needs is the x87's own fsin, fcos and fsqrt through pragmas, which the
 * Watcom host build uses as well.
 */
#include "gl_state.h"

#ifdef __WATCOMC__
static double v9x_gl_sin(double value);
#pragma aux v9x_gl_sin = "fsin" parm [8087] value [8087];
static double v9x_gl_cos(double value);
#pragma aux v9x_gl_cos = "fcos" parm [8087] value [8087];
static double v9x_gl_sqrt(double value);
#pragma aux v9x_gl_sqrt = "fsqrt" parm [8087] value [8087];
#else
#include <math.h>
#define v9x_gl_sin sin
#define v9x_gl_cos cos
#define v9x_gl_sqrt sqrt
#endif

#define V9X_GL_DEGREES_TO_RADIANS 0.017453292519943295

static void v9x_gl_identity(V9X_GL_MATRIX *matrix)
{
    unsigned int index;

    for (index = 0u; index < 16u; ++index) {
        matrix->m[index] = (index % 5u) == 0u ? 1.0f : 0.0f;
    }
}

void v9x_gl_matrices_init(V9X_GL_MATRICES *matrices)
{
    matrices->mode = V9X_GL_MODELVIEW;
    matrices->modelview_top = 0u;
    matrices->projection_top = 0u;
    matrices->texture_top = 0u;
    v9x_gl_identity(&matrices->modelview[0]);
    v9x_gl_identity(&matrices->projection[0]);
    v9x_gl_identity(&matrices->texture[0]);
}

void v9x_gl_matrix_multiply(V9X_GL_MATRIX *out, const V9X_GL_MATRIX *a,
                            const V9X_GL_MATRIX *b)
{
    unsigned int row;
    unsigned int column;
    unsigned int k;

    for (column = 0u; column < 4u; ++column) {
        for (row = 0u; row < 4u; ++row) {
            GLfloat sum = 0.0f;

            for (k = 0u; k < 4u; ++k) {
                sum += a->m[k * 4u + row] * b->m[column * 4u + k];
            }
            out->m[column * 4u + row] = sum;
        }
    }
}

const V9X_GL_MATRIX *v9x_gl_matrix_top(const V9X_GL_MATRICES *matrices,
                                       GLenum which)
{
    if (which == V9X_GL_PROJECTION) {
        return &matrices->projection[matrices->projection_top];
    }
    if (which == V9X_GL_TEXTURE) {
        return &matrices->texture[matrices->texture_top];
    }
    return &matrices->modelview[matrices->modelview_top];
}

/* The current mode's top, writable. */
static V9X_GL_MATRIX *v9x_gl_current_matrix(V9X_GL_STATE *state)
{
    return (V9X_GL_MATRIX *)v9x_gl_matrix_top(&state->matrices,
                                              state->matrices.mode);
}

static int v9x_gl_matrix_allowed(V9X_GL_STATE *state)
{
    if (state->in_begin) {
        v9x_gl_state_error(state, V9X_GL_INVALID_OPERATION);
        return 0;
    }
    return 1;
}

/* C = C * M, the one operation every command below is. */
static void v9x_gl_right_multiply(V9X_GL_STATE *state,
                                  const V9X_GL_MATRIX *matrix)
{
    V9X_GL_MATRIX *current = v9x_gl_current_matrix(state);
    V9X_GL_MATRIX product;

    v9x_gl_matrix_multiply(&product, current, matrix);
    *current = product;
}

void v9x_gl_state_matrix_mode(V9X_GL_STATE *state, GLenum mode)
{
    if (!v9x_gl_matrix_allowed(state)) {
        return;
    }
    if (mode != V9X_GL_MODELVIEW && mode != V9X_GL_PROJECTION &&
        mode != V9X_GL_TEXTURE) {
        v9x_gl_state_error(state, V9X_GL_INVALID_ENUM);
        return;
    }
    state->matrices.mode = mode;
}

void v9x_gl_state_load_identity(V9X_GL_STATE *state)
{
    if (!v9x_gl_matrix_allowed(state)) {
        return;
    }
    v9x_gl_identity(v9x_gl_current_matrix(state));
}

void v9x_gl_state_load_matrix(V9X_GL_STATE *state, const GLfloat *m)
{
    V9X_GL_MATRIX *current;
    unsigned int index;

    if (!v9x_gl_matrix_allowed(state)) {
        return;
    }
    current = v9x_gl_current_matrix(state);
    for (index = 0u; index < 16u; ++index) {
        current->m[index] = m[index];
    }
}

void v9x_gl_state_mult_matrix(V9X_GL_STATE *state, const GLfloat *m)
{
    V9X_GL_MATRIX matrix;
    unsigned int index;

    if (!v9x_gl_matrix_allowed(state)) {
        return;
    }
    for (index = 0u; index < 16u; ++index) {
        matrix.m[index] = m[index];
    }
    v9x_gl_right_multiply(state, &matrix);
}

void v9x_gl_state_translate(V9X_GL_STATE *state, GLfloat x, GLfloat y,
                            GLfloat z)
{
    V9X_GL_MATRIX matrix;

    if (!v9x_gl_matrix_allowed(state)) {
        return;
    }
    v9x_gl_identity(&matrix);
    matrix.m[12] = x;
    matrix.m[13] = y;
    matrix.m[14] = z;
    v9x_gl_right_multiply(state, &matrix);
}

void v9x_gl_state_scale(V9X_GL_STATE *state, GLfloat x, GLfloat y,
                        GLfloat z)
{
    V9X_GL_MATRIX matrix;

    if (!v9x_gl_matrix_allowed(state)) {
        return;
    }
    v9x_gl_identity(&matrix);
    matrix.m[0] = x;
    matrix.m[5] = y;
    matrix.m[10] = z;
    v9x_gl_right_multiply(state, &matrix);
}

/*
 * The rotation of `angle` degrees about (x, y, z), normalised, by the
 * specification's formula: R = u u^T + cos(a) (I - u u^T) + sin(a) S,
 * S the cross-product matrix of u. A zero axis leaves the matrix alone -
 * the specification does not define it, and doing nothing is what every
 * implementation this project has read does.
 */
void v9x_gl_state_rotate(V9X_GL_STATE *state, GLfloat angle, GLfloat x,
                         GLfloat y, GLfloat z)
{
    V9X_GL_MATRIX matrix;
    double length;
    double radians;
    double c;
    double s;
    double t;
    double ux;
    double uy;
    double uz;

    if (!v9x_gl_matrix_allowed(state)) {
        return;
    }
    length = v9x_gl_sqrt((double)x * x + (double)y * y + (double)z * z);
    if (length == 0.0) {
        return;
    }
    ux = (double)x / length;
    uy = (double)y / length;
    uz = (double)z / length;
    radians = (double)angle * V9X_GL_DEGREES_TO_RADIANS;
    c = v9x_gl_cos(radians);
    s = v9x_gl_sin(radians);
    t = 1.0 - c;

    v9x_gl_identity(&matrix);
    matrix.m[0] = (GLfloat)(ux * ux * t + c);
    matrix.m[1] = (GLfloat)(uy * ux * t + uz * s);
    matrix.m[2] = (GLfloat)(uz * ux * t - uy * s);
    matrix.m[4] = (GLfloat)(ux * uy * t - uz * s);
    matrix.m[5] = (GLfloat)(uy * uy * t + c);
    matrix.m[6] = (GLfloat)(uz * uy * t + ux * s);
    matrix.m[8] = (GLfloat)(ux * uz * t + uy * s);
    matrix.m[9] = (GLfloat)(uy * uz * t - ux * s);
    matrix.m[10] = (GLfloat)(uz * uz * t + c);
    v9x_gl_right_multiply(state, &matrix);
}

void v9x_gl_state_frustum(V9X_GL_STATE *state, GLdouble left,
                          GLdouble right, GLdouble bottom, GLdouble top,
                          GLdouble near_plane, GLdouble far_plane)
{
    V9X_GL_MATRIX matrix;
    unsigned int index;

    if (!v9x_gl_matrix_allowed(state)) {
        return;
    }
    if (near_plane <= 0.0 || far_plane <= 0.0 || left == right ||
        bottom == top || near_plane == far_plane) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    for (index = 0u; index < 16u; ++index) {
        matrix.m[index] = 0.0f;
    }
    matrix.m[0] = (GLfloat)(2.0 * near_plane / (right - left));
    matrix.m[5] = (GLfloat)(2.0 * near_plane / (top - bottom));
    matrix.m[8] = (GLfloat)((right + left) / (right - left));
    matrix.m[9] = (GLfloat)((top + bottom) / (top - bottom));
    matrix.m[10] = (GLfloat)(-(far_plane + near_plane) /
                             (far_plane - near_plane));
    matrix.m[11] = -1.0f;
    matrix.m[14] = (GLfloat)(-2.0 * far_plane * near_plane /
                             (far_plane - near_plane));
    v9x_gl_right_multiply(state, &matrix);
}

void v9x_gl_state_ortho(V9X_GL_STATE *state, GLdouble left, GLdouble right,
                        GLdouble bottom, GLdouble top, GLdouble near_plane,
                        GLdouble far_plane)
{
    V9X_GL_MATRIX matrix;

    if (!v9x_gl_matrix_allowed(state)) {
        return;
    }
    if (left == right || bottom == top || near_plane == far_plane) {
        v9x_gl_state_error(state, V9X_GL_INVALID_VALUE);
        return;
    }
    v9x_gl_identity(&matrix);
    matrix.m[0] = (GLfloat)(2.0 / (right - left));
    matrix.m[5] = (GLfloat)(2.0 / (top - bottom));
    matrix.m[10] = (GLfloat)(-2.0 / (far_plane - near_plane));
    matrix.m[12] = (GLfloat)(-(right + left) / (right - left));
    matrix.m[13] = (GLfloat)(-(top + bottom) / (top - bottom));
    matrix.m[14] = (GLfloat)(-(far_plane + near_plane) /
                             (far_plane - near_plane));
    v9x_gl_right_multiply(state, &matrix);
}

/* The current mode's stack: its array, its top, and its depth. */
static V9X_GL_MATRIX *v9x_gl_stack(V9X_GL_STATE *state, unsigned int **top,
                                   unsigned int *depth)
{
    V9X_GL_MATRICES *matrices = &state->matrices;

    if (matrices->mode == V9X_GL_PROJECTION) {
        *top = &matrices->projection_top;
        *depth = V9X_GL_PROJECTION_DEPTH;
        return matrices->projection;
    }
    if (matrices->mode == V9X_GL_TEXTURE) {
        *top = &matrices->texture_top;
        *depth = V9X_GL_TEXTURE_DEPTH;
        return matrices->texture;
    }
    *top = &matrices->modelview_top;
    *depth = V9X_GL_MODELVIEW_DEPTH;
    return matrices->modelview;
}

void v9x_gl_state_push_matrix(V9X_GL_STATE *state)
{
    V9X_GL_MATRIX *stack;
    unsigned int *top;
    unsigned int depth;

    if (!v9x_gl_matrix_allowed(state)) {
        return;
    }
    stack = v9x_gl_stack(state, &top, &depth);
    if (*top + 1u >= depth) {
        v9x_gl_state_error(state, V9X_GL_STACK_OVERFLOW);
        return;
    }
    stack[*top + 1u] = stack[*top];
    ++*top;
}

void v9x_gl_state_pop_matrix(V9X_GL_STATE *state)
{
    unsigned int *top;
    unsigned int depth;

    if (!v9x_gl_matrix_allowed(state)) {
        return;
    }
    (void)v9x_gl_stack(state, &top, &depth);
    if (*top == 0u) {
        v9x_gl_state_error(state, V9X_GL_STACK_UNDERFLOW);
        return;
    }
    --*top;
}
