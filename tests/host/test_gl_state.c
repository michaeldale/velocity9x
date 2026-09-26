/*
 * Tests for the ICD's OpenGL context state (src\opengl\gl_state.c): the
 * initial values the specification's state tables give, the error model,
 * the argument errors of each command, and glClear's plan - its rectangle
 * in surface rows, its colour conversion and its masks.
 */
#include <stdio.h>
#include "../../src/opengl/gl_state.h"

static unsigned int gl_state_failures;

#define GCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++gl_state_failures; \
    } \
} while (0)

static void bound(V9X_GL_STATE *s)
{
    v9x_gl_state_init(s);
    v9x_gl_state_drawable(s, 320ul, 200ul, V9X_GL_TARGET_RGB565, 1);
}

static void test_initial_state(void)
{
    V9X_GL_STATE s;

    bound(&s);
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    GCHECK(s.clear_color[0] == 0.0f && s.clear_color[3] == 0.0f);
    GCHECK(s.clear_depth == 1.0);
    GCHECK(s.color_mask[0] == 1 && s.color_mask[3] == 1 && s.depth_mask == 1);
    GCHECK(v9x_gl_state_is_enabled(&s, V9X_GL_DITHER) == 1);
    GCHECK(v9x_gl_state_is_enabled(&s, V9X_GL_DEPTH_TEST) == 0);
    GCHECK(v9x_gl_state_is_enabled(&s, V9X_GL_SCISSOR_TEST) == 0);
    GCHECK(v9x_gl_state_is_enabled(&s, 0x0DE1u) == 0);   /* TEXTURE_2D */
    /* The viewport and scissor take the drawable at the first bind, and
     * a later resize leaves them as they are (2.10.1, 4.1.2). */
    GCHECK(s.viewport[2] == 320 && s.viewport[3] == 200);
    GCHECK(s.scissor[2] == 320 && s.scissor[3] == 200);
    v9x_gl_state_drawable(&s, 640ul, 480ul, V9X_GL_TARGET_RGB565, 0);
    GCHECK(s.viewport[2] == 320 && s.scissor[3] == 200);
    GCHECK(s.drawable_width == 640ul && s.drawable_height == 480ul);
}

static void test_error_model(void)
{
    V9X_GL_STATE s;

    bound(&s);
    v9x_gl_state_error(&s, V9X_GL_INVALID_ENUM);
    v9x_gl_state_error(&s, V9X_GL_INVALID_VALUE);    /* the first sticks */
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);

    /* A failed command has no other effect. */
    v9x_gl_state_viewport(&s, 1, 2, -1, 10);
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    GCHECK(s.viewport[0] == 0 && s.viewport[2] == 320);
    v9x_gl_state_scissor(&s, 1, 2, 10, -1);
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    GCHECK(s.scissor[0] == 0 && s.scissor[3] == 200);

    /* Unknown capabilities, both directions and the query. */
    v9x_gl_state_enable(&s, 0x1234u, 1);
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_state_enable(&s, 0x8074u, 1);    /* VERTEX_ARRAY: a client state */
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    GCHECK(v9x_gl_state_is_enabled(&s, 0x1234u) == 0);
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);

    /* Between glBegin and glEnd, a state command is INVALID_OPERATION and
     * changes nothing. */
    s.in_begin = 1;
    v9x_gl_state_clear_color(&s, 1.0f, 1.0f, 1.0f, 1.0f);
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);
    GCHECK(s.clear_color[0] == 0.0f);
    s.in_begin = 0;
}

static void test_state_commands(void)
{
    V9X_GL_STATE s;

    bound(&s);
    /* Clamped to [0,1] when specified (4.2.3). */
    v9x_gl_state_clear_color(&s, 2.0f, -1.0f, 0.5f, 1.5f);
    GCHECK(s.clear_color[0] == 1.0f && s.clear_color[1] == 0.0f &&
           s.clear_color[2] == 0.5f && s.clear_color[3] == 1.0f);
    v9x_gl_state_clear_depth(&s, -3.0);
    GCHECK(s.clear_depth == 0.0);
    v9x_gl_state_clear_depth(&s, 0.25);
    GCHECK(s.clear_depth == 0.25);
    v9x_gl_state_viewport(&s, -5, 7, 100, 50);
    GCHECK(s.viewport[0] == -5 && s.viewport[1] == 7 &&
           s.viewport[2] == 100 && s.viewport[3] == 50);
    v9x_gl_state_color_mask(&s, 0, 1, 0, 1);
    GCHECK(s.color_mask[0] == 0 && s.color_mask[1] == 1);
    v9x_gl_state_depth_mask(&s, 0);
    GCHECK(s.depth_mask == 0);
    v9x_gl_state_enable(&s, V9X_GL_SCISSOR_TEST, 1);
    GCHECK(v9x_gl_state_is_enabled(&s, V9X_GL_SCISSOR_TEST) == 1);
    v9x_gl_state_enable(&s, V9X_GL_DITHER, 0);
    GCHECK(v9x_gl_state_is_enabled(&s, V9X_GL_DITHER) == 0);
    v9x_gl_state_enable(&s, 0x4007u, 1);    /* LIGHT7, the table's edge */
    GCHECK(v9x_gl_state_is_enabled(&s, 0x4007u) == 1);
    v9x_gl_state_enable(&s, 0x0DB8u, 1);    /* MAP2_VERTEX_4 */
    GCHECK(v9x_gl_state_is_enabled(&s, 0x0DB8u) == 1);
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
}

static void test_clear_plan(void)
{
    V9X_GL_STATE s;
    V9X_GL_CLEAR_PLAN p;

    /* The whole drawable, colour and depth. */
    bound(&s);
    v9x_gl_state_clear_color(&s, 1.0f, 0.0f, 1.0f, 1.0f);
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_COLOR_BUFFER_BIT |
                                  V9X_GL_DEPTH_BUFFER_BIT, 1, &p) != 0);
    GCHECK(p.clear_color == 1ul && p.clear_depth == 1ul);
    GCHECK(p.color_value == 0x00ff00fful);
    GCHECK(p.depth_value == 65535ul);
    GCHECK(p.write_mask == 7ul);
    GCHECK(p.rect_left == 0ul && p.rect_top == 0ul &&
           p.rect_right == 320ul && p.rect_bottom == 200ul);

    /* The conversion is round(c * (2^m - 1)) for the target's bits, packed
     * so the interface's >> 3 or >> 2 lands on it: 0.5 is 16 of 31 in a
     * five-bit field, 32 of 63 in the six-bit 565 green, 16 of 31 in 555. */
    v9x_gl_state_clear_color(&s, 0.5f, 0.5f, 0.5f, 1.0f);
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_COLOR_BUFFER_BIT, 1, &p) != 0);
    GCHECK(((p.color_value >> 19) & 31ul) == 16ul);
    GCHECK(((p.color_value >> 10) & 63ul) == 32ul);
    GCHECK(((p.color_value >> 3) & 31ul) == 16ul);
    GCHECK(p.clear_depth == 0ul);
    v9x_gl_state_drawable(&s, 320ul, 200ul, V9X_GL_TARGET_XRGB1555, 0);
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_COLOR_BUFFER_BIT, 1, &p) != 0);
    GCHECK(((p.color_value >> 11) & 31ul) == 16ul);
    /* 0.25 depth is round(0.25 * 65535) = 16384. */
    v9x_gl_state_clear_depth(&s, 0.25);
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_DEPTH_BUFFER_BIT, 1, &p) != 0);
    GCHECK(p.depth_value == 16384ul && p.clear_color == 0ul);

    /* The scissor, in window coordinates with y up: a box 10 across and 20
     * up from (30, 40) is surface rows 200-60 .. 200-40. */
    bound(&s);
    v9x_gl_state_enable(&s, V9X_GL_SCISSOR_TEST, 1);
    v9x_gl_state_scissor(&s, 30, 40, 10, 20);
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_COLOR_BUFFER_BIT, 1, &p) != 0);
    GCHECK(p.rect_left == 30ul && p.rect_right == 40ul &&
           p.rect_top == 140ul && p.rect_bottom == 160ul);
    /* Clipped to the drawable on every side, and a box off it clears
     * nothing - no work, and no error. */
    v9x_gl_state_scissor(&s, -10, 190, 50, 50);
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_COLOR_BUFFER_BIT, 1, &p) != 0);
    GCHECK(p.rect_left == 0ul && p.rect_right == 40ul &&
           p.rect_top == 0ul && p.rect_bottom == 10ul);
    v9x_gl_state_scissor(&s, 400, 10, 5, 5);
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_COLOR_BUFFER_BIT, 1, &p) == 0);
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    /* Scissor off: the whole drawable again. */
    v9x_gl_state_enable(&s, V9X_GL_SCISSOR_TEST, 0);
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_COLOR_BUFFER_BIT, 1, &p) != 0);
    GCHECK(p.rect_right == 320ul && p.rect_bottom == 200ul);

    /* Masks: a masked channel is kept; every channel and depth masked is
     * nothing to do. Depth without a depth buffer does nothing either. */
    bound(&s);
    v9x_gl_state_color_mask(&s, 1, 0, 1, 0);
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_COLOR_BUFFER_BIT, 1, &p) != 0);
    GCHECK(p.write_mask == (V9X_GL_CLEAR_RED | V9X_GL_CLEAR_BLUE));
    v9x_gl_state_color_mask(&s, 0, 0, 0, 1);
    v9x_gl_state_depth_mask(&s, 0);
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_COLOR_BUFFER_BIT |
                                  V9X_GL_DEPTH_BUFFER_BIT, 1, &p) == 0);
    bound(&s);
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_DEPTH_BUFFER_BIT, 0, &p) == 0);
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);

    /* Stencil and accumulation bits are legal and absent; any other bit is
     * INVALID_VALUE and clears nothing; inside glBegin it is
     * INVALID_OPERATION. */
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_STENCIL_BUFFER_BIT |
                                  V9X_GL_ACCUM_BUFFER_BIT, 1, &p) == 0);
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_COLOR_BUFFER_BIT | 0x1u, 1, &p) == 0);
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    s.in_begin = 1;
    GCHECK(v9x_gl_state_clear(&s, V9X_GL_COLOR_BUFFER_BIT, 1, &p) == 0);
    GCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);
}

unsigned int v9x_run_gl_state_tests(void)
{
    gl_state_failures = 0u;
    test_initial_state();
    test_error_model();
    test_state_commands();
    test_clear_plan();
    if (gl_state_failures == 0u) {
        printf("PASS: OpenGL context state and clear plan\n");
    }
    return gl_state_failures;
}
