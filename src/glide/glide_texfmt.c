/*
 * Glide texel formats to the render interface's 16-bit formats
 * (docs\plans\glide-2x-wrapper.md, Phase 1). See glide_texfmt.h for the
 * formats and the chroma-key approximation.
 *
 * A key is compared at the texel's own precision: reduced to 565, 555 or
 * 444 for the 16-bit formats, exact for a palette entry. The Voodoo
 * expands a texel to 8 bits a channel by replicating its high bits, so a
 * key that is itself an expanded texel matches exactly that texel either
 * way.
 */
#include "glide_texfmt.h"

v9x_u16 v9x_glide_texfmt_supported(v9x_u32 format)
{
    return (format == V9X_GLIDE_TEXFMT_RGB_565 ||
            format == V9X_GLIDE_TEXFMT_ARGB_1555 ||
            format == V9X_GLIDE_TEXFMT_ARGB_4444 ||
            format == V9X_GLIDE_TEXFMT_P_8) ? V9X_TRUE : V9X_FALSE;
}

v9x_u16 v9x_glide_texfmt_key_alpha_only(v9x_u32 format)
{
    return (format == V9X_GLIDE_TEXFMT_RGB_565 ||
            format == V9X_GLIDE_TEXFMT_P_8) ? V9X_TRUE : V9X_FALSE;
}

static v9x_u32 v9x_glide_key_565(v9x_u32 key_rgb)
{
    return (((key_rgb >> 19) & 0x1Ful) << 11) |
           (((key_rgb >> 10) & 0x3Ful) << 5) |
           ((key_rgb >> 3) & 0x1Ful);
}

static v9x_u32 v9x_glide_key_555(v9x_u32 key_rgb)
{
    return (((key_rgb >> 19) & 0x1Ful) << 10) |
           (((key_rgb >> 11) & 0x1Ful) << 5) |
           ((key_rgb >> 3) & 0x1Ful);
}

static v9x_u32 v9x_glide_key_444(v9x_u32 key_rgb)
{
    return (((key_rgb >> 20) & 0x0Ful) << 8) |
           (((key_rgb >> 12) & 0x0Ful) << 4) |
           ((key_rgb >> 4) & 0x0Ful);
}

static void v9x_glide_convert_565(const v9x_u16 *src, v9x_u32 count,
                                  v9x_u32 key_enable, v9x_u32 key_rgb,
                                  v9x_u16 *dst, v9x_u32 *abi_format)
{
    v9x_u32 key = v9x_glide_key_565(key_rgb);
    v9x_u32 texel;
    v9x_u32 i;

    if (!key_enable) {
        for (i = 0ul; i < count; ++i) {
            dst[i] = src[i];
        }
        *abi_format = V9X_R3D_ABI_FORMAT_RGB565;
        return;
    }
    for (i = 0ul; i < count; ++i) {
        texel = src[i];
        dst[i] = (v9x_u16)((texel == key ? 0ul : 0x8000ul) |
                           ((texel >> 1) & 0x7FE0ul) | (texel & 0x1Ful));
    }
    *abi_format = V9X_R3D_ABI_FORMAT_ARGB1555;
}

static void v9x_glide_convert_1555(const v9x_u16 *src, v9x_u32 count,
                                   v9x_u32 key_enable, v9x_u32 key_rgb,
                                   v9x_u16 *dst)
{
    v9x_u32 key = v9x_glide_key_555(key_rgb);
    v9x_u32 i;

    for (i = 0ul; i < count; ++i) {
        dst[i] = src[i];
        if (key_enable && ((v9x_u32)src[i] & 0x7FFFul) == key) {
            dst[i] = (v9x_u16)(src[i] & 0x7FFFu);
        }
    }
}

static void v9x_glide_convert_4444(const v9x_u16 *src, v9x_u32 count,
                                   v9x_u32 key_enable, v9x_u32 key_rgb,
                                   v9x_u16 *dst)
{
    v9x_u32 key = v9x_glide_key_444(key_rgb);
    v9x_u32 i;

    for (i = 0ul; i < count; ++i) {
        dst[i] = src[i];
        if (key_enable && ((v9x_u32)src[i] & 0x0FFFul) == key) {
            dst[i] = (v9x_u16)(src[i] & 0x0FFFu);
        }
    }
}

/* P_8's palette entries are opaque colours: grTexDownloadTable gives them
 * as 0xAARRGGBB with the alpha byte FF in every census palette. */
static void v9x_glide_convert_p8(const v9x_u8 *src, v9x_u32 count,
                                 const v9x_u32 *palette, v9x_u32 key_enable,
                                 v9x_u32 key_rgb, v9x_u16 *dst,
                                 v9x_u32 *abi_format)
{
    v9x_u32 entry;
    v9x_u32 i;

    for (i = 0ul; i < count; ++i) {
        entry = palette[src[i]];
        if (key_enable) {
            dst[i] = (v9x_u16)(((entry & 0x00FFFFFFul) == (key_rgb & 0x00FFFFFFul) ?
                                0ul : 0x8000ul) |
                               (((entry >> 19) & 0x1Ful) << 10) |
                               (((entry >> 11) & 0x1Ful) << 5) |
                               ((entry >> 3) & 0x1Ful));
        } else {
            dst[i] = (v9x_u16)((((entry >> 19) & 0x1Ful) << 11) |
                               (((entry >> 10) & 0x3Ful) << 5) |
                               ((entry >> 3) & 0x1Ful));
        }
    }
    *abi_format = key_enable ? V9X_R3D_ABI_FORMAT_ARGB1555 :
                               V9X_R3D_ABI_FORMAT_RGB565;
}

v9x_u16 v9x_glide_texfmt_convert(v9x_u32 format, const void *src,
                                 v9x_u32 count, const v9x_u32 *palette,
                                 v9x_u32 key_enable, v9x_u32 key_rgb,
                                 v9x_u16 *dst, v9x_u32 *abi_format)
{
    if (format == V9X_GLIDE_TEXFMT_RGB_565) {
        v9x_glide_convert_565((const v9x_u16 *)src, count, key_enable, key_rgb,
                              dst, abi_format);
        return V9X_TRUE;
    }
    if (format == V9X_GLIDE_TEXFMT_ARGB_1555) {
        v9x_glide_convert_1555((const v9x_u16 *)src, count, key_enable, key_rgb,
                               dst);
        *abi_format = V9X_R3D_ABI_FORMAT_ARGB1555;
        return V9X_TRUE;
    }
    if (format == V9X_GLIDE_TEXFMT_ARGB_4444) {
        v9x_glide_convert_4444((const v9x_u16 *)src, count, key_enable, key_rgb,
                               dst);
        *abi_format = V9X_R3D_ABI_FORMAT_ARGB4444;
        return V9X_TRUE;
    }
    if (format == V9X_GLIDE_TEXFMT_P_8 && palette != 0) {
        v9x_glide_convert_p8((const v9x_u8 *)src, count, palette, key_enable,
                             key_rgb, dst, abi_format);
        return V9X_TRUE;
    }
    return V9X_FALSE;
}
