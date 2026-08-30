/*
 * The Direct3D back-end decision. Pure policy: see
 * include\velocity9x\d3dmode.h for why none of it lives in dd16.c.
 */
#include "velocity9x/d3dmode.h"

v9x_u16 v9x_d3d_mode_resolve(v9x_u16 requested, v9x_u16 chip_has_d3d)
{
    /*
     * The unwritten modes are tested first, and the order is the decision.
     *
     * Software, hybrid and offload exist precisely so that a chip with no 3D
     * can serve Direct3D, so answering them from chip_has_d3d would report
     * NONE on every card they are meant for and HARDWARE on the one card that
     * does not need them. Neither is what was asked for. Until one of them is
     * implemented the honest answer is that the request was understood and
     * cannot be met, which advertises nothing.
     */
    if (requested == V9X_D3D_REQUEST_SOFTWARE ||
        requested == V9X_D3D_REQUEST_HYBRID ||
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
    return state == V9X_D3D_STATE_HARDWARE ? V9X_TRUE : V9X_FALSE;
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
    return "unknown";
}
