#include <stdio.h>

#include "velocity9x/intel_gma.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

/* The netbook's shape: 8 MiB of stolen memory less the 1 MiB reserve, and a
 * 640x480x16 primary at offset 0 with its back buffer directly above it. */
#define TEST_VRAM      0x006b0000ul
#define TEST_PITCH     1280ul
#define TEST_BACK      0x00096000ul
#define TEST_CRUMB     (TEST_VRAM + 0x00010000ul + 0x80ul)

static void test_blt_defaults(struct v9x_i9xx_blt *blt)
{
    blt->bytes_per_pixel = 2ul;
    blt->vram_bytes = TEST_VRAM;
    blt->destination_base = 0ul;
    blt->destination_pitch = TEST_PITCH;
    blt->left = 0ul;
    blt->top = 0ul;
    blt->right = 640ul;
    blt->bottom = 480ul;
    blt->source_base = TEST_BACK;
    blt->source_pitch = TEST_PITCH;
    blt->source_left = 0ul;
    blt->source_top = 0ul;
    blt->color = 0ul;
}

/* Seal a built stream as d3d_i9xx.c does: breadcrumb, then pad even. */
static v9x_u32 test_seal(v9x_u32 *stream, v9x_u32 at, v9x_u32 crumb)
{
    v9x_u32 produced = 0ul;

    if (crumb != 0ul) {
        CHECK(v9x_i9xx_build_breadcrumb_stream(
                  crumb, 0x1234ul, stream + at,
                  V9X_I9XX_BLT_STREAM_DWORDS - at, &produced) ==
              V9X_STATUS_OK);
        at += produced;
    }
    if ((at & 1ul) != 0ul) {
        stream[at++] = V9X_I9XX_MI_NOOP;
    }
    return at;
}

static void test_fill_golden(void)
{
    struct v9x_i9xx_blt blt;
    v9x_u32 stream[V9X_I9XX_BLT_STREAM_DWORDS];
    v9x_u32 written = 0ul;
    v9x_u32 rejected = 99ul;
    v9x_u32 at;

    test_blt_defaults(&blt);
    blt.left = 8ul;
    blt.top = 4ul;
    blt.right = 100ul;
    blt.bottom = 50ul;
    blt.color = 0xabcdf81ful;
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_OK);
    CHECK(written == 7ul);
    CHECK(stream[0] == 0x54000004ul);
    /* RGB565, PATCOPY, pitch 1280. */
    CHECK(stream[1] == 0x01f00500ul);
    CHECK(stream[2] == ((4ul << 16) | 8ul));
    CHECK(stream[3] == ((50ul << 16) | 100ul));
    CHECK(stream[4] == 0ul);
    /* The colour trimmed to the pixel. */
    CHECK(stream[5] == 0x0000f81ful);
    CHECK(stream[6] == V9X_I9XX_MI_FLUSH);

    at = test_seal(stream, written, TEST_CRUMB);
    CHECK(at == 16ul);
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, TEST_CRUMB,
                                     &rejected) == V9X_TRUE);

    /* 8 bpp: depth code zero, colour to a byte. */
    blt.bytes_per_pixel = 1ul;
    blt.destination_pitch = 1024ul;
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_OK);
    CHECK(stream[1] == 0x00f00400ul);
    CHECK(stream[5] == 0x0000001ful);
    at = test_seal(stream, written, 0ul);
    CHECK(at == 8ul);
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 1ul, TEST_VRAM, 0ul,
                                     &rejected) == V9X_TRUE);
    /* The same stream is not a 16-bpp one. */
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, 0ul,
                                     &rejected) == V9X_FALSE);
    CHECK(rejected == 1ul);
}

static void test_copy_golden(void)
{
    struct v9x_i9xx_blt blt;
    v9x_u32 stream[V9X_I9XX_BLT_STREAM_DWORDS];
    v9x_u32 written = 0ul;
    v9x_u32 rejected = 99ul;
    v9x_u32 at;

    /* Back buffer to primary, the whole screen: the present Half-Life makes
     * every frame (V9XSNA7.INI, 2026-09-25). */
    test_blt_defaults(&blt);
    CHECK(v9x_i9xx_build_copy_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_OK);
    CHECK(written == 9ul);
    CHECK(stream[0] == 0x54c00006ul);
    CHECK(stream[1] == 0x01cc0500ul);
    CHECK(stream[2] == 0ul);
    CHECK(stream[3] == ((480ul << 16) | 640ul));
    CHECK(stream[4] == 0ul);
    CHECK(stream[5] == 0ul);
    CHECK(stream[6] == TEST_PITCH);
    CHECK(stream[7] == TEST_BACK);
    CHECK(stream[8] == V9X_I9XX_MI_FLUSH);

    at = test_seal(stream, written, TEST_CRUMB);
    CHECK(at == 18ul);
    CHECK(at == V9X_I9XX_BLT_STREAM_DWORDS);
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, TEST_CRUMB,
                                     &rejected) == V9X_TRUE);

    /* A sub-rectangle between two surfaces of different pitch. */
    test_blt_defaults(&blt);
    blt.source_base = 0x00200000ul;
    blt.source_pitch = 256ul;
    blt.source_left = 16ul;
    blt.source_top = 32ul;
    blt.left = 300ul;
    blt.top = 200ul;
    blt.right = 364ul;
    blt.bottom = 264ul;
    CHECK(v9x_i9xx_build_copy_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_OK);
    CHECK(stream[5] == ((32ul << 16) | 16ul));
    CHECK(stream[6] == 256ul);
    at = test_seal(stream, written, 0ul);
    CHECK(at == 10ul);
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, 0ul,
                                     &rejected) == V9X_TRUE);
}

static void test_builder_refusals(void)
{
    struct v9x_i9xx_blt blt;
    v9x_u32 stream[V9X_I9XX_BLT_STREAM_DWORDS];
    v9x_u32 written = 99ul;

    test_blt_defaults(&blt);
    CHECK(v9x_i9xx_build_fill_blt(0, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, 6ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_build_copy_blt(&blt, stream, 8ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);

    /* 32 bpp and 24 bpp decline (plan decision 2). */
    blt.bytes_per_pixel = 4ul;
    blt.destination_pitch = 2560ul;
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_INVALID_ARGUMENT);
    blt.bytes_per_pixel = 3ul;
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_INVALID_ARGUMENT);

    /* Empty and inverted rectangles. */
    test_blt_defaults(&blt);
    blt.right = 0ul;
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_INVALID_ARGUMENT);
    test_blt_defaults(&blt);
    blt.top = 480ul;
    CHECK(v9x_i9xx_build_copy_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_INVALID_ARGUMENT);

    /* A row wider than the pitch. */
    test_blt_defaults(&blt);
    blt.right = 641ul;
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_INVALID_ARGUMENT);

    /* Pitch and base alignment, and the 16-bit pitch field. */
    test_blt_defaults(&blt);
    blt.destination_pitch = 1282ul;
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_INVALID_ARGUMENT);
    test_blt_defaults(&blt);
    blt.destination_base = 2ul;
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_INVALID_ARGUMENT);
    test_blt_defaults(&blt);
    blt.destination_pitch = 32768ul;
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_INVALID_ARGUMENT);
    test_blt_defaults(&blt);
    blt.source_pitch = 0ul;
    CHECK(v9x_i9xx_build_copy_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_INVALID_ARGUMENT);

    /* Address zero is the primary and is accepted (plan decision 3); the
     * top of video memory is the reserve and is not (decision 4). The last
     * byte may be the last byte of the heap, and one more is refused. */
    test_blt_defaults(&blt);
    blt.destination_base = TEST_VRAM - 480ul * TEST_PITCH;
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_OK);
    blt.destination_base += 4ul;
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_INSUFFICIENT_MEMORY);
    blt.destination_base = TEST_VRAM;
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_INSUFFICIENT_MEMORY);
    test_blt_defaults(&blt);
    blt.source_base = TEST_VRAM - 479ul * TEST_PITCH;
    CHECK(v9x_i9xx_build_copy_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_INSUFFICIENT_MEMORY);
}

static void test_copy_overlap(void)
{
    struct v9x_i9xx_blt blt;
    v9x_u32 stream[V9X_I9XX_BLT_STREAM_DWORDS];
    v9x_u32 written = 0ul;

    /* Within one surface, intersecting: a scroll by one line. Refused. */
    test_blt_defaults(&blt);
    blt.source_base = 0ul;
    blt.source_top = 1ul;
    blt.bottom = 479ul;
    CHECK(v9x_i9xx_build_copy_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_UNSUPPORTED);
    CHECK(written == 0ul);

    /* Within one surface, disjoint but on the same rows: no pixel is both
     * read and written, so any order is right. Accepted. */
    test_blt_defaults(&blt);
    blt.source_base = 0ul;
    blt.left = 320ul;
    blt.right = 640ul;
    CHECK(v9x_i9xx_build_copy_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_OK);

    /* Edge-adjacent is disjoint. */
    test_blt_defaults(&blt);
    blt.source_base = 0ul;
    blt.bottom = 240ul;
    blt.source_top = 240ul;
    CHECK(v9x_i9xx_build_copy_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_OK);

    /* Two surfaces whose bytes interleave are aliases: refused even though
     * their rectangles are not comparable. */
    test_blt_defaults(&blt);
    blt.source_base = 640ul;
    blt.source_pitch = 2560ul;
    blt.bottom = 100ul;
    blt.right = 320ul;
    CHECK(v9x_i9xx_build_copy_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_UNSUPPORTED);
}

static void test_decoder_refusals(void)
{
    struct v9x_i9xx_blt blt;
    v9x_u32 stream[V9X_I9XX_BLT_STREAM_DWORDS];
    v9x_u32 good[V9X_I9XX_BLT_STREAM_DWORDS];
    v9x_u32 written = 0ul;
    v9x_u32 rejected = 0ul;
    v9x_u32 at;
    v9x_u32 index;

    test_blt_defaults(&blt);
    CHECK(v9x_i9xx_build_copy_blt(&blt, good, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_OK);
    at = test_seal(good, written, TEST_CRUMB);

#define TEST_RESET() do { \
    for (index = 0ul; index < at; ++index) { stream[index] = good[index]; } \
} while (0)

    TEST_RESET();
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, TEST_CRUMB,
                                     &rejected) == V9X_TRUE);

    /* Odd length, empty, and a depth the path does not serve. */
    CHECK(v9x_i9xx_decode_blt_stream(stream, at - 1ul, 2ul, TEST_VRAM,
                                     TEST_CRUMB, &rejected) == V9X_FALSE);
    CHECK(v9x_i9xx_decode_blt_stream(stream, 0ul, 2ul, TEST_VRAM,
                                     TEST_CRUMB, &rejected) == V9X_FALSE);
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 4ul, TEST_VRAM,
                                     TEST_CRUMB, &rejected) == V9X_FALSE);

    /* A named breadcrumb is required, and an unnamed one refused. */
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM,
                                     TEST_CRUMB + 4ul, &rejected) ==
          V9X_FALSE);
    CHECK(rejected == 9ul);
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, 0ul,
                                     &rejected) == V9X_FALSE);
    CHECK(rejected == 9ul);
    stream[9] = V9X_I9XX_MI_NOOP;
    CHECK(v9x_i9xx_decode_blt_stream(stream, 10ul, 2ul, TEST_VRAM,
                                     TEST_CRUMB, &rejected) == V9X_FALSE);
    CHECK(rejected == 10ul);
    CHECK(v9x_i9xx_decode_blt_stream(stream, 10ul, 2ul, TEST_VRAM, 0ul,
                                     &rejected) == V9X_TRUE);

    /* The write-enable bits of the 32-bpp form are not accepted here. */
    TEST_RESET();
    stream[0] = V9X_I9XX_XY_SRC_COPY_BLT | 0x00300000ul;
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, TEST_CRUMB,
                                     &rejected) == V9X_FALSE);
    CHECK(rejected == 0ul);

    /* Another ROP. */
    TEST_RESET();
    stream[1] = (stream[1] & 0xff00fffful) | 0x00660000ul;
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, TEST_CRUMB,
                                     &rejected) == V9X_FALSE);
    CHECK(rejected == 1ul);

    /* A destination moved into the reserve. */
    TEST_RESET();
    stream[4] = TEST_VRAM - 0x1000ul;
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, TEST_CRUMB,
                                     &rejected) == V9X_FALSE);
    CHECK(rejected == 1ul);

    /* A source moved into the reserve. */
    TEST_RESET();
    stream[7] = TEST_VRAM - 0x1000ul;
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, TEST_CRUMB,
                                     &rejected) == V9X_FALSE);
    CHECK(rejected == 5ul);

    /* A source moved onto the destination. */
    TEST_RESET();
    stream[5] = 1ul << 16;
    stream[7] = 0ul;
    stream[3] = (479ul << 16) | 640ul;
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, TEST_CRUMB,
                                     &rejected) == V9X_FALSE);
    CHECK(rejected == 7ul);

    /* The flush behind the blit is required. */
    TEST_RESET();
    stream[8] = V9X_I9XX_MI_NOOP;
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, TEST_CRUMB,
                                     &rejected) == V9X_FALSE);
    CHECK(rejected == 8ul);

    /* Nothing is drawn after the breadcrumb. */
    TEST_RESET();
    stream[17] = V9X_I9XX_MI_FLUSH;
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, TEST_CRUMB,
                                     &rejected) == V9X_FALSE);
    CHECK(rejected == 17ul);

    /* A breadcrumb of another shape. */
    TEST_RESET();
    stream[12] = (2ul << 16) | 1ul;
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, TEST_CRUMB,
                                     &rejected) == V9X_FALSE);
    CHECK(rejected == 9ul);

    /* An unknown command. */
    TEST_RESET();
    stream[0] = V9X_I9XX_MI_DISPLAY_FLIP_I915;
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, TEST_CRUMB,
                                     &rejected) == V9X_FALSE);
    CHECK(rejected == 0ul);

    /* A fill colour carrying bits above the pixel. */
    test_blt_defaults(&blt);
    CHECK(v9x_i9xx_build_fill_blt(&blt, stream, V9X_I9XX_BLT_STREAM_DWORDS,
                                  &written) == V9X_STATUS_OK);
    at = test_seal(stream, written, 0ul);
    stream[5] = 0x00010000ul;
    CHECK(v9x_i9xx_decode_blt_stream(stream, at, 2ul, TEST_VRAM, 0ul,
                                     &rejected) == V9X_FALSE);
    CHECK(rejected == 5ul);

    /* A fill truncated before its flush. */
    CHECK(v9x_i9xx_decode_blt_stream(stream, 6ul, 2ul, TEST_VRAM, 0ul,
                                     &rejected) == V9X_FALSE);
    CHECK(rejected == 0ul);
#undef TEST_RESET
}

unsigned int v9x_run_i9xx_blt_tests(void)
{
    test_fill_golden();
    test_copy_golden();
    test_builder_refusals();
    test_copy_overlap();
    test_decoder_refusals();
    return failures;
}
