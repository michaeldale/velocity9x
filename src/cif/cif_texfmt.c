/*
 * ATI3DCIF texel formats to the render interface's 16-bit formats
 * (docs\plans\ati-3dcif-wrapper.md, Phase 2). See cif_texfmt.h.
 */
#include "cif_texfmt.h"

/* Fields of the 16-bit layouts, as the interface (and the chip) read them. */
#define V9X_CIF_1555_ALPHA 0x8000u
#define V9X_CIF_4444_ALPHA 0xf000u

/* A VQ block is 2x2 texels; a codebook entry is its four. */
#define V9X_CIF_VQ_BLOCK   2ul
#define V9X_CIF_VQ_ENTRY   4ul

v9x_u32 v9x_cif_texfmt_bits(v9x_u32 format)
{
    switch (format) {
    /* A CI4 texel is a byte with its index in one nibble (guide,
     * "Palettized Textures": "byte aligned"), which is how two CI4
     * textures share one CI8's storage. */
    case V9X_C3D_ETF_CI4:     return 8ul;
    case V9X_C3D_ETF_CI8:     return 8ul;
    case V9X_C3D_ETF_RGB332:  return 8ul;
    case V9X_C3D_ETF_RGB1555: return 16ul;
    case V9X_C3D_ETF_RGB565:  return 16ul;
    case V9X_C3D_ETF_RGB4444: return 16ul;
    case V9X_C3D_ETF_RGB8888: return 32ul;
    case V9X_C3D_ETF_VQ:      return 2ul;
    default:                  return 0ul;
    }
}

/*
 * 1555 and 4444 keep their own alpha; 8888 goes to 4444 for its alpha.
 * The formats without alpha - 565, 332 and the palette formats, whose
 * entries' high byte is a load flag, not alpha - become 565, or 1555 when
 * the key needs an alpha to clear. VQ's is its codebook's, which is RGB565:
 * the guide does not say, but in every .vqt file of SDK 4.1 the top bit
 * varies (it is red's), and 2x2 blocks are two to three times smoother
 * read as 565 than as 555
 * (docs\decisions\2026-10-14-3dcif-vq-textures.md).
 */
v9x_u32 v9x_cif_texfmt_abi_format(v9x_u32 format, v9x_u32 keyed)
{
    switch (format) {
    case V9X_C3D_ETF_RGB1555:
        return V9X_R3D_ABI_FORMAT_ARGB1555;
    case V9X_C3D_ETF_RGB4444:
    case V9X_C3D_ETF_RGB8888:
        return V9X_R3D_ABI_FORMAT_ARGB4444;
    case V9X_C3D_ETF_RGB565:
    case V9X_C3D_ETF_RGB332:
    case V9X_C3D_ETF_CI8:
    case V9X_C3D_ETF_CI4:
    case V9X_C3D_ETF_VQ:
        return keyed ? V9X_R3D_ABI_FORMAT_ARGB1555 : V9X_R3D_ABI_FORMAT_RGB565;
    default:
        return 0ul;
    }
}

/* 8-bit channels to 565, and to opaque 1555. */
static v9x_u16 v9x_cif_rgb_to_565(v9x_u32 r, v9x_u32 g, v9x_u32 b)
{
    return (v9x_u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static v9x_u16 v9x_cif_rgb_to_1555(v9x_u32 r, v9x_u32 g, v9x_u32 b)
{
    return (v9x_u16)(V9X_CIF_1555_ALPHA | ((r >> 3) << 10) |
                     ((g >> 3) << 5) | (b >> 3));
}

/* 565 to 1555 with alpha set: green loses its low bit. */
static v9x_u16 v9x_cif_565_to_1555(v9x_u16 texel)
{
    return (v9x_u16)(V9X_CIF_1555_ALPHA | ((texel >> 1) & 0x7fe0u) |
                     (texel & 0x001fu));
}

/* Whether a texel's fields equal the key's top bits, field by field:
 * `rbits`, `gbits`, `bbits` wide at the given shifts. */
static int v9x_cif_key_match(v9x_u32 texel, v9x_u32 key, unsigned int rshift,
                             unsigned int rbits, unsigned int gshift,
                             unsigned int gbits, unsigned int bshift,
                             unsigned int bbits)
{
    v9x_u32 r = (texel >> rshift) & ((1ul << rbits) - 1ul);
    v9x_u32 g = (texel >> gshift) & ((1ul << gbits) - 1ul);
    v9x_u32 b = (texel >> bshift) & ((1ul << bbits) - 1ul);

    return r == (V9X_C3D_COLOR_RED(key) >> (8u - rbits)) &&
           g == (V9X_C3D_COLOR_GREEN(key) >> (8u - gbits)) &&
           b == (V9X_C3D_COLOR_BLUE(key) >> (8u - bbits));
}

/* A palette entry (red in the low byte) as 565, or keyed 1555; the entry's
 * 8-bit colour is compared whole with the key's. */
static v9x_u16 v9x_cif_palette_texel(v9x_u32 entry, v9x_u32 keyed,
                                     v9x_u32 key)
{
    v9x_u32 r = V9X_C3D_COLOR_RED(entry);
    v9x_u32 g = V9X_C3D_COLOR_GREEN(entry);
    v9x_u32 b = V9X_C3D_COLOR_BLUE(entry);

    if (!keyed) {
        return v9x_cif_rgb_to_565(r, g, b);
    }
    if ((entry & 0x00fffffful) == (key & 0x00fffffful)) {
        return (v9x_u16)(v9x_cif_rgb_to_1555(r, g, b) & ~V9X_CIF_1555_ALPHA);
    }
    return v9x_cif_rgb_to_1555(r, g, b);
}

/* 332 widened to 565 by repeating each field's bits, so 0 stays 0 and a
 * full field stays full. */
static v9x_u16 v9x_cif_332_to_565(v9x_u32 texel)
{
    v9x_u32 r3 = (texel >> 5) & 7ul;
    v9x_u32 g3 = (texel >> 2) & 7ul;
    v9x_u32 b2 = texel & 3ul;
    v9x_u32 r5 = (r3 << 2) | (r3 >> 1);
    v9x_u32 g6 = (g3 << 3) | g3;
    v9x_u32 b5 = (b2 << 3) | (b2 << 1) | (b2 >> 1);

    return (v9x_u16)((r5 << 11) | (g6 << 5) | b5);
}

v9x_u16 v9x_cif_texfmt_convert_row(v9x_u32 format, const void *src,
                                   v9x_u32 count, const v9x_u32 *palette,
                                   v9x_u32 nibble, v9x_u32 keyed,
                                   v9x_u32 key, v9x_u16 *dst)
{
    const v9x_u8 *bytes = (const v9x_u8 *)src;
    const v9x_u16 *words = (const v9x_u16 *)src;
    const v9x_u32 *dwords = (const v9x_u32 *)src;
    v9x_u32 index;
    v9x_u32 texel;
    v9x_u16 out;

    if ((format == V9X_C3D_ETF_CI8 || format == V9X_C3D_ETF_CI4) &&
        palette == 0) {
        return V9X_FALSE;
    }
    for (index = 0ul; index < count; ++index) {
        switch (format) {
        case V9X_C3D_ETF_RGB565:
            out = words[index];
            if (keyed) {
                out = v9x_cif_565_to_1555(out);
                if (v9x_cif_key_match(words[index], key, 11u, 5u, 5u, 6u,
                                      0u, 5u)) {
                    out = (v9x_u16)(out & ~V9X_CIF_1555_ALPHA);
                }
            }
            break;
        case V9X_C3D_ETF_RGB1555:
            out = words[index];
            if (keyed && v9x_cif_key_match(out, key, 10u, 5u, 5u, 5u, 0u, 5u)) {
                out = (v9x_u16)(out & ~V9X_CIF_1555_ALPHA);
            }
            break;
        case V9X_C3D_ETF_RGB4444:
            out = words[index];
            if (keyed && v9x_cif_key_match(out, key, 8u, 4u, 4u, 4u, 0u, 4u)) {
                out = (v9x_u16)(out & ~V9X_CIF_4444_ALPHA);
            }
            break;
        case V9X_C3D_ETF_RGB8888:
            texel = dwords[index];
            out = (v9x_u16)(((texel >> 16) & 0xf000ul) |
                            ((texel >> 12) & 0x0f00ul) |
                            ((texel >> 8) & 0x00f0ul) |
                            ((texel >> 4) & 0x000ful));
            if (keyed && v9x_cif_key_match(texel, key, 16u, 8u, 8u, 8u,
                                           0u, 8u)) {
                out = (v9x_u16)(out & ~V9X_CIF_4444_ALPHA);
            }
            break;
        case V9X_C3D_ETF_RGB332:
            out = v9x_cif_332_to_565(bytes[index]);
            if (keyed) {
                if (v9x_cif_key_match(bytes[index], key, 5u, 3u, 2u, 3u,
                                      0u, 2u)) {
                    out = (v9x_u16)(v9x_cif_565_to_1555(out) &
                                    ~V9X_CIF_1555_ALPHA);
                } else {
                    out = v9x_cif_565_to_1555(out);
                }
            }
            break;
        case V9X_C3D_ETF_CI8:
            out = v9x_cif_palette_texel(palette[bytes[index]], keyed, key);
            break;
        case V9X_C3D_ETF_CI4:
            texel = nibble == V9X_C3D_ECI_TMAP_4BIT_HI
                        ? (v9x_u32)(bytes[index] >> 4)
                        : (v9x_u32)(bytes[index] & 0x0fu);
            out = v9x_cif_palette_texel(palette[texel], keyed, key);
            break;
        default:
            return V9X_FALSE;
        }
        dst[index] = out;
    }
    return V9X_TRUE;
}

v9x_u16 v9x_cif_texfmt_vq_decode(const v9x_u8 *indices, v9x_u32 index_pitch,
                                 const v9x_u16 *codebook, v9x_u32 width,
                                 v9x_u32 height, v9x_u16 *dst,
                                 v9x_u32 dst_pitch)
{
    v9x_u32 block_x;
    v9x_u32 block_y;
    const v9x_u16 *entry;
    v9x_u16 *upper;
    v9x_u16 *lower;

    if ((width % V9X_CIF_VQ_BLOCK) != 0ul || (height % V9X_CIF_VQ_BLOCK) != 0ul ||
        width == 0ul || height == 0ul) {
        return V9X_FALSE;
    }
    for (block_y = 0ul; block_y < height / V9X_CIF_VQ_BLOCK; ++block_y) {
        upper = (v9x_u16 *)((v9x_u8 *)dst +
                            block_y * V9X_CIF_VQ_BLOCK * dst_pitch);
        lower = (v9x_u16 *)((v9x_u8 *)upper + dst_pitch);
        for (block_x = 0ul; block_x < width / V9X_CIF_VQ_BLOCK; ++block_x) {
            entry = codebook +
                    (v9x_u32)indices[block_y * index_pitch + block_x] *
                    V9X_CIF_VQ_ENTRY;
            upper[block_x * 2ul] = entry[0];
            upper[block_x * 2ul + 1ul] = entry[1];
            lower[block_x * 2ul] = entry[2];
            lower[block_x * 2ul + 1ul] = entry[3];
        }
    }
    return V9X_TRUE;
}

v9x_u16 v9x_cif_texfmt_to_565(v9x_u32 abi_format, v9x_u16 *texels,
                              v9x_u32 count)
{
    v9x_u32 index;
    v9x_u32 texel;
    v9x_u32 r;
    v9x_u32 g;
    v9x_u32 b;

    if (abi_format == V9X_R3D_ABI_FORMAT_RGB565) {
        return V9X_TRUE;
    }
    if (abi_format != V9X_R3D_ABI_FORMAT_ARGB1555 &&
        abi_format != V9X_R3D_ABI_FORMAT_ARGB4444) {
        return V9X_FALSE;
    }
    /* Each field widened by repeating its top bits, so full stays full. */
    for (index = 0ul; index < count; ++index) {
        texel = texels[index];
        if (abi_format == V9X_R3D_ABI_FORMAT_ARGB1555) {
            r = (texel >> 10) & 0x1ful;
            g = (texel >> 5) & 0x1ful;
            b = texel & 0x1ful;
            g = (g << 1) | (g >> 4);
        } else {
            r = (texel >> 8) & 0x0ful;
            g = (texel >> 4) & 0x0ful;
            b = texel & 0x0ful;
            r = (r << 1) | (r >> 3);
            g = (g << 2) | (g >> 2);
            b = (b << 1) | (b >> 3);
        }
        texels[index] = (v9x_u16)((r << 11) | (g << 5) | b);
    }
    return V9X_TRUE;
}

v9x_u32 v9x_cif_texfmt_fit_level(v9x_u32 xlg2, v9x_u32 ylg2,
                                 v9x_u32 size_max)
{
    v9x_u32 largest = xlg2 > ylg2 ? xlg2 : ylg2;
    v9x_u32 limit = 0ul;

    if (size_max == 0ul) {
        return 0ul;
    }
    while ((2ul << limit) <= size_max) {
        ++limit;
    }
    return largest > limit ? largest - limit : 0ul;
}

void v9x_cif_texfmt_decimate(v9x_u16 *texels, v9x_u32 *width,
                             v9x_u32 *height, v9x_u32 shift)
{
    v9x_u32 old_width = *width;
    v9x_u32 new_width = old_width >> shift;
    v9x_u32 new_height = *height >> shift;
    v9x_u32 step_x;
    v9x_u32 step_y;
    v9x_u32 x;
    v9x_u32 y;

    if (new_width == 0ul) {
        new_width = 1ul;
    }
    if (new_height == 0ul) {
        new_height = 1ul;
    }
    step_x = old_width / new_width;
    step_y = *height / new_height;
    /* In place, front to back: each destination texel is at or before
     * its source, so nothing is read after it is overwritten. */
    for (y = 0ul; y < new_height; ++y) {
        for (x = 0ul; x < new_width; ++x) {
            texels[y * new_width + x] =
                texels[(y * step_y) * old_width + x * step_x];
        }
    }
    *width = new_width;
    *height = new_height;
}

v9x_u32 v9x_cif_texfmt_levels(v9x_u32 xlg2, v9x_u32 ylg2, v9x_u32 mipmapped)
{
    v9x_u32 levels = (xlg2 > ylg2 ? xlg2 : ylg2) + 1ul;

    if (!mipmapped) {
        return 1ul;
    }
    return levels > V9X_R3D_ABI_LEVELS_MAX ? V9X_R3D_ABI_LEVELS_MAX : levels;
}
