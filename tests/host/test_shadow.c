/*
 * Tests for the vbe screen shadow's rectangle arithmetic.
 *
 * What matters is the direction of every error: a rectangle may come out
 * larger than what was drawn, costing a copy, but never smaller, which would
 * leave the screen behind the shadow with nothing to show for it but a stale
 * patch of desktop.
 */
#include <stdio.h>

#include "velocity9x/shadow.h"

static unsigned int shadow_failures = 0u;

#define SHCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++shadow_failures; \
    } \
} while (0)

#define SH_W 1024u
#define SH_H 768u

static int shadow_rect_is(const struct v9x_shadow_rect *rect,
                          int left, int top, int right, int bottom)
{
    return rect->left == left && rect->top == top &&
           rect->right == right && rect->bottom == bottom;
}

static void test_a_cleared_dirty_holds_nothing(void)
{
    struct v9x_shadow_dirty dirty;

    dirty.pending = V9X_TRUE;
    v9x_shadow_dirty_clear(&dirty);
    SHCHECK(dirty.pending == V9X_FALSE);
}

/* The DIB Engine's rectangle is read as inclusive: one column and one row
 * more than an exclusive reading. */
static void test_an_access_rect_is_taken_as_inclusive(void)
{
    struct v9x_shadow_dirty dirty;

    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add(&dirty, 10, 20, 30, 40, SH_W, SH_H);
    SHCHECK(dirty.pending == V9X_TRUE);
    SHCHECK(shadow_rect_is(&dirty.rect, 10, 20, 31, 41));

    /* A single pixel is a 1x1 rectangle, not an empty one. */
    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add(&dirty, 5, 6, 5, 6, SH_W, SH_H);
    SHCHECK(dirty.pending == V9X_TRUE);
    SHCHECK(shadow_rect_is(&dirty.rect, 5, 6, 6, 7));
}

static void test_access_rects_union(void)
{
    struct v9x_shadow_dirty dirty;

    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add(&dirty, 100, 100, 109, 109, SH_W, SH_H);
    v9x_shadow_dirty_add(&dirty, 50, 200, 59, 209, SH_W, SH_H);
    SHCHECK(shadow_rect_is(&dirty.rect, 50, 100, 110, 210));
}

static void test_access_rects_clip_to_the_screen(void)
{
    struct v9x_shadow_dirty dirty;

    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add(&dirty, -20, -5, 2000, 900, SH_W, SH_H);
    SHCHECK(shadow_rect_is(&dirty.rect, 0, 0, (int)SH_W, (int)SH_H));

    /* Wholly off screen adds nothing. */
    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add(&dirty, 2000, 10, 2100, 20, SH_W, SH_H);
    SHCHECK(dirty.pending == V9X_FALSE);
    v9x_shadow_dirty_add(&dirty, -50, 10, -1, 20, SH_W, SH_H);
    SHCHECK(dirty.pending == V9X_FALSE);

    /* The inclusive edge at the largest coordinate does not wrap. */
    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add(&dirty, 1000, 700, 32767, 32767, SH_W, SH_H);
    SHCHECK(shadow_rect_is(&dirty.rect, 1000, 700, (int)SH_W, (int)SH_H));
}

/* An argument that cannot bound the change costs the whole screen. */
static void test_an_inverted_access_rect_is_the_whole_screen(void)
{
    struct v9x_shadow_dirty dirty;

    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add(&dirty, 30, 10, 20, 40, SH_W, SH_H);
    SHCHECK(shadow_rect_is(&dirty.rect, 0, 0, (int)SH_W, (int)SH_H));

    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add(&dirty, 10, 40, 20, 30, SH_W, SH_H);
    SHCHECK(shadow_rect_is(&dirty.rect, 0, 0, (int)SH_W, (int)SH_H));
}

static void test_the_cursor_rect_starts_at_the_hot_spot(void)
{
    struct v9x_shadow_rect rect;
    struct v9x_shadow_dirty dirty;

    v9x_shadow_cursor_rect(100, 200, 4, 7, 32u, 32u, &rect);
    SHCHECK(shadow_rect_is(&rect, 96, 193, 128, 225));

    /* At the top-left corner the shape is partly off screen; clipping keeps
     * the visible part. */
    v9x_shadow_cursor_rect(0, 0, 10, 10, 32u, 32u, &rect);
    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add_rect(&dirty, &rect, SH_W, SH_H);
    SHCHECK(shadow_rect_is(&dirty.rect, 0, 0, 22, 22));

    /* No shape, no rectangle. */
    v9x_shadow_cursor_rect(100, 200, 0, 0, 0u, 32u, &rect);
    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add_rect(&dirty, &rect, SH_W, SH_H);
    SHCHECK(dirty.pending == V9X_FALSE);
}

static void test_spans_cover_the_rect_in_whole_dwords(void)
{
    struct v9x_shadow_rect rect;
    struct v9x_shadow_span span;

    /* 16 bpp, pitch 2048: pixels 3..9 are bytes 6..19, widened to 4..20. */
    rect.left = 3;
    rect.top = 10;
    rect.right = 10;
    rect.bottom = 12;
    SHCHECK(v9x_shadow_span(&rect, 16u, 2048ul, &span) == V9X_TRUE);
    SHCHECK(span.offset == 10ul * 2048ul + 4ul);
    SHCHECK(span.row_bytes == 16u);
    SHCHECK(span.rows == 2u);

    /* 8 bpp: pixel 5 alone is byte 5, widened to 4..8. */
    rect.left = 5;
    rect.top = 0;
    rect.right = 6;
    rect.bottom = 1;
    SHCHECK(v9x_shadow_span(&rect, 8u, 1024ul, &span) == V9X_TRUE);
    SHCHECK(span.offset == 4ul);
    SHCHECK(span.row_bytes == 4u);

    /* 24 bpp: pixels 1..2 are bytes 3..8, widened to 0..12. */
    rect.left = 1;
    rect.right = 3;
    SHCHECK(v9x_shadow_span(&rect, 24u, 3072ul, &span) == V9X_TRUE);
    SHCHECK(span.offset == 0ul);
    SHCHECK(span.row_bytes == 12u);

    /* 32 bpp, the whole of a 1920-wide row. */
    rect.left = 0;
    rect.right = 1920;
    rect.top = 1439;
    rect.bottom = 1440;
    SHCHECK(v9x_shadow_span(&rect, 32u, 7680ul, &span) == V9X_TRUE);
    SHCHECK(span.offset == 1439ul * 7680ul);
    SHCHECK(span.row_bytes == 7680u);
    SHCHECK(span.rows == 1u);
}

/* The dword widening never runs past the row into the next one. */
static void test_spans_stop_at_the_pitch(void)
{
    struct v9x_shadow_rect rect;
    struct v9x_shadow_span span;

    /* 24 bpp, 1023 pixels wide with a 3070-byte row the BIOS did not pad:
     * the last pixel ends at byte 3069, which widens to 3072. */
    rect.left = 1022;
    rect.top = 0;
    rect.right = 1023;
    rect.bottom = 1;
    SHCHECK(v9x_shadow_span(&rect, 24u, 3070ul, &span) == V9X_TRUE);
    SHCHECK(span.offset == 3064ul);
    SHCHECK(span.row_bytes == 6u);
}

static void test_empty_and_unknown_spans_copy_nothing(void)
{
    struct v9x_shadow_rect rect;
    struct v9x_shadow_span span;

    rect.left = 10;
    rect.top = 10;
    rect.right = 10;
    rect.bottom = 20;
    SHCHECK(v9x_shadow_span(&rect, 16u, 2048ul, &span) == V9X_FALSE);

    rect.right = 20;
    SHCHECK(v9x_shadow_span(&rect, 4u, 2048ul, &span) == V9X_FALSE);
    SHCHECK(v9x_shadow_span(&rect, 15u, 2048ul, &span) == V9X_FALSE);
}

/* A polyline is bounded by its points, inclusive of the last. */
static void test_a_polyline_is_bounded_by_its_points(void)
{
    static const v9x_s16 points[] = { 100, 50, 20, 300, 400, 120 };
    struct v9x_shadow_rect bound;

    SHCHECK(v9x_shadow_output_bound(V9X_SHADOW_OS_POLYLINE, 3u, points,
                                    &bound) == V9X_TRUE);
    SHCHECK(shadow_rect_is(&bound, 20, 50, 401, 301));
}

/* A scanline call is one row: y in the first point, then x intervals. */
static void test_scanlines_are_one_row(void)
{
    static const v9x_s16 points[] = { 0, 77, 10, 20, 300, 310, 5, 6 };
    struct v9x_shadow_rect bound;

    SHCHECK(v9x_shadow_output_bound(V9X_SHADOW_OS_SCANLINES, 4u, points,
                                    &bound) == V9X_TRUE);
    SHCHECK(shadow_rect_is(&bound, 5, 77, 311, 78));
}

/* A poly-scanline call is a list of SCAN records: x count, top, bottom
 * (exclusive), the x pairs, the count again. */
static void test_polyscanlines_are_bounded_by_their_scans(void)
{
    static const v9x_s16 scans[] = {
        2, 10, 12, 40, 60, 2,
        4, 12, 20, 30, 35, 70, 90, 4
    };
    static const v9x_s16 odd[] = { 3, 10, 12, 40, 60, 70, 3 };
    static const v9x_s16 none[] = { 0, 10, 12, 0 };
    struct v9x_shadow_rect bound;

    SHCHECK(v9x_shadow_output_bound(V9X_SHADOW_OS_POLYSCANLINE, 2u, scans,
                                    &bound) == V9X_TRUE);
    /* Rows 10 to 19: the SCAN bottom is already exclusive
     * (GDIDEFS.INC:1370), so only the x edge gains one. */
    SHCHECK(shadow_rect_is(&bound, 30, 10, 91, 20));

    /* A record a SCAN cannot be is not guessed at. */
    SHCHECK(v9x_shadow_output_bound(V9X_SHADOW_OS_POLYSCANLINE, 1u, odd,
                                    &bound) == V9X_FALSE);
    SHCHECK(v9x_shadow_output_bound(V9X_SHADOW_OS_POLYSCANLINE, 1u, none,
                                    &bound) == V9X_FALSE);
    SHCHECK(v9x_shadow_output_bound(V9X_SHADOW_OS_POLYSCANLINE, 0u, scans,
                                    &bound) == V9X_FALSE);
}

/* The start and end of a scanline series draw nothing: an empty bound, so
 * the series' own bracket, which spans the screen, copies nothing. Their
 * points are not read. */
static void test_a_scan_series_bracket_is_empty(void)
{
    struct v9x_shadow_rect bound;
    struct v9x_shadow_dirty dirty;

    SHCHECK(v9x_shadow_output_bound(V9X_SHADOW_OS_BEGINNSCAN, 0u, 0,
                                    &bound) == V9X_TRUE);
    SHCHECK(bound.right <= bound.left);
    SHCHECK(v9x_shadow_output_bound(V9X_SHADOW_OS_ENDNSCAN, 0u, 0,
                                    &bound) == V9X_TRUE);
    SHCHECK(bound.right <= bound.left);

    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add_within(&dirty, 0, 0, 1023, 767, &bound,
                                SH_W, SH_H);
    v9x_shadow_dirty_add_within(&dirty, 9, 9, 0, 0, &bound, SH_W, SH_H);
    SHCHECK(dirty.pending == V9X_FALSE);
}

/* Anything else, or too few points to say, is not bounded: the access
 * rectangle stands. */
static void test_other_outputs_are_not_bounded(void)
{
    static const v9x_s16 points[] = { 0, 77, 10, 20 };
    struct v9x_shadow_rect bound;

    SHCHECK(v9x_shadow_output_bound(3u, 2u, points, &bound) == V9X_FALSE);
    SHCHECK(v9x_shadow_output_bound(8u, 2u, points, &bound) == V9X_FALSE);
    SHCHECK(v9x_shadow_output_bound(V9X_SHADOW_OS_POLYLINE, 0u, points,
                                    &bound) == V9X_FALSE);
    SHCHECK(v9x_shadow_output_bound(V9X_SHADOW_OS_SCANLINES, 1u, points,
                                    &bound) == V9X_FALSE);
    SHCHECK(v9x_shadow_output_bound(V9X_SHADOW_OS_POLYLINE, 2u, 0,
                                    &bound) == V9X_FALSE);
}

/* Inside a bounded call the access rectangle is cut to the bound; an
 * inverted one, which alone means the whole screen, becomes the bound. */
static void test_a_bound_cuts_the_access_rect(void)
{
    struct v9x_shadow_dirty dirty;
    struct v9x_shadow_rect bound;

    bound.left = 100;
    bound.top = 200;
    bound.right = 150;
    bound.bottom = 201;

    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add_within(&dirty, 0, 0, 1023, 767, &bound,
                                SH_W, SH_H);
    SHCHECK(shadow_rect_is(&dirty.rect, 100, 200, 150, 201));

    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add_within(&dirty, 30, 10, 20, 5, &bound, SH_W, SH_H);
    SHCHECK(shadow_rect_is(&dirty.rect, 100, 200, 150, 201));

    /* An access rectangle smaller than the bound stays as it is. */
    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add_within(&dirty, 110, 200, 119, 200, &bound,
                                SH_W, SH_H);
    SHCHECK(shadow_rect_is(&dirty.rect, 110, 200, 120, 201));

    /* Disjoint: nothing. */
    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add_within(&dirty, 500, 500, 600, 600, &bound,
                                SH_W, SH_H);
    SHCHECK(dirty.pending == V9X_FALSE);

    /* No bound is the plain add. */
    v9x_shadow_dirty_clear(&dirty);
    v9x_shadow_dirty_add_within(&dirty, 10, 20, 30, 40, 0, SH_W, SH_H);
    SHCHECK(shadow_rect_is(&dirty.rect, 10, 20, 31, 41));
}

unsigned int v9x_run_shadow_tests(void)
{
    test_a_polyline_is_bounded_by_its_points();
    test_scanlines_are_one_row();
    test_polyscanlines_are_bounded_by_their_scans();
    test_a_scan_series_bracket_is_empty();
    test_other_outputs_are_not_bounded();
    test_a_bound_cuts_the_access_rect();
    test_a_cleared_dirty_holds_nothing();
    test_an_access_rect_is_taken_as_inclusive();
    test_access_rects_union();
    test_access_rects_clip_to_the_screen();
    test_an_inverted_access_rect_is_the_whole_screen();
    test_the_cursor_rect_starts_at_the_hot_spot();
    test_spans_cover_the_rect_in_whole_dwords();
    test_spans_stop_at_the_pitch();
    test_empty_and_unknown_spans_copy_nothing();
    return shadow_failures;
}
