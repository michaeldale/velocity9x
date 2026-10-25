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

#define V9X_D3D_HSW_DISABLE_ALPHA 8u

int v9x_d3d_hsw_target(v9x_u32 r3d_format, v9x_u16 *format,
                       v9x_u8 *write_disable)
{
    if (r3d_format == V9X_R3D_FORMAT_RGB565) {
        *format = V9X_HSW_FMT_B5G6R5_UNORM;
        *write_disable = 0u;
    } else if (r3d_format == V9X_R3D_FORMAT_XRGB1555) {
        /* The A1 form, which renders; the display ignores the top bit. */
        *format = V9X_HSW_FMT_B5G5R5A1_UNORM;
        *write_disable = 0u;
    } else if (r3d_format == V9X_R3D_FORMAT_XRGB8888) {
        *format = V9X_HSW_FMT_B8G8R8A8_UNORM;
        *write_disable = V9X_D3D_HSW_DISABLE_ALPHA;
    } else {
        return 0;
    }
    return 1;
}

int v9x_d3d_hsw_texel_format(v9x_u32 bits, int alpha_pixels, v9x_u32 red,
                             v9x_u32 green, v9x_u32 blue, v9x_u32 alpha,
                             v9x_u16 *format, int *has_alpha,
                             v9x_u32 *bytes)
{
    static const struct {
        v9x_u32 bits;
        int alpha_pixels;
        v9x_u32 red;
        v9x_u32 green;
        v9x_u32 blue;
        v9x_u32 alpha;
        v9x_u16 format;
    } layouts[] = {
        { 16ul, 0, 0x0000f800ul, 0x000007e0ul, 0x0000001ful, 0ul,
          V9X_HSW_FMT_B5G6R5_UNORM },
        { 16ul, 1, 0x00007c00ul, 0x000003e0ul, 0x0000001ful, 0x00008000ul,
          V9X_HSW_FMT_B5G5R5A1_UNORM },
        { 16ul, 1, 0x00000f00ul, 0x000000f0ul, 0x0000000ful, 0x0000f000ul,
          V9X_HSW_FMT_B4G4R4A4_UNORM },
        { 32ul, 1, 0x00ff0000ul, 0x0000ff00ul, 0x000000fful, 0xff000000ul,
          V9X_HSW_FMT_B8G8R8A8_UNORM },
        { 32ul, 0, 0x00ff0000ul, 0x0000ff00ul, 0x000000fful, 0ul,
          V9X_HSW_FMT_B8G8R8X8_UNORM }
    };
    unsigned int index;

    /* A layout without the alpha flag has no alpha mask to compare: DDRAW
     * leaves it whatever the caller passed, so it is read as 0. */
    if (!alpha_pixels) {
        alpha = 0ul;
    }
    for (index = 0u; index < sizeof(layouts) / sizeof(layouts[0]); ++index) {
        if (layouts[index].bits == bits &&
            (layouts[index].alpha_pixels != 0) == (alpha_pixels != 0) &&
            layouts[index].red == red && layouts[index].green == green &&
            layouts[index].blue == blue && layouts[index].alpha == alpha) {
            *format = layouts[index].format;
            *has_alpha = layouts[index].alpha_pixels != 0;
            *bytes = bits / 8ul;
            return 1;
        }
    }
    return 0;
}

int v9x_d3d_hsw_depth_format(v9x_u32 cell_bytes, v9x_u8 *format)
{
    if (cell_bytes == 2ul) {
        *format = V9X_HSW_DEPTH_D16_UNORM;
    } else if (cell_bytes == 4ul) {
        *format = V9X_HSW_DEPTH_D24_UNORM_X8;
    } else {
        return 0;
    }
    return 1;
}

float v9x_d3d_hsw_fill_z(v9x_u32 value, v9x_u32 cell_bytes)
{
    /* Computed in double: a float's 24-bit mantissa holds the 24-bit
     * quotient's numerator but not the division's rounding. */
    if (cell_bytes == 4ul) {
        return (float)((double)(value & 0x00fffffful) / 16777215.0);
    }
    return (float)((double)(value & 0xfffful) / 65535.0);
}

/* A unit's colour combine as the generator's general op; zero for the
 * render interface's BLEND, which needs the environment colour the
 * generator does not take. */
static int v9x_d3d_hsw_color_op(v9x_u32 op, v9x_u8 *colour)
{
    if (op == V9X_R3D_ABI_COLOROP_REPLACE) {
        *colour = V9X_HSW_PS_OP_REPLACE;
    } else if (op == V9X_R3D_ABI_COLOROP_MODULATE) {
        *colour = V9X_HSW_PS_OP_MODULATE;
    } else if (op == V9X_R3D_COLOROP_ADD) {
        *colour = V9X_HSW_PS_OP_ADD;
    } else if (op == V9X_R3D_ABI_COLOROP_DECALALPHA) {
        *colour = V9X_HSW_PS_OP_DECALALPHA;
    } else {
        return 0;
    }
    return 1;
}

/* A unit's alpha combine as the generator's general op. */
static int v9x_d3d_hsw_alpha_op(v9x_u32 op, v9x_u8 *alpha)
{
    if (op == V9X_R3D_ABI_ALPHAOP_FRAGMENT) {
        *alpha = V9X_HSW_PS_AOP_INPUT;
    } else if (op == V9X_R3D_ABI_ALPHAOP_REPLACE) {
        *alpha = V9X_HSW_PS_AOP_REPLACE;
    } else if (op == V9X_R3D_ABI_ALPHAOP_MODULATE) {
        *alpha = V9X_HSW_PS_AOP_MODULATE;
    } else {
        return 0;
    }
    return 1;
}

v9x_u32 v9x_d3d_hsw_two_unit_stages(v9x_u32 color0, v9x_u32 alpha0,
                                    int has_alpha0, v9x_u32 color1,
                                    v9x_u32 alpha1, int has_alpha1,
                                    v9x_u8 *stage0, v9x_u8 *stage1)
{
    v9x_u8 colour;
    v9x_u8 alpha;

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

    /* Unit 0, against the generator's stage 0 (intel_hsw_eu.h): a named
     * code where there is one, so the render interface's kernels and their
     * slots are what they were, else a general combine. */
    if (color0 == V9X_R3D_COLOROP_ADD &&
        alpha0 == V9X_R3D_ABI_ALPHAOP_MODULATE) {
        *stage0 = V9X_HSW_PS_ADD;                /* Cf + Ct, Af At */
    } else if (color0 == V9X_R3D_ABI_COLOROP_REPLACE &&
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
    } else if (v9x_d3d_hsw_color_op(color0, &colour) &&
               v9x_d3d_hsw_alpha_op(alpha0, &alpha)) {
        *stage0 = V9X_HSW_PS_STAGE0_OPS(colour, alpha);
    } else {
        return V9X_D3D_HSW_REFUSE_COMBINE;
    }

    /* Unit 1: multiply everything, the render interface's one second
     * stage, or a general combine with stage 0's result - every pair the
     * Direct3D core makes of two stages (d3d_state.c). */
    if (color1 == V9X_R3D_ABI_COLOROP_MODULATE &&
        alpha1 == V9X_R3D_ABI_ALPHAOP_MODULATE) {
        *stage1 = V9X_HSW_PS_MODULATE_TEX1;      /* C0 Ct1, A0 At1 */
    } else if (v9x_d3d_hsw_color_op(color1, &colour) &&
               v9x_d3d_hsw_alpha_op(alpha1, &alpha)) {
        *stage1 = V9X_HSW_PS_STAGE1_OPS(colour, alpha);
    } else {
        return V9X_D3D_HSW_REFUSE_COMBINE;
    }
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
