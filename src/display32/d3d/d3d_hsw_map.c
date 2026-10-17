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

v9x_u32 v9x_d3d_hsw_two_unit_stages(v9x_u32 color0, v9x_u32 alpha0,
                                    int has_alpha0, v9x_u32 color1,
                                    v9x_u32 alpha1, int has_alpha1,
                                    v9x_u8 *stage0, v9x_u8 *stage1)
{
    /* Without texture alpha the sampler returns At = 1, so "keep the
     * fragment's alpha" and "multiply by the texture's" are the same, and
     * REPLACE and MODULATE of alpha coincide only when Af = 1, which is not
     * known: those two stay apart. */
    if (!has_alpha0 && alpha0 == V9X_R3D_ABI_ALPHAOP_MODULATE) {
        alpha0 = V9X_R3D_ABI_ALPHAOP_FRAGMENT;
    }
    if (!has_alpha1 && alpha1 == V9X_R3D_ABI_ALPHAOP_FRAGMENT) {
        alpha1 = V9X_R3D_ABI_ALPHAOP_MODULATE;
    }

    /* Unit 0, against the generator's stage 0 (intel_hsw_eu.h). */
    if (color0 == V9X_R3D_ABI_COLOROP_REPLACE &&
        alpha0 == V9X_R3D_ABI_ALPHAOP_REPLACE) {
        *stage0 = V9X_HSW_PS_TEXTURE;            /* Ct, At */
    } else if (color0 == V9X_R3D_ABI_COLOROP_REPLACE &&
               alpha0 == V9X_R3D_ABI_ALPHAOP_FRAGMENT) {
        *stage0 = V9X_HSW_PS_DECAL;              /* Ct, Af */
    } else if (color0 == V9X_R3D_ABI_COLOROP_MODULATE &&
               alpha0 == V9X_R3D_ABI_ALPHAOP_MODULATE) {
        *stage0 = V9X_HSW_PS_MODULATE;           /* Cf Ct, Af At */
    } else if (color0 == V9X_R3D_ABI_COLOROP_MODULATE &&
               alpha0 == V9X_R3D_ABI_ALPHAOP_REPLACE) {
        *stage0 = V9X_HSW_PS_MODULATE_TEXALPHA;  /* Cf Ct, At */
    } else if (color0 == V9X_R3D_ABI_COLOROP_MODULATE &&
               alpha0 == V9X_R3D_ABI_ALPHAOP_FRAGMENT && !has_alpha0) {
        *stage0 = V9X_HSW_PS_MODULATE;           /* At = 1 */
    } else {
        return V9X_D3D_HSW_REFUSE_COMBINE;
    }

    /* Unit 1: the generator's one second stage multiplies everything. */
    if (color1 != V9X_R3D_ABI_COLOROP_MODULATE ||
        alpha1 != V9X_R3D_ABI_ALPHAOP_MODULATE) {
        return V9X_D3D_HSW_REFUSE_COMBINE;
    }
    *stage1 = V9X_HSW_PS_MODULATE_TEX1;          /* C0 Ct1, A0 At1 */
    return 0ul;
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
