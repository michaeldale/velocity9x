/*
 * Rage II triangle setup against an independent reference.
 *
 * The engine model below is the edge walk measured on A8U4I5
 * (docs\decisions\2026-10-02-rage-iic-trapezoid-edge-model.md): rows from
 * DST_Y_X's Y for `length` rows; per row each edge steps in its direction
 * while its error is >= 0, adding DEC, then adds INC; DEC 0 never steps;
 * the span is [leading, trailing) with the fill to the right. Setup is
 * correct when that model, fed setup's trapezoids, draws exactly the pixels
 * the reference rasteriser (rage2_reference.c) covers.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/ati_rage2.h"
#include "rage2_reference.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define GRID 64

static unsigned char model_pixels[GRID][GRID];

/* One edge's X on each row, as the measured walk produces it. */
static void model_walk(v9x_u32 start, v9x_s32 err, v9x_s32 inc, v9x_s32 dec,
                       int rightward, v9x_u32 rows, v9x_s32 *x_out)
{
    v9x_s32 x = (v9x_s32)start;
    v9x_s32 e = err;
    v9x_u32 row;
    unsigned int guard;

    for (row = 0ul; row < rows; ++row) {
        guard = 0u;
        while (dec != 0l && e >= 0l && guard < 4096u) {
            x += rightward ? 1l : -1l;
            e += dec;
            ++guard;
        }
        x_out[row] = x;
        e += inc;
    }
}

static void model_draw(const struct v9x_r2_flat_trap *trap)
{
    v9x_s32 lead[GRID];
    v9x_s32 trail[GRID];
    v9x_u32 row;
    v9x_s32 x;

    model_walk(trap->x, trap->lead_err, trap->lead_inc, trap->lead_dec,
               (trap->dst_cntl & V9X_M64_DST_X_DIR) != 0ul, trap->length,
               lead);
    model_walk(trap->trail_x, trap->trail_err, trap->trail_inc,
               trap->trail_dec, (trap->dst_cntl & V9X_R2_TRAIL_X_DIR) != 0ul,
               trap->length, trail);
    for (row = 0ul; row < trap->length; ++row) {
        v9x_u32 y = trap->y + row;

        for (x = lead[row]; x < trail[row]; ++x) {
            if (y < GRID && x >= 0l && x < GRID) {
                ++model_pixels[y][x];
            }
        }
    }
}

static void make_target(struct v9x_r2_target *target)
{
    memset(target, 0, sizeof(*target));
    target->offset = 0x00200000ul;
    target->pitch_bytes = GRID * 2ul;
    target->width = GRID;
    target->height = GRID;
    target->vram_bytes = 0x00400000ul;
    target->scissor_right = GRID - 1ul;
    target->scissor_bottom = GRID - 1ul;
}

/* Setup, then the model, then a pixel-for-pixel comparison. Returns the
 * number of mismatching pixels; a pixel drawn twice counts too, because two
 * trapezoids that overlap would double-blend once blending exists. */
static unsigned int compare_triangle(const struct v9x_r2_vertex *v)
{
    struct v9x_r2_target target;
    struct v9x_r2_flat_trap traps[V9X_R2_SETUP_TRAPS];
    v9x_u32 count = 99ul;
    v9x_u32 index;
    unsigned int mismatches = 0u;
    v9x_s32 x;
    v9x_s32 y;

    make_target(&target);
    memset(model_pixels, 0, sizeof(model_pixels));
    if (v9x_r2_setup_triangle(&target, v, traps, &count) != V9X_STATUS_OK) {
        return 9999u;
    }
    if (count > V9X_R2_SETUP_TRAPS) {
        return 9998u;
    }
    for (index = 0ul; index < count; ++index) {
        model_draw(&traps[index]);
    }
    for (y = 0l; y < GRID; ++y) {
        for (x = 0l; x < GRID; ++x) {
            int want = v9x_r2_ref_covers(v, x, y);

            if ((want && model_pixels[y][x] != 1u) ||
                (!want && model_pixels[y][x] != 0u)) {
                ++mismatches;
            }
        }
    }
    return mismatches;
}

static struct v9x_r2_vertex vtx(v9x_s32 x, v9x_s32 y)
{
    struct v9x_r2_vertex v;

    v.x = x;
    v.y = y;
    return v;
}

static void check_triangle(v9x_s32 x0, v9x_s32 y0, v9x_s32 x1, v9x_s32 y1,
                           v9x_s32 x2, v9x_s32 y2)
{
    struct v9x_r2_vertex v[3];
    unsigned int bad;

    v[0] = vtx(x0, y0);
    v[1] = vtx(x1, y1);
    v[2] = vtx(x2, y2);
    bad = compare_triangle(v);
    if (bad != 0u) {
        printf("  triangle (%ld,%ld) (%ld,%ld) (%ld,%ld): %u mismatches\n",
               (long)x0, (long)y0, (long)x1, (long)y1, (long)x2, (long)y2,
               bad);
    }
    CHECK(bad == 0u);
}

#define P(n) ((v9x_s32)(n) * V9X_R2_SUBPIXEL)

static void test_named_triangles(void)
{
    /* Integer corners, both windings. */
    check_triangle(P(10), P(10), P(40), P(12), P(20), P(40));
    check_triangle(P(10), P(10), P(20), P(40), P(40), P(12));
    /* Flat top, flat bottom. */
    check_triangle(P(8), P(8), P(48), P(8), P(28), P(40));
    check_triangle(P(28), P(8), P(8), P(40), P(48), P(40));
    /* Vertical left and right edges: a right triangle each way. */
    check_triangle(P(16), P(16), P(16), P(48), P(48), P(48));
    check_triangle(P(48), P(16), P(48), P(48), P(16), P(48));
    /* Pixel centres exactly on edges and on the middle vertex's row. */
    check_triangle(P(8) + 8, P(8) + 8, P(40) + 8, P(20) + 8,
                   P(12) + 8, P(44) + 8);
    /* Thin slivers. */
    check_triangle(P(5), P(5), P(6), P(60), P(5) + 3, P(30));
    check_triangle(P(2), P(30), P(62), P(31), P(30), P(30) + 5);
    /* Touching the target's edges. */
    check_triangle(P(0), P(0), P(64), P(0), P(0), P(64));
    check_triangle(P(64), P(64), P(0), P(64), P(64), P(0));
    /* Subpixel everywhere. */
    check_triangle(P(10) + 3, P(7) + 11, P(51) + 13, P(19) + 2,
                   P(23) + 7, P(58) + 9);
}

static void test_degenerate(void)
{
    struct v9x_r2_target target;
    struct v9x_r2_flat_trap traps[V9X_R2_SETUP_TRAPS];
    struct v9x_r2_vertex v[3];
    v9x_u32 count = 7ul;

    make_target(&target);
    /* Collinear: no pixel. */
    v[0] = vtx(P(4), P(4));
    v[1] = vtx(P(20), P(20));
    v[2] = vtx(P(40), P(40));
    CHECK(v9x_r2_setup_triangle(&target, v, traps, &count) == V9X_STATUS_OK);
    CHECK(count == 0ul);
    /* Between two row centres: no row. */
    v[0] = vtx(P(4), P(10) + 9);
    v[1] = vtx(P(40), P(10) + 10);
    v[2] = vtx(P(20), P(11) + 7);
    CHECK(v9x_r2_setup_triangle(&target, v, traps, &count) == V9X_STATUS_OK);
    CHECK(count == 0ul);
    CHECK(compare_triangle(v) == 0u);
}

static void test_bounds(void)
{
    struct v9x_r2_target target;
    struct v9x_r2_flat_trap traps[V9X_R2_SETUP_TRAPS];
    struct v9x_r2_vertex v[3];
    v9x_u32 count = 7ul;

    make_target(&target);
    v[0] = vtx(P(4), P(4));
    v[1] = vtx(P(20), P(10));
    v[2] = vtx(P(65), P(40));      /* past the target */
    CHECK(v9x_r2_setup_triangle(&target, v, traps, &count) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(count == 0ul);
    v[2] = vtx(-1l, P(40));
    CHECK(v9x_r2_setup_triangle(&target, v, traps, &count) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_r2_setup_triangle(&target, 0, traps, &count) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* A deterministic sweep: the same sequence on every run and compiler. */
static v9x_u32 lcg_state = 12345ul;

static v9x_s32 lcg_coord(void)
{
    lcg_state = lcg_state * 1103515245ul + 12345ul;
    return (v9x_s32)((lcg_state >> 8) % (GRID * 16ul + 1ul));
}

static void test_random_triangles(void)
{
    unsigned int index;
    unsigned int failed = 0u;
    unsigned long covered = 0ul;
    unsigned int non_empty = 0u;

    for (index = 0u; index < 4000u; ++index) {
        struct v9x_r2_vertex v[3];
        unsigned int bad;
        unsigned long pixels = 0ul;
        v9x_s32 x;
        v9x_s32 y;

        v[0] = vtx(lcg_coord(), lcg_coord());
        v[1] = vtx(lcg_coord(), lcg_coord());
        v[2] = vtx(lcg_coord(), lcg_coord());
        bad = compare_triangle(v);
        for (y = 0l; y < GRID; ++y) {
            for (x = 0l; x < GRID; ++x) {
                pixels += (unsigned long)model_pixels[y][x];
            }
        }
        covered += pixels;
        if (pixels != 0ul) {
            ++non_empty;
        }
        if (bad != 0u && failed < 5u) {
            printf("  random %u: (%ld,%ld) (%ld,%ld) (%ld,%ld): %u\n",
                   index, (long)v[0].x, (long)v[0].y, (long)v[1].x,
                   (long)v[1].y, (long)v[2].x, (long)v[2].y, bad);
        }
        if (bad != 0u) {
            ++failed;
        }
    }
    CHECK(failed == 0u);
    /* Not vacuous: random triangles in a 64x64 grid average about a
     * twelfth of it, so most are non-empty and the total is large. */
    CHECK(non_empty > 3500u);
    CHECK(covered > 500000ul);
}

/* A shared edge between two triangles draws every pixel exactly once. */
static void test_shared_edge(void)
{
    struct v9x_r2_target target;
    struct v9x_r2_flat_trap traps[V9X_R2_SETUP_TRAPS];
    struct v9x_r2_vertex a[3];
    struct v9x_r2_vertex b[3];
    v9x_u32 count;
    v9x_u32 index;
    v9x_s32 x;
    v9x_s32 y;
    unsigned int twice = 0u;
    unsigned int missing = 0u;

    make_target(&target);
    memset(model_pixels, 0, sizeof(model_pixels));
    a[0] = vtx(P(4) + 5, P(6) + 1);
    a[1] = vtx(P(50) + 3, P(9) + 12);
    a[2] = vtx(P(20) + 9, P(55) + 4);
    b[0] = a[1];
    b[1] = vtx(P(60) + 1, P(50) + 7);
    b[2] = a[2];
    CHECK(v9x_r2_setup_triangle(&target, a, traps, &count) == V9X_STATUS_OK);
    for (index = 0ul; index < count; ++index) {
        model_draw(&traps[index]);
    }
    CHECK(v9x_r2_setup_triangle(&target, b, traps, &count) == V9X_STATUS_OK);
    for (index = 0ul; index < count; ++index) {
        model_draw(&traps[index]);
    }
    for (y = 0l; y < GRID; ++y) {
        for (x = 0l; x < GRID; ++x) {
            int want = v9x_r2_ref_covers(a, x, y) || v9x_r2_ref_covers(b, x, y);

            if (model_pixels[y][x] > 1u) {
                ++twice;
            }
            if (want && model_pixels[y][x] == 0u) {
                ++missing;
            }
        }
    }
    CHECK(twice == 0u);
    CHECK(missing == 0u);
}

unsigned int v9x_run_rage2_setup_tests(void)
{
    test_named_triangles();
    test_degenerate();
    test_bounds();
    test_random_triangles();
    test_shared_edge();
    return failures;
}
