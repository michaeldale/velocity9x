/*
 * The EDID detailed timing, and the VBE 3.0 CRTC block built from it.
 *
 * The positive case is a real panel: the AUO B101AW03 in the Acer NAV50 whose
 * video BIOS lists eighteen OEM mode numbers and describes none of them
 * (docs\decisions\2026-08-28-pineview-vbe-mode-list.md). Its EDID is the
 * whole reason this path exists, so it is the corpus rather than a synthetic
 * block - a builder that cannot express that panel is of no use.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/edid.h"
#include "velocity9x/vbe_crtc.h"

static unsigned int crtc_failures = 0u;

#define CRTCCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #expression); \
        ++crtc_failures; \
    } \
} while (0)

/* The NAV50 survey report, [EDID] Block0.00 through Block0.70. */
static const v9x_u8 nav50_edid[128] = {
    0x00u,0xFFu,0xFFu,0xFFu,0xFFu,0xFFu,0xFFu,0x00u,
    0x06u,0xAFu,0xD2u,0x30u,0x00u,0x00u,0x00u,0x00u,
    0x01u,0x12u,0x01u,0x03u,0x80u,0x16u,0x0Du,0x78u,
    0x0Au,0xB9u,0xA5u,0x96u,0x59u,0x57u,0x91u,0x28u,
    0x1Fu,0x50u,0x54u,0x00u,0x00u,0x00u,0x01u,0x01u,
    0x01u,0x01u,0x01u,0x01u,0x01u,0x01u,0x01u,0x01u,
    0x01u,0x01u,0x01u,0x01u,0x01u,0x01u,0x50u,0x14u,
    0x00u,0x40u,0x41u,0x58u,0x2Cu,0x20u,0x18u,0x88u,
    0x31u,0x00u,0xDFu,0x7Du,0x00u,0x00u,0x00u,0x18u,
    0x00u,0x00u,0x00u,0x0Fu,0x00u,0x00u,0x00u,0x00u,
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
    0x00u,0x20u,0x00u,0x00u,0x00u,0xFEu,0x00u,0x41u,
    0x55u,0x4Fu,0x0Au,0x20u,0x20u,0x20u,0x20u,0x20u,
    0x20u,0x20u,0x20u,0x20u,0x00u,0x00u,0x00u,0xFEu,
    0x00u,0x42u,0x31u,0x30u,0x31u,0x41u,0x57u,0x30u,
    0x33u,0x20u,0x56u,0x30u,0x20u,0x0Au,0x00u,0x3Cu
};

static v9x_u16 read16(const v9x_u8 *block, v9x_u16 at)
{
    return (v9x_u16)(block[at] | ((v9x_u16)block[at + 1u] << 8));
}

static v9x_u32 read32(const v9x_u8 *block, v9x_u16 at)
{
    return (v9x_u32)block[at] |
           ((v9x_u32)block[at + 1u] << 8) |
           ((v9x_u32)block[at + 2u] << 16) |
           ((v9x_u32)block[at + 3u] << 24);
}

static void test_nav50_timing(void)
{
    struct v9x_edid_timing timing;

    CRTCCHECK(v9x_edid_parse_timing(nav50_edid, &timing) == V9X_TRUE);

    /* 0x1450 clock units of 10 kHz. */
    CRTCCHECK(timing.pixel_clock_hz == 52000000ul);
    CRTCCHECK(timing.h_active == 1024u);
    CRTCCHECK(timing.h_blank == 320u);
    CRTCCHECK(timing.h_sync_offset == 24u);
    CRTCCHECK(timing.h_sync_width == 136u);
    CRTCCHECK(timing.v_active == 600u);
    CRTCCHECK(timing.v_blank == 44u);
    CRTCCHECK(timing.v_sync_offset == 3u);
    CRTCCHECK(timing.v_sync_width == 1u);

    /* Descriptor flags 0x18: digital separate, both polarities negative. */
    CRTCCHECK((timing.flags & V9X_EDID_TIMING_DIGITAL_SEPARATE) != 0u);
    CRTCCHECK((timing.flags & V9X_EDID_TIMING_HSYNC_NEGATIVE) != 0u);
    CRTCCHECK((timing.flags & V9X_EDID_TIMING_VSYNC_NEGATIVE) != 0u);
}

static void test_nav50_crtc_block(void)
{
    struct v9x_edid_timing timing;
    v9x_u8 block[V9X_VBE_CRTC_BYTES];
    v9x_u16 index;

    CRTCCHECK(v9x_edid_parse_timing(nav50_edid, &timing) == V9X_TRUE);
    memset(block, 0xAAu, sizeof(block));
    CRTCCHECK(v9x_vbe_crtc_build(&timing, block) == V9X_TRUE);

    CRTCCHECK(read16(block, V9X_VBE_CRTC_HTOTAL) == 1344u);
    CRTCCHECK(read16(block, V9X_VBE_CRTC_HSYNC_START) == 1048u);
    CRTCCHECK(read16(block, V9X_VBE_CRTC_HSYNC_END) == 1184u);
    CRTCCHECK(read16(block, V9X_VBE_CRTC_VTOTAL) == 644u);
    CRTCCHECK(read16(block, V9X_VBE_CRTC_VSYNC_START) == 603u);
    CRTCCHECK(read16(block, V9X_VBE_CRTC_VSYNC_END) == 604u);

    CRTCCHECK(block[V9X_VBE_CRTC_FLAGS] ==
              (v9x_u8)(V9X_VBE_CRTC_FLAG_HSYNC_NEG |
                       V9X_VBE_CRTC_FLAG_VSYNC_NEG));
    CRTCCHECK(read32(block, V9X_VBE_CRTC_PIXEL_CLOCK) == 52000000ul);

    /* 52,000,000 / (1344 * 644) = 60.0783 Hz, truncated to hundredths. */
    CRTCCHECK(read16(block, V9X_VBE_CRTC_REFRESH) == 6007u);

    /* The reserved tail must be zero, not the 0xAA the buffer was filled
     * with: the specification reserves it and a BIOS may read it. */
    for (index = 19u; index < V9X_VBE_CRTC_BYTES; ++index) {
        CRTCCHECK(block[index] == 0u);
    }
}

static void test_refusals_leave_the_block_zeroed(void)
{
    struct v9x_edid_timing timing;
    struct v9x_edid_timing broken;
    v9x_u8 block[V9X_VBE_CRTC_BYTES];
    v9x_u16 index;

    CRTCCHECK(v9x_edid_parse_timing(nav50_edid, &timing) == V9X_TRUE);

    CRTCCHECK(v9x_vbe_crtc_build(0, block) == V9X_FALSE);
    CRTCCHECK(v9x_vbe_crtc_build(&timing, 0) == V9X_FALSE);

    /* No clock: nothing to program the dot generator with. */
    broken = timing;
    broken.pixel_clock_hz = 0ul;
    memset(block, 0xAAu, sizeof(block));
    CRTCCHECK(v9x_vbe_crtc_build(&broken, block) == V9X_FALSE);
    for (index = 0u; index < V9X_VBE_CRTC_BYTES; ++index) {
        CRTCCHECK(block[index] == 0u);
    }

    /* No blanking: a total equal to its active count leaves no room for the
     * sync, and is what a descriptor of all zeroes would decode to. */
    broken = timing;
    broken.h_blank = 0u;
    CRTCCHECK(v9x_vbe_crtc_build(&broken, block) == V9X_FALSE);

    broken = timing;
    broken.v_blank = 0u;
    CRTCCHECK(v9x_vbe_crtc_build(&broken, block) == V9X_FALSE);

    broken = timing;
    broken.h_active = 0u;
    CRTCCHECK(v9x_vbe_crtc_build(&broken, block) == V9X_FALSE);

    /* A sync that ends past the total describes a frame the CRTC cannot scan
     * out, and is the shape a mis-decoded upper-bit nibble produces. */
    broken = timing;
    broken.h_sync_width = broken.h_blank;
    CRTCCHECK(v9x_vbe_crtc_build(&broken, block) == V9X_FALSE);

    broken = timing;
    broken.v_sync_offset = broken.v_blank;
    CRTCCHECK(v9x_vbe_crtc_build(&broken, block) == V9X_FALSE);
}

static void test_timing_rejects_what_the_summary_rejects(void)
{
    struct v9x_edid_timing timing;
    v9x_u8 corrupt[128];

    CRTCCHECK(v9x_edid_parse_timing(0, &timing) == V9X_FALSE);
    CRTCCHECK(v9x_edid_parse_timing(nav50_edid, 0) == V9X_FALSE);

    /* Bad checksum. */
    memcpy(corrupt, nav50_edid, sizeof(corrupt));
    corrupt[127] = (v9x_u8)(corrupt[127] ^ 0xFFu);
    CRTCCHECK(v9x_edid_parse_timing(corrupt, &timing) == V9X_FALSE);
    CRTCCHECK(timing.pixel_clock_hz == 0ul);

    /* Interlaced preferred timing: bit 7 of the descriptor flags, with the
     * checksum kept valid so it is the interlace bit under test. */
    memcpy(corrupt, nav50_edid, sizeof(corrupt));
    corrupt[71] = (v9x_u8)(corrupt[71] | 0x80u);
    corrupt[127] = (v9x_u8)(corrupt[127] - 0x80u);
    CRTCCHECK(v9x_edid_parse_timing(corrupt, &timing) == V9X_FALSE);
}

unsigned int v9x_run_vbe_crtc_tests(void)
{
    test_nav50_timing();
    test_nav50_crtc_block();
    test_refusals_leave_the_block_zeroed();
    test_timing_rejects_what_the_summary_rejects();
    return crtc_failures;
}
