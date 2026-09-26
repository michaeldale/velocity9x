/*
 * Tests for the state queries (src\opengl\gl_get.c) and the held-state
 * commands they report (glHint, glPolygonMode, glDrawBuffer, glReadBuffer,
 * glLineWidth, glPointSize): initial values from the state tables, values
 * after the commands, and the 6.1.2 conversions between the four types.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/opengl/gl_get.h"

static unsigned int gl_get_failures;

#define GGCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++gl_get_failures; \
    } \
} while (0)

static void *get_alloc(v9x_u32 bytes) { return malloc(bytes); }
static void get_free(void *memory) { free(memory); }

static V9X_GL_STATE s;
static V9X_GL_PIPELINE p;
static V9X_GL_TEXTURES t;

static void fresh(void)
{
    v9x_gl_state_init(&s);
    v9x_gl_state_drawable(&s, 320ul, 200ul, V9X_GL_TARGET_RGB565, 1);
    v9x_gl_pipeline_init(&p);
    v9x_gl_textures_init(&t, get_alloc, get_free);
}

static GLint geti(GLenum pname)
{
    GLint value[16];

    value[0] = -12345;
    v9x_gl_get(&s, &p, &t, pname, V9X_GL_GET_INTEGER, value);
    return value[0];
}

static int near_value(double a, double b)
{
    double d = a - b;

    return d < 0.0001 && d > -0.0001;
}

static void test_initial_values(void)
{
    GLint v[4];
    GLboolean b[4];
    GLfloat f[16];

    fresh();
    GGCHECK(geti(0x0BA0u) == (GLint)V9X_GL_MODELVIEW);  /* MATRIX_MODE */
    GGCHECK(geti(0x0BA3u) == 1);            /* MODELVIEW_STACK_DEPTH */
    GGCHECK(geti(0x0D36u) == 32);           /* MAX_MODELVIEW_STACK_DEPTH */
    GGCHECK(geti(0x0D38u) == 2 && geti(0x0D39u) == 2);
    GGCHECK(geti(0x0D33u) == 512);          /* MAX_TEXTURE_SIZE */
    GGCHECK(geti(0x0B74u) == (GLint)V9X_GL_LESS);        /* DEPTH_FUNC */
    GGCHECK(geti(0x0BE1u) == (GLint)V9X_GL_ONE);         /* BLEND_SRC */
    GGCHECK(geti(0x0BE0u) == (GLint)V9X_GL_ZERO);        /* BLEND_DST */
    GGCHECK(geti(0x0BC1u) == (GLint)V9X_GL_ALWAYS);
    GGCHECK(geti(0x0B45u) == (GLint)V9X_GL_BACK);        /* CULL_FACE_MODE */
    GGCHECK(geti(0x0B46u) == (GLint)V9X_GL_CCW);
    GGCHECK(geti(0x0B54u) == (GLint)V9X_GL_SMOOTH);
    GGCHECK(geti(0x8069u) == 0);            /* TEXTURE_BINDING_2D */
    GGCHECK(geti(0x0CF5u) == 4);            /* UNPACK_ALIGNMENT */
    GGCHECK(geti(0x0D05u) == 4);            /* PACK_ALIGNMENT */
    GGCHECK(geti(0x0C01u) == (GLint)V9X_GL_BACK_BUFFER); /* DRAW_BUFFER */
    GGCHECK(geti(0x0C50u) == (GLint)V9X_GL_DONT_CARE);
    /* The pixel format this ICD offers: 565, 16-bit depth, nothing else. */
    GGCHECK(geti(0x0D52u) == 5 && geti(0x0D53u) == 6 && geti(0x0D54u) == 5);
    GGCHECK(geti(0x0D55u) == 0 && geti(0x0D56u) == 16 && geti(0x0D57u) == 0);
    GGCHECK(geti(0x0D50u) == 4);            /* SUBPIXEL_BITS */
    v9x_gl_get(&s, &p, &t, 0x0C32u, V9X_GL_GET_BOOLEAN, b); /* DOUBLEBUFFER */
    GGCHECK(b[0] == 1);
    v9x_gl_get(&s, &p, &t, 0x0BA2u, V9X_GL_GET_INTEGER, v); /* VIEWPORT */
    GGCHECK(v[0] == 0 && v[1] == 0 && v[2] == 320 && v[3] == 200);
    v9x_gl_get(&s, &p, &t, 0x0B40u, V9X_GL_GET_INTEGER, v); /* POLYGON_MODE */
    GGCHECK(v[0] == (GLint)V9X_GL_FILL && v[1] == (GLint)V9X_GL_FILL);
    v9x_gl_get(&s, &p, &t, 0x0C23u, V9X_GL_GET_BOOLEAN, b); /* WRITEMASK */
    GGCHECK(b[0] == 1 && b[3] == 1);
    v9x_gl_get(&s, &p, &t, 0x0BA6u, V9X_GL_GET_FLOAT, f);   /* MODELVIEW */
    GGCHECK(f[0] == 1.0f && f[1] == 0.0f && f[15] == 1.0f);
    v9x_gl_get(&s, &p, &t, 0x0B00u, V9X_GL_GET_FLOAT, f);   /* CURRENT_COLOR */
    GGCHECK(f[0] == 1.0f && f[3] == 1.0f);
    GGCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    /* Every capability is a Boolean query too. */
    v9x_gl_get(&s, &p, &t, V9X_GL_DITHER, V9X_GL_GET_BOOLEAN, b);
    GGCHECK(b[0] == 1);
    v9x_gl_get(&s, &p, &t, 0x0B71u, V9X_GL_GET_BOOLEAN, b);
    GGCHECK(b[0] == 0);
    v9x_gl_textures_release(&t);
}

static void test_after_commands(void)
{
    GLint v[4];
    GLfloat f[16];
    GLdouble d[2];
    GLuint name = 0u;

    fresh();
    v9x_gl_state_translate(&s, 3.0f, 0.0f, 0.0f);
    v9x_gl_get(&s, &p, &t, 0x0BA6u, V9X_GL_GET_FLOAT, f);
    GGCHECK(near_value(f[12], 3.0));
    v9x_gl_state_push_matrix(&s);
    GGCHECK(geti(0x0BA3u) == 2);
    v9x_gl_state_scissor(&s, 1, 2, 3, 4);
    v9x_gl_get(&s, &p, &t, 0x0C10u, V9X_GL_GET_INTEGER, v);
    GGCHECK(v[0] == 1 && v[1] == 2 && v[2] == 3 && v[3] == 4);
    v9x_gl_prim_depth_range(&s, &p, 0.25, 0.75);
    v9x_gl_get(&s, &p, &t, 0x0B70u, V9X_GL_GET_DOUBLE, d);
    GGCHECK(near_value(d[0], 0.25) && near_value(d[1], 0.75));
    v9x_gl_prim_blend_func(&s, &p, V9X_GL_SRC_ALPHA,
                           V9X_GL_ONE_MINUS_SRC_ALPHA);
    GGCHECK(geti(0x0BE1u) == (GLint)V9X_GL_SRC_ALPHA);
    v9x_gl_tex_gen(&s, &t, 1, &name);
    v9x_gl_tex_bind(&s, &t, V9X_GL_TEXTURE_2D, name);
    GGCHECK(geti(0x8069u) == (GLint)name);

    /* The held-state commands and their queries. */
    v9x_gl_state_hint(&s, 0x0C50u, V9X_GL_FASTEST);
    GGCHECK(geti(0x0C50u) == (GLint)V9X_GL_FASTEST);
    v9x_gl_state_polygon_mode(&s, 0x0405u, V9X_GL_LINE);   /* BACK */
    v9x_gl_get(&s, &p, &t, 0x0B40u, V9X_GL_GET_INTEGER, v);
    GGCHECK(v[0] == (GLint)V9X_GL_FILL && v[1] == (GLint)V9X_GL_LINE);
    v9x_gl_state_draw_buffer(&s, V9X_GL_FRONT_BUFFER);
    GGCHECK(geti(0x0C01u) == (GLint)V9X_GL_FRONT_BUFFER);
    v9x_gl_state_line_width(&s, 2.5f);
    v9x_gl_get(&s, &p, &t, 0x0B21u, V9X_GL_GET_FLOAT, f);
    GGCHECK(f[0] == 2.5f);
    v9x_gl_state_point_size(&s, 3.0f);
    GGCHECK(geti(0x0B11u) == 3);
    GGCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);

    /* Their errors, each with no effect. */
    v9x_gl_state_hint(&s, 0x0C55u, V9X_GL_FASTEST);
    GGCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_state_hint(&s, 0x0C50u, 0x1103u);
    GGCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_state_polygon_mode(&s, 0x0404u, 0x1B03u);
    GGCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_state_draw_buffer(&s, 0x0401u);          /* RIGHT: absent */
    GGCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);
    v9x_gl_state_draw_buffer(&s, 0x0409u);          /* AUX0: absent */
    GGCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);
    v9x_gl_state_draw_buffer(&s, 0x1234u);
    GGCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_state_read_buffer(&s, 0u);               /* NONE: draw only */
    GGCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_state_line_width(&s, 0.0f);
    GGCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_state_point_size(&s, -1.0f);
    GGCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    GGCHECK(geti(0x0C01u) == (GLint)V9X_GL_FRONT_BUFFER);
    v9x_gl_textures_release(&t);
}

static void test_conversions(void)
{
    GLint v[4];
    GLboolean b[4];
    GLfloat f[4];
    GLdouble d[4];

    fresh();
    /* Colours and depths map linearly to the full integer range when read
     * as integers (6.1.2): 1.0 is 2^31 - 1, 0.5 half of it. */
    v9x_gl_state_clear_color(&s, 1.0f, 0.5f, 0.0f, 1.0f);
    v9x_gl_get(&s, &p, &t, 0x0C22u, V9X_GL_GET_INTEGER, v);
    GGCHECK(v[0] == 2147483647 && v[2] == 0);
    GGCHECK(v[1] >= 1073741823 && v[1] <= 1073741824);
    v9x_gl_get(&s, &p, &t, 0x0B73u, V9X_GL_GET_INTEGER, v); /* DEPTH_CLEAR */
    GGCHECK(v[0] == 2147483647);
    /* Other floats round to nearest: a line width of 2.4 reads as 2 (a
     * half may go either way; 6.1.2 does not fix it). */
    v9x_gl_state_line_width(&s, 2.4f);
    GGCHECK(geti(0x0B21u) == 2);
    /* Booleans: non-zero is true, as a float or an integer. */
    v9x_gl_get(&s, &p, &t, 0x0C22u, V9X_GL_GET_BOOLEAN, b);
    GGCHECK(b[0] == 1 && b[1] == 1 && b[2] == 0);
    v9x_gl_get(&s, &p, &t, 0x0B74u, V9X_GL_GET_BOOLEAN, b);
    GGCHECK(b[0] == 1);
    /* Integers and Booleans to floats and doubles directly. */
    v9x_gl_get(&s, &p, &t, 0x0D33u, V9X_GL_GET_FLOAT, f);
    GGCHECK(f[0] == 512.0f);
    v9x_gl_get(&s, &p, &t, 0x0C32u, V9X_GL_GET_DOUBLE, d);
    GGCHECK(d[0] == 1.0);

    /* Unknown, and inside Begin. */
    v[0] = 77;
    v9x_gl_get(&s, &p, &t, 0x1234u, V9X_GL_GET_INTEGER, v);
    GGCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM && v[0] == 77);
    s.in_begin = 1;
    v9x_gl_get(&s, &p, &t, 0x0D33u, V9X_GL_GET_INTEGER, v);
    GGCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);
    s.in_begin = 0;
    v9x_gl_textures_release(&t);
}

unsigned int v9x_run_gl_get_tests(void)
{
    gl_get_failures = 0u;
    test_initial_values();
    test_after_commands();
    test_conversions();
    if (gl_get_failures == 0u) {
        printf("PASS: OpenGL state queries\n");
    }
    return gl_get_failures;
}
