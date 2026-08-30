/*
 * The Direct3D back-end decision. Pure policy: see
 * include\velocity9x\d3dmode.h for why none of it lives in dd16.c.
 */
#include "velocity9x/d3dmode.h"

v9x_u16 v9x_d3d_mode_resolve(v9x_u16 requested, v9x_u16 chip_has_d3d)
{
    /*
     * The mode-supplied engines are tested first, and the order is the
     * decision.
     *
     * Software, hybrid and offload exist precisely so that a chip with no 3D
     * can serve Direct3D, so answering them from chip_has_d3d would report
     * NONE on every card they are meant for and HARDWARE on the one card that
     * does not need them. Neither is what was asked for.
     *
     * Software is now implemented and resolves to itself. Hybrid and offload
     * are not, and for them the honest answer is still that the request was
     * understood and cannot be met, which advertises nothing. That ordering
     * was measured on a Trio64 while all three were unwritten
     * (docs\decisions6-08-30-d3d-mode-disabled-gate.md), which is why
     * landing software changed one arm of this and left the rest alone.
     */
    if (requested == V9X_D3D_REQUEST_SOFTWARE) {
        return V9X_D3D_STATE_SOFTWARE;
    }
    if (requested == V9X_D3D_REQUEST_HYBRID ||
        requested == V9X_D3D_REQUEST_OFFLOAD) {
        return V9X_D3D_STATE_UNIMPLEMENTED;
    }

    /* The chip is the authority, the same way it is for the GdiAccel keys in
     * src\display16\gdi_accel.c: a setting cannot grant a capability. */
    if (chip_has_d3d == V9X_FALSE) {
        return V9X_D3D_STATE_NONE;
    }

    if (requested == V9X_D3D_REQUEST_DISABLED) {
        return V9X_D3D_STATE_DISABLED;
    }

    /*
     * HARDWARE, and so is anything unrecognised. A value this build does not
     * know is a typo or a newer driver's key, not a request for something,
     * and the safe reading of a typo is the behaviour the machine had before
     * the key existed.
     */
    return V9X_D3D_STATE_HARDWARE;
}

v9x_u16 v9x_d3d_mode_advertises(v9x_u16 state)
{
    /* Two states advertise, and they advertise different engines: HARDWARE
     * the chip's, SOFTWARE the rasterizer's. Which one the 32-bit HAL
     * resolves is decided by the capability bits this answer drives, not
     * here. */
    return (state == V9X_D3D_STATE_HARDWARE ||
            state == V9X_D3D_STATE_SOFTWARE) ? V9X_TRUE : V9X_FALSE;
}

const char *v9x_d3d_mode_text(v9x_u16 state)
{
    if (state == V9X_D3D_STATE_HARDWARE) {
        return "hardware";
    }
    if (state == V9X_D3D_STATE_NONE) {
        return "none";
    }
    if (state == V9X_D3D_STATE_DISABLED) {
        return "user-disabled";
    }
    if (state == V9X_D3D_STATE_UNIMPLEMENTED) {
        return "mode-unimplemented";
    }
    if (state == V9X_D3D_STATE_SOFTWARE) {
        return "software";
    }
    return "unknown";
}
