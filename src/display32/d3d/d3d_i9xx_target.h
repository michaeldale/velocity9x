/*
 * Binding an arbitrary surface as a Gen3 render target.
 *
 * A LEAF translation unit, on the d3d_zfixed.c precedent and for the same
 * reason: it is pure arithmetic over numbers, it is the first thing a runtime
 * draw does, and it must be reachable by the host suite. Including
 * d3d_internal.h would drag in the DDHAL headers, which the host build does
 * not have - so this file knows nothing about DirectDraw and takes the four
 * numbers a surface amounts to.
 *
 * Every draw this project has performed renders into one page of the sandbox
 * reserve, at an address and pitch the build chose. This is where that stops
 * being true.
 */
#ifndef VELOCITY9X_D3D_I9XX_TARGET_H
#define VELOCITY9X_D3D_I9XX_TARGET_H

#include "velocity9x/types.h"

/*
 * Produce the BUF_INFO identity and address for a surface, or refuse it.
 *
 * Returns V9X_TRUE on success. On refusal both outputs are zeroed, so a caller
 * that ignores the result emits a binding to address zero - which the decoder
 * refuses - rather than one to whatever the stack held.
 *
 * The constraints are BUF_INFO's, restated against a surface rather than the
 * reserve: the pitch field DISCARDS its low two bits, so a pitch that is not a
 * multiple of four puts every row but the first at the wrong address; the
 * address is a graphics offset and is dword aligned for the same reason; and
 * the whole surface must lie inside the aperture the driver was given.
 *
 * Refuses rather than clamps. A clamped surface draws a wrong picture with no
 * error anywhere, which is the failure mode this driver is built to avoid.
 */
v9x_u16 v9x_d3d_i9xx_bind_target(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 aperture_bytes,
    v9x_u32 *identity_out, v9x_u32 *address_out);

#endif /* VELOCITY9X_D3D_I9XX_TARGET_H */
