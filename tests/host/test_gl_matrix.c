/*
 * Tests for the matrix stacks (src\opengl\gl_matrix.c): each command's
 * matrix against the specification's formula (2.10.2), the right-multiply
 * order, the stacks' depths and their errors.
 */
#include <stdio.h>
#include "../../src/opengl/gl_state.h"

static unsigned int gl_matrix_failures;

#define MCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++gl_matrix_failures; \
    } \
} while (0)

static int near_value(GLfloat a, GLfloat b)
{
    GLfloat d = a - b;

    return d < 0.0001f && d > -0.0001f;
}

/* Column-major element (row, column) of the current mode's top. */
static GLfloat element(V9X_GL_STATE *s, unsigned int row, unsigned int column)
{
    return v9x_gl_matrix_top(&s->matrices, s->matrices.mode)->m[column * 4u +
                                                                row];
}

/* The top matrix applied to (x, y, z, 1), one component. */
static GLfloat apply(V9X_GL_STATE *s, unsigned int row, GLfloat x, GLfloat y,
                     GLfloat z)
{
    return element(s, row, 0u) * x + element(s, row, 1u) * y +
           element(s, row, 2u) * z + element(s, row, 3u);
}

static int is_identity(V9X_GL_STATE *s)
{
    unsigned int r;
    unsigned int c;

    for (r = 0u; r < 4u; ++r) {
        for (c = 0u; c < 4u; ++c) {
            if (!near_value(element(s, r, c), r == c ? 1.0f : 0.0f)) {
                return 0;
            }
        }
    }
    return 1;
}

static void test_initial_and_mode(void)
{
    V9X_GL_STATE s;

    v9x_gl_state_init(&s);
    MCHECK(s.matrices.mode == V9X_GL_MODELVIEW);
    MCHECK(is_identity(&s));
    v9x_gl_state_matrix_mode(&s, V9X_GL_PROJECTION);
    MCHECK(s.matrices.mode == V9X_GL_PROJECTION && is_identity(&s));
    v9x_gl_state_matrix_mode(&s, V9X_GL_TEXTURE);
    MCHECK(s.matrices.mode == V9X_GL_TEXTURE && is_identity(&s));
    v9x_gl_state_matrix_mode(&s, 0x1703u);
    MCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    MCHECK(s.matrices.mode == V9X_GL_TEXTURE);
}

static void test_transforms(void)
{
    V9X_GL_STATE s;
    GLfloat m[16];
    unsigned int i;

    v9x_gl_state_init(&s);
    v9x_gl_state_translate(&s, 1.0f, 2.0f, 3.0f);
    MCHECK(near_value(apply(&s, 0u, 0.0f, 0.0f, 0.0f), 1.0f));
    MCHECK(near_value(apply(&s, 2u, 0.0f, 0.0f, 0.0f), 3.0f));

    /* Right-multiplied: translate then scale applies the scale first to a
     * vertex, so (1,1,1) goes to (2,2,2) and then +(1,2,3). */
    v9x_gl_state_scale(&s, 2.0f, 2.0f, 2.0f);
    MCHECK(near_value(apply(&s, 0u, 1.0f, 1.0f, 1.0f), 3.0f));
    MCHECK(near_value(apply(&s, 1u, 1.0f, 1.0f, 1.0f), 4.0f));

    /* 90 degrees about z takes x to y; about a non-unit axis the axis is
     * normalised first. */
    v9x_gl_state_load_identity(&s);
    v9x_gl_state_rotate(&s, 90.0f, 0.0f, 0.0f, 1.0f);
    MCHECK(near_value(apply(&s, 0u, 1.0f, 0.0f, 0.0f), 0.0f));
    MCHECK(near_value(apply(&s, 1u, 1.0f, 0.0f, 0.0f), 1.0f));
    v9x_gl_state_load_identity(&s);
    v9x_gl_state_rotate(&s, 180.0f, 5.0f, 0.0f, 0.0f);
    MCHECK(near_value(apply(&s, 1u, 0.0f, 1.0f, 0.0f), -1.0f));
    MCHECK(near_value(apply(&s, 0u, 1.0f, 0.0f, 0.0f), 1.0f));
    /* 120 degrees about (1,1,1) cycles the axes: x -> y. */
    v9x_gl_state_load_identity(&s);
    v9x_gl_state_rotate(&s, 120.0f, 1.0f, 1.0f, 1.0f);
    MCHECK(near_value(apply(&s, 1u, 1.0f, 0.0f, 0.0f), 1.0f));
    MCHECK(near_value(apply(&s, 0u, 1.0f, 0.0f, 0.0f), 0.0f));

    /* Load and multiply, column-major. */
    for (i = 0u; i < 16u; ++i) {
        m[i] = 0.0f;
    }
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
    m[12] = 7.0f;               /* column 3, row 0: x translation */
    v9x_gl_state_load_matrix(&s, m);
    MCHECK(near_value(element(&s, 0u, 3u), 7.0f));
    v9x_gl_state_mult_matrix(&s, m);
    MCHECK(near_value(element(&s, 0u, 3u), 14.0f));
}

static void test_projections(void)
{
    V9X_GL_STATE s;

    /* glFrustum(-1,1,-1,1,1,10): the spec's matrix, and the near plane's
     * corner maps to clip x = -w, the far plane to z = +w. */
    v9x_gl_state_init(&s);
    v9x_gl_state_matrix_mode(&s, V9X_GL_PROJECTION);
    v9x_gl_state_frustum(&s, -1.0, 1.0, -1.0, 1.0, 1.0, 10.0);
    MCHECK(near_value(element(&s, 0u, 0u), 1.0f));
    MCHECK(near_value(element(&s, 2u, 2u), -11.0f / 9.0f));
    MCHECK(near_value(element(&s, 2u, 3u), -20.0f / 9.0f));
    MCHECK(near_value(element(&s, 3u, 2u), -1.0f));
    MCHECK(near_value(element(&s, 3u, 3u), 0.0f));
    /* z = -10 (the far plane): clip z / w = 1. */
    MCHECK(near_value(apply(&s, 2u, 0.0f, 0.0f, -10.0f) /
                      apply(&s, 3u, 0.0f, 0.0f, -10.0f), 1.0f));

    /* glOrtho(0,320,0,200,-1,1): x 320 maps to +1, y 0 to -1. */
    v9x_gl_state_load_identity(&s);
    v9x_gl_state_ortho(&s, 0.0, 320.0, 0.0, 200.0, -1.0, 1.0);
    MCHECK(near_value(apply(&s, 0u, 320.0f, 0.0f, 0.0f), 1.0f));
    MCHECK(near_value(apply(&s, 1u, 0.0f, 0.0f, 0.0f), -1.0f));
    MCHECK(near_value(apply(&s, 2u, 0.0f, 0.0f, 1.0f), -1.0f));

    /* Errors, and no effect: near or far not positive, a zero extent. */
    v9x_gl_state_load_identity(&s);
    v9x_gl_state_frustum(&s, -1.0, 1.0, -1.0, 1.0, 0.0, 10.0);
    MCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_state_frustum(&s, -1.0, 1.0, -1.0, 1.0, 1.0, -10.0);
    MCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_state_frustum(&s, 1.0, 1.0, -1.0, 1.0, 1.0, 10.0);
    MCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_state_ortho(&s, 0.0, 1.0, 2.0, 2.0, -1.0, 1.0);
    MCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    MCHECK(is_identity(&s));
}

static void test_stacks(void)
{
    V9X_GL_STATE s;
    unsigned int i;

    v9x_gl_state_init(&s);
    /* Push copies, pop restores. */
    v9x_gl_state_translate(&s, 5.0f, 0.0f, 0.0f);
    v9x_gl_state_push_matrix(&s);
    MCHECK(near_value(element(&s, 0u, 3u), 5.0f));
    v9x_gl_state_translate(&s, 5.0f, 0.0f, 0.0f);
    MCHECK(near_value(element(&s, 0u, 3u), 10.0f));
    v9x_gl_state_pop_matrix(&s);
    MCHECK(near_value(element(&s, 0u, 3u), 5.0f));
    MCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);

    /* Popping the last is STACK_UNDERFLOW; pushing past the depth is
     * STACK_OVERFLOW; neither changes the stack. */
    v9x_gl_state_pop_matrix(&s);
    MCHECK(v9x_gl_state_get_error(&s) == V9X_GL_STACK_UNDERFLOW);
    MCHECK(near_value(element(&s, 0u, 3u), 5.0f));
    for (i = 1u; i < V9X_GL_MODELVIEW_DEPTH; ++i) {
        v9x_gl_state_push_matrix(&s);
    }
    MCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    v9x_gl_state_push_matrix(&s);
    MCHECK(v9x_gl_state_get_error(&s) == V9X_GL_STACK_OVERFLOW);
    MCHECK(s.matrices.modelview_top == V9X_GL_MODELVIEW_DEPTH - 1u);

    /* The projection stack is two deep and separate. */
    v9x_gl_state_matrix_mode(&s, V9X_GL_PROJECTION);
    v9x_gl_state_push_matrix(&s);
    MCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    v9x_gl_state_push_matrix(&s);
    MCHECK(v9x_gl_state_get_error(&s) == V9X_GL_STACK_OVERFLOW);

    /* Inside glBegin every matrix command is INVALID_OPERATION. */
    s.in_begin = 1;
    v9x_gl_state_load_identity(&s);
    MCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);
    s.in_begin = 0;
}

unsigned int v9x_run_gl_matrix_tests(void)
{
    gl_matrix_failures = 0u;
    test_initial_and_mode();
    test_transforms();
    test_projections();
    test_stacks();
    if (gl_matrix_failures == 0u) {
        printf("PASS: OpenGL matrix stacks\n");
    }
    return gl_matrix_failures;
}
