/*
 * Tests for the vertex pipeline (src\opengl\gl_prim.c): vertices in, render
 * interface vertices out, compared exactly for scenes whose answer is
 * arithmetic - the window-to-surface flip, the depth mapping, provoking
 * vertices, strip winding, culling, clipping against the frustum, batch
 * boundaries - and the GL-to-interface fragment state.
 */
#include <stdio.h>
#include "../../src/opengl/gl_prim.h"

static unsigned int gl_prim_failures;

#define V9X_GL_SCISSOR_TEST_CAP 0x0C11u

#define PCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++gl_prim_failures; \
    } \
} while (0)

/* Everything the sink was given, across batches. */
#define SINK_MAX 256u
static V9X_R3D_ABI_VERTEX sunk[3u * SINK_MAX];
static v9x_u32 sunk_triangles;
static v9x_u32 sunk_batches;

static int sink(void *user, const V9X_R3D_ABI_VERTEX *vertices,
                v9x_u32 triangle_count)
{
    v9x_u32 i;

    (void)user;
    for (i = 0ul; i < triangle_count * 3ul && sunk_triangles * 3ul + i <
                                                  3ul * SINK_MAX; ++i) {
        sunk[sunk_triangles * 3ul + i] = vertices[i];
    }
    sunk_triangles += triangle_count;
    ++sunk_batches;
    return 1;
}

/* Vertices captured so far, within the capture array. */
static v9x_u32 vsunk_count(void)
{
    return sunk_triangles * 3ul < 3ul * SINK_MAX ? sunk_triangles * 3ul
                                                 : 3ul * SINK_MAX;
}

static int near_value(float a, float b)
{
    float d = a - b;

    return d < 0.001f && d > -0.001f;
}

/* A context on a 320x200 drawable with an ortho projection that makes
 * window coordinates equal to object x and y, and z 0 at depth 0.5. */
static void scene(V9X_GL_STATE *s, V9X_GL_PIPELINE *p)
{
    v9x_gl_state_init(s);
    v9x_gl_state_drawable(s, 320ul, 200ul, V9X_GL_TARGET_RGB565, 1);
    v9x_gl_pipeline_init(p);
    v9x_gl_pipeline_sink(p, sink, 0);
    v9x_gl_state_matrix_mode(s, V9X_GL_PROJECTION);
    v9x_gl_state_ortho(s, 0.0, 320.0, 0.0, 200.0, -1.0, 1.0);
    v9x_gl_state_matrix_mode(s, V9X_GL_MODELVIEW);
    sunk_triangles = 0ul;
    sunk_batches = 0ul;
}

static void vertex(V9X_GL_STATE *s, V9X_GL_PIPELINE *p, float x, float y)
{
    v9x_gl_prim_vertex(s, p, x, y, 0.0f, 1.0f);
}

static void test_triangle_to_surface(void)
{
    V9X_GL_STATE s;
    V9X_GL_PIPELINE p;

    scene(&s, &p);
    v9x_gl_prim_color(&p, 1.0f, 0.0f, 0.0f, 1.0f);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    vertex(&s, &p, 0.0f, 0.0f);
    vertex(&s, &p, 320.0f, 0.0f);
    vertex(&s, &p, 0.0f, 200.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
    PCHECK(sunk_triangles == 1ul && sunk_batches == 1ul);
    /* Window y up becomes surface y down: (0,0) is the bottom-left, row
     * 200. */
    PCHECK(near_value(sunk[0].sx, 0.0f) && near_value(sunk[0].sy, 200.0f));
    PCHECK(near_value(sunk[1].sx, 320.0f) && near_value(sunk[1].sy, 200.0f));
    PCHECK(near_value(sunk[2].sx, 0.0f) && near_value(sunk[2].sy, 0.0f));
    PCHECK(near_value(sunk[0].sz, 0.5f) && near_value(sunk[0].rhw, 1.0f));
    PCHECK(sunk[0].color == 0xffff0000ul && sunk[2].color == 0xffff0000ul);
    PCHECK((sunk[0].specular >> 24) == 0xfful);     /* unfogged */
}

static void test_provoking_vertex_and_quads(void)
{
    V9X_GL_STATE s;
    V9X_GL_PIPELINE p;

    /* Flat: a quad's colour is its fourth vertex's, for both triangles. */
    scene(&s, &p);
    v9x_gl_prim_shade_model(&s, &p, V9X_GL_FLAT);
    v9x_gl_prim_begin(&s, &p, V9X_GL_QUADS);
    v9x_gl_prim_color(&p, 1.0f, 0.0f, 0.0f, 1.0f);
    vertex(&s, &p, 10.0f, 10.0f);
    vertex(&s, &p, 50.0f, 10.0f);
    vertex(&s, &p, 50.0f, 50.0f);
    v9x_gl_prim_color(&p, 0.0f, 0.0f, 1.0f, 1.0f);
    vertex(&s, &p, 10.0f, 50.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles == 2ul);
    PCHECK(sunk[0].color == 0xff0000fful && sunk[5].color == 0xff0000fful);

    /* A polygon's is its first vertex's. */
    scene(&s, &p);
    v9x_gl_prim_shade_model(&s, &p, V9X_GL_FLAT);
    v9x_gl_prim_begin(&s, &p, V9X_GL_POLYGON);
    v9x_gl_prim_color(&p, 0.0f, 1.0f, 0.0f, 1.0f);
    vertex(&s, &p, 10.0f, 10.0f);
    v9x_gl_prim_color(&p, 1.0f, 0.0f, 0.0f, 1.0f);
    vertex(&s, &p, 50.0f, 10.0f);
    vertex(&s, &p, 50.0f, 50.0f);
    vertex(&s, &p, 30.0f, 70.0f);
    vertex(&s, &p, 10.0f, 50.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles == 3ul);
    PCHECK(sunk[0].color == 0xff00ff00ul && sunk[8].color == 0xff00ff00ul);

    /* Smooth keeps each vertex's own colour. */
    scene(&s, &p);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    v9x_gl_prim_color(&p, 1.0f, 0.0f, 0.0f, 1.0f);
    vertex(&s, &p, 10.0f, 10.0f);
    v9x_gl_prim_color(&p, 0.0f, 1.0f, 0.0f, 1.0f);
    vertex(&s, &p, 50.0f, 10.0f);
    v9x_gl_prim_color(&p, 0.0f, 0.0f, 1.0f, 1.0f);
    vertex(&s, &p, 10.0f, 50.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk[0].color == 0xffff0000ul && sunk[1].color == 0xff00ff00ul &&
           sunk[2].color == 0xff0000fful);

    /* An incomplete primitive draws nothing: two vertices of a triangle,
     * three of a quad. */
    scene(&s, &p);
    v9x_gl_prim_begin(&s, &p, V9X_GL_QUADS);
    vertex(&s, &p, 10.0f, 10.0f);
    vertex(&s, &p, 50.0f, 10.0f);
    vertex(&s, &p, 50.0f, 50.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles == 0ul);
}

/* Twice the signed area of a sunk triangle in window orientation (y up):
 * positive is counter-clockwise on screen. */
static float window_area(v9x_u32 triangle)
{
    const V9X_R3D_ABI_VERTEX *v = &sunk[triangle * 3ul];
    float ax = v[1].sx - v[0].sx;
    float ay = -(v[1].sy - v[0].sy);
    float bx = v[2].sx - v[0].sx;
    float by = -(v[2].sy - v[0].sy);

    return ax * by - ay * bx;
}

static void test_strips_winding_and_culling(void)
{
    V9X_GL_STATE s;
    V9X_GL_PIPELINE p;

    /* A strip of four is two triangles with the same facing: the second
     * takes its first two vertices swapped (2.6.1). */
    scene(&s, &p);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLE_STRIP);
    vertex(&s, &p, 10.0f, 10.0f);
    vertex(&s, &p, 50.0f, 10.0f);
    vertex(&s, &p, 10.0f, 50.0f);
    vertex(&s, &p, 50.0f, 50.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles == 2ul);
    PCHECK(window_area(0ul) > 0.0f && window_area(1ul) > 0.0f);

    /* A fan of five is three triangles, all from the first vertex. */
    scene(&s, &p);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLE_FAN);
    vertex(&s, &p, 30.0f, 30.0f);
    vertex(&s, &p, 50.0f, 30.0f);
    vertex(&s, &p, 50.0f, 50.0f);
    vertex(&s, &p, 30.0f, 50.0f);
    vertex(&s, &p, 10.0f, 50.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles == 3ul);
    PCHECK(near_value(sunk[6].sx, 30.0f) && near_value(sunk[6].sy, 170.0f));

    /* Culling in window space: back faces, the default, removes a
     * clockwise triangle and keeps a counter-clockwise one; FRONT the
     * reverse; FRONT_AND_BACK both; glFrontFace(CW) swaps the meaning. */
    scene(&s, &p);
    v9x_gl_state_enable(&s, V9X_GL_CULL_FACE, 1);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    vertex(&s, &p, 10.0f, 10.0f);           /* counter-clockwise */
    vertex(&s, &p, 50.0f, 10.0f);
    vertex(&s, &p, 10.0f, 50.0f);
    vertex(&s, &p, 10.0f, 10.0f);           /* clockwise */
    vertex(&s, &p, 10.0f, 50.0f);
    vertex(&s, &p, 50.0f, 10.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles == 1ul && window_area(0ul) > 0.0f);
    sunk_triangles = 0ul;
    v9x_gl_prim_cull_face(&s, &p, V9X_GL_FRONT);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    vertex(&s, &p, 10.0f, 10.0f);
    vertex(&s, &p, 50.0f, 10.0f);
    vertex(&s, &p, 10.0f, 50.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles == 0ul);
    v9x_gl_prim_front_face(&s, &p, V9X_GL_CW);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    vertex(&s, &p, 10.0f, 10.0f);
    vertex(&s, &p, 50.0f, 10.0f);
    vertex(&s, &p, 10.0f, 50.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles == 1ul);
    sunk_triangles = 0ul;
    v9x_gl_prim_cull_face(&s, &p, V9X_GL_FRONT_AND_BACK);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    vertex(&s, &p, 10.0f, 10.0f);
    vertex(&s, &p, 50.0f, 10.0f);
    vertex(&s, &p, 10.0f, 50.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles == 0ul);
}

static void test_clipping(void)
{
    V9X_GL_STATE s;
    V9X_GL_PIPELINE p;
    v9x_u32 i;
    int inside = 1;

    /* Past the right edge: cut at x = w, every output vertex on the
     * target, and more than one triangle for the resulting quadrilateral. */
    scene(&s, &p);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    vertex(&s, &p, 160.0f, 20.0f);
    vertex(&s, &p, 480.0f, 100.0f);
    vertex(&s, &p, 160.0f, 180.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles == 2ul);
    for (i = 0ul; i < sunk_triangles * 3ul; ++i) {
        if (sunk[i].sx < -0.01f || sunk[i].sx > 320.01f) {
            inside = 0;
        }
    }
    PCHECK(inside);

    /* Wholly outside: nothing. */
    scene(&s, &p);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    vertex(&s, &p, 400.0f, 20.0f);
    vertex(&s, &p, 480.0f, 100.0f);
    vertex(&s, &p, 400.0f, 180.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles == 0ul);

    /* Behind the eye under a perspective projection: the near plane cuts
     * it, and no vertex reaches the sink with a w that is not positive. */
    scene(&s, &p);
    v9x_gl_state_matrix_mode(&s, V9X_GL_PROJECTION);
    v9x_gl_state_load_identity(&s);
    v9x_gl_state_frustum(&s, -1.0, 1.0, -1.0, 1.0, 1.0, 100.0);
    v9x_gl_state_matrix_mode(&s, V9X_GL_MODELVIEW);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    v9x_gl_prim_vertex(&s, &p, -1.0f, -1.0f, -5.0f, 1.0f);
    v9x_gl_prim_vertex(&s, &p, 1.0f, -1.0f, -5.0f, 1.0f);
    v9x_gl_prim_vertex(&s, &p, 0.0f, 1.0f, 5.0f, 1.0f);    /* behind */
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles >= 1ul);
    inside = 1;
    for (i = 0ul; i < sunk_triangles * 3ul; ++i) {
        if (!(sunk[i].rhw > 0.0f) || sunk[i].sz < -0.001f ||
            sunk[i].sz > 1.001f) {
            inside = 0;
        }
    }
    PCHECK(inside);
    /* The two unclipped vertices are at z = -5, so rhw 1/5; the clipper
     * may emit the cut vertices first, so look rather than index. */
    inside = 0;
    for (i = 0ul; i < sunk_triangles * 3ul; ++i) {
        if (near_value(sunk[i].rhw, 0.2f)) {
            ++inside;
        }
    }
    PCHECK(inside >= 2);
}

static void test_depth_range_and_batches(void)
{
    V9X_GL_STATE s;
    V9X_GL_PIPELINE p;
    unsigned int i;

    /* Reversed depth range: the near plane (z = 1 in ortho object space,
     * ndc -1) maps to 1. */
    scene(&s, &p);
    v9x_gl_prim_depth_range(&s, &p, 1.0, 0.0);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    v9x_gl_prim_vertex(&s, &p, 10.0f, 10.0f, 1.0f, 1.0f);
    v9x_gl_prim_vertex(&s, &p, 50.0f, 10.0f, 1.0f, 1.0f);
    v9x_gl_prim_vertex(&s, &p, 10.0f, 50.0f, 1.0f, 1.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(near_value(sunk[0].sz, 1.0f));

    /* 70 triangles: one full batch of 64 as it fills, the rest at End. */
    scene(&s, &p);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    for (i = 0u; i < 70u; ++i) {
        vertex(&s, &p, 10.0f, 10.0f);
        vertex(&s, &p, 50.0f, 10.0f);
        vertex(&s, &p, 10.0f, 50.0f);
    }
    PCHECK(sunk_batches == 1ul && sunk_triangles == 64ul);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_batches == 2ul && sunk_triangles == 70ul);
}

static void test_errors_and_abi_state(void)
{
    V9X_GL_STATE s;
    V9X_GL_PIPELINE p;
    V9X_R3D_ABI_STATE a;

    scene(&s, &p);
    v9x_gl_prim_end(&s, &p);
    PCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);
    v9x_gl_prim_begin(&s, &p, 0x000Au);
    PCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    PCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);
    v9x_gl_prim_shade_model(&s, &p, V9X_GL_FLAT);
    PCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_OPERATION);
    v9x_gl_prim_end(&s, &p);
    v9x_gl_prim_shade_model(&s, &p, 0x1234u);
    PCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_prim_depth_func(&s, &p, 0x0208u);
    PCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);
    v9x_gl_prim_blend_func(&s, &p, V9X_GL_ONE, 0x0308u); /* SATURATE as dst */
    PCHECK(v9x_gl_state_get_error(&s) == V9X_GL_INVALID_ENUM);

    /* The interface's state: GL's numbers mapped onto D3D's. */
    scene(&s, &p);
    v9x_gl_prim_abi_state(&s, &p, &a);
    PCHECK(a.depth_enable == 0ul && a.blend_enable == 0ul &&
           a.alpha_test_enable == 0ul);
    PCHECK(a.depth_func == 2ul);                /* LESS */
    PCHECK(a.write_mask == 7ul && a.depth_write == 1ul);
    PCHECK(a.scissor_right == 320ul && a.scissor_bottom == 200ul);
    v9x_gl_state_enable(&s, 0x0B71u, 1);        /* DEPTH_TEST */
    v9x_gl_prim_depth_func(&s, &p, V9X_GL_LEQUAL);
    v9x_gl_state_enable(&s, V9X_GL_BLEND, 1);
    v9x_gl_prim_blend_func(&s, &p, V9X_GL_SRC_ALPHA, V9X_GL_ONE_MINUS_SRC_ALPHA);
    v9x_gl_state_enable(&s, V9X_GL_ALPHA_TEST, 1);
    v9x_gl_prim_alpha_func(&s, &p, 0x0204u, 0.666f);    /* GREATER */
    v9x_gl_state_depth_mask(&s, 0);
    v9x_gl_prim_abi_state(&s, &p, &a);
    PCHECK(a.depth_enable == 1ul && a.depth_func == 4ul && a.depth_write == 0ul);
    PCHECK(a.blend_enable == 1ul && a.src_blend == 5ul && a.dst_blend == 6ul);
    PCHECK(a.alpha_test_enable == 1ul && a.alpha_func == 5ul &&
           a.alpha_ref == 170ul);
    v9x_gl_prim_blend_func(&s, &p, V9X_GL_SRC_ALPHA_SATURATE, V9X_GL_ZERO);
    v9x_gl_prim_abi_state(&s, &p, &a);
    PCHECK(a.src_blend == 11ul && a.dst_blend == 1ul);
    PCHECK(v9x_gl_state_get_error(&s) == V9X_GL_NO_ERROR);
}

/* A float's sign bit, read through its bytes (-0.0 compares equal to 0). */
static int sign_set(float value)
{
    union {
        float f;
        v9x_u32 u;
    } bits;

    bits.f = value;
    return (bits.u & 0x80000000ul) != 0ul;
}

/*
 * Clipped vertices land inside the viewport and the depth range, with no
 * negative zero: Gen3's stream builder refuses both a coordinate past the
 * surface and any sign bit, and float error in the clip or the mapping can
 * produce either at an edge. Skewed perspective triangles through every
 * plane, several shapes, every emitted vertex checked.
 */
static void test_clipped_vertices_stay_inside(void)
{
    static const float far_x[6] = { -1000.0f, -333.3f, -0.001f, 700.1f,
                                    1234.5f, 320.0001f };
    V9X_GL_STATE s;
    V9X_GL_PIPELINE p;
    v9x_u32 i;
    unsigned int shape;
    int bad = 0;

    for (shape = 0u; shape < 6u; ++shape) {
        scene(&s, &p);
        v9x_gl_state_matrix_mode(&s, V9X_GL_PROJECTION);
        v9x_gl_state_load_identity(&s);
        v9x_gl_state_frustum(&s, -1.0, 1.0, -1.0, 1.0, 1.0, 10.0);
        v9x_gl_state_matrix_mode(&s, V9X_GL_MODELVIEW);
        v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
        v9x_gl_prim_vertex(&s, &p, far_x[shape] / 100.0f, -3.0f, -1.3f,
                           1.0f);
        v9x_gl_prim_vertex(&s, &p, 7.0f, -0.1f, -3.1f, 1.0f);
        v9x_gl_prim_vertex(&s, &p, -0.2f, 9.0f, -2.7f, 1.0f);
        v9x_gl_prim_vertex(&s, &p, -5.0f, -5.0f, -0.5f, 1.0f);
        v9x_gl_prim_vertex(&s, &p, 5.0f, 5.0f, -30.0f, 1.0f);
        v9x_gl_prim_vertex(&s, &p, 0.3f, 0.2f, -2.0f, 1.0f);
        v9x_gl_prim_end(&s, &p);
        for (i = 0ul; i < vsunk_count(); ++i) {
            const V9X_R3D_ABI_VERTEX *v = &sunk[i];

            if (sign_set(v->sx) || sign_set(v->sy) || sign_set(v->sz) ||
                v->sx > 320.0f || v->sy > 200.0f || v->sz > 1.0f ||
                !(v->rhw > 0.0f)) {
                ++bad;
            }
        }
    }
    PCHECK(bad == 0);
    PCHECK(sunk_triangles != 0ul);
}

/* Which batches may be merged: equal state and texture only. */
/*
 * The scissor box and the drawable as clip planes: a triangle covering the
 * whole 320x200 drawable comes out inside a scissor of x 100..200, y
 * 50..150 (GL window coordinates, y up; surface rows 50..150 too), the
 * interface's scissor is the whole surface - so an engine without one
 * draws it - and a viewport larger than the drawable is cut to the
 * drawable. A triangle outside the box draws nothing.
 */
static void test_scissor_clips_geometry(void)
{
    V9X_GL_STATE s;
    V9X_GL_PIPELINE p;
    V9X_R3D_ABI_STATE a;
    v9x_u32 i;
    int outside = 0;

    scene(&s, &p);
    v9x_gl_state_enable(&s, V9X_GL_SCISSOR_TEST_CAP, 1);
    v9x_gl_state_scissor(&s, 100, 50, 100, 100);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    vertex(&s, &p, -50.0f, -50.0f);
    vertex(&s, &p, 700.0f, -50.0f);
    vertex(&s, &p, -50.0f, 450.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles != 0ul);
    for (i = 0ul; i < sunk_triangles * 3ul && i < 3ul * SINK_MAX; ++i) {
        if (sunk[i].sx < 99.999f || sunk[i].sx > 200.001f ||
            sunk[i].sy < 49.999f || sunk[i].sy > 150.001f) {
            outside = 1;
        }
    }
    PCHECK(!outside);
    v9x_gl_prim_abi_state(&s, &p, &a);
    PCHECK(a.scissor_left == 0ul && a.scissor_top == 0ul &&
           a.scissor_right == 320ul && a.scissor_bottom == 200ul);

    /* Entirely outside the box: nothing. */
    sunk_triangles = 0ul;
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    vertex(&s, &p, 0.0f, 0.0f);
    vertex(&s, &p, 50.0f, 0.0f);
    vertex(&s, &p, 0.0f, 40.0f);
    v9x_gl_prim_end(&s, &p);
    PCHECK(sunk_triangles == 0ul);

    /* No scissor, a viewport past the drawable on every side. */
    scene(&s, &p);
    v9x_gl_state_viewport(&s, -100, -100, 520, 400);
    v9x_gl_prim_begin(&s, &p, V9X_GL_TRIANGLES);
    vertex(&s, &p, -10.0f, -10.0f);
    vertex(&s, &p, 330.0f, -10.0f);
    vertex(&s, &p, -10.0f, 210.0f);
    v9x_gl_prim_end(&s, &p);
    outside = 0;
    PCHECK(sunk_triangles != 0ul);
    for (i = 0ul; i < sunk_triangles * 3ul && i < 3ul * SINK_MAX; ++i) {
        if (sunk[i].sx < 0.0f || sunk[i].sx > 320.0f ||
            sunk[i].sy < 0.0f || sunk[i].sy > 200.0f) {
            outside = 1;
        }
    }
    PCHECK(!outside);
}

static void test_same_draw(void)
{
    static v9x_u16 texels_a[4];
    static v9x_u16 texels_b[4];
    V9X_R3D_ABI_LEVEL level_a;
    V9X_R3D_ABI_LEVEL level_a2;
    V9X_R3D_ABI_LEVEL level_b;
    V9X_R3D_ABI_TEXTURE ta;
    V9X_R3D_ABI_TEXTURE tb;
    V9X_R3D_ABI_STATE sa;
    V9X_R3D_ABI_STATE sb;
    V9X_GL_STATE s;
    V9X_GL_PIPELINE p;
    unsigned int i;

    scene(&s, &p);
    v9x_gl_prim_abi_state(&s, &p, &sa);
    v9x_gl_prim_abi_state(&s, &p, &sb);
    for (i = 0u; i < sizeof(ta); ++i) {
        ((v9x_u8 *)&ta)[i] = 0u;
        ((v9x_u8 *)&tb)[i] = 0u;
    }
    /* Untextured, same state. */
    PCHECK(v9x_gl_prim_same_draw(&ta, &sa, &tb, &sb));

    /* Textured: the same images through two level arrays still match -
     * the batch's own copy of the description is a different array. */
    level_a.pixels = texels_a;
    level_a.width = 2ul;
    level_a.height = 2ul;
    level_a2 = level_a;
    level_b = level_a;
    level_b.pixels = texels_b;
    ta.storage = V9X_R3D_ABI_TEXTURE_CPU;
    ta.format = V9X_R3D_ABI_FORMAT_RGB565;
    ta.levels = &level_a;
    ta.level_count = 1ul;
    ta.min_filter = V9X_R3D_ABI_FILTER_LINEAR;
    ta.mag_filter = V9X_R3D_ABI_FILTER_LINEAR;
    ta.mip = V9X_R3D_ABI_MIP_NONE;
    ta.address = V9X_R3D_ABI_ADDRESS_WRAP;
    ta.color_op = V9X_R3D_ABI_COLOROP_MODULATE;
    tb = ta;
    tb.levels = &level_a2;
    PCHECK(v9x_gl_prim_same_draw(&ta, &sa, &tb, &sb));
    /* Another texture's images, a filter, an op: different draws. */
    tb.levels = &level_b;
    PCHECK(!v9x_gl_prim_same_draw(&ta, &sa, &tb, &sb));
    tb = ta;
    tb.min_filter = V9X_R3D_ABI_FILTER_NEAREST;
    PCHECK(!v9x_gl_prim_same_draw(&ta, &sa, &tb, &sb));
    tb = ta;
    tb.color_op = V9X_R3D_ABI_COLOROP_REPLACE;
    PCHECK(!v9x_gl_prim_same_draw(&ta, &sa, &tb, &sb));
    tb = ta;
    tb.storage = V9X_R3D_ABI_TEXTURE_NONE;
    PCHECK(!v9x_gl_prim_same_draw(&ta, &sa, &tb, &sb));
    /* A fragment state difference: blending on. */
    v9x_gl_state_enable(&s, V9X_GL_BLEND, 1);
    v9x_gl_prim_abi_state(&s, &p, &sb);
    PCHECK(!v9x_gl_prim_same_draw(&ta, &sa, &ta, &sb));
}

static void test_fragment_alpha_used(void)
{
    V9X_GL_STATE s;
    V9X_GL_PIPELINE p;

    scene(&s, &p);
    PCHECK(!v9x_gl_prim_fragment_alpha_used(&s, &p));
    v9x_gl_state_enable(&s, V9X_GL_BLEND, 1);
    /* ONE/ZERO, and Quake's lightmap pair ZERO/SRC_COLOR, read no alpha. */
    PCHECK(!v9x_gl_prim_fragment_alpha_used(&s, &p));
    v9x_gl_prim_blend_func(&s, &p, V9X_GL_ZERO, 0x0300u /* SRC_COLOR */);
    PCHECK(!v9x_gl_prim_fragment_alpha_used(&s, &p));
    v9x_gl_prim_blend_func(&s, &p, 0x0304u, 0x0305u);   /* DST_ALPHA pair */
    PCHECK(!v9x_gl_prim_fragment_alpha_used(&s, &p));
    v9x_gl_prim_blend_func(&s, &p, V9X_GL_SRC_ALPHA,
                           V9X_GL_ONE_MINUS_SRC_ALPHA);
    PCHECK(v9x_gl_prim_fragment_alpha_used(&s, &p));
    v9x_gl_prim_blend_func(&s, &p, V9X_GL_ONE, V9X_GL_ONE_MINUS_SRC_ALPHA);
    PCHECK(v9x_gl_prim_fragment_alpha_used(&s, &p));
    v9x_gl_prim_blend_func(&s, &p, V9X_GL_SRC_ALPHA_SATURATE, V9X_GL_ONE);
    PCHECK(v9x_gl_prim_fragment_alpha_used(&s, &p));
    /* The factors are held while blending is off, and read nothing. */
    v9x_gl_state_enable(&s, V9X_GL_BLEND, 0);
    PCHECK(!v9x_gl_prim_fragment_alpha_used(&s, &p));
    v9x_gl_state_enable(&s, V9X_GL_ALPHA_TEST, 1);
    PCHECK(v9x_gl_prim_fragment_alpha_used(&s, &p));
}

/*
 * Quake 2 leaves GL_ALPHA_TEST on (GREATER 0.666) for opaque 565 walls and
 * untextured fills. There the alpha tested is the vertex alpha, or one for
 * REPLACE, and a test every vertex passes by a margin discards nothing - so
 * the batch may be sent without it to an engine whose alpha test reads only
 * texel alpha (the Mach64).
 */
static void test_alpha_test_that_cannot_fail(void)
{
    V9X_R3D_ABI_STATE state;
    V9X_R3D_ABI_TEXTURE texture;
    V9X_R3D_ABI_VERTEX v[3];
    unsigned int i;

    for (i = 0u; i < sizeof(state); ++i) {
        ((unsigned char *)&state)[i] = 0u;
    }
    for (i = 0u; i < sizeof(texture); ++i) {
        ((unsigned char *)&texture)[i] = 0u;
    }
    for (i = 0u; i < sizeof(v); ++i) {
        ((unsigned char *)v)[i] = 0u;
    }
    state.alpha_test_enable = 1ul;
    state.alpha_func = 5ul;             /* GREATER */
    state.alpha_ref = 170ul;            /* 0.666 */
    texture.storage = V9X_R3D_ABI_TEXTURE_NONE;
    v[0].color = v[1].color = v[2].color = 0xFFFFFFFFul;
    PCHECK(v9x_gl_prim_alpha_test_passes(&state, &texture, v, 3ul));

    /* One vertex at or below the reference can fail inside the triangle. */
    v[2].color = 0xAAFFFFFFul;
    PCHECK(!v9x_gl_prim_alpha_test_passes(&state, &texture, v, 3ul));
    v[2].color = 0xABFFFFFFul;          /* 171: one above, no margin */
    PCHECK(!v9x_gl_prim_alpha_test_passes(&state, &texture, v, 3ul));
    v[2].color = 0xC0FFFFFFul;
    PCHECK(v9x_gl_prim_alpha_test_passes(&state, &texture, v, 3ul));

    /* A 565 texture: REPLACE's alpha is one whatever the vertices. */
    texture.storage = V9X_R3D_ABI_TEXTURE_HW;
    texture.format = V9X_R3D_ABI_FORMAT_RGB565;
    texture.color_op = V9X_R3D_ABI_COLOROP_REPLACE;
    texture.alpha_op = V9X_R3D_ABI_ALPHAOP_REPLACE;
    v[0].color = v[1].color = v[2].color = 0x00FFFFFFul;
    PCHECK(v9x_gl_prim_alpha_test_passes(&state, &texture, v, 3ul));
    texture.alpha_op = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    PCHECK(!v9x_gl_prim_alpha_test_passes(&state, &texture, v, 3ul));

    /* Texel alpha is not the ICD's to predict. */
    texture.format = V9X_R3D_ABI_FORMAT_ARGB4444;
    texture.alpha_op = V9X_R3D_ABI_ALPHAOP_REPLACE;
    PCHECK(!v9x_gl_prim_alpha_test_passes(&state, &texture, v, 3ul));

    /* EQUAL holds only for one constant alpha. */
    texture.storage = V9X_R3D_ABI_TEXTURE_NONE;
    state.alpha_func = 3ul;
    state.alpha_ref = 255ul;
    v[0].color = v[1].color = v[2].color = 0xFFFFFFFFul;
    PCHECK(v9x_gl_prim_alpha_test_passes(&state, &texture, v, 3ul));
    v[1].color = 0xFEFFFFFFul;
    PCHECK(!v9x_gl_prim_alpha_test_passes(&state, &texture, v, 3ul));
    state.alpha_func = 1ul;             /* NEVER */
    PCHECK(!v9x_gl_prim_alpha_test_passes(&state, &texture, v, 3ul));
    state.alpha_test_enable = 0ul;
    PCHECK(!v9x_gl_prim_alpha_test_passes(&state, &texture, v, 3ul));

    /* With the test gone, a blend may still read the source alpha. */
    PCHECK(!v9x_gl_prim_blend_reads_alpha(&state));
    state.blend_enable = 1ul;
    state.src_blend = 1ul;              /* ZERO */
    state.dst_blend = 3ul;              /* SRCCOLOR: Quake 2's lightmaps */
    PCHECK(!v9x_gl_prim_blend_reads_alpha(&state));
    state.src_blend = 5ul;              /* SRCALPHA */
    state.dst_blend = 6ul;
    PCHECK(v9x_gl_prim_blend_reads_alpha(&state));
    state.blend_enable = 0ul;
    PCHECK(!v9x_gl_prim_blend_reads_alpha(&state));
}

/*
 * glBegin's per-primitive setup follows every input it is taken from when
 * that input changes between primitives of one context: the scissor box
 * and its enable, the viewport, the drawable and the depth range. From
 * 2026-10-01 the setup is kept while they are unchanged, which this holds
 * to recomputing them.
 */
static void begin_triangle(V9X_GL_STATE *s, V9X_GL_PIPELINE *p)
{
    sunk_triangles = 0ul;
    v9x_gl_prim_begin(s, p, V9X_GL_TRIANGLES);
    vertex(s, p, 0.0f, 0.0f);
    vertex(s, p, 320.0f, 0.0f);
    vertex(s, p, 0.0f, 200.0f);
    v9x_gl_prim_end(s, p);
}

static int sunk_within(float left, float top, float right, float bottom)
{
    v9x_u32 i;

    if (sunk_triangles == 0ul) {
        return 0;
    }
    for (i = 0ul; i < vsunk_count(); ++i) {
        if (sunk[i].sx < left - 0.001f || sunk[i].sx > right + 0.001f ||
            sunk[i].sy < top - 0.001f || sunk[i].sy > bottom + 0.001f) {
            return 0;
        }
    }
    return 1;
}

static void test_begin_follows_state_changes(void)
{
    V9X_GL_STATE s;
    V9X_GL_PIPELINE p;

    scene(&s, &p);
    begin_triangle(&s, &p);
    PCHECK(sunk_triangles == 1ul && sunk_within(0.0f, 0.0f, 320.0f, 200.0f));
    PCHECK(near_value(sunk[1].sx, 320.0f));

    /* The scissor box, enabled and then moved. */
    v9x_gl_state_enable(&s, V9X_GL_SCISSOR_TEST_CAP, 1);
    v9x_gl_state_scissor(&s, 100, 50, 100, 100);
    begin_triangle(&s, &p);
    PCHECK(sunk_within(100.0f, 50.0f, 200.0f, 150.0f));
    v9x_gl_state_scissor(&s, 0, 0, 50, 50);
    begin_triangle(&s, &p);
    PCHECK(sunk_within(0.0f, 150.0f, 50.0f, 200.0f));
    v9x_gl_state_enable(&s, V9X_GL_SCISSOR_TEST_CAP, 0);
    begin_triangle(&s, &p);
    PCHECK(sunk_triangles == 1ul && near_value(sunk[1].sx, 320.0f));

    /* The viewport: object x 320 is the viewport's right edge. */
    v9x_gl_state_viewport(&s, 0, 0, 160, 100);
    begin_triangle(&s, &p);
    PCHECK(sunk_triangles == 1ul && near_value(sunk[1].sx, 160.0f));
    v9x_gl_state_viewport(&s, 0, 0, 320, 200);

    /* The depth range: z 0 maps to its midpoint. */
    v9x_gl_prim_depth_range(&s, &p, 0.0, 0.5);
    begin_triangle(&s, &p);
    PCHECK(sunk_triangles == 1ul && near_value(sunk[0].sz, 0.25f));
    v9x_gl_prim_depth_range(&s, &p, 0.0, 1.0);
    begin_triangle(&s, &p);
    PCHECK(sunk_triangles == 1ul && near_value(sunk[0].sz, 0.5f));

    /* The drawable: a smaller one cuts the same viewport. */
    v9x_gl_state_drawable(&s, 160ul, 100ul, V9X_GL_TARGET_RGB565, 1);
    begin_triangle(&s, &p);
    PCHECK(sunk_triangles != 0ul && sunk_within(0.0f, 0.0f, 160.0f, 100.0f));
}

/*
 * Every emitted byte, against the pipeline as it was before the 2026-10-01
 * vertex-path work (outcode-limited clipping, the inline inside test, the
 * vertex ring), which was to change no output. A fixed pseudo-random
 * stream of perspective vertices - many off screen, some behind the eye -
 * through every assembled mode, smooth and flat, culled and not, scissored
 * and not, with projective texture coordinates, hashed FNV-1a over each
 * sunk vertex. The golden values were taken from 1366659's pipeline.
 */
static v9x_u32 golden_hash;
static v9x_u32 golden_vertices;
static v9x_u32 golden_seed;
static v9x_u32 golden_counts[V9X_GL_PRIM_PROF_DWORDS];

static int golden_sink(void *user, const V9X_R3D_ABI_VERTEX *vertices,
                       v9x_u32 triangle_count)
{
    const unsigned char *bytes = (const unsigned char *)vertices;
    v9x_u32 count = triangle_count * 3ul * (v9x_u32)sizeof(*vertices);
    v9x_u32 i;

    (void)user;
    for (i = 0ul; i < count; ++i) {
        golden_hash = (golden_hash ^ bytes[i]) * 16777619ul;
    }
    golden_vertices += triangle_count * 3ul;
    return 1;
}

/* -range..range from a 32-bit LCG. */
static float golden_random(float range)
{
    golden_seed = golden_seed * 1664525ul + 1013904223ul;
    return ((float)(golden_seed >> 8) / 8388608.0f - 1.0f) * range;
}

static void test_pipeline_output_unchanged(void)
{
    static const GLenum modes[6] = {
        V9X_GL_TRIANGLES, V9X_GL_TRIANGLE_STRIP, V9X_GL_TRIANGLE_FAN,
        V9X_GL_QUADS, V9X_GL_QUAD_STRIP, V9X_GL_POLYGON
    };
    V9X_GL_STATE s;
    V9X_GL_PIPELINE p;
    unsigned int pass;
    unsigned int prim;
    unsigned int k;

    golden_hash = 2166136261ul;
    golden_vertices = 0ul;
    golden_seed = 12345ul;
    for (k = 0u; k < V9X_GL_PRIM_PROF_DWORDS; ++k) {
        golden_counts[k] = 0ul;
    }
    for (pass = 0u; pass < 8u; ++pass) {
        v9x_gl_state_init(&s);
        v9x_gl_state_drawable(&s, 320ul, 200ul, V9X_GL_TARGET_RGB565, 1);
        v9x_gl_pipeline_init(&p);
        v9x_gl_pipeline_sink(&p, golden_sink, 0);
        p.profile = golden_counts;
        v9x_gl_state_matrix_mode(&s, V9X_GL_PROJECTION);
        v9x_gl_state_frustum(&s, -1.0, 1.0, -0.625, 0.625, 1.0, 400.0);
        v9x_gl_state_matrix_mode(&s, V9X_GL_MODELVIEW);
        v9x_gl_state_translate(&s, 0.0f, 0.0f, -60.0f);
        v9x_gl_state_rotate(&s, 17.0f + 11.0f * (GLfloat)pass, 0.3f, 1.0f,
                            0.2f);
        if ((pass & 1u) != 0u) {
            v9x_gl_state_enable(&s, V9X_GL_CULL_FACE, 1);
        }
        if ((pass & 2u) != 0u) {
            v9x_gl_prim_shade_model(&s, &p, V9X_GL_FLAT);
        }
        if ((pass & 4u) != 0u) {
            v9x_gl_state_enable(&s, V9X_GL_SCISSOR_TEST_CAP, 1);
            v9x_gl_state_scissor(&s, 40, 30, 200, 120);
        }
        for (prim = 0u; prim < 60u; ++prim) {
            unsigned int count = 3u + (unsigned int)(golden_seed % 7u);
            /* Alternately on screen and spread past every plane. */
            float spread = (prim & 1u) != 0u ? 1.0f : 0.15f;

            v9x_gl_prim_begin(&s, &p, modes[prim % 6u]);
            for (k = 0u; k < count; ++k) {
                v9x_gl_prim_color(&p, 0.5f + golden_random(0.5f),
                                  0.5f + golden_random(0.5f),
                                  0.5f + golden_random(0.5f), 1.0f);
                v9x_gl_prim_texcoord(&p, golden_random(4.0f),
                                     golden_random(4.0f), 0.0f,
                                     (k & 3u) == 3u ? 2.0f : 1.0f);
                v9x_gl_prim_vertex(&s, &p, golden_random(90.0f * spread),
                                   golden_random(60.0f * spread),
                                   golden_random(80.0f * spread), 1.0f);
            }
            v9x_gl_prim_end(&s, &p);
        }
        v9x_gl_prim_flush(&p);
    }
    printf("golden pipeline hash 0x%08lx over %lu vertices; fast %lu "
           "clipped %lu culled %lu\n", golden_hash, golden_vertices,
           golden_counts[V9X_GL_PRIM_PROF_FAST],
           golden_counts[V9X_GL_PRIM_PROF_CLIPPED],
           golden_counts[V9X_GL_PRIM_PROF_CULLED]);
    PCHECK(golden_hash == 0x0ed372e9ul && golden_vertices == 5763ul);
    /* The stream reaches every path it is meant to hold steady. */
    PCHECK(golden_counts[V9X_GL_PRIM_PROF_FAST] > 100ul);
    PCHECK(golden_counts[V9X_GL_PRIM_PROF_CLIPPED] > 100ul);
    PCHECK(golden_counts[V9X_GL_PRIM_PROF_CULLED] > 100ul);
}

unsigned int v9x_run_gl_prim_tests(void)
{
    gl_prim_failures = 0u;
    test_alpha_test_that_cannot_fail();
    test_triangle_to_surface();
    test_provoking_vertex_and_quads();
    test_strips_winding_and_culling();
    test_clipping();
    test_depth_range_and_batches();
    test_errors_and_abi_state();
    test_fragment_alpha_used();
    test_clipped_vertices_stay_inside();
    test_same_draw();
    test_scissor_clips_geometry();
    test_pipeline_output_unchanged();
    test_begin_follows_state_changes();
    if (gl_prim_failures == 0u) {
        printf("PASS: OpenGL vertex pipeline\n");
    }
    return gl_prim_failures;
}
