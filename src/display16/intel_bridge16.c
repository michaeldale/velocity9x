/*
 * The _TEXT side of the Intel code-segment boundary.
 *
 * Two shared services are called from the moved Intel units but are used by
 * every family, so they stay near in _TEXT: v9x_get_build_identity (build.c)
 * and v9x_selected_mode_geometry (ddi.c). Neither may be far-ised, because
 * that would force the other four families to change for a problem only Intel
 * has.
 *
 * This unit is the bridge: it stays in _TEXT, calls them near, and exposes
 * them to I9XXCODE as far entry points. About thirty bytes of _TEXT, which is
 * the whole cost of not touching the shared layer
 * (docs\plans\intel-gma950-phase5.md).
 */
/*
 * This unit is not an OS boundary and does not include <windows.h>: it calls
 * no Win16 API, only two of the driver's own functions. The declaration below
 * spells ddi.c's WORD FAR * parameters as plain unsigned short *, which is the
 * identical type here - the compact memory model this driver is built with
 * addresses all data far, so FAR on a data pointer is already the default.
 */
#include "velocity9x/build.h"
#include "velocity9x/intel16.h"

extern unsigned short v9x_selected_mode_geometry(
    unsigned short *width, unsigned short *height,
    unsigned short *bpp, unsigned short *pitch);

const struct v9x_build_identity *V9X_I9XX_FAR
v9x_intel_bridge_build_identity(void)
{
    return v9x_get_build_identity();
}

unsigned short V9X_I9XX_FAR v9x_intel_bridge_mode_geometry(
    unsigned short *width, unsigned short *height,
    unsigned short *bpp, unsigned short *pitch)
{
    return v9x_selected_mode_geometry(width, height, bpp, pitch);
}
