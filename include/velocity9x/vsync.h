/*
 * Whether a page flip waits for the vertical blank: the decision half.
 *
 * The user-facing setting lives in SYSTEM.INI, beside Direct3D=:
 *
 *   [Velocity9x]
 *   VSync=0
 *
 * src\display16\dd16.c reads it every time DirectDraw creates its driver
 * object and stamps the answer into engine_caps. V9XHAL.DLL reads the bits
 * and asks this module, per flip, whether to skip the blank. The rule is the
 * same on every chip, so it lives here and not in an engine
 * (docs\plans\vsync-off-setting.md).
 *
 * Pure policy in the d3dmode.h pattern: no I/O, no OS, no DirectDraw header.
 */
#ifndef VELOCITY9X_VSYNC_H
#define VELOCITY9X_VSYNC_H

#include "velocity9x/types.h"

/* The SYSTEM.INI key, in the display driver's [Velocity9x] section. */
#define V9X_VSYNC_SETTING_KEY "VSync"

/*
 * What the user asked for.
 *
 * APPLICATION is zero so that an absent key, a blank value and text that
 * GetPrivateProfileInt cannot parse all keep today's behaviour. Off is not
 * zero, because a typo must not turn vsync off.
 */
#define V9X_VSYNC_REQUEST_APPLICATION ((v9x_u16)0u)
#define V9X_VSYNC_REQUEST_ON          ((v9x_u16)1u)
#define V9X_VSYNC_REQUEST_OFF         ((v9x_u16)2u)

/*
 * What the driver does with it. Same numbers, but a resolved state is
 * always one of these three; a request may be anything the file holds.
 */
#define V9X_VSYNC_STATE_APPLICATION ((v9x_u16)0u)
#define V9X_VSYNC_STATE_ON          ((v9x_u16)1u)
#define V9X_VSYNC_STATE_OFF         ((v9x_u16)2u)

/* The request against what this build knows. Unknown values are the
 * application's choice, the behaviour before the key existed. */
v9x_u16 v9x_vsync_resolve(v9x_u16 requested);

/*
 * Whether this flip skips the blank: V9X_TRUE to write the start address
 * now and leave nothing pending.
 *
 * application_novsync is V9X_TRUE when the flip carried DDFLIP_NOVSYNC.
 * ON overrides it; OFF overrides its absence.
 */
v9x_u16 v9x_vsync_flip_novsync(v9x_u16 state, v9x_u16 application_novsync);

/*
 * The state as the string written to C:\V9XDIAG\V9XHW.INI as VSync= and
 * compared by tools\diag\settings_status.c. Stable text, not prose.
 */
const char *v9x_vsync_text(v9x_u16 state);

#endif /* VELOCITY9X_VSYNC_H */
