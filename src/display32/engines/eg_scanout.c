/*
 * The HD 5450's scanout controls for V9XHAL.DLL: the start address in
 * display controller D1's GRPH_PRIMARY_SURFACE_ADDRESS, the flip status in
 * GRPH_UPDATE's SURFACE_UPDATE_PENDING, and the vertical blank in
 * CRTC_STATUS (Linux evergreen_reg.h; the write sequence is
 * evergreen_page_flip's).
 *
 * Without these every flip was declined and DirectDraw emulated it by
 * swapping the buffers' memory, so the surface the monitor scans took its
 * turn as the back buffer: the owner of M-BXMFA8 saw 3DMark 2000 corrupt
 * every second frame while screenshots, taken after the drawing, looked
 * clean (2026-10-27). With GRPH_FLIP_CONTROL clear the controller takes a
 * new address at the vertical blank, so a flip is written at any line and
 * is pending until the update-pending bit clears, as the Rage 128's is.
 *
 * D1 is the controller the VBE drove on M-BXMFA8 (its register probe read
 * D1 scanning GPU address 0, docs\decisions\2026-10-26-cedar-register-
 * probe.md). Flips are taken only while D1's CRTC and graphics plane are
 * enabled; otherwise the scanout is not live and the core declines them as
 * before. Nothing here runs unless the 16-bit side stamped the Evergreen
 * engine with V9X_DD_ENGINE_CAP_FLIP. The address arithmetic is
 * eg_dma.c's v9x_eg_scanout_address, host-tested.
 */
#include "ddhal_internal.h"
#include "velocity9x/eg_dma.h"

/* D1's registers (evergreen_reg.h, CRTC0's offset 0). */
#define V9X_EG_GRPH_ENABLE              0x6800ul
#define V9X_EG_GRPH_PRIMARY_ADDRESS     0x6810ul
#define V9X_EG_GRPH_PRIMARY_ADDRESS_HI  0x681cul
#define V9X_EG_GRPH_UPDATE              0x6844ul
#define V9X_EG_GRPH_FLIP_CONTROL        0x6848ul
#define V9X_EG_CRTC_CONTROL             0x6e70ul
#define V9X_EG_CRTC_STATUS              0x6e8cul
#define V9X_EG_GRPH_ENABLE_BIT          0x00000001ul
#define V9X_EG_GRPH_UPDATE_PENDING      0x00000004ul
#define V9X_EG_CRTC_MASTER_EN           0x00000001ul
#define V9X_EG_CRTC_V_BLANK             0x00000001ul
/* GRPH_FLIP_CONTROL clear: the update is taken at the vertical blank, not
 * the horizontal (SURFACE_UPDATE_H_RETRACE_EN is the async flip). */
#define V9X_EG_FLIP_AT_VBLANK           0x00000000ul

static int v9x_eg_scanout_pending = 0;

static volatile DWORD *v9x_eg_scanout_reg(DWORD offset)
{
    return (volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

int v9x_eg_scanout_active(void)
{
    if (v9x_hal == 0 ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul ||
        v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_ATI_EVERGREEN ||
        v9x_hal->engine.control_linear_base == 0ul ||
        v9x_hal->engine.mapped_aperture_bytes < V9X_EG_CRTC_STATUS + 4ul) {
        return 0;
    }
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP) != 0ul;
}

int v9x_eg_scanout_live(void)
{
    if (!v9x_eg_scanout_active()) {
        return 0;
    }
    return (*v9x_eg_scanout_reg(V9X_EG_CRTC_CONTROL) &
            V9X_EG_CRTC_MASTER_EN) != 0ul &&
           (*v9x_eg_scanout_reg(V9X_EG_GRPH_ENABLE) &
            V9X_EG_GRPH_ENABLE_BIT) != 0ul;
}

int v9x_eg_in_vblank(void)
{
    if (!v9x_eg_scanout_active()) {
        return 0;
    }
    return (*v9x_eg_scanout_reg(V9X_EG_CRTC_STATUS) &
            V9X_EG_CRTC_V_BLANK) != 0ul;
}

int v9x_eg_set_display_start(DWORD byte_offset)
{
    DWORD heap_gpu;
    v9x_u32 address;

    if (!v9x_eg_scanout_live() || !v9x_eg_cp_addresses(&heap_gpu) ||
        v9x_eg_scanout_address(heap_gpu, byte_offset,
                               v9x_hal->fb.vram_bytes, &address) !=
            V9X_STATUS_OK) {
        return 0;
    }
    /* The frame is finished before it is shown: the CP's drawing and the
     * DMA engine's last fill or copy. */
    if (!v9x_eg_cp_drain(1) || !v9x_eg_dma_settle()) {
        return 0;
    }
    *v9x_eg_scanout_reg(V9X_EG_GRPH_FLIP_CONTROL) = V9X_EG_FLIP_AT_VBLANK;
    *v9x_eg_scanout_reg(V9X_EG_GRPH_PRIMARY_ADDRESS_HI) = 0ul;
    *v9x_eg_scanout_reg(V9X_EG_GRPH_PRIMARY_ADDRESS) = address;
    /* Posts the write, as Linux does. */
    (void)*v9x_eg_scanout_reg(V9X_EG_GRPH_PRIMARY_ADDRESS);
    v9x_eg_scanout_pending = 1;
    return 1;
}

/* Pending while the controller has the address but has not yet taken it
 * at a vertical blank. */
int v9x_eg_flip_pending(void)
{
    if (!v9x_eg_scanout_pending || !v9x_eg_scanout_live()) {
        return 0;
    }
    return (*v9x_eg_scanout_reg(V9X_EG_GRPH_UPDATE) &
            V9X_EG_GRPH_UPDATE_PENDING) != 0ul;
}

/* The flip state machine has called the flip done: count whether the chip
 * agreed, as the Rage 128 and Haswell paths do. */
void v9x_eg_note_flip_done(void)
{
    if (!v9x_eg_scanout_pending) {
        return;
    }
    v9x_eg_scanout_pending = 0;
    if (v9x_eg_scanout_live() &&
        (*v9x_eg_scanout_reg(V9X_EG_GRPH_UPDATE) &
         V9X_EG_GRPH_UPDATE_PENDING) == 0ul) {
        ++v9x_hal->d3d_diagnostics.flip_taken_at_done;
    } else {
        ++v9x_hal->d3d_diagnostics.flip_not_taken_at_done;
    }
}

/* A new session or mode: the video BIOS rewrote the controller. */
void v9x_eg_scanout_reset(void)
{
    v9x_eg_scanout_pending = 0;
}
