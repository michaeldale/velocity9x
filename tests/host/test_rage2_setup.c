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

/* The channel output rule, against the values ATIRX /shade G7 and G8 read
 * on A8U4I5 (boot 136). */
static void test_channel_out(void)
{
    CHECK(v9x_r2_channel_out(200l << 16) == 200ul);
    CHECK(v9x_r2_channel_out(248l << 16) == 248ul);
    CHECK(v9x_r2_channel_out(256l << 16) == 255ul);
    CHECK(v9x_r2_channel_out(376l << 16) == 255ul);
    CHECK(v9x_r2_channel_out(384l << 16) == 0ul);
    CHECK(v9x_r2_channel_out(-(8l << 16)) == 0ul);
    CHECK(v9x_r2_channel_out(-(128l << 16)) == 0ul);
    CHECK(v9x_r2_channel_out(-(136l << 16)) == 255ul);
    /* A fraction truncates. */
    CHECK(v9x_r2_channel_out((7l << 16) + 0xfff0l) == 7ul);
}

/* The engine's accumulator at (x, y) for a trapezoid anchored at
 * (trap->x, trap->y): modular sums of the register-masked values. */
static v9x_s32 model_accumulator(const struct v9x_r2_shade *shade,
                                 const struct v9x_r2_flat_trap *trap,
                                 v9x_u32 channel, v9x_s32 x, v9x_s32 y)
{
    v9x_u32 start = (v9x_u32)shade->start[channel] & V9X_R2_COLOR_MASK;
    v9x_u32 xi = (v9x_u32)shade->x_inc[channel] & V9X_R2_COLOR_MASK;
    v9x_u32 yi = (v9x_u32)shade->y_inc[channel] & V9X_R2_COLOR_MASK;
    /* X_INC counts steps in DST_X_DIR's direction (measured, boot 136). */
    v9x_u32 dx = (trap->dst_cntl & V9X_M64_DST_X_DIR) != 0ul
        ? (v9x_u32)(x - (v9x_s32)trap->x)
        : (v9x_u32)((v9x_s32)trap->x - x);
    v9x_u32 dy = (v9x_u32)(y - (v9x_s32)trap->y);

    return (v9x_s32)(start + dx * xi + dy * yi);
}

static unsigned int shade_worst = 0u;
static unsigned long shade_pixels = 0ul;
static unsigned long shade_refused = 0ul;

/* For every pixel the model draws, the engine's channel value against the
 * ideal plane at the pixel centre, rounded to nearest. */
static unsigned int compare_shade(const struct v9x_r2_vertex *v,
                                  const v9x_u32 *colors)
{
    struct v9x_r2_target target;
    struct v9x_r2_flat_trap traps[V9X_R2_SETUP_TRAPS];
    struct v9x_r2_shade shade;
    v9x_u32 count;
    v9x_u32 index;
    v9x_u32 channel;
    unsigned int bad = 0u;
    double fx[3];
    double fy[3];
    double det;
    v9x_u32 k;

    make_target(&target);
    if (v9x_r2_setup_triangle(&target, v, traps, &count) != V9X_STATUS_OK) {
        return 9999u;
    }
    for (k = 0ul; k < 3ul; ++k) {
        fx[k] = (double)v[k].x / 16.0;
        fy[k] = (double)v[k].y / 16.0;
    }
    det = (fx[1] - fx[0]) * (fy[2] - fy[0]) - (fx[2] - fx[0]) * (fy[1] - fy[0]);
    for (index = 0ul; index < count; ++index) {
        v9x_s32 lead[GRID];
        v9x_s32 trail[GRID];
        v9x_u32 row;
        v9x_status status = v9x_r2_setup_shade(v, colors, &traps[index],
                                               &shade);

        if (status == V9X_STATUS_UNSUPPORTED) {
            ++shade_refused;
            continue;
        }
        if (status != V9X_STATUS_OK) {
            return 9998u;
        }
        model_walk(traps[index].x, traps[index].lead_err,
                   traps[index].lead_inc, traps[index].lead_dec,
                   (traps[index].dst_cntl & V9X_M64_DST_X_DIR) != 0ul,
                   traps[index].length, lead);
        model_walk(traps[index].trail_x, traps[index].trail_err,
                   traps[index].trail_inc, traps[index].trail_dec,
                   (traps[index].dst_cntl & V9X_R2_TRAIL_X_DIR) != 0ul,
                   traps[index].length, trail);
        for (row = 0ul; row < traps[index].length; ++row) {
            v9x_s32 y = (v9x_s32)(traps[index].y + row);
            v9x_s32 x;

            for (x = lead[row]; x < trail[row]; ++x) {
                ++shade_pixels;
                for (channel = 0ul; channel < 3ul; ++channel) {
                    v9x_u32 shift = 16ul - 8ul * channel;
                    double c0 = (double)((colors[0] >> shift) & 0xfful);
                    double c1 = (double)((colors[1] >> shift) & 0xfful);
                    double c2 = (double)((colors[2] >> shift) & 0xfful);
                    double px = (double)x + 0.5 - fx[0];
                    double py = (double)y + 0.5 - fy[0];
                    double ideal = c0 +
                        ((c1 - c0) * (fy[2] - fy[0]) -
                         (c2 - c0) * (fy[1] - fy[0])) / det * px +
                        ((c2 - c0) * (fx[1] - fx[0]) -
                         (c1 - c0) * (fx[2] - fx[0])) / det * py;
                    long want = (long)(ideal + 0.5 + 1000.0) - 1000l;
                    long got = (long)v9x_r2_channel_out(model_accumulator(
                        &shade, &traps[index], channel, x, y));
                    long diff;

                    if (want < 0l) want = 0l;
                    if (want > 255l) want = 255l;
                    diff = got > want ? got - want : want - got;
                    if ((unsigned int)diff > shade_worst) {
                        shade_worst = (unsigned int)diff;
                    }
                    /* The window: a pixel's true value must not reach the
                     * wrap points, or saturation turns into a flip. */
                    if (ideal < -127.0 || ideal > 382.0 || diff > 1l) {
                        ++bad;
                    }
                }
            }
        }
    }
    return bad;
}

static void test_shade_random(void)
{
    unsigned int index;
    unsigned int failed = 0u;
    v9x_u32 colors[3];
    struct v9x_r2_vertex v[3];

    for (index = 0u; index < 2000u; ++index) {
        unsigned int bad;

        v[0] = vtx(lcg_coord(), lcg_coord());
        v[1] = vtx(lcg_coord(), lcg_coord());
        v[2] = vtx(lcg_coord(), lcg_coord());
        colors[0] = (v9x_u32)lcg_coord() * 0x4f1bbcdul & 0x00fffffful;
        colors[1] = (v9x_u32)lcg_coord() * 0x2545f49ul & 0x00fffffful;
        colors[2] = (v9x_u32)lcg_coord() * 0x6c07865ul & 0x00fffffful;
        bad = compare_shade(v, colors);
        if (bad != 0u && failed < 5u) {
            printf("  shade %u: %u channel values off by more than 1\n",
                   index, bad);
        }
        if (bad != 0u) {
            ++failed;
        }
    }
    CHECK(failed == 0u);
    /* Truncated hardware arithmetic against an exact plane: at most one
     * level, never more. */
    CHECK(shade_worst <= 1u);
    /* Not vacuous: most trapezoids are shaded, many pixels compared. */
    printf("  shade: %lu pixels compared, %lu trapezoids too steep\n",
           shade_pixels, shade_refused);
    CHECK(shade_pixels > 200000ul);
    CHECK(shade_refused < 200ul);
}

/* Constant colour: every pixel the exact vertex colour, no drift. */
static void test_shade_constant(void)
{
    struct v9x_r2_vertex v[3];
    v9x_u32 colors[3] = { 0x00ff8040ul, 0x00ff8040ul, 0x00ff8040ul };

    v[0] = vtx(P(4) + 5, P(6) + 1);
    v[1] = vtx(P(50) + 3, P(9) + 12);
    v[2] = vtx(P(20) + 9, P(55) + 4);
    shade_worst = 0u;
    CHECK(compare_shade(v, colors) == 0u);
    CHECK(shade_worst == 0u);
}

/* ---- Texture coordinates: rows the card drew ---------------------------- */

/* One texel of a 32-wide map, in the normalised unit (V9X_R2_ST_ONE / 32). */
#define TX1 2097152l

#define CARD_RECT  0
#define CARD_RIGHT 1
#define CARD_LEFT  2

struct card_row {
    const char *scene;
    struct v9x_r2_st st;
    v9x_u32 log2_size;
    v9x_u32 log2_pitch;
    v9x_u32 log2_height;
    int shape;              /* CARD_RECT, CARD_RIGHT or CARD_LEFT */
    v9x_s32 row;
    v9x_s32 lead_x;         /* the row's first pixel; the span ends at 47 */
    unsigned char u[32];
    unsigned char v[32];
};

/* Each pixel's (u, v), columns lead_x..47, as read from BOOT136-ATIRX-
 * TEX2..6.TXT in docs\probe\a8u4i5-rage-iic-registers-2026-10-02. */
static const struct card_row card_rows[] = {
    { "T6Quadratic",
      { { 0l, 0l }, { 0l, 0l }, { 0l, TX1 },
        { TX1 / 8l, 0l }, { 0l, 0l }, { 0l, 0l } },
      5ul, 5ul, 5ul, CARD_RECT, 16l, 16l,
      { 0, 0, 0, 0, 0, 1, 1, 2, 3, 4, 5, 6, 8, 9, 11, 13, 15, 17, 19, 21,
        23, 26, 28, 31, 2, 5, 8, 11, 15, 18, 22, 26 },
      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { "T13XYInc2",
      { { 0l, 0l }, { 0l, 0l }, { 0l, TX1 },
        { 0l, 0l }, { 0l, 0l }, { TX1 / 4l, 0l } },
      5ul, 5ul, 5ul, CARD_RECT, 23l, 16l,
      { 0, 1, 3, 5, 7, 8, 10, 12, 14, 15, 17, 19, 21, 22, 24, 26, 28, 29,
        31, 1, 3, 4, 6, 8, 10, 11, 13, 15, 17, 18, 20, 22 },
      { 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7 } },
    { "T17SlopeXY",
      { { 0l, 0l }, { 0l, 0l }, { 0l, TX1 },
        { 0l, 0l }, { 0l, 0l }, { TX1 / 4l, 0l } },
      5ul, 5ul, 5ul, CARD_RIGHT, 23l, 24l,
      { 14, 15, 17, 19, 21, 22, 24, 26, 28, 29, 31, 1, 3, 4, 6, 8, 10, 11,
        13, 15, 17, 18, 20, 22 },
      { 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7 } },
    { "T18LeftX2",
      { { 8l * TX1, TX1 / 2l }, { 0l, 0l }, { 0l, TX1 },
        { TX1 / 8l, 0l }, { 0l, 0l }, { 0l, 0l } },
      5ul, 5ul, 5ul, CARD_LEFT, 23l, 32l,
      { 11, 10, 9, 9, 8, 8, 8, 7, 7, 8, 8, 8, 9, 9, 10, 11 },
      { 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7 } },
    { "T19LeftXY",
      { { 8l * TX1, TX1 / 2l }, { 0l, 0l }, { 0l, TX1 },
        { 0l, 0l }, { 0l, 0l }, { -TX1 / 4l, 0l } },
      5ul, 5ul, 5ul, CARD_LEFT, 20l, 35l,
      { 3, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 },
      { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 } },
    { "T5bBias",
      { { 8l * TX1 + 32l, 32l }, { TX1, 0l }, { 0l, TX1 },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } },
      5ul, 5ul, 5ul, CARD_LEFT, 16l, 39l,
      { 9, 8, 6, 5, 4, 3, 2, 1, 0 },
      { 0, 0, 31, 31, 31, 31, 31, 31, 31 } },
    { "T14Tall16",
      { { 0l, 20l * TX1 }, { TX1, 0l }, { 0l, TX1 },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } },
      5ul, 4ul, 5ul, CARD_RECT, 16l, 16l,
      { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3,
        4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
      { 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
        20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20 } },
    { "T9Map16",
      { { 0l, 0l }, { TX1, 0l }, { 0l, TX1 },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } },
      4ul, 4ul, 4ul, CARD_RECT, 20l, 16l,
      { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10,
        11, 11, 12, 12, 13, 13, 14, 14, 15, 15 },
      { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2 } },
    { "T15WideWrap",
      { { 0l, 12l * TX1 }, { TX1, 0l }, { 0l, TX1 },
        { 0l, 0l }, { 0l, 0l }, { 0l, 0l } },
      5ul, 5ul, 4ul, CARD_RECT, 20l, 16l,
      { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 },
      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } }
};

/* The scene runner's trapezoids: the 32x8 rectangle at (16, 16), and the
 * leading edge one pixel a row right from 16 or left from 40, trailing
 * at 48. */
static void card_trap(int shape, struct v9x_r2_flat_trap *trap)
{
    memset(trap, 0, sizeof(*trap));
    trap->x = shape == CARD_LEFT ? 40ul : 16ul;
    trap->y = 16ul;
    trap->length = 8ul;
    trap->trail_x = 48ul;
    trap->lead_err = shape == CARD_RECT ? -1l : 0l;
    trap->lead_inc = shape == CARD_RECT ? 0l : 8l;
    trap->lead_dec = shape == CARD_RECT ? -1l : -8l;
    trap->trail_err = -1l;
    trap->trail_inc = 0l;
    trap->trail_dec = -1l;
    trap->dst_cntl = V9X_M64_DST_Y_DIR | V9X_R2_TRAIL_X_DIR |
                     V9X_R2_TRAP_FILL_DIR |
                     (shape == CARD_LEFT ? 0ul : V9X_M64_DST_X_DIR);
}

struct card_check {
    const struct card_row *r;
    unsigned int bad;
    unsigned int seen;
};

static void card_texel(void *context, v9x_s32 x, v9x_s32 y, v9x_u32 s,
                       v9x_u32 t)
{
    struct card_check *check = (struct card_check *)context;
    const struct card_row *r = check->r;
    v9x_u32 u;
    v9x_u32 v;

    if (y != r->row) {
        return;
    }
    ++check->seen;
    u = v9x_r2_texel_index(s, r->log2_size, r->log2_pitch);
    v = v9x_r2_texel_index(t, r->log2_size, r->log2_height);
    if (x < r->lead_x || x > 47l || u != r->u[x - r->lead_x] ||
        v != r->v[x - r->lead_x]) {
        ++check->bad;
    }
}

static void test_st_model_card_rows(void)
{
    unsigned int index;

    for (index = 0u; index < sizeof(card_rows) / sizeof(card_rows[0]);
         ++index) {
        const struct card_row *r = &card_rows[index];
        struct v9x_r2_flat_trap trap;
        struct card_check check;

        card_trap(r->shape, &trap);
        check.r = r;
        check.bad = 0u;
        check.seen = 0u;
        CHECK(v9x_r2_ref_walk_st(&trap, &r->st, card_texel, &check));
        if (check.bad != 0u || check.seen != (unsigned int)(48l - r->lead_x)) {
            printf("FAIL %s row %ld: %u of %u pixels differ from the card\n",
                   r->scene, (long)r->row, check.bad, check.seen);
            ++failures;
        }
    }
}

struct walk_probe {
    v9x_s32 y;
    v9x_u32 values[64];
    unsigned int count;
};

static void probe_texel(void *context, v9x_s32 x, v9x_s32 y, v9x_u32 s,
                        v9x_u32 t)
{
    struct walk_probe *probe = (struct walk_probe *)context;

    (void)x;
    (void)t;
    if (y == probe->y && probe->count < 64u) {
        probe->values[probe->count++] = s;
    }
}

/* The walk's measured rules, one at a time on the rectangle and the
 * left-leaning edge. */
static void test_st_walk_rules(void)
{
    struct v9x_r2_flat_trap trap;
    struct v9x_r2_st st;
    struct walk_probe probe;

    /* T5bBias: against DST_X_DIR each pixel loses 32 more, so the third
     * pixel reads texel 6 (exact would be 7). */
    card_trap(CARD_LEFT, &trap);
    memset(&st, 0, sizeof(st));
    st.start[0] = 8l * TX1 + 32l;
    st.xinc_start[0] = TX1;
    probe.y = 16l;
    probe.count = 0u;
    CHECK(v9x_r2_ref_walk_st(&trap, &st, probe_texel, &probe));
    CHECK(probe.count == 9u);
    CHECK(v9x_r2_texel_index(probe.values[1], 5ul, 5ul) == 8ul);
    CHECK(v9x_r2_texel_index(probe.values[2], 5ul, 5ul) == 6ul);

    /* START keeps bits 25:5. */
    card_trap(CARD_RECT, &trap);
    memset(&st, 0, sizeof(st));
    st.start[0] = 31l;
    probe.count = 0u;
    CHECK(v9x_r2_ref_walk_st(&trap, &st, probe_texel, &probe));
    CHECK(probe.count == 32u && probe.values[0] == 0ul);

    /* /texprec PX1_4: an X increment of 16 is floored to 0 as it is
     * added, so START 32 short of texel 1 never reaches it. */
    st.start[0] = TX1 - 32l;
    st.xinc_start[0] = 16l;
    probe.count = 0u;
    CHECK(v9x_r2_ref_walk_st(&trap, &st, probe_texel, &probe));
    CHECK(v9x_r2_texel_index(probe.values[31], 5ul, 5ul) == 0ul);

    /* /texprec PX2_2: X_INC2 4 builds the increment at full precision;
     * texel 1 first at dx 9, where the closed form says dx 5. */
    st.xinc_start[0] = 0l;
    st.x_inc2[0] = 4l;
    probe.count = 0u;
    CHECK(v9x_r2_ref_walk_st(&trap, &st, probe_texel, &probe));
    CHECK(v9x_r2_texel_index(probe.values[8], 5ul, 5ul) == 0ul);
    CHECK(v9x_r2_texel_index(probe.values[9], 5ul, 5ul) == 1ul);
}

struct st_stats {
    unsigned long pixels;
    unsigned long texel_misses;     /* the engine's texel != exact floor */
    double max_error;               /* texels of the larger dimension */
    unsigned int triangles;
    unsigned int unsupported;
    unsigned int over_half;         /* estimate above half a texel */
    unsigned int underestimates;    /* measured beyond the estimate */
    unsigned int affine_traps;      /* slivers given the plane */
    unsigned int within_mismatches; /* _within against the grid's reading */
};

static double lcg_unit(void)
{
    lcg_state = lcg_state * 1103515245ul + 12345ul;
    return (double)(v9x_s32)((lcg_state >> 8) & 0xffffl) / 65536.0;
}

/* The exact perspective value at pixel (x, y)'s centre, in the engine's
 * unit for `axis`. */
static double exact_st(const struct v9x_r2_vertex *v,
                       const struct v9x_r2_tex_coord *c,
                       const struct v9x_r2_texture *t, v9x_u32 axis,
                       v9x_s32 x, v9x_s32 y)
{
    double px = (double)x + 0.5;
    double py = (double)y + 0.5;
    double vx[3];
    double vy[3];
    double det;
    double num = 0.0;
    double den = 0.0;
    v9x_u32 size = t->log2_width > t->log2_height ? t->log2_width
                                                  : t->log2_height;
    double scale = 67108864.0 /
        (double)(1l << (size - (axis == 0ul ? t->log2_width
                                            : t->log2_height)));
    int k;

    for (k = 0; k < 3; ++k) {
        vx[k] = (double)v[k].x / 16.0;
        vy[k] = (double)v[k].y / 16.0;
    }
    det = (vx[1] - vx[0]) * (vy[2] - vy[0]) - (vx[2] - vx[0]) * (vy[1] - vy[0]);
    for (k = 0; k < 3; ++k) {
        int i = (k + 1) % 3;
        int j = (k + 2) % 3;
        double lambda = ((vx[i] - px) * (vy[j] - py) -
                         (vx[j] - px) * (vy[i] - py)) / det;
        double value = (axis == 0ul ? c[k].tu : c[k].tv) * scale;

        num += lambda * value * c[k].q;
        den += lambda * c[k].q;
    }
    return num / den;
}

/* The engine's distance from exact, modulo the 2^26 repeat. */
static double wrapped_error(v9x_u32 engine, double exact)
{
    double e = (double)(v9x_s32)engine - exact;

    while (e > 33554432.0) {
        e -= 67108864.0;
    }
    while (e < -33554432.0) {
        e += 67108864.0;
    }
    return e < 0.0 ? -e : e;
}

struct st_pixel_check {
    const struct v9x_r2_vertex *v;
    const struct v9x_r2_tex_coord *c;
    const struct v9x_r2_texture *t;
    struct st_stats *stats;
    v9x_u32 size;
    double texel;
    double worst;
};

static void st_pixel(void *context, v9x_s32 x, v9x_s32 y, v9x_u32 s,
                     v9x_u32 t)
{
    struct st_pixel_check *check = (struct st_pixel_check *)context;
    v9x_u32 axis;

    ++check->stats->pixels;
    for (axis = 0ul; axis < 2ul; ++axis) {
        v9x_u32 wrap = axis == 0ul ? check->t->log2_width
                                   : check->t->log2_height;
        v9x_u32 engine = axis == 0ul ? s : t;
        double exact = exact_st(check->v, check->c, check->t, axis, x, y);
        double error = wrapped_error(engine, exact) / check->texel;
        double floor_exact = exact / check->texel;
        v9x_s32 want = (v9x_s32)floor_exact;

        if (error > check->worst) {
            check->worst = error;
        }
        /* floor(exact / texel), wrapped like the engine. */
        if ((double)want > floor_exact) {
            --want;
        }
        if (v9x_r2_texel_index(engine, check->size, wrap) !=
            ((v9x_u32)want & ((1ul << wrap) - 1ul))) {
            ++check->stats->texel_misses;
        }
    }
}

/* Every covered pixel of one triangle: the engine's walk of the setup's
 * registers, against exact perspective. Returns the triangle's worst
 * error in texels, or -1 when nothing was drawn. */
static double st_triangle(const struct v9x_r2_vertex *v,
                          const struct v9x_r2_tex_coord *c,
                          const struct v9x_r2_texture *t,
                          struct st_stats *stats)
{
    struct v9x_r2_target target;
    struct v9x_r2_flat_trap traps[V9X_R2_SETUP_TRAPS];
    struct st_pixel_check check;
    v9x_u32 count = 0ul;
    v9x_u32 index;

    check.v = v;
    check.c = c;
    check.t = t;
    check.stats = stats;
    check.size = t->log2_width > t->log2_height ? t->log2_width
                                                : t->log2_height;
    check.texel = (double)(1l << (26u - check.size));
    check.worst = -1.0;

    make_target(&target);
    if (v9x_r2_setup_triangle(&target, v, traps, &count) != V9X_STATUS_OK) {
        return check.worst;
    }
    for (index = 0ul; index < count; ++index) {
        struct v9x_r2_st st;
        v9x_u32 affine = 9ul;

        if (v9x_r2_setup_texture(v, c, t, &traps[index], &st, &affine) !=
                V9X_STATUS_OK) {
            ++stats->unsupported;
            continue;
        }
        if (affine == 1ul) {
            ++stats->affine_traps;
        }
        CHECK(v9x_r2_ref_walk_st(&traps[index], &st, st_pixel, &check));
    }
    if (check.worst > stats->max_error) {
        stats->max_error = check.worst;
    }
    return check.worst;
}

/*
 * Random triangles in the 64x64 grid, textured as a game would: texel
 * coordinates of a projected plane. q = 1/w is screen-linear, from 1 to
 * q_ratio across the grid; tu*q and tv*q are screen-linear too (a random
 * matrix, up to 2 texels a pixel where q is 1, and a random offset). Per-
 * vertex random q would describe an edge-on plane for every sliver. Each
 * triangle's measured error is held to the estimator's: within 10% of it
 * plus the registers' rounding.
 */
static void st_random(struct st_stats *stats, unsigned int triangles,
                      double q_ratio, v9x_u32 log2_w, v9x_u32 log2_h)
{
    struct v9x_r2_texture t;
    unsigned int index;
    double width = (double)(1l << log2_w);
    double height = (double)(1l << log2_h);

    memset(stats, 0, sizeof(*stats));
    memset(&t, 0, sizeof(t));
    t.log2_width = log2_w;
    t.log2_height = log2_h;
    t.log2_pitch = log2_w;
    t.format = V9X_R2_TEX_FORMAT_565;
    for (index = 0u; index < triangles; ++index) {
        struct v9x_r2_vertex v[3];
        struct v9x_r2_tex_coord c[3];
        double m[4];
        double offset_u = lcg_unit() * width * 4.0 - width * 2.0;
        double offset_v = lcg_unit() * height * 4.0 - height * 2.0;
        double qa = lcg_unit();
        double qb = lcg_unit() * (1.0 - qa);
        int flip = (lcg_unit() < 0.5);
        double estimate;
        double measured;
        int k;

        for (k = 0; k < 4; ++k) {
            m[k] = lcg_unit() * 4.0 - 2.0;
        }
        for (k = 0; k < 3; ++k) {
            double x;
            double y;
            double f;

            v[k] = vtx(lcg_coord(), lcg_coord());
            x = (double)v[k].x / 16.0;
            y = (double)v[k].y / 16.0;
            /* f in [0, 1] over the grid: q's share of its range. */
            f = (qa * x + qb * y) / (double)GRID;
            if (flip) {
                f = 1.0 - f;
            }
            c[k].q = 1.0 + f * (q_ratio - 1.0);
            c[k].tu = (offset_u + m[0] * x + m[1] * y) / (c[k].q * width);
            c[k].tv = (offset_v + m[2] * x + m[3] * y) / (c[k].q * height);
        }
        measured = st_triangle(v, c, &t, stats);
        if (measured < 0.0 ||
            v9x_r2_texture_error(v, c, &t, &estimate) != V9X_STATUS_OK) {
            continue;
        }
        ++stats->triangles;
        if (estimate > 0.5) {
            ++stats->over_half;
        }
        /* The bounded decision is the grid's, at a tight, the split and a
         * loose limit: the bound may only answer where the grid agrees. */
        for (k = 0; k < 3; ++k) {
            static const v9x_u32 limits[3] = { 100ul, 500ul, 2000ul };
            v9x_u32 within = 2ul;

            if (v9x_r2_texture_error_within(v, c, &t, limits[k], &within,
                                            0) !=
                    V9X_STATUS_OK ||
                within != (estimate * 1000.0 >
                           (double)(v9x_s32)limits[k] ? 0ul : 1ul)) {
                ++stats->within_mismatches;
            }
        }
        if (measured > estimate * 1.1 + 0.01) {
            if (stats->underestimates < 3u) {
                printf("    under: measured %.4f estimate %.4f q %.3f %.3f"
                       " %.3f v (%ld,%ld) (%ld,%ld) (%ld,%ld)\n", measured,
                       estimate, c[0].q, c[1].q, c[2].q, (long)v[0].x,
                       (long)v[0].y, (long)v[1].x, (long)v[1].y,
                       (long)v[2].x, (long)v[2].y);
            }
            ++stats->underestimates;
        }
    }
}

static void st_report(const char *name, const struct st_stats *s)
{
    printf("  texture %s: %u tri, %lu px, %lu texel misses, max %.4f,"
           " %u over 0.5 (est), %u under-estimated, %u refused,"
           " %u sliver traps\n", name, s->triangles, s->pixels,
           s->texel_misses, s->max_error, s->over_half, s->underestimates,
           s->unsupported, s->affine_traps);
}

static void test_texture_setup(void)
{
    struct st_stats affine;
    struct st_stats mild;
    struct st_stats strong;
    struct st_stats wide;

    st_random(&affine, 1500u, 1.0, 5ul, 5ul);
    st_random(&mild, 1500u, 2.0, 5ul, 5ul);
    st_random(&strong, 1500u, 8.0, 5ul, 5ul);
    st_random(&wide, 1500u, 2.0, 8ul, 6ul);
    st_report("affine 32x32", &affine);
    st_report("q 1..2 32x32", &mild);
    st_report("q 1..8 32x32", &strong);
    st_report("q 1..2 256x64", &wide);
    CHECK(affine.pixels > 100000ul);
    /* Affine is the plane: the error is register rounding alone. */
    CHECK(affine.max_error < 0.002);
    CHECK(affine.over_half == 0u);
    CHECK(affine.unsupported == 0u && mild.unsupported == 0u &&
          strong.unsupported == 0u && wide.unsupported == 0u);
    /* The estimator is what subdivision trusts: it must not under-read. */
    CHECK(affine.underestimates == 0u && mild.underestimates == 0u &&
          strong.underestimates == 0u && wide.underestimates == 0u);
    CHECK(affine.within_mismatches == 0u && mild.within_mismatches == 0u &&
          strong.within_mismatches == 0u && wide.within_mismatches == 0u);
}

unsigned int v9x_run_rage2_setup_tests(void)
{
    test_texture_setup();
    test_st_model_card_rows();
    test_st_walk_rules();
    test_channel_out();
    test_shade_constant();
    test_shade_random();
    test_named_triangles();
    test_degenerate();
    test_bounds();
    test_random_triangles();
    test_shared_edge();
    return failures;
}
