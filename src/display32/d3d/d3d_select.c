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
        return V9X_D3D_SELECT_GEN3;
    case V9X_DD_ENGINE_TYPE_ATI_MACH64:
        return V9X_D3D_SELECT_MACH64;
    case V9X_DD_ENGINE_TYPE_ATI_RAGE2:
        return V9X_D3D_SELECT_RAGE2;
    case V9X_DD_ENGINE_TYPE_SIS_6326:
        return V9X_D3D_SELECT_SIS6326;
    case V9X_DD_ENGINE_TYPE_MGA:
        /* One type for both Millenniums; only the 2164W has a texture
         * engine, and only its hook stamps CAP_D3D. */
        if ((engine_caps & V9X_DD_ENGINE_CAP_D3D) == 0ul) {
            return V9X_D3D_SELECT_NONE;
        }
        return V9X_D3D_SELECT_MGA;
    default:
        return V9X_D3D_SELECT_NONE;
    }
}
