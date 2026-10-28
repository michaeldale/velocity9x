#include "d3d_select.h"
#include "velocity9x/engine_abi.h"

v9x_u32 v9x_d3d_select_engine(int valid, v9x_u32 engine_type,
                              v9x_u32 engine_caps)
{
    /*
     * Mode first, chip second: the software engine is asked for by
     * capability, so a card with an S3D unit can still be given the
     * rasterizer, and a chip with no engine can have it at all.
     */
    if ((engine_caps & V9X_DD_ENGINE_CAP_D3D_SOFTWARE) != 0ul) {
        return V9X_D3D_SELECT_SOFTWARE;
    }
    if (!valid) {
        return V9X_D3D_SELECT_NONE;
    }

    /*
     * Only types with an engine in this binary. S3_TRIO64 has no S3D core
     * and selects nothing rather than inheriting another chip's caps. The
     * Mach64 engine is present but dormant: no ATI chip stamps its type.
     * The Rage II class has the Mach64 2D engine but no setup engine, so it
     * has its own (d3d_rage2.c) and never reaches d3d_mach64.c.
     */
    switch (engine_type) {
    case V9X_DD_ENGINE_TYPE_S3_VIRGE_DX:
        return V9X_D3D_SELECT_VIRGE;
    case V9X_DD_ENGINE_TYPE_INTEL_GEN3:
        /* The GM965 and Ironlake share the type for their ring and
         * blitter; their 3D engine is Gen4's, and only with CAP_D3D, as
         * Haswell's. */
        if ((engine_caps & V9X_DD_ENGINE_CAPS_I9XX_G4_3D) != 0ul) {
            if ((engine_caps & V9X_DD_ENGINE_CAP_D3D) == 0ul) {
                return V9X_D3D_SELECT_NONE;
            }
            return V9X_D3D_SELECT_GEN4;
        }
        return V9X_D3D_SELECT_GEN3;
    case V9X_DD_ENGINE_TYPE_ATI_MACH64:
        return V9X_D3D_SELECT_MACH64;
    case V9X_DD_ENGINE_TYPE_ATI_RAGE2:
        return V9X_D3D_SELECT_RAGE2;
    case V9X_DD_ENGINE_TYPE_SIS_6326:
        return V9X_D3D_SELECT_SIS6326;
    case V9X_DD_ENGINE_TYPE_MGA:
        /* One type for the family. The 2064W has no texture engine and its
         * hook stamps no CAP_D3D. The 2164W's stamps MGA_DEPTH32 beside it;
         * the Mystique's, with a 16-bit Z buffer only, does not. */
        if ((engine_caps & V9X_DD_ENGINE_CAP_D3D) == 0ul) {
            return V9X_D3D_SELECT_NONE;
        }
        if ((engine_caps & V9X_DD_ENGINE_CAP_MGA_DEPTH32) == 0ul) {
            return V9X_D3D_SELECT_MGA16;
        }
        return V9X_D3D_SELECT_MGA;
    case V9X_DD_ENGINE_TYPE_TDFX:
        /* The VSA-100: its hook stamps CAP_D3D beside the 2D engine, and a
         * build without the 3D engine selects nothing. */
        if ((engine_caps & V9X_DD_ENGINE_CAP_D3D) == 0ul) {
            return V9X_D3D_SELECT_NONE;
        }
        return V9X_D3D_SELECT_TDFX;
    case V9X_DD_ENGINE_TYPE_INTEL_GEN75:
        /* Haswell: hsw_hw16.c stamps CAP_D3D only when the mini-VDD opened
         * the render ring beside the blitter's. */
        if ((engine_caps & V9X_DD_ENGINE_CAP_D3D) == 0ul) {
            return V9X_D3D_SELECT_NONE;
        }
        return V9X_D3D_SELECT_GEN75;
    case V9X_DD_ENGINE_TYPE_ATI_RAGE128:
        /* The Rage 128: rage128_hw16.c stamps CAP_D3D beside the 2D
         * engine; without it the type selects nothing. */
        if ((engine_caps & V9X_DD_ENGINE_CAP_D3D) == 0ul) {
            return V9X_D3D_SELECT_NONE;
        }
        return V9X_D3D_SELECT_R128;
    case V9X_DD_ENGINE_TYPE_ATI_EVERGREEN:
        /* The HD 5450: cedar_hw16.c stamps CAP_D3D only when the
         * mini-VDD mapped the window the CP's ring, shaders and vertices
         * live in; without it the DMA engine serves DirectDraw alone. */
        if ((engine_caps & V9X_DD_ENGINE_CAP_D3D) == 0ul) {
            return V9X_D3D_SELECT_NONE;
        }
        return V9X_D3D_SELECT_EVERGREEN;
    case V9X_DD_ENGINE_TYPE_ATI_VI:
        /* The R7E: carrizo_hw16.c stamps CAP_D3D only when the mini-VDD
         * mapped the window the CP's ring, shaders and vertices share with
         * SDMA's ring; without it SDMA serves DirectDraw alone. */
        if ((engine_caps & V9X_DD_ENGINE_CAP_D3D) == 0ul) {
            return V9X_D3D_SELECT_NONE;
        }
        return V9X_D3D_SELECT_VI;
    default:
        return V9X_D3D_SELECT_NONE;
    }
}
