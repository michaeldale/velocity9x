/*
 * See d3d_i9xx_target.h for why this is a leaf translation unit.
 *
 * The bounds arithmetic below is the whole content, and the overflow checks in
 * it are not defensive. These are application-supplied numbers: a surface
 * whose footprint wraps a 32-bit product passes a bounds test BY BEING SMALL,
 * and the surface it described would then be written wherever the wrap landed.
 * The same hazard the blit builder's own bounds check exists for, at the other
 * end of the driver.
 */
#include "d3d_i9xx_target.h"

#include "velocity9x/intel_gma.h"
#include "velocity9x/intel_gen3_3d.h"

/*
 * The widest surface this accepts, in pixels.
 *
 * Not the part's architectural maximum, which nothing has exercised. It bounds
 * the products below so their overflow checks have something to be checked
 * against, and it is restated in the engine's limits table where the core
 * reads it.
 */
#define V9X_I9XX_TARGET_DIMENSION_MAX ((v9x_u32)2048ul)

v9x_u16 v9x_d3d_i9xx_bind_target(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 aperture_bytes,
    v9x_u32 *identity_out, v9x_u32 *address_out)
{
    v9x_u32 row_bytes;
    v9x_u32 last_row;

    if (identity_out == 0 || address_out == 0) {
        return V9X_FALSE;
    }
    *identity_out = 0ul;
    *address_out = 0ul;

    if (width == 0ul || height == 0ul || pitch == 0ul ||
        aperture_bytes == 0ul) {
        return V9X_FALSE;
    }
    if (width > V9X_I9XX_TARGET_DIMENSION_MAX ||
        height > V9X_I9XX_TARGET_DIMENSION_MAX) {
        return V9X_FALSE;
    }

    /*
     * The pitch must survive the encoding. BUF_INFO carries it in a masked
     * field whose low two bits are discarded, so a pitch of 1282 - a plausible
     * thing for an application to ask for - would place every row but the
     * first two bytes early. A sheared picture, and no error anywhere.
     */
    if ((pitch & 3ul) != 0ul || pitch > V9X_I9XX_BUF_3D_PITCH_MASK) {
        return V9X_FALSE;
    }
    /* And it must hold its own row: RGB565 is two bytes a pixel. */
    row_bytes = width * 2ul;
    if (pitch < row_bytes) {
        return V9X_FALSE;
    }
    if ((offset & 3ul) != 0ul) {
        return V9X_FALSE;
    }

    /*
     * The last row's last byte must be inside the aperture.
     *
     * (height - 1) rows plus one row, not height rows: a surface whose final
     * row ends exactly at the aperture's end is legal, and the simpler
     * arithmetic would refuse it. Refusing a legal surface is how a driver
     * comes to work only on the modes somebody happened to try.
     */
    if ((height - 1ul) > (0xfffffffful / pitch)) {
        return V9X_FALSE;
    }
    last_row = (height - 1ul) * pitch;
    if (last_row > 0xfffffffful - row_bytes) {
        return V9X_FALSE;
    }
    if (offset > 0xfffffffful - last_row - row_bytes) {
        return V9X_FALSE;
    }
    if (offset + last_row + row_bytes > aperture_bytes) {
        return V9X_FALSE;
    }

    *identity_out = V9X_I9XX_BUF_3D_ID_COLOR_BACK |
                    (pitch & V9X_I9XX_BUF_3D_PITCH_MASK);
    *address_out = offset;
    return V9X_TRUE;
}
