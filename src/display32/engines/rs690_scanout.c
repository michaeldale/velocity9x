/*
 * The Radeon X1200 IGP's (RS690) scanout controls for V9XHAL.DLL: the start
 * address in the AVIVO display controller D1's D1GRPH_PRIMARY_SURFACE_ADDRESS,
 * the flip status in D1GRPH_UPDATE's SURFACE_UPDATE_PENDING, and the
 * vertical blank in D1CRTC_STATUS. Offsets and bits are Linux r500_reg.h;
 * the write sequence is rs600_page_flip (Linux rs600.c), which radeon_asic.c
 * gives the RS690 too.
 *
 * Without these a flip went to the VGA CRTC start address, which the AVIVO
 * controller does not scan from: V9XDDP on M-GB8KRE read FlipHr=0 with
 * FlipPixelOk=0 (2026-11-05, docs\issues\2026-11-04-rs690-ati-build-black-
 * screen-at-boot.md). The register probe read the VBE desktop on D1 at the
 * frame buffer's GPU base, 78000000h (docs\decisions\2026-11-04-rs690-
 * register-probe.md), so a flip is that base plus the surface's offset.
 *
 * rs600_page_flip holds D1GRPH_UPDATE_LOCK while it writes both surface
 * addresses, waits for SURFACE_UPDATE_PENDING to rise, then drops the lock
 * so the controller takes the new address at the next vertical blank (with
 * D1GRPH_FLIP_CONTROL clear). The flip is then pending until that bit
 * clears, as on Evergreen (eg_scanout.c). Flips are taken only while D1's
 * CRTC and graphics plane are enabled; otherwise the core declines them.
 * Nothing here runs unless the 16-bit side stamped the RS690 engine with
 * V9X_DD_ENGINE_CAP_FLIP. The address arithmetic is rs690_2d.c's
 * v9x_rs690_scanout_address, host-tested.
 */
#include "ddhal_internal.h"

/* D1's registers (r500_reg.h). */
#define V9X_RS690_D1CRTC_CONTROL        0x6080ul
#define V9X_RS690_D1CRTC_STATUS         0x609cul
#define V9X_RS690_D1GRPH_ENABLE         0x6100ul
#define V9X_RS690_D1GRPH_PRIMARY        0x6110ul
#define V9X_RS690_D1GRPH_SECONDARY      0x6118ul
#define V9X_RS690_D1GRPH_UPDATE         0x6144ul
#define V9X_RS690_D1GRPH_FLIP_CONTROL   0x6148ul
#define V9X_RS690_CRTC_EN               0x00000001ul
#define V9X_RS690_CRTC_V_BLANK          0x00000001ul
#define V9X_RS690_GRPH_ENABLE_BIT       0x00000001ul
#define V9X_RS690_UPDATE_PENDING        0x00000004ul
#define V9X_RS690_UPDATE_LOCK           0x00010000ul
/* D1GRPH_FLIP_CONTROL clear: taken at the vertical blank, not the
 * horizontal (SURFACE_UPDATE_H_RETRACE_EN is the async flip). */
#define V9X_RS690_FLIP_AT_VBLANK        0x00000000ul

/* rs600_page_flip waits up to usec_timeout (100 ms) for the pending bit to
 * rise under the lock; it rises on the write, so this bound only has to
 * tell slow from never. */
#define V9X_RS690_PENDING_SPINS         0x00010000ul

/* The window the mini-VDD maps; D1's registers sit inside it. */
#define V9X_RS690_SCANOUT_WINDOW_BYTES  0x00010000ul

static int v9x_rs690_scanout_pending = 0;

static volatile DWORD *v9x_rs690_scanout_reg(DWORD offset)
{
    return (volatile DWORD *)(v9x_hal->engine.control_linear_base + offset);
}

int v9x_rs690_scanout_active(void)
{
    /* The HD 2600 XT (RV630) too: the same AVIVO D1 block and the same
     * Linux routine (radeon_asic.c rv6xx_asic: rs600_page_flip,
     * rs600_page_flip_pending, avivo_wait_for_vblank), with its own
     * address (eng_r6.c, v9x_r6_flip_address). Its register probe read the
     * VBE mode on D1GRPH
     * (docs\decisions\2026-11-13-rv630-register-probe.md). */
    if (v9x_hal == 0 ||
        (v9x_hal->engine.flags & V9X_DD_ENGINE_VALID) == 0ul ||
        (v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_ATI_RS690 &&
         v9x_hal->engine.engine_type != V9X_DD_ENGINE_TYPE_ATI_R600) ||
        v9x_hal->engine.control_linear_base == 0ul ||
        v9x_hal->engine.mapped_aperture_bytes <
            V9X_RS690_SCANOUT_WINDOW_BYTES) {
        return 0;
    }
    return (v9x_hal->engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP) != 0ul;
}

int v9x_rs690_scanout_live(void)
{
    if (!v9x_rs690_scanout_active()) {
        return 0;
    }
    return (*v9x_rs690_scanout_reg(V9X_RS690_D1CRTC_CONTROL) &
            V9X_RS690_CRTC_EN) != 0ul &&
           (*v9x_rs690_scanout_reg(V9X_RS690_D1GRPH_ENABLE) &
            V9X_RS690_GRPH_ENABLE_BIT) != 0ul;
}

int v9x_rs690_in_vblank(void)
{
    if (!v9x_rs690_scanout_active()) {
        return 0;
    }
    return (*v9x_rs690_scanout_reg(V9X_RS690_D1CRTC_STATUS) &
            V9X_RS690_CRTC_V_BLANK) != 0ul;
}

int v9x_rs690_set_display_start(DWORD byte_offset)
{
    DWORD address;
    DWORD update;
    DWORD spins;

    if (!v9x_rs690_scanout_live()) {
        return 0;
    }
    if (v9x_hal->engine.engine_type == V9X_DD_ENGINE_TYPE_ATI_R600) {
        if (!v9x_r6_flip_address(byte_offset, &address)) {
            return 0;
        }
    } else if (!v9x_rs690_flip_address(byte_offset, &address)) {
        return 0;
    }
    update = *v9x_rs690_scanout_reg(V9X_RS690_D1GRPH_UPDATE);
    *v9x_rs690_scanout_reg(V9X_RS690_D1GRPH_UPDATE) =
        update | V9X_RS690_UPDATE_LOCK;
    *v9x_rs690_scanout_reg(V9X_RS690_D1GRPH_FLIP_CONTROL) =
        V9X_RS690_FLIP_AT_VBLANK;
    *v9x_rs690_scanout_reg(V9X_RS690_D1GRPH_SECONDARY) = address;
    *v9x_rs690_scanout_reg(V9X_RS690_D1GRPH_PRIMARY) = address;
    for (spins = 0ul; spins < V9X_RS690_PENDING_SPINS; ++spins) {
        if ((*v9x_rs690_scanout_reg(V9X_RS690_D1GRPH_UPDATE) &
             V9X_RS690_UPDATE_PENDING) != 0ul) {
            break;
        }
    }
    /* Unlocked, the controller takes the address at the next blank. Linux
     * unlocks whether or not the bit rose, and so does this. */
    *v9x_rs690_scanout_reg(V9X_RS690_D1GRPH_UPDATE) =
        update & ~V9X_RS690_UPDATE_LOCK;
    v9x_rs690_scanout_pending = 1;
    return 1;
}

/* Pending while the controller has the address but has not yet taken it
 * at a vertical blank (rs600_page_flip_pending). */
int v9x_rs690_flip_pending(void)
{
    if (!v9x_rs690_scanout_pending || !v9x_rs690_scanout_live()) {
        return 0;
    }
    return (*v9x_rs690_scanout_reg(V9X_RS690_D1GRPH_UPDATE) &
            V9X_RS690_UPDATE_PENDING) != 0ul;
}

/* The flip state machine has called the flip done: count whether the chip
 * agreed, as the Evergreen and Rage 128 paths do. */
void v9x_rs690_note_flip_done(void)
{
    if (!v9x_rs690_scanout_pending) {
        return;
    }
    v9x_rs690_scanout_pending = 0;
    if (v9x_rs690_scanout_live() &&
        (*v9x_rs690_scanout_reg(V9X_RS690_D1GRPH_UPDATE) &
         V9X_RS690_UPDATE_PENDING) == 0ul) {
        ++v9x_hal->d3d_diagnostics.flip_taken_at_done;
    } else {
        ++v9x_hal->d3d_diagnostics.flip_not_taken_at_done;
    }
}

/* A new session or mode: the video BIOS rewrote the controller. */
void v9x_rs690_scanout_reset(void)
{
    v9x_rs690_scanout_pending = 0;
}
