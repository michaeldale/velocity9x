/*
 * Tests for Glide 3's vertex layouts, texture-info translation and
 * primitive expansion (src\glide3\glide3_layout.c). The layouts and
 * vertices are the ones the census logged: Diablo II's menus
 * (docs\decisions\2026-10-10-diablo2-glide3-census.md) and Rollcage
 * (docs\decisions\2026-10-10-rollcage-glide3-census.md).
 */
#include <stdio.h>
#include "../../src/glide3/glide3_layout.h"

static unsigned int glide3_layout_failures;

#define G3CHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++glide3_layout_failures; \
    } \
} while (0)

static int g3_approx(float value, float expected)
{
    float difference = value - expected;

    if (difference < 0.0f) {
        difference = -difference;
    }
    return difference <= 0.0001f;
}

static float g3_bits(v9x_u32 bits)
{
    union {
        v9x_u32 u;
        float f;
    } value;

    value.u = bits;
    return value.f;
}

/* Diablo II: XY at 0, PARGB at 8, Q0 at 12, ST0 at 16, stride 28. Its
 * second menu vertex: (256, 0), white, q0 1, s 256, t 0. */
static void test_diablo_vertex(void)
{
    V9X_GLIDE3_LAYOUT layout;
    v9x_u32 words[7];
    float out[V9X_GLIDE_VERTEX_FLOATS];

    v9x_glide3_layout_init(&layout);
    G3CHECK(v9x_glide3_layout_set(&layout, 0x01ul, 0ul, 1ul));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x50ul, 12ul, 1ul));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x40ul, 16ul, 1ul));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x30ul, 8ul, 1ul));
    words[0] = 0x43800000ul;
    words[1] = 0x00000000ul;
    words[2] = 0x80FF4020ul;     /* A 80, R FF, G 40, B 20 */
    words[3] = 0x3F800000ul;
    words[4] = 0x43800000ul;
    words[5] = 0x00000000ul;
    words[6] = 0ul;
    v9x_glide3_vertex_read(&layout, (const v9x_u8 *)words, out);
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_X], 256.0f));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_Y], 0.0f));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_A], 128.0f));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_R], 255.0f));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_G], 64.0f));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_B], 32.0f));
    /* No Q: Q0 stands for the vertex oow. */
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_OOW], 1.0f));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_SOW], 256.0f));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_TOW], 0.0f));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_OOZ], 0.0f));
}

/* Rollcage: XY at 0, RGB floats at 8, Q and Q0 at 20, ST0 and ST1 at 24,
 * stride 32, the rest disabled first. Its first logged vertex. */
static void test_rollcage_vertex(void)
{
    V9X_GLIDE3_LAYOUT layout;
    v9x_u32 words[8];
    float out[V9X_GLIDE_VERTEX_FLOATS];

    v9x_glide3_layout_init(&layout);
    G3CHECK(v9x_glide3_layout_set(&layout, 0x05ul, 0ul, 0ul));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x30ul, 0ul, 0ul));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x42ul, 0ul, 0ul));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x02ul, 0ul, 0ul));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x03ul, 0ul, 0ul));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x01ul, 0ul, 1ul));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x20ul, 8ul, 1ul));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x04ul, 20ul, 1ul));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x50ul, 20ul, 1ul));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x40ul, 24ul, 1ul));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x41ul, 24ul, 1ul));
    G3CHECK(!v9x_glide3_layout_set(&layout, 0x99ul, 0ul, 1ul));
    words[0] = 0x00000000ul;
    words[1] = 0x43800000ul;
    words[2] = 0x437E0000ul;
    words[3] = 0x437E0000ul;
    words[4] = 0x437E0000ul;
    words[5] = 0x3EF3CF3Eul;
    words[6] = 0x42F352E7ul;
    words[7] = 0x3E6EEEF0ul;
    v9x_glide3_vertex_read(&layout, (const v9x_u8 *)words, out);
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_Y], 256.0f));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_R], 254.0f));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_B], 254.0f));
    /* No A: full. */
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_A], 255.0f));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_OOW], g3_bits(0x3EF3CF3Eul)));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_SOW], g3_bits(0x42F352E7ul)));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_TOW], g3_bits(0x3E6EEEF0ul)));
}

/* W alone gives oow as its reciprocal; nothing at all gives 1. */
static void test_w_and_defaults(void)
{
    V9X_GLIDE3_LAYOUT layout;
    v9x_u32 words[3];
    float out[V9X_GLIDE_VERTEX_FLOATS];

    v9x_glide3_layout_init(&layout);
    G3CHECK(v9x_glide3_layout_set(&layout, 0x01ul, 0ul, 1ul));
    words[0] = 0x41200000ul;     /* 10 */
    words[1] = 0x41A00000ul;     /* 20 */
    words[2] = 0x40800000ul;     /* 4 */
    v9x_glide3_vertex_read(&layout, (const v9x_u8 *)words, out);
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_OOW], 1.0f));
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_R], 255.0f));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x03ul, 8ul, 1ul));
    v9x_glide3_vertex_read(&layout, (const v9x_u8 *)words, out);
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_OOW], 0.25f));
    G3CHECK(v9x_glide3_layout_set(&layout, 0x02ul, 8ul, 1ul));
    v9x_glide3_vertex_read(&layout, (const v9x_u8 *)words, out);
    G3CHECK(g3_approx(out[V9X_GLIDE_VERTEX_OOZ], 4.0f));
}

/* Glide 3 LOD log2 8 (256) is Glide 2's GR_LOD_256 (0); 0 (1) is GR_LOD_1
 * (8). Aspect log2 3 (8x1) is GR_ASPECT_8x1 (0), -3 (1x8) is 1x8 (6).
 * Rollcage's textures: LOD 8..8, aspects 0 to 3, P_8. */
static void test_texinfo(void)
{
    v9x_u32 g3[5];
    v9x_u32 g2[5];
    v9x_u32 value;

    G3CHECK(v9x_glide3_lod_to_glide2(8ul, &value) && value == 0ul);
    G3CHECK(v9x_glide3_lod_to_glide2(0ul, &value) && value == 8ul);
    G3CHECK(!v9x_glide3_lod_to_glide2(9ul, &value));
    G3CHECK(!v9x_glide3_lod_to_glide2(0xFFFFFFFFul, &value));
    G3CHECK(v9x_glide3_aspect_to_glide2(3ul, &value) && value == 0ul);
    G3CHECK(v9x_glide3_aspect_to_glide2(0ul, &value) && value == 3ul);
    G3CHECK(v9x_glide3_aspect_to_glide2(0xFFFFFFFDul, &value) && value == 6ul);
    G3CHECK(!v9x_glide3_aspect_to_glide2(4ul, &value));
    G3CHECK(!v9x_glide3_aspect_to_glide2(0xFFFFFFFCul, &value));

    g3[0] = 8ul;
    g3[1] = 8ul;
    g3[2] = 2ul;
    g3[3] = 5ul;
    g3[4] = 0x12345678ul;
    G3CHECK(v9x_glide3_texinfo_to_glide2(g3, g2));
    G3CHECK(g2[0] == 0ul && g2[1] == 0ul && g2[2] == 1ul);
    G3CHECK(g2[3] == 5ul && g2[4] == 0x12345678ul);
    /* A mip chain from 1x1 to 64x64: small 0 -> GR_LOD_1, large 6 ->
     * GR_LOD_64 (2). */
    g3[0] = 0ul;
    g3[1] = 6ul;
    G3CHECK(v9x_glide3_texinfo_to_glide2(g3, g2));
    G3CHECK(g2[0] == 8ul && g2[1] == 2ul);
}

static void test_primitives(void)
{
    v9x_u32 i[3];

    /* Rollcage's GR_POLYGON of 4, Diablo II's fan of 4: two triangles. */
    G3CHECK(v9x_glide3_triangle_count(3ul, 4ul) == 2ul);
    G3CHECK(v9x_glide3_triangle_count(5ul, 4ul) == 2ul);
    v9x_glide3_triangle_at(3ul, 1ul, i);
    G3CHECK(i[0] == 0ul && i[1] == 2ul && i[2] == 3ul);
    /* Rollcage's GR_TRIANGLES of 6. */
    G3CHECK(v9x_glide3_triangle_count(6ul, 6ul) == 2ul);
    G3CHECK(v9x_glide3_triangle_count(6ul, 7ul) == 2ul);
    v9x_glide3_triangle_at(6ul, 1ul, i);
    G3CHECK(i[0] == 3ul && i[1] == 4ul && i[2] == 5ul);
    /* Strips keep the first triangle's winding. */
    G3CHECK(v9x_glide3_triangle_count(4ul, 5ul) == 3ul);
    v9x_glide3_triangle_at(4ul, 0ul, i);
    G3CHECK(i[0] == 0ul && i[1] == 1ul && i[2] == 2ul);
    v9x_glide3_triangle_at(4ul, 1ul, i);
    G3CHECK(i[0] == 2ul && i[1] == 1ul && i[2] == 3ul);
    v9x_glide3_triangle_at(4ul, 2ul, i);
    G3CHECK(i[0] == 2ul && i[1] == 3ul && i[2] == 4ul);
    /* Too few vertices, points, lines and the continue modes: none. */
    G3CHECK(v9x_glide3_triangle_count(3ul, 2ul) == 0ul);
    G3CHECK(v9x_glide3_triangle_count(0ul, 9ul) == 0ul);
    G3CHECK(v9x_glide3_triangle_count(2ul, 9ul) == 0ul);
    G3CHECK(v9x_glide3_triangle_count(7ul, 9ul) == 0ul);
    G3CHECK(v9x_glide3_triangle_count(8ul, 9ul) == 0ul);

    G3CHECK(v9x_glide3_line_count(2ul, 5ul) == 2ul);
    v9x_glide3_line_at(2ul, 1ul, i);
    G3CHECK(i[0] == 2ul && i[1] == 3ul);
    G3CHECK(v9x_glide3_line_count(1ul, 5ul) == 4ul);
    v9x_glide3_line_at(1ul, 3ul, i);
    G3CHECK(i[0] == 3ul && i[1] == 4ul);
    G3CHECK(v9x_glide3_line_count(1ul, 1ul) == 0ul);
    G3CHECK(v9x_glide3_line_count(6ul, 6ul) == 0ul);
}

unsigned int v9x_run_glide3_layout_tests(void)
{
    glide3_layout_failures = 0u;
    test_diablo_vertex();
    test_rollcage_vertex();
    test_w_and_defaults();
    test_texinfo();
    test_primitives();
    return glide3_layout_failures;
}
