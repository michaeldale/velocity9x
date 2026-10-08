/*
 * Tests for Glide vertex conversion (src\glide\glide_vertex.c): the snap
 * bias, the origin, depth from ooz and oow, texture scales, colour and its
 * sources, table and iterated-alpha fog, culling, and lines as triangles.
 * The vertex values are of the kind NFS II SE sent in the census
 * (docs\decisions\2026-10-08-nfs2se-glide-census.md).
 */
#include <stdio.h>
#include "../../src/glide/glide_vertex.h"

static unsigned int glide_vertex_failures;

#define VCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++glide_vertex_failures; \
    } \
} while (0)

static int approx(float value, float expected)
{
    float difference = value - expected;
    float scale = expected < 0.0f ? -expected : expected;

    if (difference < 0.0f) {
        difference = -difference;
    }
    return difference <= 0.0001f * (scale > 1.0f ? scale : 1.0f);
}

static void setup_default(V9X_GLIDE_VERTEX_SETUP *setup)
{
    setup->origin = V9X_GLIDE_ORIGIN_UPPER_LEFT;
    setup->height = 480.0f;
    setup->depth_mode = V9X_GLIDE_DEPTH_WBUFFER;
    setup->s_scale = 1.0f / 256.0f;
    setup->t_scale = 1.0f / 256.0f;
    setup->color_source = V9X_GLIDE_SOURCE_ITERATED;
    setup->alpha_source = V9X_GLIDE_SOURCE_ITERATED;
    setup->constant_argb = 0ul;
    setup->fog_mode = V9X_GLIDE_FOG_DISABLE;
    setup->fog_table = 0;
}

static void vertex_in(float *in, float x, float y, float oow)
{
    unsigned int i;

    for (i = 0u; i < V9X_GLIDE_VERTEX_FLOATS; ++i) {
        in[i] = 0.0f;
    }
    in[V9X_GLIDE_VERTEX_X] = x;
    in[V9X_GLIDE_VERTEX_Y] = y;
    in[V9X_GLIDE_VERTEX_OOW] = oow;
    in[V9X_GLIDE_VERTEX_R] = 255.0f;
    in[V9X_GLIDE_VERTEX_G] = 255.0f;
    in[V9X_GLIDE_VERTEX_B] = 255.0f;
    in[V9X_GLIDE_VERTEX_A] = 255.0f;
}

static void test_position(void)
{
    V9X_GLIDE_VERTEX_SETUP setup;
    V9X_R3D_ABI_VERTEX out;
    float in[V9X_GLIDE_VERTEX_FLOATS];

    setup_default(&setup);

    /* NFS II SE's snapped coordinates: 3 << 18 plus the screen position. */
    vertex_in(in, 786432.0f + 100.5f, 786432.0f + 20.0f, 1.0f);
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.sx, 100.5f));
    VCHECK(approx(out.sy, 20.0f));

    /* An unsnapped coordinate passes through. */
    vertex_in(in, 100.0f, 20.0f, 1.0f);
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.sx, 100.0f));
    VCHECK(approx(out.sy, 20.0f));

    /* The lower-left origin counts lines up from the bottom. */
    setup.origin = V9X_GLIDE_ORIGIN_LOWER_LEFT;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.sy, 460.0f));
}

static void test_depth(void)
{
    V9X_GLIDE_VERTEX_SETUP setup;
    V9X_R3D_ABI_VERTEX out;
    float in[V9X_GLIDE_VERTEX_FLOATS];

    setup_default(&setup);

    /* W-buffer: depth grows with W, 1 - oow, and rhw is oow. */
    vertex_in(in, 0.0f, 0.0f, 0.25f);
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.sz, 0.75f));
    VCHECK(approx(out.rhw, 0.25f));

    /* Census oow reached 0.999985; above 1 is nearer than the near plane
     * and clamps to 0. */
    vertex_in(in, 0.0f, 0.0f, 2.0f);
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.sz, 0.0f));

    /* Z-buffer: ooz over 65535. */
    setup.depth_mode = V9X_GLIDE_DEPTH_ZBUFFER;
    vertex_in(in, 0.0f, 0.0f, 1.0f);
    in[V9X_GLIDE_VERTEX_OOZ] = 65535.0f;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.sz, 1.0f));
    in[V9X_GLIDE_VERTEX_OOZ] = 32767.5f;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.sz, 0.5f));

    setup.depth_mode = V9X_GLIDE_DEPTH_DISABLE;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.sz, 0.0f));
}

static void test_texture_coordinates(void)
{
    V9X_GLIDE_VERTEX_SETUP setup;
    V9X_R3D_ABI_VERTEX out;
    float in[V9X_GLIDE_VERTEX_FLOATS];
    float s_scale = 0.0f;
    float t_scale = 0.0f;

    VCHECK(v9x_glide_texture_scales(V9X_GLIDE_ASPECT_1X1, &s_scale, &t_scale));
    VCHECK(approx(s_scale, 1.0f / 256.0f) && approx(t_scale, 1.0f / 256.0f));
    /* 2x1 (aspect 2): t runs 0..128 across the shorter side. */
    VCHECK(v9x_glide_texture_scales(2ul, &s_scale, &t_scale));
    VCHECK(approx(s_scale, 1.0f / 256.0f) && approx(t_scale, 1.0f / 128.0f));
    /* 1x8 (aspect 6): s runs 0..32. */
    VCHECK(v9x_glide_texture_scales(V9X_GLIDE_ASPECT_1X8, &s_scale, &t_scale));
    VCHECK(approx(s_scale, 1.0f / 32.0f) && approx(t_scale, 1.0f / 256.0f));
    VCHECK(!v9x_glide_texture_scales(7ul, &s_scale, &t_scale));

    /* sow and tow are s/w and t/w: s = sow / oow. */
    setup_default(&setup);
    vertex_in(in, 0.0f, 0.0f, 0.5f);
    in[V9X_GLIDE_VERTEX_SOW] = 64.0f;   /* s = 128 */
    in[V9X_GLIDE_VERTEX_TOW] = 127.5f;  /* t = 255 */
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.tu, 0.5f));
    VCHECK(approx(out.tv, 255.0f / 256.0f));
}

static void test_color(void)
{
    V9X_GLIDE_VERTEX_SETUP setup;
    V9X_R3D_ABI_VERTEX out;
    float in[V9X_GLIDE_VERTEX_FLOATS];

    setup_default(&setup);
    vertex_in(in, 0.0f, 0.0f, 1.0f);
    in[V9X_GLIDE_VERTEX_R] = 255.0f;
    in[V9X_GLIDE_VERTEX_G] = 127.5f;    /* rounds up */
    in[V9X_GLIDE_VERTEX_B] = 0.0f;
    in[V9X_GLIDE_VERTEX_A] = 64.0f;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(out.color == 0x40FF8000ul);

    in[V9X_GLIDE_VERTEX_R] = 300.0f;    /* clamped */
    in[V9X_GLIDE_VERTEX_G] = -5.0f;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(out.color == 0x40FF0000ul);

    /* A constant colour replaces the iterated RGB; alpha keeps its own
     * source. */
    setup.color_source = V9X_GLIDE_SOURCE_CONSTANT;
    setup.constant_argb = 0x80112233ul;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(out.color == 0x40112233ul);
    setup.alpha_source = V9X_GLIDE_SOURCE_CONSTANT;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(out.color == 0x80112233ul);
}

static void test_fog(void)
{
    V9X_GLIDE_VERTEX_SETUP setup;
    V9X_R3D_ABI_VERTEX out;
    float in[V9X_GLIDE_VERTEX_FLOATS];
    v9x_u8 table[V9X_GLIDE_FOG_TABLE_SIZE];
    unsigned int i;

    VCHECK(approx(v9x_glide_fog_index_to_w(0u), 1.0f));
    VCHECK(approx(v9x_glide_fog_index_to_w(1u), 8.0f / 7.0f));
    VCHECK(approx(v9x_glide_fog_index_to_w(4u), 2.0f));
    VCHECK(approx(v9x_glide_fog_index_to_w(63u), 262144.0f / 5.0f));

    for (i = 0u; i < V9X_GLIDE_FOG_TABLE_SIZE; ++i) {
        table[i] = (v9x_u8)(i * 4u);
    }
    VCHECK(v9x_glide_fog_amount(table, 2.0f) == 16ul);
    /* Halfway between entries 4 (W 2) and 5 (W 16/7). */
    VCHECK(v9x_glide_fog_amount(table, (2.0f + 16.0f / 7.0f) / 2.0f) == 18ul);
    VCHECK(v9x_glide_fog_amount(table, 0.5f) == 0ul);
    VCHECK(v9x_glide_fog_amount(table, 100000.0f) == 252ul);

    /* Specular alpha carries 255 minus the fog amount; RGB is zero. */
    setup_default(&setup);
    vertex_in(in, 0.0f, 0.0f, 0.5f);
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(out.specular == 0xFF000000ul);

    setup.fog_mode = V9X_GLIDE_FOG_TABLE;
    setup.fog_table = table;
    v9x_glide_vertex_convert(&setup, in, &out);  /* W 2: amount 16 */
    VCHECK(out.specular == 0xEF000000ul);

    setup.fog_mode = V9X_GLIDE_FOG_ITERATED_ALPHA;
    in[V9X_GLIDE_VERTEX_A] = 64.0f;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(out.specular == 0xBF000000ul);
}

static void test_cull(void)
{
    V9X_R3D_ABI_VERTEX a;
    V9X_R3D_ABI_VERTEX b;
    V9X_R3D_ABI_VERTEX c;

    a.sx = 0.0f;  a.sy = 0.0f;
    b.sx = 10.0f; b.sy = 0.0f;
    c.sx = 0.0f;  c.sy = 10.0f;
    /* (b - a) x (c - a) = +100 in the given coordinates. */
    VCHECK(v9x_glide_cull_keep(V9X_GLIDE_CULL_DISABLE,
                               V9X_GLIDE_ORIGIN_UPPER_LEFT, &a, &b, &c));
    VCHECK(v9x_glide_cull_keep(V9X_GLIDE_CULL_NEGATIVE,
                               V9X_GLIDE_ORIGIN_UPPER_LEFT, &a, &b, &c));
    VCHECK(!v9x_glide_cull_keep(V9X_GLIDE_CULL_POSITIVE,
                                V9X_GLIDE_ORIGIN_UPPER_LEFT, &a, &b, &c));
    /* The same converted triangle under a lower-left origin had the
     * opposite area in Glide's coordinates. */
    VCHECK(!v9x_glide_cull_keep(V9X_GLIDE_CULL_NEGATIVE,
                                V9X_GLIDE_ORIGIN_LOWER_LEFT, &a, &b, &c));
    VCHECK(v9x_glide_cull_keep(V9X_GLIDE_CULL_POSITIVE,
                               V9X_GLIDE_ORIGIN_LOWER_LEFT, &a, &b, &c));
}

static void test_line(void)
{
    V9X_R3D_ABI_VERTEX a;
    V9X_R3D_ABI_VERTEX b;
    V9X_R3D_ABI_VERTEX out[6];

    a.sx = 0.0f;  a.sy = 0.0f;  a.sz = 0.0f; a.rhw = 1.0f;
    a.color = 0xFF101010ul; a.specular = 0xFF000000ul; a.tu = 0.0f; a.tv = 0.0f;
    b = a;
    b.sx = 10.0f; b.sy = 2.0f;  b.color = 0xFF202020ul;

    /* X-major: widened by half a pixel up and down. */
    v9x_glide_line_triangles(&a, &b, out);
    VCHECK(approx(out[0].sx, 0.0f) && approx(out[0].sy, -0.5f));
    VCHECK(approx(out[1].sx, 0.0f) && approx(out[1].sy, 0.5f));
    VCHECK(approx(out[2].sx, 10.0f) && approx(out[2].sy, 2.5f));
    VCHECK(approx(out[3].sx, 0.0f) && approx(out[3].sy, -0.5f));
    VCHECK(approx(out[4].sx, 10.0f) && approx(out[4].sy, 2.5f));
    VCHECK(approx(out[5].sx, 10.0f) && approx(out[5].sy, 1.5f));
    VCHECK(out[0].color == 0xFF101010ul && out[2].color == 0xFF202020ul);

    /* Y-major: widened left and right. */
    b.sx = 2.0f; b.sy = 10.0f;
    v9x_glide_line_triangles(&a, &b, out);
    VCHECK(approx(out[0].sx, -0.5f) && approx(out[0].sy, 0.0f));
    VCHECK(approx(out[1].sx, 0.5f) && approx(out[1].sy, 0.0f));
    VCHECK(approx(out[2].sx, 2.5f) && approx(out[2].sy, 10.0f));
    VCHECK(approx(out[5].sx, 1.5f) && approx(out[5].sy, 10.0f));
}

unsigned int v9x_run_glide_vertex_tests(void)
{
    glide_vertex_failures = 0u;
    test_position();
    test_depth();
    test_texture_coordinates();
    test_color();
    test_fog();
    test_cull();
    test_line();
    return glide_vertex_failures;
}
