/*
 * Tests for vertex arrays (src\opengl\gl_varray.c): pointer and enable
 * errors, element fetch and the table 2.6 conversions, glDrawArrays and
 * glDrawElements producing the triangles glBegin/glEnd would, and
 * glInterleavedArrays' table 2.5 layouts.
 */
#include <stdio.h>
#include "../../src/opengl/gl_varray.h"

static unsigned int gl_varray_failures;

#define VCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++gl_varray_failures; \
    } \
} while (0)

#define VSINK_MAX 16u
static V9X_R3D_ABI_VERTEX vsunk[3u * VSINK_MAX];
static v9x_u32 vsunk_triangles;

static int vsink(void *user, const V9X_R3D_ABI_VERTEX *vertices,
                 v9x_u32 triangle_count)
{
    v9x_u32 i;

    (void)user;
    for (i = 0ul; i < triangle_count * 3ul &&
                  vsunk_triangles * 3ul + i < 3ul * VSINK_MAX; ++i) {
        vsunk[vsunk_triangles * 3ul + i] = vertices[i];
    }
    vsunk_triangles += triangle_count;
    return 1;
}

static int vnear(float a, float b)
{
    float d = a - b;

    return d < 0.001f && d > -0.001f;
}

static V9X_GL_STATE s;
static V9X_GL_PIPELINE p;
static V9X_GL_ARRAYS a;

/* 320x200, window coordinates equal to object x and y (test_gl_prim.c's
 * scene). */
static void vscene(void)
{
    v9x_gl_state_init(&s);
    v9x_gl_state_drawable(&s, 320ul, 200ul, V9X_GL_TARGET_RGB565, 1);
    v9x_gl_pipeline_init(&p);
    v9x_gl_pipeline_sink(&p, vsink, 0);
    v9x_gl_state_matrix_mode(&s, V9X_GL_PROJECTION);
    v9x_gl_state_ortho(&s, 0.0, 320.0, 0.0, 200.0, -1.0, 1.0);
    v9x_gl_state_matrix_mode(&s, V9X_GL_MODELVIEW);
    v9x_gl_arrays_init(&a);
    vsunk_triangles = 0ul;
}

static void test_pointer_and_enable_errors(void)
{
    static const GLfloat any[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    GLboolean enabled = 7u;
    void *pointer = 0;

    vscene();
    VCHECK(a.array[V9X_GL_ARRAY_VERTEX].size == 4 &&
           a.array[V9X_GL_ARRAY_VERTEX].type == V9X_GL_FLOAT &&
           a.array[V9X_GL_ARRAY_NORMAL].size == 3);
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_VERTEX, 1, V9X_GL_FLOAT, 0,
                          any);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_VERTEX, 3,
                          V9X_GL_UNSIGNED_BYTE, 0, any);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_VERTEX, 3, V9X_GL_FLOAT, -4,
                          any);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_COLOR, 2, V9X_GL_FLOAT, 0,
                          any);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_TEXCOORD, 5, V9X_GL_FLOAT, 0,
                          any);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_NORMAL, 3,
                          V9X_GL_UNSIGNED_BYTE, 0, any);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    /* A refused call changes nothing. */
    VCHECK(a.array[V9X_GL_ARRAY_VERTEX].pointer == 0 &&
           a.array[V9X_GL_ARRAY_VERTEX].size == 4);

    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_VERTEX, 2, V9X_GL_SHORT, 8,
                          any);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    v9x_gl_arrays_get_pointer(&s, &a, 0x808Eu, &pointer);
    VCHECK(pointer == (const void *)any);
    v9x_gl_arrays_get_pointer(&s, &a, 0x0DF0u, &pointer); /* FEEDBACK */
    VCHECK(pointer == 0 && v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    v9x_gl_arrays_get_pointer(&s, &a, 0x0B00u, &pointer);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);

    v9x_gl_arrays_client_state(&s, &a, V9X_GL_COLOR_ARRAY, 1);
    VCHECK(v9x_gl_arrays_is_enabled(&a, V9X_GL_COLOR_ARRAY, &enabled) &&
           enabled == 1u);
    VCHECK(v9x_gl_arrays_is_enabled(&a, V9X_GL_VERTEX_ARRAY, &enabled) &&
           enabled == 0u);
    VCHECK(!v9x_gl_arrays_is_enabled(&a, 0x0B71u /* DEPTH_TEST */,
                                     &enabled));
    v9x_gl_arrays_client_state(&s, &a, 0x0B71u, 1);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
}

static void test_draw_arrays(void)
{
    static const GLfloat xy[6] = { 0.0f, 0.0f, 320.0f, 0.0f, 0.0f, 200.0f };
    static const GLubyte rgba[12] = { 255u, 0u, 0u, 255u,  0u, 255u, 0u,
                                      255u,  0u, 0u, 255u, 128u };

    vscene();
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_VERTEX, 2, V9X_GL_FLOAT, 0,
                          xy);
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_COLOR, 4,
                          V9X_GL_UNSIGNED_BYTE, 0, rgba);
    v9x_gl_arrays_client_state(&s, &a, V9X_GL_VERTEX_ARRAY, 1);
    v9x_gl_arrays_client_state(&s, &a, V9X_GL_COLOR_ARRAY, 1);
    v9x_gl_arrays_draw(&s, &p, &a, V9X_GL_TRIANGLES, 0, 3);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    VCHECK(!s.in_begin);
    VCHECK(vsunk_triangles == 1ul);
    VCHECK(vnear(vsunk[0].sx, 0.0f) && vnear(vsunk[0].sy, 200.0f));
    VCHECK(vnear(vsunk[1].sx, 320.0f) && vnear(vsunk[1].sy, 200.0f));
    VCHECK(vnear(vsunk[2].sx, 0.0f) && vnear(vsunk[2].sy, 0.0f));
    VCHECK(vsunk[0].color == 0xffff0000ul);
    VCHECK(vsunk[1].color == 0xff00ff00ul);
    VCHECK(vsunk[2].color == 0x800000fful);

    /* The first argument offsets into the arrays: first 1, count 2 is not
     * a triangle and draws nothing. */
    vsunk_triangles = 0ul;
    v9x_gl_arrays_draw(&s, &p, &a, V9X_GL_TRIANGLES, 1, 2);
    VCHECK(vsunk_triangles == 0ul && !s.in_begin);

    /* Without the vertex array enabled nothing is drawn, but the colour
     * array still sets the current colour. */
    v9x_gl_arrays_client_state(&s, &a, V9X_GL_VERTEX_ARRAY, 0);
    v9x_gl_arrays_draw(&s, &p, &a, V9X_GL_TRIANGLES, 0, 3);
    VCHECK(vsunk_triangles == 0ul);
    VCHECK(vnear(p.color[2], 1.0f) && vnear(p.color[3], 128.0f / 255.0f));

    /* Errors. */
    v9x_gl_arrays_draw(&s, &p, &a, V9X_GL_TRIANGLES, 0, -1);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    v9x_gl_arrays_draw(&s, &p, &a, 0x000Au, 0, 3);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    VCHECK(!s.in_begin);
    v9x_gl_arrays_client_state(&s, &a, V9X_GL_VERTEX_ARRAY, 1);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    v9x_gl_arrays_draw(&s, &p, &a, V9X_GL_TRIANGLES, 0, 3);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);
    v9x_gl_prim_end(&s, &p);
    VCHECK(vsunk_triangles == 0ul);
}

static void test_draw_elements_and_strides(void)
{
    /* Shorts x, y with two shorts of padding: stride 8. */
    static const GLshort xy[12] = { 0, 0, 99, 99,  320, 0, 99, 99,
                                    0, 200, 99, 99 };
    static const GLushort order[3] = { 2u, 0u, 1u };
    static const GLubyte order8[3] = { 0u, 1u, 2u };
    static const GLuint order32[3] = { 1u, 2u, 0u };

    vscene();
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_VERTEX, 2, V9X_GL_SHORT, 8,
                          xy);
    v9x_gl_arrays_client_state(&s, &a, V9X_GL_VERTEX_ARRAY, 1);
    v9x_gl_arrays_draw_elements(&s, &p, &a, V9X_GL_TRIANGLES, 3,
                                V9X_GL_UNSIGNED_SHORT, order);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    VCHECK(vsunk_triangles == 1ul);
    VCHECK(vnear(vsunk[0].sx, 0.0f) && vnear(vsunk[0].sy, 0.0f));
    VCHECK(vnear(vsunk[1].sx, 0.0f) && vnear(vsunk[1].sy, 200.0f));
    VCHECK(vnear(vsunk[2].sx, 320.0f) && vnear(vsunk[2].sy, 200.0f));

    v9x_gl_arrays_draw_elements(&s, &p, &a, V9X_GL_TRIANGLES, 3,
                                V9X_GL_UNSIGNED_BYTE, order8);
    v9x_gl_arrays_draw_elements(&s, &p, &a, V9X_GL_TRIANGLES, 3,
                                V9X_GL_UNSIGNED_INT, order32);
    VCHECK(vsunk_triangles == 3ul);
    VCHECK(vnear(vsunk[3].sx, 0.0f) && vnear(vsunk[3].sy, 200.0f));
    VCHECK(vnear(vsunk[6].sx, 320.0f) && vnear(vsunk[6].sy, 200.0f));

    v9x_gl_arrays_draw_elements(&s, &p, &a, V9X_GL_TRIANGLES, 3,
                                V9X_GL_SHORT, order);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_arrays_draw_elements(&s, &p, &a, V9X_GL_TRIANGLES, -3,
                                V9X_GL_UNSIGNED_SHORT, order);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
    VCHECK(vsunk_triangles == 3ul && !s.in_begin);
}

static void test_conversions(void)
{
    static const GLbyte colour_b[4] = { 127, -128, 0, 127 };
    static const GLushort colour_us[3] = { 65535u, 0u, 65535u };
    static const GLshort normal_s[3] = { 32767, 0, -32768 };
    static const GLdouble tex_d[1] = { 0.25 };
    static const GLint vertex_i[3] = { 10, 20, 30 };

    vscene();
    /* Table 2.6: signed byte c maps to (2c + 1) / 255, so 127 is 1 and
     * -128 is -1; 0 is 1/255. */
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_COLOR, 4, V9X_GL_BYTE, 0,
                          colour_b);
    v9x_gl_arrays_client_state(&s, &a, V9X_GL_COLOR_ARRAY, 1);
    v9x_gl_arrays_element(&s, &p, &a, 0);
    VCHECK(vnear(p.color[0], 1.0f) && vnear(p.color[1], -1.0f) &&
           vnear(p.color[2], 1.0f / 255.0f) && vnear(p.color[3], 1.0f));

    /* Unsigned short divides by 65535; a size-3 colour has alpha 1. */
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_COLOR, 3,
                          V9X_GL_UNSIGNED_SHORT, 0, colour_us);
    v9x_gl_prim_color(&p, 0.0f, 0.0f, 0.0f, 0.0f);
    v9x_gl_arrays_element(&s, &p, &a, 0);
    VCHECK(vnear(p.color[0], 1.0f) && vnear(p.color[1], 0.0f) &&
           vnear(p.color[3], 1.0f));

    /* Normals convert as colours do. */
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_NORMAL, 3, V9X_GL_SHORT, 0,
                          normal_s);
    v9x_gl_arrays_client_state(&s, &a, V9X_GL_NORMAL_ARRAY, 1);
    v9x_gl_arrays_element(&s, &p, &a, 0);
    VCHECK(vnear(p.normal[0], 1.0f) && vnear(p.normal[1], 1.0f / 65535.0f)
           && vnear(p.normal[2], -1.0f));

    /* Texture coordinates and vertices are not normalized; missing
     * components default to t 0, r 0, q 1 and z 0, w 1. */
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_TEXCOORD, 1, V9X_GL_DOUBLE, 0,
                          tex_d);
    v9x_gl_arrays_client_state(&s, &a, V9X_GL_TEXTURE_COORD_ARRAY, 1);
    v9x_gl_prim_texcoord(&p, 9.0f, 9.0f, 9.0f, 9.0f);
    v9x_gl_arrays_element(&s, &p, &a, 0);
    VCHECK(vnear(p.tex[0], 0.25f) && vnear(p.tex[1], 0.0f) &&
           vnear(p.tex[2], 0.0f) && vnear(p.tex[3], 1.0f));

    /* An INT vertex of size 3 inside Begin/End reaches the pipeline. */
    v9x_gl_arrays_pointer(&s, &a, V9X_GL_ARRAY_VERTEX, 3, V9X_GL_INT, 0,
                          vertex_i);
    v9x_gl_arrays_client_state(&s, &a, V9X_GL_VERTEX_ARRAY, 1);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    v9x_gl_arrays_element(&s, &p, &a, 0);
    VCHECK(p.count == 1ul);
    v9x_gl_prim_end(&s, &p);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
}

static void test_interleaved(void)
{
    /* T2F_C4UB_V3F: s t | r g b a (4 ubytes) | x y z, 24 bytes a record. */
    static struct {
        GLfloat st[2];
        GLubyte rgba[4];
        GLfloat xyz[3];
    } records[3] = {
        { { 0.0f, 0.0f }, { 255u, 0u, 0u, 255u }, { 0.0f, 0.0f, 0.0f } },
        { { 1.0f, 0.0f }, { 255u, 0u, 0u, 255u }, { 320.0f, 0.0f, 0.0f } },
        { { 0.5f, 1.0f }, { 255u, 0u, 0u, 255u }, { 0.0f, 200.0f, 0.0f } }
    };
    static GLfloat block[16];
    GLboolean enabled;
    void *pointer;

    vscene();
    VCHECK((const char *)&records[1] - (const char *)&records[0] == 24);
    v9x_gl_arrays_client_state(&s, &a, V9X_GL_NORMAL_ARRAY, 1);
    v9x_gl_arrays_interleaved(&s, &a, 0x2A29u, 0, records);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    VCHECK(v9x_gl_arrays_is_enabled(&a, V9X_GL_NORMAL_ARRAY, &enabled) &&
           enabled == 0u);
    VCHECK(v9x_gl_arrays_is_enabled(&a, V9X_GL_TEXTURE_COORD_ARRAY,
                                    &enabled) && enabled == 1u);
    VCHECK(a.array[V9X_GL_ARRAY_VERTEX].stride == 24 &&
           a.array[V9X_GL_ARRAY_COLOR].type == V9X_GL_UNSIGNED_BYTE);
    v9x_gl_arrays_draw(&s, &p, &a, V9X_GL_TRIANGLES, 0, 3);
    VCHECK(vsunk_triangles == 1ul);
    VCHECK(vnear(vsunk[1].sx, 320.0f) && vnear(vsunk[1].tu, 1.0f));
    VCHECK(vnear(vsunk[2].tv, 1.0f) && vsunk[2].color == 0xffff0000ul);

    /* T4F_C4F_N3F_V4F: the vertex starts 11 floats in and a record is 15
     * floats; an explicit stride replaces the record size. */
    v9x_gl_arrays_interleaved(&s, &a, 0x2A2Du, 0, block);
    v9x_gl_arrays_get_pointer(&s, &a, 0x808Eu, &pointer);
    VCHECK(pointer == (void *)&block[11]);
    VCHECK(a.array[V9X_GL_ARRAY_VERTEX].stride == 60 &&
           a.array[V9X_GL_ARRAY_VERTEX].size == 4);
    v9x_gl_arrays_get_pointer(&s, &a, 0x808Fu, &pointer);
    VCHECK(pointer == (void *)&block[8]);
    v9x_gl_arrays_interleaved(&s, &a, 0x2A20u, 64, block);   /* V2F */
    VCHECK(a.array[V9X_GL_ARRAY_VERTEX].stride == 64 &&
           a.array[V9X_GL_ARRAY_VERTEX].size == 2);
    VCHECK(v9x_gl_arrays_is_enabled(&a, V9X_GL_COLOR_ARRAY, &enabled) &&
           enabled == 0u);

    v9x_gl_arrays_interleaved(&s, &a, 0x2A2Eu, 0, block);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_arrays_interleaved(&s, &a, 0x2A20u, -1, block);
    VCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_VALUE);
}

unsigned int v9x_run_gl_varray_tests(void)
{
    gl_varray_failures = 0u;
    test_pointer_and_enable_errors();
    test_draw_arrays();
    test_draw_elements_and_strides();
    test_conversions();
    test_interleaved();
    if (gl_varray_failures == 0u) {
        printf("PASS: OpenGL vertex arrays\n");
    }
    return gl_varray_failures;
}
