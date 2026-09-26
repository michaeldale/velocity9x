/*
 * Tests for glReadPixels' conversion (src\opengl\gl_pixels.c): the window
 * to surface flip, 565 and 555 expansion, each format's components, the
 * pack alignment and skips, pixels outside the buffer, and the errors.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/opengl/gl_pixels.h"

static unsigned int gl_pixels_failures;

#define XCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++gl_pixels_failures; \
    } \
} while (0)

static void *px_alloc(v9x_u32 bytes) { return malloc(bytes); }
static void px_free(void *memory) { free(memory); }

/* A 4x3 565 surface, pitch padded to 10 bytes. Row 0 is the top. */
#define SW 4u
#define SH 3u
static v9x_u16 surface[5u * SH];

static void surface_reset(void)
{
    unsigned int i;

    for (i = 0u; i < 5u * SH; ++i) {
        surface[i] = 0x0000u;
    }
    surface[2u * 5u + 0u] = 0xF800u;    /* bottom row, x 0: red */
    surface[2u * 5u + 1u] = 0x07E0u;    /* bottom row, x 1: green */
    surface[1u * 5u + 1u] = 0x001Fu;    /* middle row, x 1: blue */
    surface[1u * 5u + 2u] = 0xFFFFu;    /* middle row, x 2: white */
    surface[0u * 5u + 3u] = 0x8410u;    /* top row, x 3: grey */
}

static V9X_GL_STATE s;
static V9X_GL_TEXTURES t;

static void fresh(void)
{
    v9x_gl_state_init(&s);
    v9x_gl_textures_init(&t, px_alloc, px_free);
    surface_reset();
}

static void test_rgb_and_flip(void)
{
    V9X_GL_READ_PLAN plan;
    GLubyte out[32];
    unsigned int i;

    fresh();
    for (i = 0u; i < sizeof(out); ++i) {
        out[i] = 0xAAu;
    }
    /* 2x2 from window (0,0): the bottom two rows of the left half. RGB
     * with the default pack alignment of 4: 6 bytes a row, padded to 8. */
    XCHECK(v9x_gl_read_plan(&s, &t, 0, 0, 2, 2, V9X_GL_RGB,
                            V9X_GL_UNSIGNED_BYTE, &plan) != 0);
    XCHECK(plan.row_bytes == 8ul);
    v9x_gl_read_convert(&plan, surface, 10ul, SW, SH, V9X_GL_TARGET_RGB565,
                        out);
    XCHECK(out[0] == 255u && out[1] == 0u && out[2] == 0u);     /* red */
    XCHECK(out[3] == 0u && out[4] == 255u && out[5] == 0u);     /* green */
    XCHECK(out[6] == 0xAAu && out[7] == 0xAAu);                 /* padding */
    XCHECK(out[8] == 0u && out[9] == 0u && out[10] == 0u);      /* black */
    XCHECK(out[11] == 0u && out[12] == 0u && out[13] == 255u);  /* blue */
    XCHECK(out[16] == 0xAAu);                                   /* past it */

    /* RGBA: alpha is 1 (255), there being no alpha plane; grey 0x8410
     * expands by replication to 132, 130, 132. */
    for (i = 0u; i < sizeof(out); ++i) {
        out[i] = 0xAAu;
    }
    XCHECK(v9x_gl_read_plan(&s, &t, 3, 2, 1, 1, V9X_GL_RGBA,
                            V9X_GL_UNSIGNED_BYTE, &plan) != 0);
    v9x_gl_read_convert(&plan, surface, 10ul, SW, SH, V9X_GL_TARGET_RGB565,
                        out);
    XCHECK(out[0] == 132u && out[1] == 130u && out[2] == 132u &&
           out[3] == 255u);
    v9x_gl_textures_release(&t);
}

static void test_formats_and_555(void)
{
    V9X_GL_READ_PLAN plan;
    GLubyte out[16];
    unsigned int i;

    fresh();
    /* LUMINANCE is R + G + B, clamped (4.3.2): white is 255, blue 255,
     * black 0. One byte a pixel, rows of 3 padded to 4. */
    XCHECK(v9x_gl_read_plan(&s, &t, 0, 1, 3, 1, V9X_GL_LUMINANCE,
                            V9X_GL_UNSIGNED_BYTE, &plan) != 0);
    v9x_gl_read_convert(&plan, surface, 10ul, SW, SH, V9X_GL_TARGET_RGB565,
                        out);
    XCHECK(out[0] == 0u && out[1] == 255u && out[2] == 255u);
    /* ALPHA is all ones; GREEN one channel. */
    XCHECK(v9x_gl_read_plan(&s, &t, 0, 0, 2, 1, V9X_GL_ALPHA,
                            V9X_GL_UNSIGNED_BYTE, &plan) != 0);
    v9x_gl_read_convert(&plan, surface, 10ul, SW, SH, V9X_GL_TARGET_RGB565,
                        out);
    XCHECK(out[0] == 255u && out[1] == 255u);
    XCHECK(v9x_gl_read_plan(&s, &t, 0, 0, 2, 1, 0x1904u /* GREEN */,
                            V9X_GL_UNSIGNED_BYTE, &plan) != 0);
    v9x_gl_read_convert(&plan, surface, 10ul, SW, SH, V9X_GL_TARGET_RGB565,
                        out);
    XCHECK(out[0] == 0u && out[1] == 255u);

    /* 555: 0x7C00 is red, and a 5-bit green expands to 255 too. */
    surface[2u * 5u + 0u] = 0x7C00u;
    surface[2u * 5u + 1u] = 0x03E0u;
    XCHECK(v9x_gl_read_plan(&s, &t, 0, 0, 2, 1, V9X_GL_RGB,
                            V9X_GL_UNSIGNED_BYTE, &plan) != 0);
    v9x_gl_read_convert(&plan, surface, 10ul, SW, SH,
                        V9X_GL_TARGET_XRGB1555, out);
    XCHECK(out[0] == 255u && out[1] == 0u && out[4] == 255u);

    /* Outside the buffer: untouched. A 2x1 read from window x 3 has one
     * pixel inside (x 3) and one past the right edge. */
    for (i = 0u; i < sizeof(out); ++i) {
        out[i] = 0xAAu;
    }
    surface_reset();
    XCHECK(v9x_gl_read_plan(&s, &t, 3, 2, 2, 1, V9X_GL_RGB,
                            V9X_GL_UNSIGNED_BYTE, &plan) != 0);
    v9x_gl_read_convert(&plan, surface, 10ul, SW, SH, V9X_GL_TARGET_RGB565,
                        out);
    XCHECK(out[0] == 132u && out[3] == 0xAAu && out[5] == 0xAAu);
    v9x_gl_textures_release(&t);
}

static void test_pack_parameters_and_errors(void)
{
    V9X_GL_READ_PLAN plan;
    GLubyte out[64];
    unsigned int i;

    fresh();
    /* Alignment 1, a row length of 4 and one skipped pixel and row: the
     * one pixel read lands at (1 + 1 * 4) * 3 bytes in. */
    v9x_gl_pixel_store(&s, &t, 0x0D05u, 1);         /* PACK_ALIGNMENT */
    v9x_gl_pixel_store(&s, &t, 0x0D02u, 4);         /* PACK_ROW_LENGTH */
    v9x_gl_pixel_store(&s, &t, 0x0D03u, 1);         /* PACK_SKIP_ROWS */
    v9x_gl_pixel_store(&s, &t, 0x0D04u, 1);         /* PACK_SKIP_PIXELS */
    for (i = 0u; i < sizeof(out); ++i) {
        out[i] = 0xAAu;
    }
    XCHECK(v9x_gl_read_plan(&s, &t, 0, 0, 1, 1, V9X_GL_RGB,
                            V9X_GL_UNSIGNED_BYTE, &plan) != 0);
    v9x_gl_read_convert(&plan, surface, 10ul, SW, SH, V9X_GL_TARGET_RGB565,
                        out);
    XCHECK(out[15] == 255u && out[16] == 0u && out[14] == 0xAAu);

    /* Errors: a negative size, formats and types not read here, and
     * inside glBegin. An empty rectangle reads nothing, silently. */
    XCHECK(v9x_gl_read_plan(&s, &t, 0, 0, -1, 1, V9X_GL_RGB,
                            V9X_GL_UNSIGNED_BYTE, &plan) == 0);
    XCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    XCHECK(v9x_gl_read_plan(&s, &t, 0, 0, 1, 1, 0x1902u /* DEPTH */,
                            V9X_GL_UNSIGNED_BYTE, &plan) == 0);
    XCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    XCHECK(v9x_gl_read_plan(&s, &t, 0, 0, 1, 1, V9X_GL_RGB,
                            0x1406u /* FLOAT */, &plan) == 0);
    XCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    XCHECK(v9x_gl_read_plan(&s, &t, 0, 0, 0, 5, V9X_GL_RGB,
                            V9X_GL_UNSIGNED_BYTE, &plan) == 0);
    XCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    s.in_begin = 1;
    XCHECK(v9x_gl_read_plan(&s, &t, 0, 0, 1, 1, V9X_GL_RGB,
                            V9X_GL_UNSIGNED_BYTE, &plan) == 0);
    XCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);
    s.in_begin = 0;
    v9x_gl_textures_release(&t);
}

unsigned int v9x_run_gl_pixels_tests(void)
{
    gl_pixels_failures = 0u;
    test_rgb_and_flip();
    test_formats_and_555();
    test_pack_parameters_and_errors();
    if (gl_pixels_failures == 0u) {
        printf("PASS: OpenGL pixel reads\n");
    }
    return gl_pixels_failures;
}
