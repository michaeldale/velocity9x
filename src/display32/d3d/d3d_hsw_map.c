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
    return 0ul;
}

/* The ABI's mask bits (r3d_abi.h V9X_R3D_ABI_WRITE_*). */
#define V9X_D3D_HSW_MASK_RED   1ul
#define V9X_D3D_HSW_MASK_GREEN 2ul
#define V9X_D3D_HSW_MASK_BLUE  4ul
/* BLEND_STATE DW1 27:24 (Vol 2d) as the nibble v9x_hsw_blend takes:
 * A 3, R 2, G 1, B 0. */
#define V9X_D3D_HSW_DISABLE_RED   4u
#define V9X_D3D_HSW_DISABLE_GREEN 2u
#define V9X_D3D_HSW_DISABLE_BLUE  1u

v9x_u8 v9x_d3d_hsw_write_disable(v9x_u32 write_mask)
{
    v9x_u8 disable = 0u;

    if ((write_mask & V9X_D3D_HSW_MASK_RED) == 0ul) {
        disable |= V9X_D3D_HSW_DISABLE_RED;
    }
    if ((write_mask & V9X_D3D_HSW_MASK_GREEN) == 0ul) {
        disable |= V9X_D3D_HSW_DISABLE_GREEN;
    }
    if ((write_mask & V9X_D3D_HSW_MASK_BLUE) == 0ul) {
        disable |= V9X_D3D_HSW_DISABLE_BLUE;
    }
    return disable;
}

int v9x_d3d_hsw_scissor(const V9X_R3D_DRAW *draw, v9x_u32 rect[4])
{
    v9x_u32 left = 0ul;
    v9x_u32 top = 0ul;
    v9x_u32 right = draw->target.width;
    v9x_u32 bottom = draw->target.height;

    /* Only an explicit draw states one; its right and bottom are
     * exclusive, the drawing rectangle's inclusive. */
    if (draw->explicit_state != 0ul) {
        left = draw->scissor_left;
        top = draw->scissor_top;
        if (draw->scissor_right < right) {
            right = draw->scissor_right;
        }
        if (draw->scissor_bottom < bottom) {
            bottom = draw->scissor_bottom;
        }
    }
    if (left >= right || top >= bottom) {
        return 0;
    }
    rect[0] = left;
    rect[1] = top;
    rect[2] = right - 1ul;
    rect[3] = bottom - 1ul;
    return 1;
}
