/*
 * The Rage IIC's scanout controls for V9XHAL.DLL: the start address in
 * CRTC_OFF_PITCH and the vertical position in CRTC_VLINE_CRNT_VLINE.
 *
 * The VGA fallback the core used before cannot serve this chip: its
 * start-address write is S3's (CR0C/CR0D plus CR69), and the Mach64 CRTC
 * in the VBE modes scans from CRTC_OFF_PITCH. Measured on A8U4I5 (ATIRX
 * /crtc, boot 141): the offset field moves the picture cleanly, and a
 * mid-frame write showed mid-frame, so a flip is written inside the
 * vertical blank - which the flip state machine then completes when that
 * blank ends (docs\decisions\2026-10-03-rage-iic-scanout-start.md).
 *
 * Nothing here runs unless the 16-bit side stamped ATI_RAGE2 with
 * V9X_DD_ENGINE_CAP_FLIP. The arithmetic is mach64_crtc.c, host-tested.
 */
#include "ddhal_internal.h"
#include "velocity9x/ati_mach64_crtc.h"

static volatile DWORD *v9x_m64_scanout_reg(DWORD offset)
{
    return (volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

int v9x_m64_scanout_active(void)
{
    /* The Rage XL's Mach64 CRTC too, where its 16-bit side stamps the cap
     * (mobility_hw16.c, 2026-10-04). */
    if (v9x_hal == 0 ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul ||
        (v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_ATI_RAGE2 &&
         v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_ATI_MACH64) ||
        v9x_hal->engine.control_linear_base == 0ul ||
        v9x_hal->engine.mapped_aperture_bytes < 0x1000ul) {
        return 0;
    }
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP) != 0ul;
}

int v9x_m64_in_vblank(void)
{
    return v9x_m64_crtc_in_blank(
        *v9x_m64_scanout_reg(V9X_M64_CRTC_VLINE),
        *v9x_m64_scanout_reg(V9X_M64_CRTC_V_TOTAL_DISP));
}

int v9x_m64_flip_window_open(void)
{
    return v9x_m64_crtc_flip_window(
        *v9x_m64_scanout_reg(V9X_M64_CRTC_VLINE),
        *v9x_m64_scanout_reg(V9X_M64_CRTC_V_TOTAL_DISP));
}

int v9x_m64_set_display_start(DWORD byte_offset)
{
    v9x_u32 value;

    if (v9x_m64_crtc_start(*v9x_m64_scanout_reg(V9X_M64_CRTC_OFF_PITCH),
                           byte_offset, v9x_hal->fb.vram_bytes, &value) !=
            V9X_STATUS_OK) {
        return 0;
    }
    *v9x_m64_scanout_reg(V9X_M64_CRTC_OFF_PITCH) = value;
    return 1;
}
