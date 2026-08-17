/*
 * VBE 4F00h/4F01h parser tests.
 *
 * The driver cannot try these decisions out: by the time a BIOS answer is
 * wrong, a mode has already been set and the framebuffer is either mapped at
 * the wrong address or striped at the wrong pitch. So the judgement is a pure
 * function over a byte block, and the block shapes below are what the BIOSes
 * actually return - the offsets match the fields tools\diag\vbe_inventory_dos.c
 * dumps from real hardware.
 *
 * The tier-0 rule these encode: refuse anything not clearly drivable. A mode
 * whose stride disagrees with the family's table is refused rather than adapted
 * to, because GDI and the registry already agreed on that pitch.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/vbe_parse.h"

static unsigned int vbe_failures = 0u;

#define VCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++vbe_failures; \
    } \
} while (0)

static v9x_u8 controller_block[V9X_VBE_CONTROLLER_BLOCK_BYTES];
static v9x_u8 mode_block[V9X_VBE_MODE_BLOCK_BYTES];

static void put_u16(v9x_u8 *block, unsigned int offset, v9x_u16 value)
{
    block[offset] = (v9x_u8)(value & 0x00ffu);
    block[offset + 1u] = (v9x_u8)((value >> 8) & 0x00ffu);
}

static void put_u32(v9x_u8 *block, unsigned int offset, v9x_u32 value)
{
    block[offset] = (v9x_u8)(value & 0x000000fful);
    block[offset + 1u] = (v9x_u8)((value >> 8) & 0x000000fful);
    block[offset + 2u] = (v9x_u8)((value >> 16) & 0x000000fful);
    block[offset + 3u] = (v9x_u8)((value >> 24) & 0x000000fful);
}

/* A credible VBE 2.0 controller with 16 MiB reported. */
static void build_controller(void)
{
    memset(controller_block, 0, sizeof(controller_block));
    controller_block[0] = (v9x_u8)'V';
    controller_block[1] = (v9x_u8)'E';
    controller_block[2] = (v9x_u8)'S';
    controller_block[3] = (v9x_u8)'A';
    put_u16(controller_block, 4u, 0x0200u);
    put_u16(controller_block, 18u, 256u);
}

/* 640x480x8 packed-pixel with a linear framebuffer, VBE 2.0 shape (offset 50
 * left zero, as a 2.0 BIOS leaves it in a caller-zeroed block). */
static void build_mode(void)
{
    memset(mode_block, 0, sizeof(mode_block));
    put_u16(mode_block, 0u, 0x00bbu); /* supported | colour | graphics | LFB */
    put_u16(mode_block, 16u, 640u);
    put_u16(mode_block, 18u, 640u);
    put_u16(mode_block, 20u, 480u);
    mode_block[25] = 8u;
    mode_block[27] = 4u;
    put_u32(mode_block, 40u, 0xfd000000ul);
}

static void test_controller_info(void)
{
    struct v9x_vbe_controller_summary summary;

    build_controller();
    VCHECK(v9x_vbe_parse_controller_info(controller_block, &summary) ==
           V9X_TRUE);
    VCHECK(summary.version == 0x0200u);
    VCHECK(summary.total_memory_bytes == 16ul * 1024ul * 1024ul);

    /* QEMU's BIOS reports 3.0; it must not be treated as exotic. */
    build_controller();
    put_u16(controller_block, 4u, 0x0300u);
    put_u16(controller_block, 18u, 64u);
    VCHECK(v9x_vbe_parse_controller_info(controller_block, &summary) ==
           V9X_TRUE);
    VCHECK(summary.version == 0x0300u);
    VCHECK(summary.total_memory_bytes == 4ul * 1024ul * 1024ul);

    /* VBE 1.2 has no linear framebuffer at all. */
    build_controller();
    put_u16(controller_block, 4u, 0x0102u);
    VCHECK(v9x_vbe_parse_controller_info(controller_block, &summary) ==
           V9X_FALSE);
    VCHECK(summary.version == 0u);
    VCHECK(summary.total_memory_bytes == 0ul);

    /* The caller stamps "VBE2" going in; a BIOS that never answered leaves it,
     * and that must not read as success. */
    build_controller();
    controller_block[0] = (v9x_u8)'V';
    controller_block[1] = (v9x_u8)'B';
    controller_block[2] = (v9x_u8)'E';
    controller_block[3] = (v9x_u8)'2';
    VCHECK(v9x_vbe_parse_controller_info(controller_block, &summary) ==
           V9X_FALSE);

    build_controller();
    put_u16(controller_block, 18u, 0u);
    VCHECK(v9x_vbe_parse_controller_info(controller_block, &summary) ==
           V9X_FALSE);

    VCHECK(v9x_vbe_parse_controller_info(0, &summary) == V9X_FALSE);
    VCHECK(summary.version == 0u);
    VCHECK(v9x_vbe_parse_controller_info(controller_block, 0) == V9X_FALSE);
}

static void test_mode_info(void)
{
    struct v9x_vbe_mode_summary summary;

    build_mode();
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_TRUE);
    VCHECK(summary.width == 640u);
    VCHECK(summary.height == 480u);
    VCHECK(summary.bits_per_pixel == 8u);
    VCHECK(summary.memory_model == 4u);
    VCHECK(summary.bytes_per_scan_line == 640u);
    VCHECK(summary.lin_bytes_per_scan_line == 0u);
    VCHECK(summary.phys_base == 0xfd000000ul);

    /* Direct colour at 16 bpp is the other model tier-0 serves. */
    build_mode();
    mode_block[25] = 16u;
    mode_block[27] = 6u;
    put_u16(mode_block, 16u, 1280u);
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_TRUE);
    VCHECK(summary.bits_per_pixel == 16u);
    VCHECK(summary.bytes_per_scan_line == 1280u);

    /* VBE 3.0 linear stride surfaces separately. */
    build_mode();
    put_u16(mode_block, 50u, 1024u);
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_TRUE);
    VCHECK(summary.bytes_per_scan_line == 640u);
    VCHECK(summary.lin_bytes_per_scan_line == 1024u);

    /* Banked-only: the mode exists but there is nothing to map. */
    build_mode();
    put_u16(mode_block, 0u, 0x003bu);
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_FALSE);
    VCHECK(summary.phys_base == 0ul);
    VCHECK(summary.width == 0u);

    /* Not supported in hardware. */
    build_mode();
    put_u16(mode_block, 0u, 0x00bau);
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_FALSE);

    build_mode();
    put_u32(mode_block, 40u, 0ul);
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_FALSE);

    /* An aperture in the first megabyte is real-mode memory, not a card. */
    build_mode();
    put_u32(mode_block, 40u, 0x000a0000ul);
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_FALSE);

    build_mode();
    mode_block[27] = 0u; /* text */
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_FALSE);
    build_mode();
    mode_block[27] = 3u; /* planar - the 4 bpp path vga.drv keeps */
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_FALSE);

    build_mode();
    put_u16(mode_block, 18u, 0u);
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_FALSE);
    build_mode();
    put_u16(mode_block, 16u, 0u);
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_FALSE);

    VCHECK(v9x_vbe_parse_mode_info(0, &summary) == V9X_FALSE);
    VCHECK(v9x_vbe_parse_mode_info(mode_block, 0) == V9X_FALSE);
}

static void test_mode_matches(void)
{
    struct v9x_vbe_mode_summary summary;

    build_mode();
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_TRUE);
    VCHECK(v9x_vbe_mode_matches(&summary, 640u, 480u, 8u, 640u) == V9X_TRUE);

    /* The refusal that matters: a BIOS striding the surface differently from
     * the family table would misplace every scan line. */
    VCHECK(v9x_vbe_mode_matches(&summary, 640u, 480u, 8u, 1024u) == V9X_FALSE);
    VCHECK(v9x_vbe_mode_matches(&summary, 800u, 480u, 8u, 640u) == V9X_FALSE);
    VCHECK(v9x_vbe_mode_matches(&summary, 640u, 600u, 8u, 640u) == V9X_FALSE);
    VCHECK(v9x_vbe_mode_matches(&summary, 640u, 480u, 16u, 640u) == V9X_FALSE);
    VCHECK(v9x_vbe_mode_matches(0, 640u, 480u, 8u, 640u) == V9X_FALSE);

    /* VBE 3.0: the linear stride wins over BytesPerScanLine, so a table pitch
     * agreeing with the wrong one of the two is still a refusal. */
    build_mode();
    put_u16(mode_block, 50u, 1024u);
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_TRUE);
    VCHECK(v9x_vbe_mode_matches(&summary, 640u, 480u, 8u, 1024u) == V9X_TRUE);
    VCHECK(v9x_vbe_mode_matches(&summary, 640u, 480u, 8u, 640u) == V9X_FALSE);
}

/*
 * The drivability rule, exercised directly rather than through a byte block.
 *
 * The mini-VDD hands its answers back in registers, so the 16-bit side builds a
 * summary by hand and applies this rule to it. If the rule only ever ran as part
 * of block parsing, that second caller would be untested.
 */
static void test_mode_summary_is_drivable(void)
{
    struct v9x_vbe_mode_summary s;

    /* The Mach64 VT2's real 0101h answer, as the DOS inventory reported it. */
    s.attributes = 0x00bbu;
    s.bytes_per_scan_line = 640u;
    s.lin_bytes_per_scan_line = 0u;
    s.width = 640u;
    s.height = 480u;
    s.bits_per_pixel = 8u;
    s.memory_model = 4u;
    s.phys_base = 0xe6000000ul;
    VCHECK(v9x_vbe_mode_summary_is_drivable(&s) == V9X_TRUE);

    s.attributes = 0x003bu;              /* no linear framebuffer */
    VCHECK(v9x_vbe_mode_summary_is_drivable(&s) == V9X_FALSE);
    s.attributes = 0x00bau;              /* not supported in hardware */
    VCHECK(v9x_vbe_mode_summary_is_drivable(&s) == V9X_FALSE);
    s.attributes = 0x00bbu;

    s.memory_model = 3u;                 /* planar */
    VCHECK(v9x_vbe_mode_summary_is_drivable(&s) == V9X_FALSE);
    s.memory_model = 6u;                 /* direct colour is fine */
    VCHECK(v9x_vbe_mode_summary_is_drivable(&s) == V9X_TRUE);
    s.memory_model = 4u;

    s.phys_base = 0x000a0000ul;          /* inside the first megabyte */
    VCHECK(v9x_vbe_mode_summary_is_drivable(&s) == V9X_FALSE);
    s.phys_base = 0ul;
    VCHECK(v9x_vbe_mode_summary_is_drivable(&s) == V9X_FALSE);
    s.phys_base = 0xe6000000ul;

    s.bytes_per_scan_line = 0u;
    VCHECK(v9x_vbe_mode_summary_is_drivable(&s) == V9X_FALSE);
    s.bytes_per_scan_line = 640u;
    s.width = 0u;
    VCHECK(v9x_vbe_mode_summary_is_drivable(&s) == V9X_FALSE);

    VCHECK(v9x_vbe_mode_summary_is_drivable(0) == V9X_FALSE);
}

unsigned int v9x_run_vbe_parse_tests(void)
{
    test_controller_info();
    test_mode_info();
    test_mode_matches();
    test_mode_summary_is_drivable();
    return vbe_failures;
}
