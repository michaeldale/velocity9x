/*
 * Tests for the CPU rasterizer's arithmetic.
 *
 * Mode 2's work order splits deliberately: steps 3 to 5 proved the plumbing on
 * a guest with a stub that drew rectangles, and step 6 is the arithmetic,
 * which needs no guest at all. This file is the second half of that bargain.
 * If a triangle comes out wrong on a Trio64 after this passes, the fault is
 * upstream of the maths.
 *
 * What is checked here is mostly not "does it look right" - a rasterizer looks
 * right long before it is right. It is the properties that fail silently:
 *
 *   - that two triangles sharing an edge cover the pixels along it exactly
 *     once, neither twice nor not at all, which is the whole content of the
 *     coverage rule and is invisible until something blends or a crack appears
 *     in a mesh;
 *   - that nothing is written outside the target, on a card where the target
 *     is also the desktop;
 *   - that a flat-coloured triangle is exactly one colour, because the
 *     interpolator running when it has nothing to interpolate is how a solid
 *     surface acquires a gradient;
 *   - that the same triangle handed over in a different vertex order draws the
 *     same pixels.
 *
 * docs\plans\s3-trio64-voodoo2-hybrid-3d.md, mode 2, work-order step 6.
 */
#include <stdio.h>

/* Reached by relative path, as test_d3d_zfixed.c reaches its header: this one
 * is engine-side and deliberately not published under include\. */
#include "../../src/display32/d3d/d3d_raster.h"

static unsigned int raster_failures = 0u;

#define RCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++raster_failures; \
    } \
} while (0)

/*
 * A target with room either side of it.
 *
 * The pitch is wider than the width and the buffer is wider than the pitch, so
 * a span that runs one pixel long lands in a cell this file can name rather
 * than in whatever the allocator put next.
 */
#define RASTER_WIDTH   32u
#define RASTER_HEIGHT  24u
#define RASTER_STRIDE  40u   /* pixels per row, i.e. an 80-byte pitch */
#define RASTER_GUARD   16u   /* pixels of margin before and after */
#define RASTER_CELLS   (RASTER_GUARD * 2u + RASTER_STRIDE * RASTER_HEIGHT)

#define RASTER_BACKGROUND ((v9x_u16)0x1234u)

static v9x_u16 raster_cells[RASTER_CELLS];

/* Whole pixels as a 28.4 coordinate. */
#define PX(value) (((v9x_s32)(value)) << V9X_D3D_RASTER_SUBPIXEL_BITS)

static void raster_reset(V9X_D3D_RASTER_TARGET *target)
{
    unsigned int index;

    for (index = 0u; index < RASTER_CELLS; ++index) {
        raster_cells[index] = RASTER_BACKGROUND;
    }
    target->pixels = &raster_cells[RASTER_GUARD];
    target->pitch = RASTER_STRIDE * 2ul;
    target->width = RASTER_WIDTH;
    target->height = RASTER_HEIGHT;
}

static v9x_u16 raster_pixel(unsigned int x, unsigned int y)
{
    return raster_cells[RASTER_GUARD + y * RASTER_STRIDE + x];
}

/*
 * Nothing outside the declared extent was written: not the guard margins, not
 * the padding between the width and the pitch.
 */
static void raster_check_untouched_margins(void)
{
    unsigned int index;
    unsigned int row;
    unsigned int column;

    for (index = 0u; index < RASTER_GUARD; ++index) {
        RCHECK(raster_cells[index] == RASTER_BACKGROUND);
        RCHECK(raster_cells[RASTER_CELLS - 1u - index] == RASTER_BACKGROUND);
    }
    for (row = 0u; row < RASTER_HEIGHT; ++row) {
        for (column = RASTER_WIDTH; column < RASTER_STRIDE; ++column) {
            RCHECK(raster_pixel(column, row) == RASTER_BACKGROUND);
        }
    }
}

static void raster_vertex(V9X_D3D_RASTER_VERTEX *vertex,
                          v9x_s32 x, v9x_s32 y,
                          v9x_s32 red, v9x_s32 green, v9x_s32 blue)
{
    vertex->x = x;
    vertex->y = y;
    vertex->red = red;
    vertex->green = green;
    vertex->blue = blue;
}

static void test_rgb565_packing(void)
{
    RCHECK(v9x_d3d_raster_rgb565(0l, 0l, 0l) == 0x0000u);
    RCHECK(v9x_d3d_raster_rgb565(255l, 255l, 255l) == 0xffffu);
    RCHECK(v9x_d3d_raster_rgb565(255l, 0l, 0l) == 0xf800u);
    RCHECK(v9x_d3d_raster_rgb565(0l, 255l, 0l) == 0x07e0u);
    RCHECK(v9x_d3d_raster_rgb565(0l, 0l, 255l) == 0x001fu);

    /* Saturating at both ends. The low end is the one that matters: the span
     * interpolator's last pixel can land a fraction of a level below the
     * endpoint colour, and a wrapped channel is a bright speck at the end of
     * every span rather than a dark one. */
    RCHECK(v9x_d3d_raster_rgb565(300l, 300l, 300l) == 0xffffu);
    RCHECK(v9x_d3d_raster_rgb565(-1l, -1l, -1l) == 0x0000u);
    RCHECK(v9x_d3d_raster_rgb565(-40000l, 0l, 0l) == 0x0000u);
}

static void test_target_validation(void)
{
    V9X_D3D_RASTER_TARGET target;

    raster_reset(&target);
    RCHECK(v9x_d3d_raster_target_valid(&target) != 0);
    RCHECK(v9x_d3d_raster_target_valid(0) == 0);

    target.pixels = 0;
    RCHECK(v9x_d3d_raster_target_valid(&target) == 0);

    raster_reset(&target);
    target.width = 0ul;
    RCHECK(v9x_d3d_raster_target_valid(&target) == 0);

    raster_reset(&target);
    target.height = 0ul;
    RCHECK(v9x_d3d_raster_target_valid(&target) == 0);

    /* The overflow bound. A wider target makes the interpolator's products
     * exceed a signed 32-bit integer, so it is refused rather than drawn
     * wrongly on large modes only. */
    raster_reset(&target);
    target.width = V9X_D3D_RASTER_DIMENSION_MAX;
    target.pitch = V9X_D3D_RASTER_DIMENSION_MAX * 2ul;
    RCHECK(v9x_d3d_raster_target_valid(&target) != 0);
    target.width = V9X_D3D_RASTER_DIMENSION_MAX + 1ul;
    RCHECK(v9x_d3d_raster_target_valid(&target) == 0);
    raster_reset(&target);
    target.height = V9X_D3D_RASTER_DIMENSION_MAX + 1ul;
    RCHECK(v9x_d3d_raster_target_valid(&target) == 0);

    /* A pitch narrower than the row is the one target defect that corrupts the
     * next scanline instead of faulting. */
    raster_reset(&target);
    target.pitch = RASTER_WIDTH * 2ul;
    RCHECK(v9x_d3d_raster_target_valid(&target) != 0);
    target.pitch = RASTER_WIDTH * 2ul - 1ul;
    RCHECK(v9x_d3d_raster_target_valid(&target) == 0);
}

static void test_refuses_coordinates_it_cannot_carry(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_VERTEX triangle[3];
    unsigned int index;

    raster_reset(&target);
    raster_vertex(&triangle[0], PX(2), PX(2), 255l, 255l, 255l);
    raster_vertex(&triangle[1], PX(20), PX(4), 255l, 255l, 255l);
    raster_vertex(&triangle[2], PX(4), PX(18), 255l, 255l, 255l);
    RCHECK(v9x_d3d_raster_triangle(&target, triangle) != 0);
    RCHECK(v9x_d3d_raster_triangle(&target, 0) == 0);

    /* The caller clips and clamps. A refusal here means it did not, and
     * drawing anyway would write outside a surface that is also the desktop. */
    for (index = 0u; index < 3u; ++index) {
        v9x_s32 saved;
        unsigned int cell;

        raster_reset(&target);
        saved = triangle[index].x;
        triangle[index].x = -1l;
        RCHECK(v9x_d3d_raster_triangle(&target, triangle) == 0);
        triangle[index].x = V9X_D3D_RASTER_COORD_MAX + 1l;
        RCHECK(v9x_d3d_raster_triangle(&target, triangle) == 0);
        triangle[index].x = saved;

        saved = triangle[index].y;
        triangle[index].y = -1l;
        RCHECK(v9x_d3d_raster_triangle(&target, triangle) == 0);
        triangle[index].y = V9X_D3D_RASTER_COORD_MAX + 1l;
        RCHECK(v9x_d3d_raster_triangle(&target, triangle) == 0);
        triangle[index].y = saved;

        /* A refusal draws nothing at all, rather than the part it liked. */
        raster_check_untouched_margins();
        for (cell = 0u; cell < RASTER_STRIDE * RASTER_HEIGHT; ++cell) {
            if (raster_cells[RASTER_GUARD + cell] != RASTER_BACKGROUND) {
                printf("FAIL %s:%u: refused triangle wrote cell %u\n",
                       __FILE__, (unsigned int)__LINE__, cell);
                ++raster_failures;
                break;
            }
        }
    }
}

/*
 * A flat-coloured triangle is exactly one colour, and it covers the pixels
 * whose centres are inside it.
 *
 * The right-angled triangle below has its legs on pixel boundaries, so the
 * expected set can be written down: rows 2 to 7 and, on each, the columns from
 * 2 up to the diagonal.
 */
static void test_flat_triangle_is_one_colour(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_VERTEX triangle[3];
    unsigned int row;
    unsigned int column;
    unsigned int painted = 0u;

    raster_reset(&target);
    raster_vertex(&triangle[0], PX(2), PX(2), 255l, 0l, 0l);
    raster_vertex(&triangle[1], PX(10), PX(2), 255l, 0l, 0l);
    raster_vertex(&triangle[2], PX(2), PX(8), 255l, 0l, 0l);
    RCHECK(v9x_d3d_raster_triangle(&target, triangle) != 0);

    for (row = 0u; row < RASTER_HEIGHT; ++row) {
        for (column = 0u; column < RASTER_WIDTH; ++column) {
            v9x_u16 value = raster_pixel(column, row);

            if (value == RASTER_BACKGROUND) {
                continue;
            }
            ++painted;
            /* Not "close to" red. One colour in, one colour out: an
             * interpolator that runs with nothing to interpolate is how a flat
             * surface acquires a gradient, and in RGB565 a drift of one level
             * is a visible band. */
            RCHECK(value == 0xf800u);
            RCHECK(row >= 2u && row < 8u);
            RCHECK(column >= 2u);
        }
    }
    /* Half the 8 x 6 box, give or take the diagonal's own pixels. */
    RCHECK(painted > 16u && painted < 40u);
    raster_check_untouched_margins();
}

/*
 * Two triangles sharing an edge cover every pixel along it exactly once.
 *
 * This is the property the coverage rule exists for and the one nothing else
 * would catch. Drawn into separate targets and compared as sets, because
 * drawing both into one cannot tell "covered twice" from "covered once".
 */
static void test_shared_edge_is_covered_exactly_once(void)
{
    static v9x_u16 first[RASTER_HEIGHT][RASTER_WIDTH];
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_VERTEX triangle[3];
    unsigned int row;
    unsigned int column;

    raster_reset(&target);
    raster_vertex(&triangle[0], PX(2), PX(2), 255l, 255l, 255l);
    raster_vertex(&triangle[1], PX(10), PX(2), 255l, 255l, 255l);
    raster_vertex(&triangle[2], PX(2), PX(8), 255l, 255l, 255l);
    RCHECK(v9x_d3d_raster_triangle(&target, triangle) != 0);
    for (row = 0u; row < RASTER_HEIGHT; ++row) {
        for (column = 0u; column < RASTER_WIDTH; ++column) {
            first[row][column] = raster_pixel(column, row);
        }
    }

    raster_reset(&target);
    raster_vertex(&triangle[0], PX(10), PX(2), 255l, 255l, 255l);
    raster_vertex(&triangle[1], PX(10), PX(8), 255l, 255l, 255l);
    raster_vertex(&triangle[2], PX(2), PX(8), 255l, 255l, 255l);
    RCHECK(v9x_d3d_raster_triangle(&target, triangle) != 0);

    for (row = 0u; row < RASTER_HEIGHT; ++row) {
        for (column = 0u; column < RASTER_WIDTH; ++column) {
            int in_first = first[row][column] != RASTER_BACKGROUND;
            int in_second = raster_pixel(column, row) != RASTER_BACKGROUND;
            int expected = (row >= 2u && row < 8u &&
                            column >= 2u && column < 10u);

            if ((in_first && in_second) || ((in_first || in_second) != expected)) {
                printf("FAIL %s:%u: pixel (%u,%u) first=%d second=%d "
                       "expected=%d\n",
                       __FILE__, (unsigned int)__LINE__, column, row,
                       in_first, in_second, expected);
                ++raster_failures;
            }
        }
    }
    raster_check_untouched_margins();
}

/*
 * A triangle larger than the target writes inside it and nowhere else.
 *
 * The coordinates stay inside the declared range - the caller's job - but the
 * geometry covers every pixel and overhangs each edge, so a scanline range or
 * span range that rounds outwards lands in the guard cells.
 */
static void test_full_target_triangle_stays_inside(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_VERTEX triangle[3];
    unsigned int row;
    unsigned int column;

    raster_reset(&target);
    raster_vertex(&triangle[0], PX(0), PX(0), 0l, 255l, 0l);
    raster_vertex(&triangle[1], PX(RASTER_WIDTH * 3u), PX(0), 0l, 255l, 0l);
    raster_vertex(&triangle[2], PX(0), PX(RASTER_HEIGHT * 3u), 0l, 255l, 0l);
    RCHECK(v9x_d3d_raster_triangle(&target, triangle) != 0);

    for (row = 0u; row < RASTER_HEIGHT; ++row) {
        for (column = 0u; column < RASTER_WIDTH; ++column) {
            RCHECK(raster_pixel(column, row) == 0x07e0u);
        }
    }
    raster_check_untouched_margins();
}

/* A triangle with no area covers no pixel centre, and says so by succeeding
 * rather than by failing: nothing was refused, there was nothing to draw. */
static void test_degenerate_triangles_draw_nothing(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_VERTEX triangle[3];
    unsigned int row;
    unsigned int column;
    unsigned int shape;

    for (shape = 0u; shape < 3u; ++shape) {
        raster_reset(&target);
        if (shape == 0u) {
            /* One point. */
            raster_vertex(&triangle[0], PX(5), PX(5), 255l, 255l, 255l);
            raster_vertex(&triangle[1], PX(5), PX(5), 255l, 255l, 255l);
            raster_vertex(&triangle[2], PX(5), PX(5), 255l, 255l, 255l);
        } else if (shape == 1u) {
            /* Zero height: every pixel centre is above or below it. */
            raster_vertex(&triangle[0], PX(2), PX(5), 255l, 255l, 255l);
            raster_vertex(&triangle[1], PX(20), PX(5), 255l, 255l, 255l);
            raster_vertex(&triangle[2], PX(11), PX(5), 255l, 255l, 255l);
        } else {
            /* Zero width. */
            raster_vertex(&triangle[0], PX(7), PX(2), 255l, 255l, 255l);
            raster_vertex(&triangle[1], PX(7), PX(18), 255l, 255l, 255l);
            raster_vertex(&triangle[2], PX(7), PX(9), 255l, 255l, 255l);
        }
        RCHECK(v9x_d3d_raster_triangle(&target, triangle) != 0);

        for (row = 0u; row < RASTER_HEIGHT; ++row) {
            for (column = 0u; column < RASTER_WIDTH; ++column) {
                RCHECK(raster_pixel(column, row) == RASTER_BACKGROUND);
            }
        }
    }
    raster_check_untouched_margins();
}

/*
 * The same triangle in every vertex order draws the same pixels.
 *
 * The y sort is what makes that true, and it is the part of the routine most
 * likely to be quietly asymmetric. The three y values are distinct on purpose:
 * with a tie the sort's result depends on input order by definition, and which
 * of two coincident vertices is called the middle one is not a property this
 * test should pin down.
 */
static void test_vertex_order_does_not_matter(void)
{
    static const unsigned int orders[6][3] = {
        { 0u, 1u, 2u }, { 0u, 2u, 1u }, { 1u, 0u, 2u },
        { 1u, 2u, 0u }, { 2u, 0u, 1u }, { 2u, 1u, 0u }
    };
    static v9x_u16 reference[RASTER_HEIGHT][RASTER_WIDTH];
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_VERTEX source[3];
    V9X_D3D_RASTER_VERTEX permuted[3];
    unsigned int order;
    unsigned int row;
    unsigned int column;

    raster_vertex(&source[0], PX(3) + 5l, PX(2), 255l, 0l, 0l);
    raster_vertex(&source[1], PX(27), PX(9) + 11l, 0l, 255l, 0l);
    raster_vertex(&source[2], PX(6), PX(20), 0l, 0l, 255l);

    for (order = 0u; order < 6u; ++order) {
        permuted[0] = source[orders[order][0]];
        permuted[1] = source[orders[order][1]];
        permuted[2] = source[orders[order][2]];

        raster_reset(&target);
        RCHECK(v9x_d3d_raster_triangle(&target, permuted) != 0);

        for (row = 0u; row < RASTER_HEIGHT; ++row) {
            for (column = 0u; column < RASTER_WIDTH; ++column) {
                v9x_u16 value = raster_pixel(column, row);

                if (order == 0u) {
                    reference[row][column] = value;
                } else if (value != reference[row][column]) {
                    printf("FAIL %s:%u: order %u pixel (%u,%u) is %04x, "
                           "not %04x\n",
                           __FILE__, (unsigned int)__LINE__, order, column,
                           row, (unsigned int)value,
                           (unsigned int)reference[row][column]);
                    ++raster_failures;
                }
            }
        }
    }
    raster_check_untouched_margins();
}

/*
 * Gouraud interpolation runs across the span and reaches both endpoints.
 *
 * The triangle is a rectangle's lower half with black down its left edge and
 * full red down its right, so every filled scanline is a horizontal ramp. The
 * assertion is monotonicity plus the two ends, not a per-pixel table: RGB565
 * keeps five bits of red and the exact quantisation of the middle of the ramp
 * is not a property worth freezing.
 */
static void test_gouraud_ramps_across_a_span(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_VERTEX triangle[3];
    unsigned int row = 10u;
    unsigned int column;
    unsigned int filled = 0u;
    int previous = -1;
    int highest = -1;
    int lowest = 32;

    raster_reset(&target);
    raster_vertex(&triangle[0], PX(1), PX(4), 0l, 0l, 0l);
    raster_vertex(&triangle[1], PX(31), PX(4), 255l, 0l, 0l);
    raster_vertex(&triangle[2], PX(1), PX(20), 0l, 0l, 0l);
    RCHECK(v9x_d3d_raster_triangle(&target, triangle) != 0);

    for (column = 0u; column < RASTER_WIDTH; ++column) {
        v9x_u16 value = raster_pixel(column, row);
        int red;

        if (value == RASTER_BACKGROUND) {
            continue;
        }
        ++filled;
        /* Only the red channel carries anything; the other two must stay at
         * zero rather than picking up the ramp's rounding. */
        RCHECK((value & 0x07ffu) == 0u);
        red = (int)(value >> 11);
        RCHECK(red >= previous);
        previous = red;
        if (red > highest) {
            highest = red;
        }
        if (red < lowest) {
            lowest = red;
        }
    }

    RCHECK(filled > 8u);
    RCHECK(lowest == 0);
    /* The right edge of this scanline is short of the red vertex, so the ramp
     * does not reach the top of the range - but it must get most of the way,
     * which a step computed from the wrong denominator would not. */
    RCHECK(highest >= 12);
    raster_check_untouched_margins();
}

unsigned int v9x_run_d3d_raster_tests(void)
{
    test_rgb565_packing();
    test_target_validation();
    test_refuses_coordinates_it_cannot_carry();
    test_flat_triangle_is_one_colour();
    test_shared_edge_is_covered_exactly_once();
    test_full_target_triangle_stays_inside();
    test_degenerate_triangles_draw_nothing();
    test_vertex_order_does_not_matter();
    test_gouraud_ramps_across_a_span();
    return raster_failures;
}
