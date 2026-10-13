#include <stdio.h>

#include "velocity9x/tdfx_vsa100.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define MIB(n) ((v9x_u32)(n) * 1048576ul)

/*
 * The Mini-Me v2 (single VSA-100) after its BIOS mode set, measured by
 * VelocityNT on Windows 2000
 * (docs\decisions\2026-10-11-what-velocitynt-measured-on-the-mini-me-vsa100.md): dramInit0
 * 0x607EADF9 decodes to 4 parts of 128 Mbit, and a full write/read sweep of
 * [1 MiB, 64 MiB) confirmed 64 MiB. Tile control 0x3FFF, compare 0, put the
 * first tiled page at 0x01FFF000, so only just under 32 MiB is linear.
 */
#define MINI_ME_DRAM_INIT0   0x607eadf9ul
#define MINI_ME_TILE_CONTROL 0x00003ffful
#define MINI_ME_TILE_COMPARE 0x00000000ul

/*
 * The retail Voodoo4 4500 AGP under 3dfx 1.04.00 on A8U4I5
 * (docs\decisions\2026-10-11-voodoo4-stock-driver-tiles-its-desktop-at-the-top-of-memory.md):
 * 32 MiB, desktop tiled from 01F60000h at 16 bpp, PLL1 at the 166 MHz
 * graphics clock 3dfx Tools reports, PLL0 at the 640x480 59.94 Hz pixel
 * clock.
 */
#define VOODOO4_DRAM_INIT0   0x581ea9a9ul
#define VOODOO4_TILE_CONTROL 0x000a3f60ul
#define VOODOO4_PLL0         0x0000d137ul
#define VOODOO4_PLL1         0x0000e721ul

static void test_memory_decode(void)
{
    CHECK(v9x_tdfx_vsa100_decode_memory(MINI_ME_DRAM_INIT0) == MIB(64));
    CHECK(v9x_tdfx_vsa100_decode_memory(VOODOO4_DRAM_INIT0) == MIB(32));

    /* Constructed 32 MiB boards: 4 parts of 64 Mbit (bit 26 clear, density
     * 3), and 8 parts of 32 Mbit (bit 26 set, density 2). */
    CHECK(v9x_tdfx_vsa100_decode_memory(0x18000000ul) == MIB(32));
    CHECK(v9x_tdfx_vsa100_decode_memory(0x14000000ul) == MIB(32));

    /* Smallest: 4 parts of 8 Mbit. */
    CHECK(v9x_tdfx_vsa100_decode_memory(0x00000000ul) == MIB(4));

    /* Density codes 5-7 are undefined and refuse. */
    CHECK(v9x_tdfx_vsa100_decode_memory(0x28000000ul) == 0ul);
    CHECK(v9x_tdfx_vsa100_decode_memory(0x38000000ul) == 0ul);

    /* Bits outside 29:26 do not change the size. */
    CHECK(v9x_tdfx_vsa100_decode_memory(0x18000000ul | 0xc3fffffful) ==
          MIB(32));
}

static void test_tile_boundary(void)
{
    CHECK(v9x_tdfx_vsa100_tile_first_page(MINI_ME_TILE_CONTROL,
                                          MINI_ME_TILE_COMPARE) == 0x1ffful);

    /* The Voodoo4's boundary sits at its desktop start, 01F60000h. */
    CHECK(v9x_tdfx_vsa100_tile_first_page(VOODOO4_TILE_CONTROL, 0ul) *
          V9X_TDFX_TILE_PAGE_BYTES == 0x01f60000ul);

    /* Control bits 24:23 are page bits 14:13. */
    CHECK(v9x_tdfx_vsa100_tile_first_page(0x01801234ul, 0ul) == 0x7234ul);

    /* Compare bit 15 set overrides control: VelocityNT's measured 64 MiB
     * boundary was compare 0xC000, i.e. page 0x4000. */
    CHECK(v9x_tdfx_vsa100_tile_first_page(MINI_ME_TILE_CONTROL, 0xc000ul) ==
          0x4000ul);
}

static void test_usable_memory(void)
{
    /* Mini-Me: 64 MiB decoded, linear view ends one page short of 32 MiB. */
    CHECK(v9x_tdfx_vsa100_usable_memory(MINI_ME_DRAM_INIT0,
                                        MINI_ME_TILE_CONTROL,
                                        MINI_ME_TILE_COMPARE) ==
          0x01fff000ul);

    /* The 64 MiB boundary VelocityNT set reaches all 64 MiB. */
    CHECK(v9x_tdfx_vsa100_usable_memory(MINI_ME_DRAM_INIT0,
                                        MINI_ME_TILE_CONTROL, 0xc000ul) ==
          MIB(64));

    /* A 32 MiB board under a boundary at 64 MiB is limited by its memory. */
    CHECK(v9x_tdfx_vsa100_usable_memory(0x18000000ul, MINI_ME_TILE_CONTROL,
                                        0xc000ul) == MIB(32));

    /* Undecodable straps give nothing, whatever the boundary says. */
    CHECK(v9x_tdfx_vsa100_usable_memory(0x38000000ul, MINI_ME_TILE_CONTROL,
                                        0xc000ul) == 0ul);
}

static void test_pll(void)
{
    /* The Mini-Me's unused PLL0, 0x00000C01: M = 0 refuses. */
    CHECK(v9x_tdfx_vsa100_pll_khz(0x00000c01ul) == 0ul);

    /* The Voodoo4: PLL0 25.176 MHz (800x525 at 59.94 Hz), PLL1 the
     * 166 MHz graphics clock. */
    CHECK(v9x_tdfx_vsa100_pll_khz(VOODOO4_PLL0) == 25176ul);
    CHECK(v9x_tdfx_vsa100_pll_khz(VOODOO4_PLL1) == 166806ul);

    /* N = 0x34, M = 4, K = 1: 14318.18 x 54 / (6 x 2) = 64431 kHz. */
    CHECK(v9x_tdfx_vsa100_pll_khz((0x34ul << 8) | (4ul << 2) | 1ul) ==
          64431ul);

    /* N = 0xFF, M = 1, K = 0: the largest numerator does not overflow.
     * 14318.18 x 257 / 3 = 1226590.75, truncated. */
    CHECK(v9x_tdfx_vsa100_pll_khz((0xfful << 8) | (1ul << 2)) == 1226590ul);
}

/*
 * The desktop/Glide split. The Voodoo4's linear memory under the BIOS tile
 * control is 01FFF000h; Glide's FIFO starts at 18000h and its buffers end
 * at the fbRam it is told (A8U4I5 boot 424,
 * docs\issues\2026-10-12-glide-cmdfifo-overlaps-the-desktop.md).
 */
static void test_desktop_layout(void)
{
    v9x_u32 linear;

    linear = v9x_tdfx_vsa100_usable_memory(VOODOO4_DRAM_INIT0,
                                           V9X_TDFX_LFB_BIOS_TILE_CONTROL,
                                           0ul);
    CHECK(linear == 0x01fff000ul);

    /* Half, rounded down to a MiB: 15 MiB, so the desktop and DirectDraw
     * get the 17 MiB above and Glide the memory below less the guard. */
    CHECK(v9x_tdfx_vsa100_desktop_offset(linear) == MIB(15));
    CHECK(v9x_tdfx_vsa100_glide_bytes(linear) == MIB(14));

    /* Glide's FIFO and the largest desktop this family sets (1024x768x16)
     * clear each other with room to spare. */
    CHECK(v9x_tdfx_vsa100_desktop_offset(linear) > 0x00117000ul);
    CHECK(linear - v9x_tdfx_vsa100_desktop_offset(linear) >
          1024ul * 768ul * 2ul);

    /* The Mini-Me under the same tile control: 32 MiB less a page linear,
     * whatever its 64 MiB of parts. */
    CHECK(v9x_tdfx_vsa100_desktop_offset(
              v9x_tdfx_vsa100_usable_memory(MINI_ME_DRAM_INIT0,
                                            V9X_TDFX_LFB_BIOS_TILE_CONTROL,
                                            0ul)) == MIB(15));

    /* The smallest split, and below it none: the desktop stays at 0 and
     * Glide is told all of it, as before the split existed. */
    CHECK(v9x_tdfx_vsa100_desktop_offset(MIB(8)) == MIB(4));
    CHECK(v9x_tdfx_vsa100_glide_bytes(MIB(8)) == MIB(3));
    CHECK(v9x_tdfx_vsa100_desktop_offset(MIB(8) - 1ul) == 0ul);
    CHECK(v9x_tdfx_vsa100_glide_bytes(MIB(8) - 1ul) == MIB(8) - 1ul);
    CHECK(v9x_tdfx_vsa100_desktop_offset(0ul) == 0ul);
    CHECK(v9x_tdfx_vsa100_glide_bytes(0ul) == 0ul);
}

static void test_desktop_registers(void)
{
    /* What the BIOS mode set leaves at 640x480x16 is already right. */
    CHECK(v9x_tdfx_vsa100_desktop_vidproc(0x30040c81ul) == 0x30040c81ul);

    /* A Glide session that never closed: overlay on, desktop off. Only
     * those two bits change. */
    CHECK(v9x_tdfx_vsa100_desktop_vidproc(0x32e50501ul) == 0x32e50481ul);

    /* A tiled desktop is made linear; nothing else moves. */
    CHECK(v9x_tdfx_vsa100_desktop_vidproc(0x31040c81ul) == 0x30040c81ul);

    /* Stride: Glide's tiled 10/10 becomes 1280 linear bytes for the
     * desktop, the overlay half kept. */
    CHECK(v9x_tdfx_vsa100_desktop_stride(0x000a000aul, 1280ul) ==
          0x000a0500ul);
    CHECK(v9x_tdfx_vsa100_desktop_stride(0x00000500ul, 2048ul) ==
          0x00000800ul);
    /* A pitch wider than the field cannot be expressed and is masked, not
     * allowed to spill into the overlay stride. */
    CHECK(v9x_tdfx_vsa100_desktop_stride(0ul, 0x00018000ul) == 0ul);
}

unsigned int v9x_run_vsa100_decode_tests(void)
{
    failures = 0u;
    test_memory_decode();
    test_tile_boundary();
    test_usable_memory();
    test_desktop_layout();
    test_desktop_registers();
    test_pll();
    return failures;
}
