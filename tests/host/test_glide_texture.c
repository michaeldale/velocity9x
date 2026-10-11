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

/* The TMU memory itself, as large as the model holds. Static, so zero. */
static v9x_u8 tmu[V9X_GLIDE_TEXMEM_PAGES * V9X_GLIDE_TEXMEM_PAGE_BYTES];
static v9x_u8 data[131072ul];

static void fill(v9x_u8 *bytes, v9x_u32 count, v9x_u8 first)
{
    v9x_u32 i;

    for (i = 0ul; i < count; ++i) {
        bytes[i] = (v9x_u8)(first + i);
    }
}

/*
 * A download writes bytes and a source names bytes. The first source of a
 * texture is NEW, a second with nothing written since is CURRENT, and one
 * after a write anywhere in its span is STALE.
 */
static void test_source_states(void)
{
    V9X_GLIDE_TEXINFO small;
    V9X_GLIDE_TEXINFO other;
    v9x_u32 state = 99ul;
    int a;
    int b;

    v9x_glide_texmem_init(&mem, tmu, sizeof(tmu));
    info_set(&small, 3ul, 3ul, 3ul, V9X_GLIDE_TEXFMT_ARGB_1555);  /* 2048 */
    info_set(&other, 3ul, 3ul, 3ul, V9X_GLIDE_TEXFMT_P_8);        /* 1024 */

    fill(data, 2048ul, 1u);
    XCHECK(v9x_glide_texmem_download(&mem, 0x0ul, 3ul, &small, data) == 2048ul);
    XCHECK(tmu[0] == 1u && tmu[2047] == (v9x_u8)(1u + 2047u));
    a = v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &small, &state);
    XCHECK(a >= 0 && state == V9X_GLIDE_TEXMEM_NEW);
    XCHECK(v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &small, &state) == a);
    XCHECK(state == V9X_GLIDE_TEXMEM_CURRENT);

    /* The same bytes in another format: another entry, decoded its way. */
    b = v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &other, &state);
    XCHECK(b >= 0 && b != a && state == V9X_GLIDE_TEXMEM_NEW);

    /* A write beside it leaves it current; one inside its span does not. */
    XCHECK(v9x_glide_texmem_download(&mem, 0x1000ul, 3ul, &other, data) ==
           1024ul);
    XCHECK(v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &small, &state) == a);
    XCHECK(state == V9X_GLIDE_TEXMEM_CURRENT);
    XCHECK(v9x_glide_texmem_download(&mem, 0x400ul, 3ul, &other, data) ==
           1024ul);
    XCHECK(v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &small, &state) == a);
    XCHECK(state == V9X_GLIDE_TEXMEM_STALE);
    XCHECK(v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &small, &state) == a);
    XCHECK(state == V9X_GLIDE_TEXMEM_CURRENT);

    /* Invalid info, or a span past the end of memory, is no texture. */
    info_set(&other, 3ul, 3ul, 3ul, 99ul);
    XCHECK(v9x_glide_texmem_download(&mem, 0x0ul, 3ul, &other, data) == 0ul);
    XCHECK(v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &other, &state) == -1);
    XCHECK(v9x_glide_texmem_source(&mem, sizeof(tmu) - 1024ul, 3ul, &small,
                                   &state) == -1);
}

/*
 * Carmageddon II's menu (netbook, 2026-10-11): a 64x64 ARGB4444 texture,
 * a 64x64 RGB565 one, a 4x4 and an 8x8 glyph, all downloaded to address 0,
 * and earlier ones sourced again without downloading them. Each source
 * finds a texture, made from the bytes that are there now, as on a Voodoo.
 */
static void test_shared_address(void)
{
    V9X_GLIDE_TEXINFO big;
    V9X_GLIDE_TEXINFO picture;
    V9X_GLIDE_TEXINFO glyph4;
    V9X_GLIDE_TEXINFO glyph8;
    v9x_u32 state = 0ul;
    int i;

    v9x_glide_texmem_init(&mem, tmu, sizeof(tmu));
    info_set(&big, 2ul, 2ul, 3ul, V9X_GLIDE_TEXFMT_ARGB_4444);      /* 8192 */
    info_set(&picture, 2ul, 2ul, 3ul, V9X_GLIDE_TEXFMT_RGB_565);    /* 8192 */
    info_set(&glyph4, 6ul, 6ul, 3ul, V9X_GLIDE_TEXFMT_ARGB_4444);   /* 32 */
    info_set(&glyph8, 5ul, 5ul, 3ul, V9X_GLIDE_TEXFMT_ARGB_4444);   /* 128 */

    fill(data, 8192ul, 10u);
    XCHECK(v9x_glide_texmem_download(&mem, 0x0ul, 3ul, &big, data) == 8192ul);
    XCHECK(v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &big, &state) >= 0);
    fill(data, 8192ul, 20u);
    XCHECK(v9x_glide_texmem_download(&mem, 0x0ul, 3ul, &picture, data) ==
           8192ul);
    fill(data, 32ul, 30u);
    XCHECK(v9x_glide_texmem_download(&mem, 0x0ul, 3ul, &glyph4, data) == 32ul);
    fill(data, 128ul, 40u);
    XCHECK(v9x_glide_texmem_download(&mem, 0x0ul, 3ul, &glyph8, data) == 128ul);

    /* Every one of them is still a texture, and the overwritten ones are
     * stale: the caller re-reads the bytes that are there. */
    i = v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &big, &state);
    XCHECK(i >= 0 && state == V9X_GLIDE_TEXMEM_STALE);
    XCHECK(v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &glyph4, &state) >= 0);
    XCHECK(state == V9X_GLIDE_TEXMEM_NEW);
    XCHECK(v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &picture, &state) >= 0);
    /* The bytes are the 8x8 glyph's first, then the picture's. */
    XCHECK(tmu[0] == 40u && tmu[127] == (v9x_u8)(40u + 127u));
    XCHECK(tmu[128] == (v9x_u8)(20u + 128u));
}

/*
 * Levels land at Glide's offsets: each selected level after the last,
 * rounded to the granule, read from the caller's data where all levels
 * lie end to end. Only the even levels of a 4x4..1x1 8-bit chain: 4x4
 * (16 bytes) at 0, 1x1 (1 byte, a granule) at 16; the 2x2 in between in
 * the data is skipped.
 */
static void test_download_levels(void)
{
    V9X_GLIDE_TEXINFO chain;

    v9x_glide_texmem_init(&mem, tmu, sizeof(tmu));
    info_set(&chain, 8ul, 6ul, 3ul, V9X_GLIDE_TEXFMT_P_8);
    fill(data, 21ul, 1u);   /* 16 + 4 + 1 */
    XCHECK(v9x_glide_texmem_download(&mem, 0x100ul, V9X_GLIDE_MIPMAPLEVELMASK_EVEN,
                                     &chain, data) == 24ul);
    XCHECK(tmu[0x100] == 1u && tmu[0x10F] == 16u);
    XCHECK(tmu[0x110] == 21u);
    /* A download that runs past the end is clipped, not wrapped. */
    info_set(&chain, 2ul, 2ul, 3ul, V9X_GLIDE_TEXFMT_RGB_565);
    fill(data, 8192ul, 7u);
    XCHECK(v9x_glide_texmem_download(&mem, sizeof(tmu) - 16ul, 3ul, &chain,
                                     data) == 16ul);
    XCHECK(tmu[0] == 0u);
}

/* The cache is full: the least recently sourced entry goes, not one in
 * use. 4096 entries side by side, then one more. */
static void test_cache_full(void)
{
    V9X_GLIDE_TEXINFO tiny;
    v9x_u32 state = 0ul;
    unsigned int i;
    int first;

    v9x_glide_texmem_init(&mem, tmu, sizeof(tmu));
    info_set(&tiny, 7ul, 7ul, 3ul, V9X_GLIDE_TEXFMT_ARGB_1555);    /* 2x2 */
    first = v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &tiny, &state);
    for (i = 1u; i < V9X_GLIDE_TEXMEM_RECORDS; ++i) {
        (void)v9x_glide_texmem_source(&mem, i * 8ul, 3ul, &tiny, &state);
    }
    /* Touch the first again so the second is now the oldest. */
    XCHECK(v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &tiny, &state) == first);
    XCHECK(state == V9X_GLIDE_TEXMEM_CURRENT);
    XCHECK(v9x_glide_texmem_source(&mem, 0x10000ul, 3ul, &tiny, &state) >= 0);
    XCHECK(state == V9X_GLIDE_TEXMEM_NEW);
    XCHECK(v9x_glide_texmem_source(&mem, 0x0ul, 3ul, &tiny, &state) == first);
    XCHECK(state == V9X_GLIDE_TEXMEM_CURRENT);
    XCHECK(v9x_glide_texmem_source(&mem, 8ul, 3ul, &tiny, &state) >= 0);
    XCHECK(state == V9X_GLIDE_TEXMEM_NEW);
}

/* NFS II SE's pattern: 1,024 32x32 ARGB1555 textures in their own slots,
 * each downloaded once and sourced many times, never re-read. */
static void test_distinct_slots(void)
{
    V9X_GLIDE_TEXINFO tile;
    v9x_u32 state = 0ul;
    unsigned int i;
    unsigned int round;
    unsigned int refills = 0u;

    v9x_glide_texmem_init(&mem, tmu, sizeof(tmu));
    info_set(&tile, 3ul, 3ul, 3ul, V9X_GLIDE_TEXFMT_ARGB_1555);    /* 2048 */
    fill(data, 2048ul, 3u);
    for (i = 0u; i < 1024u; ++i) {
        (void)v9x_glide_texmem_download(&mem, i * 2048ul, 3ul, &tile, data);
    }
    for (round = 0u; round < 3u; ++round) {
        for (i = 0u; i < 1024u; ++i) {
            if (v9x_glide_texmem_source(&mem, i * 2048ul, 3ul, &tile, &state) < 0 ||
                (round != 0u && state != V9X_GLIDE_TEXMEM_CURRENT)) {
                ++refills;
            }
        }
    }
    XCHECK(refills == 0u);
}

/*
 * The memory spans the larger of the two DLLs' reports: GLIDE3X.DLL answers
 * GR_MEMORY_TMU with 4 MiB, and Diablo II places textures above 2 MiB
 * (0x2EC000, A8U4I5, 2026-10-11). With 2 MiB those sources were refused
 * and the town drew without its sprites.
 */
static void test_glide3_range(void)
{
    V9X_GLIDE_TEXINFO tile;
    v9x_u32 state = 99ul;
    int a;

    v9x_glide_texmem_init(&mem, tmu, sizeof(tmu));
    info_set(&tile, 3ul, 3ul, 3ul, V9X_GLIDE_TEXFMT_ARGB_1555);    /* 2048 */
    fill(data, 2048ul, 7u);
    XCHECK(v9x_glide_texmem_download(&mem, 0x2EC000ul, 3ul, &tile, data) ==
           2048ul);
    a = v9x_glide_texmem_source(&mem, 0x2EC000ul, 3ul, &tile, &state);
    XCHECK(a >= 0 && state == V9X_GLIDE_TEXMEM_NEW);
    XCHECK(tmu[0x2EC000ul] == 7u);
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

    /* Keyed, 565 and P_8 come out with alpha from the key alone; 1555 and
     * 4444 keep their own alpha beside it. NFS II SE's map pane, an
     * alpha format keyed under a vertex-alpha combine, vanished when its
     * texture alpha was put in charge (netbook, 2026-10-10). */
    XCHECK(v9x_glide_texfmt_key_alpha_only(V9X_GLIDE_TEXFMT_RGB_565));
    XCHECK(v9x_glide_texfmt_key_alpha_only(V9X_GLIDE_TEXFMT_P_8));
    XCHECK(!v9x_glide_texfmt_key_alpha_only(V9X_GLIDE_TEXFMT_ARGB_1555));
    XCHECK(!v9x_glide_texfmt_key_alpha_only(V9X_GLIDE_TEXFMT_ARGB_4444));
    XCHECK(!v9x_glide_texfmt_key_alpha_only(V9X_GLIDE_TEXFMT_RGB_332));

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
    test_source_states();
    test_shared_address();
    test_download_levels();
    test_cache_full();
    test_distinct_slots();
    test_glide3_range();
    test_formats();
    return glide_texture_failures;
}
