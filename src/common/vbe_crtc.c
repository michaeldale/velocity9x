/*
 * Building the VBE 3.0 CRTCInfoBlock. Pure arithmetic and pure policy: see
 * include\velocity9x\vbe_crtc.h for why it is bytes at offsets rather than a
 * struct, and why none of it lives in the mini-VDD.
 */
#include "velocity9x/vbe_crtc.h"

/* The refresh field is hundredths of a hertz. */
#define V9X_VBE_CRTC_REFRESH_SCALE ((v9x_u32)100ul)

static void v9x_vbe_crtc_put16(v9x_u8 *block, v9x_u16 at, v9x_u16 value)
{
    block[at] = (v9x_u8)(value & 0x00ffu);
    block[at + 1u] = (v9x_u8)((value >> 8) & 0x00ffu);
}

static void v9x_vbe_crtc_put32(v9x_u8 *block, v9x_u16 at, v9x_u32 value)
{
    block[at] = (v9x_u8)(value & 0xfful);
    block[at + 1u] = (v9x_u8)((value >> 8) & 0xfful);
    block[at + 2u] = (v9x_u8)((value >> 16) & 0xfful);
    block[at + 3u] = (v9x_u8)((value >> 24) & 0xfful);
}

/*
 * Refresh in hundredths of a hertz, without overflowing 32 bits.
 *
 * The direct form - clock * 100 / total - overflows at 43 MHz, which is below
 * every mode worth setting. Dividing first and scaling the remainder keeps
 * both products inside 32 bits for any total a CRTC can scan.
 */
static v9x_u16 v9x_vbe_crtc_refresh(v9x_u32 pixel_clock_hz, v9x_u32 total)
{
    v9x_u32 whole;
    v9x_u32 remainder;
    v9x_u32 hundredths;

    if (total == 0ul) {
        return 0u;
    }
    whole = pixel_clock_hz / total;
    remainder = pixel_clock_hz - (whole * total);
    hundredths = (whole * V9X_VBE_CRTC_REFRESH_SCALE) +
                 ((remainder * V9X_VBE_CRTC_REFRESH_SCALE) / total);
    if (hundredths > 0xfffful) {
        return 0u;
    }
    return (v9x_u16)hundredths;
}

v9x_u16 v9x_vbe_crtc_build(const struct v9x_edid_timing *timing,
                           v9x_u8 *block)
{
    v9x_u32 h_total;
    v9x_u32 h_sync_start;
    v9x_u32 h_sync_end;
    v9x_u32 v_total;
    v9x_u32 v_sync_start;
    v9x_u32 v_sync_end;
    v9x_u16 index;
    v9x_u8 flags;

    if (block == 0) {
        return V9X_FALSE;
    }
    for (index = 0u; index < V9X_VBE_CRTC_BYTES; ++index) {
        block[index] = 0u;
    }
    if (timing == 0) {
        return V9X_FALSE;
    }

    /* A mode with no clock or no picture is not a mode. */
    if (timing->pixel_clock_hz == 0ul) {
        return V9X_FALSE;
    }
    if (timing->h_active == 0u || timing->v_active == 0u) {
        return V9X_FALSE;
    }
    /* Zero blanking leaves nowhere for the sync to live, and is also what a
     * descriptor of all zeroes decodes to. */
    if (timing->h_blank == 0u || timing->v_blank == 0u) {
        return V9X_FALSE;
    }

    h_total = (v9x_u32)timing->h_active + (v9x_u32)timing->h_blank;
    h_sync_start = (v9x_u32)timing->h_active + (v9x_u32)timing->h_sync_offset;
    h_sync_end = h_sync_start + (v9x_u32)timing->h_sync_width;
    v_total = (v9x_u32)timing->v_active + (v9x_u32)timing->v_blank;
    v_sync_start = (v9x_u32)timing->v_active + (v9x_u32)timing->v_sync_offset;
    v_sync_end = v_sync_start + (v9x_u32)timing->v_sync_width;

    /*
     * A sync that runs past the total describes a frame the CRTC cannot scan
     * out. It is worth refusing rather than clamping: the shape it takes is
     * the shape a mis-decoded upper-bit nibble produces, and clamping would
     * hand the BIOS a plausible-looking frame built from a decoding mistake.
     */
    if (h_sync_end > h_total || v_sync_end > v_total) {
        return V9X_FALSE;
    }
    /* Every field is a word to the BIOS. */
    if (h_total > 0xfffful || v_total > 0xfffful) {
        return V9X_FALSE;
    }

    flags = 0u;
    if ((timing->flags & V9X_EDID_TIMING_HSYNC_NEGATIVE) != 0u) {
        flags = (v9x_u8)(flags | V9X_VBE_CRTC_FLAG_HSYNC_NEG);
    }
    if ((timing->flags & V9X_EDID_TIMING_VSYNC_NEGATIVE) != 0u) {
        flags = (v9x_u8)(flags | V9X_VBE_CRTC_FLAG_VSYNC_NEG);
    }

    v9x_vbe_crtc_put16(block, V9X_VBE_CRTC_HTOTAL, (v9x_u16)h_total);
    v9x_vbe_crtc_put16(block, V9X_VBE_CRTC_HSYNC_START, (v9x_u16)h_sync_start);
    v9x_vbe_crtc_put16(block, V9X_VBE_CRTC_HSYNC_END, (v9x_u16)h_sync_end);
    v9x_vbe_crtc_put16(block, V9X_VBE_CRTC_VTOTAL, (v9x_u16)v_total);
    v9x_vbe_crtc_put16(block, V9X_VBE_CRTC_VSYNC_START, (v9x_u16)v_sync_start);
    v9x_vbe_crtc_put16(block, V9X_VBE_CRTC_VSYNC_END, (v9x_u16)v_sync_end);
    block[V9X_VBE_CRTC_FLAGS] = flags;
    v9x_vbe_crtc_put32(block, V9X_VBE_CRTC_PIXEL_CLOCK, timing->pixel_clock_hz);
    v9x_vbe_crtc_put16(block, V9X_VBE_CRTC_REFRESH,
                       v9x_vbe_crtc_refresh(timing->pixel_clock_hz,
                                            h_total * v_total));

    /* The reserved tail was zeroed above and stays that way. */
    return V9X_TRUE;
}
