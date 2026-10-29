/*
 * D3DCAPS8 and the DX8 format list from the DDI 7 description
 * (src\display32\d3d\d3d_caps8.h). Bit values from d3dcaps.h,
 * d3d8caps.h and the DDK's ddrawi.h, as tabled in
 * docs\specifications\ddi8-on-win9x.md sections 3 and 4.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/display32/d3d/d3d_caps8.h"

static unsigned int failures;
#define CHECK(e) do { if (!(e)) { ++failures; \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
} } while (0)

static const V9X_D3D_FORMAT8 *find(const V9X_D3D_FORMAT8 *list,
                                   v9x_u32 count, v9x_u32 format)
{
    v9x_u32 i;

    for (i = 0ul; i < count; ++i) {
        if (list[i].format == format) {
            return &list[i];
        }
    }
    return 0;
}

static void word_tests(void)
{
    /* NEAREST and LINEAR add MINF and MAGF; the mip forms MIPF. The DX5
     * bits stay, as the runtime keeps them at DDI 7: the HD 4600's
     * 0000003F became 0303033F there (V9XD32 T17, boot 111). */
    CHECK(v9x_d3d_caps8_filter(0x00000001ul) == 0x01000101ul);
    CHECK(v9x_d3d_caps8_filter(0x00000002ul) == 0x02000202ul);
    CHECK(v9x_d3d_caps8_filter(0x00000004ul | 0x00000010ul) ==
          0x00010014ul);
    CHECK(v9x_d3d_caps8_filter(0x00000008ul | 0x00000020ul) ==
          0x00020028ul);
    CHECK(v9x_d3d_caps8_filter(0x03030300ul) == 0x03030300ul);
    CHECK(v9x_d3d_caps8_filter(0x0000003ful) == 0x0303033ful);

    /* MIPMAP (DX8's) where any mip filter is reported: the runtime's
     * 00000005 became 00004005 at DDI 7 (boot 111), and without it
     * 3DMark 2001 SE's first mipped texture was D3DERR_INVALIDCALL at
     * DDI 8. */
    CHECK(v9x_d3d_caps8_texture(0x00000005ul, 0x0000003ful) == 0x00004005ul);
    CHECK(v9x_d3d_caps8_texture(0x00000005ul, 0x00020000ul) == 0x00004005ul);
    CHECK(v9x_d3d_caps8_texture(0x00000005ul, 0x00000003ul) == 0x00000005ul);

    /* ROP2, XOR, SUBPIXEL, SUBPIXELX, STIPPLE, the antialias sorts and
     * TRANSLUCENTSORTINDEPENDENT go; DITHER, ZTEST and FOGVERTEX stay. */
    CHECK(v9x_d3d_caps8_raster(0x00080e66ul) == 0ul);
    CHECK(v9x_d3d_caps8_raster(0x00080e66ul | 0x91ul) == 0x91ul);

    /* MASKPLANES and CONFORMANT go; MASKZ and the culls stay. */
    CHECK(v9x_d3d_caps8_misc(0x0000007ful) == 0x00000076ul);
}

static void format_tests(void)
{
    v9x_u32 textures[6];
    V9X_D3D_FORMAT8 list[16];
    const V9X_D3D_FORMAT8 *f;
    v9x_u32 n;

    CHECK(v9x_d3d_caps8_format_of(0x40ul, 16ul, 0xf800ul, 0x07e0ul,
                                  0x001ful, 0ul) == V9X_D3DFMT_R5G6B5);
    CHECK(v9x_d3d_caps8_format_of(0x41ul, 16ul, 0x7c00ul, 0x03e0ul,
                                  0x001ful, 0x8000ul) == V9X_D3DFMT_A1R5G5B5);
    CHECK(v9x_d3d_caps8_format_of(0x40ul, 16ul, 0x7c00ul, 0x03e0ul,
                                  0x001ful, 0ul) == V9X_D3DFMT_X1R5G5B5);
    CHECK(v9x_d3d_caps8_format_of(0x41ul, 16ul, 0x0f00ul, 0x00f0ul,
                                  0x000ful, 0xf000ul) == V9X_D3DFMT_A4R4G4B4);
    CHECK(v9x_d3d_caps8_format_of(0x41ul, 32ul, 0x00ff0000ul, 0x0000ff00ul,
                                  0x000000fful, 0xff000000ul) ==
          V9X_D3DFMT_A8R8G8B8);
    CHECK(v9x_d3d_caps8_format_of(0x40ul, 32ul, 0x00ff0000ul, 0x0000ff00ul,
                                  0x000000fful, 0ul) == V9X_D3DFMT_X8R8G8B8);
    /* BGR order, palettes and alpha flags without a mask have no name. */
    CHECK(v9x_d3d_caps8_format_of(0x40ul, 16ul, 0x001ful, 0x07e0ul,
                                  0xf800ul, 0ul) == 0ul);
    CHECK(v9x_d3d_caps8_format_of(0x20ul, 8ul, 0ul, 0ul, 0ul, 0ul) == 0ul);
    CHECK(v9x_d3d_caps8_format_of(0x41ul, 16ul, 0xf800ul, 0x07e0ul,
                                  0x001ful, 0ul) == 0ul);

    /* The HD 4600's: five textures, 16 and 32 bpp targets, D16 and D24X8. */
    textures[0] = V9X_D3DFMT_R5G6B5;
    textures[1] = V9X_D3DFMT_A1R5G5B5;
    textures[2] = V9X_D3DFMT_A4R4G4B4;
    textures[3] = V9X_D3DFMT_A8R8G8B8;
    textures[4] = V9X_D3DFMT_X8R8G8B8;
    textures[5] = 0ul;
    n = v9x_d3d_caps8_formats(textures, 6ul,
                              V9X_D3D_RENDERS_16 | V9X_D3D_RENDERS_32,
                              16ul, 24ul, list, 16ul);
    CHECK(n == 7ul);
    f = find(list, n, V9X_D3DFMT_R5G6B5);
    CHECK(f != 0 && f->operations ==
          (V9X_D3DFORMAT_OP_TEXTURE | V9X_D3DFORMAT_OP_DISPLAYMODE |
           V9X_D3DFORMAT_OP_3DACCELERATION |
           V9X_D3DFORMAT_OP_SAME_FORMAT_RENDERTARGET));
    f = find(list, n, V9X_D3DFMT_X8R8G8B8);
    CHECK(f != 0 && f->operations ==
          (V9X_D3DFORMAT_OP_TEXTURE | V9X_D3DFORMAT_OP_DISPLAYMODE |
           V9X_D3DFORMAT_OP_3DACCELERATION |
           V9X_D3DFORMAT_OP_SAME_FORMAT_RENDERTARGET));
    f = find(list, n, V9X_D3DFMT_A8R8G8B8);
    CHECK(f != 0 && f->operations == V9X_D3DFORMAT_OP_TEXTURE);
    f = find(list, n, V9X_D3DFMT_A4R4G4B4);
    CHECK(f != 0 && f->operations == V9X_D3DFORMAT_OP_TEXTURE);
    f = find(list, n, V9X_D3DFMT_D16);
    CHECK(f != 0 && f->operations ==
          (V9X_D3DFORMAT_OP_ZSTENCIL |
           V9X_D3DFORMAT_OP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH));
    f = find(list, n, V9X_D3DFMT_D24X8);
    CHECK(f != 0 && f->operations ==
          (V9X_D3DFORMAT_OP_ZSTENCIL |
           V9X_D3DFORMAT_OP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH));

    /* A 16 bpp-only engine with no 32-bit depth: no X8R8G8B8 display
     * format beyond its texture, no D24X8. */
    n = v9x_d3d_caps8_formats(textures, 3ul, V9X_D3D_RENDERS_16, 16ul, 0ul,
                              list, 16ul);
    CHECK(n == 4ul && find(list, n, V9X_D3DFMT_X8R8G8B8) == 0 &&
          find(list, n, V9X_D3DFMT_D24X8) == 0);

    /* The count is whole even when the room is not. */
    memset(list, 0, sizeof(list));
    n = v9x_d3d_caps8_formats(textures, 6ul,
                              V9X_D3D_RENDERS_16 | V9X_D3D_RENDERS_32,
                              16ul, 24ul, list, 2ul);
    CHECK(n == 7ul && list[2].format == 0ul);
}

static void caps_tests(void)
{
    CHECK(v9x_d3d_caps8_lines(1) == 0x1ful);
    CHECK(v9x_d3d_caps8_lines(0) == 0ul);
}

unsigned int v9x_run_d3d_caps8_tests(void)
{
    failures = 0u;
    word_tests();
    format_tests();
    caps_tests();
    return failures;
}
