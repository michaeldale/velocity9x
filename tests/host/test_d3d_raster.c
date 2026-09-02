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

static v9x_u16 raster_depth_cells[RASTER_CELLS];

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

/*
 * A depth buffer laid out like the colour one, cleared to a chosen value.
 *
 * Its own guard margins matter as much as the colour buffer's: a depth write
 * that runs off the end of a row corrupts the next scanline's depths, which
 * shows up later as geometry disappearing rather than as a depth bug.
 */
static void raster_depth_reset(V9X_D3D_RASTER_DEPTH *depth,
                               v9x_u32 compare, v9x_u32 write, v9x_u16 clear)
{
    unsigned int index;

    for (index = 0u; index < RASTER_CELLS; ++index) {
        raster_depth_cells[index] = clear;
    }
    depth->pixels = &raster_depth_cells[RASTER_GUARD];
    depth->pitch = RASTER_STRIDE * 2ul;
    depth->compare = compare;
    depth->write = write;
}

static v9x_u16 raster_depth_at(unsigned int x, unsigned int y)
{
    return raster_depth_cells[RASTER_GUARD + y * RASTER_STRIDE + x];
}

static v9x_u16 raster_pixel(unsigned int x, unsigned int y)
{
    return raster_cells[RASTER_GUARD + y * RASTER_STRIDE + x];
}

/*
 * Nothing outside the declared extent was written: not the guard margins, not
 * the padding between the width and the pitch.
 */
/* The same check against a chosen background, for the tests that fill the
 * whole buffer rather than reset it. */
static void raster_check_untouched_margins_value(v9x_u16 background)
{
    unsigned int index;
    unsigned int row;
    unsigned int column;

    for (index = 0u; index < RASTER_GUARD; ++index) {
        RCHECK(raster_cells[index] == background);
        RCHECK(raster_cells[RASTER_CELLS - 1u - index] == background);
    }
    for (row = 0u; row < RASTER_HEIGHT; ++row) {
        for (column = RASTER_WIDTH; column < RASTER_STRIDE; ++column) {
            RCHECK(raster_pixel(column, row) == background);
        }
    }
}

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
    vertex->z = 0l;
    vertex->u = 0l;
    vertex->v = 0l;
    vertex->red = red;
    vertex->green = green;
    vertex->blue = blue;
    /* Opaque unless a test says otherwise, so every draw that predates
     * blending keeps meaning what it meant. */
    vertex->alpha = 255l;
}

/* The depth tests want the same vertex with a depth on it. Kept separate so
 * the twenty-odd draws that predate depth stay readable. */
static void raster_vertex_z(V9X_D3D_RASTER_VERTEX *vertex,
                            v9x_s32 x, v9x_s32 y, v9x_s32 z,
                            v9x_s32 red, v9x_s32 green, v9x_s32 blue)
{
    raster_vertex(vertex, x, y, red, green, blue);
    vertex->z = z;
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
    RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, triangle) != 0);
    RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, 0) == 0);

    /* The caller clips and clamps. A refusal here means it did not, and
     * drawing anyway would write outside a surface that is also the desktop. */
    for (index = 0u; index < 3u; ++index) {
        v9x_s32 saved;
        unsigned int cell;

        raster_reset(&target);
        saved = triangle[index].x;
        triangle[index].x = -1l;
        RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, triangle) == 0);
        triangle[index].x = V9X_D3D_RASTER_COORD_MAX + 1l;
        RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, triangle) == 0);
        triangle[index].x = saved;

        saved = triangle[index].y;
        triangle[index].y = -1l;
        RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, triangle) == 0);
        triangle[index].y = V9X_D3D_RASTER_COORD_MAX + 1l;
        RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, triangle) == 0);
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
    RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, triangle) != 0);

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
    RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, triangle) != 0);
    for (row = 0u; row < RASTER_HEIGHT; ++row) {
        for (column = 0u; column < RASTER_WIDTH; ++column) {
            first[row][column] = raster_pixel(column, row);
        }
    }

    raster_reset(&target);
    raster_vertex(&triangle[0], PX(10), PX(2), 255l, 255l, 255l);
    raster_vertex(&triangle[1], PX(10), PX(8), 255l, 255l, 255l);
    raster_vertex(&triangle[2], PX(2), PX(8), 255l, 255l, 255l);
    RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, triangle) != 0);

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
    RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, triangle) != 0);

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
        RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, triangle) != 0);

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
        RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, permuted) != 0);

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
    RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, triangle) != 0);

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

/*
 * A quad covering the sample point, at a chosen depth and colour.
 *
 * Two triangles rather than one, so the shared diagonal runs through the
 * region every depth test reads and every one of them would notice a seam
 * appearing under depth testing that is not there without it.
 */
static int raster_depth_quad(V9X_D3D_RASTER_TARGET *target,
                             const V9X_D3D_RASTER_DEPTH *depth,
                             v9x_s32 z, v9x_s32 red, v9x_s32 green,
                             v9x_s32 blue)
{
    V9X_D3D_RASTER_VERTEX triangle[3];
    int ok;

    raster_vertex_z(&triangle[0], PX(4), PX(4), z, red, green, blue);
    raster_vertex_z(&triangle[1], PX(28), PX(4), z, red, green, blue);
    raster_vertex_z(&triangle[2], PX(4), PX(20), z, red, green, blue);
    ok = v9x_d3d_raster_triangle(target, depth, 0, 0, triangle) != 0;

    raster_vertex_z(&triangle[0], PX(28), PX(4), z, red, green, blue);
    raster_vertex_z(&triangle[1], PX(28), PX(20), z, red, green, blue);
    raster_vertex_z(&triangle[2], PX(4), PX(20), z, red, green, blue);
    return ok && v9x_d3d_raster_triangle(target, depth, 0, 0, triangle) != 0;
}

/*
 * All eight comparison functions, against a buffer cleared to a known depth.
 *
 * The table is the point. Six of the eight names would still "work" under a
 * transposed encoding - LESS and GREATER are symmetric, and a scene rendered
 * with the two swapped is inside out rather than blank - so each is asserted
 * from both sides: a fragment nearer than the stored depth, and one further.
 * The ViRGE engine needs a translation table for exactly this reason and got
 * its order from the DDK rather than from arithmetic.
 */
static void test_depth_comparisons(void)
{
    static const struct {
        v9x_u32 compare;
        int nearer_passes;
        int equal_passes;
        int further_passes;
        const char *name;
    } cases[] = {
        { V9X_D3D_RASTER_CMP_NEVER,        0, 0, 0, "NEVER" },
        { V9X_D3D_RASTER_CMP_LESS,         1, 0, 0, "LESS" },
        { V9X_D3D_RASTER_CMP_EQUAL,        0, 1, 0, "EQUAL" },
        { V9X_D3D_RASTER_CMP_LESSEQUAL,    1, 1, 0, "LESSEQUAL" },
        { V9X_D3D_RASTER_CMP_GREATER,      0, 0, 1, "GREATER" },
        { V9X_D3D_RASTER_CMP_NOTEQUAL,     1, 0, 1, "NOTEQUAL" },
        { V9X_D3D_RASTER_CMP_GREATEREQUAL, 0, 1, 1, "GREATEREQUAL" },
        { V9X_D3D_RASTER_CMP_ALWAYS,       1, 1, 1, "ALWAYS" }
    };
    static const v9x_s32 depths[3] = { 8000l, 16384l, 30000l };
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_DEPTH depth;
    unsigned int index;
    unsigned int rung;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        for (rung = 0u; rung < 3u; ++rung) {
            int expected = rung == 0u ? cases[index].nearer_passes
                         : (rung == 1u ? cases[index].equal_passes
                                       : cases[index].further_passes);
            v9x_u16 value;

            raster_reset(&target);
            raster_depth_reset(&depth, cases[index].compare, 0ul, 16384u);
            RCHECK(raster_depth_quad(&target, &depth, depths[rung],
                                     255l, 255l, 255l) != 0);

            value = raster_pixel(16u, 12u);
            if ((value != RASTER_BACKGROUND) != expected) {
                printf("FAIL %s:%u: %s at depth %ld %s, expected %s\n",
                       __FILE__, (unsigned int)__LINE__, cases[index].name,
                       depths[rung],
                       value != RASTER_BACKGROUND ? "drew" : "did not draw",
                       expected ? "to draw" : "not to draw");
                ++raster_failures;
            }
            /* write is zero throughout, so a passing fragment must not have
             * touched the buffer. A rasterizer that always writes still
             * passes every comparison test above. */
            RCHECK(raster_depth_at(16u, 12u) == 16384u);
        }
    }
    raster_check_untouched_margins();
}

/*
 * The write mask, which is a separate render state from the comparison and is
 * separately capable of doing nothing.
 *
 * Two draws: a near one that passes and either records itself or does not,
 * then a middle one whose fate says which happened. With writes on, the near
 * depth is stored and the middle draw is rejected; with writes off, the buffer
 * still holds the far clear value and the middle draw lands.
 */
static void test_depth_write_mask(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_DEPTH depth;

    raster_reset(&target);
    raster_depth_reset(&depth, V9X_D3D_RASTER_CMP_LESS, 1ul, 65535u);
    RCHECK(raster_depth_quad(&target, &depth, 4000l, 255l, 0l, 0l) != 0);
    RCHECK(raster_depth_at(16u, 12u) == 4000u);
    RCHECK(raster_pixel(16u, 12u) == 0xf800u);
    RCHECK(raster_depth_quad(&target, &depth, 20000l, 0l, 255l, 0l) != 0);
    RCHECK(raster_depth_at(16u, 12u) == 4000u);
    RCHECK(raster_pixel(16u, 12u) == 0xf800u);

    raster_reset(&target);
    raster_depth_reset(&depth, V9X_D3D_RASTER_CMP_LESS, 0ul, 65535u);
    RCHECK(raster_depth_quad(&target, &depth, 4000l, 255l, 0l, 0l) != 0);
    RCHECK(raster_depth_at(16u, 12u) == 65535u);
    RCHECK(raster_depth_quad(&target, &depth, 20000l, 0l, 255l, 0l) != 0);
    /* Nothing was recorded, so the further quad is still nearer than the
     * clear value and overwrites the colour. */
    RCHECK(raster_depth_at(16u, 12u) == 65535u);
    RCHECK(raster_pixel(16u, 12u) == 0x07e0u);
    raster_check_untouched_margins();
}

/*
 * Order independence: the nearer surface wins whichever way round it is drawn.
 *
 * This is the property a depth buffer exists for, and the one the driver
 * previously advertised for weeks without having. It is deliberately not the
 * same assertion as the comparison table above - that table checks the eight
 * functions, this checks that the result does not depend on submission order,
 * which is what an engine that accepts depth and ignores it would fail.
 */
static void test_depth_orders_two_surfaces(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_DEPTH depth;

    raster_reset(&target);
    raster_depth_reset(&depth, V9X_D3D_RASTER_CMP_LESS, 1ul, 65535u);
    RCHECK(raster_depth_quad(&target, &depth, 10000l, 255l, 0l, 0l) != 0);
    RCHECK(raster_depth_quad(&target, &depth, 40000l, 0l, 255l, 0l) != 0);
    RCHECK(raster_pixel(16u, 12u) == 0xf800u);

    raster_reset(&target);
    raster_depth_reset(&depth, V9X_D3D_RASTER_CMP_LESS, 1ul, 65535u);
    RCHECK(raster_depth_quad(&target, &depth, 40000l, 0l, 255l, 0l) != 0);
    RCHECK(raster_depth_quad(&target, &depth, 10000l, 255l, 0l, 0l) != 0);
    RCHECK(raster_pixel(16u, 12u) == 0xf800u);
    raster_check_untouched_margins();
}

/*
 * Depth interpolates across the triangle, and a sloped surface intersects a
 * flat one.
 *
 * A single flat quad cannot tell an interpolated depth from a constant one -
 * the whole surface would pass or fail together. This draws a quad whose depth
 * ramps left to right and then a flat one halfway through that range, and
 * checks the flat quad wins on exactly the side where it is nearer.
 */
static void test_depth_interpolates_across_a_triangle(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_DEPTH depth;
    V9X_D3D_RASTER_VERTEX triangle[3];
    unsigned int column;
    unsigned int crossings = 0u;
    int previous = -1;

    raster_reset(&target);
    raster_depth_reset(&depth, V9X_D3D_RASTER_CMP_LESS, 1ul, 65535u);

    /* A ramp from near on the left to far on the right, over the same region
     * raster_depth_quad covers. */
    raster_vertex_z(&triangle[0], PX(4), PX(4), 1000l, 255l, 0l, 0l);
    raster_vertex_z(&triangle[1], PX(28), PX(4), 60000l, 255l, 0l, 0l);
    raster_vertex_z(&triangle[2], PX(4), PX(20), 1000l, 255l, 0l, 0l);
    RCHECK(v9x_d3d_raster_triangle(&target, &depth, 0, 0, triangle) != 0);
    raster_vertex_z(&triangle[0], PX(28), PX(4), 60000l, 255l, 0l, 0l);
    raster_vertex_z(&triangle[1], PX(28), PX(20), 60000l, 255l, 0l, 0l);
    raster_vertex_z(&triangle[2], PX(4), PX(20), 1000l, 255l, 0l, 0l);
    RCHECK(v9x_d3d_raster_triangle(&target, &depth, 0, 0, triangle) != 0);

    /* Row 5 sits inside the first triangle, above the diagonal, so its stored
     * depths come from the ramp and must increase left to right. */
    for (column = 5u; column < 27u; ++column) {
        int stored = (int)raster_depth_at(column, 5u);

        RCHECK(stored > previous);
        previous = stored;
    }
    RCHECK(previous > 40000);

    /* Now a flat surface at the middle of that range. It must win on the far
     * side of the ramp and lose on the near side, so the row changes colour
     * exactly once. */
    RCHECK(raster_depth_quad(&target, &depth, 30000l, 0l, 255l, 0l) != 0);
    previous = -1;
    for (column = 5u; column < 27u; ++column) {
        int green = raster_pixel(column, 5u) == 0x07e0u;

        if (previous >= 0 && green != previous) {
            ++crossings;
        }
        previous = green;
    }
    RCHECK(crossings == 1u);
    raster_check_untouched_margins();
}

/*
 * The depth buffer's own refusals, and that a refusal draws nothing.
 *
 * A null depth pointer means "no depth" and must keep working; a non-null one
 * that is unusable must be refused rather than quietly ignored. Those two
 * cannot be allowed to look alike: silently dropping the test renders the
 * scene in submission order, which is the exact defect this driver shipped
 * once with the capability advertised.
 */
static void test_depth_refusals(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_DEPTH depth;
    V9X_D3D_RASTER_VERTEX triangle[3];

    raster_reset(&target);
    raster_depth_reset(&depth, V9X_D3D_RASTER_CMP_ALWAYS, 1ul, 65535u);
    RCHECK(v9x_d3d_raster_depth_valid(&depth, &target) != 0);
    RCHECK(v9x_d3d_raster_depth_valid(0, &target) == 0);

    depth.pixels = 0;
    RCHECK(v9x_d3d_raster_depth_valid(&depth, &target) == 0);

    raster_depth_reset(&depth, V9X_D3D_RASTER_CMP_ALWAYS, 1ul, 65535u);
    depth.pitch = RASTER_WIDTH * 2ul;
    RCHECK(v9x_d3d_raster_depth_valid(&depth, &target) != 0);
    depth.pitch = RASTER_WIDTH * 2ul - 1ul;
    RCHECK(v9x_d3d_raster_depth_valid(&depth, &target) == 0);

    /* A refused depth buffer refuses the draw, and the draw writes nothing. */
    raster_vertex_z(&triangle[0], PX(4), PX(4), 100l, 255l, 255l, 255l);
    raster_vertex_z(&triangle[1], PX(28), PX(4), 100l, 255l, 255l, 255l);
    raster_vertex_z(&triangle[2], PX(4), PX(20), 100l, 255l, 255l, 255l);
    RCHECK(v9x_d3d_raster_triangle(&target, &depth, 0, 0, triangle) == 0);
    RCHECK(raster_pixel(10u, 8u) == RASTER_BACKGROUND);

    /* An out-of-range depth is refused the same way a coordinate is. */
    raster_depth_reset(&depth, V9X_D3D_RASTER_CMP_ALWAYS, 1ul, 65535u);
    triangle[1].z = V9X_D3D_RASTER_DEPTH_MAX + 1l;
    RCHECK(v9x_d3d_raster_triangle(&target, &depth, 0, 0, triangle) == 0);
    triangle[1].z = -1l;
    RCHECK(v9x_d3d_raster_triangle(&target, &depth, 0, 0, triangle) == 0);
    RCHECK(raster_pixel(10u, 8u) == RASTER_BACKGROUND);

    /* And the same triangle with no depth buffer at all still draws. This one
     * is a single triangle rather than the quad the tests above use, so the
     * sample sits at (10,8) - well inside it - and not at the quad's centre,
     * which is on the far side of this triangle's hypotenuse. */
    triangle[1].z = 100l;
    RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, 0, triangle) != 0);
    RCHECK(raster_pixel(10u, 8u) == 0xffffu);
    raster_check_untouched_margins();
}

/*
 * The worst case for the edge interpolator's headroom, drawn rather than
 * argued about.
 *
 * The lerp forms max(from, to) * denominator, and depth is the widest thing it
 * carries: 65535 against a y-span of 32752 subpixels is 2,146,631,520, which
 * is 852,127 short of overflowing a signed 32-bit integer. That margin is why
 * V9X_D3D_RASTER_DIMENSION_MAX is 2048. A tall, narrow target reaches it
 * without a four-megabyte buffer: the denominator is the triangle's height in
 * subpixels and does not care how wide it is.
 *
 * An overflow here does not produce a slightly wrong depth. It produces a
 * negative one, which clamps to the near plane, so the far end of the ramp
 * would come out nearer than the near end - hence checking monotonicity over
 * the whole height rather than the endpoints alone.
 */
#define RASTER_TALL_WIDTH  4u
#define RASTER_TALL_HEIGHT 2048u

static v9x_u16 raster_tall_colour[RASTER_TALL_WIDTH * RASTER_TALL_HEIGHT];
static v9x_u16 raster_tall_depth[RASTER_TALL_WIDTH * RASTER_TALL_HEIGHT];

static void test_depth_full_height_interpolation(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_DEPTH depth;
    V9X_D3D_RASTER_VERTEX triangle[3];
    unsigned int index;
    unsigned int row;
    unsigned int covered = 0u;
    unsigned int uncovered = 0u;
    int previous = -1;
    int lowest = 65536;
    int highest = -1;

    for (index = 0u; index < RASTER_TALL_WIDTH * RASTER_TALL_HEIGHT; ++index) {
        raster_tall_colour[index] = 0u;
        raster_tall_depth[index] = 65535u;
    }
    target.pixels = raster_tall_colour;
    target.pitch = RASTER_TALL_WIDTH * 2ul;
    target.width = RASTER_TALL_WIDTH;
    target.height = RASTER_TALL_HEIGHT;
    depth.pixels = raster_tall_depth;
    depth.pitch = RASTER_TALL_WIDTH * 2ul;
    depth.compare = V9X_D3D_RASTER_CMP_LESS;
    depth.write = 1ul;

    /*
     * The full declared range in both y and z, as a quad rather than one
     * triangle. A single triangle spanning corner to corner narrows to a point
     * and stops covering column 0 about seven eighths of the way down, so the
     * rows past that keep the clear value - and a test that reads them is
     * measuring the memset. That is not hypothetical; the first version of
     * this test passed its far-end assertion on exactly that.
     */
    raster_vertex_z(&triangle[0], PX(0), PX(0), 0l, 255l, 255l, 255l);
    raster_vertex_z(&triangle[1], PX(RASTER_TALL_WIDTH), PX(0), 0l,
                    255l, 255l, 255l);
    raster_vertex_z(&triangle[2], PX(0), PX(RASTER_TALL_HEIGHT - 1u),
                    V9X_D3D_RASTER_DEPTH_MAX, 255l, 255l, 255l);
    RCHECK(v9x_d3d_raster_triangle(&target, &depth, 0, 0, triangle) != 0);
    raster_vertex_z(&triangle[0], PX(RASTER_TALL_WIDTH), PX(0), 0l,
                    255l, 255l, 255l);
    raster_vertex_z(&triangle[1], PX(RASTER_TALL_WIDTH),
                    PX(RASTER_TALL_HEIGHT - 1u), V9X_D3D_RASTER_DEPTH_MAX,
                    255l, 255l, 255l);
    raster_vertex_z(&triangle[2], PX(0), PX(RASTER_TALL_HEIGHT - 1u),
                    V9X_D3D_RASTER_DEPTH_MAX, 255l, 255l, 255l);
    RCHECK(v9x_d3d_raster_triangle(&target, &depth, 0, 0, triangle) != 0);

    for (row = 0u; row < RASTER_TALL_HEIGHT; ++row) {
        int stored = (int)raster_tall_depth[row * RASTER_TALL_WIDTH];

        if (raster_tall_colour[row * RASTER_TALL_WIDTH] == 0u) {
            /* Not covered. Counted rather than skipped quietly: every row of
             * this quad should be, and a test that tolerates gaps would also
             * tolerate the rasterizer dropping the bottom of the triangle. */
            ++uncovered;
            continue;
        }
        ++covered;
        if (stored < previous) {
            printf("FAIL %s:%u: depth at row %u is %d, below row %u's %d\n",
                   __FILE__, (unsigned int)__LINE__, row, stored, row - 1u,
                   previous);
            ++raster_failures;
            break;
        }
        previous = stored;
        if (stored < lowest) {
            lowest = stored;
        }
        if (stored > highest) {
            highest = stored;
        }
    }

    /*
     * Every row but the last, and the last one is arithmetic rather than a
     * gap. The largest coordinate this rasterizer accepts is
     * (dimension - 1) << 4 - the bottom row's top edge, not its bottom - so
     * that row's centre lies half a pixel past the furthest geometry anything
     * can express. The D3D core's clipper cuts to exactly the same boundary,
     * `context->height - 1` (d3d_core.c), so this is not a limit the tests
     * invented: through the driver, the bottom scanline and the rightmost
     * column of a full-screen triangle are never covered either.
     *
     * Whether that is right is an open question about the clipper, which is
     * chip-neutral and shared with the ViRGE engine, so it is not one the
     * software rasterizer should answer by widening its own range. Pinned here
     * as a number so that changing it is a deliberate act with a failing test
     * attached.
     */
    RCHECK(uncovered == 1u);
    RCHECK(covered == RASTER_TALL_HEIGHT - 1u);
    /* The first scanline's centre is half a pixel into a 2047-pixel ramp, so
     * the near end is a few levels above zero rather than at it. What matters
     * is that it is near: an interpolator that overflowed would land at the
     * clamp, and the clamp is zero. */
    RCHECK(lowest > 0 && lowest < 64);
    RCHECK(highest > 65400);
}

/*
 * A 4x4 texture, its own guard margins either side.
 *
 * Four by four so that one texel covers a whole block of the 32x24 target -
 * eight columns and six rows - which makes "which texel did it sample" a
 * question about a block of pixels rather than about one, and makes an
 * off-by-one in the texel index show up as a shifted boundary rather than as
 * a single wrong pixel that could be rounding.
 */
#define RASTER_TEX_SIZE  4u
#define RASTER_TEX_GUARD 16u
#define RASTER_TEX_CELLS (RASTER_TEX_GUARD * 2u + RASTER_TEX_SIZE * RASTER_TEX_SIZE)

static v9x_u16 raster_texture_cells[RASTER_TEX_CELLS];

static void raster_texture_reset(V9X_D3D_RASTER_TEXTURE *texture,
                                 v9x_u32 format, v9x_u32 filter,
                                 v9x_u32 blend)
{
    unsigned int index;

    for (index = 0u; index < RASTER_TEX_CELLS; ++index) {
        raster_texture_cells[index] = 0u;
    }
    texture->pixels = &raster_texture_cells[RASTER_TEX_GUARD];
    texture->pitch = RASTER_TEX_SIZE * 2ul;
    texture->size = RASTER_TEX_SIZE;
    texture->format = format;
    texture->filter = filter;
    texture->blend = blend;
    /* CLAMP unless a test says otherwise, so every draw that predates the
     * address mode keeps meaning what it meant: those tests all use
     * coordinates inside the first repeat, where the two modes agree, but
     * saying so is cheaper than checking. */
    texture->address = V9X_D3D_RASTER_ADDRESS_CLAMP;
}

static void raster_texel_set(unsigned int x, unsigned int y, v9x_u16 value)
{
    raster_texture_cells[RASTER_TEX_GUARD + y * RASTER_TEX_SIZE + x] = value;
}

static void raster_texture_check_margins(void)
{
    unsigned int index;

    for (index = 0u; index < RASTER_TEX_GUARD; ++index) {
        RCHECK(raster_texture_cells[index] == 0u);
        RCHECK(raster_texture_cells[RASTER_TEX_CELLS - 1u - index] == 0u);
    }
}

/*
 * A quad over the whole target with texture coordinates spanning it exactly.
 *
 * Two triangles, so the shared diagonal runs through the middle of the sampled
 * region: a texture coordinate interpolated inconsistently between the two
 * halves shows up as a visible break along it rather than as a small error
 * everywhere.
 */
static int raster_textured_quad(V9X_D3D_RASTER_TARGET *target,
                                const V9X_D3D_RASTER_TEXTURE *texture,
                                v9x_s32 red, v9x_s32 green, v9x_s32 blue)
{
    V9X_D3D_RASTER_VERTEX triangle[3];
    /* One whole repeat, which is what these tests mean by "the texture".
     * TEXCOORD_MAX is thirty-three of them and would tile the quad. */
    v9x_s32 edge = V9X_D3D_RASTER_TEXCOORD_ONE - 1l;
    int ok;

    raster_vertex(&triangle[0], PX(0), PX(0), red, green, blue);
    raster_vertex(&triangle[1], PX(RASTER_WIDTH), PX(0), red, green, blue);
    triangle[1].u = edge;
    raster_vertex(&triangle[2], PX(0), PX(RASTER_HEIGHT), red, green, blue);
    triangle[2].v = edge;
    ok = v9x_d3d_raster_triangle(target, 0, texture, 0, triangle) != 0;

    raster_vertex(&triangle[0], PX(RASTER_WIDTH), PX(0), red, green, blue);
    triangle[0].u = edge;
    raster_vertex(&triangle[1], PX(RASTER_WIDTH), PX(RASTER_HEIGHT),
                  red, green, blue);
    triangle[1].u = edge;
    triangle[1].v = edge;
    raster_vertex(&triangle[2], PX(0), PX(RASTER_HEIGHT), red, green, blue);
    triangle[2].v = edge;
    return ok && v9x_d3d_raster_triangle(target, 0, texture, 0, triangle) != 0;
}

static void test_texture_validation(void)
{
    V9X_D3D_RASTER_TEXTURE texture;

    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    RCHECK(v9x_d3d_raster_texture_valid(&texture) != 0);
    RCHECK(v9x_d3d_raster_texture_valid(0) == 0);

    texture.pixels = 0;
    RCHECK(v9x_d3d_raster_texture_valid(&texture) == 0);

    raster_texture_reset(&texture, 0ul, V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    RCHECK(v9x_d3d_raster_texture_valid(&texture) == 0);
    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB4444,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    RCHECK(v9x_d3d_raster_texture_valid(&texture) != 0);

    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555, 99ul,
                         V9X_D3D_RASTER_BLEND_DECAL);
    RCHECK(v9x_d3d_raster_texture_valid(&texture) == 0);
    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555,
                         V9X_D3D_RASTER_FILTER_LINEAR, 99ul);
    RCHECK(v9x_d3d_raster_texture_valid(&texture) == 0);

    /*
     * Power of two, and this is the refusal that matters most: the sampler
     * wraps its texel index with size - 1 as a mask, so a size of 6 would mask
     * to 0..5 with 5 unreachable and 6, 7 indexing rows that are not there.
     * A wrong-looking texture would be the good outcome.
     */
    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    texture.size = 6ul;
    RCHECK(v9x_d3d_raster_texture_valid(&texture) == 0);
    texture.size = 8ul;
    texture.pitch = 16ul;
    RCHECK(v9x_d3d_raster_texture_valid(&texture) != 0);

    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    texture.size = V9X_D3D_RASTER_TEXTURE_SIZE_MIN / 2ul;
    RCHECK(v9x_d3d_raster_texture_valid(&texture) == 0);
    texture.size = V9X_D3D_RASTER_TEXTURE_SIZE_MAX * 2ul;
    RCHECK(v9x_d3d_raster_texture_valid(&texture) == 0);

    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    texture.pitch = RASTER_TEX_SIZE * 2ul - 1ul;
    RCHECK(v9x_d3d_raster_texture_valid(&texture) == 0);
}

/*
 * Point sampling puts the right texel under the right block of pixels.
 *
 * Four corner texels, four distinct colours, everything else black. One texel
 * is eight columns by six rows of the target, so each assertion below is about
 * a pixel well inside its block rather than at a boundary where rounding could
 * legitimately go either way.
 */
static void test_texture_point_sampling(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_TEXTURE texture;

    raster_reset(&target);
    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    raster_texel_set(0u, 0u, 0x7c00u);   /* red */
    raster_texel_set(3u, 0u, 0x03e0u);   /* green */
    raster_texel_set(0u, 3u, 0x001fu);   /* blue */
    raster_texel_set(3u, 3u, 0x7fffu);   /* white */

    RCHECK(raster_textured_quad(&target, &texture, 255l, 255l, 255l) != 0);

    /* Five bits of 31 expand to a full 255, so a saturated 1555 channel comes
     * out saturated in 565 rather than one level short. */
    RCHECK(raster_pixel(2u, 2u) == 0xf800u);
    RCHECK(raster_pixel(29u, 2u) == 0x07e0u);
    RCHECK(raster_pixel(2u, 21u) == 0x001fu);
    RCHECK(raster_pixel(29u, 21u) == 0xffffu);
    /* An interior texel is black, which is also what an unsampled pixel would
     * be - so the four above are what carry this test. */
    RCHECK(raster_pixel(12u, 8u) == 0x0000u);
    raster_check_untouched_margins();
    raster_texture_check_margins();
}


/*
 * WRAP tiles the texture; CLAMP stretches its edge.
 *
 * The texture is a single bright texel at the origin on black, so the drawn
 * quad shows exactly where the coordinate landed: under WRAP with a four-
 * repeat span the bright column appears four times across the row, under CLAMP
 * once. Counting the bright runs rather than checking named pixels, because
 * the exact columns depend on the quad's geometry and the count does not.
 */
static void test_texture_wrap_tiles(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_TEXTURE texture;
    V9X_D3D_RASTER_VERTEX triangle[3];
    unsigned int column;
    unsigned int runs;
    unsigned int previous;
    unsigned int y;
    unsigned int x;
    v9x_s32 span = V9X_D3D_RASTER_TEXCOORD_ONE * 4l;

    raster_reset(&target);
    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    for (y = 0u; y < RASTER_TEX_SIZE; ++y) {
        for (x = 0u; x < RASTER_TEX_SIZE; ++x) {
            raster_texel_set(x, y, x == 0u ? 0x7fffu : 0x0000u);
        }
    }
    texture.address = V9X_D3D_RASTER_ADDRESS_WRAP;

    /* A band four pixels tall so the sampled row is well inside it. */
    raster_vertex(&triangle[0], PX(0), PX(4), 255l, 255l, 255l);
    raster_vertex(&triangle[1], PX(32), PX(4), 255l, 255l, 255l);
    raster_vertex(&triangle[2], PX(0), PX(12), 255l, 255l, 255l);
    triangle[1].u = span;
    RCHECK(v9x_d3d_raster_triangle(&target, 0, &texture, 0, triangle) != 0);
    raster_vertex(&triangle[0], PX(32), PX(4), 255l, 255l, 255l);
    raster_vertex(&triangle[1], PX(32), PX(12), 255l, 255l, 255l);
    raster_vertex(&triangle[2], PX(0), PX(12), 255l, 255l, 255l);
    triangle[0].u = span;
    triangle[1].u = span;
    RCHECK(v9x_d3d_raster_triangle(&target, 0, &texture, 0, triangle) != 0);

    runs = 0u;
    previous = 0u;
    for (column = 0u; column < RASTER_WIDTH; ++column) {
        unsigned int bright = raster_pixel(column, 6u) != 0x0000u ? 1u : 0u;

        if (bright != 0u && previous == 0u) {
            ++runs;
        }
        previous = bright;
    }
    RCHECK(runs == 4u);

    /* The same draw under CLAMP reaches the bright column once, at the left,
     * and then holds the last texel for the rest of the row. */
    raster_reset(&target);
    texture.address = V9X_D3D_RASTER_ADDRESS_CLAMP;
    raster_vertex(&triangle[0], PX(0), PX(4), 255l, 255l, 255l);
    raster_vertex(&triangle[1], PX(32), PX(4), 255l, 255l, 255l);
    raster_vertex(&triangle[2], PX(0), PX(12), 255l, 255l, 255l);
    triangle[1].u = span;
    RCHECK(v9x_d3d_raster_triangle(&target, 0, &texture, 0, triangle) != 0);
    raster_vertex(&triangle[0], PX(32), PX(4), 255l, 255l, 255l);
    raster_vertex(&triangle[1], PX(32), PX(12), 255l, 255l, 255l);
    raster_vertex(&triangle[2], PX(0), PX(12), 255l, 255l, 255l);
    triangle[0].u = span;
    triangle[1].u = span;
    RCHECK(v9x_d3d_raster_triangle(&target, 0, &texture, 0, triangle) != 0);

    runs = 0u;
    previous = 0u;
    for (column = 0u; column < RASTER_WIDTH; ++column) {
        unsigned int bright = raster_pixel(column, 6u) != 0x0000u ? 1u : 0u;

        if (bright != 0u && previous == 0u) {
            ++runs;
        }
        previous = bright;
    }
    RCHECK(runs == 1u);

    /*
     * And it is at the left, with the rest of the row holding the texture's
     * last texel, which here is black.
     *
     * Counting runs alone does not say that. A clamp that clamped to
     * TEXCOORD_MAX instead of to the texture's edge leaves the coordinate
     * thirty-three repeats out, where the sampler's mask brings it back to
     * texel zero - one bright run again, covering the whole row. That
     * mutation passed the count and fails these, which is why they are here.
     */
    RCHECK(raster_pixel(0u, 6u) != 0x0000u);
    RCHECK(raster_pixel(16u, 6u) == 0x0000u);
    RCHECK(raster_pixel(RASTER_WIDTH - 1u, 6u) == 0x0000u);
    raster_check_untouched_margins();
    raster_texture_check_margins();
}

/*
 * The interpolator rounds down on a falling gradient as well as a rising one.
 *
 * v9x_d3d_raster_lerp divides before it multiplies so that a texture
 * coordinate can leave the first repeat, and the form it uses would truncate
 * toward zero - which is the same as rounding down while the delta is
 * positive and one level different once it is negative. C89 leaves the
 * direction implementation defined for both / and %, so the function corrects
 * the quotient explicitly, and this pins the corrected value.
 *
 * Depth carries it rather than colour, because depth is read back at its full
 * sixteen bits where a colour channel is quantised to five on the way out and
 * a one-level difference disappears. The quad's z depends only on y, so the
 * span interpolator's step is zero and what lands in the buffer is the edge
 * interpolator's answer unmodified.
 *
 * At row 5 the edge runs from z = 1001 at y = 2 to z = 0 at y = 22, so the
 * sample is 56 subpixels into 320: rounding down gives 825 and truncating
 * toward zero gives 826.
 */
static void test_lerp_rounds_down_descending(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_DEPTH depth;
    V9X_D3D_RASTER_VERTEX triangle[3];

    raster_reset(&target);
    raster_depth_reset(&depth, V9X_D3D_RASTER_CMP_ALWAYS, 1ul, 0xffffu);

    raster_vertex_z(&triangle[0], PX(2), PX(2), 1001l, 255l, 255l, 255l);
    raster_vertex_z(&triangle[1], PX(30), PX(2), 1001l, 255l, 255l, 255l);
    raster_vertex_z(&triangle[2], PX(2), PX(22), 0l, 255l, 255l, 255l);
    RCHECK(v9x_d3d_raster_triangle(&target, &depth, 0, 0, triangle) != 0);

    RCHECK(raster_depth_at(10u, 5u) == 825u);
}

/*
 * A coordinate at the top of the range is still sampled safely.
 *
 * V9X_D3D_RASTER_TEXCOORD_MAX times the largest texture edge is the widest
 * product the sampler forms, and the bilinear arm adds a whole texture of bias
 * on top of it. This draws at that corner with the linear filter, which is the
 * arm with the larger intermediate, and requires that the triangle is accepted
 * and that what comes back is a texel from the texture rather than whatever
 * a wrapped index found outside it - the texture is uniform, so any texel but
 * the right one is a different colour.
 */
static void test_texture_wrap_extreme_coordinate(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_TEXTURE texture;
    V9X_D3D_RASTER_VERTEX triangle[3];
    unsigned int y;
    unsigned int x;

    raster_reset(&target);
    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555,
                         V9X_D3D_RASTER_FILTER_LINEAR,
                         V9X_D3D_RASTER_BLEND_DECAL);
    for (y = 0u; y < RASTER_TEX_SIZE; ++y) {
        for (x = 0u; x < RASTER_TEX_SIZE; ++x) {
            raster_texel_set(x, y, 0x03e0u);    /* uniform green */
        }
    }
    texture.address = V9X_D3D_RASTER_ADDRESS_WRAP;

    raster_vertex(&triangle[0], PX(2), PX(2), 255l, 255l, 255l);
    raster_vertex(&triangle[1], PX(28), PX(2), 255l, 255l, 255l);
    raster_vertex(&triangle[2], PX(2), PX(20), 255l, 255l, 255l);
    triangle[0].u = V9X_D3D_RASTER_TEXCOORD_MAX;
    triangle[0].v = V9X_D3D_RASTER_TEXCOORD_MAX;
    triangle[1].u = V9X_D3D_RASTER_TEXCOORD_MAX;
    triangle[1].v = V9X_D3D_RASTER_TEXCOORD_MAX;
    triangle[2].u = V9X_D3D_RASTER_TEXCOORD_MAX;
    triangle[2].v = V9X_D3D_RASTER_TEXCOORD_MAX;
    RCHECK(v9x_d3d_raster_triangle(&target, 0, &texture, 0, triangle) != 0);
    RCHECK(raster_pixel(6u, 6u) == v9x_d3d_raster_rgb565(0l, 255l, 0l));

    /* One beyond it is refused, on either axis. */
    triangle[0].u = V9X_D3D_RASTER_TEXCOORD_MAX + 1l;
    RCHECK(v9x_d3d_raster_triangle(&target, 0, &texture, 0, triangle) == 0);
    triangle[0].u = V9X_D3D_RASTER_TEXCOORD_MAX;
    triangle[2].v = V9X_D3D_RASTER_TEXCOORD_MAX + 1l;
    RCHECK(v9x_d3d_raster_triangle(&target, 0, &texture, 0, triangle) == 0);
    raster_texture_check_margins();
}

/* An address mode the sampler does not implement is refused. MIRROR is a legal
 * D3DTADDRESS_* value and is not one of the two, so it stands for the class. */
static void test_texture_address_refusals(void)
{
    V9X_D3D_RASTER_TEXTURE texture;

    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    texture.address = V9X_D3D_RASTER_ADDRESS_WRAP;
    RCHECK(v9x_d3d_raster_texture_valid(&texture) != 0);
    texture.address = 2ul;      /* D3DTADDRESS_MIRROR */
    RCHECK(v9x_d3d_raster_texture_valid(&texture) == 0);
    texture.address = 0ul;
    RCHECK(v9x_d3d_raster_texture_valid(&texture) == 0);
}

/*
 * Fill the whole target - guard margins included - with one colour, so a
 * blend has a known destination to read.
 *
 * The margins get it too, deliberately: they are checked afterwards against
 * the same value, so a blend that read or wrote outside the extent is still
 * caught.
 */
static void raster_fill(v9x_u16 colour)
{
    unsigned int index;

    for (index = 0u; index < RASTER_CELLS; ++index) {
        raster_cells[index] = colour;
    }
}

/*
 * One flat quad, with a colour and an alpha, so the blend tests read an
 * interior pixel rather than an edge.
 */
static int raster_blended_quad(const V9X_D3D_RASTER_TARGET *target,
                               const V9X_D3D_RASTER_ALPHA *alpha,
                               v9x_s32 red, v9x_s32 green, v9x_s32 blue,
                               v9x_s32 alpha_value)
{
    V9X_D3D_RASTER_VERTEX triangle[3];
    int ok;

    raster_vertex(&triangle[0], PX(4), PX(4), red, green, blue);
    raster_vertex(&triangle[1], PX(24), PX(4), red, green, blue);
    raster_vertex(&triangle[2], PX(4), PX(20), red, green, blue);
    triangle[0].alpha = alpha_value;
    triangle[1].alpha = alpha_value;
    triangle[2].alpha = alpha_value;
    ok = v9x_d3d_raster_triangle(target, 0, 0, alpha, triangle) != 0;

    raster_vertex(&triangle[0], PX(24), PX(4), red, green, blue);
    raster_vertex(&triangle[1], PX(24), PX(20), red, green, blue);
    raster_vertex(&triangle[2], PX(4), PX(20), red, green, blue);
    triangle[0].alpha = alpha_value;
    triangle[1].alpha = alpha_value;
    triangle[2].alpha = alpha_value;
    return ok &&
           v9x_d3d_raster_triangle(target, 0, 0, alpha, triangle) != 0;
}

/*
 * Opaque means opaque: ONE with ZERO writes the source and reads nothing.
 *
 * Drawn over a saturated destination rather than over black, because a blend
 * that ignored its factors and averaged would land halfway and a blend that
 * dropped the destination term entirely would not - only a full-strength
 * destination separates the two.
 */
static void test_alpha_one_zero_replaces(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_ALPHA alpha;

    raster_reset(&target);
    raster_fill(0xffffu);       /* white */
    alpha.src = V9X_D3D_RASTER_BLEND_SRC_ONE;
    alpha.dst = V9X_D3D_RASTER_BLEND_DST_ZERO;
    RCHECK(raster_blended_quad(&target, &alpha, 255l, 0l, 0l, 0l) != 0);

    /* Alpha zero and still fully red, because neither factor consults it. */
    RCHECK(raster_pixel(12u, 10u) == 0xf800u);
}

/*
 * A fully opaque SRCALPHA blend is exactly the source, not one level short.
 *
 * This is the test for the 255-to-256 weight correction, and the colour it
 * uses is chosen to make that correction observable. A saturated channel
 * cannot show it: 255 * 255 + 255 * 1 is 65280, which is 255 after the shift
 * either way, so red-over-white passes with the correction removed. A middling
 * channel over black does show it - 200 * 256 is 200 and 200 * 255 is 199 -
 * and 200 and 199 differ in the fifth bit of the red field, so the packed
 * pixel differs too.
 *
 * The error the correction prevents is one level per channel on everything
 * blended, which reads as a grey haze rather than as a bug.
 */
static void test_alpha_full_is_exact(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_ALPHA alpha;

    raster_reset(&target);
    raster_fill(0x0000u);       /* black, so the destination term is zero */
    alpha.src = V9X_D3D_RASTER_BLEND_SRC_SRCALPHA;
    alpha.dst = V9X_D3D_RASTER_BLEND_DST_INVSRCALPHA;
    RCHECK(raster_blended_quad(&target, &alpha, 200l, 200l, 200l, 255l) != 0);
    RCHECK(raster_pixel(12u, 10u) ==
           v9x_d3d_raster_rgb565(200l, 200l, 200l));

    /* And with a saturated source over white, where the two arithmetics agree
     * - stated so that a later reader does not "simplify" the case above. */
    raster_reset(&target);
    raster_fill(0xffffu);
    RCHECK(raster_blended_quad(&target, &alpha, 255l, 0l, 0l, 255l) != 0);
    RCHECK(raster_pixel(12u, 10u) == 0xf800u);
}

/*
 * A fully transparent one leaves the destination untouched.
 *
 * The other end of the same range, and the one that catches a weight computed
 * with the wrong sign: the pixel has to come back bit for bit, not merely
 * close.
 */
static void test_alpha_zero_keeps_destination(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_ALPHA alpha;

    raster_reset(&target);
    raster_fill(0x001fu);       /* blue */
    alpha.src = V9X_D3D_RASTER_BLEND_SRC_SRCALPHA;
    alpha.dst = V9X_D3D_RASTER_BLEND_DST_INVSRCALPHA;
    RCHECK(raster_blended_quad(&target, &alpha, 255l, 255l, 255l, 0l) != 0);
    RCHECK(raster_pixel(12u, 10u) == 0x001fu);
}

/*
 * Half alpha lands near the middle in both channels.
 *
 * The same case the guest probe draws - 0x80ff0000, half-alpha red, over a
 * blue destination - so a disagreement between this test and the probe is a
 * disagreement about the driver rather than about the arithmetic. Checked as
 * a window rather than an exact value because the destination's blue arrives
 * through a five-bit field and leaves through one.
 */
static void test_alpha_half_blends(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_ALPHA alpha;
    v9x_u16 value;

    raster_reset(&target);
    raster_fill(0x001fu);
    alpha.src = V9X_D3D_RASTER_BLEND_SRC_SRCALPHA;
    alpha.dst = V9X_D3D_RASTER_BLEND_DST_INVSRCALPHA;
    RCHECK(raster_blended_quad(&target, &alpha, 255l, 0l, 0l, 128l) != 0);

    value = raster_pixel(12u, 10u);
    RCHECK((value >> 11) >= 13u && (value >> 11) <= 18u);       /* red */
    RCHECK(((value >> 5) & 0x3fu) <= 2u);                       /* green */
    RCHECK((value & 0x1fu) >= 13u && (value & 0x1fu) <= 18u);   /* blue */
    raster_check_untouched_margins_value(0x001fu);
}

/*
 * Alpha is interpolated across the triangle, not taken from one vertex.
 *
 * Opaque at the left edge and transparent at the right, over a blue
 * destination: the row has to run from red to blue rather than being one
 * colour throughout. A flat-alpha engine passes every test above and fails
 * this one, which is why it exists - ALPHAGOURAUDBLEND is a separate claim
 * from ALPHAFLATBLEND and describe_caps publishes both.
 */
static void test_alpha_gouraud_varies(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_ALPHA alpha;
    V9X_D3D_RASTER_VERTEX triangle[3];
    unsigned int left_red;
    unsigned int right_red;
    unsigned int middle_red;

    raster_reset(&target);
    raster_fill(0x001fu);
    alpha.src = V9X_D3D_RASTER_BLEND_SRC_SRCALPHA;
    alpha.dst = V9X_D3D_RASTER_BLEND_DST_INVSRCALPHA;

    raster_vertex(&triangle[0], PX(4), PX(4), 255l, 0l, 0l);
    raster_vertex(&triangle[1], PX(28), PX(4), 255l, 0l, 0l);
    raster_vertex(&triangle[2], PX(4), PX(20), 255l, 0l, 0l);
    triangle[0].alpha = 255l;
    triangle[1].alpha = 0l;
    triangle[2].alpha = 255l;
    RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, &alpha, triangle) != 0);

    left_red = (unsigned int)(raster_pixel(6u, 6u) >> 11);
    middle_red = (unsigned int)(raster_pixel(12u, 6u) >> 11);
    right_red = (unsigned int)(raster_pixel(20u, 6u) >> 11);
    RCHECK(left_red > middle_red);
    RCHECK(middle_red > right_red);
    RCHECK(left_red >= 24u);
}

/*
 * The factor pairs this engine does not implement are refused, not
 * approximated.
 *
 * A driver that substituted the nearest factor it had would draw a plausible
 * wrong picture with nothing anywhere to say so, and the caps published in
 * d3d_soft.c claim exactly these four. The engine there turns a refused pair
 * into an opaque draw before it reaches here; this is the layer that says no.
 */
static void test_alpha_refusals(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_ALPHA alpha;
    V9X_D3D_RASTER_VERTEX triangle[3];

    raster_reset(&target);
    raster_vertex(&triangle[0], PX(2), PX(2), 255l, 0l, 0l);
    raster_vertex(&triangle[1], PX(20), PX(2), 255l, 0l, 0l);
    raster_vertex(&triangle[2], PX(2), PX(18), 255l, 0l, 0l);

    alpha.src = V9X_D3D_RASTER_BLEND_SRC_ONE;
    alpha.dst = V9X_D3D_RASTER_BLEND_DST_ZERO;
    RCHECK(v9x_d3d_raster_alpha_valid(&alpha) != 0);
    RCHECK(v9x_d3d_raster_alpha_valid(0) == 0);

    /* D3DBLEND_SRCCOLOR, a legal render state and not one of the four. */
    alpha.src = 3ul;
    RCHECK(v9x_d3d_raster_alpha_valid(&alpha) == 0);
    RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, &alpha, triangle) == 0);

    /* D3DBLEND_DESTCOLOR on the other side. */
    alpha.src = V9X_D3D_RASTER_BLEND_SRC_SRCALPHA;
    alpha.dst = 9ul;
    RCHECK(v9x_d3d_raster_alpha_valid(&alpha) == 0);
    RCHECK(v9x_d3d_raster_triangle(&target, 0, 0, &alpha, triangle) == 0);

    /* Nothing was drawn by either refusal. */
    RCHECK(raster_pixel(4u, 4u) == RASTER_BACKGROUND);
}

/*
 * ARGB4444 is decoded as 4444 and not as 1555.
 *
 * 0xF0F0 is opaque pure green in 4444. The same sixteen bits read as ARGB1555
 * are red 28 of 31, green 7 and blue 16 - strong red and blue with little
 * green - so the channel balance distinguishes a correct decode from a
 * misread format without predicting how either expands its bits. The probe
 * makes exactly this argument on the guest; this is the host half of it.
 */
static void test_texture_format_decode(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_TEXTURE texture;
    v9x_u16 value;
    unsigned int x;
    unsigned int y;

    raster_reset(&target);
    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB4444,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    for (y = 0u; y < RASTER_TEX_SIZE; ++y) {
        for (x = 0u; x < RASTER_TEX_SIZE; ++x) {
            raster_texel_set(x, y, 0xf0f0u);
        }
    }
    RCHECK(raster_textured_quad(&target, &texture, 255l, 255l, 255l) != 0);

    value = raster_pixel(16u, 12u);
    RCHECK((value >> 11) <= 1u);            /* red, 5 bits */
    RCHECK(((value >> 5) & 0x3fu) >= 60u);  /* green, 6 bits */
    RCHECK((value & 0x1fu) <= 1u);          /* blue, 5 bits */

    /* The same bits declared as 1555 must not produce that. */
    raster_reset(&target);
    texture.format = V9X_D3D_RASTER_TEXFMT_ARGB1555;
    RCHECK(raster_textured_quad(&target, &texture, 255l, 255l, 255l) != 0);
    value = raster_pixel(16u, 12u);
    RCHECK((value >> 11) >= 20u);
    RCHECK((value & 0x1fu) >= 10u);
    raster_texture_check_margins();
}

/*
 * RGB565 is decoded as 565, and not as either of the other two.
 *
 * 0x8400 is chosen because the three layouts disagree about it completely:
 * in 565 the top bit is red's high bit and bit 10 is green's, giving red 16 of
 * 31 and green 32 of 63 - a middling olive. In 1555 the top bit is alpha and
 * bit 10 is red's low bit, so the same word is red 1 of 31 and nothing else,
 * near black. In 4444 it is dark red with no green at all. Green is therefore
 * the channel that decides: only a 565 decode puts any there.
 *
 * The format matters beyond bit-shuffling. It is the display's own layout on
 * every target this driver serves, so it is what an application converting a
 * bitmap for a 16-bit screen hands over, and the S3D texture unit cannot
 * sample it - the software engine accepts a format the hardware path refuses.
 */
static void test_texture_format_decode_565(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_TEXTURE texture;
    v9x_u16 value;
    unsigned int x;
    unsigned int y;

    raster_reset(&target);
    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_RGB565,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    for (y = 0u; y < RASTER_TEX_SIZE; ++y) {
        for (x = 0u; x < RASTER_TEX_SIZE; ++x) {
            raster_texel_set(x, y, 0x8400u);
        }
    }
    RCHECK(raster_textured_quad(&target, &texture, 255l, 255l, 255l) != 0);

    /* Red 16/31 and green 32/63 as written, allowing a level either way for
     * the expansion to eight bits and back. */
    value = raster_pixel(16u, 12u);
    RCHECK((value >> 11) >= 15u && (value >> 11) <= 17u);
    RCHECK(((value >> 5) & 0x3fu) >= 31u && ((value >> 5) & 0x3fu) <= 33u);
    RCHECK((value & 0x1fu) == 0u);

    /* The same bits declared as 1555 are near black in every channel. */
    raster_reset(&target);
    texture.format = V9X_D3D_RASTER_TEXFMT_ARGB1555;
    RCHECK(raster_textured_quad(&target, &texture, 255l, 255l, 255l) != 0);
    value = raster_pixel(16u, 12u);
    RCHECK((value >> 11) <= 2u);
    RCHECK(((value >> 5) & 0x3fu) <= 2u);

    /* And declared as 4444, red without green. */
    raster_reset(&target);
    texture.format = V9X_D3D_RASTER_TEXFMT_ARGB4444;
    RCHECK(raster_textured_quad(&target, &texture, 255l, 255l, 255l) != 0);
    value = raster_pixel(16u, 12u);
    RCHECK(((value >> 5) & 0x3fu) == 0u);
    raster_texture_check_margins();
}

/*
 * Bilinear filtering produces texels that are not in the texture.
 *
 * A two-colour texture point-sampled can only ever put white or black on the
 * screen. If a middling grey appears anywhere along a row, something
 * interpolated - which is the whole claim, and it cannot be produced by
 * accident.
 */
static void test_texture_bilinear_blends(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_TEXTURE texture;
    unsigned int x;
    unsigned int y;
    unsigned int column;
    unsigned int between = 0u;

    raster_reset(&target);
    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    for (y = 0u; y < RASTER_TEX_SIZE; ++y) {
        for (x = 0u; x < RASTER_TEX_SIZE; ++x) {
            raster_texel_set(x, y, (x & 1u) != 0u ? 0x0000u : 0x7fffu);
        }
    }

    RCHECK(raster_textured_quad(&target, &texture, 255l, 255l, 255l) != 0);
    for (column = 0u; column < RASTER_WIDTH; ++column) {
        unsigned int red = (unsigned int)(raster_pixel(column, 12u) >> 11);

        if (red > 2u && red < 29u) {
            ++between;
        }
    }
    RCHECK(between == 0u);

    raster_reset(&target);
    texture.filter = V9X_D3D_RASTER_FILTER_LINEAR;
    RCHECK(raster_textured_quad(&target, &texture, 255l, 255l, 255l) != 0);
    between = 0u;
    for (column = 0u; column < RASTER_WIDTH; ++column) {
        unsigned int red = (unsigned int)(raster_pixel(column, 12u) >> 11);

        if (red > 2u && red < 29u) {
            ++between;
        }
    }
    RCHECK(between >= 8u);
    raster_check_untouched_margins();
    raster_texture_check_margins();
}

/*
 * Modulate scales the texel by the vertex colour; decal ignores it.
 *
 * The same white texture and the same half-bright red vertex, twice. Under
 * decal the result is the texel, unchanged - which is the case that catches a
 * modulate that was applied when it should not have been. Under modulate the
 * result is half-bright red, which catches the reverse.
 */
static void test_texture_blend_modes(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_TEXTURE texture;
    v9x_u16 value;
    unsigned int x;
    unsigned int y;

    raster_reset(&target);
    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    for (y = 0u; y < RASTER_TEX_SIZE; ++y) {
        for (x = 0u; x < RASTER_TEX_SIZE; ++x) {
            raster_texel_set(x, y, 0x7fffu);
        }
    }

    RCHECK(raster_textured_quad(&target, &texture, 128l, 0l, 0l) != 0);
    RCHECK(raster_pixel(16u, 12u) == 0xffffu);

    raster_reset(&target);
    texture.blend = V9X_D3D_RASTER_BLEND_MODULATE;
    RCHECK(raster_textured_quad(&target, &texture, 128l, 0l, 0l) != 0);
    value = raster_pixel(16u, 12u);
    /* 128 of 255 in five bits is 16; one either side for the rounding in the
     * modulate and the pack. */
    RCHECK((value >> 11) >= 15u && (value >> 11) <= 17u);
    RCHECK(((value >> 5) & 0x3fu) == 0u);
    RCHECK((value & 0x1fu) == 0u);
    raster_check_untouched_margins();
    raster_texture_check_margins();
}

/*
 * An unusable texture, or a coordinate outside the range, is refused - and
 * refusing draws nothing.
 *
 * Same discipline as the depth buffer's refusals: a null pointer means
 * untextured and must keep working, while a non-null one that cannot be
 * sampled must be refused rather than silently dropped.
 */
static void test_texture_refusals(void)
{
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_TEXTURE texture;
    V9X_D3D_RASTER_VERTEX triangle[3];

    raster_reset(&target);
    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    raster_texel_set(0u, 0u, 0x7fffu);

    raster_vertex(&triangle[0], PX(2), PX(2), 255l, 255l, 255l);
    raster_vertex(&triangle[1], PX(28), PX(2), 255l, 255l, 255l);
    raster_vertex(&triangle[2], PX(2), PX(20), 255l, 255l, 255l);

    texture.size = 6ul;
    RCHECK(v9x_d3d_raster_triangle(&target, 0, &texture, 0, triangle) == 0);
    RCHECK(raster_pixel(10u, 8u) == RASTER_BACKGROUND);

    raster_texture_reset(&texture, V9X_D3D_RASTER_TEXFMT_ARGB1555,
                         V9X_D3D_RASTER_FILTER_POINT,
                         V9X_D3D_RASTER_BLEND_DECAL);
    raster_texel_set(0u, 0u, 0x7fffu);
    triangle[1].u = V9X_D3D_RASTER_TEXCOORD_MAX + 1l;
    RCHECK(v9x_d3d_raster_triangle(&target, 0, &texture, 0, triangle) == 0);
    triangle[1].u = -1l;
    RCHECK(v9x_d3d_raster_triangle(&target, 0, &texture, 0, triangle) == 0);
    triangle[1].u = 0l;
    triangle[1].v = V9X_D3D_RASTER_TEXCOORD_MAX + 1l;
    RCHECK(v9x_d3d_raster_triangle(&target, 0, &texture, 0, triangle) == 0);
    triangle[1].v = 0l;
    RCHECK(raster_pixel(10u, 8u) == RASTER_BACKGROUND);

    /* Texel (0,0) covers the sampled point at these coordinates, and it is
     * white; the whole triangle takes it because every coordinate is zero. */
    RCHECK(v9x_d3d_raster_triangle(&target, 0, &texture, 0, triangle) != 0);
    RCHECK(raster_pixel(10u, 8u) == 0xffffu);
    raster_texture_check_margins();
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
    test_depth_comparisons();
    test_depth_write_mask();
    test_depth_orders_two_surfaces();
    test_depth_interpolates_across_a_triangle();
    test_depth_refusals();
    test_depth_full_height_interpolation();
    test_texture_validation();
    test_texture_point_sampling();
    test_texture_format_decode();
    test_texture_format_decode_565();
    test_alpha_one_zero_replaces();
    test_alpha_full_is_exact();
    test_alpha_zero_keeps_destination();
    test_alpha_half_blends();
    test_alpha_gouraud_varies();
    test_alpha_refusals();
    test_texture_wrap_tiles();
    test_texture_wrap_extreme_coordinate();
    test_texture_address_refusals();
    test_lerp_rounds_down_descending();
    test_texture_bilinear_blends();
    test_texture_blend_modes();
    test_texture_refusals();
    return raster_failures;
}
