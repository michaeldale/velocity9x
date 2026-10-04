/*
 * The vsync setting's decision. Pure policy: see include\velocity9x\vsync.h
 * for why none of it lives in dd16.c or the HAL.
 */
#include "velocity9x/vsync.h"

v9x_u16 v9x_vsync_resolve(v9x_u16 requested)
{
    if (requested == V9X_VSYNC_REQUEST_ON) {
        return V9X_VSYNC_STATE_ON;
    }
    if (requested == V9X_VSYNC_REQUEST_OFF) {
        return V9X_VSYNC_STATE_OFF;
    }

    /* Absent, unparsable, or a value this build does not know: the
     * behaviour the machine had before the key existed. */
    return V9X_VSYNC_STATE_APPLICATION;
}

v9x_u16 v9x_vsync_flip_novsync(v9x_u16 state, v9x_u16 application_novsync)
{
    if (state == V9X_VSYNC_STATE_ON) {
        return V9X_FALSE;
    }
    if (state == V9X_VSYNC_STATE_OFF) {
        return V9X_TRUE;
    }

    /* The application decides, and so does any state not named above: the
     * caller derives the state from a capability word, and a word nobody
     * meant must not tear. */
    return application_novsync != V9X_FALSE ? V9X_TRUE : V9X_FALSE;
}

const char *v9x_vsync_text(v9x_u16 state)
{
    if (state == V9X_VSYNC_STATE_ON) {
        return "always-on";
    }
    if (state == V9X_VSYNC_STATE_OFF) {
        return "always-off";
    }
    return "application";
}
