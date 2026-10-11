/*
 * Tests for Glide vertex conversion (src\glide\glide_vertex.c): the snap
 * bias, the origin, depth from ooz and oow, texture scales, colour and its
 * sources, table and iterated-alpha fog, culling, and lines as triangles.
 * The vertex values are of the kind NFS II SE sent in the census
 * (docs\decisions\2026-10-08-nfs2se-glide-census.md).
 */
#include <stdio.h>
#include <string.h>
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
    setup->textured = 1ul;
    setup->tmu0_w = 0ul;
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

/* A float from its bits, for the values Carmageddon II left in oow. */
static float float_from_bits(v9x_u32 bits)
{
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

/*
 * oow only where something reads it. Carmageddon II's untextured, Z-buffered
 * quads on the netbook (2026-10-11) carried oow of -1.97, 0 and the bytes
 * "ombi" - memory it never wrote - and Gen3 refused each one for its rhw.
 * A Voodoo reads oow for texturing, the W-buffer and table fog, and for
 * nothing else, so where none applies rhw is 1: the same flat
 * interpolation the Voodoo gives.
 */
static void test_oow_unused_is_ignored(void)
{
    V9X_GLIDE_VERTEX_SETUP setup;
    V9X_R3D_ABI_VERTEX out;
    float in[V9X_GLIDE_VERTEX_FLOATS];
    static const v9x_u32 garbage[4] = {
        0xBFFC05B4ul, 0x00000000ul, 0x69626D6Ful, 0x7FC00000ul
    };
    unsigned int i;

    setup_default(&setup);
    setup.textured = 0ul;
    setup.depth_mode = V9X_GLIDE_DEPTH_ZBUFFER;
    for (i = 0u; i < 4u; ++i) {
        vertex_in(in, 10.0f, 20.0f, float_from_bits(garbage[i]));
        in[V9X_GLIDE_VERTEX_OOZ] = 32767.5f;
        v9x_glide_vertex_convert(&setup, in, &out);
        VCHECK(out.rhw == 1.0f);
        VCHECK(approx(out.sz, 0.5f));
        VCHECK(out.tu == 0.0f && out.tv == 0.0f);
    }

    /* Each reader of oow keeps it. */
    vertex_in(in, 10.0f, 20.0f, 0.5f);
    setup.textured = 1ul;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.rhw, 0.5f));

    setup.textured = 0ul;
    setup.depth_mode = V9X_GLIDE_DEPTH_WBUFFER;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.rhw, 0.5f));
    VCHECK(approx(out.sz, 0.5f));

    setup.depth_mode = V9X_GLIDE_DEPTH_ZBUFFER;
    setup.fog_mode = V9X_GLIDE_FOG_TABLE;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.rhw, 0.5f));

    /* Iterated-alpha fog reads the vertex alpha, not oow. */
    setup.fog_mode = V9X_GLIDE_FOG_ITERATED_ALPHA;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(out.rhw == 1.0f);
}

/*
 * grHints(GR_HINT_STWHINT, GR_STWHINT_W_DIFF_TMU0): the texture's W is the
 * TMU's own oow, the vertex's twelfth float. Carmageddon II's menu text
 * left the vertex oow unwritten (netbook, 2026-10-11: 0x0044C7F7, the bytes
 * "fnuc", 0x0000000A) while texturing; without the hint the TMU's oow is
 * not read, since NFS II SE leaves it unwritten instead.
 */
static void test_tmu_w_hint(void)
{
    V9X_GLIDE_VERTEX_SETUP setup;
    V9X_R3D_ABI_VERTEX out;
    float in[V9X_GLIDE_VERTEX_FLOATS];

    setup_default(&setup);
    setup.depth_mode = V9X_GLIDE_DEPTH_ZBUFFER;
    vertex_in(in, 10.0f, 20.0f, float_from_bits(0x636E7566ul));
    in[V9X_GLIDE_VERTEX_TMU0_OOW] = 0.5f;
    in[V9X_GLIDE_VERTEX_SOW] = 64.0f;   /* s = 128 */
    in[V9X_GLIDE_VERTEX_TOW] = 32.0f;   /* t = 64 */

    setup.tmu0_w = 1ul;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.rhw, 0.5f));
    VCHECK(approx(out.tu, 0.5f) && approx(out.tv, 0.25f));

    /* Without the hint the vertex's oow is the texture's, as before. */
    setup.tmu0_w = 0ul;
    in[V9X_GLIDE_VERTEX_OOW] = 0.25f;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.rhw, 0.25f));
    VCHECK(approx(out.tu, 1.0f));

    /* The W-buffer reads the vertex's oow whatever the hint says. */
    setup.tmu0_w = 1ul;
    setup.depth_mode = V9X_GLIDE_DEPTH_WBUFFER;
    v9x_glide_vertex_convert(&setup, in, &out);
    VCHECK(approx(out.sz, 0.75f));
    VCHECK(approx(out.rhw, 0.5f));
}

/* grConstantColorValue4's four floats, 0..255 each, as ARGB. Carmageddon
 * II sets its constant colour this way, and the menu text's alpha is that
 * constant times the glyph's (netbook, 2026-10-11). */
static void test_argb_from_floats(void)
{
    VCHECK(v9x_glide_argb_from_floats(255.0f, 17.0f, 34.0f, 51.0f) ==
           0xFF112233ul);
    VCHECK(v9x_glide_argb_from_floats(127.6f, 0.0f, 0.0f, 0.0f) ==
           0x80000000ul);
    /* Out of range clamps rather than wraps. */
    VCHECK(v9x_glide_argb_from_floats(300.0f, -5.0f, 255.0f, 0.0f) ==
           0xFF00FF00ul);
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

    /* X-major: the strip runs from y to y + 1, so the row a line through
     * integer y names is lit whether the engine samples pixels at their
     * corner or their centre. A strip of y +- 0.5 lit nothing at y = 400
     * on Gen3 (V9XGLIDP, netbook boot 127). */
    v9x_glide_line_triangles(&a, &b, out);
    VCHECK(approx(out[0].sx, 0.0f) && approx(out[0].sy, 0.0f));
    VCHECK(approx(out[1].sx, 0.0f) && approx(out[1].sy, 1.0f));
    VCHECK(approx(out[2].sx, 10.0f) && approx(out[2].sy, 3.0f));
    VCHECK(approx(out[3].sx, 0.0f) && approx(out[3].sy, 0.0f));
    VCHECK(approx(out[4].sx, 10.0f) && approx(out[4].sy, 3.0f));
    VCHECK(approx(out[5].sx, 10.0f) && approx(out[5].sy, 2.0f));
    VCHECK(out[0].color == 0xFF101010ul && out[2].color == 0xFF202020ul);

    /* Y-major: from x to x + 1. */
    b.sx = 2.0f; b.sy = 10.0f;
    v9x_glide_line_triangles(&a, &b, out);
    VCHECK(approx(out[0].sx, 0.0f) && approx(out[0].sy, 0.0f));
    VCHECK(approx(out[1].sx, 1.0f) && approx(out[1].sy, 0.0f));
    VCHECK(approx(out[2].sx, 3.0f) && approx(out[2].sy, 10.0f));
    VCHECK(approx(out[5].sx, 2.0f) && approx(out[5].sy, 10.0f));
}

static float tri_area(const V9X_R3D_ABI_VERTEX *t)
{
    float area = ((t[1].sx - t[0].sx) * (t[2].sy - t[0].sy) -
                  (t[2].sx - t[0].sx) * (t[1].sy - t[0].sy)) * 0.5f;

    return area < 0.0f ? -area : area;
}

static void clip_vertex(V9X_R3D_ABI_VERTEX *v, float x, float y, float rhw,
                        float tu, v9x_u32 color)
{
    v->sx = x;
    v->sy = y;
    v->sz = 0.5f;
    v->rhw = rhw;
    v->color = color;
    v->specular = 0xFF000000ul;
    v->tu = tu;
    v->tv = 0.0f;
}

/* NFS II SE's HUD panes are clip windows (census), and Gen3 refuses a
 * draw with a partial scissor (netbook, 2026-10-09). */
static void test_clip_rect(void)
{
    V9X_R3D_ABI_VERTEX tri[3];
    V9X_R3D_ABI_VERTEX out[3u * V9X_GLIDE_CLIP_TRIANGLES_MAX];
    unsigned int count;
    unsigned int i;
    float area = 0.0f;
    int inside = 1;

    clip_vertex(&tri[0], 0.0f, 0.0f, 1.0f, 0.0f, 0xFF000000ul);
    clip_vertex(&tri[1], 10.0f, 0.0f, 1.0f, 1.0f, 0xFF0000FFul);
    clip_vertex(&tri[2], 0.0f, 10.0f, 1.0f, 0.0f, 0xFF000000ul);

    /* Wholly inside: unchanged, one triangle. */
    count = v9x_glide_clip_rect(tri, 0.0f, 0.0f, 640.0f, 480.0f, out);
    VCHECK(count == 1u);
    VCHECK(count == 1u && approx(out[1].sx, 10.0f) && out[1].color == 0xFF0000FFul);

    /* Wholly outside: nothing. */
    VCHECK(v9x_glide_clip_rect(tri, 20.0f, 0.0f, 40.0f, 10.0f, out) == 0u);

    /* Cut at x = 5: the part left of it, area 37.5, all inside. */
    count = v9x_glide_clip_rect(tri, 0.0f, 0.0f, 5.0f, 10.0f, out);
    VCHECK(count >= 2u && count <= V9X_GLIDE_CLIP_TRIANGLES_MAX);
    for (i = 0u; i < count * 3u; ++i) {
        if (out[i].sx < -0.001f || out[i].sx > 5.001f ||
            out[i].sy < -0.001f || out[i].sy > 10.001f) {
            inside = 0;
        }
        /* At x = 5 on the top edge, halfway along a with rhw 1 throughout:
         * tu 0.5 and blue half way. */
        if (approx(out[i].sx, 5.0f) && approx(out[i].sy, 0.0f)) {
            VCHECK(approx(out[i].tu, 0.5f));
            VCHECK(out[i].color == 0xFF000080ul || out[i].color == 0xFF00007Ful);
        }
    }
    for (i = 0u; i < count; ++i) {
        area += tri_area(&out[i * 3u]);
    }
    VCHECK(inside);
    VCHECK(approx(area, 37.5f));

    /* Perspective: from rhw 1 (tu 0) to rhw 0.25 (tu 1), the screen-space
     * midpoint has rhw 0.625 and tu = (0.5 * 0.25) / 0.625 = 0.2. */
    clip_vertex(&tri[0], 0.0f, 0.0f, 1.0f, 0.0f, 0xFF000000ul);
    clip_vertex(&tri[1], 10.0f, 0.0f, 0.25f, 1.0f, 0xFF000000ul);
    clip_vertex(&tri[2], 0.0f, 10.0f, 1.0f, 0.0f, 0xFF000000ul);
    count = v9x_glide_clip_rect(tri, 0.0f, 0.0f, 5.0f, 10.0f, out);
    VCHECK(count >= 2u);
    for (i = 0u; i < count * 3u; ++i) {
        if (approx(out[i].sx, 5.0f) && approx(out[i].sy, 0.0f)) {
            VCHECK(approx(out[i].rhw, 0.625f));
            VCHECK(approx(out[i].tu, 0.2f));
        }
    }
}

unsigned int v9x_run_glide_vertex_tests(void)
{
    glide_vertex_failures = 0u;
    test_position();
    test_depth();
    test_oow_unused_is_ignored();
    test_tmu_w_hint();
    test_argb_from_floats();
    test_texture_coordinates();
    test_color();
    test_fog();
    test_cull();
    test_line();
    test_clip_rect();
    return glide_vertex_failures;
}
