#include <stdio.h>
#include <string.h>

#include "velocity9x/mga_3d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define GRID_ROWS 8u
#define GRID_COLS 24

/* What the model plotted, -1 where it plotted nothing. */
static long grid[GRID_ROWS][GRID_COLS];
static unsigned int plotted_outside;

static void clear_grid(void)
{
    unsigned int row;
    int col;

    for (row = 0u; row < GRID_ROWS; ++row) {
        for (col = 0; col < GRID_COLS; ++col) {
            grid[row][col] = -1L;
        }
    }
    plotted_outside = 0u;
}

static void plot(void *context, v9x_s32 x, v9x_u32 row, v9x_u32 pixel)
{
    (void)context;
    if (x < 0L || x >= GRID_COLS || row >= GRID_ROWS) {
        ++plotted_outside;
        return;
    }
    grid[row][x] = (long)pixel;
}

/* The first and one-past-last plotted column of a row, both -1 if none. */
static void span(unsigned int row, int *first, int *end)
{
    int col;

    *first = -1;
    *end = -1;
    for (col = 0; col < GRID_COLS; ++col) {
        if (grid[row][col] != -1L) {
            if (*first < 0) {
                *first = col;
            }
            *end = col + 1;
        }
    }
}

static v9x_u32 reg_value(const struct v9x_mga3d_writes *writes,
                         v9x_u32 offset)
{
    v9x_u32 index;

    for (index = 0u; index < writes->count; ++index) {
        if (writes->offsets[index] == offset) {
            return writes->values[index];
        }
    }
    return 0xdeadbeeful;
}

static void base_trap(struct v9x_mga3d_trap *trap)
{
    memset(trap, 0, sizeof(*trap));
    trap->vram_bytes = 0x00400000ul;
    trap->target_offset = 0x00300000ul;
    trap->pitch_bytes = 1024ul * 4ul;
    trap->bytes_per_pixel = 4ul;
    trap->top = 0ul;
    trap->color = 0x00a0b0c0ul;
}

/* A 45-degree apex: spec 4-35's terms for each edge, and one more column
 * each side per row. */
static void test_flat_apex(void)
{
    struct v9x_mga3d_trap trap;
    struct v9x_mga3d_writes writes;
    int first;
    int end;

    base_trap(&trap);
    trap.length = 4ul;
    trap.left.x = 10L;
    trap.left.dx = -4L;
    trap.left.dy = 4L;
    trap.right.x = 11L;
    trap.right.dx = 4L;
    trap.right.dy = 4L;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_AR0) == 4ul);
    CHECK(reg_value(&writes, V9X_MGA_AR1) == 0xfffffffful); /* -4 + 4 - 1 */
    CHECK(reg_value(&writes, V9X_MGA_AR2) == 0xfffffffcul);
    CHECK(reg_value(&writes, V9X_MGA_AR4) == 0xfffffffcul); /* -dX */
    CHECK(reg_value(&writes, V9X_MGA_AR5) == 0xfffffffcul);
    CHECK(reg_value(&writes, V9X_MGA_AR6) == 4ul);
    CHECK(reg_value(&writes, V9X_MGA_SGN) == 0x00000002ul);
    CHECK(reg_value(&writes, V9X_MGA_FXBNDRY) == ((11ul << 16) | 10ul));
    CHECK(reg_value(&writes, V9X_MGA_PITCH) == 1024ul);
    CHECK(reg_value(&writes, V9X_MGA_YDSTORG) == 0x000c0000ul);
    CHECK(reg_value(&writes, V9X_MGA_FCOL) == 0x00a0b0c0ul);
    CHECK(reg_value(&writes, V9X_MGA_DWGCTL) == 0x000c4804ul);
    CHECK(writes.offsets[writes.count - 1u] ==
          V9X_MGA_YDSTLEN + V9X_MGA_GO);
    CHECK(writes.values[writes.count - 1u] == 4ul);
    CHECK(reg_value(&writes, V9X_MGA_DR4) == 0xdeadbeeful);

    clear_grid();
    CHECK(v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_EDGE, plot, 0, 0) ==
          V9X_STATUS_OK);
    CHECK(plotted_outside == 0u);
    span(0u, &first, &end);
    CHECK(first == 10 && end == 11);
    span(1u, &first, &end);
    CHECK(first == 9 && end == 12);
    span(3u, &first, &end);
    CHECK(first == 7 && end == 14);
    CHECK(grid[3][7] == 0x00a0b0c0L);
}

/* Shallow edges: half a column per row. Rightward the engine steps on the
 * first row boundary, leftward on the second (the asymmetric rounding the
 * error terms imply). */
static void test_flat_shallow(void)
{
    struct v9x_mga3d_trap trap;
    int first;
    int end;

    base_trap(&trap);
    trap.length = 4ul;
    trap.left.x = 10L;
    trap.left.dx = -2L;
    trap.left.dy = 4L;
    trap.right.x = 12L;
    trap.right.dx = 2L;
    trap.right.dy = 4L;
    clear_grid();
    CHECK(v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_EDGE, plot, 0, 0) ==
          V9X_STATUS_OK);
    span(0u, &first, &end);
    CHECK(first == 10 && end == 12);
    span(1u, &first, &end);
    CHECK(first == 10 && end == 13);
    span(2u, &first, &end);
    CHECK(first == 9 && end == 13);
    span(3u, &first, &end);
    CHECK(first == 9 && end == 14);
}

/* The left edge moves one column a row; red rises one level a pixel. The
 * two hypotheses differ on the second row's first pixel. */
static void test_gouraud_fold(void)
{
    struct v9x_mga3d_trap trap;
    struct v9x_mga3d_writes writes;

    base_trap(&trap);
    trap.shade = V9X_MGA3D_SHADE_GOURAUD;
    trap.color = 0x7f000000ul;
    trap.length = 2ul;
    trap.left.x = 0L;
    trap.left.dx = 2L;
    trap.left.dy = 2L;
    trap.right.x = 8L;
    trap.right.dx = 0L;
    trap.right.dy = 2L;
    trap.red[0] = 10L * V9X_MGA3D_COLOR_ONE;
    trap.red[1] = V9X_MGA3D_COLOR_ONE;
    trap.red[2] = 0L;
    trap.green[0] = 0x40L * V9X_MGA3D_COLOR_ONE;
    trap.blue[2] = -1L;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_DWGCTL) == 0x000c4074ul);
    CHECK(reg_value(&writes, V9X_MGA_DR4) == 0x00050000ul);
    CHECK(reg_value(&writes, V9X_MGA_DR6) == 0x00008000ul);
    CHECK(reg_value(&writes, V9X_MGA_DR15) == 0x00fffffful);
    CHECK(reg_value(&writes, V9X_MGA_FCOL) == 0x7f000000ul);
    CHECK(writes.offsets[writes.count - 1u] ==
          V9X_MGA_YDSTLEN + V9X_MGA_GO);

    clear_grid();
    CHECK(v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_EDGE, plot, 0, 0) ==
          V9X_STATUS_OK);
    CHECK(grid[0][0] == 0x7f0a4000L);
    CHECK(grid[0][7] == 0x7f114000L);
    CHECK(grid[1][0] == -1L);
    CHECK(grid[1][1] == 0x7f0b4000L);

    clear_grid();
    CHECK(v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_NONE, plot, 0, 0) ==
          V9X_STATUS_OK);
    CHECK(grid[1][1] == 0x7f0a4000L);
}

static void test_refusals(void)
{
    struct v9x_mga3d_trap trap;
    struct v9x_mga3d_writes writes;

    base_trap(&trap);
    trap.length = 2ul;
    trap.left.x = 5L;
    trap.left.dx = 4L;
    trap.left.dy = 2L;
    trap.right.x = 6L;
    trap.right.dx = 0L;
    trap.right.dy = 2L;
    /* The edges cross on the second row. */
    CHECK(v9x_mga3d_build_trap(&trap, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(writes.count == 0u);

    trap.left.dx = 0L;
    trap.left.dy = 0L;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_UNSUPPORTED);

    /* The left edge reaches column -1 on the second row. */
    trap.left.dy = 2L;
    trap.left.x = 0L;
    trap.left.dx = -2L;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);

    trap.left.dx = 0L;
    trap.shade = V9X_MGA3D_SHADE_GOURAUD;
    trap.bytes_per_pixel = 1ul;
    trap.pitch_bytes = 1024ul;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_UNSUPPORTED);

    trap.bytes_per_pixel = 2ul;
    trap.pitch_bytes = 2048ul;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_OK);
    /* 16 bpp shading is dithered: the model does not claim it. */
    CHECK(v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_EDGE, plot, 0, 0) ==
          V9X_STATUS_UNSUPPORTED);

    trap.red[1] = 0x00800000L;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_UNSUPPORTED);

    /* One row past the end of VRAM: 0x100 rows of 4 KiB fill the last
     * MiB exactly. */
    base_trap(&trap);
    trap.length = 0x101ul;
    trap.left.dy = 1L;
    trap.right.x = 4L;
    trap.right.dy = 1L;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* A pitch the linearizer has no entry for. */
    base_trap(&trap);
    trap.length = 1ul;
    trap.left.dy = 1L;
    trap.right.x = 4L;
    trap.right.dy = 1L;
    trap.pitch_bytes = 1000ul * 4ul;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_UNSUPPORTED);
}

/* The model's Z buffer for the depth tests. */
static unsigned long zgrid[GRID_ROWS][GRID_COLS];

static void fill_zgrid(unsigned long value)
{
    unsigned int row;
    int col;

    for (row = 0u; row < GRID_ROWS; ++row) {
        for (col = 0; col < GRID_COLS; ++col) {
            zgrid[row][col] = value;
        }
    }
}

static v9x_u32 z_read(void *context, v9x_s32 x, v9x_u32 row)
{
    (void)context;
    return (x >= 0L && x < GRID_COLS && row < GRID_ROWS)
        ? zgrid[row][x] : 0xfffffffful;
}

static void z_write(void *context, v9x_s32 x, v9x_u32 row, v9x_u32 value)
{
    (void)context;
    if (x >= 0L && x < GRID_COLS && row < GRID_ROWS) {
        zgrid[row][x] = value;
    }
}

static const struct v9x_mga3d_depth_io depth_io = { z_read, z_write, 0 };

/* A Gouraud trapezoid over columns 0-7, two rows, with 16-bit Z beside it:
 * colour at 0x300000 (origin 0xC0000 pixels), Z at 0x380000, so ZORG is
 * 0x380000 - 2 x 0xC0000. Depth runs 100, 101, ... across a row. */
static void base_depth(struct v9x_mga3d_trap *trap)
{
    base_trap(trap);
    trap->shade = V9X_MGA3D_SHADE_GOURAUD;
    trap->color = 0ul;
    trap->length = 2ul;
    trap->left.x = 0L;
    trap->left.dy = 2L;
    trap->right.x = 8L;
    trap->right.dy = 2L;
    trap->red[0] = 0x10L * V9X_MGA3D_COLOR_ONE;
    trap->depth = V9X_MGA3D_DEPTH_16;
    trap->z_offset = 0x00380000ul;
    trap->z[0] = 100L * V9X_MGA3D_Z_ONE;
    trap->z[1] = V9X_MGA3D_Z_ONE;
}

static void test_depth16(void)
{
    struct v9x_mga3d_trap trap;
    struct v9x_mga3d_writes writes;

    base_depth(&trap);
    trap.zmode = V9X_MGA3D_ZMODE_ZLT;
    trap.z_write = 1ul;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_DR0) == 0x00320000ul);
    CHECK(reg_value(&writes, V9X_MGA_DR2) == 0x00008000ul);
    CHECK(reg_value(&writes, V9X_MGA_DR3) == 0ul);
    CHECK(reg_value(&writes, V9X_MGA_ZORG) == 0x00200000ul);
    CHECK(reg_value(&writes, V9X_MGA_MACCESS) == 0x00000002ul);
    /* TRAP, atype ZI, zmode ZLT. */
    CHECK(reg_value(&writes, V9X_MGA_DWGCTL) == 0x000c4434ul);
    CHECK(reg_value(&writes, V9X_MGA_DR0_Z32_LSB) == 0xdeadbeeful);
    CHECK(writes.offsets[writes.count - 1u] ==
          V9X_MGA_YDSTLEN + V9X_MGA_GO);

    /* Stored 104 everywhere: 100..103 pass, are drawn and written. */
    clear_grid();
    fill_zgrid(104ul);
    CHECK(v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_EDGE, plot, 0,
                               &depth_io) == V9X_STATUS_OK);
    CHECK(grid[0][3] != -1L);
    CHECK(grid[0][4] == -1L);
    CHECK(zgrid[0][0] == 100ul);
    CHECK(zgrid[1][3] == 103ul);
    CHECK(zgrid[0][4] == 104ul);

    /* atype I: the same compare reversed, and nothing written. */
    trap.zmode = V9X_MGA3D_ZMODE_ZGTE;
    trap.z_write = 0ul;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_DWGCTL) == 0x000c4774ul);
    clear_grid();
    fill_zgrid(104ul);
    CHECK(v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_EDGE, plot, 0,
                               &depth_io) == V9X_STATUS_OK);
    CHECK(grid[0][3] == -1L);
    CHECK(grid[0][4] != -1L);
    CHECK(zgrid[0][4] == 104ul);

    /* Past 65535 the stored value stays at 65535, as measured. */
    base_depth(&trap);
    trap.z_write = 1ul;
    trap.z[0] = 65534L * V9X_MGA3D_Z_ONE;
    clear_grid();
    fill_zgrid(7ul);
    CHECK(v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_EDGE, plot, 0,
                               &depth_io) == V9X_STATUS_OK);
    CHECK(zgrid[0][0] == 65534ul);
    CHECK(zgrid[0][1] == 65535ul);
    CHECK(zgrid[0][2] == 65535ul);
    CHECK(zgrid[0][7] == 65535ul);

    /* Below 0 it is 0. */
    trap.z[0] = -2L * V9X_MGA3D_Z_ONE;
    fill_zgrid(7ul);
    CHECK(v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_EDGE, plot, 0,
                               &depth_io) == V9X_STATUS_OK);
    CHECK(zgrid[0][1] == 0ul);
    CHECK(zgrid[0][2] == 0ul);
    CHECK(zgrid[0][3] == 1ul);
}

static void test_depth32(void)
{
    struct v9x_mga3d_trap trap;
    struct v9x_mga3d_writes writes;

    base_depth(&trap);
    trap.depth = V9X_MGA3D_DEPTH_32;
    trap.z_write = 1ul;
    /* 0x12345678 in 33.15: 0x91A_2B3C0000. */
    trap.z32[0].hi = 0x091aL;
    trap.z32[0].lo = 0x2b3c0000ul;
    trap.z32[1].lo = 0x00008000ul;
    /* Minus one a row: -1 in 33.15. */
    trap.z32[2].hi = -1L;
    trap.z32[2].lo = 0xffff8000ul;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_OK);
    CHECK(reg_value(&writes, V9X_MGA_DR0_Z32_LSB) == 0x2b3c0000ul);
    CHECK(reg_value(&writes, V9X_MGA_DR0_Z32_MSB) == 0x0000091aul);
    CHECK(reg_value(&writes, V9X_MGA_DR3_Z32_MSB) == 0x0000fffful);
    CHECK(reg_value(&writes, V9X_MGA_DR0) == 0xdeadbeeful);
    /* Z at 4 bytes a pixel: 0x380000 - 4 x 0xC0000. */
    CHECK(reg_value(&writes, V9X_MGA_ZORG) == 0x00080000ul);
    CHECK(reg_value(&writes, V9X_MGA_MACCESS) == 0x0000000aul);
    CHECK(writes.count <= V9X_MGA3D_MAX_WRITES);

    clear_grid();
    fill_zgrid(0ul);
    CHECK(v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_EDGE, plot, 0,
                               &depth_io) == V9X_STATUS_OK);
    CHECK(zgrid[0][0] == 0x12345678ul);
    CHECK(zgrid[0][7] == 0x1234567ful);
    CHECK(zgrid[1][0] == 0x12345677ul);

    /* From 0xFFFFFFFC through 2^32: held at 0xFFFFFFFF, as measured. */
    trap.z32[0].hi = 0x7fffL;
    trap.z32[0].lo = 0xfffe0000ul;
    fill_zgrid(0ul);
    CHECK(v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_EDGE, plot, 0,
                               &depth_io) == V9X_STATUS_OK);
    CHECK(zgrid[0][0] == 0xfffffffcul);
    CHECK(zgrid[0][3] == 0xfffffffful);
    CHECK(zgrid[0][4] == 0xfffffffful);
    CHECK(zgrid[0][7] == 0xfffffffful);
}

static void test_depth_refusals(void)
{
    struct v9x_mga3d_trap trap;
    struct v9x_mga3d_writes writes;

    base_depth(&trap);
    trap.zmode = 1ul;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);

    base_depth(&trap);
    trap.shade = V9X_MGA3D_SHADE_FLAT;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_UNSUPPORTED);

    /* Z over the colour rows. */
    base_depth(&trap);
    trap.z_offset = 0x00300000ul;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* A ZORG with low bits set. */
    base_depth(&trap);
    trap.z_offset = 0x00380100ul;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_UNSUPPORTED);

    /* Z below the address the origin implies: ZORG would be negative. */
    base_depth(&trap);
    trap.z_offset = 0x00100000ul;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_UNSUPPORTED);

    /* A 32-bit start past the MSB register's 16 bits. */
    base_depth(&trap);
    trap.depth = V9X_MGA3D_DEPTH_32;
    trap.z32[0].hi = 0x8000L;
    CHECK(v9x_mga3d_build_trap(&trap, &writes) == V9X_STATUS_UNSUPPORTED);

    /* The model needs the Z buffer. */
    base_depth(&trap);
    CHECK(v9x_mga3d_model_trap(&trap, V9X_MGA3D_FOLD_EDGE, plot, 0, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_mga_3d_tests(void)
{
    failures = 0u;
    test_flat_apex();
    test_flat_shallow();
    test_gouraud_fold();
    test_refusals();
    test_depth16();
    test_depth32();
    test_depth_refusals();
    return failures;
}
