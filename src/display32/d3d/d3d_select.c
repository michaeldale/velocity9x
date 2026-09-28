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
     * and ATI_MACH64 has no Direct3D engine yet; both select nothing rather
     * than inheriting another chip's caps.
     */
    switch (engine_type) {
    case V9X_DD_ENGINE_TYPE_S3_VIRGE_DX:
        return V9X_D3D_SELECT_VIRGE;
    case V9X_DD_ENGINE_TYPE_INTEL_GEN3:
        return V9X_D3D_SELECT_GEN3;
    default:
        return V9X_D3D_SELECT_NONE;
    }
}
