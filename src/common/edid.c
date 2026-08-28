#include "velocity9x/edid.h"

/* The first detailed-timing descriptor. EDID 1.x puts four 18-byte
 * descriptors at 54, 72, 90 and 108; only the first is consulted, because
 * that is the one the preferred-timing feature bit promotes and the only one
 * this driver acts on. */
#define V9X_EDID_DTD_OFFSET 54u

/* EDID states the pixel clock in units of 10 kHz. */
#define V9X_EDID_CLOCK_UNIT_HZ ((v9x_u32)10000ul)

/* Descriptor byte 17: interlace, and the sync-type field that decides whether
 * the polarity bits below it mean anything. */
#define V9X_EDID_DTD_INTERLACED     0x80u
#define V9X_EDID_DTD_SYNC_MASK      0x18u
#define V9X_EDID_DTD_SYNC_SEPARATE  0x18u
#define V9X_EDID_DTD_VSYNC_POSITIVE 0x04u
#define V9X_EDID_DTD_HSYNC_POSITIVE 0x02u

/*
 * Validate the block and return its first detailed timing, or 0.
 *
 * Both public parses accept exactly the same blocks, so the rules live here
 * once. A second copy would be a second thing to keep in step, and the two
 * would drift on the day one of them learned about a new BIOS defect.
 */
static const v9x_u8 *v9x_edid_timing_descriptor(const v9x_u8 *block)
{
    static const v9x_u8 header[8] = {
        0x00u, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x00u
    };
    const v9x_u8 *dtd;
    v9x_u16 index;
    v9x_u8 sum;

    if (block == 0) {
        return 0;
    }

    for (index = 0u; index < 8u; ++index) {
        if (block[index] != header[index]) {
            return 0;
        }
    }

    sum = 0u;
    for (index = 0u; index < V9X_EDID_BLOCK_BYTES; ++index) {
        sum = (v9x_u8)(sum + block[index]);
    }
    if (sum != 0u) {
        return 0;
    }

    if (block[18] != 1u) {
        return 0;
    }

    dtd = block + V9X_EDID_DTD_OFFSET;
    /* Pixel clock zero marks a display descriptor, not a timing: a block
     * whose first slot is a name or a range limit states no preference this
     * driver can read. */
    if (dtd[0] == 0u && dtd[1] == 0u) {
        return 0;
    }
    /* Interlaced: bit 7 of the flags byte. */
    if ((dtd[17] & V9X_EDID_DTD_INTERLACED) != 0u) {
        return 0;
    }
    return dtd;
}

v9x_u16 v9x_edid_parse(const v9x_u8 *block, struct v9x_edid_summary *out)
{
    const v9x_u8 *dtd;
    v9x_u16 width;
    v9x_u16 height;

    if (out == 0) {
        return V9X_FALSE;
    }
    out->version = 0u;
    out->preferred_width = 0u;
    out->preferred_height = 0u;
    out->extension_count = 0u;

    dtd = v9x_edid_timing_descriptor(block);
    if (dtd == 0) {
        return V9X_FALSE;
    }

    width = (v9x_u16)(dtd[2] | ((v9x_u16)(dtd[4] & 0xf0u) << 4));
    height = (v9x_u16)(dtd[5] | ((v9x_u16)(dtd[7] & 0xf0u) << 4));
    if (width == 0u || height == 0u) {
        return V9X_FALSE;
    }

    out->version = (v9x_u16)(((v9x_u16)block[18] << 8) | block[19]);
    out->preferred_width = width;
    out->preferred_height = height;
    out->extension_count = block[126];
    return V9X_TRUE;
}

v9x_u16 v9x_edid_parse_timing(const v9x_u8 *block,
                              struct v9x_edid_timing *out)
{
    const v9x_u8 *dtd;

    if (out == 0) {
        return V9X_FALSE;
    }
    out->pixel_clock_hz = 0ul;
    out->h_active = 0u;
    out->h_blank = 0u;
    out->h_sync_offset = 0u;
    out->h_sync_width = 0u;
    out->v_active = 0u;
    out->v_blank = 0u;
    out->v_sync_offset = 0u;
    out->v_sync_width = 0u;
    out->flags = 0u;

    dtd = v9x_edid_timing_descriptor(block);
    if (dtd == 0) {
        return V9X_FALSE;
    }

    /*
     * The upper bits of each figure are packed into shared nibbles, and the
     * four sync figures share a single byte two bits apiece. Byte 11 is the
     * one that repays reading twice: 7:6 horizontal offset, 5:4 horizontal
     * width, 3:2 vertical offset, 1:0 vertical width.
     */
    out->h_active = (v9x_u16)(dtd[2] | ((v9x_u16)(dtd[4] & 0xf0u) << 4));
    out->h_blank = (v9x_u16)(dtd[3] | ((v9x_u16)(dtd[4] & 0x0fu) << 8));
    out->v_active = (v9x_u16)(dtd[5] | ((v9x_u16)(dtd[7] & 0xf0u) << 4));
    out->v_blank = (v9x_u16)(dtd[6] | ((v9x_u16)(dtd[7] & 0x0fu) << 8));

    out->h_sync_offset =
        (v9x_u16)(dtd[8] | ((v9x_u16)(dtd[11] & 0xc0u) << 2));
    out->h_sync_width =
        (v9x_u16)(dtd[9] | ((v9x_u16)(dtd[11] & 0x30u) << 4));
    out->v_sync_offset =
        (v9x_u16)(((dtd[10] & 0xf0u) >> 4) | ((v9x_u16)(dtd[11] & 0x0cu) << 2));
    out->v_sync_width =
        (v9x_u16)((dtd[10] & 0x0fu) | ((v9x_u16)(dtd[11] & 0x03u) << 4));

    if (out->h_active == 0u || out->v_active == 0u) {
        out->h_active = 0u;
        out->v_active = 0u;
        return V9X_FALSE;
    }

    out->pixel_clock_hz =
        ((v9x_u32)dtd[0] | ((v9x_u32)dtd[1] << 8)) * V9X_EDID_CLOCK_UNIT_HZ;

    /*
     * Polarity is only a polarity when the sync is digital separate. On an
     * analog composite descriptor these same two bits mean serration and
     * sync-on-green, so a consumer that read them as polarity would program
     * the CRTC from a field about something else entirely.
     */
    if ((dtd[17] & V9X_EDID_DTD_SYNC_MASK) == V9X_EDID_DTD_SYNC_SEPARATE) {
        out->flags |= V9X_EDID_TIMING_DIGITAL_SEPARATE;
        if ((dtd[17] & V9X_EDID_DTD_HSYNC_POSITIVE) == 0u) {
            out->flags |= V9X_EDID_TIMING_HSYNC_NEGATIVE;
        }
        if ((dtd[17] & V9X_EDID_DTD_VSYNC_POSITIVE) == 0u) {
            out->flags |= V9X_EDID_TIMING_VSYNC_NEGATIVE;
        }
    }
    return V9X_TRUE;
}
