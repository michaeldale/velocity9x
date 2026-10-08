/*
 * Tests for the Glide TMU address space (src\glide\glide_texmem.c) and
 * texel conversion (src\glide\glide_texfmt.c). Sizes and addresses are
 * the kind NFS II SE used in the census
 * (docs\decisions\2026-10-08-nfs2se-glide-census.md): square single-level
 * textures, 2048 bytes for a 32x32 ARGB1555 level, 256x256 ARGB4444
 * downloads 0x20000 apart.
 */
#include <stdio.h>
#include "../../src/glide/glide_texmem.h"
#include "../../src/glide/glide_texfmt.h"

static unsigned int glide_texture_failures;

#define XCHECK(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, \
               #condition); \
        ++glide_texture_failures; \
    } \
} while (0)

static V9X_GLIDE_TEXMEM mem;

static void info_set(V9X_GLIDE_TEXINFO *info, v9x_u32 small_lod,
                     v9x_u32 large_lod, v9x_u32 aspect, v9x_u32 format)
{
    info->small_lod = small_lod;
    info->large_lod = large_lod;
    info->aspect = aspect;
    info->format = format;
}

static void test_level_sizes(void)
{
    v9x_u32 width = 0ul;
    v9x_u32 height = 0ul;

    XCHECK(v9x_glide_level_size(0ul, V9X_GLIDE_ASPECT_1X1, &width, &height));
    XCHECK(width == 256ul && height == 256ul);
    XCHECK(v9x_glide_level_size(3ul, V9X_GLIDE_ASPECT_8X1, &width, &height));
    XCHECK(width == 32ul && height == 4ul);
    XCHECK(v9x_glide_level_size(0ul, V9X_GLIDE_ASPECT_1X8, &width, &height));
    XCHECK(width == 32ul && height == 256ul);
    /* A 1-texel LOD of an 8x1 texture is still 1x1. */
    XCHECK(v9x_glide_level_size(8ul, V9X_GLIDE_ASPECT_8X1, &width, &height));
    XCHECK(width == 1ul && height == 1ul);
    XCHECK(!v9x_glide_level_size(9ul, V9X_GLIDE_ASPECT_1X1, &width, &height));
    XCHECK(!v9x_glide_level_size(0ul, 7ul, &width, &height));

    XCHECK(v9x_glide_level_bytes(3ul, 3ul, V9X_GLIDE_TEXFMT_ARGB_1555) == 2048ul);
    XCHECK(v9x_glide_level_bytes(0ul, 3ul, V9X_GLIDE_TEXFMT_ARGB_4444) == 131072ul);
    XCHECK(v9x_glide_level_bytes(3ul, 3ul, V9X_GLIDE_TEXFMT_P_8) == 1024ul);
    /* One 8-bit texel still takes a granule. */
    XCHECK(v9x_glide_level_bytes(8ul, 3ul, V9X_GLIDE_TEXFMT_P_8) == 8ul);
}

static void test_required(void)
{
    V9X_GLIDE_TEXINFO info;

    info_set(&info, 8ul, 0ul, 3ul, V9X_GLIDE_TEXFMT_RGB_565);
    XCHECK(v9x_glide_texmem_required(&info, V9X_GLIDE_MIPMAPLEVELMASK_BOTH) ==
           174768ul);
    XCHECK(v9x_glide_texmem_required(&info, V9X_GLIDE_MIPMAPLEVELMASK_EVEN) ==
           139816ul);
    XCHECK(v9x_glide_texmem_required(&info, V9X_GLIDE_MIPMAPLEVELMASK_ODD) ==
           34952ul);
    /* The census's call: lodmin 3, lodmax 3, ARGB1555. */
    info_set(&info, 3ul, 3ul, 3ul, V9X_GLIDE_TEXFMT_ARGB_1555);
    XCHECK(v9x_glide_texmem_required(&info, V9X_GLIDE_MIPMAPLEVELMASK_BOTH) ==
           2048ul);
    /* Order taken from the values: swapped LODs give the same answer. */
    info_set(&info, 0ul, 8ul, 3ul, V9X_GLIDE_TEXFMT_RGB_565);
    XCHECK(v9x_glide_texmem_required(&info, V9X_GLIDE_MIPMAPLEVELMASK_BOTH) ==
           174768ul);
    info_set(&info, 3ul, 3ul, 9ul, V9X_GLIDE_TEXFMT_RGB_565);
    XCHECK(v9x_glide_texmem_required(&info, V9X_GLIDE_MIPMAPLEVELMASK_BOTH) ==
           0ul);
}

static void test_records(void)
{
    V9X_GLIDE_TEXINFO small;
    V9X_GLIDE_TEXINFO other;
    int a;
    int b;
    int c;
    v9x_u32 serial;

    v9x_glide_texmem_init(&mem);
    info_set(&small, 3ul, 3ul, 3ul, V9X_GLIDE_TEXFMT_ARGB_1555);  /* 2048 */
    info_set(&other, 3ul, 3ul, 3ul, V9X_GLIDE_TEXFMT_P_8);        /* 1024 */

    a = v9x_glide_texmem_download(&mem, 0x0ul, 3ul, &small);
    XCHECK(a >= 0);
    XCHECK(v9x_glide_texmem_find(&mem, 0x0ul, 3ul, &small) == a);
    /* Same address, different format: not the texture that was loaded. */
    XCHECK(v9x_glide_texmem_find(&mem, 0x0ul, 3ul, &other) == -1);
    XCHECK(v9x_glide_texmem_find(&mem, 0x800ul, 3ul, &small) == -1);

    /* A second texture beside the first leaves it alone. */
    b = v9x_glide_texmem_download(&mem, 0x800ul, 3ul, &other);
    XCHECK(b >= 0 && b != a);
    XCHECK(v9x_glide_texmem_find(&mem, 0x0ul, 3ul, &small) == a);
    XCHECK(v9x_glide_texmem_find(&mem, 0x800ul, 3ul, &other) == b);

    /* One overlapping both replaces both. */
    c = v9x_glide_texmem_download(&mem, 0x400ul, 3ul, &small);
    XCHECK(c >= 0);
    XCHECK(v9x_glide_texmem_find(&mem, 0x0ul, 3ul, &small) == -1);
    XCHECK(v9x_glide_texmem_find(&mem, 0x800ul, 3ul, &other) == -1);
    XCHECK(v9x_glide_texmem_find(&mem, 0x400ul, 3ul, &small) == c);

    /* Downloading again to the same place is a new serial. */
    serial = mem.records[c].serial;
    c = v9x_glide_texmem_download(&mem, 0x400ul, 3ul, &small);
    XCHECK(c >= 0 && mem.records[c].serial != serial);
    XCHECK(v9x_glide_texmem_find(&mem, 0x400ul, 3ul, &small) == c);

    info_set(&other, 3ul, 3ul, 3ul, 99ul);
    XCHECK(v9x_glide_texmem_download(&mem, 0x4000ul, 3ul, &other) == -1);
}

static void test_records_full(void)
{
    V9X_GLIDE_TEXINFO tiny;
    unsigned int i;
    int first;
    int last = -1;

    /* 8-byte textures side by side until the table is full; the next
     * evicts the oldest. */
    v9x_glide_texmem_init(&mem);
    info_set(&tiny, 7ul, 7ul, 3ul, V9X_GLIDE_TEXFMT_ARGB_1555);    /* 2x2 */
    first = v9x_glide_texmem_download(&mem, 0x0ul, 3ul, &tiny);
    for (i = 1u; i < V9X_GLIDE_TEXMEM_RECORDS; ++i) {
        last = v9x_glide_texmem_download(&mem, i * 8ul, 3ul, &tiny);
    }
    XCHECK(first >= 0 && last >= 0);
    XCHECK(v9x_glide_texmem_find(&mem, 0x0ul, 3ul, &tiny) == first);
    XCHECK(v9x_glide_texmem_download(&mem, 0x10000ul, 3ul, &tiny) >= 0);
    XCHECK(v9x_glide_texmem_find(&mem, 0x0ul, 3ul, &tiny) == -1);
    XCHECK(v9x_glide_texmem_find(&mem, 8ul, 3ul, &tiny) >= 0);
}

static void test_formats(void)
{
    v9x_u16 src16[4];
    v9x_u8 src8[3];
    v9x_u16 dst[4];
    v9x_u32 palette[V9X_GLIDE_PALETTE_ENTRIES];
    v9x_u32 format = 0ul;
    unsigned int i;

    XCHECK(v9x_glide_texfmt_supported(V9X_GLIDE_TEXFMT_RGB_565));
    XCHECK(v9x_glide_texfmt_supported(V9X_GLIDE_TEXFMT_ARGB_1555));
    XCHECK(v9x_glide_texfmt_supported(V9X_GLIDE_TEXFMT_ARGB_4444));
    XCHECK(v9x_glide_texfmt_supported(V9X_GLIDE_TEXFMT_P_8));
    XCHECK(!v9x_glide_texfmt_supported(V9X_GLIDE_TEXFMT_RGB_332));
    XCHECK(!v9x_glide_texfmt_supported(V9X_GLIDE_TEXFMT_ALPHA_INTENSITY_88));

    /* 565 copies; with a key it becomes 1555 and keyed texels lose alpha. */
    src16[0] = 0x0000u; src16[1] = 0xFFFFu; src16[2] = 0x07E0u; src16[3] = 0xF800u;
    XCHECK(v9x_glide_texfmt_convert(V9X_GLIDE_TEXFMT_RGB_565, src16, 4ul, 0,
                                    0ul, 0ul, dst, &format));
    XCHECK(format == V9X_R3D_ABI_FORMAT_RGB565);
    XCHECK(dst[0] == 0x0000u && dst[1] == 0xFFFFu && dst[2] == 0x07E0u &&
           dst[3] == 0xF800u);
    XCHECK(v9x_glide_texfmt_convert(V9X_GLIDE_TEXFMT_RGB_565, src16, 4ul, 0,
                                    1ul, 0x0000FF00ul, dst, &format));
    XCHECK(format == V9X_R3D_ABI_FORMAT_ARGB1555);
    XCHECK(dst[0] == 0x8000u);      /* black, kept */
    XCHECK(dst[1] == 0xFFFFu);
    XCHECK(dst[2] == 0x03E0u);      /* pure green, the census's key: alpha 0 */
    XCHECK(dst[3] == 0xFC00u);

    /* 1555 copies; the key clears alpha on a match. */
    src16[0] = 0x8000u; src16[1] = 0x7FFFu;
    XCHECK(v9x_glide_texfmt_convert(V9X_GLIDE_TEXFMT_ARGB_1555, src16, 2ul, 0,
                                    0ul, 0ul, dst, &format));
    XCHECK(format == V9X_R3D_ABI_FORMAT_ARGB1555);
    XCHECK(dst[0] == 0x8000u && dst[1] == 0x7FFFu);
    XCHECK(v9x_glide_texfmt_convert(V9X_GLIDE_TEXFMT_ARGB_1555, src16, 2ul, 0,
                                    1ul, 0x00000000ul, dst, &format));
    XCHECK(dst[0] == 0x0000u && dst[1] == 0x7FFFu);

    /* 4444 likewise. */
    src16[0] = 0xF000u; src16[1] = 0xFFFFu;
    XCHECK(v9x_glide_texfmt_convert(V9X_GLIDE_TEXFMT_ARGB_4444, src16, 2ul, 0,
                                    1ul, 0x00000000ul, dst, &format));
    XCHECK(format == V9X_R3D_ABI_FORMAT_ARGB4444);
    XCHECK(dst[0] == 0x0000u && dst[1] == 0xFFFFu);

    /* P_8 through the palette: 565 without a key, 1555 with one. */
    for (i = 0u; i < V9X_GLIDE_PALETTE_ENTRIES; ++i) {
        palette[i] = 0xFF000000ul;
    }
    palette[1] = 0xFFFF0000ul;
    palette[2] = 0xFF00FF00ul;
    src8[0] = 0u; src8[1] = 1u; src8[2] = 2u;
    XCHECK(v9x_glide_texfmt_convert(V9X_GLIDE_TEXFMT_P_8, src8, 3ul, palette,
                                    0ul, 0ul, dst, &format));
    XCHECK(format == V9X_R3D_ABI_FORMAT_RGB565);
    XCHECK(dst[0] == 0x0000u && dst[1] == 0xF800u && dst[2] == 0x07E0u);
    XCHECK(v9x_glide_texfmt_convert(V9X_GLIDE_TEXFMT_P_8, src8, 3ul, palette,
                                    1ul, 0x0000FF00ul, dst, &format));
    XCHECK(format == V9X_R3D_ABI_FORMAT_ARGB1555);
    XCHECK(dst[0] == 0x8000u && dst[1] == 0xFC00u && dst[2] == 0x03E0u);
    XCHECK(!v9x_glide_texfmt_convert(V9X_GLIDE_TEXFMT_P_8, src8, 3ul, 0,
                                     0ul, 0ul, dst, &format));
    XCHECK(!v9x_glide_texfmt_convert(V9X_GLIDE_TEXFMT_RGB_332, src8, 3ul, 0,
                                     0ul, 0ul, dst, &format));
}

unsigned int v9x_run_glide_texture_tests(void)
{
    glide_texture_failures = 0u;
    test_level_sizes();
    test_required();
    test_records();
    test_records_full();
    test_formats();
    return glide_texture_failures;
}
