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

/*
 * The footprint arithmetic, shared by every surface this engine binds.
 *
 * Extracted when textures and depth buffers arrived rather than copied: the
 * last-row overflow reasoning below is the memory-safety argument for the
 * whole runtime path, and three copies of it is three places for it to drift.
 * The differences between the surfaces are their alignment and dimension
 * rules, which stay with their callers.
 *
 * Returns V9X_TRUE when every byte of the surface lies inside the aperture.
 */
static v9x_u16 v9x_d3d_i9xx_footprint_fits(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 aperture_bytes)
{
    v9x_u32 row_bytes;
    v9x_u32 last_row;

    if (width == 0ul || height == 0ul || pitch == 0ul ||
        aperture_bytes == 0ul) {
        return V9X_FALSE;
    }
    /* A row must hold its own pixels: every surface this engine binds is two
     * bytes a pixel, target, texture and depth alike. */
    row_bytes = width * 2ul;
    if (pitch < row_bytes) {
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
    return (offset + last_row + row_bytes <= aperture_bytes)
        ? V9X_TRUE : V9X_FALSE;
}

/*
 * Binding an application surface as a TEXTURE MAP.
 *
 * The same footprint question as a render target and three different
 * constraints, each MAP_STATE's rather than BUF_INFO's:
 *
 *  - The address is PAGE aligned. i9xx_texture.c requires it and the packet
 *    audit records that as this driver's choice rather than a databook
 *    requirement, since neither reference emitter states one. It is kept
 *    because relaxing an unmeasured choice on the strength of wanting more
 *    textures to work is how a driver acquires a fault nobody can explain.
 *  - The pitch is encoded in dwords, so it is a multiple of four.
 *  - Width and height go in fields one less than the extent, so both are
 *    bounded by the field rather than by the target's dimension limit.
 *
 * Refuses rather than clamps, for the reason bind_target does.
 */
v9x_u16 v9x_d3d_i9xx_bind_map(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 aperture_bytes, v9x_u32 *address_out)
{
    if (address_out == 0) {
        return V9X_FALSE;
    }
    *address_out = 0ul;

    if (width == 0ul || height == 0ul ||
        width > V9X_I9XX_MAP_DIMENSION_MAX ||
        height > V9X_I9XX_MAP_DIMENSION_MAX) {
        return V9X_FALSE;
    }
    if (pitch == 0ul || (pitch & 3ul) != 0ul ||
        pitch > V9X_I9XX_MAP_PITCH_MAX) {
        return V9X_FALSE;
    }
    if ((offset & (V9X_I9XX_SANDBOX_PAGE_BYTES - 1ul)) != 0ul) {
        return V9X_FALSE;
    }
    if (v9x_d3d_i9xx_footprint_fits(offset, pitch, width, height,
                                    aperture_bytes) == V9X_FALSE) {
        return V9X_FALSE;
    }
    *address_out = offset;
    return V9X_TRUE;
}

/*
 * Binding an application surface as the DEPTH buffer.
 *
 * BUF_INFO's constraints, exactly the target's - the two are the same packet
 * with a different identity - so this shares the target's rules rather than
 * restating them, and differs only in not producing a colour identity.
 */
v9x_u16 v9x_d3d_i9xx_bind_depth(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 aperture_bytes, v9x_u32 *address_out)
{
    if (address_out == 0) {
        return V9X_FALSE;
    }
    *address_out = 0ul;

    if (width == 0ul || height == 0ul ||
        width > V9X_I9XX_TARGET_DIMENSION_MAX ||
        height > V9X_I9XX_TARGET_DIMENSION_MAX) {
        return V9X_FALSE;
    }
    if (pitch == 0ul || (pitch & 3ul) != 0ul ||
        pitch > V9X_I9XX_BUF_3D_PITCH_MASK || (offset & 3ul) != 0ul) {
        return V9X_FALSE;
    }
    /*
     * Address zero is refused, and not only because it is a degenerate
     * offset: it is inside the aperture and is not ours, and it is the exact
     * value the removed Phase 5 depth binding carried. The builder and the
     * decoder both refuse it, and this makes three.
     */
    if (offset == 0ul) {
        return V9X_FALSE;
    }
    if (v9x_d3d_i9xx_footprint_fits(offset, pitch, width, height,
                                    aperture_bytes) == V9X_FALSE) {
        return V9X_FALSE;
    }
    *address_out = offset;
    return V9X_TRUE;
}

v9x_u16 v9x_d3d_i9xx_bind_target(
    v9x_u32 offset, v9x_u32 pitch, v9x_u32 width, v9x_u32 height,
    v9x_u32 aperture_bytes,
    v9x_u32 *identity_out, v9x_u32 *address_out)
{
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
    if ((offset & 3ul) != 0ul) {
        return V9X_FALSE;
    }
    if (v9x_d3d_i9xx_footprint_fits(offset, pitch, width, height,
                                    aperture_bytes) == V9X_FALSE) {
        return V9X_FALSE;
    }

    *identity_out = V9X_I9XX_BUF_3D_ID_COLOR_BACK |
                    (pitch & V9X_I9XX_BUF_3D_PITCH_MASK);
    *address_out = offset;
    return V9X_TRUE;
}
