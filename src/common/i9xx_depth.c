/*
 * The Gen3 depth comparison table. See include\velocity9x\i9xx_depth.h for
 * why this is a module of its own and where the encodings come from.
 */
#include "velocity9x/i9xx_depth.h"

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
