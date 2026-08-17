#include "velocity9x/vbe_parse.h"

/* VbeInfoBlock offsets (VBE 2.0+). */
#define V9X_VBE_CI_SIGNATURE     0u
#define V9X_VBE_CI_VERSION       4u
#define V9X_VBE_CI_TOTAL_MEMORY 18u

/* ModeInfoBlock offsets. Proven against real BIOSes by the DOS inventory tool
 * in tools\diag\vbe_inventory_dos.c, which dumps exactly these fields. */
#define V9X_VBE_MI_ATTRIBUTES        0u
#define V9X_VBE_MI_BYTES_PER_SCAN   16u
#define V9X_VBE_MI_WIDTH            18u
#define V9X_VBE_MI_HEIGHT           20u
#define V9X_VBE_MI_BITS_PER_PIXEL   25u
#define V9X_VBE_MI_MEMORY_MODEL     27u
#define V9X_VBE_MI_PHYS_BASE        40u
#define V9X_VBE_MI_LIN_BYTES_PER_SCAN 50u

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

v9x_u16 v9x_vbe_parse_controller_info(
    const v9x_u8 *block, struct v9x_vbe_controller_summary *out)
{
    v9x_u16 version;
    v9x_u16 blocks_of_64k;

    if (out == 0) {
        return V9X_FALSE;
    }
    out->version = 0u;
    out->total_memory_bytes = 0ul;
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
    out->attributes = 0u;
    out->bytes_per_scan_line = 0u;
    out->lin_bytes_per_scan_line = 0u;
    out->width = 0u;
    out->height = 0u;
    out->bits_per_pixel = 0u;
    out->memory_model = 0u;
    out->phys_base = 0ul;
    if (block == 0) {
        return V9X_FALSE;
    }

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
