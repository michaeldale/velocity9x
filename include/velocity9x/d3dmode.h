/*
 * Which Direct3D back end serves this machine: the decision half.
 *
 * The driver publishes Direct3D capability at DriverInit, before the 32-bit
 * HAL knows anything about the hardware, so the choice has to be made on the
 * 16-bit side and stamped into the shared block. That mechanism is in
 * src\display16\dd16.c. This header and src\common\d3dmode.c are the policy
 * behind it: no I/O, no OS, no DirectDraw header - a requested mode and one
 * fact about the chip go in, a state code comes out, and every rule below is
 * host-tested.
 *
 * The split is the same one src\common\mtrr.c uses and for the same reason.
 * It also buys something specific here: the characteristic Direct3D failure
 * on this driver is advertising a capability that is not implemented, and the
 * mode selector multiplies the ways to do that by the number of modes. A
 * table-driven resolve with a test per row is what stops a mode nobody has
 * written from resolving to one that exists.
 *
 * The user-facing setting lives in SYSTEM.INI:
 *
 *   [Velocity9x]
 *   Direct3D=0
 *
 * Read at Enable, with GetPrivateProfileInt, in the same section and by the
 * same call as the GdiAccel keys in src\display16\gdi_accel.c. An absent key
 * is V9X_D3D_REQUEST_HARDWARE, which is what every family did before this
 * existed, so adding the mechanism changes no machine's behaviour.
 *
 * docs\plans\s3-trio64-voodoo2-hybrid-3d.md is the plan these values come
 * from; the request numbers below are that document's mode numbers, with zero
 * added for the chip's own engine.
 */
#ifndef VELOCITY9X_D3DMODE_H
#define VELOCITY9X_D3DMODE_H

#include "velocity9x/types.h"

/* The SYSTEM.INI key. The section and the file are the display driver's, in
 * src\display16\dd16.c, beside the identical pair in gdi_accel.c. */
#define V9X_D3D_SETTING_KEY "Direct3D"

/*
 * What the user asked for.
 *
 * HARDWARE is zero so that an absent key, a blank value and a value
 * GetPrivateProfileInt cannot parse all land on the pre-existing behaviour.
 */
#define V9X_D3D_REQUEST_HARDWARE ((v9x_u16)0u)
#define V9X_D3D_REQUEST_DISABLED ((v9x_u16)1u)
#define V9X_D3D_REQUEST_SOFTWARE ((v9x_u16)2u)
#define V9X_D3D_REQUEST_HYBRID   ((v9x_u16)3u)
#define V9X_D3D_REQUEST_OFFLOAD  ((v9x_u16)4u)

/*
 * What the driver will actually do, which is not the same thing.
 *
 * NONE and DISABLED both end with no Direct3D advertised and are deliberately
 * distinct: one is a property of the card and the other is a property of the
 * setting, and a settings page that conflated them would tell somebody with a
 * Trio64 to turn Direct3D back on.
 */
#define V9X_D3D_STATE_HARDWARE      ((v9x_u16)0u)
#define V9X_D3D_STATE_NONE          ((v9x_u16)1u)
#define V9X_D3D_STATE_DISABLED      ((v9x_u16)2u)
#define V9X_D3D_STATE_UNIMPLEMENTED ((v9x_u16)3u)

/*
 * Resolve the setting against the chip.
 *
 * chip_has_d3d is V9X_TRUE when the chip's engine descriptor claims
 * V9X_DD_ENGINE_CAP_D3D. It is passed as a plain flag rather than as the caps
 * word so that this file need not include the DirectDraw ABI header, which
 * would put a 16:16 far-pointer contract into the host build.
 */
v9x_u16 v9x_d3d_mode_resolve(v9x_u16 requested, v9x_u16 chip_has_d3d);

/* Whether that state advertises Direct3D to DDRAW. Exactly one state does. */
v9x_u16 v9x_d3d_mode_advertises(v9x_u16 state);

/*
 * The state as the string written to C:\V9XDIAG\V9XHW.INI and read back by
 * the Display Properties page. Stable text, not prose: settings_status.c
 * compares against these spellings.
 */
const char *v9x_d3d_mode_text(v9x_u16 state);

#endif /* VELOCITY9X_D3DMODE_H */
