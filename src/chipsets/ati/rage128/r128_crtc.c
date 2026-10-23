/*
 * The Rage 128's scanout start and flip status: see
 * include\velocity9x\r128_crtc.h for the registers and their source.
 */
#include "velocity9x/r128_crtc.h"

int v9x_r128_crtc_flips_at_blank(v9x_u32 offset_cntl)
{
    return (offset_cntl & (V9X_R128_CRTC_TILE_EN |
                           V9X_R128_CRTC_OFFSET_FLIP_CNTL |
                           V9X_R128_CRTC_OFFSET_LOCK)) == 0ul;
}

v9x_status v9x_r128_crtc_start(v9x_u32 byte_offset, v9x_u32 vram_bytes,
                               v9x_u32 bytes_per_pixel, v9x_u32 *value)
{
    if (value == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (byte_offset % V9X_R128_CRTC_OFFSET_ALIGN != 0ul ||
        byte_offset >= vram_bytes ||
        (byte_offset & ~V9X_R128_CRTC_OFFSET_MASK) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* A 24 bpp start must also fall on a whole pixel. */
    if (bytes_per_pixel == 3ul && byte_offset % 3ul != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *value = byte_offset;
    return V9X_STATUS_OK;
}

int v9x_r128_crtc_flip_pending(v9x_u32 crtc_offset)
{
    return (crtc_offset & V9X_R128_CRTC_GUI_TRIG_OFFSET) != 0ul;
}

int v9x_r128_crtc_in_vblank(v9x_u32 crtc_status)
{
    return (crtc_status & V9X_R128_CRTC_VBLANK_CUR) != 0ul;
}
