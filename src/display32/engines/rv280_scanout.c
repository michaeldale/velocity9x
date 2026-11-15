/*
 * The Radeon 9250's (RV280) scanout controls for V9XHAL.DLL: the start
 * address in the legacy CRTC's CRTC_OFFSET, the flip status in its
 * GUI_TRIG_OFFSET bit, and the vertical blank in CRTC_STATUS.
 *
 * Without these a flip went nowhere: V9XDDP on M-HNBJNV read FlipHr=0 with
 * Flip20Ms=0 on the 2D-only row (docs\decisions\2026-11-15-rv280-2d-write-
 * probe.md). The register probe read the VBE desktop at CRTC_OFFSET 0 with
 * the frame buffer at GPU address 0 (docs\decisions\2026-11-15-rv280-
 * register-probe.md), so a flip is the frame buffer's GPU base plus the
 * surface's offset (eng_rv280.c, v9x_rv280_flip_address).
 *
 * The write sequence is Linux r100_page_flip's: CRTC_OFFSET with
 * OFFSET_LOCK set, wait for GUI_TRIG_OFFSET to rise, then the same address
 * unlocked, so the CRTC takes it at the next vertical blank. The flip is
 * pending until GUI_TRIG_OFFSET clears (r100_page_flip_pending). The Rage
 * 128's path (r128_scanout.c) is the model on the same register offsets;
 * there is no CCE here, so the engine is idle before the write.
 *
 * Flips are taken only while CRTC_OFFSET_CNTL describes that model
 * (rv280_crtc.c); otherwise the scanout reports not live and the core
 * declines them. Nothing here runs unless the 16-bit side stamped
 * ATI_RV280 with V9X_DD_ENGINE_CAP_FLIP.
 */
#include "ddhal_internal.h"
#include "velocity9x/rv280_crtc.h"

/* r100_page_flip waits up to usec_timeout (100 ms) for the bit to rise
 * under the lock; this bound only has to tell slow from never. */
#define V9X_RV280_PENDING_SPINS 0x00010000ul

static int v9x_rv280_scanout_pending = 0;

static volatile DWORD *v9x_rv280_scanout_reg(DWORD offset)
{
    return (volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

int v9x_rv280_scanout_active(void)
{
    if (v9x_hal == 0 ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul ||
        v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_ATI_RV280 ||
        v9x_hal->engine.control_linear_base == 0ul ||
        v9x_hal->engine.mapped_aperture_bytes <
            V9X_RV280_CRTC_OFFSET_CNTL + 4ul) {
        return 0;
    }
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP) != 0ul;
}

int v9x_rv280_scanout_live(void)
{
    if (!v9x_rv280_scanout_active()) {
        return 0;
    }
    return v9x_rv280_crtc_flips_at_blank(
        *v9x_rv280_scanout_reg(V9X_RV280_CRTC_OFFSET_CNTL));
}

int v9x_rv280_in_vblank(void)
{
    if (!v9x_rv280_scanout_active()) {
        return 0;
    }
    return v9x_rv280_crtc_in_vblank(
        *v9x_rv280_scanout_reg(V9X_RV280_CRTC_STATUS));
}

int v9x_rv280_set_display_start(DWORD byte_offset)
{
    DWORD value;
    DWORD spins;

    if (!v9x_rv280_scanout_live()) {
        return 0;
    }
    if (!v9x_rv280_flip_address(byte_offset, &value)) {
        return 0;
    }
    *v9x_rv280_scanout_reg(V9X_RV280_CRTC_OFFSET) =
        value | V9X_RV280_CRTC_OFFSET_LOCK;
    for (spins = 0ul; spins < V9X_RV280_PENDING_SPINS; ++spins) {
        if (v9x_rv280_crtc_flip_pending(
                *v9x_rv280_scanout_reg(V9X_RV280_CRTC_OFFSET))) {
            break;
        }
    }
    /* Unlocked, the CRTC takes the address at the next blank. Linux
     * unlocks whether or not the bit rose, and so does this. */
    *v9x_rv280_scanout_reg(V9X_RV280_CRTC_OFFSET) = value;
    v9x_rv280_scanout_pending = 1;
    return 1;
}

/* Pending while the CRTC has the offset but has not started displaying it
 * (r100_page_flip_pending). */
int v9x_rv280_flip_pending(void)
{
    if (!v9x_rv280_scanout_pending || !v9x_rv280_scanout_live()) {
        return 0;
    }
    return v9x_rv280_crtc_flip_pending(
        *v9x_rv280_scanout_reg(V9X_RV280_CRTC_OFFSET));
}

/* The flip state machine has called the flip done: count whether the chip
 * agreed, as the Rage 128 and RS690 paths do. */
void v9x_rv280_note_flip_done(void)
{
    if (!v9x_rv280_scanout_pending) {
        return;
    }
    v9x_rv280_scanout_pending = 0;
    if (v9x_rv280_scanout_live() &&
        !v9x_rv280_crtc_flip_pending(
            *v9x_rv280_scanout_reg(V9X_RV280_CRTC_OFFSET))) {
        ++v9x_hal->d3d_diagnostics.flip_taken_at_done;
    } else {
        ++v9x_hal->d3d_diagnostics.flip_not_taken_at_done;
    }
}

/* A new session or mode: the video BIOS rewrote the CRTC. */
void v9x_rv280_scanout_reset(void)
{
    v9x_rv280_scanout_pending = 0;
}
