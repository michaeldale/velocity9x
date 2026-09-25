/*
 * Tests for the neutral core's clipper and list builder.
 *
 * Written as characterisation of what d3d_core.c did before Phase 1a of the
 * OpenGL plan moved the code: each case states the behaviour the D3D
 * regression set on the guests depends on - the guard band refusing a NaN
 * with a count, the right and bottom edges at width and height rather than
 * the last pixel, perspective-correct texture coordinates at a cut only when
 * rhw differs, runs of on-target triangles going through as windows on the
 * caller's array, a culled triangle ending a run and not being sent, a
 * refused triangle failing the call without stopping the rest of the list.
 *
 * The vertex layout is held against V9X_D3DTLVERTEX field by field in
 * d3d_core.c, at compile time, where both types are visible; the ABI header
 * that declares the D3D one is Windows-facing and stays out of the host
 * build. Here only the size is checked.
 */
#include <stdio.h>
#include <stddef.h>

#include "../../src/display32/r3d/r3d.h"

static unsigned int clip_failures = 0u;

#define CLCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++clip_failures; \
    } \
} while (0)

#define CLIP_WIDTH  640.0f
#define CLIP_HEIGHT 480.0f
#define CLIP_GUARD  2048.0f

static V9X_R3D_VERTEX clip_vertex(float x, float y, float rhw,
                                  unsigned long color, float u, float v)
{
    V9X_R3D_VERTEX vertex;

    vertex.sx = x;
    vertex.sy = y;
    vertex.sz = 0.5f;
    vertex.rhw = rhw;
    vertex.color = color;
    vertex.specular = 0xff000000ul;
    vertex.tu = u;
    vertex.tv = v;
    return vertex;
}

static float clip_nan(void)
{
    union {
        unsigned long bits;
        float value;
    } quiet;

    quiet.bits = 0x7fc00000ul;
    return quiet.value;
}

/* D3DTLVERTEX's layout, as d3dtypes.h fixes it: eight 4-byte fields in
 * this order. Through a table so the comparison is made at run time. */
static void test_vertex_matches_tlvertex(void)
{
    static const size_t offsets[8] = {
        offsetof(V9X_R3D_VERTEX, sx), offsetof(V9X_R3D_VERTEX, sy),
        offsetof(V9X_R3D_VERTEX, sz), offsetof(V9X_R3D_VERTEX, rhw),
        offsetof(V9X_R3D_VERTEX, color), offsetof(V9X_R3D_VERTEX, specular),
        offsetof(V9X_R3D_VERTEX, tu), offsetof(V9X_R3D_VERTEX, tv)
    };
    unsigned int index;

    for (index = 0u; index < 8u; ++index) {
        CLCHECK(offsets[index] == (size_t)index * 4u);
    }
    /* Through the loop variable so the compiler cannot fold the size test
     * to a constant and refuse the branch as unreachable. */
    CLCHECK(offsets[index - 1u] + 4u == sizeof(V9X_R3D_VERTEX));
}

static void test_on_target_edges_included(void)
{
    V9X_R3D_VERTEX tri[3];

    tri[0] = clip_vertex(0.0f, 0.0f, 1.0f, 0xffffffff, 0.0f, 0.0f);
    tri[1] = clip_vertex(CLIP_WIDTH, 0.0f, 1.0f, 0xffffffff, 1.0f, 0.0f);
    tri[2] = clip_vertex(0.0f, CLIP_HEIGHT, 1.0f, 0xffffffff, 0.0f, 1.0f);
    CLCHECK(v9x_r3d_triangle_on_target(tri, CLIP_WIDTH, CLIP_HEIGHT) == 1);

    tri[1].sx = CLIP_WIDTH + 0.5f;
    CLCHECK(v9x_r3d_triangle_on_target(tri, CLIP_WIDTH, CLIP_HEIGHT) == 0);

    tri[1].sx = clip_nan();
    CLCHECK(v9x_r3d_triangle_on_target(tri, CLIP_WIDTH, CLIP_HEIGHT) == 0);
}

static void test_guard_band_and_nan_refuse(void)
{
    V9X_R3D_VERTEX tri[3];
    V9X_R3D_VERTEX out[V9X_R3D_CLIP_MAX_VERTICES];

    tri[0] = clip_vertex(10.0f, 10.0f, 1.0f, 0xffffffff, 0.0f, 0.0f);
    tri[1] = clip_vertex(CLIP_GUARD, 10.0f, 1.0f, 0xffffffff, 1.0f, 0.0f);
    tri[2] = clip_vertex(10.0f, 50.0f, 1.0f, 0xffffffff, 0.0f, 1.0f);
    /* The band is half-open: exactly +limit is outside, -limit is inside. */
    CLCHECK(v9x_r3d_clip_triangle(tri, CLIP_GUARD, CLIP_WIDTH, CLIP_HEIGHT, out) == -1);
    tri[1].sx = -CLIP_GUARD;
    CLCHECK(v9x_r3d_clip_triangle(tri, CLIP_GUARD, CLIP_WIDTH, CLIP_HEIGHT, out) >= 0);

    tri[1].sx = clip_nan();
    CLCHECK(v9x_r3d_clip_triangle(tri, CLIP_GUARD, CLIP_WIDTH, CLIP_HEIGHT, out) == -1);
    tri[1].sx = 100.0f;
    tri[1].sy = clip_nan();
    CLCHECK(v9x_r3d_clip_triangle(tri, CLIP_GUARD, CLIP_WIDTH, CLIP_HEIGHT, out) == -1);
}

static void test_inside_triangle_is_unchanged(void)
{
    V9X_R3D_VERTEX tri[3];
    V9X_R3D_VERTEX out[V9X_R3D_CLIP_MAX_VERTICES];
    int count;

    tri[0] = clip_vertex(10.0f, 10.0f, 1.0f, 0xff112233, 0.0f, 0.0f);
    tri[1] = clip_vertex(100.0f, 20.0f, 0.5f, 0xff445566, 1.0f, 0.0f);
    tri[2] = clip_vertex(30.0f, 90.0f, 2.0f, 0xff778899, 0.0f, 1.0f);
    count = v9x_r3d_clip_triangle(tri, CLIP_GUARD, CLIP_WIDTH, CLIP_HEIGHT, out);
    CLCHECK(count == 3);
    CLCHECK(out[0].sx == 10.0f && out[1].sx == 100.0f && out[2].sy == 90.0f);
    CLCHECK(out[1].color == 0xff445566ul && out[2].rhw == 2.0f);
}

/* A triangle sticking past the right edge is cut exactly at width. */
static void test_cut_at_width_not_last_pixel(void)
{
    V9X_R3D_VERTEX tri[3];
    V9X_R3D_VERTEX out[V9X_R3D_CLIP_MAX_VERTICES];
    int count;
    int index;
    int at_edge = 0;

    tri[0] = clip_vertex(600.0f, 100.0f, 1.0f, 0xff000000, 0.0f, 0.0f);
    tri[1] = clip_vertex(680.0f, 100.0f, 1.0f, 0xff000000, 1.0f, 0.0f);
    tri[2] = clip_vertex(600.0f, 180.0f, 1.0f, 0xff000000, 0.0f, 1.0f);
    count = v9x_r3d_clip_triangle(tri, CLIP_GUARD, CLIP_WIDTH, CLIP_HEIGHT, out);
    CLCHECK(count == 4);
    for (index = 0; index < count; ++index) {
        CLCHECK(out[index].sx <= CLIP_WIDTH);
        if (out[index].sx == CLIP_WIDTH) {
            ++at_edge;
        }
    }
    CLCHECK(at_edge == 2);
}

/* Off every edge at once: the corner of a big triangle leaves a fan of the
 * target's shape. */
static void test_covering_triangle_becomes_target(void)
{
    V9X_R3D_VERTEX tri[3];
    V9X_R3D_VERTEX out[V9X_R3D_CLIP_MAX_VERTICES];
    int count;
    int index;

    /* The hypotenuse is x + y = 1200, past the far corner's 1120, so the
     * whole target is inside and only the four edges cut. */
    tri[0] = clip_vertex(-100.0f, -100.0f, 1.0f, 0xffffffff, 0.0f, 0.0f);
    tri[1] = clip_vertex(1300.0f, -100.0f, 1.0f, 0xffffffff, 1.0f, 0.0f);
    tri[2] = clip_vertex(-100.0f, 1300.0f, 1.0f, 0xffffffff, 0.0f, 1.0f);
    count = v9x_r3d_clip_triangle(tri, CLIP_GUARD, CLIP_WIDTH, CLIP_HEIGHT, out);
    CLCHECK(count == 4);
    for (index = 0; index < count; ++index) {
        CLCHECK(out[index].sx >= 0.0f && out[index].sx <= CLIP_WIDTH);
        CLCHECK(out[index].sy >= 0.0f && out[index].sy <= CLIP_HEIGHT);
    }
}

/* Entirely off the target: nothing is left and nothing is refused. */
static void test_offscreen_triangle_is_empty(void)
{
    V9X_R3D_VERTEX tri[3];
    V9X_R3D_VERTEX out[V9X_R3D_CLIP_MAX_VERTICES];

    tri[0] = clip_vertex(700.0f, 10.0f, 1.0f, 0xffffffff, 0.0f, 0.0f);
    tri[1] = clip_vertex(800.0f, 10.0f, 1.0f, 0xffffffff, 1.0f, 0.0f);
    tri[2] = clip_vertex(700.0f, 90.0f, 1.0f, 0xffffffff, 0.0f, 1.0f);
    CLCHECK(v9x_r3d_clip_triangle(tri, CLIP_GUARD, CLIP_WIDTH, CLIP_HEIGHT, out) == 0);
}

/*
 * The cut vertex: colour and rhw blend linearly; tu blends linearly when
 * rhw is equal at both ends and as (tu*rhw)/rhw when it is not.
 */
static void test_cut_interpolation(void)
{
    V9X_R3D_VERTEX tri[3];
    V9X_R3D_VERTEX out[V9X_R3D_CLIP_MAX_VERTICES];
    int count;
    int index;
    int found = 0;

    /* A horizontal edge from x=560 to x=720 at y=100, cut at 640: halfway.
     * Colour 0x00..0x80 halfway is 0x40, no rounding tie. */
    tri[0] = clip_vertex(560.0f, 100.0f, 1.0f, 0xff000000, 0.0f, 0.0f);
    tri[1] = clip_vertex(720.0f, 100.0f, 1.0f, 0xff808080, 1.0f, 0.0f);
    tri[2] = clip_vertex(560.0f, 200.0f, 1.0f, 0xff000000, 0.0f, 1.0f);
    count = v9x_r3d_clip_triangle(tri, CLIP_GUARD, CLIP_WIDTH, CLIP_HEIGHT, out);
    for (index = 0; index < count; ++index) {
        if (out[index].sx == CLIP_WIDTH && out[index].sy == 100.0f) {
            CLCHECK(out[index].color == 0xff404040ul);
            CLCHECK(out[index].tu == 0.5f);
            found = 1;
        }
    }
    CLCHECK(found);

    /* The same edge with rhw 1 at one end and 3 at the other: the cut's rhw
     * is 2, and tu = ((0*1) + (1*3 - 0) * 0.5) / 2 = 0.75, not 0.5. */
    found = 0;
    tri[1].rhw = 3.0f;
    count = v9x_r3d_clip_triangle(tri, CLIP_GUARD, CLIP_WIDTH, CLIP_HEIGHT, out);
    for (index = 0; index < count; ++index) {
        if (out[index].sx == CLIP_WIDTH && out[index].sy == 100.0f) {
            CLCHECK(out[index].rhw == 2.0f);
            CLCHECK(out[index].tu == 0.75f);
            found = 1;
        }
    }
    CLCHECK(found);
}

/* The list builder's sink: records every batch it is handed. */
typedef struct clip_batch_record {
    const V9X_R3D_VERTEX *vertices;
    v9x_u32 triangles;
} CLIP_BATCH_RECORD;

typedef struct clip_sink {
    CLIP_BATCH_RECORD batches[16];
    unsigned int count;
    unsigned int refuse_index;   /* batch number to decline, or 999 */
    unsigned long cull_mask;     /* bit n: triangle n is "culled" */
    const V9X_R3D_VERTEX *base;
} CLIP_SINK;

static int clip_batch(void *user, const V9X_R3D_VERTEX *vertices,
                      v9x_u32 triangle_count)
{
    CLIP_SINK *sink = (CLIP_SINK *)user;

    if (sink->count < 16u) {
        sink->batches[sink->count].vertices = vertices;
        sink->batches[sink->count].triangles = triangle_count;
    }
    ++sink->count;
    return sink->count - 1u != sink->refuse_index;
}

static int clip_culled(void *user, const V9X_R3D_VERTEX *triangle)
{
    CLIP_SINK *sink = (CLIP_SINK *)user;
    unsigned long n = (unsigned long)(triangle - sink->base) / 3ul;

    return (sink->cull_mask & (1ul << n)) != 0ul;
}

static void clip_list_setup(V9X_R3D_LIST *list, CLIP_SINK *sink,
                            const V9X_R3D_VERTEX *base, v9x_u32 clip_in_core)
{
    unsigned int i;

    for (i = 0u; i < 16u; ++i) {
        sink->batches[i].vertices = 0;
        sink->batches[i].triangles = 0ul;
    }
    sink->count = 0u;
    sink->refuse_index = 999u;
    sink->cull_mask = 0ul;
    sink->base = base;
    list->guard_limit = CLIP_GUARD;
    list->width = CLIP_WIDTH;
    list->height = CLIP_HEIGHT;
    list->clip_in_core = clip_in_core;
    list->batch = clip_batch;
    list->culled = clip_culled;
    list->user = sink;
}

/* Four triangles: inside, crossing the right edge, inside, inside. */
static void clip_fill_list(V9X_R3D_VERTEX *v)
{
    v[0] = clip_vertex(10.0f, 10.0f, 1.0f, 0xffffffff, 0.0f, 0.0f);
    v[1] = clip_vertex(50.0f, 10.0f, 1.0f, 0xffffffff, 1.0f, 0.0f);
    v[2] = clip_vertex(10.0f, 50.0f, 1.0f, 0xffffffff, 0.0f, 1.0f);
    v[3] = clip_vertex(600.0f, 100.0f, 1.0f, 0xffffffff, 0.0f, 0.0f);
    v[4] = clip_vertex(680.0f, 100.0f, 1.0f, 0xffffffff, 1.0f, 0.0f);
    v[5] = clip_vertex(600.0f, 180.0f, 1.0f, 0xffffffff, 0.0f, 1.0f);
    v[6] = clip_vertex(100.0f, 100.0f, 1.0f, 0xffffffff, 0.0f, 0.0f);
    v[7] = clip_vertex(150.0f, 100.0f, 1.0f, 0xffffffff, 1.0f, 0.0f);
    v[8] = clip_vertex(100.0f, 150.0f, 1.0f, 0xffffffff, 0.0f, 1.0f);
    v[9] = clip_vertex(200.0f, 200.0f, 1.0f, 0xffffffff, 0.0f, 0.0f);
    v[10] = clip_vertex(250.0f, 200.0f, 1.0f, 0xffffffff, 1.0f, 0.0f);
    v[11] = clip_vertex(200.0f, 250.0f, 1.0f, 0xffffffff, 0.0f, 1.0f);
}

static void test_list_runs_and_fans(void)
{
    V9X_R3D_VERTEX v[12];
    V9X_R3D_LIST list;
    CLIP_SINK sink;

    clip_fill_list(v);
    clip_list_setup(&list, &sink, v, 1ul);
    CLCHECK(v9x_r3d_draw_list(&list, v, 4ul) == 1);
    /* Run of 1 (a window on the caller's array), the fan of the cut
     * triangle (4 vertices, 2 triangles, a copy), then the run of 2. */
    CLCHECK(sink.count == 3u);
    CLCHECK(sink.batches[0].vertices == v && sink.batches[0].triangles == 1ul);
    CLCHECK(sink.batches[1].triangles == 2ul && sink.batches[1].vertices != v);
    CLCHECK(sink.batches[2].vertices == v + 6 && sink.batches[2].triangles == 2ul);

    /* An engine that clips for itself gets the whole list as one run. */
    clip_list_setup(&list, &sink, v, 0ul);
    CLCHECK(v9x_r3d_draw_list(&list, v, 4ul) == 1);
    CLCHECK(sink.count == 1u);
    CLCHECK(sink.batches[0].vertices == v && sink.batches[0].triangles == 4ul);
}

static void test_list_culled_ends_run_and_is_not_sent(void)
{
    V9X_R3D_VERTEX v[12];
    V9X_R3D_LIST list;
    CLIP_SINK sink;

    clip_fill_list(v);
    /* Cull triangle 2 (the first inside one after the cut). */
    clip_list_setup(&list, &sink, v, 0ul);
    sink.cull_mask = 1ul << 2;
    CLCHECK(v9x_r3d_draw_list(&list, v, 4ul) == 1);
    CLCHECK(sink.count == 2u);
    CLCHECK(sink.batches[0].vertices == v && sink.batches[0].triangles == 2ul);
    CLCHECK(sink.batches[1].vertices == v + 9 && sink.batches[1].triangles == 1ul);

    /* Every triangle culled: nothing is sent and the call still succeeds. */
    clip_list_setup(&list, &sink, v, 1ul);
    sink.cull_mask = 0xful;
    CLCHECK(v9x_r3d_draw_list(&list, v, 4ul) == 1);
    CLCHECK(sink.count == 0u);
}

static void test_list_refusals(void)
{
    V9X_R3D_VERTEX v[12];
    V9X_R3D_LIST list;
    CLIP_SINK sink;

    /* A NaN triangle fails the call; the triangles after it still draw. */
    clip_fill_list(v);
    v[4].sx = clip_nan();
    clip_list_setup(&list, &sink, v, 1ul);
    CLCHECK(v9x_r3d_draw_list(&list, v, 4ul) == 0);
    CLCHECK(sink.count == 2u);
    CLCHECK(sink.batches[0].triangles == 1ul);
    CLCHECK(sink.batches[1].vertices == v + 6 && sink.batches[1].triangles == 2ul);

    /* A sink that declines a batch fails the call; the rest still go. */
    clip_fill_list(v);
    clip_list_setup(&list, &sink, v, 1ul);
    sink.refuse_index = 0u;
    CLCHECK(v9x_r3d_draw_list(&list, v, 4ul) == 0);
    CLCHECK(sink.count == 3u);

    /* An empty list sends nothing and succeeds. */
    clip_list_setup(&list, &sink, v, 1ul);
    CLCHECK(v9x_r3d_draw_list(&list, v, 0ul) == 1);
    CLCHECK(sink.count == 0u);
}

unsigned int v9x_run_r3d_clip_tests(void)
{
    clip_failures = 0u;
    test_vertex_matches_tlvertex();
    test_on_target_edges_included();
    test_guard_band_and_nan_refuse();
    test_inside_triangle_is_unchanged();
    test_cut_at_width_not_last_pixel();
    test_covering_triangle_becomes_target();
    test_offscreen_triangle_is_empty();
    test_cut_interpolation();
    test_list_runs_and_fans();
    test_list_culled_ends_run_and_is_not_sent();
    test_list_refusals();
    if (clip_failures == 0u) {
        puts("PASS: neutral core clipper and list builder");
    }
    return clip_failures;
}
