/*
 * DDI 8's cap words and format list from the DDI 7 description;
 * d3d_caps8.h says what each answers. Pure, held to
 * tests\host\test_d3d_caps8.c.
 */
#include "d3d_caps8.h"

/* d3dcaps.h: the DX5 filter words, and the DX6 bits DX8 keeps. */
#define V9X_CAPS8_NEAREST          0x00000001ul
#define V9X_CAPS8_LINEAR           0x00000002ul
#define V9X_CAPS8_MIPNEAREST       0x00000004ul
#define V9X_CAPS8_MIPLINEAR        0x00000008ul
#define V9X_CAPS8_LINEARMIPNEAREST 0x00000010ul
#define V9X_CAPS8_LINEARMIPLINEAR  0x00000020ul
#define V9X_CAPS8_MINFPOINT        0x00000100ul
#define V9X_CAPS8_MINFLINEAR       0x00000200ul
#define V9X_CAPS8_MIPFPOINT        0x00010000ul
#define V9X_CAPS8_MIPFLINEAR       0x00020000ul
#define V9X_CAPS8_MAGFPOINT        0x01000000ul
#define V9X_CAPS8_MAGFLINEAR       0x02000000ul
/* Every DX6 filter bit: MINF 8-10, MIPF 16-17, MAGF 24-28; and the six
 * DX5 words. */
#define V9X_CAPS8_FILTER_DX6       0x1f030700ul
#define V9X_CAPS8_FILTER_DX5       0x0000003ful
/* D3DPTEXTURECAPS_MIPMAP, d3d8caps.h. */
#define V9X_CAPS8_TEXTURE_MIPMAP   0x00004000ul

/* DX7 raster bits DX8 dropped: ROP2, XOR, SUBPIXEL, SUBPIXELX, STIPPLE,
 * ANTIALIASSORTDEPENDENT, ANTIALIASSORTINDEPENDENT,
 * TRANSLUCENTSORTINDEPENDENT. */
#define V9X_CAPS8_RASTER_DROPPED   0x00080e66ul
/* DX7 misc bits DX8 dropped: MASKPLANES, CONFORMANT. */
#define V9X_CAPS8_MISC_DROPPED     0x00000009ul

/* D3DLINECAPS_TEXTURE, ZTEST, BLEND, ALPHACMP and FOG. */
#define V9X_CAPS8_LINECAPS_ALL     0x0000001ful

/* DDPF_ALPHAPIXELS and DDPF_RGB. */
#define V9X_CAPS8_DDPF_ALPHA       0x00000001ul
#define V9X_CAPS8_DDPF_RGB         0x00000040ul

v9x_u32 v9x_d3d_caps8_filter(v9x_u32 dx7)
{
    v9x_u32 caps = dx7 & (V9X_CAPS8_FILTER_DX6 | V9X_CAPS8_FILTER_DX5);

    if ((dx7 & V9X_CAPS8_NEAREST) != 0ul) {
        caps |= V9X_CAPS8_MINFPOINT | V9X_CAPS8_MAGFPOINT;
    }
    if ((dx7 & V9X_CAPS8_LINEAR) != 0ul) {
        caps |= V9X_CAPS8_MINFLINEAR | V9X_CAPS8_MAGFLINEAR;
    }
    if ((dx7 & (V9X_CAPS8_MIPNEAREST | V9X_CAPS8_LINEARMIPNEAREST)) != 0ul) {
        caps |= V9X_CAPS8_MIPFPOINT;
    }
    if ((dx7 & (V9X_CAPS8_MIPLINEAR | V9X_CAPS8_LINEARMIPLINEAR)) != 0ul) {
        caps |= V9X_CAPS8_MIPFLINEAR;
    }
    return caps;
}

v9x_u32 v9x_d3d_caps8_texture(v9x_u32 dx7, v9x_u32 filter_dx7)
{
    if ((v9x_d3d_caps8_filter(filter_dx7) &
         (V9X_CAPS8_MIPFPOINT | V9X_CAPS8_MIPFLINEAR)) != 0ul) {
        return dx7 | V9X_CAPS8_TEXTURE_MIPMAP;
    }
    return dx7;
}

v9x_u32 v9x_d3d_caps8_raster(v9x_u32 dx7)
{
    return dx7 & ~V9X_CAPS8_RASTER_DROPPED;
}

v9x_u32 v9x_d3d_caps8_misc(v9x_u32 dx7)
{
    return dx7 & ~V9X_CAPS8_MISC_DROPPED;
}

v9x_u32 v9x_d3d_caps8_lines(int drawn)
{
    return drawn ? V9X_CAPS8_LINECAPS_ALL : 0ul;
}

v9x_u32 v9x_d3d_caps8_format_of(v9x_u32 flags, v9x_u32 bits, v9x_u32 red,
                                v9x_u32 green, v9x_u32 blue, v9x_u32 alpha)
{
    int alpha_flag = (flags & V9X_CAPS8_DDPF_ALPHA) != 0ul;

    if ((flags & V9X_CAPS8_DDPF_RGB) == 0ul || alpha_flag != (alpha != 0ul)) {
        return 0ul;
    }
    if (bits == 16ul && red == 0xf800ul && green == 0x07e0ul &&
        blue == 0x001ful && alpha == 0ul) {
        return V9X_D3DFMT_R5G6B5;
    }
    if (bits == 16ul && red == 0x7c00ul && green == 0x03e0ul &&
        blue == 0x001ful) {
        return alpha == 0x8000ul ? V9X_D3DFMT_A1R5G5B5
                                 : (alpha == 0ul ? V9X_D3DFMT_X1R5G5B5 : 0ul);
    }
    if (bits == 16ul && red == 0x0f00ul && green == 0x00f0ul &&
        blue == 0x000ful && alpha == 0xf000ul) {
        return V9X_D3DFMT_A4R4G4B4;
    }
    if (bits == 32ul && red == 0x00ff0000ul && green == 0x0000ff00ul &&
        blue == 0x000000fful) {
        return alpha == 0xff000000ul ? V9X_D3DFMT_A8R8G8B8
                                     : (alpha == 0ul ? V9X_D3DFMT_X8R8G8B8
                                                     : 0ul);
    }
    return 0ul;
}

/* The most texture formats any engine publishes is five (the shared
 * block's slots); the list is built whole here, then copied. */
#define V9X_CAPS8_TEXTURES_MAX 16ul
#define V9X_CAPS8_FORMATS_MAX  (V9X_CAPS8_TEXTURES_MAX + 4ul)

/* Add operations to format's entry, making it if it is new. */
static v9x_u32 v9x_d3d_caps8_add(V9X_D3D_FORMAT8 *list, v9x_u32 count,
                                 v9x_u32 format, v9x_u32 operations)
{
    v9x_u32 i;

    for (i = 0ul; i < count; ++i) {
        if (list[i].format == format) {
            list[i].operations |= operations;
            return count;
        }
    }
    list[count].format = format;
    list[count].operations = operations;
    return count + 1ul;
}

v9x_u32 v9x_d3d_caps8_formats(const v9x_u32 *textures, v9x_u32 texture_count,
                              v9x_u32 renders, v9x_u32 depth16_bits,
                              v9x_u32 depth32_bits, V9X_D3D_FORMAT8 *out,
                              v9x_u32 room)
{
    V9X_D3D_FORMAT8 list[V9X_CAPS8_FORMATS_MAX];
    V9X_D3D_FORMAT8 *all = list;
    v9x_u32 count = 0ul;
    v9x_u32 i;
    v9x_u32 display = V9X_D3DFORMAT_OP_DISPLAYMODE |
                      V9X_D3DFORMAT_OP_3DACCELERATION |
                      V9X_D3DFORMAT_OP_SAME_FORMAT_RENDERTARGET;
    v9x_u32 depth = V9X_D3DFORMAT_OP_ZSTENCIL |
                    V9X_D3DFORMAT_OP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH;

    if (texture_count > V9X_CAPS8_TEXTURES_MAX) {
        texture_count = V9X_CAPS8_TEXTURES_MAX;
    }
    for (i = 0ul; i < texture_count; ++i) {
        if (textures[i] != 0ul) {
            count = v9x_d3d_caps8_add(all, count, textures[i],
                                      V9X_D3DFORMAT_OP_TEXTURE);
        }
    }
    if ((renders & V9X_D3D_RENDERS_16) != 0ul) {
        count = v9x_d3d_caps8_add(all, count, V9X_D3DFMT_R5G6B5, display);
    }
    if ((renders & V9X_D3D_RENDERS_32) != 0ul) {
        count = v9x_d3d_caps8_add(all, count, V9X_D3DFMT_X8R8G8B8, display);
    }
    if (depth16_bits == 16ul) {
        count = v9x_d3d_caps8_add(all, count, V9X_D3DFMT_D16, depth);
    }
    if (depth32_bits == 24ul) {
        count = v9x_d3d_caps8_add(all, count, V9X_D3DFMT_D24X8, depth);
    }
    for (i = 0ul; i < count && i < room; ++i) {
        out[i] = all[i];
    }
    return count;
}
