#include <stdio.h>
#include <string.h>

#include "velocity9x/mga_setup.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* Triangles stay inside this many pixels each way. */
#define GRID 128

/* What the setup and the engine model drew, and what the reference says
 * should be: 0 untouched, else the pixel. */
static unsigned long drawn[GRID][GRID];
static unsigned char wanted[GRID][GRID];
static unsigned int outside;
static unsigned int overdrawn;

static void plot(void *context, v9x_s32 x, v9x_u32 row, v9x_u32 pixel)
{
    v9x_u32 top = *(const v9x_u32 *)context;

    row += top;
    if (x < 0L || x >= GRID || row >= (v9x_u32)GRID) {
        ++outside;
        return;
    }
    if (drawn[row][x] != 0ul) {
        ++overdrawn;
    }
    drawn[row][x] = pixel | 0x01000000ul;
}

static unsigned long seed = 12345ul;

static v9x_s32 random_below(v9x_s32 limit)
{
    seed = seed * 1103515245ul + 12345ul;
    return (v9x_s32)((seed >> 8) % (unsigned long)limit);
}

/*
 * The reference: row r is in when its centre yc = 16r + 8 lies in
 * [top, bottom), and pixel X when 16X + 8 lies in [x_left, x_right) of the
 * centre line's intersection with the triangle. An edge a -> b (a.y < b.y)
 * meets the line when a.y <= yc < b.y, at x = a.x + dx (yc - a.y) / dy,
 * compared exactly by cross-multiplying.
 */
static int ref_inside(const struct v9x_mga_setup_vertex *v, v9x_s32 px,
                      v9x_s32 yc)
{
    int crossings = 0;
    /* Each crossing as a fraction num / den with den > 0. */
    double x_at[3];
    unsigned int edge;
    double left;
    double right;

    for (edge = 0u; edge < 3u; ++edge) {
        const struct v9x_mga_setup_vertex *a = &v[edge];
        const struct v9x_mga_setup_vertex *b = &v[(edge + 1u) % 3u];

        if (a->y > b->y) {
            const struct v9x_mga_setup_vertex *swap = a;

            a = b;
            b = swap;
        }
        if (a->y <= yc && yc < b->y) {
            /* Exact as a double: every term is an integer below 2^31. */
            x_at[crossings++] = (double)a->x +
                (double)(b->x - a->x) * (double)(yc - a->y) /
                (double)(b->y - a->y);
        }
    }
    if (crossings < 2) {
        return 0;
    }
    left = x_at[0] < x_at[1] ? x_at[0] : x_at[1];
    right = x_at[0] < x_at[1] ? x_at[1] : x_at[0];
    return (double)px >= left && (double)px < right;
}

static void reference(const struct v9x_mga_setup_vertex *v)
{
    int row;
    int col;

    for (row = 0; row < GRID; ++row) {
        for (col = 0; col < GRID; ++col) {
            wanted[row][col] = (unsigned char)ref_inside(
                v, 16L * col + 8L, 16L * row + 8L);
        }
    }
}

static void base_flat(struct v9x_mga3d_trap *base)
{
    memset(base, 0, sizeof(*base));
    base->vram_bytes = 0x00800000ul;
    base->target_offset = 0ul;
    base->pitch_bytes = 1024ul * 4ul;
    base->bytes_per_pixel = 4ul;
    base->shade = V9X_MGA3D_SHADE_FLAT;
    base->color = 0x00123456ul;
}

/* Run one triangle through setup and the model; count disagreements with
 * the reference. */
static unsigned int run_coverage(const struct v9x_mga_setup_vertex *v)
{
    struct v9x_mga3d_trap base;
    struct v9x_mga3d_trap traps[V9X_MGA_SETUP_TRAPS];
    v9x_u32 count = 0ul;
    v9x_u32 index;
    unsigned int wrong = 0u;
    int row;
    int col;

    base_flat(&base);
    memset(drawn, 0, sizeof(drawn));
    outside = 0u;
    overdrawn = 0u;
    reference(v);
    CHECK(v9x_mga_setup_triangle(&base, v, traps, &count) == V9X_STATUS_OK);
    for (index = 0ul; index < count; ++index) {
        CHECK(v9x_mga3d_model_trap(&traps[index], V9X_MGA3D_FOLD_EDGE, plot,
                                   &traps[index].top, 0) == V9X_STATUS_OK);
    }
    for (row = 0; row < GRID; ++row) {
        for (col = 0; col < GRID; ++col) {
            if ((drawn[row][col] != 0ul) != (wanted[row][col] != 0u)) {
                ++wrong;
            }
        }
    }
    return wrong + outside + overdrawn;
}

static void test_random_coverage(void)
{
    struct v9x_mga_setup_vertex v[3];
    unsigned int trial;
    unsigned int corner;
    unsigned int bad = 0u;

    memset(v, 0, sizeof(v));
    for (trial = 0u; trial < 3000u; ++trial) {
        for (corner = 0u; corner < 3u; ++corner) {
            if ((trial & 1u) != 0u) {
                /* Half-pixel lattice: centres and edges coincide. */
                v[corner].x = random_below(GRID * 2) * 8L;
                v[corner].y = random_below(GRID * 2) * 8L;
            } else {
                v[corner].x = random_below(GRID * 16);
                v[corner].y = random_below(GRID * 16);
            }
        }
        if (run_coverage(v) != 0u) {
            if (bad++ < 3u) {
                printf("  coverage mismatch: (%ld,%ld) (%ld,%ld) (%ld,%ld)\n",
                       (long)v[0].x, (long)v[0].y, (long)v[1].x,
                       (long)v[1].y, (long)v[2].x, (long)v[2].y);
            }
        }
    }
    CHECK(bad == 0u);
}

/* Two triangles sharing an edge cover each pixel of their union once:
 * the top-left rule, end to end. */
static void test_shared_edge(void)
{
    struct v9x_mga_setup_vertex a[3];
    struct v9x_mga_setup_vertex b[3];
    struct v9x_mga3d_trap base;
    struct v9x_mga3d_trap traps[V9X_MGA_SETUP_TRAPS];
    v9x_u32 count;
    v9x_u32 index;
    unsigned int trial;

    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    base_flat(&base);
    for (trial = 0u; trial < 500u; ++trial) {
        a[0].x = random_below(GRID * 16);
        a[0].y = random_below(GRID * 16);
        a[1].x = random_below(GRID * 16);
        a[1].y = random_below(GRID * 16);
        a[2].x = random_below(GRID * 16);
        a[2].y = random_below(GRID * 16);
        b[0] = a[0];
        b[1] = a[1];
        b[2].x = random_below(GRID * 16);
        b[2].y = random_below(GRID * 16);
        memset(drawn, 0, sizeof(drawn));
        outside = 0u;
        overdrawn = 0u;
        CHECK(v9x_mga_setup_triangle(&base, a, traps, &count) ==
              V9X_STATUS_OK);
        for (index = 0ul; index < count; ++index) {
            v9x_mga3d_model_trap(&traps[index], V9X_MGA3D_FOLD_EDGE, plot,
                                 &traps[index].top, 0);
        }
        CHECK(v9x_mga_setup_triangle(&base, b, traps, &count) ==
              V9X_STATUS_OK);
        for (index = 0ul; index < count; ++index) {
            v9x_mga3d_model_trap(&traps[index], V9X_MGA3D_FOLD_EDGE, plot,
                                 &traps[index].top, 0);
        }
        /* Only when the third corners lie on opposite sides of the shared
         * edge do the triangles tile; overlap is then a double draw. */
        if (((double)(a[1].x - a[0].x) * (a[2].y - a[0].y) -
             (double)(a[1].y - a[0].y) * (a[2].x - a[0].x)) *
            ((double)(b[1].x - b[0].x) * (b[2].y - b[0].y) -
             (double)(b[1].y - b[0].y) * (b[2].x - b[0].x)) < 0.0) {
            CHECK(overdrawn == 0u);
        }
    }
}

/* Gouraud red follows its plane at every drawn pixel, rounded to nearest
 * (the setup's half level under the engine's truncation). A sliver whose
 * colour runs past the field is refused, and must be rare. */
static void test_gouraud_plane(void)
{
    struct v9x_mga_setup_vertex v[3];
    struct v9x_mga3d_trap base;
    struct v9x_mga3d_trap traps[V9X_MGA_SETUP_TRAPS];
    v9x_u32 count;
    v9x_u32 index;
    unsigned int trial;
    unsigned int bad = 0u;
    unsigned int refused = 0u;
    v9x_status status;
    int row;
    int col;

    memset(v, 0, sizeof(v));
    base_flat(&base);
    base.shade = V9X_MGA3D_SHADE_GOURAUD;
    base.color = 0ul;
    for (trial = 0u; trial < 300u; ++trial) {
        double det;

        for (index = 0ul; index < 3ul; ++index) {
            v[index].x = random_below(GRID * 16);
            v[index].y = random_below(GRID * 16);
            v[index].red = (double)random_below(256);
        }
        memset(drawn, 0, sizeof(drawn));
        outside = 0u;
        overdrawn = 0u;
        status = v9x_mga_setup_triangle(&base, v, traps, &count);
        if (status == V9X_STATUS_UNSUPPORTED) {
            ++refused;
            continue;
        }
        CHECK(status == V9X_STATUS_OK);
        for (index = 0ul; index < count; ++index) {
            CHECK(v9x_mga3d_model_trap(&traps[index], V9X_MGA3D_FOLD_EDGE,
                                       plot, &traps[index].top, 0) ==
                  V9X_STATUS_OK);
        }
        det = (double)(v[1].x - v[0].x) * (v[2].y - v[0].y) -
              (double)(v[2].x - v[0].x) * (v[1].y - v[0].y);
        if (det == 0.0) {
            continue;
        }
        for (row = 0; row < GRID; ++row) {
            for (col = 0; col < GRID; ++col) {
                double px;
                double py;
                double l1;
                double l2;
                double red;
                long got;

                if (drawn[row][col] == 0ul) {
                    continue;
                }
                px = 16.0 * col + 8.0;
                py = 16.0 * row + 8.0;
                l1 = ((px - v[0].x) * (v[2].y - v[0].y) -
                      (py - v[0].y) * (v[2].x - v[0].x)) / det;
                l2 = ((py - v[0].y) * (v[1].x - v[0].x) -
                      (px - v[0].x) * (v[1].y - v[0].y)) / det;
                red = v[0].red + l1 * (v[1].red - v[0].red) +
                      l2 * (v[2].red - v[0].red);
                got = (long)((drawn[row][col] >> 16) & 0xfful);
                if ((double)got < red - 0.51 || (double)got > red + 0.51) {
                    ++bad;
                }
            }
        }
    }
    CHECK(bad == 0u);
    CHECK(refused < 15u);
}

static void test_refusals(void)
{
    struct v9x_mga_setup_vertex v[3];
    struct v9x_mga3d_trap base;
    struct v9x_mga3d_trap traps[V9X_MGA_SETUP_TRAPS];
    v9x_u32 count = 7ul;

    memset(v, 0, sizeof(v));
    base_flat(&base);
    v[1].x = 160L;
    v[2].y = 160L;
    v[2].x = -1L;
    CHECK(v9x_mga_setup_triangle(&base, v, traps, &count) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(count == 0ul);
    /* A line covers nothing. */
    v[2].x = 320L;
    v[2].y = 0L;
    CHECK(v9x_mga_setup_triangle(&base, v, traps, &count) == V9X_STATUS_OK);
    CHECK(count == 0ul);
    /* 32-bit depth is not set up. */
    v[2].y = 160L;
    base.depth = V9X_MGA3D_DEPTH_32;
    CHECK(v9x_mga_setup_triangle(&base, v, traps, &count) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_mga_setup_tests(void)
{
    failures = 0u;
    test_random_coverage();
    test_shared_edge();
    test_gouraud_plane();
    test_refusals();
    return failures;
}
