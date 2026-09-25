/*
 * Tests for the core's back-face culling decision.
 *
 * Direct3D defines the winding in screen space, where y grows DOWN, so the
 * sign convention is the opposite of the textbook y-up one and is the single
 * thing most likely to be got backwards. Every case below names the winding
 * as it would look on the monitor, and the triangles are the ones 3D WinBench
 * 98's Cull Clockwise and Cull Counterclockwise tests are about.
 */
#include <stdio.h>

#include "../../src/display32/r3d/r3d_cull.h"

static unsigned int cull_failures = 0u;

#define CCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++cull_failures; \
    } \
} while (0)

/* Right along the top edge, then down and back left: clockwise on screen. */
static const float cw_tri[6] = { 10.0f, 10.0f, 50.0f, 10.0f, 10.0f, 50.0f };
/* The same three points in the other order: counterclockwise on screen. */
static const float ccw_tri[6] = { 10.0f, 10.0f, 10.0f, 50.0f, 50.0f, 10.0f };

static int culled(unsigned long mode, const float *t)
{
    return v9x_r3d_cull_triangle(mode, t[0], t[1], t[2], t[3], t[4], t[5]);
}

static void test_values_match_d3dtypes(void)
{
    /* d3dtypes.h: D3DCULL_NONE 1, D3DCULL_CW 2, D3DCULL_CCW 3. Through a
     * table so the comparison happens at run time; the compiler rejects a
     * constant one as unreachable code. */
    static const unsigned long values[3] = {
        V9X_R3D_CULL_NONE, V9X_R3D_CULL_CW, V9X_R3D_CULL_CCW
    };
    unsigned int index;

    for (index = 0u; index < 3u; ++index) {
        CCHECK(values[index] == (unsigned long)index + 1ul);
    }
}

static void test_cull_none_draws_both(void)
{
    CCHECK(culled(V9X_R3D_CULL_NONE, cw_tri) == 0);
    CCHECK(culled(V9X_R3D_CULL_NONE, ccw_tri) == 0);
}

static void test_cull_cw_removes_clockwise_only(void)
{
    CCHECK(culled(V9X_R3D_CULL_CW, cw_tri) == 1);
    CCHECK(culled(V9X_R3D_CULL_CW, ccw_tri) == 0);
}

static void test_cull_ccw_removes_counterclockwise_only(void)
{
    CCHECK(culled(V9X_R3D_CULL_CCW, ccw_tri) == 1);
    CCHECK(culled(V9X_R3D_CULL_CCW, cw_tri) == 0);
}

/* Winding is a property of the cycle, not of which vertex comes first. */
static void test_rotation_keeps_winding(void)
{
    CCHECK(v9x_r3d_cull_triangle(V9X_R3D_CULL_CW,
                                 50.0f, 10.0f, 10.0f, 50.0f,
                                 10.0f, 10.0f) == 1);
    CCHECK(v9x_r3d_cull_triangle(V9X_R3D_CULL_CCW,
                                 10.0f, 50.0f, 50.0f, 10.0f,
                                 10.0f, 10.0f) == 1);
}

/*
 * A triangle partly off the target keeps its winding: the core culls before
 * it clips, so the test must hold for guard-band coordinates too.
 */
static void test_offscreen_coordinates(void)
{
    CCHECK(v9x_r3d_cull_triangle(V9X_R3D_CULL_CW,
                                 -500.0f, -500.0f, 900.0f, -500.0f,
                                 -500.0f, 900.0f) == 1);
    CCHECK(v9x_r3d_cull_triangle(V9X_R3D_CULL_CCW,
                                 -500.0f, -500.0f, 900.0f, -500.0f,
                                 -500.0f, 900.0f) == 0);
}

/*
 * Zero area and NaN are never culled. A degenerate triangle covers nothing
 * whichever way it is decided; a NaN vertex must reach the clipper, whose
 * guard-band test is what refuses it with a count.
 */
static void test_degenerate_and_nan_pass_through(void)
{
    /* The quiet NaN from its bits: 0.0f / 0.0f is folded by the compiler and
     * is not guaranteed to produce one. */
    union {
        unsigned long bits;
        float value;
    } quiet;
    float nan;
    float inf;

    quiet.bits = 0x7fc00000ul;
    nan = quiet.value;
    quiet.bits = 0x7f800000ul;
    inf = quiet.value;
    CCHECK(v9x_r3d_cull_triangle(V9X_R3D_CULL_CW,
                                 10.0f, 10.0f, inf, 10.0f,
                                 10.0f, 50.0f) == 0);

    CCHECK(v9x_r3d_cull_triangle(V9X_R3D_CULL_CW,
                                 10.0f, 10.0f, 20.0f, 20.0f,
                                 30.0f, 30.0f) == 0);
    CCHECK(v9x_r3d_cull_triangle(V9X_R3D_CULL_CCW,
                                 10.0f, 10.0f, 20.0f, 20.0f,
                                 30.0f, 30.0f) == 0);
    CCHECK(v9x_r3d_cull_triangle(V9X_R3D_CULL_CW,
                                 nan, 10.0f, 50.0f, 10.0f,
                                 10.0f, 50.0f) == 0);
    CCHECK(v9x_r3d_cull_triangle(V9X_R3D_CULL_CCW,
                                 nan, 10.0f, 10.0f, 50.0f,
                                 50.0f, 10.0f) == 0);
}

/* An undefined render-state value is treated as NONE, never as a cull. */
static void test_unknown_mode_draws(void)
{
    CCHECK(culled(0ul, cw_tri) == 0);
    CCHECK(culled(4ul, ccw_tri) == 0);
    CCHECK(culled(0x7ffffffful, cw_tri) == 0);
}

/*
 * The core honours a cull mode only when the engine advertises it. An engine
 * that claims CULLNONE alone - the ViRGE and the software engine today -
 * keeps drawing every triangle whatever the application asks, which is what
 * it did before this module existed.
 */
static void test_mode_gated_by_caps(void)
{
    CCHECK(v9x_r3d_cull_honoured(V9X_R3D_CULL_CCW, 0, 0) == V9X_R3D_CULL_NONE);
    CCHECK(v9x_r3d_cull_honoured(V9X_R3D_CULL_CW, 0, 0) == V9X_R3D_CULL_NONE);
    CCHECK(v9x_r3d_cull_honoured(V9X_R3D_CULL_CCW, 1, 0) == V9X_R3D_CULL_NONE);
    CCHECK(v9x_r3d_cull_honoured(V9X_R3D_CULL_CW, 0, 1) == V9X_R3D_CULL_NONE);
    CCHECK(v9x_r3d_cull_honoured(V9X_R3D_CULL_CW, 1, 0) == V9X_R3D_CULL_CW);
    CCHECK(v9x_r3d_cull_honoured(V9X_R3D_CULL_CCW, 0, 1) == V9X_R3D_CULL_CCW);
    CCHECK(v9x_r3d_cull_honoured(V9X_R3D_CULL_NONE, 1, 1) == V9X_R3D_CULL_NONE);
    CCHECK(v9x_r3d_cull_honoured(9ul, 1, 1) == V9X_R3D_CULL_NONE);
}

unsigned int v9x_run_r3d_cull_tests(void)
{
    cull_failures = 0u;
    test_values_match_d3dtypes();
    test_cull_none_draws_both();
    test_cull_cw_removes_clockwise_only();
    test_cull_ccw_removes_counterclockwise_only();
    test_rotation_keeps_winding();
    test_offscreen_coordinates();
    test_degenerate_and_nan_pass_through();
    test_unknown_mode_draws();
    test_mode_gated_by_caps();
    if (cull_failures == 0u) {
        puts("PASS: Direct3D back-face culling decision");
    }
    return cull_failures;
}
