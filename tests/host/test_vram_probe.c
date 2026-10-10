#include <stdio.h>

#include "velocity9x/vram_probe.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define MIB 0x00100000ul

/*
 * The card as the walk sees it: memory of installed bytes that wraps (or,
 * with floats set, reads back 0xffffffff above the end). Writes go highest
 * point first, as the hook does them.
 */
static void simulate(v9x_u32 window, v9x_u32 installed, int floats,
                     v9x_u32 *readback, v9x_u32 *count)
{
    v9x_u32 memory[V9X_VRAM_PROBE_MAX_POINTS];
    v9x_u32 index;
    v9x_u32 points = v9x_vram_probe_points(window);
    v9x_u32 installed_points = installed / V9X_VRAM_PROBE_STEP;

    for (index = 0u; index < V9X_VRAM_PROBE_MAX_POINTS; ++index) {
        memory[index] = 0u;
    }
    for (index = points; index-- != 0u;) {
        if (index < installed_points) {
            memory[index] = v9x_vram_probe_signature(index);
        } else if (!floats) {
            memory[index % installed_points] =
                v9x_vram_probe_signature(index);
        }
    }
    for (index = 0u; index < points; ++index) {
        readback[index] = index < installed_points
            ? memory[index] : (floats ? 0xfffffffful
                                      : memory[index % installed_points]);
    }
    *count = points;
}

static void test_geometry(void)
{
    CHECK(v9x_vram_probe_points(16ul * MIB) == 32u);
    CHECK(v9x_vram_probe_points(64ul * MIB) == 32u);
    CHECK(v9x_vram_probe_points(8ul * MIB) == 16u);
    CHECK(v9x_vram_probe_points(0x7fffful) == 0u);
    CHECK(v9x_vram_probe_offset(0u) == 0x200ul);
    CHECK(v9x_vram_probe_offset(15u) == 0x00780200ul);
    CHECK(v9x_vram_probe_signature(3u) == 0x5a3c0003ul);
}

static void test_sizes(void)
{
    v9x_u32 readback[V9X_VRAM_PROBE_MAX_POINTS];
    v9x_u32 count;

    /* A8U4I5's 2164W: 8 MiB in a 16 MiB window, wrapping. */
    simulate(16ul * MIB, 8ul * MIB, 0, readback, &count);
    CHECK(v9x_vram_probe_size(readback, count) == 8ul * MIB);
    /* The same card if the upper half floats. */
    simulate(16ul * MIB, 8ul * MIB, 1, readback, &count);
    CHECK(v9x_vram_probe_size(readback, count) == 8ul * MIB);
    /* 86Box's 4 MiB 2064W in an 8 MiB window. */
    simulate(8ul * MIB, 4ul * MIB, 0, readback, &count);
    CHECK(v9x_vram_probe_size(readback, count) == 4ul * MIB);
    /* A full window: every point holds. */
    simulate(16ul * MIB, 16ul * MIB, 0, readback, &count);
    CHECK(v9x_vram_probe_size(readback, count) == 16ul * MIB);
    /* A failed walk: point 0 lost. */
    readback[0] = 0u;
    CHECK(v9x_vram_probe_size(readback, count) == 0u);
    CHECK(v9x_vram_probe_size(0, 4u) == 0u);
}

static void test_accept(void)
{
    /* The card corrects its BIOS. */
    CHECK(v9x_vram_probe_accept(8ul * MIB, 4ul * MIB, 16ul * MIB) ==
          8ul * MIB);
    /* Agreement. */
    CHECK(v9x_vram_probe_accept(8ul * MIB, 8ul * MIB, 16ul * MIB) ==
          8ul * MIB);
    /* Less than the BIOS says: the walk is doubted, not the BIOS. */
    CHECK(v9x_vram_probe_accept(2ul * MIB, 4ul * MIB, 16ul * MIB) ==
          4ul * MIB);
    /* A failed walk keeps the BIOS figure. */
    CHECK(v9x_vram_probe_accept(0u, 4ul * MIB, 16ul * MIB) == 4ul * MIB);
    /* Past the window cannot have been measured. */
    CHECK(v9x_vram_probe_accept(32ul * MIB, 4ul * MIB, 16ul * MIB) ==
          4ul * MIB);
    /* No BIOS figure: the measurement stands alone. */
    CHECK(v9x_vram_probe_accept(8ul * MIB, 0u, 16ul * MIB) == 8ul * MIB);
}

unsigned int v9x_run_vram_probe_tests(void)
{
    failures = 0u;
    test_geometry();
    test_sizes();
    test_accept();
    return failures;
}
