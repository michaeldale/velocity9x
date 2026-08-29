/*
 * ViRGE S3D 1.31 depth conversion.
 *
 * See d3d_zfixed.h for why this is a leaf translation unit.
 *
 * The clamp below is the entire content of this file, and it is not
 * defensive. sz = 1.0 is ordinary geometry - a cleared depth buffer's far
 * plane, any unprojected background quad - and it scales to exactly 2^31,
 * which is one past the largest signed 32-bit integer. Measured on the host
 * build before the clamp existed:
 *
 *     depth(1.000000) = -2147483648      i.e. 0x80000000
 *
 * That is the x87 integer indefinite, which is what `fistp dword` stores for
 * an out-of-range operand of EITHER sign, and what a C cast produces here.
 * It is also the worst value this register can carry: 86Box forms the pixel
 * depth as (start << 1) >> 16 (build\reference-vid_s3_virge.c:4261), and
 * 0x80000000 << 1 is zero - so the far plane becomes the NEAR plane and the
 * background occludes the whole scene. Nothing about that looks like a
 * conversion fault when you see it on screen.
 *
 * So the range check happens in the float domain, before the conversion. It
 * cannot be done afterwards: a post-conversion clamp sees 0x80000000 as a
 * large negative number and clamps it to the wrong end. That is why the
 * clamp-after pattern in v9x_d3d_fixed_8_7 could not be copied here - that
 * one is safe only because byte-range colours cannot overflow in the first
 * place.
 *
 * The comparisons are written so that a NaN takes the else arm rather than
 * reaching the conversion: `!(value > 0.0f)` is true for NaN, `value >= 1.0f`
 * is false for it.
 */
#include "d3d_zfixed.h"

/*
 * 2^31. Exact as a float, and multiplying by a power of two loses no mantissa,
 * so the only inaccuracy is the input's own 24-bit precision - far more than
 * the 16 bits the hardware keeps.
 */
#define V9X_D3D_Z_1_31_SCALE 2147483648.0f

/*
 * The float-to-integer step, inline.
 *
 * A plain C cast cannot be used here: Open Watcom lowers it to a call to its
 * runtime helper __CHP, and the HAL links with `option nodefaultlibs`, so the
 * DLL fails to link with __CHP undefined. That is the same reason
 * ddhal_internal.h carries this pragma for the other fixed-point converters -
 * but this file deliberately does not include that header, because doing so
 * would drag MMIO and the whole DDHAL into the host build and put this
 * arithmetic back out of reach of a test. Four instructions duplicated is the
 * cheaper of the two prices.
 *
 * fistp rounds to nearest under the default control word, which is what the
 * DDK's MYFLINT31 does too, and what the host build gets as well - so the
 * tested behaviour and the shipped behaviour are the same behaviour.
 *
 * It neither saturates nor wraps on overflow: it raises a masked
 * invalid-operation and stores 0x80000000 for either sign. Everything below
 * exists to make sure it is never handed a value that can do that.
 */
static long v9x_d3d_z_fistp(float value);
#pragma aux v9x_d3d_z_fistp = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];
long v9x_d3d_z_to_1_31_depth(float value)
{
    if (value >= 1.0f) {
        return V9X_D3D_Z_1_31_MAX;
    }
    if (!(value > 0.0f)) {
        /* Zero, negative, or NaN. A NaN vertex resolves to the far plane so
         * that garbage is occluded rather than occluding; a negative depth is
         * in front of the near plane and clamps to it. */
        return value == value ? 0l : V9X_D3D_Z_1_31_MAX;
    }
    return v9x_d3d_z_fistp(value * V9X_D3D_Z_1_31_SCALE);
}

long v9x_d3d_z_to_1_31_signed(float value)
{
    if (value >= 1.0f) {
        return V9X_D3D_Z_1_31_MAX;
    }
    if (value <= -1.0f) {
        return -V9X_D3D_Z_1_31_MAX;
    }
    if (value != value) {
        /* NaN. A flat-depth triangle degrades gracefully; the indefinite
         * would be a maximal negative slope across the whole span. */
        return 0l;
    }
    return v9x_d3d_z_fistp(value * V9X_D3D_Z_1_31_SCALE);
}
