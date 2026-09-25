/*
 * The Gen3 depth comparison table. See include\velocity9x\i9xx_depth.h for
 * why this is a module of its own and where the encodings come from.
 */
#include "velocity9x/i9xx_depth.h"
#include "velocity9x/intel_gen3_3d.h"

/*
 * D3DCMP_*, from the DirectX headers, repeated here so this module does not
 * depend on the DirectDraw ABI header for eight small numbers.
 */
#define V9X_I9XX_D3DCMP_NEVER         ((v9x_u32)1ul)
#define V9X_I9XX_D3DCMP_LESS          ((v9x_u32)2ul)
#define V9X_I9XX_D3DCMP_EQUAL         ((v9x_u32)3ul)
#define V9X_I9XX_D3DCMP_LESSEQUAL     ((v9x_u32)4ul)
#define V9X_I9XX_D3DCMP_GREATER       ((v9x_u32)5ul)
#define V9X_I9XX_D3DCMP_NOTEQUAL      ((v9x_u32)6ul)
#define V9X_I9XX_D3DCMP_GREATEREQUAL  ((v9x_u32)7ul)
#define V9X_I9XX_D3DCMP_ALWAYS        ((v9x_u32)8ul)

v9x_u16 v9x_i9xx_depth_func(v9x_u32 d3d_compare, v9x_u32 *encoded_out)
{
    v9x_u32 encoded;

    if (encoded_out == 0) {
        return V9X_FALSE;
    }

    /*
     * Seven of the eight map to their own number, which is a coincidence of
     * the two tables and not a rule - D3DCMP_ALWAYS is 8 and COMPAREFUNC
     * ALWAYS is 0. Written out one arm at a time rather than as arithmetic,
     * so that the coincidence cannot quietly become an assumption.
     */
    switch (d3d_compare) {
    case V9X_I9XX_D3DCMP_NEVER:
        encoded = V9X_I9XX_COMPAREFUNC_NEVER_V;
        break;
    case V9X_I9XX_D3DCMP_LESS:
        encoded = V9X_I9XX_COMPAREFUNC_LESS_V;
        break;
    case V9X_I9XX_D3DCMP_EQUAL:
        encoded = V9X_I9XX_COMPAREFUNC_EQUAL_V;
        break;
    case V9X_I9XX_D3DCMP_LESSEQUAL:
        encoded = V9X_I9XX_COMPAREFUNC_LEQUAL_V;
        break;
    case V9X_I9XX_D3DCMP_GREATER:
        encoded = V9X_I9XX_COMPAREFUNC_GREATER_V;
        break;
    case V9X_I9XX_D3DCMP_NOTEQUAL:
        encoded = V9X_I9XX_COMPAREFUNC_NOTEQUAL_V;
        break;
    case V9X_I9XX_D3DCMP_GREATEREQUAL:
        encoded = V9X_I9XX_COMPAREFUNC_GEQUAL_V;
        break;
    case V9X_I9XX_D3DCMP_ALWAYS:
        encoded = V9X_I9XX_COMPAREFUNC_ALWAYS_V;
        break;
    default:
        return V9X_FALSE;
    }

    *encoded_out = encoded;

    return V9X_TRUE;
}

/* The largest value ALPHAREF can mean as a whole byte, and 1.0 as D3DFIXED. */
#define V9X_I9XX_ALPHA_REF_BYTE_MAX   ((v9x_u32)0x000000fful)
#define V9X_I9XX_ALPHA_REF_FIXED_ONE  ((v9x_u32)0x00010000ul)

v9x_u16 v9x_i9xx_alpha_test_bits(v9x_u32 enable, v9x_u32 d3d_compare,
                                 v9x_u32 d3d_ref, v9x_u32 *bits_out)
{
    v9x_u32 function;
    v9x_u32 reference;

    if (bits_out == 0) {
        return V9X_FALSE;
    }
    if (enable == 0ul) {
        *bits_out = 0ul;
        return V9X_TRUE;
    }
    if (v9x_i9xx_depth_func(d3d_compare, &function) == V9X_FALSE) {
        return V9X_FALSE;
    }

    /* ALWAYS passes every fragment, which is no test: Direct3D's default
     * with the enable set, and emitted as nothing. */
    if (function == V9X_I9XX_COMPAREFUNC_ALWAYS_V) {
        *bits_out = 0ul;
        return V9X_TRUE;
    }

    /*
     * The reference. The DirectX 5 header declares D3DRENDERSTATE_ALPHAREF a
     * D3DFIXED, 16.16; DirectX 6 redefined it as a byte, 0 to 255, and
     * applications written against either reach this HAL. A value that fits
     * a byte is read as one, and anything larger as 16.16 scaled to a byte,
     * with 1.0 and above saturating. A DirectX 5 application passing a
     * fraction below 1/256 would be read as a whole byte instead - not seen,
     * and zero, what Half-Life passes, means zero either way.
     */
    if (d3d_ref <= V9X_I9XX_ALPHA_REF_BYTE_MAX) {
        reference = d3d_ref;
    } else if (d3d_ref >= V9X_I9XX_ALPHA_REF_FIXED_ONE) {
        reference = V9X_I9XX_ALPHA_REF_BYTE_MAX;
    } else {
        reference = (d3d_ref * V9X_I9XX_ALPHA_REF_BYTE_MAX + 0x8000ul) >> 16;
    }

    *bits_out = V9X_I9XX_S6_ALPHA_TEST_ENABLE |
                (function << V9X_I9XX_S6_ALPHA_FUNC_SHIFT) |
                (reference << V9X_I9XX_S6_ALPHA_REF_SHIFT);
    return V9X_TRUE;
}
