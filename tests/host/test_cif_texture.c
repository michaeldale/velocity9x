/*
 * Tests for ATI3DCIF texel formats (src\cif\cif_texfmt.c): the formats
 * the census saw - CI8, RGB1555, RGB332, RGB4444, VQ - and the guide's
 * others that convert, the chroma key, CI4's nibbles, VQ's 2x2 blocks,
 * and the level count (docs\decisions\2026-10-14-3dcif-demo-census.md).
 */
#include <stdio.h>
#include "../../src/cif/cif_texfmt.h"

static unsigned int cif_texture_failures;

#define TCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++cif_texture_failures; \
    } \
} while (0)

/* C3D_COLOR: red in the low byte. Magenta. */
#define KEY_MAGENTA 0x00ff00fful

static void test_bits_and_formats(void)
{
    TCHECK(v9x_cif_texfmt_bits(V9X_C3D_ETF_CI4) == 4ul);
    TCHECK(v9x_cif_texfmt_bits(V9X_C3D_ETF_CI8) == 8ul);
    TCHECK(v9x_cif_texfmt_bits(V9X_C3D_ETF_RGB1555) == 16ul);
    TCHECK(v9x_cif_texfmt_bits(V9X_C3D_ETF_RGB565) == 16ul);
    TCHECK(v9x_cif_texfmt_bits(V9X_C3D_ETF_RGB8888) == 32ul);
    TCHECK(v9x_cif_texfmt_bits(V9X_C3D_ETF_RGB332) == 8ul);
    TCHECK(v9x_cif_texfmt_bits(V9X_C3D_ETF_RGB4444) == 16ul);
    TCHECK(v9x_cif_texfmt_bits(V9X_C3D_ETF_VQ) == 2ul);
    TCHECK(v9x_cif_texfmt_bits(V9X_C3D_ETF_Y8) == 0ul);
    TCHECK(v9x_cif_texfmt_bits(V9X_C3D_ETF_YUV422) == 0ul);

    TCHECK(v9x_cif_texfmt_abi_format(V9X_C3D_ETF_RGB1555, 0ul) == V9X_R3D_ABI_FORMAT_ARGB1555);
    TCHECK(v9x_cif_texfmt_abi_format(V9X_C3D_ETF_RGB1555, 1ul) == V9X_R3D_ABI_FORMAT_ARGB1555);
    TCHECK(v9x_cif_texfmt_abi_format(V9X_C3D_ETF_RGB565, 0ul) == V9X_R3D_ABI_FORMAT_RGB565);
    TCHECK(v9x_cif_texfmt_abi_format(V9X_C3D_ETF_RGB565, 1ul) == V9X_R3D_ABI_FORMAT_ARGB1555);
    TCHECK(v9x_cif_texfmt_abi_format(V9X_C3D_ETF_RGB4444, 0ul) == V9X_R3D_ABI_FORMAT_ARGB4444);
    TCHECK(v9x_cif_texfmt_abi_format(V9X_C3D_ETF_RGB8888, 0ul) == V9X_R3D_ABI_FORMAT_ARGB4444);
    TCHECK(v9x_cif_texfmt_abi_format(V9X_C3D_ETF_RGB332, 0ul) == V9X_R3D_ABI_FORMAT_RGB565);
    TCHECK(v9x_cif_texfmt_abi_format(V9X_C3D_ETF_RGB332, 1ul) == V9X_R3D_ABI_FORMAT_ARGB1555);
    TCHECK(v9x_cif_texfmt_abi_format(V9X_C3D_ETF_CI8, 0ul) == V9X_R3D_ABI_FORMAT_RGB565);
    TCHECK(v9x_cif_texfmt_abi_format(V9X_C3D_ETF_CI8, 1ul) == V9X_R3D_ABI_FORMAT_ARGB1555);
    TCHECK(v9x_cif_texfmt_abi_format(V9X_C3D_ETF_CI4, 0ul) == V9X_R3D_ABI_FORMAT_RGB565);
    TCHECK(v9x_cif_texfmt_abi_format(V9X_C3D_ETF_VQ, 0ul) == 0ul);
    TCHECK(v9x_cif_texfmt_abi_format(V9X_C3D_ETF_Y8, 0ul) == 0ul);
}

static void test_direct_formats(void)
{
    v9x_u16 src[2];
    v9x_u16 dst[2];
    v9x_u32 argb8888[1];

    /* 565 as it stands; keyed, to 1555 with the key's texel cleared. */
    src[0] = 0xf81fu;
    src[1] = 0x07e0u;
    TCHECK(v9x_cif_texfmt_convert_row(V9X_C3D_ETF_RGB565, src, 2ul, 0, 0ul, 0ul, 0ul, dst));
    TCHECK(dst[0] == 0xf81fu && dst[1] == 0x07e0u);
    TCHECK(v9x_cif_texfmt_convert_row(V9X_C3D_ETF_RGB565, src, 2ul, 0, 0ul, 1ul, KEY_MAGENTA, dst));
    TCHECK(dst[0] == 0x7c1fu);
    TCHECK(dst[1] == 0x83e0u);

    /* 1555 in the hardware's sense; keyed, alpha cleared on a match. */
    src[0] = 0xfc1fu;
    src[1] = 0x83e0u;
    TCHECK(v9x_cif_texfmt_convert_row(V9X_C3D_ETF_RGB1555, src, 2ul, 0, 0ul, 0ul, 0ul, dst));
    TCHECK(dst[0] == 0xfc1fu && dst[1] == 0x83e0u);
    TCHECK(v9x_cif_texfmt_convert_row(V9X_C3D_ETF_RGB1555, src, 2ul, 0, 0ul, 1ul, KEY_MAGENTA, dst));
    TCHECK(dst[0] == 0x7c1fu && dst[1] == 0x83e0u);

    /* 4444 as it stands; keyed. */
    src[0] = 0xff0fu;
    src[1] = 0x80f0u;
    TCHECK(v9x_cif_texfmt_convert_row(V9X_C3D_ETF_RGB4444, src, 2ul, 0, 0ul, 1ul, KEY_MAGENTA, dst));
    TCHECK(dst[0] == 0x0f0fu && dst[1] == 0x80f0u);

    /* 8888 to 4444, keeping each channel's top bits. */
    argb8888[0] = 0x80ff8040ul;
    TCHECK(v9x_cif_texfmt_convert_row(V9X_C3D_ETF_RGB8888, argb8888, 1ul, 0, 0ul, 0ul, 0ul, dst));
    TCHECK(dst[0] == 0x8f84u);
}

static void test_rgb332(void)
{
    v9x_u8 src[5];
    v9x_u16 dst[5];

    /* Each field widened by repeating its bits, so full stays full. */
    src[0] = 0xe0u;
    src[1] = 0x1cu;
    src[2] = 0x03u;
    src[3] = 0x00u;
    src[4] = 0xffu;
    TCHECK(v9x_cif_texfmt_convert_row(V9X_C3D_ETF_RGB332, src, 5ul, 0, 0ul, 0ul, 0ul, dst));
    TCHECK(dst[0] == 0xf800u);
    TCHECK(dst[1] == 0x07e0u);
    TCHECK(dst[2] == 0x001fu);
    TCHECK(dst[3] == 0x0000u);
    TCHECK(dst[4] == 0xffffu);
}

static void test_palettes(void)
{
    v9x_u32 palette[256];
    v9x_u8 src[3];
    v9x_u16 dst[3];
    unsigned int i;

    for (i = 0u; i < 256u; ++i) {
        palette[i] = 0x40000000ul;
    }
    /* ATi-3D's second entry: flags 40, blue 2B, green 15, red 1A. */
    palette[1] = 0x402b151aul;
    palette[2] = 0x40ff00fful;

    src[0] = 1u;
    src[1] = 2u;
    src[2] = 0u;
    TCHECK(v9x_cif_texfmt_convert_row(V9X_C3D_ETF_CI8, src, 3ul, palette, 0ul, 0ul, 0ul, dst));
    TCHECK(dst[0] == 0x18a5u);
    TCHECK(dst[1] == 0xf81fu);
    TCHECK(dst[2] == 0x0000u);

    /* Keyed: the palette's 8-bit colour against the key, to 1555. */
    TCHECK(v9x_cif_texfmt_convert_row(V9X_C3D_ETF_CI8, src, 3ul, palette, 0ul, 1ul, KEY_MAGENTA, dst));
    TCHECK(dst[0] == 0x8c45u);
    TCHECK(dst[1] == 0x7c1fu);

    /* CI4: two textures in one byte, one per nibble. */
    src[0] = 0x21u;
    TCHECK(v9x_cif_texfmt_convert_row(V9X_C3D_ETF_CI4, src, 1ul, palette, V9X_C3D_ECI_TMAP_4BIT_LOW, 0ul, 0ul, dst));
    TCHECK(dst[0] == 0x18a5u);
    TCHECK(v9x_cif_texfmt_convert_row(V9X_C3D_ETF_CI4, src, 1ul, palette, V9X_C3D_ECI_TMAP_4BIT_HI, 0ul, 0ul, dst));
    TCHECK(dst[0] == 0xf81fu);

    /* No palette, or a format not converted. */
    TCHECK(!v9x_cif_texfmt_convert_row(V9X_C3D_ETF_CI8, src, 1ul, 0, 0ul, 0ul, 0ul, dst));
    TCHECK(!v9x_cif_texfmt_convert_row(V9X_C3D_ETF_Y8, src, 1ul, palette, 0ul, 0ul, 0ul, dst));
    TCHECK(!v9x_cif_texfmt_convert_row(V9X_C3D_ETF_VQ, src, 1ul, palette, 0ul, 0ul, 0ul, dst));
}

static void test_vq(void)
{
    v9x_u16 codebook[256u * 4u];
    v9x_u8 indices[2];
    v9x_u16 dst[2][4];
    unsigned int i;

    for (i = 0u; i < 256u * 4u; ++i) {
        codebook[i] = 0u;
    }
    codebook[4] = 0x1111u;
    codebook[5] = 0x2222u;
    codebook[6] = 0x3333u;
    codebook[7] = 0x4444u;
    codebook[8] = 0x5555u;
    codebook[9] = 0x6666u;
    codebook[10] = 0x7777u;
    codebook[11] = 0x8888u;

    /* A 4x2 texture is two blocks: entries 1 and 2. */
    indices[0] = 1u;
    indices[1] = 2u;
    TCHECK(v9x_cif_texfmt_vq_decode(indices, 2ul, codebook, 4ul, 2ul, &dst[0][0], 8ul));
    TCHECK(dst[0][0] == 0x1111u && dst[0][1] == 0x2222u);
    TCHECK(dst[0][2] == 0x5555u && dst[0][3] == 0x6666u);
    TCHECK(dst[1][0] == 0x3333u && dst[1][1] == 0x4444u);
    TCHECK(dst[1][2] == 0x7777u && dst[1][3] == 0x8888u);

    TCHECK(!v9x_cif_texfmt_vq_decode(indices, 2ul, codebook, 3ul, 2ul, &dst[0][0], 8ul));
    TCHECK(!v9x_cif_texfmt_vq_decode(indices, 2ul, codebook, 4ul, 1ul, &dst[0][0], 8ul));
}

static void test_levels(void)
{
    TCHECK(v9x_cif_texfmt_levels(8ul, 8ul, 0ul) == 1ul);
    TCHECK(v9x_cif_texfmt_levels(8ul, 8ul, 1ul) == 9ul);
    /* Pixie's 512x512 has ten. */
    TCHECK(v9x_cif_texfmt_levels(9ul, 9ul, 1ul) == 10ul);
    /* The Knight VQ demo's 1024x256 would have eleven. */
    TCHECK(v9x_cif_texfmt_levels(10ul, 8ul, 1ul) == V9X_R3D_ABI_LEVELS_MAX);
    TCHECK(v9x_cif_texfmt_levels(3ul, 7ul, 1ul) == 8ul);
    TCHECK(v9x_cif_texfmt_levels(0ul, 0ul, 1ul) == 1ul);
}

unsigned int v9x_run_cif_texture_tests(void)
{
    cif_texture_failures = 0u;
    test_bits_and_formats();
    test_direct_formats();
    test_rgb332();
    test_palettes();
    test_vq();
    test_levels();
    return cif_texture_failures;
}
