/*
 * The Haswell engine's render-interface refusals. See d3d_hsw_map.h.
 */
#include "d3d_hsw_map.h"

v9x_u32 v9x_d3d_hsw_explicit_refusal(const V9X_R3D_DRAW *draw)
{
    /* The sampler reads surfaces in video memory; CPU levels have none, and
     * mapping them as untextured is the wrong picture, not a degraded one. */
    if (draw->texture.levels != 0 || draw->texture1.levels != 0) {
        return V9X_D3D_HSW_REFUSE_CPU_TEXTURE;
    }
    if (draw->explicit_state == 0ul) {
        return 0ul;
    }
    if (draw->write_mask != V9X_D3D_HSW_WRITE_RGB) {
        return V9X_D3D_HSW_REFUSE_MASK;
    }
    /* No SCISSOR_RECT is programmed yet (plan step 2): the batch's only clip
     * is the drawing rectangle, the whole target. */
    if (draw->scissor_left != 0ul || draw->scissor_top != 0ul ||
        draw->scissor_right < draw->target.width ||
        draw->scissor_bottom < draw->target.height) {
        return V9X_D3D_HSW_REFUSE_SCISSOR;
    }
    return 0ul;
}
