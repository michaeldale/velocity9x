#include "velocity9x/mode.h"

#define V9X_U32_MAX ((v9x_u32)0xfffffffful)

static v9x_u16 v9x_is_power_of_two(v9x_u16 value)
{
    if (value == 0u) {
        return V9X_FALSE;
    }
    return ((value & (v9x_u16)(value - 1u)) == 0u) ? V9X_TRUE : V9X_FALSE;
}

v9x_status v9x_mode_calculate(const struct v9x_mode_request *request,
                              struct v9x_mode_layout *layout)
{
    v9x_u32 bytes_per_pixel;
    v9x_u32 raw_pitch;
    v9x_u32 alignment_mask;
    v9x_u32 pitch;
    v9x_u32 visible;

    if (request == 0 || layout == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    layout->pitch_bytes = 0ul;
    layout->visible_bytes = 0ul;
    layout->offscreen_bytes = 0ul;

    if (request->width == 0u || request->height == 0u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* 8, 16, 24 and 32 only. Every one of these divides into whole bytes per
     * pixel, which is what the pitch arithmetic below assumes; 15bpp and the
     * other sub-byte depths are refused rather than silently rounded. */
    if (request->bits_per_pixel != 8u &&
        request->bits_per_pixel != 16u &&
        request->bits_per_pixel != 24u &&
        request->bits_per_pixel != 32u) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (v9x_is_power_of_two(request->pitch_alignment) == V9X_FALSE) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    bytes_per_pixel = (v9x_u32)(request->bits_per_pixel / 8u);
    raw_pitch = (v9x_u32)request->width * bytes_per_pixel;
    alignment_mask = (v9x_u32)request->pitch_alignment - 1ul;
    /* With a 16-bit width and depths up to 32 bpp the widest raw_pitch is
     * 65535*4, so this guard still cannot trigger; it protects future wider
     * fields. The reachable overflow path is the pitch*height check below. */
    if (raw_pitch > V9X_U32_MAX - alignment_mask) {
        return V9X_STATUS_INTEGER_OVERFLOW;
    }
    pitch = (raw_pitch + alignment_mask) & ~alignment_mask;

    if (pitch != 0ul &&
        (v9x_u32)request->height > V9X_U32_MAX / pitch) {
        return V9X_STATUS_INTEGER_OVERFLOW;
    }
    visible = pitch * (v9x_u32)request->height;
    if (visible > request->framebuffer_bytes) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }

    layout->pitch_bytes = pitch;
    layout->visible_bytes = visible;
    layout->offscreen_bytes = request->framebuffer_bytes - visible;
    return V9X_STATUS_OK;
}
