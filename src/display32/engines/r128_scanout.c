/*
 * The Rage 128's scanout controls for V9XHAL.DLL: the start address in
 * CRTC_OFFSET, the flip status in its CRTC_GUI_TRIG_OFFSET bit, and the
 * vertical blank in CRTC_STATUS.
 *
 * Without these every flip was declined (FlipDeclined=77,448 in 3DMark
 * 2000 on A8U4I5, boot 527) and DirectDraw presented by copying the back
 * buffer on the CPU, unsynchronised: the flicker and much of the cost
 * Michael saw. The CRTC takes a new CRTC_OFFSET at the vertical blank when
 * CRTC_OFFSET_CNTL says so (RRG-G04500 p. 3-71), so a flip is written at
 * any line and is pending until the chip reports display started for it,
 * as Haswell's plane is. A CRTC_OFFSET_CNTL that is tiled, per-line or
 * locked is not this model, and the scanout then reports not live, so the
 * core declines flips as before.
 *
 * Nothing here runs unless the 16-bit side stamped ATI_RAGE128 with
 * V9X_DD_ENGINE_CAP_FLIP. The arithmetic is r128_crtc.c, host-tested.
 */
#include "ddhal_internal.h"
#include "velocity9x/r128_crtc.h"

static int v9x_r128_scanout_pending = 0;

static volatile DWORD *v9x_r128_scanout_reg(DWORD offset)
{
    return (volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

int v9x_r128_scanout_active(void)
{
    if (v9x_hal == 0 ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul ||
        v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_ATI_RAGE128 ||
        v9x_hal->engine.control_linear_base == 0ul ||
        v9x_hal->engine.mapped_aperture_bytes <
            V9X_R128_CRTC_OFFSET_CNTL + 4ul) {
        return 0;
    }
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP) != 0ul;
}

int v9x_r128_scanout_live(void)
{
    if (!v9x_r128_scanout_active()) {
        return 0;
    }
    return v9x_r128_crtc_flips_at_blank(
        *v9x_r128_scanout_reg(V9X_R128_CRTC_OFFSET_CNTL));
}

int v9x_r128_in_vblank(void)
{
    if (!v9x_r128_scanout_active()) {
        return 0;
    }
    return v9x_r128_crtc_in_vblank(
        *v9x_r128_scanout_reg(V9X_R128_CRTC_STATUS));
}

int v9x_r128_set_display_start(DWORD byte_offset)
{
    v9x_u32 value;

    if (!v9x_r128_scanout_live()) {
        return 0;
    }
    /* The frame finished before it is shown: the core's wait before a flip
     * is the ViRGE's, and Direct3D leaves the CCE running (r128_cce.c). */
    if (!v9x_r128_cce_drain(1)) {
        return 0;
    }
    if (v9x_r128_crtc_start(byte_offset, v9x_hal->fb.vram_bytes,
                            v9x_hal->fb.bits_per_pixel / 8ul, &value) !=
            V9X_STATUS_OK) {
        return 0;
    }
    *v9x_r128_scanout_reg(V9X_R128_CRTC_OFFSET) = value;
    v9x_r128_scanout_pending = 1;
    return 1;
}

int v9x_r128_flip_pending(void)
{
    if (!v9x_r128_scanout_pending || !v9x_r128_scanout_live()) {
        return 0;
    }
    return v9x_r128_crtc_flip_pending(
        *v9x_r128_scanout_reg(V9X_R128_CRTC_OFFSET));
}

/* The flip state machine has called the flip done: count whether the chip
 * agreed, as the Haswell path does. */
void v9x_r128_note_flip_done(void)
{
    if (!v9x_r128_scanout_pending) {
        return;
    }
    v9x_r128_scanout_pending = 0;
    if (v9x_r128_scanout_live() &&
        !v9x_r128_crtc_flip_pending(
            *v9x_r128_scanout_reg(V9X_R128_CRTC_OFFSET))) {
        ++v9x_hal->d3d_diagnostics.flip_taken_at_done;
    } else {
        ++v9x_hal->d3d_diagnostics.flip_not_taken_at_done;
    }
}

/* A new session or mode: the video BIOS rewrote the CRTC. */
void v9x_r128_scanout_reset(void)
{
    v9x_r128_scanout_pending = 0;
}
