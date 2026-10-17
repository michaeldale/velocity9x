#include <stdio.h>

#include "velocity9x/intel_hsw_display.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

/*
 * The offset split, worked by hand. 3DMark 99's back buffer at 800x600x16,
 * D3dTargetOffset 001D4C00h (boot 15): page 001D4000h, rest C00h = 3072
 * bytes, x 1536. The /flip probe's row 383 at pitch 2112: C57C0h, page
 * C5000h, rest 7C0h, x 992 at 16 bpp, 496 at 32.
 */
static void test_plane_split(void)
{
    v9x_u32 surf = 0xfffffffful;
    v9x_u32 offset = 0xfffffffful;

    CHECK(v9x_intel_hsw_plane_split(0x001d4c00ul, 2ul, &surf, &offset) ==
          V9X_STATUS_OK);
    CHECK(surf == 0x001d4000ul && offset == 1536ul);
    CHECK(v9x_intel_hsw_plane_split(0x000c57c0ul, 2ul, &surf, &offset) ==
          V9X_STATUS_OK);
    CHECK(surf == 0x000c5000ul && offset == 992ul);
    CHECK(v9x_intel_hsw_plane_split(0x000c57c0ul, 4ul, &surf, &offset) ==
          V9X_STATUS_OK);
    CHECK(surf == 0x000c5000ul && offset == 496ul);
    /* The desktop, and a page-aligned buffer: x 0. */
    CHECK(v9x_intel_hsw_plane_split(0ul, 2ul, &surf, &offset) ==
          V9X_STATUS_OK);
    CHECK(surf == 0ul && offset == 0ul);
    CHECK(v9x_intel_hsw_plane_split(0x00200000ul, 1ul, &surf, &offset) ==
          V9X_STATUS_OK);
    CHECK(surf == 0x00200000ul && offset == 0ul);
    /* 8 bpp: every byte is a pixel, the largest x is FFFh. */
    CHECK(v9x_intel_hsw_plane_split(0x00001ffful, 1ul, &surf, &offset) ==
          V9X_STATUS_OK);
    CHECK(surf == 0x00001000ul && offset == 0x0ffful);

    /* Not a whole pixel, or not a depth the plane scans: refused, and the
     * outputs are not written. */
    surf = 0x12345678ul;
    offset = 0x9abcdef0ul;
    CHECK(v9x_intel_hsw_plane_split(0x00001001ul, 2ul, &surf, &offset) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_plane_split(0x00001002ul, 4ul, &surf, &offset) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_intel_hsw_plane_split(0x00001000ul, 3ul, &surf, &offset) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(surf == 0x12345678ul && offset == 0x9abcdef0ul);
    CHECK(v9x_intel_hsw_plane_split(0ul, 2ul, 0, &offset) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

/* Boot 14's words: PIPEACONF C0000010h, DSPACNTR C8000000h. */
static void test_scanout_live(void)
{
    CHECK(v9x_intel_hsw_scanout_live(0xc0000010ul, 0xc8000000ul));
    /* Enabled but not running, not enabled, or the plane off. */
    CHECK(!v9x_intel_hsw_scanout_live(0x80000010ul, 0xc8000000ul));
    CHECK(!v9x_intel_hsw_scanout_live(0x40000010ul, 0xc8000000ul));
    CHECK(!v9x_intel_hsw_scanout_live(0xc0000010ul, 0x48000000ul));
}

/*
 * Boot 14's TRANS_VTOTAL_A, 020C01DFh: vactive 480, vtotal 525. PIPEADSL
 * reads one line behind the beam (i915's scanline_offset 1 on DDI parts;
 * the /flip probe, boot 16, saw the latch at DSL 767 of a 768-line mode),
 * so DSL vactive - 1 is the first blank line.
 */
static void test_in_vblank(void)
{
    CHECK(!v9x_intel_hsw_in_vblank(0ul, 0x020c01dful));
    CHECK(!v9x_intel_hsw_in_vblank(478ul, 0x020c01dful));
    CHECK(v9x_intel_hsw_in_vblank(479ul, 0x020c01dful));
    CHECK(v9x_intel_hsw_in_vblank(523ul, 0x020c01dful));
    /* The last blank line reads vtotal - 2; vtotal - 1 is the beam on line
     * 0 again, which is active. */
    CHECK(v9x_intel_hsw_in_vblank(523ul, 0x020c01dful));
    CHECK(!v9x_intel_hsw_in_vblank(524ul, 0x020c01dful));
    /* Boot 16's 1024x768 timing, 032502FFh: the latch line. */
    CHECK(v9x_intel_hsw_in_vblank(767ul, 0x032502fful));
    CHECK(!v9x_intel_hsw_in_vblank(766ul, 0x032502fful));
    /* Bits above the line field are not the line. */
    CHECK(!v9x_intel_hsw_in_vblank(0x80000010ul, 0x020c01dful));
}

unsigned int v9x_run_intel_hsw_display_tests(void)
{
    failures = 0u;
    test_plane_split();
    test_scanout_live();
    test_in_vblank();
    return failures;
}
