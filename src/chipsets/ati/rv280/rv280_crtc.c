/*
 * The Radeon 9250's (RV280) scanout start and flip status: see
 * include\velocity9x\rv280_crtc.h for the registers and their sources.
 */
#include "velocity9x/rv280_crtc.h"

int v9x_rv280_crtc_flips_at_blank(v9x_u32 offset_cntl)
{
    if ((offset_cntl & (V9X_RV280_CRTC_TILE_EN_RIGHT |
                        V9X_RV280_CRTC_TILE_EN |
                        V9X_RV280_CRTC_OFFSET_FLIP_CNTL |
                        V9X_RV280_CRTC_STEREO_OFFSET_EN |
                        V9X_RV280_CRTC_CNTL_LOCK)) != 0ul) {
        return 0;
    }
    /* Without it GUI_TRIG_OFFSET never reports the flip pending, and the
     * core would call every flip done at once. */
    return (offset_cntl & V9X_RV280_CRTC_GUI_TRIG_LEFT_EN) != 0ul;
}

v9x_status v9x_rv280_crtc_start(v9x_u32 fb_gpu_base, v9x_u32 fb_bytes,
                                v9x_u32 byte_offset,
                                v9x_u32 bytes_per_pixel, v9x_u32 *value)
{
    v9x_u32 address;

    if (value == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (byte_offset % V9X_RV280_CRTC_OFFSET_ALIGN != 0ul ||
        byte_offset >= fb_bytes ||
        fb_gpu_base % V9X_RV280_CRTC_OFFSET_ALIGN != 0ul ||
        byte_offset > 0xfffffffful - fb_gpu_base) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* A 24 bpp start must also fall on a whole pixel. */
    if (bytes_per_pixel == 3ul && byte_offset % 3ul != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    address = fb_gpu_base + byte_offset;
    if ((address & ~V9X_RV280_CRTC_OFFSET_MASK) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *value = address;
    return V9X_STATUS_OK;
}

int v9x_rv280_crtc_flip_pending(v9x_u32 crtc_offset)
{
    return (crtc_offset & V9X_RV280_CRTC_GUI_TRIG_OFFSET) != 0ul;
}

int v9x_rv280_crtc_in_vblank(v9x_u32 crtc_status)
{
    return (crtc_status & V9X_RV280_CRTC_VBLANK_CUR) != 0ul;
}
