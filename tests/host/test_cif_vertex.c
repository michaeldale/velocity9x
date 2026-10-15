/*
 * Tests for ATI3DCIF vertex and primitive conversion
 * (src\cif\cif_vertex.c): position and the viewport origin, depth in
 * 16-bit Z units, s and t divided by w, colour, solid and flat shading,
 * fog in specular alpha, and strips and lists as triangles. The vertex
 * values are the census's (docs\decisions\2026-10-14-3dcif-demo-census.md,
 * decoded from analysis.txt).
 */
#include <stdio.h>
#include "../../src/cif/cif_vertex.h"

static unsigned int cif_vertex_failures;

#define VCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++cif_vertex_failures; \
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

static void setup_default(V9X_CIF_VERTEX_SETUP *setup)
{
    setup->origin_x = 0.0f;
    setup->origin_y = 0.0f;
    setup->shade = V9X_C3D_ESH_SMOOTH;
    setup->solid_argb = 0ul;
    setup->fog = 0ul;
    setup->white = 0ul;
}

static void vtcf(float *in, float x, float y, float z, float s, float t,
                 float w, float r, float g, float b, float a)
{
    in[V9X_C3D_VTCF_X] = x;
    in[V9X_C3D_VTCF_Y] = y;
    in[V9X_C3D_VTCF_Z] = z;
    in[V9X_C3D_VTCF_S] = s;
    in[V9X_C3D_VTCF_T] = t;
    in[V9X_C3D_VTCF_W] = w;
    in[V9X_C3D_VTCF_R] = r;
    in[V9X_C3D_VTCF_G] = g;
    in[V9X_C3D_VTCF_B] = b;
    in[V9X_C3D_VTCF_A] = a;
}

static void test_position(void)
{
    V9X_CIF_VERTEX_SETUP setup;
    V9X_R3D_ABI_VERTEX out;
    float in[V9X_C3D_VTCF_FLOATS];

    setup_default(&setup);

    /* Cubes' first vertex: pixels from the surface's top-left. */
    vtcf(in, 389.94f, 170.06f, -0.8903f, 0.0f, 0.0f, 2.3529f,
         192.7f, 192.7f, 192.7f, 0.0f);
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(approx(out.sx, 389.94f));
    VCHECK(approx(out.sy, 170.06f));

    /* A viewport at (20, 10) moves the origin. */
    setup.origin_x = 20.0f;
    setup.origin_y = 10.0f;
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(approx(out.sx, 409.94f));
    VCHECK(approx(out.sy, 180.06f));
}

static void test_depth(void)
{
    V9X_CIF_VERTEX_SETUP setup;
    V9X_R3D_ABI_VERTEX out;
    float in[V9X_C3D_VTCF_FLOATS];

    setup_default(&setup);

    /* Z-buffer units, 0 to 65535, scaled to 0..1. */
    vtcf(in, 0.0f, 0.0f, 65535.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(approx(out.sz, 1.0f));

    /* Laputa's sprite layers, 1 and 2, stay ordered and near 0. */
    in[V9X_C3D_VTCF_Z] = 1.0f;
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(out.sz > 0.0f && out.sz < 0.0001f);
    in[V9X_C3D_VTCF_Z] = 2.0f;
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(approx(out.sz, 2.0f / 65535.0f));

    /* The Knight sky, near the far end. */
    in[V9X_C3D_VTCF_Z] = 65531.1719f;
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(out.sz > 0.9999f && out.sz < 1.0f);

    /* Cubes sends -0.89 with Z off; beyond either end clamps. */
    in[V9X_C3D_VTCF_Z] = -0.8903f;
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(out.sz == 0.0f);
    in[V9X_C3D_VTCF_Z] = 70000.0f;
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(out.sz == 1.0f);
}

static void test_texture_coordinates(void)
{
    V9X_CIF_VERTEX_SETUP setup;
    V9X_R3D_ABI_VERTEX out;
    float in[V9X_C3D_VTCF_FLOATS];

    setup_default(&setup);

    /* ATi-3D's corner: s and t arrive multiplied by w, which is 1/w. */
    vtcf(in, 414.94f, 135.45f, -0.9712f, 1.3642f, 0.0095f, 1.3548f,
         38.4f, 38.4f, 38.4f, 0.0f);
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(approx(out.rhw, 1.3548f));
    VCHECK(approx(out.tu, 1.3642f / 1.3548f));
    VCHECK(approx(out.tv, 0.0095f / 1.3548f));

    /* Laputa: w is 1, s passes through, negative included. */
    vtcf(in, 231.60f, 193.04f, 1.0f, -0.8655f, 0.5009f, 1.0f,
         255.0f, 255.0f, 255.0f, 255.0f);
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(approx(out.rhw, 1.0f));
    VCHECK(approx(out.tu, -0.8655f));
    VCHECK(approx(out.tv, 0.5009f));

    /* A degenerate w of 0 gives finite coordinates, not a division by 0. */
    in[V9X_C3D_VTCF_W] = 0.0f;
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(out.rhw > 0.0f);
    VCHECK(approx(out.tu, -0.8655f));
}

static void test_color(void)
{
    V9X_CIF_VERTEX_SETUP setup;
    V9X_R3D_ABI_VERTEX out;
    float in[V9X_C3D_VTCF_FLOATS];

    setup_default(&setup);

    /* Cubes: 0..255 floats, rounded; alpha 0 kept. */
    vtcf(in, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
         192.7f, 179.5f, 175.7f, 0.0f);
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(out.color == 0x00c1b4b0ul);
    VCHECK(out.specular == 0xff000000ul);

    /* Out of range clamps: the Knight demos send about -1.9. */
    vtcf(in, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 300.0f, -1.9f, 255.0f,
         255.0f);
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(out.color == 0xffff00fful);

    /* Solid shading takes the context's colour whatever the vertex says. */
    setup.shade = V9X_C3D_ESH_SOLID;
    setup.solid_argb = 0xffffff00ul;
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(out.color == 0xffffff00ul);

    /* White: the colour forced to white, the alpha kept, for an unlit
     * texture drawn as MODULATE (cif_state.h). */
    setup.shade = V9X_C3D_ESH_SMOOTH;
    setup.white = 1ul;
    vtcf(in, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 10.0f, 20.0f, 30.0f, 64.0f);
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(out.color == 0x40fffffful);
}

static void test_fog(void)
{
    V9X_CIF_VERTEX_SETUP setup;
    V9X_R3D_ABI_VERTEX out;
    float in[V9X_C3D_VTCF_FLOATS];

    setup_default(&setup);
    setup.fog = 1ul;

    /* With fog on, the vertex alpha is the fog factor (255 none) and the
     * colour is opaque: fog and alpha blending exclude each other. */
    vtcf(in, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 10.0f, 20.0f, 30.0f, 128.0f);
    v9x_cif_vtcf_convert(&setup, in, &out);
    VCHECK(out.specular == 0x80000000ul);
    VCHECK(out.color == 0xff0a141eul);
}

static void test_flat(void)
{
    V9X_R3D_ABI_VERTEX triangle[3];

    triangle[0].color = 0xff000000ul;
    triangle[1].color = 0xff00ff00ul;
    triangle[2].color = 0x80123456ul;
    triangle[0].specular = 0x10000000ul;
    triangle[1].specular = 0x20000000ul;
    triangle[2].specular = 0x30000000ul;
    v9x_cif_triangle_flat(triangle);
    VCHECK(triangle[0].color == 0x80123456ul);
    VCHECK(triangle[1].color == 0x80123456ul);
    VCHECK(triangle[2].color == 0x80123456ul);
    VCHECK(triangle[0].specular == 0x10000000ul);
    VCHECK(triangle[1].specular == 0x20000000ul);
}

static void test_primitives(void)
{
    /* Triangles: lists of 3, 6, 12 and 18 and strips of 3 in the census. */
    VCHECK(v9x_cif_prim_triangles(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_LIST, 6ul) == 2ul);
    VCHECK(v9x_cif_prim_triangles(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_LIST, 7ul) == 2ul);
    VCHECK(v9x_cif_prim_triangles(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_STRIP, 3ul) == 1ul);
    VCHECK(v9x_cif_prim_triangles(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_STRIP, 5ul) == 3ul);
    VCHECK(v9x_cif_prim_triangles(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_STRIP, 2ul) == 0ul);
    VCHECK(v9x_cif_prim_triangles(V9X_C3D_EPRIM_QUAD, V9X_CIF_FORM_LIST, 8ul) == 4ul);
    VCHECK(v9x_cif_prim_triangles(V9X_C3D_EPRIM_QUAD, V9X_CIF_FORM_STRIP, 8ul) == 0ul);
    VCHECK(v9x_cif_prim_triangles(V9X_C3D_EPRIM_POINT, V9X_CIF_FORM_LIST, 6ul) == 0ul);

    /* Lines: two triangles each. A list takes pairs, a strip a polyline.
     * Assault Rigs draws its menus' text with line lists (A8U4I5,
     * 2026-10-16: 50,097 skipped). */
    VCHECK(v9x_cif_prim_triangles(V9X_C3D_EPRIM_LINE, V9X_CIF_FORM_LIST, 6ul) == 6ul);
    VCHECK(v9x_cif_prim_triangles(V9X_C3D_EPRIM_LINE, V9X_CIF_FORM_LIST, 5ul) == 4ul);
    VCHECK(v9x_cif_prim_triangles(V9X_C3D_EPRIM_LINE, V9X_CIF_FORM_LIST, 1ul) == 0ul);
    VCHECK(v9x_cif_prim_triangles(V9X_C3D_EPRIM_LINE, V9X_CIF_FORM_STRIP, 4ul) == 6ul);
    VCHECK(v9x_cif_prim_triangles(V9X_C3D_EPRIM_LINE, V9X_CIF_FORM_STRIP, 1ul) == 0ul);

    /* Each line is a, a, b then a, b, b; the widening offsets the second
     * copies. List line 1 is vertices 2 and 3; strip line 1 is 1 and 2. */
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_LINE, V9X_CIF_FORM_LIST, 2ul, 0ul) == 2ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_LINE, V9X_CIF_FORM_LIST, 2ul, 1ul) == 2ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_LINE, V9X_CIF_FORM_LIST, 2ul, 2ul) == 3ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_LINE, V9X_CIF_FORM_LIST, 3ul, 0ul) == 2ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_LINE, V9X_CIF_FORM_LIST, 3ul, 1ul) == 3ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_LINE, V9X_CIF_FORM_LIST, 3ul, 2ul) == 3ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_LINE, V9X_CIF_FORM_STRIP, 2ul, 0ul) == 1ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_LINE, V9X_CIF_FORM_STRIP, 3ul, 2ul) == 2ul);

    /* A list's second triangle is vertices 3, 4, 5. */
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_LIST, 1ul, 0ul) == 3ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_LIST, 1ul, 2ul) == 5ul);

    /* A strip keeps the first triangle's winding: 0 1 2, then 2 1 3. */
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_STRIP, 0ul, 0ul) == 0ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_STRIP, 0ul, 2ul) == 2ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_STRIP, 1ul, 0ul) == 2ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_STRIP, 1ul, 1ul) == 1ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_STRIP, 1ul, 2ul) == 3ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_STRIP, 2ul, 0ul) == 2ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_TRI, V9X_CIF_FORM_STRIP, 2ul, 2ul) == 4ul);

    /* A quadrangle is the fan 0-1-2, 0-2-3. */
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_QUAD, V9X_CIF_FORM_LIST, 0ul, 2ul) == 2ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_QUAD, V9X_CIF_FORM_LIST, 1ul, 0ul) == 0ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_QUAD, V9X_CIF_FORM_LIST, 1ul, 1ul) == 2ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_QUAD, V9X_CIF_FORM_LIST, 1ul, 2ul) == 3ul);
    VCHECK(v9x_cif_prim_vertex(V9X_C3D_EPRIM_QUAD, V9X_CIF_FORM_LIST, 3ul, 2ul) == 7ul);
}

/* Whether a batch's colours are all fully opaque: then texel alpha times
 * vertex alpha is the texel's alone, which the Rage IIC can blend by
 * (docs\issues\2026-10-03-rage-iic-texel-times-vertex-alpha-refused.md). */
static void test_opaque(void)
{
    V9X_R3D_ABI_VERTEX batch[6];
    unsigned int i;

    for (i = 0u; i < 6u; ++i) {
        batch[i].color = 0xff123456ul;
    }
    VCHECK(v9x_cif_vertices_opaque(batch, 2ul) == V9X_TRUE);
    batch[4].color = 0xfe123456ul;
    VCHECK(v9x_cif_vertices_opaque(batch, 2ul) == V9X_FALSE);
    VCHECK(v9x_cif_vertices_opaque(batch, 1ul) == V9X_TRUE);
}

/* A line's triangles widened one pixel across the minor axis, toward the
 * next whole pixel, as the Glide wrapper draws grDrawLine
 * (glide_vertex.c, v9x_glide_line_triangles). */
static void set_xy(V9X_R3D_ABI_VERTEX *v, float x, float y)
{
    v->sx = x;
    v->sy = y;
}

static void test_line_widen(void)
{
    V9X_R3D_ABI_VERTEX t[3];

    /* Mostly horizontal, (10,20) to (30,25): y + 1 on the copies. */
    set_xy(&t[0], 10.0f, 20.0f);
    set_xy(&t[1], 10.0f, 20.0f);
    set_xy(&t[2], 30.0f, 25.0f);
    v9x_cif_line_widen(t, 0ul);
    VCHECK(approx(t[0].sx, 10.0f) && approx(t[0].sy, 20.0f));
    VCHECK(approx(t[1].sx, 10.0f) && approx(t[1].sy, 21.0f));
    VCHECK(approx(t[2].sx, 30.0f) && approx(t[2].sy, 26.0f));

    /* The second triangle, a, b, b: only the middle corner moves. */
    set_xy(&t[0], 10.0f, 20.0f);
    set_xy(&t[1], 30.0f, 25.0f);
    set_xy(&t[2], 30.0f, 25.0f);
    v9x_cif_line_widen(t, 1ul);
    VCHECK(approx(t[0].sy, 20.0f));
    VCHECK(approx(t[1].sx, 30.0f) && approx(t[1].sy, 26.0f));
    VCHECK(approx(t[2].sy, 25.0f));

    /* Mostly vertical, (5,5) to (3,40): x + 1, from either direction. */
    set_xy(&t[0], 5.0f, 5.0f);
    set_xy(&t[1], 5.0f, 5.0f);
    set_xy(&t[2], 3.0f, 40.0f);
    v9x_cif_line_widen(t, 0ul);
    VCHECK(approx(t[1].sx, 6.0f) && approx(t[1].sy, 5.0f));
    VCHECK(approx(t[2].sx, 4.0f) && approx(t[2].sy, 40.0f));
}

unsigned int v9x_run_cif_vertex_tests(void)
{
    cif_vertex_failures = 0u;
    test_position();
    test_depth();
    test_texture_coordinates();
    test_color();
    test_fog();
    test_flat();
    test_primitives();
    test_line_widen();
    test_opaque();
    return cif_vertex_failures;
}
