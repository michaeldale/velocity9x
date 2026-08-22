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

/* A credible VBE 2.0 controller with 16 MiB reported, a switchable DAC and a
 * BIOS revision. */
static void build_controller(void)
{
    memset(controller_block, 0, sizeof(controller_block));
    controller_block[0] = (v9x_u8)'V';
    controller_block[1] = (v9x_u8)'E';
    controller_block[2] = (v9x_u8)'S';
    controller_block[3] = (v9x_u8)'A';
    put_u16(controller_block, 4u, 0x0200u);
    put_u32(controller_block, 10u, 1ul);
    put_u16(controller_block, 18u, 256u);
    put_u16(controller_block, 20u, 0x0204u);
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

/*
 * Legacy (VBE 2) and linear (VBE 3) channel layouts, written into the two sets
 * of offsets a real ModeInfoBlock carries. Size then position for each of red,
 * green, blue and reserved, exactly as the BIOS orders them.
 */
static void put_legacy_channels(v9x_u16 rs, v9x_u16 rp, v9x_u16 gs, v9x_u16 gp,
                                v9x_u16 bs, v9x_u16 bp, v9x_u16 xs, v9x_u16 xp)
{
    mode_block[31] = (v9x_u8)rs;
    mode_block[32] = (v9x_u8)rp;
    mode_block[33] = (v9x_u8)gs;
    mode_block[34] = (v9x_u8)gp;
    mode_block[35] = (v9x_u8)bs;
    mode_block[36] = (v9x_u8)bp;
    mode_block[37] = (v9x_u8)xs;
    mode_block[38] = (v9x_u8)xp;
}

static void put_linear_channels(v9x_u16 rs, v9x_u16 rp, v9x_u16 gs, v9x_u16 gp,
                                v9x_u16 bs, v9x_u16 bp, v9x_u16 xs, v9x_u16 xp)
{
    mode_block[54] = (v9x_u8)rs;
    mode_block[55] = (v9x_u8)rp;
    mode_block[56] = (v9x_u8)gs;
    mode_block[57] = (v9x_u8)gp;
    mode_block[58] = (v9x_u8)bs;
    mode_block[59] = (v9x_u8)bp;
    mode_block[60] = (v9x_u8)xs;
    mode_block[61] = (v9x_u8)xp;
}

/* A direct-colour mode at one depth, geometry left at 640x480. */
static void build_direct_mode(v9x_u16 bits_per_pixel, v9x_u16 pitch)
{
    build_mode();
    mode_block[25] = (v9x_u8)bits_per_pixel;
    mode_block[27] = 6u; /* direct colour */
    put_u16(mode_block, 16u, pitch);
}

/*
 * The two controller fields carried for identification rather than for any
 * decision: the capability bits, and the BIOS's own revision number.
 *
 * The reason they exist is in docs\specifications\dos-vbe-conformance.md: across
 * a corpus of 200 cards, defects tracked the video BIOS revision rather than the
 * chip, to the point where swapping a BIOS between two cards moved the fault
 * with it. A driver that records only chip identity cannot attribute a report
 * from an untested card to the one variable that predicts behaviour.
 */
static void test_controller_identity(void)
{
    struct v9x_vbe_controller_summary summary;

    build_controller();
    VCHECK(v9x_vbe_parse_controller_info(controller_block, &summary) ==
           V9X_TRUE);
    VCHECK(summary.oem_software_rev == 0x0204u);
    VCHECK((summary.capabilities & V9X_VBE_CAP_DAC_SWITCHABLE) != 0u);
    VCHECK((summary.capabilities & V9X_VBE_CAP_NOT_VGA_COMPAT) == 0u);

    /* A controller that says it is not VGA-compatible is reported as such, not
     * refused: every text-mode restore and Safe Mode fallback in this driver
     * assumes otherwise, so the fact has to be visible in diagnostics even
     * though nothing acts on it. */
    build_controller();
    put_u32(controller_block, 10u, 0x0000000ful);
    VCHECK(v9x_vbe_parse_controller_info(controller_block, &summary) ==
           V9X_TRUE);
    VCHECK((summary.capabilities & V9X_VBE_CAP_NOT_VGA_COMPAT) != 0u);
    VCHECK((summary.capabilities & V9X_VBE_CAP_RAMDAC_BLANK) != 0u);
    VCHECK((summary.capabilities & V9X_VBE_CAP_STEREO_SIGNAL) != 0u);

    /* Sparse is not incredible. A BIOS reporting no capabilities and no
     * revision still describes a usable controller, and refusing it over a
     * diagnostic field would cost the driver its aperture for nothing. */
    build_controller();
    put_u32(controller_block, 10u, 0ul);
    put_u16(controller_block, 20u, 0u);
    VCHECK(v9x_vbe_parse_controller_info(controller_block, &summary) ==
           V9X_TRUE);
    VCHECK(summary.capabilities == 0ul);
    VCHECK(summary.oem_software_rev == 0u);
    VCHECK(summary.total_memory_bytes == 16ul * 1024ul * 1024ul);

    /* And a refused block leaves them zeroed with everything else. */
    build_controller();
    controller_block[1] = (v9x_u8)'X';
    VCHECK(v9x_vbe_parse_controller_info(controller_block, &summary) ==
           V9X_FALSE);
    VCHECK(summary.capabilities == 0ul);
    VCHECK(summary.oem_software_rev == 0u);
    VCHECK(summary.version == 0u);

    /* The clear helper covers every field, including any added later. */
    summary.version = 1u;
    summary.total_memory_bytes = 1ul;
    summary.capabilities = 1ul;
    summary.oem_software_rev = 1u;
    v9x_vbe_controller_summary_clear(&summary);
    VCHECK(summary.version == 0u);
    VCHECK(summary.total_memory_bytes == 0ul);
    VCHECK(summary.capabilities == 0ul);
    VCHECK(summary.oem_software_rev == 0u);
    v9x_vbe_controller_summary_clear(0);
}

static void test_colour_field_source(void)
{
    struct v9x_vbe_mode_summary summary;

    /*
     * VBE 2: only the legacy set is written, because a 2.0 BIOS does not know
     * the linear offsets exist and the caller zeroed the block. 5:6:5.
     */
    build_direct_mode(16u, 1280u);
    put_legacy_channels(5u, 11u, 6u, 5u, 5u, 0u, 0u, 0u);
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_TRUE);
    VCHECK(summary.red_mask_size == 5u);
    VCHECK(summary.red_field_position == 11u);
    VCHECK(summary.green_mask_size == 6u);
    VCHECK(summary.blue_mask_size == 5u);
    VCHECK((summary.mask_flags & V9X_VBE_RF_MASKS_LEGACY) != 0u);
    VCHECK((summary.mask_flags & V9X_VBE_RF_MASKS_LINEAR) == 0u);
    VCHECK((summary.mask_flags & V9X_VBE_RF_LIN_STRIDE) == 0u);
    VCHECK(summary.significant_depth == 16u);

    /* VBE 3: the linear set is written and is the one that describes what a
     * mode set with the linear bit actually produces. */
    build_direct_mode(32u, 2560u);
    put_u16(mode_block, 50u, 2560u);
    put_linear_channels(8u, 16u, 8u, 8u, 8u, 0u, 8u, 24u);
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_TRUE);
    VCHECK(summary.red_mask_size == 8u);
    VCHECK(summary.red_field_position == 16u);
    VCHECK(summary.blue_field_position == 0u);
    VCHECK(summary.rsvd_mask_size == 8u);
    VCHECK(summary.rsvd_field_position == 24u);
    VCHECK((summary.mask_flags & V9X_VBE_RF_MASKS_LINEAR) != 0u);
    VCHECK((summary.mask_flags & V9X_VBE_RF_MASKS_LEGACY) == 0u);
    VCHECK((summary.mask_flags & V9X_VBE_RF_LIN_STRIDE) != 0u);
    VCHECK(summary.significant_depth == 24u);

    /*
     * Both sets present and disagreeing. The linear set wins - it is the one
     * that describes the aperture this driver draws into - and the legacy set
     * is not merged in, because a half-linear half-legacy layout describes no
     * surface at all.
     */
    build_direct_mode(32u, 2560u);
    put_legacy_channels(8u, 0u, 8u, 8u, 8u, 16u, 8u, 24u);   /* BGR */
    put_linear_channels(8u, 16u, 8u, 8u, 8u, 0u, 8u, 24u);   /* RGB */
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_TRUE);
    VCHECK(summary.red_field_position == 16u);
    VCHECK(summary.blue_field_position == 0u);
    VCHECK((summary.mask_flags & V9X_VBE_RF_MASKS_LINEAR) != 0u);

    /* A BIOS that writes only one linear byte has still written the set: the
     * whole point of the flag test is that zero means "not reported". */
    build_direct_mode(16u, 1280u);
    put_legacy_channels(5u, 11u, 6u, 5u, 5u, 0u, 0u, 0u);
    mode_block[57] = 5u; /* LinGreenFieldPosition alone */
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_TRUE);
    VCHECK((summary.mask_flags & V9X_VBE_RF_MASKS_LINEAR) != 0u);
    VCHECK(summary.red_mask_size == 0u);

    /* Palettized: no channels at all, and no claim about where they came from. */
    build_mode();
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_TRUE);
    VCHECK(summary.mask_flags == 0u);
    VCHECK(summary.significant_depth == 8u);

    /*
     * Stale scratch, which is what the zero-before-every-call rule exists to
     * prevent. Here a 16-bpp mode's block still holds the previous 32-bpp
     * mode's linear channels: 8:8:8 in a 16-bit pixel. The parser must not
     * describe the mode with them - the channels do not fit, so the layout is
     * refused rather than transposed, and the significant depth comes out zero
     * rather than 24.
     */
    build_direct_mode(16u, 1280u);
    put_linear_channels(8u, 16u, 8u, 8u, 8u, 0u, 8u, 24u);
    VCHECK(v9x_vbe_parse_mode_info(mode_block, &summary) == V9X_TRUE);
    VCHECK(summary.significant_depth == 0u);
    {
        v9x_u32 red = 1ul;
        v9x_u32 green = 1ul;
        v9x_u32 blue = 1ul;
        VCHECK(v9x_vbe_masks_to_bits(&summary, &red, &green, &blue) ==
               V9X_FALSE);
        VCHECK(red == 0ul && green == 0ul && blue == 0ul);
    }
}

static void test_significant_depth(void)
{
    struct v9x_vbe_mode_summary summary;

    v9x_vbe_mode_summary_clear(&summary);
    VCHECK(v9x_vbe_summary_significant_depth(&summary) == 0u);
    VCHECK(v9x_vbe_summary_significant_depth(0) == 0u);

    /* Palettized 8 bpp: 8 and 8. An index is not a channel, but every bit of
     * it is significant, and leftover channel bytes must not change that. */
    summary.bits_per_pixel = 8u;
    VCHECK(v9x_vbe_summary_significant_depth(&summary) == 8u);
    summary.red_mask_size = 8u;
    summary.green_mask_size = 8u;
    summary.blue_mask_size = 8u;
    VCHECK(v9x_vbe_summary_significant_depth(&summary) == 8u);

    /* 16/16 - the 5:6:5 that is the only 16-bpp layout this driver programs,
     * both spelled out and left to the all-zero convention. */
    v9x_vbe_mode_summary_clear(&summary);
    summary.bits_per_pixel = 16u;
    summary.red_mask_size = 5u;
    summary.green_mask_size = 6u;
    summary.blue_mask_size = 5u;
    VCHECK(v9x_vbe_summary_significant_depth(&summary) == 16u);
    v9x_vbe_mode_summary_clear(&summary);
    summary.bits_per_pixel = 16u;
    VCHECK(v9x_vbe_summary_significant_depth(&summary) == 16u);

    /* 5:5:5 in 16 storage bits: 15 significant. Reported honestly here and
     * rejected by admission policy, rather than rounded up to 16 and drawn
     * wrong. */
    v9x_vbe_mode_summary_clear(&summary);
    summary.bits_per_pixel = 16u;
    summary.red_mask_size = 5u;
    summary.green_mask_size = 5u;
    summary.blue_mask_size = 5u;
    VCHECK(v9x_vbe_summary_significant_depth(&summary) == 15u);

    /* 24/24 packed and 32/24 XRGB: the pair BitsPerPixel alone cannot tell
     * apart. Both are derived; only 32 is admitted, and that is policy
     * elsewhere, not a gap here. */
    v9x_vbe_mode_summary_clear(&summary);
    summary.bits_per_pixel = 24u;
    summary.red_mask_size = 8u;
    summary.green_mask_size = 8u;
    summary.blue_mask_size = 8u;
    VCHECK(v9x_vbe_summary_significant_depth(&summary) == 24u);
    summary.bits_per_pixel = 32u;
    VCHECK(v9x_vbe_summary_significant_depth(&summary) == 24u);
    summary.rsvd_mask_size = 8u;
    VCHECK(v9x_vbe_summary_significant_depth(&summary) == 24u);

    /* 24 and 32 bpp with no channels reported have no convention to fall back
     * on: inventing one is how a scanned mode ends up transposed. */
    v9x_vbe_mode_summary_clear(&summary);
    summary.bits_per_pixel = 24u;
    VCHECK(v9x_vbe_summary_significant_depth(&summary) == 0u);
    summary.bits_per_pixel = 32u;
    VCHECK(v9x_vbe_summary_significant_depth(&summary) == 0u);

    /* Channels that claim more bits than the pixel holds describe nothing. */
    v9x_vbe_mode_summary_clear(&summary);
    summary.bits_per_pixel = 16u;
    summary.red_mask_size = 8u;
    summary.green_mask_size = 8u;
    summary.blue_mask_size = 8u;
    VCHECK(v9x_vbe_summary_significant_depth(&summary) == 0u);
}

static void test_summary_clear(void)
{
    struct v9x_vbe_mode_summary summary;

    /* A caller filling a summary from registers rather than a block starts
     * here, so every field has to land at zero - including any field added
     * after this test was written, which is why it checks the lot. */
    summary.attributes = 1u;
    summary.bytes_per_scan_line = 1u;
    summary.lin_bytes_per_scan_line = 1u;
    summary.width = 1u;
    summary.height = 1u;
    summary.bits_per_pixel = 1u;
    summary.significant_depth = 1u;
    summary.mask_flags = 1u;
    summary.memory_model = 1u;
    summary.phys_base = 1ul;
    summary.red_mask_size = 1u;
    summary.red_field_position = 1u;
    summary.green_mask_size = 1u;
    summary.green_field_position = 1u;
    summary.blue_mask_size = 1u;
    summary.blue_field_position = 1u;
    summary.rsvd_mask_size = 1u;
    summary.rsvd_field_position = 1u;

    v9x_vbe_mode_summary_clear(&summary);
    VCHECK(summary.attributes == 0u);
    VCHECK(summary.bytes_per_scan_line == 0u);
    VCHECK(summary.lin_bytes_per_scan_line == 0u);
    VCHECK(summary.width == 0u);
    VCHECK(summary.height == 0u);
    VCHECK(summary.bits_per_pixel == 0u);
    VCHECK(summary.significant_depth == 0u);
    VCHECK(summary.mask_flags == 0u);
    VCHECK(summary.memory_model == 0u);
    VCHECK(summary.phys_base == 0ul);
    VCHECK(summary.red_mask_size == 0u);
    VCHECK(summary.red_field_position == 0u);
    VCHECK(summary.green_mask_size == 0u);
    VCHECK(summary.green_field_position == 0u);
    VCHECK(summary.blue_mask_size == 0u);
    VCHECK(summary.blue_field_position == 0u);
    VCHECK(summary.rsvd_mask_size == 0u);
    VCHECK(summary.rsvd_field_position == 0u);

    /* And a null pointer is a no-op, not a fault: the clear runs on paths that
     * have already decided to refuse. */
    v9x_vbe_mode_summary_clear(0);
}

unsigned int v9x_run_vbe_parse_tests(void)
{
    test_controller_info();
    test_mode_info();
    test_mode_matches();
    test_mode_summary_is_drivable();
    test_controller_identity();
    test_colour_field_source();
    test_significant_depth();
    test_summary_clear();
    return vbe_failures;
}
