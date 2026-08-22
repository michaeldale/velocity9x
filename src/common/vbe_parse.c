#include "velocity9x/vbe_parse.h"

/* VbeInfoBlock offsets (VBE 2.0+). */
#define V9X_VBE_CI_SIGNATURE     0u
#define V9X_VBE_CI_VERSION       4u
#define V9X_VBE_CI_CAPABILITIES 10u
#define V9X_VBE_CI_TOTAL_MEMORY 18u
/* VBE 2.0 and later only, which is the floor this driver requires anyway. */
#define V9X_VBE_CI_OEM_SOFTWARE_REV 20u

/* ModeInfoBlock offsets. Proven against real BIOSes by the DOS inventory tool
 * in tools\diag\vbe_inventory_dos.c, which dumps exactly these fields. */
#define V9X_VBE_MI_ATTRIBUTES        0u
#define V9X_VBE_MI_BYTES_PER_SCAN   16u
#define V9X_VBE_MI_WIDTH            18u
#define V9X_VBE_MI_HEIGHT           20u
#define V9X_VBE_MI_BITS_PER_PIXEL   25u
#define V9X_VBE_MI_MEMORY_MODEL     27u
#define V9X_VBE_MI_RED_MASK_SIZE    31u
#define V9X_VBE_MI_RED_FIELD_POS    32u
#define V9X_VBE_MI_GREEN_MASK_SIZE  33u
#define V9X_VBE_MI_GREEN_FIELD_POS  34u
#define V9X_VBE_MI_BLUE_MASK_SIZE   35u
#define V9X_VBE_MI_BLUE_FIELD_POS   36u
#define V9X_VBE_MI_RSVD_MASK_SIZE   37u
#define V9X_VBE_MI_RSVD_FIELD_POS   38u
#define V9X_VBE_MI_PHYS_BASE        40u
#define V9X_VBE_MI_LIN_BYTES_PER_SCAN 50u

/* VBE 3.0 added a second copy of the channel layout, for the linear
 * framebuffer specifically. A VBE 2 BIOS writes none of these bytes, which is
 * why the block has to be zeroed before the call: zero here has to mean "not
 * reported" rather than "whatever the last mode left behind". */
#define V9X_VBE_MI_LIN_RED_MASK_SIZE   54u
#define V9X_VBE_MI_LIN_RED_FIELD_POS   55u
#define V9X_VBE_MI_LIN_GREEN_MASK_SIZE 56u
#define V9X_VBE_MI_LIN_GREEN_FIELD_POS 57u
#define V9X_VBE_MI_LIN_BLUE_MASK_SIZE  58u
#define V9X_VBE_MI_LIN_BLUE_FIELD_POS  59u
#define V9X_VBE_MI_LIN_RSVD_MASK_SIZE  60u
#define V9X_VBE_MI_LIN_RSVD_FIELD_POS  61u

/* ModeAttributes bits this driver cares about. */
#define V9X_VBE_ATTR_SUPPORTED ((v9x_u16)0x0001u)
#define V9X_VBE_ATTR_LINEAR    ((v9x_u16)0x0080u)

/* MemoryModel values a DIB-engine surface can live in. */
#define V9X_VBE_MODEL_PACKED_PIXEL ((v9x_u16)4u)
#define V9X_VBE_MODEL_DIRECT_COLOR ((v9x_u16)6u)

/* An aperture inside the first megabyte is real-mode memory, not a card. */
#define V9X_VBE_MIN_PHYS_BASE ((v9x_u32)0x00100000ul)

#define V9X_VBE_MIN_VERSION ((v9x_u16)0x0200u)

static v9x_u16 v9x_vbe_read_u16(const v9x_u8 *data)
{
    return (v9x_u16)(((v9x_u16)data[1] << 8) | (v9x_u16)data[0]);
}

static v9x_u32 v9x_vbe_read_u32(const v9x_u8 *data)
{
    return (v9x_u32)data[0] |
           ((v9x_u32)data[1] << 8) |
           ((v9x_u32)data[2] << 16) |
           ((v9x_u32)data[3] << 24);
}

void v9x_vbe_mode_summary_clear(struct v9x_vbe_mode_summary *out)
{
    if (out == 0) {
        return;
    }
    out->attributes = 0u;
    out->bytes_per_scan_line = 0u;
    out->lin_bytes_per_scan_line = 0u;
    out->width = 0u;
    out->height = 0u;
    out->bits_per_pixel = 0u;
    out->significant_depth = 0u;
    out->mask_flags = 0u;
    out->memory_model = 0u;
    out->phys_base = 0ul;
    out->red_mask_size = 0u;
    out->red_field_position = 0u;
    out->green_mask_size = 0u;
    out->green_field_position = 0u;
    out->blue_mask_size = 0u;
    out->blue_field_position = 0u;
    out->rsvd_mask_size = 0u;
    out->rsvd_field_position = 0u;
}

v9x_u16 v9x_vbe_summary_significant_depth(
    const struct v9x_vbe_mode_summary *summary)
{
    v9x_u16 total;

    if (summary == 0 || summary->bits_per_pixel == 0u) {
        return 0u;
    }
    /* An 8-bpp mode is palettized: the byte is an index into a LUT rather than
     * three channels, and all eight bits of it are significant. */
    if (summary->bits_per_pixel == 8u) {
        return 8u;
    }
    if (summary->red_mask_size == 0u && summary->green_mask_size == 0u &&
        summary->blue_mask_size == 0u) {
        /* The one assumption this file makes, and it is made in exactly one
         * other place: v9x_vbe_masks_to_bits reads all-zero 16-bpp channels as
         * 5:6:5. The two have to agree or a mode would be admitted with 16
         * significant bits and drawn with some other number. */
        return summary->bits_per_pixel == 16u ? 16u : 0u;
    }
    total = (v9x_u16)(summary->red_mask_size + summary->green_mask_size +
                      summary->blue_mask_size);
    /* Nothing may claim more colour bits than the pixel has room for. */
    if (total > summary->bits_per_pixel) {
        return 0u;
    }
    return total;
}

void v9x_vbe_controller_summary_clear(
    struct v9x_vbe_controller_summary *out)
{
    if (out == 0) {
        return;
    }
    out->version = 0u;
    out->total_memory_bytes = 0ul;
    out->capabilities = 0ul;
    out->oem_software_rev = 0u;
}

v9x_u16 v9x_vbe_parse_controller_info(
    const v9x_u8 *block, struct v9x_vbe_controller_summary *out)
{
    v9x_u16 version;
    v9x_u16 blocks_of_64k;

    if (out == 0) {
        return V9X_FALSE;
    }
    v9x_vbe_controller_summary_clear(out);
    if (block == 0) {
        return V9X_FALSE;
    }

    if (block[V9X_VBE_CI_SIGNATURE + 0u] != (v9x_u8)'V' ||
        block[V9X_VBE_CI_SIGNATURE + 1u] != (v9x_u8)'E' ||
        block[V9X_VBE_CI_SIGNATURE + 2u] != (v9x_u8)'S' ||
        block[V9X_VBE_CI_SIGNATURE + 3u] != (v9x_u8)'A') {
        return V9X_FALSE;
    }

    version = v9x_vbe_read_u16(block + V9X_VBE_CI_VERSION);
    if (version < V9X_VBE_MIN_VERSION) {
        return V9X_FALSE;
    }

    /* VBE 1.x had no linear framebuffer, so a 1.x controller cannot serve
     * tier-0 even if every other field looks sane. */
    blocks_of_64k = v9x_vbe_read_u16(block + V9X_VBE_CI_TOTAL_MEMORY);
    if (blocks_of_64k == 0u) {
        return V9X_FALSE;
    }

    out->version = version;
    out->total_memory_bytes = (v9x_u32)blocks_of_64k * 65536ul;
    /*
     * Neither of these can fail the block: a BIOS reporting zero capabilities
     * and no revision is describing itself sparsely, not incredibly, and
     * refusing the controller over a diagnostic field would cost the driver its
     * aperture for nothing.
     */
    out->capabilities = v9x_vbe_read_u32(block + V9X_VBE_CI_CAPABILITIES);
    out->oem_software_rev =
        v9x_vbe_read_u16(block + V9X_VBE_CI_OEM_SOFTWARE_REV);
    return V9X_TRUE;
}

v9x_u16 v9x_vbe_mode_summary_is_drivable(
    const struct v9x_vbe_mode_summary *summary)
{
    if (summary == 0) {
        return V9X_FALSE;
    }
    if ((summary->attributes & V9X_VBE_ATTR_SUPPORTED) == 0u) {
        return V9X_FALSE;
    }
    if ((summary->attributes & V9X_VBE_ATTR_LINEAR) == 0u) {
        return V9X_FALSE;
    }
    if (summary->memory_model != V9X_VBE_MODEL_PACKED_PIXEL &&
        summary->memory_model != V9X_VBE_MODEL_DIRECT_COLOR) {
        return V9X_FALSE;
    }
    if (summary->phys_base < V9X_VBE_MIN_PHYS_BASE) {
        return V9X_FALSE;
    }
    if (summary->width == 0u || summary->height == 0u ||
        summary->bits_per_pixel == 0u || summary->bytes_per_scan_line == 0u) {
        return V9X_FALSE;
    }
    return V9X_TRUE;
}

v9x_u16 v9x_vbe_parse_mode_info(
    const v9x_u8 *block, struct v9x_vbe_mode_summary *out)
{
    struct v9x_vbe_mode_summary candidate;

    if (out == 0) {
        return V9X_FALSE;
    }
    v9x_vbe_mode_summary_clear(out);
    if (block == 0) {
        return V9X_FALSE;
    }

    v9x_vbe_mode_summary_clear(&candidate);
    candidate.attributes = v9x_vbe_read_u16(block + V9X_VBE_MI_ATTRIBUTES);
    candidate.bytes_per_scan_line =
        v9x_vbe_read_u16(block + V9X_VBE_MI_BYTES_PER_SCAN);
    candidate.lin_bytes_per_scan_line =
        v9x_vbe_read_u16(block + V9X_VBE_MI_LIN_BYTES_PER_SCAN);
    candidate.width = v9x_vbe_read_u16(block + V9X_VBE_MI_WIDTH);
    candidate.height = v9x_vbe_read_u16(block + V9X_VBE_MI_HEIGHT);
    candidate.bits_per_pixel = (v9x_u16)block[V9X_VBE_MI_BITS_PER_PIXEL];
    candidate.memory_model = (v9x_u16)block[V9X_VBE_MI_MEMORY_MODEL];
    candidate.phys_base = v9x_vbe_read_u32(block + V9X_VBE_MI_PHYS_BASE);
    candidate.red_mask_size = (v9x_u16)block[V9X_VBE_MI_RED_MASK_SIZE];
    candidate.red_field_position = (v9x_u16)block[V9X_VBE_MI_RED_FIELD_POS];
    candidate.green_mask_size = (v9x_u16)block[V9X_VBE_MI_GREEN_MASK_SIZE];
    candidate.green_field_position = (v9x_u16)block[V9X_VBE_MI_GREEN_FIELD_POS];
    candidate.blue_mask_size = (v9x_u16)block[V9X_VBE_MI_BLUE_MASK_SIZE];
    candidate.blue_field_position = (v9x_u16)block[V9X_VBE_MI_BLUE_FIELD_POS];
    candidate.rsvd_mask_size = (v9x_u16)block[V9X_VBE_MI_RSVD_MASK_SIZE];
    candidate.rsvd_field_position = (v9x_u16)block[V9X_VBE_MI_RSVD_FIELD_POS];

    /*
     * The VBE 3 linear channel layout replaces the legacy one when the BIOS
     * reports it. "Reports it" means any of the eight bytes is non-zero: a
     * BIOS that fills in the linear set at all fills in the whole set, and a
     * VBE 2 BIOS writes none of it into a block the caller zeroed.
     *
     * The reserved pair is copied even though DirectDraw publishes
     * dwAlphaBitMask = 0. It is what separates XRGB 8:8:8:8 from packed
     * 8:8:8 when both report 32 bits per pixel, so dropping it would lose the
     * distinction the significant depth exists to make.
     */
    if (block[V9X_VBE_MI_LIN_RED_MASK_SIZE] != 0u ||
        block[V9X_VBE_MI_LIN_RED_FIELD_POS] != 0u ||
        block[V9X_VBE_MI_LIN_GREEN_MASK_SIZE] != 0u ||
        block[V9X_VBE_MI_LIN_GREEN_FIELD_POS] != 0u ||
        block[V9X_VBE_MI_LIN_BLUE_MASK_SIZE] != 0u ||
        block[V9X_VBE_MI_LIN_BLUE_FIELD_POS] != 0u ||
        block[V9X_VBE_MI_LIN_RSVD_MASK_SIZE] != 0u ||
        block[V9X_VBE_MI_LIN_RSVD_FIELD_POS] != 0u) {
        candidate.red_mask_size =
            (v9x_u16)block[V9X_VBE_MI_LIN_RED_MASK_SIZE];
        candidate.red_field_position =
            (v9x_u16)block[V9X_VBE_MI_LIN_RED_FIELD_POS];
        candidate.green_mask_size =
            (v9x_u16)block[V9X_VBE_MI_LIN_GREEN_MASK_SIZE];
        candidate.green_field_position =
            (v9x_u16)block[V9X_VBE_MI_LIN_GREEN_FIELD_POS];
        candidate.blue_mask_size =
            (v9x_u16)block[V9X_VBE_MI_LIN_BLUE_MASK_SIZE];
        candidate.blue_field_position =
            (v9x_u16)block[V9X_VBE_MI_LIN_BLUE_FIELD_POS];
        candidate.rsvd_mask_size =
            (v9x_u16)block[V9X_VBE_MI_LIN_RSVD_MASK_SIZE];
        candidate.rsvd_field_position =
            (v9x_u16)block[V9X_VBE_MI_LIN_RSVD_FIELD_POS];
        candidate.mask_flags = V9X_VBE_RF_MASKS_LINEAR;
    } else if (candidate.red_mask_size != 0u ||
               candidate.green_mask_size != 0u ||
               candidate.blue_mask_size != 0u ||
               candidate.rsvd_mask_size != 0u) {
        candidate.mask_flags = V9X_VBE_RF_MASKS_LEGACY;
    }
    /* A palettized mode reports no channels at all and keeps mask_flags zero;
     * so does the 16-bpp all-zero case, where 5:6:5 is this driver's
     * convention rather than something the BIOS said. */
    if (candidate.lin_bytes_per_scan_line != 0u) {
        candidate.mask_flags =
            (v9x_u16)(candidate.mask_flags | V9X_VBE_RF_LIN_STRIDE);
    }
    candidate.significant_depth =
        v9x_vbe_summary_significant_depth(&candidate);

    /* One rule, applied here and by the mini-VDD path, so the two cannot
     * drift into accepting different things. */
    if (v9x_vbe_mode_summary_is_drivable(&candidate) == V9X_FALSE) {
        return V9X_FALSE;
    }

    *out = candidate;
    return V9X_TRUE;
}

v9x_u16 v9x_vbe_mode_matches(const struct v9x_vbe_mode_summary *summary,
                             v9x_u16 width, v9x_u16 height,
                             v9x_u16 bits_per_pixel, v9x_u16 pitch)
{
    v9x_u16 stride;

    if (summary == 0) {
        return V9X_FALSE;
    }
    if (summary->width != width || summary->height != height ||
        summary->bits_per_pixel != bits_per_pixel) {
        return V9X_FALSE;
    }

    stride = summary->lin_bytes_per_scan_line != 0u
                 ? summary->lin_bytes_per_scan_line
                 : summary->bytes_per_scan_line;
    return stride == pitch ? V9X_TRUE : V9X_FALSE;
}

/* One channel: size bits starting at position, or 0 when it does not fit. */
static v9x_u32 v9x_vbe_channel_mask(v9x_u16 size, v9x_u16 position,
                                    v9x_u16 bits_per_pixel)
{
    v9x_u32 mask;

    if (size == 0u || size > 32u || position > 31u ||
        (v9x_u16)(position + size) > bits_per_pixel) {
        return 0ul;
    }
    /* Build by shifting in ones rather than (1 << size) - 1: size can be 32,
     * where that shift is undefined. */
    mask = 0ul;
    while (size-- != 0u) {
        mask = (mask << 1) | 1ul;
    }
    return mask << position;
}

v9x_u16 v9x_vbe_masks_to_bits(const struct v9x_vbe_mode_summary *summary,
                              v9x_u32 *red, v9x_u32 *green, v9x_u32 *blue)
{
    v9x_u32 r;
    v9x_u32 g;
    v9x_u32 b;

    if (red == 0 || green == 0 || blue == 0) {
        return V9X_FALSE;
    }
    *red = 0ul;
    *green = 0ul;
    *blue = 0ul;
    if (summary == 0 || summary->bits_per_pixel == 8u) {
        return V9X_FALSE;
    }

    if (summary->red_mask_size == 0u && summary->green_mask_size == 0u &&
        summary->blue_mask_size == 0u) {
        /* See the header: only 16 bpp has a single layout worth assuming. */
        if (summary->bits_per_pixel != 16u) {
            return V9X_FALSE;
        }
        *red = 0x0000f800ul;
        *green = 0x000007e0ul;
        *blue = 0x0000001ful;
        return V9X_TRUE;
    }

    r = v9x_vbe_channel_mask(summary->red_mask_size,
                             summary->red_field_position,
                             summary->bits_per_pixel);
    g = v9x_vbe_channel_mask(summary->green_mask_size,
                             summary->green_field_position,
                             summary->bits_per_pixel);
    b = v9x_vbe_channel_mask(summary->blue_mask_size,
                             summary->blue_field_position,
                             summary->bits_per_pixel);
    if (r == 0ul || g == 0ul || b == 0ul) {
        return V9X_FALSE;
    }
    /* Channels that share a bit describe no layout any consumer can use. */
    if ((r & g) != 0ul || (r & b) != 0ul || (g & b) != 0ul) {
        return V9X_FALSE;
    }

    *red = r;
    *green = g;
    *blue = b;
    return V9X_TRUE;
}
